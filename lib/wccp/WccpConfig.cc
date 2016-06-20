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
#include <tsconfig/TsValue.h>
#include <arpa/inet.h>
#include <iostream>
#include <errno.h>
#include <stdlib.h>

using ts::config::Configuration;
using ts::config::Value;

// Support that must go in the standard namespace.
namespace std
{
inline ostream &
operator<<(ostream &s, ts::ConstBuffer const &b)
{
  if (b._ptr)
    s.write(b._ptr, b._size);
  else
    s << b._size;
  return s;
}

} // namespace std

// WCCP related things that are file local.
namespace
{
using namespace wccp;

// Scratch global list of seed router addresses.
// Yeah, not thread safe, but it's just during configuration load.
// Logic somewhat changed - can we move in to the load method?
std::vector<uint32_t> Seed_Router;

// Names used for various elements and properties.
static char const *const SVC_NAME = "service";

static char const *const SVC_PROP_ID           = "id";
static char const *const SVC_PROP_TYPE         = "type";
static char const *const SVC_PROP_PRIORITY     = "priority";
static char const *const SVC_PROP_PROTOCOL     = "protocol";
static char const *const SVC_PROP_PRIMARY_HASH = "primary-hash";
static char const *const SVC_PROP_ALT_HASH     = "alt-hash";
static char const *const SVC_PROP_PORTS        = "ports";
static char const *const SVC_PROP_PORT_TYPE    = "port-type";
static char const *const SVC_PROP_SECURITY     = "security";
static char const *const SVC_PROP_ROUTERS      = "routers";
static char const *const SVC_PROP_FORWARD      = "forward";
static char const *const SVC_PROP_RETURN       = "return";
static char const *const SVC_PROP_ASSIGN       = "assignment";
static char const *const SVC_PROP_PROC         = "proc-name";

static char const *const SECURITY_PROP_OPTION = "option";
static char const *const SECURITY_PROP_KEY    = "key";

/// Helper structure for processing configuration strings.
struct CfgString {
  char const *m_text; ///< Text value of the option.
  bool m_found;       ///< String was found.
};
typedef std::vector<CfgString> CfgOpts;

#define N_OPTS(x) (sizeof(x) / sizeof(*x))

CfgString FORWARD_OPTS[]    = {{"gre"}, {"l2"}};
size_t const N_FORWARD_OPTS = sizeof(FORWARD_OPTS) / sizeof(*FORWARD_OPTS);

CfgString RETURN_OPTS[]    = {{"gre"}, {"l2"}};
size_t const N_RETURN_OPTS = sizeof(RETURN_OPTS) / sizeof(*RETURN_OPTS);

CfgString ASSIGN_OPTS[] = {{"hash"}, {"mask"}};

CfgString HASH_OPTS[] = {{"src_ip"}, {"dst_ip"}, {"src_port"}, {"dst_port"}};

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

struct ValueNamePrinter {
  Value const &_v;
  ValueNamePrinter(Value const &v) : _v(v) {}
};

std::ostream &
operator<<(std::ostream &out, ValueNamePrinter const &v)
{
  ts::ConstBuffer const &name = v._v.getName();
  if (name._ptr)
    out << "'" << name << "'";
  else
    out << v._v.getIndex();
  return out;
}

#if 0 /* silence -Wunused-function */
ts::Errata::Message File_Syntax_Error(int line, char const* text) {
  std::ostringstream out;
  out << "Service configuration error. Line "
      << line
      << ": " << text
    ;
  return ts::Errata::Message(1, LVL_FATAL, out.str());
}

ts::Errata::Message File_Read_Error(char const* text) {
  std::ostringstream out;
  out << "Failed to parse configuration file."
      << ": " << text
    ;
  return ts::Errata::Message(2, LVL_FATAL, out.str());
}
#endif

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
Service_Not_A_Group(int line)
{
  std::ostringstream out;
  out << "'" << SVC_NAME << "' must be a group at line " << line << ".";
  return ts::Errata::Message(5, LVL_WARN, out.str());
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
Service_Type_Invalid(ts::ConstBuffer const &text, int line)
{
  std::ostringstream out;
  out << "Service type '" << text << "' at line " << line << " invalid. Must be \"STANDARD\" or \"DYNAMIC\"";
  return ts::Errata::Message(7, LVL_WARN, out.str());
}

ts::Errata::Message
Prop_Not_Found(char const *prop_name, char const *group_name, int line)
{
  std::ostringstream out;
  out << "Required '" << prop_name << "' property not found in '" << group_name << "' at line " << line << ".";
  return ts::Errata::Message(8, LVL_WARN, out.str());
}

ts::Errata::Message
Prop_Invalid_Type(Value const &prop_cfg, ts::config::ValueType expected)
{
  std::ostringstream out;
  out << "'" << prop_cfg.getName() << "' at line " << prop_cfg.getSourceLine() << " is of type '" << prop_cfg.getType()
      << "' instead of required type '" << expected << "'.";
  return ts::Errata::Message(9, LVL_WARN, out.str());
}

ts::Errata::Message
Prop_List_Invalid_Type(Value const &elt_cfg, ///< List element.
                       ts::config::ValueType expected)
{
  std::ostringstream out;
  out << "Element " << ValueNamePrinter(elt_cfg) << " at line " << elt_cfg.getSourceLine() << " in the aggregate property '"
      << elt_cfg.getParent().getName() << "' is of type '" << elt_cfg.getType() << "' instead of required type '" << expected
      << "'.";
  return ts::Errata::Message(9, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Prop_Out_Of_Range(char const *name, Value const &elt_cfg, int v, int min, int max)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << elt_cfg.getSourceLine() << " has a value " << v
      << " that is not in the allowed range of " << min << ".." << max << ".";
  return ts::Errata::Message(10, LVL_WARN, out.str());
}

ts::Errata::Message
Svc_Prop_Ignored(char const *name, int line)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line << " ignored because the service is of type standard.";
  return ts::Errata::Message(11, LVL_INFO, out.str());
}

#if 0 /* silence -Wunused-function */
ts::Errata::Message Svc_Flags_No_Hash_Set(int line) {
  std::ostringstream out;
  out << "Service flags have no hash set at line " << line
    ;
  return ts::Errata::Message(12, LVL_WARN, out.str());
}

ts::Errata::Message Svc_Flags_Ignored(int line) {
  std::ostringstream out;
  out << "Invalid service flags at line  " << line
      << " ignored."
    ;
  return ts::Errata::Message(13, LVL_INFO, out.str());
}
#endif

ts::Errata::Message
Svc_Ports_Too_Many(int line, int n)
{
  std::ostringstream out;
  out << "Excess ports ignored at line " << line << ". " << n << " ports specified, only" << wccp::ServiceGroup::N_PORTS
      << " supported.";
  return ts::Errata::Message(14, LVL_INFO, out.str());
}

ts::Errata::Message
Svc_Ports_Malformed(int line)
{
  std::ostringstream out;
  out << "Port value ignored (not a number) at line " << line << ".";
  return ts::Errata::Message(15, LVL_INFO, out.str());
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
Svc_Prop_Ignored_In_Standard(const char *name, int line)
{
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line << " ignored because the service is of type STANDARD.";
  return ts::Errata::Message(19, LVL_INFO, out.str());
}

ts::Errata::Message
Security_Opt_Invalid(ts::ConstBuffer const &text, int line)
{
  std::ostringstream out;
  out << "Security option '" << text << "' at line " << line << " is invalid. It must be 'none' or 'md5'.";
  return ts::Errata::Message(20, LVL_WARN, out.str());
}

ts::Errata::Message
Value_Malformed(char const *name, char const *text, int line)
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
Ignored_Option_Value(ts::ConstBuffer const &text, ts::ConstBuffer const &name, int line)
{
  std::ostringstream out;
  out << "Value '" << text << "' at line " << line << " was ignored because it is not a valid option for '" << name << "'.";
  return ts::Errata::Message(24, LVL_INFO, out.str());
}

ts::Errata::Message
Ignored_Opt_Errors(char const *name, int line)
{
  std::ostringstream out;
  out << "Errors in  '" << name << "' at line " << line << " were ignored.";
  return ts::Errata::Message(28, LVL_INFO, out.str());
}

ts::Errata::Message
List_Valid_Opts(ts::ConstBuffer const &name, int line, CfgString *values, size_t n)
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
Port_Type_Invalid(ts::ConstBuffer const &text, int line)
{
  std::ostringstream out;
  out << "Value '" << text << "' at line " << line << "for property '" << SVC_PROP_PORT_TYPE
      << "' is invalid. It must be 'src' or 'dst'.";
  return ts::Errata::Message(30, LVL_WARN, out.str());
}

} // anon namespace

namespace wccp
{
inline bool
operator==(ts::ConstBuffer const &b, char const *text)
{
  return 0 == strncasecmp(text, b._ptr, b._size);
}

inline bool
operator==(char const *text, ts::ConstBuffer const &b)
{
  return 0 == strncasecmp(text, b._ptr, b._size);
}

ts::Errata
load_option_set(Value const &setting, CfgString *opts, size_t count)
{
  ts::Errata zret;
  CfgString *spot;
  CfgString *limit            = opts + count;
  ts::ConstBuffer const &name = setting.getName();
  int src_line                = setting.getSourceLine();

  // Clear all found flags.
  for (spot = opts; spot < limit; ++spot)
    spot->m_found = false;

  // Walk through the strings in the setting.
  if (setting.isContainer()) {
    int nr         = setting.childCount();
    bool list_opts = false;
    for (int i = 0; i < nr; ++i) {
      Value item = setting[i];
      if (ts::config::StringValue == item.getType()) {
        ts::ConstBuffer text = item.getText();
        for (spot = opts; spot < limit; ++spot) {
          if (spot->m_text == text) {
            spot->m_found = true;
            break;
          }
        }
        if (spot >= limit) {
          zret.push(Ignored_Option_Value(text, name, item.getSourceLine()));
          list_opts = true;
        }
      } else {
        zret.push(Prop_Invalid_Type(setting, ts::config::StringValue));
      }
    }
    if (list_opts)
      zret.push(List_Valid_Opts(name, src_line, opts, count));
  } else {
    zret.push(Prop_Invalid_Type(setting, ts::config::ListValue));
  }
  return zret;
}

/** On success this returns a non @c NULL pointer if the MD5 option is
    set.  In that case the pointer points at the MD5 key.  Otherwise
    the option was none and the pointer is @c NULL
 */
ts::Rv<ts::ConstBuffer>
load_security(Value const &setting ///< Security setting.
              )
{
  ts::Rv<ts::ConstBuffer> zret;
  int src_line;
  ts::ConstBuffer text;

  zret.result().set(0);

  src_line = setting.getSourceLine();
  if (ts::config::GroupValue == setting.getType()) {
    Value opt = setting[SECURITY_PROP_OPTION];
    if (opt) {
      if (ts::config::StringValue == opt.getType()) {
        text = opt.getText();
        if ("none" == text) {
        } else if ("md5" == text) {
          Value key = setting[SECURITY_PROP_KEY];
          if (key) {
            if (ts::config::StringValue == key.getType()) {
              zret = key.getText();
            } else {
              zret.push(Prop_Invalid_Type(key, ts::config::StringValue));
            }
          } else {
            zret.push(Prop_Not_Found(SECURITY_PROP_KEY, SVC_PROP_SECURITY, src_line));
          }
        } else {
          zret.push(Security_Opt_Invalid(text, opt.getSourceLine()));
        }
      } else {
        zret.push(Prop_Invalid_Type(opt, ts::config::StringValue));
      }
    } else {
      zret.push(Prop_Not_Found(SECURITY_PROP_OPTION, SVC_PROP_SECURITY, src_line));
    }
  } else {
    zret.push(Prop_Invalid_Type(setting, ts::config::GroupValue));
  }
  return zret;
}

/// Process a router address list.
ts::Errata
load_routers(Value const &setting,        ///< Source of addresses.
             std::vector<uint32_t> &addrs ///< Output list
             )
{
  ts::Errata zret;
  char const *text;
  static char const *const NAME = "IPv4 Address";

  if (setting.isContainer()) {
    int nr = setting.childCount();
    for (int i = 0; i < nr; ++i) {
      Value const &addr_cfg = setting[i];
      int addr_line         = addr_cfg.getSourceLine();
      in_addr addr;
      if (ts::config::StringValue == addr_cfg.getType()) {
        text = addr_cfg.getText()._ptr;
        if (inet_aton(text, &addr))
          addrs.push_back(addr.s_addr);
        else
          zret.push(Value_Malformed(NAME, text, addr_line));
      } else {
        zret.push(Prop_List_Invalid_Type(addr_cfg, ts::config::StringValue));
      }
    }
  } else {
    zret.push(Prop_Invalid_Type(setting, ts::config::ListValue));
  }
  return zret;
}

ts::Errata
CacheImpl::loadServicesFromFile(char const *path)
{
  ts::Errata zret;
  int src_line = 0;              // scratch for local source line caching.
  std::vector<uint32_t> routers; // scratch per service loop.
  Value prop;                    // scratch var.

  ts::Rv<Configuration> cv = Configuration::loadFromPath(path);
  if (!cv.isOK())
    return cv.errata();

  ts::config::Configuration cfg = cv.result();
  Value svc_list                = cfg.find("services");
  // No point in going on from here.
  if (!svc_list)
    return Services_Not_Found();

  if (!svc_list.isContainer())
    return Services_Not_A_Sequence();

  // Check for global (default) security setting.
  if ((prop = cfg[SVC_PROP_SECURITY]).hasValue()) {
    ts::Rv<ts::ConstBuffer> rv = load_security(prop);
    if (rv.isOK())
      this->useMD5Security(rv);
    else
      zret.pull(rv.errata());
  }

  if ((prop = cfg[SVC_PROP_ROUTERS]).hasValue()) {
    zret.pull(load_routers(prop, Seed_Router).doNotLog());
  }

  int idx, nsvc;
  for (idx = 0, nsvc = svc_list.childCount(); idx < nsvc; ++idx) {
    int x; // scratch int.
    char const *md5_key = 0;
    ts::ConstBuffer text;
    SecurityOption security_style = SECURITY_NONE;
    bool use_group_local_security = false;
    Value const &svc_cfg          = svc_list[idx];
    int svc_line                  = svc_cfg.getSourceLine();
    ServiceGroup svc_info;

    if (ts::config::GroupValue != svc_cfg.getType()) {
      zret.push(Service_Not_A_Group(svc_line));
      continue;
    }

    // Get the service ID.
    if ((prop = svc_cfg[SVC_PROP_ID]).hasValue()) {
      if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setSvcId(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::IntegerValue));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_ID, SVC_NAME, svc_line));
    }

    // Service type.
    if ((prop = svc_cfg[SVC_PROP_TYPE]).hasValue()) {
      if (ts::config::StringValue == prop.getType()) {
        text = prop.getText();
        if ("DYNAMIC" == text)
          svc_info.setSvcType(ServiceGroup::DYNAMIC);
        else if ("STANDARD" == text)
          svc_info.setSvcType(ServiceGroup::STANDARD);
        else
          zret.push(Service_Type_Invalid(text, prop.getSourceLine()));
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::StringValue));
      }
    } else { // default type based on ID.
      ServiceGroup::Type svc_type = svc_info.getSvcId() <= ServiceGroup::RESERVED ? ServiceGroup::STANDARD : ServiceGroup::DYNAMIC;
      svc_info.setSvcType(svc_type);
      zret.push(Service_Type_Defaulted(svc_type, svc_line));
    }

    // Get the protocol.
    if ((prop = svc_cfg[SVC_PROP_PROTOCOL]).hasValue()) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PROTOCOL, prop.getSourceLine()));
      } else if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setProtocol(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::IntegerValue));
      }
    } else if (svc_info.getSvcType() != ServiceGroup::STANDARD) {
      // Required if it's not standard / predefined.
      zret.push(Prop_Not_Found(SVC_PROP_PROTOCOL, SVC_NAME, svc_line));
    }

    // Get the priority.
    svc_info.setPriority(0); // OK to default to this value.
    if ((prop = svc_cfg[SVC_PROP_PRIORITY]).hasValue()) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PRIORITY, prop.getSourceLine()));
      } else if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setPriority(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::IntegerValue));
      }
    }

    // Service flags.
    svc_info.setFlags(0);
    if ((prop = svc_cfg[SVC_PROP_PRIMARY_HASH]).hasValue()) {
      ts::Errata status = load_option_set(prop, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f        = 0;
      src_line          = prop.getSourceLine();
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i)
        if (HASH_OPTS[i].m_found)
          f |= ServiceGroup::SRC_IP_HASH << i;
      if (f) {
        svc_info.enableFlags(f);
        if (!status)
          zret.push(Ignored_Opt_Errors(SVC_PROP_PRIMARY_HASH, src_line).set(status));
      } else {
        zret.push(List_Valid_Opts(prop.getName(), src_line, HASH_OPTS, N_OPTS(HASH_OPTS)).set(status));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_PRIMARY_HASH, SVC_NAME, svc_line));
    }

    if ((prop = svc_cfg[SVC_PROP_ALT_HASH]).hasValue()) {
      ts::Errata status = load_option_set(prop, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32_t f        = 0;
      src_line          = prop.getSourceLine();
      for (size_t i = 0; i < N_OPTS(HASH_OPTS); ++i)
        if (HASH_OPTS[i].m_found)
          f |= ServiceGroup::SRC_IP_ALT_HASH << i;
      if (f)
        svc_info.enableFlags(f);
      if (!status)
        zret.push(Ignored_Opt_Errors(SVC_PROP_ALT_HASH, src_line).set(status));
    }

    if ((prop = svc_cfg[SVC_PROP_PORT_TYPE]).hasValue()) {
      src_line = prop.getSourceLine();
      if (ts::config::StringValue == prop.getType()) {
        text = prop.getText();
        if ("src" == text)
          svc_info.enableFlags(ServiceGroup::PORTS_SOURCE);
        else if ("dst" == text)
          svc_info.disableFlags(ServiceGroup::PORTS_SOURCE);
        else
          zret.push(Port_Type_Invalid(text, src_line));
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::StringValue));
      }
    }

    // Ports for service.
    svc_info.clearPorts();
    if ((prop = svc_cfg[SVC_PROP_PORTS]).hasValue()) {
      src_line = prop.getSourceLine();
      if (ServiceGroup::STANDARD == svc_info.getSvcType()) {
        zret.push(Svc_Prop_Ignored_In_Standard(SVC_PROP_PORTS, src_line));
      } else {
        if (prop.isContainer()) {
          size_t nport = prop.childCount();
          size_t pidx, sidx;
          bool malformed_error = false;
          // Clip to maximum protocol allowed ports.
          if (nport > ServiceGroup::N_PORTS) {
            zret.push(Svc_Ports_Too_Many(src_line, nport));
            nport = ServiceGroup::N_PORTS;
          }
          // Step through the ports.
          for (pidx = sidx = 0; pidx < nport; ++pidx) {
            Value const &port_cfg = prop[pidx];
            if (ts::config::IntegerValue == port_cfg.getType()) {
              x = atoi(port_cfg.getText()._ptr);
              if (0 <= x && x <= 65535)
                svc_info.setPort(sidx++, x);
              else
                zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_PORTS, port_cfg, x, 0, 65535));
            } else if (!malformed_error) { // only report this once.
              zret.push(Svc_Ports_Malformed(src_line));
              malformed_error = true;
            }
          }
          if (sidx)
            svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
          else
            zret.push(Svc_Ports_None_Valid(src_line));
        } else {
          zret.push(Prop_Invalid_Type(prop, ts::config::ListValue));
        }
      }
    } else if (ServiceGroup::STANDARD != svc_info.getSvcType()) {
      zret.push(Svc_Ports_Not_Found(svc_line));
    }

    // Security option for this service group.
    if ((prop = svc_cfg[SVC_PROP_SECURITY]).hasValue()) {
      ts::Rv<ts::ConstBuffer> security = load_security(prop);
      if (security.isOK()) {
        use_group_local_security = true;
        if (security.result()._ptr) {
          md5_key        = security.result()._ptr;
          security_style = SECURITY_MD5;
        } else {
          security_style = SECURITY_NONE;
        }
      }
      zret.pull(security.errata());
    }

    // Get any group specific routers.
    routers.clear(); // reset list.
    if ((prop = svc_cfg[SVC_PROP_ROUTERS]).hasValue()) {
      ts::Errata status = load_routers(prop, routers);
      if (!status)
        zret.push(ts::Errata::Message(23, LVL_INFO, "Router specification invalid.").set(status));
    }
    if (!routers.size() && !Seed_Router.size())
      zret.push(No_Valid_Routers(svc_line));

    // See if can proceed with service group creation.
    ts::Errata::Code code = code_max(zret);
    if (code >= LVL_WARN) {
      zret = Unable_To_Create_Service_Group(svc_line).set(zret);
      return zret;
    }

    // Properties after this are optional so we can proceed if they fail.
    GroupData &svc = this->defineServiceGroup(svc_info);

    // Is there a process we should track?
    if ((prop = svc_cfg[SVC_PROP_PROC]).hasValue()) {
      if (ts::config::StringValue == prop.getType()) {
        svc.setProcName(prop.getText());
      } else {
        zret.push(Prop_Invalid_Type(prop, ts::config::StringValue));
      }
    }

    // Add seed routers.
    std::vector<uint32_t>::iterator rspot, rlimit;
    for (rspot = routers.begin(), rlimit = routers.end(); rspot != rlimit; ++rspot)
      svc.seedRouter(*rspot);
    for (rspot = Seed_Router.begin(), rlimit = Seed_Router.end(); rspot != rlimit; ++rspot)
      svc.seedRouter(*rspot);

    if (use_group_local_security)
      svc.setSecurity(security_style).setKey(md5_key);

    // Look for optional properties.

    svc.m_packet_forward = ServiceGroup::GRE; // default
    if ((prop = svc_cfg[SVC_PROP_FORWARD]).hasValue()) {
      ts::Errata status = load_option_set(prop, FORWARD_OPTS, N_FORWARD_OPTS);
      bool gre          = FORWARD_OPTS[0].m_found;
      bool l2           = FORWARD_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_forward = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.isOK())
          zret.push(Ignored_Opt_Errors(SVC_PROP_FORWARD, prop.getSourceLine()).set(status));
      } else {
        zret.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to GRE forwarding.").set(status));
      }
    }

    svc.m_packet_return = ServiceGroup::GRE; // default.
    if ((prop = svc_cfg[SVC_PROP_RETURN]).hasValue()) {
      ts::Errata status = load_option_set(prop, RETURN_OPTS, N_RETURN_OPTS);
      bool gre          = RETURN_OPTS[0].m_found;
      bool l2           = RETURN_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_return = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.isOK())
          zret.push(Ignored_Opt_Errors(SVC_PROP_RETURN, prop.getSourceLine()).set(status));
      } else {
        zret.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to GRE return.").set(status));
      }
    }

    svc.m_cache_assign = ServiceGroup::HASH_ONLY; // default
    if ((prop = svc_cfg[SVC_PROP_ASSIGN]).hasValue()) {
      ts::Errata status = load_option_set(prop, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      bool hash         = ASSIGN_OPTS[0].m_found;
      bool mask         = ASSIGN_OPTS[1].m_found;
      if (hash || mask) {
        svc.m_cache_assign = hash ? mask ? ServiceGroup::HASH_OR_MASK : ServiceGroup::HASH_ONLY : ServiceGroup::MASK_ONLY;
        if (!status.isOK())
          zret.push(Ignored_Opt_Errors(SVC_PROP_ASSIGN, prop.getSourceLine()).set(status));
      } else {
        status.push(ts::Errata::Message(26, LVL_INFO, "Defaulting to hash assignment only."));
        zret.push(List_Valid_Opts(prop.getName(), src_line, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS)).set(status));
      }
    }
  }
  return zret;
}

} // namespace.
