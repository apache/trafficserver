/** @file

  Implementation of Traffic Dump transaction data.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <sstream>

#include "transaction_data.h"
#include "global_variables.h"
#include "json_utils.h"
#include "session_data.h"

namespace traffic_dump
{
int TransactionData::transaction_arg_index = 0;
sensitive_fields_t TransactionData::sensitive_fields;

std::string default_sensitive_field_value;

/// Fields considered sensitive because they may contain user-private
/// information. These fields are replaced with auto-generated generic content by
/// default. To override this behavior, the user should specify their own fields
/// they consider sensitive with --sensitive-fields.
///
/// While these are specified with case, they are matched case-insensitively.
sensitive_fields_t default_sensitive_fields = {
  "Set-Cookie",
  "Cookie",
};

void
TransactionData::initialize_default_sensitive_field()
{
  // 128 KB is the maximum size supported for all headers, so this size should
  // be plenty large for our needs.
  constexpr size_t default_field_size = 128 * 1024;
  default_sensitive_field_value.resize(default_field_size);

  char *field_buffer = default_sensitive_field_value.data();
  for (auto i = 0u; i < default_field_size; i += 8) {
    sprintf(field_buffer, "%07x ", i / 8);
    field_buffer += 8;
  }
}

/// The set of fields, default and user-specified, that are sensitive and whose
/// values will be replaced with auto-generated generic content.
sensitive_fields_t sensitive_fields;

std::string
TransactionData::get_sensitive_field_description()
{
  std::string sensitive_fields_string;
  bool is_first = true;
  for (const auto &field : sensitive_fields) {
    if (!is_first) {
      sensitive_fields_string += ", ";
    }
    is_first = false;
    sensitive_fields_string += field;
  }
  return sensitive_fields_string;
}

TransactionData::TransactionData(TSHttpTxn txnp, std::string_view http_version_from_client_stack)
  : _txnp{txnp}, _http_version_from_client_stack{http_version_from_client_stack}
{
}

bool
TransactionData::init(sensitive_fields_t &&new_fields)
{
  sensitive_fields = std::move(new_fields);
  return init_helper();
}

bool
TransactionData::init()
{
  sensitive_fields = default_sensitive_fields;
  return init_helper();
}

std::string_view
TransactionData::replace_sensitive_fields(std::string_view name, std::string_view original_value)
{
  auto search = sensitive_fields.find(std::string(name));
  if (search == sensitive_fields.end()) {
    return original_value;
  }
  auto new_value_size = original_value.size();
  if (original_value.size() > default_sensitive_field_value.size()) {
    new_value_size = default_sensitive_field_value.size();
    TSError("[%s] Encountered a sensitive field value larger than our default "
            "field size. Default size: %zu, incoming field size: %zu",
            debug_tag, default_sensitive_field_value.size(), original_value.size());
  }
  return std::string_view{default_sensitive_field_value.data(), new_value_size};
}

std::string
TransactionData::write_content_node(int64_t num_body_bytes)
{
  return std::string(R"(,"content":{"encoding":"plain","size":)" + std::to_string(num_body_bytes) + '}');
}

std::string
TransactionData::write_message_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc, std::string_view http_version)
{
  std::string result;
  int len        = 0;
  char const *cp = nullptr;
  TSMLoc url_loc = nullptr;

  // 1. "version"
  result += R"("version":")";
  if (http_version.empty()) {
    int version = TSHttpHdrVersionGet(buffer, hdr_loc);
    result += std::to_string(TS_HTTP_MAJOR(version)) + "." + std::to_string(TS_HTTP_MINOR(version));
  } else {
    result += http_version;
  }
  result += R"(",)";

  // Log scheme+method+request-target or status+reason based on header type
  if (TSHttpHdrTypeGet(buffer, hdr_loc) == TS_HTTP_TYPE_REQUEST) {
    TSAssert(TS_SUCCESS == TSHttpHdrUrlGet(buffer, hdr_loc, &url_loc));
    // 2. "scheme":
    cp = TSUrlSchemeGet(buffer, url_loc, &len);
    TSDebug(debug_tag, "write_message_node(): found scheme %.*s ", len, cp);
    result += traffic_dump::json_entry("scheme", cp, len);

    // 3. "method":(string)
    cp = TSHttpHdrMethodGet(buffer, hdr_loc, &len);
    TSDebug(debug_tag, "write_message_node(): found method %.*s ", len, cp);
    result += "," + traffic_dump::json_entry("method", cp, len);

    // 4. "url"
    cp = TSUrlHostGet(buffer, url_loc, &len);
    std::string_view host{cp, static_cast<size_t>(len)};

    char *url = TSUrlStringGet(buffer, url_loc, &len);
    std::string_view url_string{url, static_cast<size_t>(len)};

    if (host.empty()) {
      // TSUrlStringGet will add the scheme to the URL, even if the request
      // target doesn't contain it. However, we cannot just always remove the
      // scheme because the original request target may include it. We assume
      // here that a URL with a scheme but not a host is artificial and thus
      // we remove it.
      url_string = remove_scheme_prefix(url_string);
    }

    TSDebug(debug_tag, "write_message_node(): found host target %.*s", static_cast<int>(url_string.size()), url_string.data());
    result += ',' + traffic_dump::json_entry("url", url_string);
    TSfree(url);
    TSHandleMLocRelease(buffer, hdr_loc, url_loc);
  } else {
    // 2. "status":(string)
    result += R"("status":)" + std::to_string(TSHttpHdrStatusGet(buffer, hdr_loc));
    // 3. "reason":(string)
    cp = TSHttpHdrReasonGet(buffer, hdr_loc, &len);
    result += ',' + traffic_dump::json_entry("reason", cp, len);
  }

  // "headers": [[name(string), value(string)]]
  result += R"(,"headers":{"encoding":"esc_json", "fields": [)";
  TSMLoc field_loc = TSMimeHdrFieldGet(buffer, hdr_loc, 0);
  while (field_loc) {
    TSMLoc next_field_loc;
    char const *name  = nullptr;
    char const *value = nullptr;
    int name_len = 0, value_len = 0;
    // Append to "fields" list if valid value exists
    if ((name = TSMimeHdrFieldNameGet(buffer, hdr_loc, field_loc, &name_len)) && name_len) {
      std::string_view name_view{name, static_cast<size_t>(name_len)};
      value = TSMimeHdrFieldValueStringGet(buffer, hdr_loc, field_loc, -1, &value_len);
      std::string_view value_view{value, static_cast<size_t>(value_len)};
      std::string_view new_value = replace_sensitive_fields(name_view, value_view);
      result += traffic_dump::json_entry_array(name_view, new_value);
    }

    next_field_loc = TSMimeHdrFieldNext(buffer, hdr_loc, field_loc);
    TSHandleMLocRelease(buffer, hdr_loc, field_loc);
    if ((field_loc = next_field_loc) != nullptr) {
      result += ",";
    }
  }
  return result += "]}";
}

std::string
TransactionData::write_message_node(TSMBuffer &buffer, TSMLoc &hdr_loc, int64_t num_body_bytes, std::string_view http_version)
{
  std::string result = write_message_node_no_content(buffer, hdr_loc, http_version);
  result += write_content_node(num_body_bytes);
  return result + "}";
}

std::string_view
TransactionData::remove_scheme_prefix(std::string_view url)
{
  const auto scheme_separator = url.find("://");
  if (scheme_separator == std::string::npos) {
    return url;
  }
  url.remove_prefix(scheme_separator + 3);
  return url;
}

bool
TransactionData::init_helper()
{
  initialize_default_sensitive_field();
  const std::string sensitive_fields_string = get_sensitive_field_description();
  TSDebug(debug_tag, "Sensitive fields for which generic values will be dumped: %s", sensitive_fields_string.c_str());

  if (TS_SUCCESS !=
      TSUserArgIndexReserve(TS_USER_ARGS_TXN, traffic_dump::debug_tag, "Track transaction related data", &transaction_arg_index)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve transaction arg.", traffic_dump::debug_tag);
    return false;
  }

  // Register the collecting of client-request headers at the global level so
  // we can process requests before other plugins. (Global hooks are
  // processed before session and transaction ones.)
  TSCont txn_cont = TSContCreate(global_transaction_handler, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txn_cont);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, txn_cont);
  return true;
}

void
TransactionData::write_client_request_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc)
{
  std::ostringstream client_request_node;
  client_request_node << R"(,"client-request":{)";

  auto const http_version = _http_version_from_client_stack;
  if (http_version == "2") {
    client_request_node << R"("http2":{)";

    uint64_t stream_id;
    TSAssert(TS_SUCCESS == TSHttpTxnClientStreamIdGet(_txnp, &stream_id));
    client_request_node << R"("stream-id":)" << std::to_string(stream_id);

    TSHttp2Priority priority;
    memset(&priority, 0, sizeof(priority));
    TSAssert(TS_SUCCESS == TSHttpTxnClientStreamPriorityGet(_txnp, reinterpret_cast<TSHttpPriority *>(&priority)));
    TSAssert(HTTP_PRIORITY_TYPE_HTTP_2 == priority.priority_type);
    // Http2Stream uses -1 as an indication that no priority was set.
    if (priority.stream_dependency != -1) {
      client_request_node << R"(,"priority":{)";
      client_request_node << R"("stream-depenency":)" << std::to_string(priority.stream_dependency);
      client_request_node << R"(,"weight":)" << std::to_string(priority.weight);
      client_request_node << "}";
    }

    client_request_node << "},";
  }

  // We don't have an accurate view of the body size until TXN_CLOSE so we hold
  // off on writing the content:size node until then.
  client_request_node << write_message_node_no_content(buffer, hdr_loc, http_version);
  _txn_json += client_request_node.str();
}

void
TransactionData::write_proxy_request_node(TSMBuffer &buffer, TSMLoc &hdr_loc)
{
  std::ostringstream proxy_request_node;
  proxy_request_node << R"(,"proxy-request":{)";
  proxy_request_node << _server_protocol_description + ",";
  proxy_request_node << write_message_node(buffer, hdr_loc, TSHttpTxnServerReqBodyBytesGet(_txnp));
  _txn_json += proxy_request_node.str();
}

void
TransactionData::write_server_response_node(TSMBuffer &buffer, TSMLoc &hdr_loc)
{
  _txn_json += R"(,"server-response":{)" + write_message_node(buffer, hdr_loc, TSHttpTxnServerRespBodyBytesGet(_txnp));
}

void
TransactionData::write_proxy_response_node(TSMBuffer &buffer, TSMLoc &hdr_loc)
{
  _txn_json += R"(,"proxy-response":{)" +
               write_message_node(buffer, hdr_loc, TSHttpTxnClientRespBodyBytesGet(_txnp), _http_version_from_client_stack);
}

// Transaction handler: writes headers to the log file using AIO
int
TransactionData::global_transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  // Retrieve SessionData
  TSHttpSsn ssnp       = TSHttpTxnSsnGet(txnp);
  SessionData *ssnData = static_cast<SessionData *>(TSUserArgGet(ssnp, SessionData::get_session_arg_index()));

  // If no valid ssnData, continue transaction as if nothing happened. This transaction
  // must be filtered out by our filter criteria.
  if (!ssnData) {
    TSDebug(debug_tag, "session_txn_handler(): No ssnData found. Abort.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    // We will piece together JSON content accumulated across several hooks of
    // the transaction. The catch is that hooks across transactions in a
    // session may fire interleaved in HTTP/2. Thus, in order to get
    // non-garbled JSON content, we accumulate the data for an entire
    // transaction and write that atomically once the transaction is completed.
    TransactionData *txnData = new TransactionData(txnp, ssnData->get_http_version_in_client_stack());
    TSUserArgSet(txnp, transaction_arg_index, txnData);
    // Get UUID
    char uuid[TS_CRUUID_STRING_LEN + 1];
    TSAssert(TS_SUCCESS == TSClientRequestUuidGet(txnp, uuid));
    std::string_view uuid_view{uuid, strnlen(uuid, TS_CRUUID_STRING_LEN)};

    // Generate per transaction json records
    txnData->_txn_json += "{";
    // "connection-time":(number)
    TSHRTime start_time;
    TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_UA_BEGIN, &start_time);
    txnData->_txn_json += "\"connection-time\":" + std::to_string(start_time);

    // "uuid":(string)
    // The uuid is a header field for each message in the transaction. Use the
    // "all" node to apply to each message.
    std::string_view name = "uuid";
    txnData->_txn_json += R"(,"all":{"headers":{"fields":[)" + json_entry_array(name, uuid_view);
    txnData->_txn_json += "]}}";
    break;
  }

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TransactionData *txnData = static_cast<TransactionData *>(TSUserArgGet(txnp, transaction_arg_index));
    if (!txnData) {
      TSError("[%s] No transaction data found for the header hook we registered for.", traffic_dump::debug_tag);
      break;
    }
    // This hook is registered globally, not at TS_EVENT_HTTP_SSN_START in
    // global_session_handler(). As such, this handler will be called with every
    // transaction. However, we infer that we are dumping this transaction
    // because there is a ssnData associated with it.

    // We must grab the client request information before remap happens because
    // the remap process modifies the request buffer.
    TSMBuffer buffer;
    TSMLoc hdr_loc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(debug_tag, "Found client request");
      // We don't have an accurate view of the body size until TXN_CLOSE so we hold
      // off on writing the content:size node until then.
      txnData->write_client_request_node_no_content(buffer, hdr_loc);
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    break;
  }

  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    TransactionData *txnData = static_cast<TransactionData *>(TSUserArgGet(txnp, transaction_arg_index));
    if (!txnData) {
      TSError("[%s] No transaction data found for the header hook we registered for.", traffic_dump::debug_tag);
      break;
    }
    txnData->_server_protocol_description = ssnData->get_server_protocol_description(txnp);
    break;
  }

  case TS_EVENT_HTTP_TXN_CLOSE: {
    TransactionData *txnData = static_cast<TransactionData *>(TSUserArgGet(txnp, SessionData::get_session_arg_index()));
    if (!txnData) {
      TSError("[%s] No transaction data found for the close hook we registered for.", traffic_dump::debug_tag);
      break;
    }
    // proxy-request/response headers
    TSMBuffer buffer;
    TSMLoc hdr_loc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
      // The node was started above in TS_EVENT_HTTP_READ_REQUEST_HDR. Here we
      // just have to finish it off by writing the content node.
      txnData->_txn_json += txnData->write_content_node(TSHttpTxnClientReqBodyBytesGet(txnp)) + "}";
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(debug_tag, "Found proxy request");
      txnData->write_proxy_request_node(buffer, hdr_loc);
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(debug_tag, "Found server response");
      txnData->write_server_response_node(buffer, hdr_loc);
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(debug_tag, "Found proxy response");
      txnData->write_proxy_response_node(buffer, hdr_loc);
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }

    txnData->_txn_json += "}";
    ssnData->write_transaction_to_disk(txnData->_txn_json);
    delete txnData;
    break;
  }
  default:
    TSDebug(debug_tag, "session_txn_handler(): Unhandled events %d", event);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

} // namespace traffic_dump
