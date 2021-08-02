/** @file

  Traffic Dump data specific to transactions.

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

#pragma once

#include <string>
#include <string_view>

#include "ts/ts.h"

#include "sensitive_fields.h"

namespace traffic_dump
{
/** The information associated with an single transaction.
 *
 * This class is responsible for containing the members associated with a
 * particular transaction and defines the transaction handler callback.
 */
class TransactionData
{
private:
  /// The TSHttpTxn of the associated HTTP transaction.
  TSHttpTxn _txnp = nullptr;

  /// The HTTP version in the client-side protocol stack or empty string
  /// if it was not specified there.
  std::string _http_version_from_client_stack;

  /** The string for the JSON content of this transaction. */
  std::string _txn_json;

  /** The client-response body bytes, if dump_body is true. */
  std::string _response_body;

  /** The '"protocol" node for this transaction's server-side connection. */
  std::string _server_protocol_description;

  // The index to be used for the TS API for storing this TransactionData on a
  // per-transaction basis.
  static int transaction_arg_index;

  /// The set of fields, default and user-specified, that are sensitive and
  /// whose values will be replaced with auto-generated generic content.
  static sensitive_fields_t sensitive_fields;

  /// Whether the user configured the dumping of body content.
  static bool _dump_body;

public:
  /** Initialize TransactionData, using the provided sensitive fields.
   *
   * @param[in] dump_body Whether to dump body content.
   *
   * @param[in] sensitive_fields_t The HTTP fields considered to have sensitive
   * data.
   *
   * @return True if initialization is successful, false otherwise.
   */
  static bool init(bool dump_body, sensitive_fields_t &&sensitive_fields);

  /** Initialize TransactionData, using default sensitive fields.
   *
   * @param[in] dump_body Whether to dump body content.
   *
   * @return True if initialization is successful, false otherwise.
   */
  static bool init(bool dump_body);

  /** Read the txn information from TSMBuffer and write the header information.
   * This function does not write the content node.
   *
   * @param[in] http_version An optional specification for the HTTP "version"
   * node.
   */
  std::string write_message_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc, std::string_view http_version = "");

  /** Read the txn information from TSMBuffer and write the header information including
   * the content node describing the body characteristics.
   *
   * @param[in] num_body_bytes The number of body bytes to specify in the content node.
   * @param[in] http_version An optional specification for the HTTP "version"
   * node.
   */
  std::string write_message_node(TSMBuffer &buffer, TSMLoc &hdr_loc, int64_t num_body_bytes, std::string_view http_version = "");

  /** Read the txn information from TSMBuffer and write the header information including
   * the content node containing the provided body.
   *
   * @param[in] body The body bytes to place in the content node.
   * @param[in] http_version An optional specification for the HTTP "version"
   * node.
   */
  std::string write_message_node(TSMBuffer &buffer, TSMLoc &hdr_loc, std::string_view body, std::string_view http_version = "");

  /// The handler callback for transaction events.
  static int global_transaction_handler(TSCont contp, TSEvent event, void *edata);

private:
  /** Common logic for the init overloads.
   *
   * @param[in] dump_body Whether the user configured the dumping of body
   * content.
   *
   * @return True if initialization is successful, false otherwise.
   */
  static bool init_helper(bool dump_body);

  /** Initialize the generic sensitive field to be dumped. This is used instead
   * of the sensitive field values seen on the wire.
   */
  static void initialize_default_sensitive_field();

  /** Return a separated string representing the HTTP fields considered sensitive.
   *
   * @return A comma-separated string representing the sensitive HTTP fields.
   */
  static std::string get_sensitive_field_description();

  /** Construct a TransactionData object.
   *
   * Note that this constructor is private since only the global handler
   * creates these at the moment.
   *
   * @param[in] txnp The TSHttpTxn for the associated HTTP transaction.
   *
   * @param[in] http_version_from_client_stack The HTTP version as specified in
   *    the protocol stack, or empty string if no so specified.
   */
  TransactionData(TSHttpTxn txnp, std::string_view http_version_from_client_stack);

  /** Retrieve the response body from the transaction.
   *
   * @param[in] txnp The transaction from which to retrieve the response body.
   *
   * @return The response body string.
   */
  static std::string response_body_get(TSHttpTxn txnp);

  /** The callback for gathering response body data.
   *
   * @note This is only called if the user enabled dump_body.
   */
  static int response_buffer_handler(TSCont contp, TSEvent event, void *edata);

  /** Inspect the field to see whether it is sensitive and return a generic value
   * of equal size to the original if it is.
   *
   * @param[in] name The field name to inspect.
   * @param[in] original_value The field value to inspect.
   *
   * @return The value traffic_dump should dump for the given field.
   */
  std::string_view replace_sensitive_fields(std::string_view name, std::string_view original_value);

  /// Write the content JSON node for an HTTP message.
  //
  /// "content"
  ///    "encoding"
  ///    "size"
  std::string write_content_node(int64_t num_body_bytes);

  /// Write the content JSON node for an HTTP message.
  //
  /// "content"
  ///    "encoding"
  ///    "size"
  ///    "data"
  std::string write_content_node(std::string_view body);

  /** Remove the scheme prefix from the url.
   *
   * @return The view without the scheme prefix.
   */
  std::string_view remove_scheme_prefix(std::string_view url);

  /** Write the "client-request" node to _txn_json.
   *
   * Note that the "content" node is not written with this function, so it will
   * have to be written later.
   */
  void write_client_request_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc);

  /// Write the "proxy-request" node to _txn_json.
  void write_proxy_request_node(TSMBuffer &buffer, TSMLoc &hdr_loc);

  /// Write the "server-response" node to _txn_json.
  void write_server_response_node(TSMBuffer &buffer, TSMLoc &hdr_loc);

  /// Write the "proxy-response" node to _txn_json.
  void write_proxy_response_node(TSMBuffer &buffer, TSMLoc &hdr_loc);
};

} // namespace traffic_dump
