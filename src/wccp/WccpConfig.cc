/** @file WCCP Configuration processing.

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

#include "WccpLocal.h"

#include <sstream>
#include <arpa/inet.h>
#include <iostream>
#include <errno.h>
#include <stdlib.h>

#include "swoc/Lexicon.h"

#include <yaml-cpp/yaml.h>

using swoc::TextView;
using swoc::Errata;

using namespace swoc::literals;

namespace wccp
{
swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, ServiceGroup::Type type)
{
  return bwformat(w, spec, type == wccp::ServiceGroup::STANDARD ? "STANDARD"_tv : "DYNAMIC"_tv);
}
} // namespace wccp

namespace YAML
{
swoc::Lexicon<NodeType::value> YamlNodeTypeNames{
  {{NodeType::Undefined, "Undefined"},
   {NodeType::Null, "NULL"},
   {NodeType::Scalar, "Scalar"},
   {NodeType::Sequence, "Sequence"},
   {NodeType::Map, "Map"}},
  "Unknown"
};

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, NodeType::value v)
{
  return bwformat(w, spec, YamlNodeTypeNames[v]);
}

} // namespace YAML

// WCCP related things that are file local.
namespace
{
using namespace wccp;

// Scratch global list of seed router addresses.
// Yeah, not thread safe, but it's just during configuration load.
// Logic somewhat changed - can we move in to the load method?
std::vector<uint32_t> Seed_Router;

// Names used for various elements and properties.
static constexpr TextView SVC_NAME = "service";

const std::string SVCS_NAME = "services";
const std::string WCCP_NAME = "wccp";

static constexpr TextView SVC_PROP_ID           = "id";
static const char *const SVC_PROP_TYPE          = "type";
static constexpr TextView SVC_PROP_PRIORITY     = "priority";
static constexpr TextView SVC_PROP_PROTOCOL     = "protocol";
static constexpr TextView SVC_PROP_PRIMARY_HASH = "primary-hash";
static constexpr TextView SVC_PROP_ALT_HASH     = "alt-hash";
static constexpr TextView SVC_PROP_PORTS        = "ports";
static const char *const SVC_PROP_PORT_TYPE     = "port-type";
static constexpr TextView SVC_PROP_SECURITY     = "security";
static const char *const SVC_PROP_ROUTERS       = "routers";
static constexpr TextView SVC_PROP_FORWARD      = "forward";
static constexpr TextView SVC_PROP_RETURN       = "return";
static constexpr TextView SVC_PROP_ASSIGN       = "assignment";
static const char *const SVC_PROP_PROC          = "proc-name";

static constexpr TextView SECURITY_PROP_OPTION = "option";
static constexpr TextView SECURITY_PROP_KEY    = "key";

/// Helper structure for processing configuration strings.
struct CfgString {
  std::string m_text; ///< Text value of the option.
  bool m_found;       ///< String was found.
};
using CfgOpts = std::vector<CfgString>;

#define N_OPTS(x) (sizeof(x) / sizeof(*x))

CfgString FORWARD_OPTS[] = {
  {"gre", false},
  {"l2",  false}
};
size_t const N_FORWARD_OPTS = sizeof(FORWARD_OPTS) / sizeof(*FORWARD_OPTS);

CfgString RETURN_OPTS[] = {
  {"gre", false},
  {"l2",  false}
};
size_t const N_RETURN_OPTS = sizeof(RETURN_OPTS) / sizeof(*RETURN_OPTS);

CfgString ASSIGN_OPTS[] = {
  {"hash", false},
  {"mask", false}
};

CfgString HASH_OPTS[] = {
  {"src_ip",   false},
  {"dst_ip",   false},
  {"src_port", false},
  {"dst_port", false}
};

inline Errata
Unable_To_Create_Service_Group(int line)
{
  return Errata(ec_for(EINVAL), ERRATA_FATAL, "Unable to create service group at line {} of configuration.", line);
}

inline Errata
Services_Not_Found()
{
  Errata errata;
  errata.note(ERRATA_NOTE, "No services found in configuration.");
  return errata;
}

inline Errata
Services_Not_A_Sequence()
{
  return Errata(ec_for(EINVAL), ERRATA_NOTE, "The 'services' setting was not a list nor array.");
}

inline Errata &
Note_Service_Type_Defaulted(Errata &errata, wccp::ServiceGroup::Type type, int line)
{
  return errata.note(ERRATA_NOTE, "'type' keyword not found in {} at line {} -- defaulting to {}", SVC_NAME, line, type);
}

inline Errata &
Note_Service_Type_Invalid(Errata &errata, TextView text, int line)
{
  return errata.note(ERRATA_WARN, R"(Service type "{}" at line {} is invalid. Must be "STANDARD" or "DYNAMIC".)");
}

inline Errata &
Note_Prop_Not_Found(Errata &errata, TextView prop_name, TextView group_name, int line)
{
  return errata.note(ERRATA_WARN, R"(Required property "{}" not found in "{}" at line {})", prop_name, group_name, line);
}

inline Errata
Prop_Not_Found(TextView prop_name, TextView group_name, int line)
{
  Errata errata;
  Note_Prop_Not_Found(errata, prop_name, group_name, line);
  return errata;
}

inline Errata
Prop_Invalid_Type(TextView prop_name, const YAML::Node &prop_cfg, YAML::NodeType::value expected)
{
  return {ec_for(EINVAL), ERRATA_WARN,     R"("{}" is not of type "{}" instead of required type "{}".)",
          prop_name,      prop_cfg.Type(), expected};
}

inline swoc::Errata &
Note_Svc_Prop_Out_Of_Range(Errata &errata, TextView name, int line, int v, int min, int max)
{
  return errata.note(ERRATA_WARN,
                     R"(Service property "{}" at line {} has a value "{}" that is not in the allowed range of {}..{} .)", name,
                     line, v, min, max);
}

inline Errata &
Note_Svc_Prop_Ignored(Errata &errata, TextView name, int line)
{
  errata.note(ERRATA_NOTE, R"(Service property "{}" at line {} ignored because the service is of type standard.)", name, line);
  return errata;
}

inline Errata &
Note_Svc_Ports_Too_Many(Errata &errata, int line, int n)
{
  errata.note(ERRATA_NOTE, "Excess ports ignored at line {}. {} ports specified, only {} supported.", line, n,
              wccp::ServiceGroup::N_PORTS);
  return errata;
}

inline Errata &
Note_Svc_Ports_None_Valid(Errata &errata, int line)
{
  errata.note(ERRATA_WARN, R"(A "{}" property was found at line {} but none of the ports were valid.)", SVC_PROP_PORTS, line);
  return errata;
}

inline Errata &
Note_Svc_Ports_Not_Found(Errata &errata, int line)
{
  return errata.note("Ports not found in service at line {]. Ports must be defined for a dynamic service.", line);
}

Errata &
Note_Svc_Prop_Ignored_In_Standard(Errata &errata, TextView name, int line)
{
  return errata.note(R"(Service property "{}" at line {} ignored because the service is of type STANDARD.)", name, line);
}

inline Errata
Security_Opt_Invalid(TextView text, int line)
{
  return {ERRATA_WARN, R"(Security option "{}" at line {} is invalid. It must be 'none' or 'md5'.)", text};
}

inline Errata &
Note_Value_Malformed(Errata &errata, const std::string &name, const std::string &text, int line)
{
  return errata.note(ERRATA_WARN, R"("{}" value "{}" malformed at line {})", name, text, line);
}

inline Errata &
Note_No_Valid_Routers(Errata &errata, int line)
{
  return errata.note(ERRATA_WARN, "No valid IP address for routers found for Service Group at line {}.", line);
}

inline Errata &
Note_Ignored_Option_Value(Errata &errata, TextView text, TextView name, int line)
{
  return errata.note(ERRATA_NOTE, "Value \"{}\" at line {} was ignored because it is not a valid option for \"{}\".", text, line,
                     name);
}

inline Errata &
Note_Ignored_Opt_Errors(Errata &errata, TextView name, int line)
{
  return errata.note(ERRATA_NOTE, "Errors in \"{}\" were ignored.", name);
}

Errata &
Note_List_Valid_Opts(Errata &errata, TextView name, int line, CfgString *values, size_t n)
{
  swoc::LocalBufferWriter<2048> w;
  w.print("Valid values for the \"{}\" property at line {} are:", name, line);
  for (size_t i = 0; i < n; ++i) {
    w.print("{}", values[i].m_text);
  }
  return errata.note(ERRATA_NOTE, w.view());
}

inline Errata &
Note_Port_Type_Invalid(Errata &errata, TextView text, int line)
{
  return errata.note(ERRATA_WARN, R"(Value "{}" at line {} for property "{}" is invalid. It must be 'src' or 'dst'.)", text, line,
                     SVC_PROP_PORT_TYPE);
}

} // namespace

namespace wccp
{
Errata
load_option_set(const YAML::Node &setting, TextView name, CfgString *opts, size_t count)
{
  CfgString *spot;
  CfgString *limit = opts + count;
  int src_line     = setting.Mark().line;

  // Clear all found flags.
  for (spot = opts; spot < limit; ++spot)
    spot->m_found = false;

  // Walk through the strings in the setting.
  if (!setting.IsSequence()) {
    return Prop_Invalid_Type(name, setting, YAML::NodeType::Sequence);
  }

  Errata errata;
  bool list_opts = false;
  for (auto it = setting.begin(); it != setting.end(); ++it) {
    YAML::Node item       = *it;
    std::string_view text = item.Scalar();
    for (spot = opts; spot < limit; ++spot) {
      if (spot->m_text == text) {
        spot->m_found = true;
        break;
      }
    }
    if (spot >= limit) {
      Note_Ignored_Option_Value(errata, text, name, item.Mark().line);
      list_opts = true;
    }
  }

  if (list_opts) {
    Note_List_Valid_Opts(errata, name, src_line, opts, count);
  }

  return errata;
}

/// If successful, the string contains the MD5 key is present, otherwise it's empty.
swoc::Rv<std::string>
load_security(const YAML::Node &setting)
{
  auto opt = setting[SECURITY_PROP_OPTION.data()];
  if (!opt) {
    return Prop_Not_Found(SECURITY_PROP_OPTION, SVC_PROP_SECURITY, setting.Mark().line);
  }

  TextView text = opt.Scalar();
  if ("none" == text) {
  } else if ("md5" == text) {
    YAML::Node key = setting[SECURITY_PROP_KEY.data()];
    if (key) {
      return key.as<std::string>();
    } else {
      return Prop_Not_Found(SECURITY_PROP_KEY, SVC_PROP_SECURITY, opt.Mark().line);
    }
  } else {
    return Security_Opt_Invalid(text, opt.Mark().line);
  }
  return {};
}

/// Process a router address list.
Errata
load_routers(const YAML::Node &setting,   ///< Source of addresses.
             std::vector<uint32_t> &addrs ///< Output list
)
{
  static const char *const NAME = "IPv4 Address";

  if (!setting.IsSequence()) {
    return Prop_Invalid_Type("routers", setting, YAML::NodeType::Sequence);
  }

  Errata errata;
  for (auto const &addr_cfg : setting) {
    in_addr addr;
    std::string text = addr_cfg.as<std::string>();
    if (inet_aton(text.c_str(), &addr)) {
      addrs.push_back(addr.s_addr);
    } else {
      Note_Value_Malformed(errata, NAME, text, addr_cfg.Mark().line);
    }
  }

  return errata;
}

Errata
CacheImpl::loadServicesFromFile(const char *path)
{
  try {
    YAML::Node cfg = YAML::LoadFile(path);
    if (cfg.IsNull()) {
      return Services_Not_Found();
    }

    YAML::Node wccp_node = cfg[WCCP_NAME];
    if (!wccp_node) {
      return Errata(ec_for(ENOENT), ERRATA_NOTE, "No 'wccp' node found in configuration.");
    }

    return loader(wccp_node);
  } catch (std::exception &ex) {
    return Errata(ec_for(EINVAL), ERRATA_ERROR, ex.what());
  }

  return {};
}

Errata
CacheImpl::loader(const YAML::Node &cfg)
{
  YAML::Node svc_list = cfg[SVCS_NAME];

  // No point in going on from here.
  if (!svc_list) {
    return Services_Not_Found();
  }

  if (!svc_list.IsSequence()) {
    return Services_Not_A_Sequence();
  }

  Errata errata;

  // Check for global (default) security setting.
  YAML::Node prop_sec = cfg[SVC_PROP_SECURITY.data()];
  if (prop_sec) {
    auto rv = load_security(prop_sec);
    if (rv.is_ok()) {
      if (rv.result().size() > 0) {
        this->useMD5Security(rv.result());
      }
    } else {
      errata.note(rv);
    }
  }

  auto prop_routers = cfg[SVC_PROP_ROUTERS];
  if (prop_routers) {
    errata.note(load_routers(prop_routers, Seed_Router));
  }

  for (auto it = svc_list.begin(); it != svc_list.end(); ++it) {
    std::string md5_key;
    SecurityOption security_style = SECURITY_NONE;
    bool use_group_local_security = false;

    const YAML::Node &svc_cfg = *it;
    ServiceGroup svc_info;

    // Get the service ID.
    YAML::Node propId = svc_cfg[SVC_PROP_ID.data()];
    if (propId) {
      int x = propId.as<int>();
      if (0 <= x && x <= 255) {
        svc_info.setSvcId(x);
      } else {
        Note_Svc_Prop_Out_Of_Range(errata, SVC_PROP_ID, propId.Mark().line, x, 0, 255);
      }
    } else {
      errata.note(ERRATA_WARN, R"(Required property "{}" not found in "{}" at line {})", SVC_PROP_ID, SVC_NAME,
                  svc_cfg.Mark().line);
    }

    // Service type.
    YAML::Node propType = svc_cfg[SVC_PROP_TYPE];
    if (propType) {
      std::string text = propType.as<std::string>();
      if ("DYNAMIC" == text) {
        svc_info.setSvcType(ServiceGroup::DYNAMIC);
      } else if ("STANDARD" == text) {
        svc_info.setSvcType(ServiceGroup::STANDARD);
      } else {
        Note_Service_Type_Invalid(errata, text, propType.Mark().line);
      }
    } else { // default type based on ID.
      ServiceGroup::Type svc_type = svc_info.getSvcId() <= ServiceGroup::RESERVED ? ServiceGroup::STANDARD : ServiceGroup::DYNAMIC;
      svc_info.setSvcType(svc_type);
      Note_Service_Type_Defaulted(errata, svc_type, it->Mark().line);
    }

    // Get the protocol.
    YAML::Node protocol = svc_cfg[SVC_PROP_PROTOCOL.data()];
    if (protocol) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        Note_Svc_Prop_Ignored(errata, SVC_PROP_PROTOCOL, protocol.Mark().line);
      } else {
        int x = protocol.as<int>();
        if (0 <= x && x <= 255) {
          svc_info.setProtocol(x);
        } else {
          Note_Svc_Prop_Out_Of_Range(errata, SVC_PROP_ID, protocol.Mark().line, x, 0, 255);
        }
      }
    } else if (svc_info.getSvcType() != ServiceGroup::STANDARD) {
      // Required if it's not standard / predefined.
      Note_Prop_Not_Found(errata, SVC_PROP_PROTOCOL, SVC_NAME, it->Mark().line);
    }

    // Get the priority.
    svc_info.setPriority(0); // OK to default to this value.
    YAML::Node priority = svc_cfg[SVC_PROP_PRIORITY.data()];
    if (priority) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        Note_Svc_Prop_Ignored(errata, SVC_PROP_PRIORITY, priority.Mark().line);
      } else {
        int x = priority.as<int>();
        if (0 <= x && x <= 255) {
          svc_info.setPriority(x);
        } else {
          Note_Svc_Prop_Out_Of_Range(errata, SVC_PROP_ID, priority.Mark().line, x, 0, 255);
        }
      }
    }

    // Service flags.
    svc_info.setFlags(0);
    YAML::Node primaryHash = svc_cfg[SVC_PROP_PRIMARY_HASH.data()];
    if (primaryHash) {
      Errata status = load_option_set(primaryHash, SVC_PROP_PRIMARY_HASH, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f    = 0;
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i) {
        if (HASH_OPTS[i].m_found) {
          f |= ServiceGroup::SRC_IP_HASH << i;
        }
      }

      if (f) {
        svc_info.enableFlags(f);
        if (!status) {
          Note_Ignored_Opt_Errors(errata, SVC_PROP_PRIMARY_HASH, primaryHash.Mark().line).note(status);
        }
      } else {
        Note_List_Valid_Opts(errata, SVC_PROP_PRIMARY_HASH, primaryHash.Mark().line, HASH_OPTS, N_OPTS(HASH_OPTS)).note(status);
      }
    } else {
      Note_Prop_Not_Found(errata, SVC_PROP_PRIMARY_HASH, SVC_NAME, primaryHash.Mark().line);
    }

    YAML::Node altHash = svc_cfg[SVC_PROP_ALT_HASH.data()];
    if (altHash) {
      auto status = load_option_set(altHash, SVC_PROP_ALT_HASH, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f  = 0;
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i) {
        if (HASH_OPTS[i].m_found) {
          f |= ServiceGroup::SRC_IP_ALT_HASH << i;
        }
      }

      if (f) {
        svc_info.enableFlags(f);
      }

      if (!status) {
        Note_Ignored_Opt_Errors(errata, SVC_PROP_ALT_HASH, altHash.Mark().line).note(status);
      }
    }

    YAML::Node portType = svc_cfg[SVC_PROP_PORT_TYPE];
    if (portType) {
      std::string text = portType.as<std::string>();
      if ("src" == text) {
        svc_info.enableFlags(ServiceGroup::PORTS_SOURCE);
      } else if ("dst" == text) {
        svc_info.disableFlags(ServiceGroup::PORTS_SOURCE);
      } else {
        Note_Port_Type_Invalid(errata, text, portType.Mark().line);
      }
    }

    // Ports for service.
    svc_info.clearPorts();

    YAML::Node ports = svc_cfg[SVC_PROP_PORTS.data()];
    if (ports) {
      if (ServiceGroup::STANDARD == svc_info.getSvcType()) {
        Note_Svc_Prop_Ignored_In_Standard(errata, SVC_PROP_PORTS, ports.Mark().line);
      } else {
        size_t sidx = 0;
        if (ports.IsSequence()) {
          size_t nport = ports.size();

          // Clip to maximum protocol allowed ports.
          if (nport > ServiceGroup::N_PORTS) {
            Note_Svc_Ports_Too_Many(errata, ports.Mark().line, nport);
            nport = ServiceGroup::N_PORTS;
          }

          // Step through the ports.
          for (auto it = ports.begin(); it != ports.end(); ++it) {
            const YAML::Node &port_cfg = *it;
            int x                      = port_cfg.as<int>();
            if (0 <= x && x <= 65535) {
              svc_info.setPort(sidx++, x);
            } else {
              Note_Svc_Prop_Out_Of_Range(errata, SVC_PROP_PORTS, ports.Mark().line, x, 0, 65535);
            }
          }

          if (sidx) {
            svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
          } else {
            Note_Svc_Ports_None_Valid(errata, ports.Mark().line);
          }
        } else {
          // port is a scalar
          int x = ports.as<int>();
          svc_info.setPort(sidx, x);
          svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
        }
      }
    } else if (ServiceGroup::STANDARD != svc_info.getSvcType()) {
      Note_Svc_Ports_Not_Found(errata, ports.Mark().line);
    }

    // Security option for this service group.
    YAML::Node sec_prop = svc_cfg[SVC_PROP_SECURITY.data()];
    if (sec_prop) {
      swoc::Rv<std::string> security = load_security(sec_prop);
      if (security.is_ok()) {
        use_group_local_security = true;
        if (!security.result().empty()) {
          md5_key        = security.result();
          security_style = SECURITY_MD5;
        } else {
          security_style = SECURITY_NONE;
        }
      }
      errata.note(security.errata());
    }

    // Get any group specific routers.
    std::vector<uint32_t> routers;
    YAML::Node routers_prop = svc_cfg[SVC_PROP_ROUTERS];
    if (routers_prop) {
      auto status = load_routers(routers_prop, routers);
      if (!status)
        errata.note(status);
    }

    if (!routers.size() && !Seed_Router.size()) {
      Note_No_Valid_Routers(errata, routers_prop.Mark().line);
    }

    if (errata.severity() >= ERRATA_WARN) {
      return std::move(Unable_To_Create_Service_Group(svc_cfg.Mark().line).note(errata));
    }

    // Properties after this are optional so we can proceed if they fail.
    GroupData &svc = this->defineServiceGroup(svc_info);

    // Is there a process we should track?
    YAML::Node proc_name = svc_cfg[SVC_PROP_PROC];
    if (proc_name) {
      std::string text = proc_name.as<std::string>();
      svc.setProcName(text);
    }

    // Add seed routers.
    std::vector<uint32_t>::iterator rspot, rlimit;
    for (rspot = routers.begin(), rlimit = routers.end(); rspot != rlimit; ++rspot) {
      svc.seedRouter(*rspot);
    }
    for (rspot = Seed_Router.begin(), rlimit = Seed_Router.end(); rspot != rlimit; ++rspot) {
      svc.seedRouter(*rspot);
    }

    if (use_group_local_security) {
      svc.setSecurity(security_style).setKey(md5_key.c_str());
    }

    // Look for optional properties.

    svc.m_packet_forward = ServiceGroup::GRE; // default
    YAML::Node forward   = svc_cfg[SVC_PROP_FORWARD.data()];
    if (forward) {
      auto status = load_option_set(forward, SVC_PROP_FORWARD, FORWARD_OPTS, N_FORWARD_OPTS);
      bool gre    = FORWARD_OPTS[0].m_found;
      bool l2     = FORWARD_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_forward = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.is_ok()) {
          Note_Ignored_Opt_Errors(errata, SVC_PROP_FORWARD, forward.Mark().line).note(status);
        }
      } else {
        errata.note(ERRATA_NOTE, "Defaulting to GRE forwarding").note(status);
      }
    }

    svc.m_packet_return    = ServiceGroup::GRE; // default.
    YAML::Node prop_return = svc_cfg[SVC_PROP_RETURN.data()];
    if (prop_return) {
      auto status = load_option_set(prop_return, SVC_PROP_RETURN, RETURN_OPTS, N_RETURN_OPTS);
      bool gre    = RETURN_OPTS[0].m_found;
      bool l2     = RETURN_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_return = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.is_ok()) {
          Note_Ignored_Opt_Errors(errata, SVC_PROP_RETURN, prop_return.Mark().line).note(status);
        }
      } else {
        errata.note(ERRATA_NOTE, "Defaulting to GRE return.").note(status);
      }
    }

    svc.m_cache_assign = ServiceGroup::HASH_ONLY; // default
    YAML::Node assign  = svc_cfg[SVC_PROP_ASSIGN.data()];
    if (assign) {
      auto status = load_option_set(assign, SVC_PROP_ASSIGN, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      bool hash   = ASSIGN_OPTS[0].m_found;
      bool mask   = ASSIGN_OPTS[1].m_found;

      if (hash || mask) {
        svc.m_cache_assign = hash ? mask ? ServiceGroup::HASH_OR_MASK : ServiceGroup::HASH_ONLY : ServiceGroup::MASK_ONLY;
        if (!status.is_ok())
          Note_Ignored_Opt_Errors(errata, SVC_PROP_ASSIGN, assign.Mark().line).note(status);
      } else {
        errata.note(ERRATA_NOTE, "Defaulting to hash assignment only.");
        Note_List_Valid_Opts(errata, SVC_PROP_ASSIGN, assign.Mark().line, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS)).note(status);
      }
    }
  }
  return errata;
}

} // namespace wccp
