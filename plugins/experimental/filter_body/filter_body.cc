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

#include "swoc/TextView.h"
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

// Header to add when action triggers
struct AddHeader {
  std::string name;
  std::string value; // supports <rule_name> substitution
};

// A single filtering rule
struct Rule {
  std::string                  name;
  Direction                    direction = Direction::REQUEST;
  unsigned                     actions   = ACTION_LOG;  // default: log only
  std::vector<AddHeader>       add_headers;             // headers to add on match
  std::vector<std::string>     methods;                 // for request rules
  std::vector<int>             status_codes;            // for response rules
  int64_t                      max_content_length = -1; // -1 means no limit
  std::vector<HeaderCondition> headers;
  std::vector<std::string>     body_patterns; // case-sensitive match
  size_t                       max_pattern_len = 0;
  int                          stat_id         = -1; // metrics counter for matches (-1 = not created)
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
  Rule const               *matched_rule = nullptr;
  FilterConfig const       *config       = nullptr;
  std::vector<Rule const *> active_rules; // rules that passed header check
  std::string               lookback;     // small buffer for cross-boundary patterns
  TSIOBuffer                output_buffer = nullptr;
  TSIOBufferReader          output_reader = nullptr;
  TSVIO                     output_vio    = nullptr;
  Direction                 direction     = Direction::REQUEST; // direction of this transform
  bool                      blocked       = false;
  bool                      headers_added = false;
};

/**
 * @brief Case-insensitive substring search.
 *
 * Searches for @a needle within @a haystack using case-insensitive comparison.
 *
 * @param[in] haystack The string to search within.
 * @param[in] needle   The pattern to search for.
 * @return Pointer to the first occurrence of needle in haystack, or nullptr if not found.
 */
const char *
strcasestr_local(swoc::TextView haystack, swoc::TextView needle)
{
  if (needle.empty() || haystack.size() < needle.size()) {
    return nullptr;
  }

  for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
    if (haystack.substr(i, needle.size()).starts_with_nocase(needle)) {
      return haystack.data() + i;
    }
  }
  return nullptr;
}

/**
 * @brief Case-sensitive substring search.
 *
 * Searches for @a needle within @a haystack using exact (case-sensitive) comparison.
 *
 * @param[in] haystack The string to search within.
 * @param[in] needle   The pattern to search for.
 * @return Pointer to the first occurrence of needle in haystack, or nullptr if not found.
 */
const char *
strstr_local(swoc::TextView haystack, swoc::TextView needle)
{
  if (needle.empty() || haystack.size() < needle.size()) {
    return nullptr;
  }

  auto pos = haystack.find(needle);
  if (pos != std::string::npos) {
    return haystack.data() + pos;
  }
  return nullptr;
}

/**
 * @brief Check if the HTTP method matches the rule's method filter.
 *
 * If the rule has no method restrictions, all methods match.
 *
 * @param[in] rule    The rule containing method restrictions.
 * @param[in] bufp    The message buffer containing the HTTP headers.
 * @param[in] hdr_loc The location of the HTTP header.
 * @return true if the method matches or no method restriction exists, false otherwise.
 */
bool
method_matches(Rule const &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  if (rule.methods.empty()) {
    return true;
  }

  int         method_len = 0;
  const char *method     = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
  if (method == nullptr) {
    return false;
  }

  swoc::TextView method_view(method, method_len);
  method_view.trim_if(::isspace);

  for (auto const &m : rule.methods) {
    if (0 == strcasecmp(method_view, swoc::TextView(m))) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Check if the HTTP status code matches the rule's status filter.
 *
 * For response rules, this checks if the response status code is in the rule's
 * allowed status codes list.
 *
 * @param[in] rule    The rule containing the status code filter.
 * @param[in] bufp    The message buffer containing the HTTP response.
 * @param[in] hdr_loc The location of the HTTP response header.
 * @return true if the status matches or no status restriction exists, false otherwise.
 */
bool
status_matches(Rule const &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  if (rule.status_codes.empty()) {
    return true; // no status restriction
  }

  TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr_loc);
  for (int const code : rule.status_codes) {
    if (static_cast<int>(status) == code) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Check if Content-Length is within the rule's max_content_length limit.
 *
 * If the rule has no content length limit (max_content_length < 0), all sizes are allowed.
 * If the Content-Length header is missing, the check passes.
 *
 * @param[in] rule    The rule containing the content length limit.
 * @param[in] bufp    The message buffer containing the HTTP headers.
 * @param[in] hdr_loc The location of the HTTP header.
 * @return true if content length is within limit or no limit exists, false otherwise.
 */
bool
content_length_ok(Rule const &rule, TSMBuffer bufp, TSMLoc hdr_loc)
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

/**
 * @brief Check if a single header condition matches.
 *
 * Uses case-insensitive pattern search. Returns true if any pattern in the
 * condition matches any value of the specified header (OR logic within header).
 *
 * @param[in] cond    The header condition to check.
 * @param[in] bufp    The message buffer containing the HTTP headers.
 * @param[in] hdr_loc The location of the HTTP header.
 * @return true if the header exists and any pattern matches, false otherwise.
 */
bool
header_condition_matches(HeaderCondition const &cond, TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, cond.name.c_str(), static_cast<int>(cond.name.length()));
  if (field_loc == TS_NULL_MLOC) {
    return false;
  }

  bool matched    = false;
  int  num_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
  for (int i = 0; i < num_values && !matched; ++i) {
    int         value_len = 0;
    const char *value     = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value_len);
    if (value == nullptr) {
      continue;
    }

    swoc::TextView value_view(value, value_len);
    for (auto const &pattern : cond.patterns) {
      if (strcasestr_local(value_view, swoc::TextView(pattern)) != nullptr) {
        matched = true;
        break;
      }
    }
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return matched;
}

/**
 * @brief Check if ALL header conditions in a rule match.
 *
 * Uses AND logic between headers - all header conditions must match for the
 * rule to apply.
 *
 * @param[in] rule    The rule containing header conditions.
 * @param[in] bufp    The message buffer containing the HTTP headers.
 * @param[in] hdr_loc The location of the HTTP header.
 * @return true if all header conditions match, false otherwise.
 */
bool
headers_match(Rule const &rule, TSMBuffer bufp, TSMLoc hdr_loc)
{
  for (auto const &cond : rule.headers) {
    if (!header_condition_matches(cond, bufp, hdr_loc)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Search for body patterns in the given data.
 *
 * Searches for any of the rule's body patterns in the data using case-sensitive
 * matching. Returns the first matched pattern.
 *
 * @param[in] rule The rule containing body patterns to search for.
 * @param[in] data The data buffer to search within.
 * @return Pointer to the matched pattern string, or nullptr if no match.
 */
std::string const *
search_body_patterns(Rule const &rule, swoc::TextView data)
{
  for (auto const &pattern : rule.body_patterns) {
    if (strstr_local(data, swoc::TextView(pattern)) != nullptr) {
      return &pattern;
    }
  }
  return nullptr;
}

/**
 * @brief Add a header field to an HTTP message.
 *
 * Creates and appends a new header field with the given name and value.
 *
 * @param[in] bufp    The message buffer to add the header to.
 * @param[in] hdr_loc The location of the HTTP header.
 * @param[in] name    The header field name.
 * @param[in] value   The header field value.
 */
void
add_header_to_message(TSMBuffer bufp, TSMLoc hdr_loc, std::string const &name, std::string const &value)
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

/**
 * @brief Substitute <rule_name> placeholder in header value.
 *
 * @param[in] value     The header value that may contain <rule_name>.
 * @param[in] rule_name The rule name to substitute.
 * @return The value with <rule_name> replaced by the actual rule name.
 */
std::string
substitute_rule_name(std::string const &value, std::string const &rule_name)
{
  std::string       result      = value;
  std::string const placeholder = "<rule_name>";
  size_t            pos         = 0;
  while ((pos = result.find(placeholder, pos)) != std::string::npos) {
    result.replace(pos, placeholder.length(), rule_name);
    pos += rule_name.length();
  }
  return result;
}

/**
 * @brief Execute the configured actions for a matched rule.
 *
 * Performs the actions specified in the rule: log, add_header, and/or block.
 * For request rules, headers are added to the server request (proxy request to origin).
 * For response rules, headers are added to the client response.
 *
 * @note Headers are added during body inspection, which occurs after headers may have
 *       already been sent. For request transforms, the server request headers should
 *       still be modifiable. For response transforms, headers are added before the
 *       response is sent to the client.
 *
 * @param[in,out] data            The transform data containing transaction state.
 * @param[in]     rule            The matched rule containing actions to execute.
 * @param[in]     matched_pattern The pattern that triggered the match (for logging).
 */
void
execute_actions(TransformData *data, Rule const *rule, std::string const *matched_pattern)
{
  // Increment the metrics counter for this rule (stat_id is guaranteed valid at load time)
  TSStatIntIncrement(rule->stat_id, 1);

  // Log action always writes to diags.log so it doesn't require debug tags
  if (rule->actions & ACTION_LOG) {
    TSError("[%s] Matched rule: %s, pattern: %s", PLUGIN_NAME, rule->name.c_str(),
            matched_pattern ? matched_pattern->c_str() : "unknown");
  }

  if ((rule->actions & ACTION_ADD_HEADER) && !data->headers_added && !rule->add_headers.empty()) {
    TSMBuffer bufp;
    TSMLoc    hdr_loc;
    bool      success = false;

    if (data->direction == Direction::REQUEST) {
      // For request rules: add headers to server request (proxy request going to origin)
      if (TSHttpTxnServerReqGet(data->txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
        for (auto const &hdr : rule->add_headers) {
          std::string value = substitute_rule_name(hdr.value, rule->name);
          add_header_to_message(bufp, hdr_loc, hdr.name, value);
          Dbg(dbg_ctl, "Added header %s: %s to server request", hdr.name.c_str(), value.c_str());
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        success = true;
      }
    } else {
      // For response rules: add headers to client response
      if (TSHttpTxnClientRespGet(data->txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
        for (auto const &hdr : rule->add_headers) {
          std::string value = substitute_rule_name(hdr.value, rule->name);
          add_header_to_message(bufp, hdr_loc, hdr.name, value);
          Dbg(dbg_ctl, "Added header %s: %s to client response", hdr.name.c_str(), value.c_str());
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        success = true;
      }
    }

    if (success) {
      data->headers_added = true;
    }
  }

  if (rule->actions & ACTION_BLOCK) {
    data->blocked = true;
    TSHttpTxnStatusSet(data->txnp, TS_HTTP_STATUS_FORBIDDEN);
    // Set error body so client gets a proper response
    char const *error_body = "Blocked by content filter";
    TSHttpTxnErrorBodySet(data->txnp, TSstrdup(error_body), strlen(error_body), TSstrdup("text/plain"));
    Dbg(dbg_ctl, "Blocking request due to rule: %s", rule->name.c_str());
  }
}

/**
 * @brief Transform continuation handler for streaming body inspection.
 *
 * Processes body data in a streaming fashion, searching for patterns across
 * buffer blocks. Uses a lookback buffer to detect patterns that span block
 * boundaries.
 *
 * @note The pattern search creates a temporary string when the lookback buffer
 *       is non-empty, which involves a memory copy. This is necessary to handle
 *       patterns spanning buffer boundaries.
 *
 * @param[in] contp The transform continuation.
 * @param[in] event The event type (WRITE_READY, WRITE_COMPLETE, ERROR).
 * @param[in] edata Event data (unused).
 * @return Always returns 0.
 */
int
transform_handler(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  if (TSVConnClosedGet(contp)) {
    auto *data = static_cast<TransformData *>(TSContDataGet(contp));
    if (data) {
      if (data->output_reader) {
        TSIOBufferReaderFree(data->output_reader);
      }
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
        // Stop iterating if we've already found a match (matched_rule != nullptr)
        TSIOBufferBlock block = TSIOBufferReaderStart(reader);
        while (block != nullptr && !data->matched_rule) {
          int64_t     block_avail = 0;
          const char *block_data  = TSIOBufferBlockReadStart(block, reader, &block_avail);

          if (block_data && block_avail > 0) {
            // Two-phase search to minimize memory copying:
            //
            // Phase 1 (boundary search): When we have lookback data, create a small
            // buffer containing the lookback + first few bytes of the current block.
            // This catches patterns that span block boundaries. The copy is limited
            // to at most (2 * max_lookback) bytes.
            //
            // Phase 2 (block search): Search the remainder of the current block
            // in-place (zero-copy). This catches patterns entirely within the block
            // that weren't already covered by Phase 1.

            size_t search_offset = 0; // Where to start Phase 2 search

            // Phase 1: Boundary search (only when we have lookback data)
            // Skip if we've already found a match (matched_rule != nullptr)
            if (!data->lookback.empty() && !data->matched_rule) {
              // Create boundary buffer: lookback + enough of block to fully contain any
              // pattern that starts within the first max_lookback bytes of the block.
              // We need 2*max_lookback bytes from the block to ensure a max-length pattern
              // starting at position (max_lookback-1) is fully contained.
              size_t      boundary_extent = std::min(static_cast<size_t>(block_avail), 2 * data->config->max_lookback);
              std::string boundary_buffer;
              boundary_buffer.reserve(data->lookback.length() + boundary_extent);
              boundary_buffer = data->lookback;
              boundary_buffer.append(block_data, boundary_extent);

              // Search boundary for patterns spanning block boundaries or starting near boundary
              for (Rule const *rule : data->active_rules) {
                std::string const *matched = search_body_patterns(*rule, swoc::TextView(boundary_buffer));
                if (matched) {
                  data->matched_rule = rule;
                  execute_actions(data, rule, matched);
                  break; // Stop searching after first match
                }
              }

              // Phase 2 starts after max_lookback bytes - these are guaranteed to be fully
              // searchable in Phase 1's boundary_buffer, avoiding duplicate detection
              search_offset = std::min(static_cast<size_t>(block_avail), data->config->max_lookback);
            }

            // Phase 2: Search remainder of block in-place (zero-copy)
            // Skip if we've already found a match or bytes already covered by Phase 1
            if (!data->matched_rule && search_offset < static_cast<size_t>(block_avail)) {
              for (Rule const *rule : data->active_rules) {
                std::string const *matched = search_body_patterns(
                  *rule, swoc::TextView(block_data + search_offset, static_cast<size_t>(block_avail) - search_offset));
                if (matched) {
                  data->matched_rule = rule;
                  execute_actions(data, rule, matched);
                  break; // Stop searching after first match
                }
              }
            }

            // Update lookback buffer (only keep last max_lookback bytes)
            // Skip if we've found a match - no need to search further blocks
            if (data->config->max_lookback > 0 && !data->matched_rule) {
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
          // Blocking action - complete the transform with zero output
          // The 403 status we set will cause ATS to generate the error response
          TSVIONBytesSet(data->output_vio, 0);
          TSVIOReenable(data->output_vio);

          // Consume all remaining input
          int64_t const remaining = TSIOBufferReaderAvail(reader);
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

/**
 * @brief Create a transform continuation for body inspection.
 *
 * Allocates and initializes a TransformData structure and creates a transform
 * continuation that will process the body data.
 *
 * @param[in] txnp         The HTTP transaction.
 * @param[in] config       The plugin configuration.
 * @param[in] active_rules The rules that passed header matching and should be checked.
 * @param[in] dir          The direction (request or response) for this transform.
 * @return The transform virtual connection.
 */
TSVConn
create_transform(TSHttpTxn txnp, FilterConfig const *config, std::vector<Rule const *> const &active_rules, Direction dir)
{
  TSVConn connp = TSTransformCreate(transform_handler, txnp);

  auto *data         = new TransformData();
  data->txnp         = txnp;
  data->config       = config;
  data->active_rules = active_rules;
  data->direction    = dir;

  // Pre-allocate lookback buffer
  if (config->max_lookback > 0) {
    data->lookback.reserve(config->max_lookback);
  }

  TSContDataSet(connp, data);
  return connp;
}

/**
 * @brief Response handler for response rules.
 *
 * Called on TS_HTTP_READ_RESPONSE_HDR_HOOK to check response rules and add
 * a response transform if any rules match. Also handles TS_HTTP_TXN_CLOSE_HOOK
 * to clean up the continuation. Request rules are handled directly in TSRemapDoRemap.
 *
 * @param[in] contp The continuation (contains FilterConfig pointer).
 * @param[in] event The event type (READ_RESPONSE_HDR or TXN_CLOSE).
 * @param[in] edata The HTTP transaction.
 * @return Always returns 0.
 */
int
response_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn           txnp   = static_cast<TSHttpTxn>(edata);
  FilterConfig const *config = static_cast<FilterConfig const *>(TSContDataGet(contp));

  // Handle transaction close - clean up continuation
  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    TSContDestroy(contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  if (config == nullptr) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMBuffer bufp;
  TSMLoc    hdr_loc;

  std::vector<Rule const *> active_rules;

  if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    // Check response rules
    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }

    for (auto const &rule : config->response_rules) {
      // For response rules: check status codes and headers on response
      if (status_matches(rule, bufp, hdr_loc) && content_length_ok(rule, bufp, hdr_loc) && headers_match(rule, bufp, hdr_loc)) {
        Dbg(dbg_ctl, "Response rule '%s' header conditions matched, will inspect body", rule.name.c_str());
        active_rules.push_back(&rule);
      }
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    if (!active_rules.empty()) {
      TSVConn transform = create_transform(txnp, config, active_rules, Direction::RESPONSE);
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform);
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

/**
 * @brief Parse the YAML configuration file.
 *
 * Loads and parses the YAML configuration file, creating Rule objects for each
 * rule definition. Rules are separated into request_rules and response_rules
 * based on their direction setting. Filtering criteria are contained within a
 * 'filter' node to separate them from actions.
 *
 * @param[in] filename The configuration file path (absolute or relative to config dir).
 * @return Pointer to the parsed FilterConfig, or nullptr on error.
 */
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

    for (auto const &rule_node : root["rules"]) {
      Rule rule;

      // Name (required)
      if (rule_node["name"]) {
        rule.name = rule_node["name"].as<std::string>();
      } else {
        TSError("[%s] Rule missing 'name' field", PLUGIN_NAME);
        delete config;
        return nullptr;
      }

      // Filter node is required (contains all filtering criteria)
      YAML::Node filter_node = rule_node["filter"];
      if (!filter_node) {
        TSError("[%s] Rule '%s' missing 'filter' node", PLUGIN_NAME, rule.name.c_str());
        delete config;
        return nullptr;
      }

      // Direction (default: request) - from filter node
      if (filter_node["direction"]) {
        std::string dir = filter_node["direction"].as<std::string>();
        if (dir == "response") {
          rule.direction = Direction::RESPONSE;
        } else {
          rule.direction = Direction::REQUEST;
        }
      }

      // Actions (default: [log])
      // Supports string actions: "log", "block"
      // Supports map actions with add_header:
      //   - add_header:
      //       X-Header-Name: header-value
      //       X-Another: <rule_name>
      rule.actions = 0;
      if (rule_node["action"]) {
        for (auto const &action_node : rule_node["action"]) {
          if (action_node.IsScalar()) {
            std::string action = action_node.as<std::string>();
            if (action == "log") {
              rule.actions |= ACTION_LOG;
            } else if (action == "block") {
              rule.actions |= ACTION_BLOCK;
            }
          } else if (action_node.IsMap() && action_node["add_header"]) {
            rule.actions             |= ACTION_ADD_HEADER;
            auto const &headers_node  = action_node["add_header"];
            for (auto const &hdr : headers_node) {
              AddHeader add_hdr;
              add_hdr.name  = hdr.first.as<std::string>();
              add_hdr.value = hdr.second.as<std::string>();
              rule.add_headers.push_back(add_hdr);
            }
          }
        }
      }
      if (rule.actions == 0) {
        rule.actions = ACTION_LOG; // default
      }

      // Methods (for request rules) - from filter node
      if (filter_node["methods"]) {
        for (auto const &method_node : filter_node["methods"]) {
          rule.methods.push_back(method_node.as<std::string>());
        }
      }

      // Status codes (for response rules) - from filter node
      if (filter_node["status"]) {
        for (auto const &status_node : filter_node["status"]) {
          rule.status_codes.push_back(status_node.as<int>());
        }
      }

      // Validate method/status usage
      if (rule.direction == Direction::REQUEST && !rule.status_codes.empty()) {
        TSError("[%s] Rule '%s': 'status' is only valid for response rules", PLUGIN_NAME, rule.name.c_str());
        delete config;
        return nullptr;
      }
      if (rule.direction == Direction::RESPONSE && !rule.methods.empty()) {
        TSError("[%s] Rule '%s': 'methods' is only valid for request rules", PLUGIN_NAME, rule.name.c_str());
        delete config;
        return nullptr;
      }

      // Max content length - from filter node
      if (filter_node["max_content_length"]) {
        rule.max_content_length = filter_node["max_content_length"].as<int64_t>();
      }

      // Header conditions - from filter node
      if (filter_node["headers"]) {
        for (auto const &header_node : filter_node["headers"]) {
          HeaderCondition cond;
          if (header_node["name"]) {
            cond.name = header_node["name"].as<std::string>();
          }
          if (header_node["patterns"]) {
            for (auto const &pattern_node : header_node["patterns"]) {
              cond.patterns.push_back(pattern_node.as<std::string>());
            }
          }
          rule.headers.push_back(cond);
        }
      }

      // Body patterns - from filter node
      if (filter_node["body_patterns"]) {
        for (auto const &pattern_node : filter_node["body_patterns"]) {
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

      // Create a metrics counter for this rule
      std::string stat_name = std::string("plugin.") + PLUGIN_NAME + ".rule." + rule.name + ".matches";
      rule.stat_id          = TSStatCreate(stat_name.c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
      if (rule.stat_id == TS_ERROR) {
        TSError("[%s] Failed to create stat '%s'", PLUGIN_NAME, stat_name.c_str());
        delete config;
        return nullptr;
      }
      Dbg(dbg_ctl, "Created stat '%s' with id %d", stat_name.c_str(), rule.stat_id);

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
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size)
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

  *instance = config;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  auto *config = static_cast<FilterConfig *>(instance);
  delete config;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri ATS_UNUSED)
{
  auto *config = static_cast<FilterConfig *>(instance);
  if (config == nullptr) {
    return TSREMAP_NO_REMAP;
  }

  // For request rules, check headers now (in TSRemapDoRemap, headers are already available)
  if (!config->request_rules.empty()) {
    TSMBuffer bufp;
    TSMLoc    hdr_loc;

    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      std::vector<Rule const *> active_rules;

      for (auto const &rule : config->request_rules) {
        if (method_matches(rule, bufp, hdr_loc) && content_length_ok(rule, bufp, hdr_loc) && headers_match(rule, bufp, hdr_loc)) {
          Dbg(dbg_ctl, "Request rule '%s' header conditions matched, will inspect body", rule.name.c_str());
          active_rules.push_back(&rule);
        }
      }

      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

      if (!active_rules.empty()) {
        TSVConn transform = create_transform(txnp, config, active_rules, Direction::REQUEST);
        TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, transform);
      }
    }
  }

  // For response rules, add a hook to check when response headers arrive
  if (!config->response_rules.empty()) {
    TSCont contp = TSContCreate(response_handler, nullptr);
    TSContDataSet(contp, config);
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    // Add TXN_CLOSE_HOOK to clean up the continuation
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);
  }

  return TSREMAP_NO_REMAP;
}
