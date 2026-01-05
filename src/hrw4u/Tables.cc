/*
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

#include "hrw4u/Tables.h"
#include <cctype>

namespace hrw4u
{
hrw::OperatorType
ResolveResult::get_operator_type(bool is_append, bool is_remove) const
{
  if (op_prefix == OperatorPrefix::NONE) {
    return op_type;
  }

  if (is_remove) {
    if (op_type == hrw::OperatorType::SET_HEADER || op_type == hrw::OperatorType::ADD_HEADER) {
      return hrw::OperatorType::RM_HEADER;
    }
    if (op_type == hrw::OperatorType::SET_COOKIE || op_type == hrw::OperatorType::ADD_COOKIE) {
      return hrw::OperatorType::RM_COOKIE;
    }
    if (op_type == hrw::OperatorType::SET_DESTINATION) {
      return hrw::OperatorType::RM_DESTINATION;
    }
  }

  if (is_append) {
    if (op_type == hrw::OperatorType::SET_HEADER) {
      return hrw::OperatorType::ADD_HEADER;
    }
    if (op_type == hrw::OperatorType::SET_COOKIE) {
      return hrw::OperatorType::ADD_COOKIE;
    }
  }

  return op_type;
}

namespace
{
  std::string
  generate_condition_target(hrw::ConditionType type, bool wrap_in_braces = true)
  {
    auto name = hrw::condition_type_name(type);
    if (name.empty()) {
      return "";
    }
    if (wrap_in_braces) {
      return "%{" + std::string(name) + "}";
    }
    return std::string(name);
  }

  // HTTP_SECTIONS: All hooks where HTTP transaction data is available (excludes TXN_START/TXN_CLOSE)
  const SectionSet HTTP_SECTIONS = {SectionType::PRE_REMAP,    SectionType::REMAP,         SectionType::READ_REQUEST,
                                    SectionType::SEND_REQUEST, SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE};

} // namespace

SymbolResolver::SymbolResolver()
{
  _operator_map = {
    {"http.cntl.",
     {.sections              = HTTP_SECTIONS,
      .upper                 = true,
      .suffix_group          = SuffixGroup::HTTP_CNTL_FIELDS,
      .has_suffix_validation = true,
      .op_prefix             = OperatorPrefix::NONE,
      .op_type               = hrw::OperatorType::SET_HTTP_CNTL}                                                                       },
    {"http.status.reason",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_STATUS_REASON}                   },
    {"http.status",            {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_STATUS}},
    {"inbound.conn.dscp",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_CONN_DSCP}                       },
    {"inbound.conn.mark",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_CONN_MARK}                       },
    {"outbound.conn.dscp",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST},
      .op_prefix = OperatorPrefix::NONE,
      .op_type   = hrw::OperatorType::SET_CONN_DSCP}                                                                                   },
    {"outbound.conn.mark",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST},
      .op_prefix = OperatorPrefix::NONE,
      .op_type   = hrw::OperatorType::SET_CONN_MARK}                                                                                   },
    {"inbound.cookie.",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::SET_ADD_RM, .op_type = hrw::OperatorType::SET_COOKIE}                    },
    {"inbound.req.",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::SET_ADD_RM, .op_type = hrw::OperatorType::SET_HEADER}                    },
    {"inbound.resp.body",      {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_BODY}  },
    {"inbound.resp.",
     {.sections  = {SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE, SectionType::TXN_CLOSE},
      .op_prefix = OperatorPrefix::SET_ADD_RM,
      .op_type   = hrw::OperatorType::SET_HEADER}                                                                                      },
    {"inbound.status.reason",
     {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_STATUS_REASON}                   },
    {"inbound.status",         {.sections = HTTP_SECTIONS, .op_prefix = OperatorPrefix::NONE, .op_type = hrw::OperatorType::SET_STATUS}},
    {"inbound.url.",
     {.sections              = HTTP_SECTIONS,
      .upper                 = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .op_prefix             = OperatorPrefix::SET_RM,
      .op_type               = hrw::OperatorType::SET_DESTINATION}                                                                     },
    {"outbound.cookie.",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .op_prefix = OperatorPrefix::SET_ADD_RM,
      .op_type   = hrw::OperatorType::SET_COOKIE}                                                                                      },
    {"outbound.req.",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST},
      .op_prefix = OperatorPrefix::SET_ADD_RM,
      .op_type   = hrw::OperatorType::SET_HEADER}                                                                                      },
    {"outbound.resp.",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .op_prefix = OperatorPrefix::SET_ADD_RM,
      .op_type   = hrw::OperatorType::SET_HEADER}                                                                                      },
    {"outbound.status.reason",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .op_prefix = OperatorPrefix::NONE,
      .op_type   = hrw::OperatorType::SET_STATUS_REASON}                                                                               },
    {"outbound.status",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .op_prefix = OperatorPrefix::NONE,
      .op_type   = hrw::OperatorType::SET_STATUS}                                                                                      },
    {"outbound.url.",
     {.upper                 = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST},
      .op_prefix             = OperatorPrefix::SET_RM,
      .op_type               = hrw::OperatorType::SET_DESTINATION}                                                                     },
  };

  _condition_map = {
    {"inbound.ip",                     {.target = "%{IP:CLIENT}", .cond_type = hrw::ConditionType::COND_IP}                            },
    {"inbound.method",                 {.sections = HTTP_SECTIONS, .cond_type = hrw::ConditionType::COND_METHOD}                       },
    {"inbound.server",                 {.target = "%{IP:INBOUND}", .cond_type = hrw::ConditionType::COND_IP}                           },
    {"inbound.status",                 {.sections = HTTP_SECTIONS, .cond_type = hrw::ConditionType::COND_STATUS}                       },
    {"now",                            {.cond_type = hrw::ConditionType::COND_NOW}                                                     },
    {"outbound.ip",
     {.target    = "%{IP:SERVER}",
      .sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type = hrw::ConditionType::COND_IP}                                                                                        },
    {"outbound.method",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST},
      .cond_type = hrw::ConditionType::COND_METHOD}                                                                                    },
    {"outbound.server",
     {.target    = "%{IP:OUTBOUND}",
      .sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type = hrw::ConditionType::COND_IP}                                                                                        },
    {"outbound.status",
     {.sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type = hrw::ConditionType::COND_STATUS}                                                                                    },
    {"tcp.info",                       {.cond_type = hrw::ConditionType::COND_TCP_INFO}                                                },
    {"capture.",                       {.prefix = true, .cond_type = hrw::ConditionType::COND_LAST_CAPTURE}                            },
    {"from.url.",
     {.sections              = HTTP_SECTIONS,
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_FROM_URL}                                                                      },
    {"geo.",
     {.upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::GEO_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_GEO}                                                                           },
    {"http.cntl.",
     {.sections              = HTTP_SECTIONS,
      .upper                 = true,
      .suffix_group          = SuffixGroup::HTTP_CNTL_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_HTTP_CNTL}                                                                     },
    {"id.",
     {.upper                 = true,
      .suffix_group          = SuffixGroup::ID_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_ID}                                                                            },
    {"inbound.conn.client-cert.SAN.",
     {.target                = "INBOUND:CLIENT-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.server-cert.SAN.",
     {.target                = "INBOUND:SERVER-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.client-cert.san.",
     {.target                = "INBOUND:CLIENT-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.server-cert.san.",
     {.target                = "INBOUND:SERVER-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.client-cert.",
     {.target                = "INBOUND:CLIENT-CERT",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CERT_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.server-cert.",
     {.target                = "INBOUND:SERVER-CERT",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CERT_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.conn.",
     {.target                = "INBOUND",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CONN_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"inbound.cookie.",                {.sections = HTTP_SECTIONS, .prefix = true, .cond_type = hrw::ConditionType::COND_COOKIE}       },
    {"inbound.req.",                   {.sections = HTTP_SECTIONS, .prefix = true, .cond_type = hrw::ConditionType::COND_CLIENT_HEADER}},
    {"inbound.resp.",
     {.sections  = {SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE, SectionType::TXN_CLOSE},
      .prefix    = true,
      .cond_type = hrw::ConditionType::COND_HEADER}                                                                                    },
    {"inbound.url.",
     {.sections              = HTTP_SECTIONS,
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_CLIENT_URL}                                                                    },
    {"now.",
     {.upper                 = true,
      .suffix_group          = SuffixGroup::DATE_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_NOW}                                                                           },
    {"outbound.conn.client-cert.SAN.",
     {.target                = "OUTBOUND:CLIENT-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.server-cert.SAN.",
     {.target                = "OUTBOUND:SERVER-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.client-cert.san.",
     {.target                = "OUTBOUND:CLIENT-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.server-cert.san.",
     {.target                = "OUTBOUND:SERVER-CERT:SAN",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::SAN_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.client-cert.",
     {.target                = "OUTBOUND:CLIENT-CERT",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CERT_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.server-cert.",
     {.target                = "OUTBOUND:SERVER-CERT",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CERT_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.conn.",
     {.target                = "OUTBOUND",
      .upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::CONN_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                                SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type             = hrw::ConditionType::COND_INBOUND}                                                                       },
    {"outbound.cookie.",
     {.prefix    = true,
      .sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type = hrw::ConditionType::COND_COOKIE}                                                                                    },
    {"outbound.req.",
     {.prefix    = true,
      .sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST},
      .cond_type = hrw::ConditionType::COND_HEADER}                                                                                    },
    {"outbound.resp.",
     {.prefix    = true,
      .sections  = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST,
                    SectionType::READ_RESPONSE, SectionType::SEND_RESPONSE},
      .cond_type = hrw::ConditionType::COND_HEADER}                                                                                    },
    {"outbound.url.",
     {.upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .sections              = {SectionType::PRE_REMAP, SectionType::REMAP, SectionType::READ_REQUEST, SectionType::SEND_REQUEST},
      .cond_type             = hrw::ConditionType::COND_NEXT_HOP}                                                                      },
    {"to.url.",
     {.upper                 = true,
      .prefix                = true,
      .suffix_group          = SuffixGroup::URL_FIELDS,
      .has_suffix_validation = true,
      .cond_type             = hrw::ConditionType::COND_TO_URL}                                                                        },
  };

  _function_map = {
    {"access",        {.bare = true, .cond_type = hrw::ConditionType::COND_ACCESS}                },
    {"cache",         {.bare = true, .cond_type = hrw::ConditionType::COND_CACHE}                 },
    {"cidr",          {.bare = true, .cond_type = hrw::ConditionType::COND_CIDR}                  },
    {"internal",      {.bare = true, .cond_type = hrw::ConditionType::COND_INTERNAL_TXN}          },
    {"random",        {.bare = true, .cond_type = hrw::ConditionType::COND_RANDOM}                },
    {"ssn-txn-count", {.bare = true, .cond_type = hrw::ConditionType::COND_SESSION_TRANSACT_COUNT}},
    {"txn-count",     {.bare = true, .cond_type = hrw::ConditionType::COND_TRANSACT_COUNT}        },
  };

  _statement_function_map = {
    {"add-header",            {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::ADD_HEADER}                                      },
    {"counter",               {.op_type = hrw::OperatorType::COUNTER}                                                                    },
    {"set-debug",             {.op_type = hrw::OperatorType::SET_DEBUG}                                                                  },
    {"no-op",                 {.op_type = hrw::OperatorType::NO_OP}                                                                      },
    {"remove_query",          {.target = "QUERY", .sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::RM_DESTINATION}               },
    {"keep_query",            {.target = "QUERY", .sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::RM_DESTINATION}               },
    {"run-plugin",            {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::RUN_PLUGIN}                                      },
    {"set-body-from",         {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_BODY_FROM}                                   },
    {"set-cc-alg",            {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_CC_ALG}                                      },
    {"set-config",            {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_CONFIG}                                      },
    {"set-effective-address", {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_EFFECTIVE_ADDRESS}                           },
    {"set-redirect",          {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_REDIRECT}                                    },
    {"skip-remap",            {.sections = {SectionType::PRE_REMAP, SectionType::READ_REQUEST}, .op_type = hrw::OperatorType::SKIP_REMAP}},
    {"set-plugin-cntl",       {.sections = HTTP_SECTIONS, .op_type = hrw::OperatorType::SET_PLUGIN_CNTL}                                 },
  };

  _hook_map = {
    {"read_request",  SectionType::READ_REQUEST },
    {"send_request",  SectionType::SEND_REQUEST },
    {"read_response", SectionType::READ_RESPONSE},
    {"send_response", SectionType::SEND_RESPONSE},
    {"pre_remap",     SectionType::PRE_REMAP    },
    {"post_remap",    SectionType::POST_REMAP   },
    {"remap",         SectionType::REMAP        },
    {"txn_start",     SectionType::TXN_START    },
    {"txn_close",     SectionType::TXN_CLOSE    },
  };

  _var_type_map = {
    {"bool",    VarType::BOOL },
    {"boolean", VarType::BOOL },
    {"int8",    VarType::INT8 },
    {"int16",   VarType::INT16},
  };
}

ResolveResult
SymbolResolver::resolve_in_table(std::string_view symbol, const std::unordered_map<std::string, MapParams> &table,
                                 SectionType section) const
{
  ResolveResult result;
  std::string   symbol_str(symbol);
  auto          it = table.find(symbol_str);

  if (it != table.end()) {
    if (!it->second.valid_for_section(section)) {
      result.error_message = "symbol '" + symbol_str + "' not valid in section " + std::string(section_type_to_string(section));
      return result;
    }
    if (it->second.target.empty()) {
      if (it->second.cond_type != hrw::ConditionType::NONE) {
        result.target = generate_condition_target(it->second.cond_type, !it->second.bare);
      }
    } else {
      result.target = it->second.target;
    }
    result.op_prefix = it->second.op_prefix;
    result.cond_type = it->second.cond_type;
    result.op_type   = it->second.op_type;
    result.success   = true;

    return result;
  }

  std::string_view longest_prefix;
  const MapParams *longest_params = nullptr;

  for (const auto &[prefix, params] : table) {
    if (prefix.back() == '.' && symbol.size() >= prefix.size() && symbol.substr(0, prefix.size()) == prefix) {
      if (prefix.size() > longest_prefix.size()) {
        longest_prefix = prefix;
        longest_params = &params;
      }
    }
  }

  if (longest_params) {
    if (!longest_params->valid_for_section(section)) {
      result.error_message = "symbol '" + symbol_str + "' not valid in section " + std::string(section_type_to_string(section));

      return result;
    }

    std::string suffix(symbol.substr(longest_prefix.size()));

    if (longest_params->has_suffix_validation && !validate_suffix(longest_params->suffix_group, suffix)) {
      result.error_message = "invalid suffix '" + suffix + "' for " + std::string(longest_prefix);

      return result;
    }

    if (longest_params->upper) {
      suffix = to_upper(suffix);
    }

    if (longest_params->target.empty()) {
      if (longest_params->cond_type != hrw::ConditionType::NONE) {
        result.target = generate_condition_target(longest_params->cond_type, false);
      }
    } else {
      result.target = longest_params->target;
    }
    result.suffix    = suffix;
    result.op_prefix = longest_params->op_prefix;
    result.cond_type = longest_params->cond_type;
    result.op_type   = longest_params->op_type;
    result.prefix    = longest_params->prefix;
    result.success   = true;
    return result;
  }
  result.error_message = "unknown symbol: " + symbol_str;

  return result;
}

ResolveResult
SymbolResolver::resolve_operator(std::string_view symbol, SectionType section) const
{
  return resolve_in_table(symbol, _operator_map, section);
}

ResolveResult
SymbolResolver::resolve_condition(std::string_view symbol, SectionType section) const
{
  return resolve_in_table(symbol, _condition_map, section);
}

ResolveResult
SymbolResolver::resolve_function(std::string_view name, SectionType section) const
{
  return resolve_in_table(name, _function_map, section);
}

ResolveResult
SymbolResolver::resolve_statement_function(std::string_view name, SectionType section) const
{
  return resolve_in_table(name, _statement_function_map, section);
}

std::optional<SectionType>
SymbolResolver::resolve_hook(std::string_view name) const
{
  auto it = _hook_map.find(to_lower(name));

  if (it != _hook_map.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::optional<VarType>
SymbolResolver::resolve_var_type(std::string_view name) const
{
  auto it = _var_type_map.find(to_lower(name));

  if (it != _var_type_map.end()) {
    return it->second;
  }

  return std::nullopt;
}

const MapParams *
SymbolResolver::get_operator_params(std::string_view prefix) const
{
  std::string key(prefix);
  auto        it = _operator_map.find(key);

  if (it != _operator_map.end()) {
    return &it->second;
  }

  return nullptr;
}

const MapParams *
SymbolResolver::get_condition_params(std::string_view prefix) const
{
  std::string key(prefix);
  auto        it = _condition_map.find(key);

  if (it != _condition_map.end()) {
    return &it->second;
  }

  return nullptr;
}

const SymbolResolver &
symbol_resolver()
{
  static SymbolResolver instance;
  return instance;
}

} // namespace hrw4u
