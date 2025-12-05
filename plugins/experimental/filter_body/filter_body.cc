/** @file

  @brief A remap plugin that filters request/response bodies for CVE exploitation patterns.

  This plugin performs zero-copy streaming inspection of request or response bodies,
  looking for configured patterns. When a pattern matches, it can log, block (403),
  and/or add a header.

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

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include <yaml-cpp/yaml.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "filter_body"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

// Action flags
constexpr unsigned ACTION_LOG        = 1 << 0;
constexpr unsigned ACTION_BLOCK      = 1 << 1;
constexpr unsigned ACTION_ADD_HEADER = 1 << 2;

// Direction
enum class Direction { REQUEST, RESPONSE };

// Header match condition
struct HeaderCondition {
  std::string              name;
  std::vector<std::string> patterns; // case-insensitive match
};

// A single filtering rule
struct Rule {
  std::string                  name;
  Direction                    direction = Direction::REQUEST;
  unsigned                     actions   = ACTION_LOG; // default: log only
  std::string                  add_header_name;
  std::string                  add_header_value;
  std::vector<std::string>     methods;
  int64_t                      max_content_length = -1; // -1 means no limit
  std::vector<HeaderCondition> headers;
  std::vector<std::string>     body_patterns; // case-sensitive match
  size_t                       max_pattern_len = 0;
};

// Plugin configuration (per remap instance)
struct FilterConfig {
  std::vector<Rule> request_rules;
  std::vector<Rule> response_rules;
  size_t            max_lookback = 0; // max pattern length - 1 across all rules
};

// Per-transaction transform data
struct TransformData {
  TSHttpTxn                 txnp;
  const Rule               *matched_rule = nullptr;
  const FilterConfig       *config       = nullptr;
  std::vector<const Rule *> active_rules; // rules that passed header check
  std::string               lookback;     // small buffer for cross-boundary patterns
  TSIOBuffer                output_buffer = nullptr;
  TSIOBufferReader          output_reader = nullptr;
  TSVIO                     output_vio    = nullptr;
  bool                      blocked       = false;
  bool                      headers_added = false;
};

// Case-insensitive string search
const char *
strcasestr_local(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
  if (needle_len == 0) {
    return haystack;
  }
  if (haystack_len < needle_len) {
    return nullptr;
  }

  for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
    bool match = true;
    for (size_t j = 0; j < needle_len; ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) != std::tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) {
      return haystack + i;
    }
  }
  return nullptr;
}

// Case-sensitive string search
const char *
strstr_local(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
  if (needle_len == 0) {
    return haystack;
  }
  if (haystack_len < needle_len) {
    return nullptr;
  }

  for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
    if (memcmp(haystack + i, needle, needle_len) == 0) {
      return haystack + i;
    }
  }
  return nullptr;
}

// Check if method matches
bool
method_matches(const Rule &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  if (rule.methods.empty()) {
    return true; // no method restriction
  }

  int         method_len = 0;
  const char *method     = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
  if (method == nullptr) {
    return false;
  }

  std::string method_str(method, method_len);
  for (const auto &m : rule.methods) {
    if (strcasecmp(method_str.c_str(), m.c_str()) == 0) {
      return true;
    }
  }
  return false;
}

// Check Content-Length against max
bool
content_length_ok(const Rule &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  if (rule.max_content_length < 0) {
    return true; // no limit
  }

  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (field_loc == TS_NULL_MLOC) {
    return true; // no Content-Length header, allow
  }

  int64_t content_length = TSMimeHdrFieldValueInt64Get(bufp, hdr_loc, field_loc, 0);
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);

  return content_length <= rule.max_content_length;
}

// Check if a single header condition matches (case-insensitive pattern search)
bool
header_condition_matches(const HeaderCondition &cond, TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, cond.name.c_str(), static_cast<int>(cond.name.length()));
  if (field_loc == TS_NULL_MLOC) {
    return false;
  }

  bool matched = false;
  // Check all values of this header field
  int num_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
  for (int i = 0; i < num_values && !matched; ++i) {
    int         value_len = 0;
    const char *value     = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value_len);
    if (value == nullptr) {
      continue;
    }

    // Check if any pattern matches (OR logic within header)
    for (const auto &pattern : cond.patterns) {
      if (strcasestr_local(value, value_len, pattern.c_str(), pattern.length()) != nullptr) {
        matched = true;
        break;
      }
    }
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return matched;
}

// Check if ALL header conditions match (AND logic between headers)
bool
headers_match(const Rule &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  for (const auto &cond : rule.headers) {
    if (!header_condition_matches(cond, bufp, hdr_loc)) {
      return false;
    }
  }
  return true;
}

// Search for body patterns in data (case-sensitive)
// Returns the matched pattern or nullptr
const std::string *
search_body_patterns(const Rule &rule, const char *data, size_t data_len)
{
  for (const auto &pattern : rule.body_patterns) {
    if (strstr_local(data, data_len, pattern.c_str(), pattern.length()) != nullptr) {
      return &pattern;
    }
  }
  return nullptr;
}

// Add header to the request/response
void
add_header_to_message(TSMBuffer bufp, TSMLoc hdr_loc, const std::string &name, const std::string &value)
{
  TSMLoc field_loc;
  if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, name.c_str(), static_cast<int>(name.length()), &field_loc) != TS_SUCCESS) {
    TSError("[%s] Failed to create header field: %s", PLUGIN_NAME, name.c_str());
    return;
  }

  if (TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, value.c_str(), static_cast<int>(value.length())) != TS_SUCCESS) {
    TSError("[%s] Failed to set header value: %s", PLUGIN_NAME, name.c_str());
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    return;
  }

  if (TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc) != TS_SUCCESS) {
    TSError("[%s] Failed to append header field: %s", PLUGIN_NAME, name.c_str());
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
}

// Execute actions for a matched rule
void
execute_actions(TransformData *data, const Rule *rule, const std::string *matched_pattern)
{
  if (rule->actions & ACTION_LOG) {
    Dbg(dbg_ctl, "Matched rule: %s, pattern: %s", rule->name.c_str(), matched_pattern ? matched_pattern->c_str() : "unknown");
  }

  if ((rule->actions & ACTION_ADD_HEADER) && !data->headers_added) {
    TSMBuffer bufp;
    TSMLoc    hdr_loc;

    // Add header to server request (proxy request going to origin)
    if (TSHttpTxnServerReqGet(data->txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      add_header_to_message(bufp, hdr_loc, rule->add_header_name, rule->add_header_value);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      data->headers_added = true;
      Dbg(dbg_ctl, "Added header %s: %s", rule->add_header_name.c_str(), rule->add_header_value.c_str());
    } else {
      // Fallback to client request if server request not available
      if (TSHttpTxnClientReqGet(data->txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
        add_header_to_message(bufp, hdr_loc, rule->add_header_name, rule->add_header_value);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        data->headers_added = true;
        Dbg(dbg_ctl, "Added header %s: %s (to client request)", rule->add_header_name.c_str(), rule->add_header_value.c_str());
      }
    }
  }

  if (rule->actions & ACTION_BLOCK) {
    data->blocked = true;
    TSHttpTxnStatusSet(data->txnp, TS_HTTP_STATUS_FORBIDDEN);
    // Set error body so client gets a proper response
    static const char *error_body = "Blocked by content filter";
    TSHttpTxnErrorBodySet(data->txnp, TSstrdup(error_body), strlen(error_body), TSstrdup("text/plain"));
    Dbg(dbg_ctl, "Blocking request due to rule: %s", rule->name.c_str());
  }
}

// Transform handler - processes streaming data
int
transform_handler(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  if (TSVConnClosedGet(contp)) {
    auto *data = static_cast<TransformData *>(TSContDataGet(contp));
    if (data) {
      if (data->output_buffer) {
        TSIOBufferDestroy(data->output_buffer);
      }
      delete data;
    }
    TSContDestroy(contp);
    return 0;
  }

  auto *data = static_cast<TransformData *>(TSContDataGet(contp));
  if (data == nullptr) {
    return 0;
  }

  switch (event) {
  case TS_EVENT_ERROR: {
    TSVIO write_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
    break;
  }

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
  default: {
    // Get the write VIO
    TSVIO write_vio = TSVConnWriteVIOGet(contp);
    if (!TSVIOBufferGet(write_vio)) {
      // No more data
      if (data->output_vio) {
        TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio));
        TSVIOReenable(data->output_vio);
      }
      return 0;
    }

    // Initialize output buffer if needed
    if (!data->output_buffer) {
      TSVConn output_conn = TSTransformOutputVConnGet(contp);
      data->output_buffer = TSIOBufferCreate();
      data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);

      int64_t nbytes   = TSVIONBytesGet(write_vio);
      data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, nbytes);
    }

    // Process available data
    int64_t towrite = TSVIONTodoGet(write_vio);
    if (towrite > 0 && !data->blocked) {
      TSIOBufferReader reader = TSVIOReaderGet(write_vio);
      int64_t          avail  = TSIOBufferReaderAvail(reader);
      if (avail > towrite) {
        avail = towrite;
      }

      if (avail > 0) {
        // Zero-copy: iterate through buffer blocks
        TSIOBufferBlock block = TSIOBufferReaderStart(reader);
        while (block != nullptr && !data->blocked) {
          int64_t     block_avail = 0;
          const char *block_data  = TSIOBufferBlockReadStart(block, reader, &block_avail);

          if (block_data && block_avail > 0) {
            // Search for patterns in lookback + current block
            std::string search_window;
            if (!data->lookback.empty()) {
              search_window = data->lookback + std::string(block_data, block_avail);
            }

            const char *search_data;
            size_t      search_len;
            if (!data->lookback.empty()) {
              search_data = search_window.c_str();
              search_len  = search_window.length();
            } else {
              search_data = block_data;
              search_len  = block_avail;
            }

            // Check each active rule
            for (const Rule *rule : data->active_rules) {
              const std::string *matched = search_body_patterns(*rule, search_data, search_len);
              if (matched) {
                execute_actions(data, rule, matched);
                if (data->blocked) {
                  break;
                }
              }
            }

            // Update lookback buffer (only keep last max_lookback bytes)
            if (data->config->max_lookback > 0 && !data->blocked) {
              size_t lookback_size = data->config->max_lookback;
              if (static_cast<size_t>(block_avail) >= lookback_size) {
                data->lookback.assign(block_data + block_avail - lookback_size, lookback_size);
              } else {
                data->lookback.append(block_data, block_avail);
                if (data->lookback.length() > lookback_size) {
                  data->lookback = data->lookback.substr(data->lookback.length() - lookback_size);
                }
              }
            }
          }

          block = TSIOBufferBlockNext(block);
        }

        if (data->blocked) {
          // Complete the transform with zero output - the 403 status we set
          // will cause ATS to generate the error response
          TSVIONBytesSet(data->output_vio, 0);
          TSVIOReenable(data->output_vio);

          // Consume all remaining input
          int64_t remaining = TSIOBufferReaderAvail(reader);
          if (remaining > 0) {
            TSIOBufferReaderConsume(reader, remaining);
          }
          TSVIONDoneSet(write_vio, TSVIONBytesGet(write_vio));

          // Signal write complete
          TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
          return 0;
        }

        // Zero-copy: copy data through to output
        TSIOBufferCopy(data->output_buffer, reader, avail, 0);
        TSIOBufferReaderConsume(reader, avail);
        TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + avail);
      }
    }

    // Check if we're done
    if (TSVIONTodoGet(write_vio) > 0) {
      if (towrite > 0) {
        TSVIOReenable(data->output_vio);
        TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
      }
    } else {
      TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio));
      TSVIOReenable(data->output_vio);
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
    }
    break;
  }
  }

  return 0;
}

// Create transform continuation
TSVConn
create_transform(TSHttpTxn txnp, const FilterConfig *config, const std::vector<const Rule *> &active_rules)
{
  TSVConn connp = TSTransformCreate(transform_handler, txnp);

  auto *data         = new TransformData();
  data->txnp         = txnp;
  data->config       = config;
  data->active_rules = active_rules;

  // Pre-allocate lookback buffer
  if (config->max_lookback > 0) {
    data->lookback.reserve(config->max_lookback);
  }

  TSContDataSet(connp, data);
  return connp;
}

// Hook handler for response rules (request rules are handled directly in TSRemapDoRemap)
int
hook_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn           txnp   = static_cast<TSHttpTxn>(edata);
  const FilterConfig *config = static_cast<const FilterConfig *>(TSContDataGet(contp));

  if (config == nullptr) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMBuffer bufp;
  TSMLoc    hdr_loc;

  std::vector<const Rule *> active_rules;

  if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    // Check response rules
    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }

    // Need client request for method check
    TSMBuffer req_bufp;
    TSMLoc    req_hdr_loc;
    if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr_loc) != TS_SUCCESS) {
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }

    for (const auto &rule : config->response_rules) {
      if (method_matches(rule, req_bufp, req_hdr_loc) && content_length_ok(rule, bufp, hdr_loc) &&
          headers_match(rule, bufp, hdr_loc)) {
        Dbg(dbg_ctl, "Response rule '%s' header conditions matched, will inspect body", rule.name.c_str());
        active_rules.push_back(&rule);
      }
    }

    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    if (!active_rules.empty()) {
      TSVConn transform = create_transform(txnp, config, active_rules);
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform);
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

// Parse YAML configuration
FilterConfig *
parse_config(const char *filename)
{
  std::string path;
  if (filename[0] == '/') {
    path = filename;
  } else {
    path = std::string(TSConfigDirGet()) + "/" + filename;
  }

  Dbg(dbg_ctl, "Loading configuration from %s", path.c_str());

  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const std::exception &ex) {
    TSError("[%s] Failed to load config file '%s': %s", PLUGIN_NAME, path.c_str(), ex.what());
    return nullptr;
  }

  auto *config = new FilterConfig();

  try {
    if (!root["rules"]) {
      TSError("[%s] No 'rules' section in config", PLUGIN_NAME);
      delete config;
      return nullptr;
    }

    for (const auto &rule_node : root["rules"]) {
      Rule rule;

      // Name (required)
      if (rule_node["name"]) {
        rule.name = rule_node["name"].as<std::string>();
      } else {
        TSError("[%s] Rule missing 'name' field", PLUGIN_NAME);
        delete config;
        return nullptr;
      }

      // Direction (default: request)
      if (rule_node["direction"]) {
        std::string dir = rule_node["direction"].as<std::string>();
        if (dir == "response") {
          rule.direction = Direction::RESPONSE;
        } else {
          rule.direction = Direction::REQUEST;
        }
      }

      // Actions (default: [log])
      rule.actions = 0;
      if (rule_node["action"]) {
        for (const auto &action_node : rule_node["action"]) {
          std::string action = action_node.as<std::string>();
          if (action == "log") {
            rule.actions |= ACTION_LOG;
          } else if (action == "block") {
            rule.actions |= ACTION_BLOCK;
          } else if (action == "add_header") {
            rule.actions |= ACTION_ADD_HEADER;
          }
        }
      }
      if (rule.actions == 0) {
        rule.actions = ACTION_LOG; // default
      }

      // Add header config
      if (rule_node["add_header"]) {
        if (rule_node["add_header"]["name"]) {
          rule.add_header_name = rule_node["add_header"]["name"].as<std::string>();
        }
        if (rule_node["add_header"]["value"]) {
          rule.add_header_value = rule_node["add_header"]["value"].as<std::string>();
        }
      }

      // Methods
      if (rule_node["methods"]) {
        for (const auto &method_node : rule_node["methods"]) {
          rule.methods.push_back(method_node.as<std::string>());
        }
      }

      // Max content length
      if (rule_node["max_content_length"]) {
        rule.max_content_length = rule_node["max_content_length"].as<int64_t>();
      }

      // Header conditions
      if (rule_node["headers"]) {
        for (const auto &header_node : rule_node["headers"]) {
          HeaderCondition cond;
          if (header_node["name"]) {
            cond.name = header_node["name"].as<std::string>();
          }
          if (header_node["patterns"]) {
            for (const auto &pattern_node : header_node["patterns"]) {
              cond.patterns.push_back(pattern_node.as<std::string>());
            }
          }
          rule.headers.push_back(cond);
        }
      }

      // Body patterns
      if (rule_node["body_patterns"]) {
        for (const auto &pattern_node : rule_node["body_patterns"]) {
          std::string pattern = pattern_node.as<std::string>();
          rule.body_patterns.push_back(pattern);
          if (pattern.length() > rule.max_pattern_len) {
            rule.max_pattern_len = pattern.length();
          }
        }
      }

      // Update max lookback
      if (rule.max_pattern_len > 1) {
        size_t lookback = rule.max_pattern_len - 1;
        if (lookback > config->max_lookback) {
          config->max_lookback = lookback;
        }
      }

      Dbg(dbg_ctl, "Loaded rule: %s (direction=%s, actions=%u)", rule.name.c_str(),
          rule.direction == Direction::REQUEST ? "request" : "response", rule.actions);

      // Add to appropriate list
      if (rule.direction == Direction::REQUEST) {
        config->request_rules.push_back(std::move(rule));
      } else {
        config->response_rules.push_back(std::move(rule));
      }
    }
  } catch (const std::exception &ex) {
    TSError("[%s] Error parsing config: %s", PLUGIN_NAME, ex.what());
    delete config;
    return nullptr;
  }

  Dbg(dbg_ctl, "Loaded %zu request rules and %zu response rules (max_lookback=%zu)", config->request_rules.size(),
      config->response_rules.size(), config->max_lookback);

  return config;
}

} // anonymous namespace

///////////////////////////////////////////////////////////////////////////////
// Remap plugin interface
///////////////////////////////////////////////////////////////////////////////

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    TSstrlcpy(errbuf, "[TSRemapInit] Invalid TSRemapInterface argument", errbuf_size);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    TSstrlcpy(errbuf, "[TSRemapInit] Incorrect size of TSRemapInterface structure", errbuf_size);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "filter_body remap plugin initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  if (argc < 3) {
    TSstrlcpy(errbuf, "[TSRemapNewInstance] Missing configuration file argument", errbuf_size);
    return TS_ERROR;
  }

  FilterConfig *config = parse_config(argv[2]);
  if (config == nullptr) {
    TSstrlcpy(errbuf, "[TSRemapNewInstance] Failed to parse configuration file", errbuf_size);
    return TS_ERROR;
  }

  *ih = config;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto *config = static_cast<FilterConfig *>(ih);
  delete config;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri ATS_UNUSED)
{
  auto *config = static_cast<FilterConfig *>(ih);
  if (config == nullptr) {
    return TSREMAP_NO_REMAP;
  }

  // For request rules, check headers now (in TSRemapDoRemap, headers are already available)
  if (!config->request_rules.empty()) {
    TSMBuffer bufp;
    TSMLoc    hdr_loc;

    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      std::vector<const Rule *> active_rules;

      for (const auto &rule : config->request_rules) {
        if (method_matches(rule, bufp, hdr_loc) && content_length_ok(rule, bufp, hdr_loc) && headers_match(rule, bufp, hdr_loc)) {
          Dbg(dbg_ctl, "Request rule '%s' header conditions matched, will inspect body", rule.name.c_str());
          active_rules.push_back(&rule);
        }
      }

      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

      if (!active_rules.empty()) {
        TSVConn transform = create_transform(txnp, config, active_rules);
        TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, transform);
      }
    }
  }

  // For response rules, add a hook to check when response headers arrive
  if (!config->response_rules.empty()) {
    TSCont contp = TSContCreate(hook_handler, nullptr);
    TSContDataSet(contp, config);
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  }

  return TSREMAP_NO_REMAP;
}
