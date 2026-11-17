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
//////////////////////////////////////////////////////////////////////////////////////////////
// factory.cc: Factory functions for operators, conditions and condition variables.
//
//
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "operators.h"
#include "conditions.h"
#include "conditions_geo.h"

namespace
{
// Tables rather than if-chains so the set of names is plain data, which tools/hrw4u's coverage test reads.
using OperatorEntry  = std::pair<std::string_view, Operator *(*)()>;
using ConditionEntry = std::pair<std::string_view, Condition *(*)()>;

constexpr OperatorEntry OPERATORS[] = {
  {"rm-header",             []() -> Operator * { return new OperatorRMHeader(); }                     },
  {"set-header",            []() -> Operator * { return new OperatorSetHeader(); }                    },
  {"add-header",            []() -> Operator * { return new OperatorAddHeader(); }                    },
  {"set-config",            []() -> Operator * { return new OperatorSetConfig(); }                    },
  {"set-status",            []() -> Operator * { return new OperatorSetStatus(); }                    },
  {"set-status-reason",     []() -> Operator * { return new OperatorSetStatusReason(); }              },
  {"set-destination",       []() -> Operator * { return new OperatorSetDestination(); }               },
  {"rm-destination",        []() -> Operator * { return new OperatorRMDestination(); }                },
  {"set-redirect",          []() -> Operator * { return new OperatorSetRedirect(); }                  },
  {"timeout-out",           []() -> Operator * { return new OperatorSetTimeoutOut(); }                },
  {"skip-remap",            []() -> Operator * { return new OperatorSkipRemap(); }                    },
  {"no-op",                 []() -> Operator * { return new OperatorNoOp(); }                         },
  {"counter",               []() -> Operator * { return new OperatorCounter(); }                      },
  {"rm-cookie",             []() -> Operator * { return new OperatorRMCookie(); }                     },
  {"set-cookie",            []() -> Operator * { return new OperatorSetCookie(); }                    },
  {"add-cookie",            []() -> Operator * { return new OperatorAddCookie(); }                    },
  {"set-conn-dscp",         []() -> Operator * { return new OperatorSetConnDSCP(); }                  },
  {"set-conn-mark",         []() -> Operator * { return new OperatorSetConnMark(); }                  },
  {"set-debug",             []() -> Operator * { return new OperatorSetDebug(); }                     },
  {"set-body",              []() -> Operator * { return new OperatorSetBody(); }                      },
  {"set-body-from-file",    []() -> Operator * { return new OperatorSetBodyFromFile(); }              },
  {"set-http-cntl",         []() -> Operator * { return new OperatorSetHttpCntl(); }                  },
  {"set-plugin-cntl",       []() -> Operator * { return new OperatorSetPluginCntl(); }                },
  {"run-plugin",            []() -> Operator * { return new OperatorRunPlugin(); }                    },
  {"set-body-from",         []() -> Operator * { return new OperatorSetBodyFrom(); }                  },
  {"set-state-flag",        []() -> Operator * { return new OperatorSetStateFlag(); }                 },
  {"set-state-int8",        []() -> Operator * { return new OperatorSetStateInt8(); }                 },
  {"set-state-int16",       []() -> Operator * { return new OperatorSetStateInt16(); }                },
  {"set-session-flag",      []() -> Operator * { return new OperatorSetStateFlag(TS_USER_ARGS_SSN); } },
  {"set-session-int8",      []() -> Operator * { return new OperatorSetStateInt8(TS_USER_ARGS_SSN); } },
  {"set-session-int16",     []() -> Operator * { return new OperatorSetStateInt16(TS_USER_ARGS_SSN); }},
  {"set-effective-address", []() -> Operator * { return new OperatorSetEffectiveAddress(); }          },
  {"set-next-hop-strategy", []() -> Operator * { return new OperatorSetNextHopStrategy(); }           },
  {"set-cc-alg",            []() -> Operator * { return new OperatorSetCCAlgorithm(); }               },
};

constexpr ConditionEntry CONDITIONS[] = {
  {"TRUE",                 []() -> Condition * { return new ConditionTrue(); }                         },
  {"FALSE",                []() -> Condition * { return new ConditionFalse(); }                        },
  {"STATUS",               []() -> Condition * { return new ConditionStatus(); }                       },
  {"RANDOM",               []() -> Condition * { return new ConditionRandom(); }                       },
  {"ACCESS",               []() -> Condition * { return new ConditionAccess(); }                       },
  {"COOKIE",               []() -> Condition * { return new ConditionCookie(); }                       },
  // HEADER, CLIENT-URL and NEXT-HOP adapt to the hook.
  {"HEADER",               []() -> Condition * { return new ConditionHeader(); }                       },
  {"CLIENT-HEADER",        []() -> Condition * { return new ConditionHeader(ConditionHeader::CLIENT); }},
  {"SERVER-HEADER",        []() -> Condition * { return new ConditionHeader(ConditionHeader::SERVER); }},
  {"CLIENT-URL",           []() -> Condition * { return new ConditionUrl(ConditionUrl::CLIENT); }      },
  {"SERVER-URL",           []() -> Condition * { return new ConditionUrl(ConditionUrl::SERVER); }      },
  {"URL",                  []() -> Condition * { return new ConditionUrl(ConditionUrl::URL); }         },
  {"FROM-URL",             []() -> Condition * { return new ConditionUrl(ConditionUrl::FROM); }        },
  {"TO-URL",               []() -> Condition * { return new ConditionUrl(ConditionUrl::TO); }          },
  {"DBM",                  []() -> Condition * { return new ConditionDBM(); }                          },
  {"INTERNAL-TRANSACTION", []() -> Condition * { return new ConditionInternalTxn(); }                  },
  {"INTERNAL-TXN",         []() -> Condition * { return new ConditionInternalTxn(); }                  },
  {"IP",                   []() -> Condition * { return new ConditionIp(); }                           },
  {"METHOD",               []() -> Condition * { return new ConditionMethod(); }                       },
  {"TXN-COUNT",            []() -> Condition * { return new ConditionTransactCount(); }                },
  {"NOW",                  []() -> Condition * { return new ConditionNow(); }                          },
  {"GEO",
   []() -> Condition * {
#if TS_USE_HRW_GEOIP
     return new GeoIPConditionGeo();
#elif TS_USE_HRW_MAXMINDDB
     return new MMConditionGeo();
#else
     return new ConditionGeo();
#endif
   }                                                                                                   },
  {"ID",                   []() -> Condition * { return new ConditionId(); }                           },
  {"CIDR",                 []() -> Condition * { return new ConditionCidr(); }                         },
  {"INBOUND",              []() -> Condition * { return new ConditionInbound(); }                      },
  {"SSN-TXN-COUNT",        []() -> Condition * { return new ConditionSessionTransactCount(); }         },
  {"TCP-INFO",             []() -> Condition * { return new ConditionTcpInfo(); }                      },
  {"CACHE",                []() -> Condition * { return new ConditionCache(); }                        },
  {"NEXT-HOP",             []() -> Condition * { return new ConditionNextHop(); }                      },
  {"HTTP-CNTL",            []() -> Condition * { return new ConditionHttpCntl(); }                     },
  {"GROUP",                []() -> Condition * { return new ConditionGroup(); }                        },
  {"STATE-FLAG",           []() -> Condition * { return new ConditionStateFlag(); }                    },
  {"STATE-INT8",           []() -> Condition * { return new ConditionStateInt8(); }                    },
  {"STATE-INT16",          []() -> Condition * { return new ConditionStateInt16(); }                   },
  {"SESSION-FLAG",         []() -> Condition * { return new ConditionStateFlag(TS_USER_ARGS_SSN); }    },
  {"SESSION-INT8",         []() -> Condition * { return new ConditionStateInt8(TS_USER_ARGS_SSN); }    },
  {"SESSION-INT16",        []() -> Condition * { return new ConditionStateInt16(TS_USER_ARGS_SSN); }   },
  {"LAST-CAPTURE",         []() -> Condition * { return new ConditionLastCapture(); }                  },
};

} // namespace

Operator *
operator_factory(const std::string &op)
{
  // Note that we don't support the OperatorIf() pseudo-operator here!
  auto it = std::find_if(std::begin(OPERATORS), std::end(OPERATORS), [&op](const OperatorEntry &e) { return e.first == op; });

  if (it == std::end(OPERATORS)) {
    TSError("[%s] Unknown operator: %s", PLUGIN_NAME, op.c_str());
    return nullptr;
  }
  return it->second();
}

Condition *
condition_factory(const std::string &cond)
{
  std::string_view       c_name = cond;
  std::string            c_qual;
  std::string::size_type pos = cond.find_first_of(':');

  if (pos != std::string::npos) {
    c_name = c_name.substr(0, pos);
    c_qual = cond.substr(pos + 1);
  }

  auto it =
    std::find_if(std::begin(CONDITIONS), std::end(CONDITIONS), [c_name](const ConditionEntry &e) { return e.first == c_name; });

  if (it == std::end(CONDITIONS)) {
    TSError("[%s] Unknown condition %.*s", PLUGIN_NAME, static_cast<int>(c_name.size()), c_name.data());
    return nullptr;
  }

  Condition *c = it->second();

  if (c_qual != "") {
    c->set_qualifier(c_qual);
  }

  return c;
}
