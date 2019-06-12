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

#include <yaml-cpp/yaml.h>

// WCCP related things that are file local.
namespace
{
using namespace wccp;

// Scratch global list of seed router addresses.
// Yeah, not thread safe, but it's just during configuration load.
// Logic somewhat changed - can we move in to the load method?
std::vector<uint32_t> Seed_Router;

// Names used for various elements and properties.
static const char *const SVC_NAME = "service";

const std::string SVCS_NAME = "services";
const std::string WCCP_NAME = "wccp";

static const char *const SVC_PROP_ID           = "id";
static const char *const SVC_PROP_TYPE         = "type";
static const char *const SVC_PROP_PRIORITY     = "priority";
static const char *const SVC_PROP_PROTOCOL     = "protocol";
static const char *const SVC_PROP_PRIMARY_HASH = "primary-hash";
static const char *const SVC_PROP_ALT_HASH     = "alt-hash";
static const char *const SVC_PROP_PORTS        = "ports";
static const char *const SVC_PROP_PORT_TYPE    = "port-type";
static const char *const SVC_PROP_SECURITY     = "security";
static const char *const SVC_PROP_ROUTERS      = "routers";
static const char *const SVC_PROP_FORWARD      = "forward";
static const char *const SVC_PROP_RETURN       = "return";
static const char *const SVC_PROP_ASSIGN       = "assignment";
static const char *const SVC_PROP_PROC         = "proc-name";

static const char *const SECURITY_PROP_OPTION = "option";
static const char *const SECURITY_PROP_KEY    = "key";

/// Helper structure for processing configuration strings.
struct CfgString {
  std::string m_text; ///< Text value of the option.
  bool m_found;       ///< String was found.
};
typedef std::vector<CfgString> CfgOpts;

#define N_OPTS(x) (sizeof(x) / sizeof(*x))

CfgString FORWARD_OPTS[]    = {{"gre", false}, {"l2", false}};
size_t const N_FORWARD_OPTS = sizeof(FORWARD_OPTS) / sizeof(*FORWARD_OPTS);

CfgString RETURN_OPTS[]    = {{"gre", false}, {"l2", false}};
size_t const N_RETURN_OPTS = sizeof(RETURN_OPTS) / sizeof(*RETURN_OPTS);

CfgString ASSIGN_OPTS[] = {{"hash", false}, {"mask", false}};

CfgString HASH_OPTS[] = {{"src_ip", false}, {"dst_ip", false}, {"src_port", false}, {"dst_port", false}};

ts::Errata::Code
code_max(ts::Errata const &err)
{
  ts::Errata::Code zret            = std::numeric_limits<ts::Errata::Code::raw_type>::min();
  ts::Errata::const_iterator spot  = err.begin();
  ts::Errata::const_iterator limit = err.end();
  for (; spot != limit; ++spot)
    zret = std::max(zret, spot->getCode());
  return zret;
}

ts::Errata::Message
Unable_To_Create_Service_Group(int line)
{
  std::ostringstream out;
  out << "Unable to create service group at line " << line << " because of configuration errors.";
  return ts::Errata::Message(23, LVL_FATAL, out.str());
}

ts::Errata::Message
Services_Not_Found()
{
  return ts::Errata::Message(3, LVL_INFO, "No services found in configuration.");
}

ts::Errata::Message
Services_Not_A_Sequence()
{
  return ts::Errata::Message(4, LVL_INFO, "The 'services' setting was not a list nor array.");
}

ts::Errata::Message
Service_Type_Defaulted(wccp::ServiceGroup::Type type, int line)
{
  std::ostringstream out;
  out << "'type' not found in " << SVC_NAME << " at line " << line << "' -- defaulting to "
      << (type == wccp::ServiceGroup::STANDARD ? "STANDARD" : "DYNAMIC");
  return ts::Errata::Message(6, LVL_INFO, out.str());
}

ts::Errata::Message
Service_Type_Invalid(const std::string &text, int line)
{
  std::ostringstream out;
  out << "Service type '" << text << "' at line " << line << " invalid. Must be \"STANDARD\" or \"DYNAMIC\"";
  return ts::Errata::Message(7, LVL_WARN, out.str());
}

ts::Errata::Message
Prop_Not_Found(const std::string &prop_name, const std::string &group_name, int line)
{
  std::ostringstream out;
  out << "Required '" << prop_name << "' property not found in '" << group_name << "' at line " << line << ".";
  return ts::Errata::Message(8, LVL_WARN, out.str());
}

ts::Errata::Message
Prop_Invalid_Type(const std::string &prop_name, const YAML::Node &prop_cfg, YAML::NodeType::value expected)
{
  std::ostringstream out;
  out << "'" << prop_name << "' is of type '" << prop_cfg.Type() << "' instead of required type '" << expected << "'.";
  return ts::Errata::Message(9, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Prop_Out_Of_Range(const std::string &name, int line, int v, int min, int max)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line << " has a value " << v << " that is not in the allowed range of "
      << min << ".." << max << ".";
  return ts::Errata::Message(10, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Prop_Ignored(const char *name, int line)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line << " ignored because the service is of type standard.";
  return ts::Errata::Message(11, LVL_INFO, out.str());
}

ts::Errata::Message
Svc_Ports_Too_Many(int line, int n)
{
  std::ostringstream out;
  out << "Excess ports ignored at line " << line << ". " << n << " ports specified, only" << wccp::ServiceGroup::N_PORTS
      << " supported.";
  return ts::Errata::Message(14, LVL_INFO, out.str());
}

ts::Errata::Message
Svc_Ports_None_Valid(int line)
{
  std::ostringstream out;
  out << "A '" << SVC_PROP_PORTS << "' property was found at line " << line << " but none of the ports were valid.";
  return ts::Errata::Message(17, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Ports_Not_Found(int line)
{
  std::ostringstream out;
  out << "Ports not found in service at line " << line << ". Ports must be defined for a dynamic service.";
  return ts::Errata::Message(18, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Prop_Ignored_In_Standard(const std::string &name, int line)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line << " ignored because the service is of type STANDARD.";
  return ts::Errata::Message(19, LVL_INFO, out.str());
}

ts::Errata::Message
Security_Opt_Invalid(const std::string &text, int line)
{
  std::ostringstream out;
  out << "Security option '" << text << "' at line " << line << " is invalid. It must be 'none' or 'md5'.";
  return ts::Errata::Message(20, LVL_WARN, out.str());
}

ts::Errata::Message
Value_Malformed(const std::string &name, const std::string &text, int line)
{
  std::ostringstream out;
  out << "'" << name << "' value '" << text << "' malformed at line " << line << ".";
  return ts::Errata::Message(21, LVL_WARN, out.str());
}

ts::Errata::Message
No_Valid_Routers(int line)
{
  std::ostringstream out;
  out << "No valid IP address for routers found for Service Group at line " << line << ".";
  return ts::Errata::Message(22, LVL_WARN, out.str());
}

ts::Errata::Message
Ignored_Option_Value(const std::string &text, const std::string &name, int line)
{
  std::ostringstream out;
  out << "Value '" << text << "' at line " << line << " was ignored because it is not a valid option for '" << name << "'.";
  return ts::Errata::Message(24, LVL_INFO, out.str());
}

ts::Errata::Message
Ignored_Opt_Errors(const std::string &name, int line)
{
  std::ostringstream out;
  out << "Errors in  '" << name << "' were ignored.";
  return ts::Errata::Message(28, LVL_INFO, out.str());
}

ts::Errata::Message
List_Valid_Opts(const std::string &name, int line, CfgString *values, size_t n)
{
  std::ostringstream out;
  out << "Valid values for the '" << name << "' property at line " << line << " are: ";
  out << '"' << values[0].m_text << '"';
  for (size_t i = 1; i < n; ++i)
    out << ", \"" << values[i].m_text << '"';
  out << '.';
  return ts::Errata::Message(29, LVL_INFO, out.str());
}

ts::Errata::Message
Port_Type_Invalid(const std::string &text, int line)
{
  std::ostringstream out;
  out << "Value '" << text << "' at line " << line << "for property '" << SVC_PROP_PORT_TYPE
      << "' is invalid. It must be 'src' or 'dst'.";
  return ts::Errata::Message(30, LVL_WARN, out.str());
}

} // namespace

namespace wccp
{
ts::Errata
load_option_set(const YAML::Node &setting, const char *name, CfgString *opts, size_t count)
{
  ts::Errata zret;
  CfgString *spot;
  CfgString *limit = opts + count;
  int src_line     = setting.Mark().line;

  // Clear all found flags.
  for (spot = opts; spot < limit; ++spot)
    spot->m_found = false;

  // Walk through the strings in the setting.
  if (!setting.IsSequence()) {
    zret.push(Prop_Invalid_Type(name, setting, YAML::NodeType::Sequence));
    return zret;
  }

  bool list_opts = false;
  for (auto it = setting.begin(); it != setting.end(); ++it) {
    YAML::Node item  = *it;
    std::string text = item.as<std::string>();
    for (spot = opts; spot < limit; ++spot) {
      if (spot->m_text == text) {
        spot->m_found = true;
        break;
      }
    }
    if (spot >= limit) {
      zret.push(Ignored_Option_Value(text, name, item.Mark().line));
      list_opts = true;
    }
  }

  if (list_opts) {
    zret.push(List_Valid_Opts(name, src_line, opts, count));
  }

  return zret;
}

/** On success this returns a non @c NULL pointer if the MD5 option is
  set.  In that case the pointer points at the MD5 key.  Otherwise
  the option was none and the pointer is @c NULL
  */
ts::Rv<std::string>
load_security(const YAML::Node &setting ///< Security setting.
)
{
  ts::Rv<std::string> zret;

  auto opt = setting[SECURITY_PROP_OPTION];
  if (!opt) {
    zret.push(Prop_Not_Found(SECURITY_PROP_OPTION, SVC_PROP_SECURITY, setting.Mark().line));
    return zret;
  }

  std::string text = opt.as<std::string>();
  if ("none" == text) {
  } else if ("md5" == text) {
    YAML::Node key = setting[SECURITY_PROP_KEY];
    if (key) {
      zret = key.as<std::string>();
    } else {
      zret.push(Prop_Not_Found(SECURITY_PROP_KEY, SVC_PROP_SECURITY, opt.Mark().line));
    }
  } else {
    zret.push(Security_Opt_Invalid(text, opt.Mark().line));
  }
  return zret;
}

/// Process a router address list.
ts::Errata
load_routers(const YAML::Node &setting,   ///< Source of addresses.
             std::vector<uint32_t> &addrs ///< Output list
)
{
  ts::Errata zret;
  static const char *const NAME = "IPv4 Address";

  if (!setting.IsSequence()) {
    zret.push(Prop_Invalid_Type("routers", setting, YAML::NodeType::Sequence));
    return zret;
  }

  for (auto const &addr_cfg : setting) {
    in_addr addr;
    std::string text = addr_cfg.as<std::string>();
    if (inet_aton(text.c_str(), &addr)) {
      addrs.push_back(addr.s_addr);
    } else {
      zret.push(Value_Malformed(NAME, text, addr_cfg.Mark().line));
    }
  }

  return zret;
}

ts::Errata
CacheImpl::loadServicesFromFile(const char *path)
{
  try {
    YAML::Node cfg = YAML::LoadFile(path);
    if (cfg.IsNull()) {
      return Services_Not_Found();
    }

    YAML::Node wccp_node = cfg[WCCP_NAME];
    if (!wccp_node) {
      return ts::Errata::Message(3, LVL_INFO, "No 'wccp' node found in configuration.");
    }

    return loader(wccp_node);
  } catch (std::exception &ex) {
    return ts::Errata::Message(1, 1, ex.what());
  }

  return ts::Errata();
}

ts::Errata
CacheImpl::loader(const YAML::Node &cfg)
{
  ts::Errata zret;

  YAML::Node svc_list = cfg[SVCS_NAME];

  // No point in going on from here.
  if (!svc_list) {
    return Services_Not_Found();
  }

  if (!svc_list.IsSequence()) {
    return Services_Not_A_Sequence();
  }

  // Check for global (default) security setting.
  YAML::Node prop_sec = cfg[SVC_PROP_SECURITY];
  if (prop_sec) {
    ts::Rv<std::string> rv = load_security(prop_sec);
    if (rv.isOK() && rv.result().size() > 0) {
      std::string md5_key = rv.result();
      this->useMD5Security(md5_key);
    } else {
      zret.pull(rv.errata());
    }
  }

  auto prop_routers = cfg[SVC_PROP_ROUTERS];
  if (prop_routers) {
    zret.pull(load_routers(prop_routers, Seed_Router).doNotLog());
  }

  for (auto it = svc_list.begin(); it != svc_list.end(); ++it) {
    std::string md5_key;
    SecurityOption security_style = SECURITY_NONE;
    bool use_group_local_security = false;

    const YAML::Node &svc_cfg = *it;
    ServiceGroup svc_info;

    // Get the service ID.
    YAML::Node propId = svc_cfg[SVC_PROP_ID];
    if (propId) {
      int x = propId.as<int>();
      if (0 <= x && x <= 255) {
        svc_info.setSvcId(x);
      } else {
        zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, propId.Mark().line, x, 0, 255));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_ID, SVC_NAME, svc_cfg.Mark().line));
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
        zret.push(Service_Type_Invalid(text, propType.Mark().line));
      }
    } else { // default type based on ID.
      ServiceGroup::Type svc_type = svc_info.getSvcId() <= ServiceGroup::RESERVED ? ServiceGroup::STANDARD : ServiceGroup::DYNAMIC;
      svc_info.setSvcType(svc_type);
      zret.push(Service_Type_Defaulted(svc_type, it->Mark().line));
    }

    // Get the protocol.
    YAML::Node protocol = svc_cfg[SVC_PROP_PROTOCOL];
    if (protocol) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PROTOCOL, protocol.Mark().line));
      } else {
        int x = protocol.as<int>();
        if (0 <= x && x <= 255) {
          svc_info.setProtocol(x);
        } else {
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, protocol.Mark().line, x, 0, 255));
        }
      }
    } else if (svc_info.getSvcType() != ServiceGroup::STANDARD) {
      // Required if it's not standard / predefined.
      zret.push(Prop_Not_Found(SVC_PROP_PROTOCOL, SVC_NAME, it->Mark().line));
    }

    // Get the priority.
    svc_info.setPriority(0); // OK to default to this value.
    YAML::Node priority = svc_cfg[SVC_PROP_PRIORITY];
    if (priority) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PRIORITY, priority.Mark().line));
      } else {
        int x = priority.as<int>();
        if (0 <= x && x <= 255) {
          svc_info.setPriority(x);
        } else {
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, priority.Mark().line, x, 0, 255));
        }
      }
    }

    // Service flags.
    svc_info.setFlags(0);
    YAML::Node primaryHash = svc_cfg[SVC_PROP_PRIMARY_HASH];
    if (primaryHash) {
      ts::Errata status = load_option_set(primaryHash, SVC_PROP_PRIMARY_HASH, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f        = 0;
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i) {
        if (HASH_OPTS[i].m_found) {
          f |= ServiceGroup::SRC_IP_HASH << i;
        }
      }

      if (f) {
        svc_info.enableFlags(f);
        if (!status) {
          zret.push(Ignored_Opt_Errors(SVC_PROP_PRIMARY_HASH, primaryHash.Mark().line).set(status));
        }
      } else {
        zret.push(List_Valid_Opts(SVC_PROP_PRIMARY_HASH, primaryHash.Mark().line, HASH_OPTS, N_OPTS(HASH_OPTS)).set(status));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_PRIMARY_HASH, SVC_NAME, primaryHash.Mark().line));
    }

    YAML::Node altHash = svc_cfg[SVC_PROP_ALT_HASH];
    if (altHash) {
      ts::Errata status = load_option_set(altHash, SVC_PROP_ALT_HASH, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f        = 0;
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i) {
        if (HASH_OPTS[i].m_found) {
          f |= ServiceGroup::SRC_IP_ALT_HASH << i;
        }
      }

      if (f) {
        svc_info.enableFlags(f);
      }

      if (!status) {
        zret.push(Ignored_Opt_Errors(SVC_PROP_ALT_HASH, altHash.Mark().line).set(status));
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
        zret.push(Port_Type_Invalid(text, portType.Mark().line));
      }
    }

    // Ports for service.
    svc_info.clearPorts();

    YAML::Node ports = svc_cfg[SVC_PROP_PORTS];
    if (ports) {
      if (ServiceGroup::STANDARD == svc_info.getSvcType()) {
        zret.push(Svc_Prop_Ignored_In_Standard(SVC_PROP_PORTS, ports.Mark().line));
      } else {
        size_t sidx = 0;
        if (ports.IsSequence()) {
          size_t nport = ports.size();

          // Clip to maximum protocol allowed ports.
          if (nport > ServiceGroup::N_PORTS) {
            zret.push(Svc_Ports_Too_Many(ports.Mark().line, nport));
            nport = ServiceGroup::N_PORTS;
          }

          // Step through the ports.
          for (auto it = ports.begin(); it != ports.end(); ++it) {
            const YAML::Node &port_cfg = *it;
            int x                      = port_cfg.as<int>();
            if (0 <= x && x <= 65535) {
              svc_info.setPort(sidx++, x);
            } else {
              zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_PORTS, ports.Mark().line, x, 0, 65535));
            }
          }

          if (sidx) {
            svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
          } else {
            zret.push(Svc_Ports_None_Valid(ports.Mark().line));
          }
        } else {
          // port is a scalar
          int x = ports.as<int>();
          svc_info.setPort(sidx, x);
          svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
        }
      }
    } else if (ServiceGroup::STANDARD != svc_info.getSvcType()) {
      zret.push(Svc_Ports_Not_Found(ports.Mark().line));
    }

    // Security option for this service group.
    YAML::Node sec_prop = svc_cfg[SVC_PROP_SECURITY];
    if (sec_prop) {
      ts::Rv<std::string> security = load_security(sec_prop);
      if (security.isOK()) {
        use_group_local_security = true;
        if (!security.result().empty()) {
          md5_key        = security.result();
          security_style = SECURITY_MD5;
        } else {
          security_style = SECURITY_NONE;
        }
      }
      zret.pull(security.errata());
    }

    // Get any group specific routers.
    std::vector<uint32_t> routers;
    YAML::Node routers_prop = svc_cfg[SVC_PROP_ROUTERS];
    if (routers_prop) {
      ts::Errata status = load_routers(routers_prop, routers);
      if (!status)
        zret.push(ts::Errata::Message(23, LVL_INFO, "Router specification invalid.").set(status));
    }

    if (!routers.size() && !Seed_Router.size()) {
      zret.push(No_Valid_Routers(routers_prop.Mark().line));
    }

    // See if can proceed with service group creation.
    ts::Errata::Code code = code_max(zret);
    if (code >= LVL_WARN) {
      zret = Unable_To_Create_Service_Group(svc_cfg.Mark().line).set(zret);
      return zret;
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
    YAML::Node forward   = svc_cfg[SVC_PROP_FORWARD];
    if (forward) {
      ts::Errata status = load_option_set(forward, SVC_PROP_FORWARD, FORWARD_OPTS, N_FORWARD_OPTS);
      bool gre          = FORWARD_OPTS[0].m_found;
      bool l2           = FORWARD_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_forward = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.isOK()) {
          zret.push(Ignored_Opt_Errors(SVC_PROP_FORWARD, forward.Mark().line).set(status));
        }
      } else {
        zret.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to GRE forwarding.").set(status));
      }
    }

    svc.m_packet_return    = ServiceGroup::GRE; // default.
    YAML::Node prop_return = svc_cfg[SVC_PROP_RETURN];
    if (prop_return) {
      ts::Errata status = load_option_set(prop_return, SVC_PROP_RETURN, RETURN_OPTS, N_RETURN_OPTS);
      bool gre          = RETURN_OPTS[0].m_found;
      bool l2           = RETURN_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_return = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.isOK()) {
          zret.push(Ignored_Opt_Errors(SVC_PROP_RETURN, prop_return.Mark().line).set(status));
        }
      } else {
        zret.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to GRE return.").set(status));
      }
    }

    svc.m_cache_assign = ServiceGroup::HASH_ONLY; // default
    YAML::Node assign  = svc_cfg[SVC_PROP_ASSIGN];
    if (assign) {
      ts::Errata status = load_option_set(assign, SVC_PROP_ASSIGN, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      bool hash         = ASSIGN_OPTS[0].m_found;
      bool mask         = ASSIGN_OPTS[1].m_found;

      if (hash || mask) {
        svc.m_cache_assign = hash ? mask ? ServiceGroup::HASH_OR_MASK : ServiceGroup::HASH_ONLY : ServiceGroup::MASK_ONLY;
        if (!status.isOK())
          zret.push(Ignored_Opt_Errors(SVC_PROP_ASSIGN, assign.Mark().line).set(status));
      } else {
        status.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to hash assignment only."));
        zret.push(List_Valid_Opts(SVC_PROP_ASSIGN, assign.Mark().line, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS)).set(status));
      }
    }
  }
  return zret;
}

} // namespace wccp
