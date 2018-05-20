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
using ts::Errata;
using ts::Severity;

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

namespace ts
{
BufferWriter &
bwformat(BufferWriter &w, const BWFSpec &spec, const ConstBuffer &buff)
{
  return w.write(buff.data(), buff.size());
}
BufferWriter &
bwformat(BufferWriter &w, const BWFSpec &spec, const config::ValueType &vt)
{
  switch (vt) {
  case config::ValueType::VoidValue:
    w.print("Void");
    break;
  case config::ValueType::ListValue:
    w.print("List");
    break;
  case config::ValueType::GroupValue:
    w.print("Group");
    break;
  case config::ValueType::StringValue:
    w.print("String");
    break;
  case config::ValueType::IntegerValue:
    w.print("Integer");
    break;
  case config::ValueType::PathValue:
    w.print("Path");
    break;
  }
  return w;
}
} // namespace ts

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
  const char *m_text; ///< Text value of the option.
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

struct ValueNamePrinter {
  Value const &_v;
  ValueNamePrinter(Value const &v) : _v(v) {}
};

ts::BufferWriter &
bwformat(ts::BufferWriter &w, const ts::BWFSpec &spec, const ValueNamePrinter &v)
{
  ts::ConstBuffer const &name = v._v.getName();
  if (name._ptr)
    w.write('\'').write(name._ptr).write('\'');
  else
    w.print("{}", v._v.getIndex());
  return w;
}

Errata &
Unable_To_Create_Service_Group(Errata &erratum, int line)
{
  erratum.note(Severity::FATAL, "Unable to create service group at line {} because of configuration errors.", line);
  return erratum;
}

Errata &
Services_Not_Found(Errata &erratum)
{
  return erratum.note(Severity::INFO, "No services found in configuration.");
}

Errata &
Services_Not_A_Sequence(Errata &erratum)
{
  return erratum.note(Severity::INFO, "The 'services' setting was not a list nor array.");
}

Errata &
Service_Not_A_Group(Errata &erratum, int line)
{
  return erratum.note(Severity::WARN, "{} must be a group at line {}.", SVC_NAME, line);
}

Errata &
Service_Type_Defaulted(Errata &erratum, wccp::ServiceGroup::Type type, int line)
{
  erratum.note(Severity::INFO, "'type' not found in {} at line {} -- defaulting to {}.", SVC_NAME, line,
               (type == wccp::ServiceGroup::STANDARD ? "STANDARD" : "DYNAMIC"));
  return erratum;
}

Errata &
Service_Type_Invalid(Errata &erratum, ts::ConstBuffer const &text, int line)
{
  erratum.note(Severity::WARN, "Service type '{}' at line {} invalid. Must be \"STANDARD\" or \"DYNAMIC\"", text, line);
  return erratum;
}

Errata &
Prop_Not_Found(Errata &erratum, const char *prop_name, char const *group_name, int line)
{
  erratum.note(Severity::WARN, "Required '{}' property not found in '{}' at line {}.", prop_name, group_name, line);
  return erratum;
}

Errata &
Prop_Invalid_Type(Errata &erratum, Value const &prop_cfg, ts::config::ValueType expected)
{
  erratum.note(Severity::WARN, "'{}' at line {} is of type '{}' instead of required type '{}'.", prop_cfg.getName(),
               prop_cfg.getSourceLine(), prop_cfg.getType(), expected);
  return erratum;
}

Errata &
Prop_List_Invalid_Type(Errata &erratum, Value const &elt_cfg, ts::config::ValueType expected)
{
  erratum.note(Severity::WARN,
               "Element '{}' at line {} in the aggregate property '{}' is of type '{}' instead of required type '{}'.",
               ValueNamePrinter(elt_cfg), elt_cfg.getSourceLine(), elt_cfg.getParent().getName(), elt_cfg.getType(), expected);
  return erratum;
}

Errata &
Svc_Prop_Out_Of_Range(Errata &erratum, const char *name, Value const &elt_cfg, int v, int min, int max)
{
  erratum.note(Severity::WARN, "Service property '{}' at line {} has a value '{}' that is not in the allowed range of {}..{}.",
               name, elt_cfg.getSourceLine(), v, min, max);
  return erratum;
}

Errata &
Svc_Prop_Ignored(Errata &erratum, const char *name, int line)
{
  erratum.note(Severity::INFO, "Service property '{}' at line {} ignored because the service is of type standard.", name, line);
  return erratum;
}

Errata &
Svc_Ports_Too_Many(Errata &erratum, int line, int n)
{
  erratum.note(Severity::WARN, "Excess ports ignored at line {}. {} ports specified, only {} supported.", line, n,
               wccp::ServiceGroup::N_PORTS);
  return erratum;
}

Errata &
Svc_Ports_Malformed(Errata &erratum, int line)
{
  erratum.note(Severity::INFO, "Port value ignored (not a number) at line {}.", line);
  return erratum;
}

Errata &
Svc_Ports_None_Valid(Errata &erratum, int line)
{
  erratum.note(Severity::WARN, "A '{}' property was found at line {} but none of the ports were valid.", SVC_PROP_PORTS, line);
  return erratum;
}

Errata &
Svc_Ports_Not_Found(Errata &erratum, int line)
{
  erratum.note(Severity::WARN, "Ports not found in service at line {}. Ports must be defined for a dynamic service.", line);
  return erratum;
}

Errata &
Svc_Prop_Ignored_In_Standard(Errata &erratum, const char *name, int line)
{
  return erratum.note(Severity::INFO, "Service property '{}' at line {} ignored because the service is of type STANDARD.", name,
                      line);
}

Errata &
Security_Opt_Invalid(Errata &erratum, ts::ConstBuffer const &text, int line)
{
  return erratum.note(Severity::WARN, "Security option '{}' at line {} is invalid. It must be 'none' or 'md5'.", text, line);
}

Errata &
Value_Malformed(Errata &erratum, const char *name, char const *text, int line)
{
  return erratum.note(Severity::WARN, "'{}' value '{}' malformed at line {}.", name, text, line);
}

Errata &
No_Valid_Routers(Errata &erratum, int line)
{
  return erratum.note(Severity::WARN, "No valid IP address for routers found for Service Group at line {}.", line);
}

Errata &
Ignored_Option_Value(Errata &erratum, ts::ConstBuffer const &text, ts::ConstBuffer const &name, int line)
{
  return erratum.note(Severity::INFO, "Value '{}' at line {} was ignored because it is not a valid option for '{}'.", text, line,
                      name);
}

Errata &
Ignored_Opt_Errors(Errata &erratum, const char *name, int line)
{
  return erratum.note(Severity::INFO, "Errors in  '{}' at line {} were ignored.", name, line);
}

Errata &
List_Valid_Opts(Errata &erratum, ts::ConstBuffer const &name, int line, CfgString *values, size_t n)
{
  ts::LocalBufferWriter<1024> w;
  w.print("Valid values for the '{}' property at line {} are '{}'", name, line, values[0].m_text);
  for (size_t i = 1; i < n; ++i)
    w.print(", '{}'", values[i].m_text);
  w.write('.');
  return erratum.note(Severity::INFO, w.view());
}

Errata &
Port_Type_Invalid(Errata &erratum, ts::ConstBuffer const &text, int line)
{
  return erratum.note(Severity::WARN, "Value '{}' at line {} for property '{}' is invalid. It must be 'src' or 'dst'.", text, line,
                      SVC_PROP_PORT_TYPE);
}

} // namespace

namespace wccp
{
inline bool
operator==(ts::ConstBuffer const &b, const char *text)
{
  return 0 == strncasecmp(text, b._ptr, b._size);
}

inline bool
operator==(const char *text, ts::ConstBuffer const &b)
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
          Ignored_Option_Value(zret, text, name, item.getSourceLine());
          list_opts = true;
        }
      } else {
        Prop_Invalid_Type(zret, setting, ts::config::StringValue);
      }
    }
    if (list_opts)
      List_Valid_Opts(zret, name, src_line, opts, count);
  } else {
    Prop_Invalid_Type(zret, setting, ts::config::ListValue);
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
              Prop_Invalid_Type(zret, key, ts::config::StringValue);
            }
          } else {
            Prop_Not_Found(zret, SECURITY_PROP_KEY, SVC_PROP_SECURITY, src_line);
          }
        } else {
          Security_Opt_Invalid(zret, text, opt.getSourceLine());
        }
      } else {
        Prop_Invalid_Type(zret, opt, ts::config::StringValue);
      }
    } else {
      Prop_Not_Found(zret, SECURITY_PROP_OPTION, SVC_PROP_SECURITY, src_line);
    }
  } else {
    Prop_Invalid_Type(zret, setting, ts::config::GroupValue);
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
  const char *text;
  static const char *const NAME = "IPv4 Address";

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
          Value_Malformed(zret, NAME, text, addr_line);
      } else {
        Prop_List_Invalid_Type(zret, addr_cfg, ts::config::StringValue);
      }
    }
  } else {
    Prop_Invalid_Type(zret, setting, ts::config::ListValue);
  }
  return zret;
}

ts::Errata &&
CacheImpl::loadServicesFromFile(const char *path)
{
  ts::Errata zret;
  int src_line = 0;              // scratch for local source line caching.
  std::vector<uint32_t> routers; // scratch per service loop.
  Value prop;                    // scratch var.

  ts::Rv<Configuration> cv = Configuration::loadFromPath(path);
  if (!cv.is_ok())
    return std::move(cv.errata());

  ts::config::Configuration cfg = cv.result();
  Value svc_list                = cfg.find("services");
  // No point in going on from here.
  if (!svc_list)
    return std::move(Services_Not_Found(zret));

  if (!svc_list.isContainer())
    return std::move(Services_Not_A_Sequence(zret));

  // Check for global (default) security setting.
  if ((prop = cfg[SVC_PROP_SECURITY]).hasValue()) {
    ts::Rv<ts::ConstBuffer> rv = load_security(prop);
    if (rv.is_ok())
      this->useMD5Security(rv);
    else
      zret.note(rv.errata());
  }

  if ((prop = cfg[SVC_PROP_ROUTERS]).hasValue()) {
    zret.note(load_routers(prop, Seed_Router).clear());
  }

  int idx, nsvc;
  for (idx = 0, nsvc = svc_list.childCount(); idx < nsvc; ++idx) {
    int x; // scratch int.
    const char *md5_key = 0;
    ts::ConstBuffer text;
    SecurityOption security_style = SECURITY_NONE;
    bool use_group_local_security = false;
    Value const &svc_cfg          = svc_list[idx];
    int svc_line                  = svc_cfg.getSourceLine();
    ServiceGroup svc_info;

    if (ts::config::GroupValue != svc_cfg.getType()) {
      Service_Not_A_Group(zret, svc_line);
      continue;
    }

    // Get the service ID.
    if ((prop = svc_cfg[SVC_PROP_ID]).hasValue()) {
      if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setSvcId(x);
        else
          Svc_Prop_Out_Of_Range(zret, SVC_PROP_ID, prop, x, 0, 255);
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::IntegerValue);
      }
    } else {
      Prop_Not_Found(zret, SVC_PROP_ID, SVC_NAME, svc_line);
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
          Service_Type_Invalid(zret, text, prop.getSourceLine());
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::StringValue);
      }
    } else { // default type based on ID.
      ServiceGroup::Type svc_type = svc_info.getSvcId() <= ServiceGroup::RESERVED ? ServiceGroup::STANDARD : ServiceGroup::DYNAMIC;
      svc_info.setSvcType(svc_type);
      Service_Type_Defaulted(zret, svc_type, svc_line);
    }

    // Get the protocol.
    if ((prop = svc_cfg[SVC_PROP_PROTOCOL]).hasValue()) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        Svc_Prop_Ignored(zret, SVC_PROP_PROTOCOL, prop.getSourceLine());
      } else if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setProtocol(x);
        else
          Svc_Prop_Out_Of_Range(zret, SVC_PROP_ID, prop, x, 0, 255);
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::IntegerValue);
      }
    } else if (svc_info.getSvcType() != ServiceGroup::STANDARD) {
      // Required if it's not standard / predefined.
      Prop_Not_Found(zret, SVC_PROP_PROTOCOL, SVC_NAME, svc_line);
    }

    // Get the priority.
    svc_info.setPriority(0); // OK to default to this value.
    if ((prop = svc_cfg[SVC_PROP_PRIORITY]).hasValue()) {
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        Svc_Prop_Ignored(zret, SVC_PROP_PRIORITY, prop.getSourceLine());
      } else if (ts::config::IntegerValue == prop.getType()) {
        x = atoi(prop.getText()._ptr);
        if (0 <= x && x <= 255)
          svc_info.setPriority(x);
        else
          Svc_Prop_Out_Of_Range(zret, SVC_PROP_ID, prop, x, 0, 255);
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::IntegerValue);
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
          Ignored_Opt_Errors(zret, SVC_PROP_PRIMARY_HASH, src_line).note(status);
      } else {
        List_Valid_Opts(zret, prop.getName(), src_line, HASH_OPTS, N_OPTS(HASH_OPTS)).note(status);
      }
    } else {
      Prop_Not_Found(zret, SVC_PROP_PRIMARY_HASH, SVC_NAME, svc_line);
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
        Ignored_Opt_Errors(zret, SVC_PROP_ALT_HASH, src_line).note(status);
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
          Port_Type_Invalid(zret, text, src_line);
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::StringValue);
      }
    }

    // Ports for service.
    svc_info.clearPorts();
    if ((prop = svc_cfg[SVC_PROP_PORTS]).hasValue()) {
      src_line = prop.getSourceLine();
      if (ServiceGroup::STANDARD == svc_info.getSvcType()) {
        Svc_Prop_Ignored_In_Standard(zret, SVC_PROP_PORTS, src_line);
      } else {
        if (prop.isContainer()) {
          size_t nport = prop.childCount();
          size_t pidx, sidx;
          bool malformed_error = false;
          // Clip to maximum protocol allowed ports.
          if (nport > ServiceGroup::N_PORTS) {
            Svc_Ports_Too_Many(zret, src_line, nport);
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
                Svc_Prop_Out_Of_Range(zret, SVC_PROP_PORTS, port_cfg, x, 0, 65535);
            } else if (!malformed_error) { // only report this once.
              Svc_Ports_Malformed(zret, src_line);
              malformed_error = true;
            }
          }
          if (sidx)
            svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
          else
            Svc_Ports_None_Valid(zret, src_line);
        } else {
          Prop_Invalid_Type(zret, prop, ts::config::ListValue);
        }
      }
    } else if (ServiceGroup::STANDARD != svc_info.getSvcType()) {
      Svc_Ports_Not_Found(zret, svc_line);
    }

    // Security option for this service group.
    if ((prop = svc_cfg[SVC_PROP_SECURITY]).hasValue()) {
      ts::Rv<ts::ConstBuffer> security = load_security(prop);
      if (security.is_ok()) {
        use_group_local_security = true;
        if (security.result()._ptr) {
          md5_key        = security.result()._ptr;
          security_style = SECURITY_MD5;
        } else {
          security_style = SECURITY_NONE;
        }
      }
      zret.note(security.errata());
    }

    // Get any group specific routers.
    routers.clear(); // reset list.
    if ((prop = svc_cfg[SVC_PROP_ROUTERS]).hasValue()) {
      ts::Errata status = load_routers(prop, routers);
      if (!status)
        zret.note(Severity::INFO, "Router specification invalid.").note(status);
    }
    if (!routers.size() && !Seed_Router.size())
      No_Valid_Routers(zret, svc_line);

    // See if can proceed with service group creation.
    if (zret.severity() >= Severity::WARN) {
      Unable_To_Create_Service_Group(zret, svc_line);
      return std::move(zret);
    }

    // Properties after this are optional so we can proceed if they fail.
    GroupData &svc = this->defineServiceGroup(svc_info);

    // Is there a process we should track?
    if ((prop = svc_cfg[SVC_PROP_PROC]).hasValue()) {
      if (ts::config::StringValue == prop.getType()) {
        svc.setProcName(prop.getText());
      } else {
        Prop_Invalid_Type(zret, prop, ts::config::StringValue);
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
        if (!status.is_ok())
          Ignored_Opt_Errors(zret.note(status), SVC_PROP_FORWARD, prop.getSourceLine());
      } else {
        zret.note(status).note(Severity::INFO, "Defaulting to GRE forwarding.");
      }
    }

    svc.m_packet_return = ServiceGroup::GRE; // default.
    if ((prop = svc_cfg[SVC_PROP_RETURN]).hasValue()) {
      ts::Errata status = load_option_set(prop, RETURN_OPTS, N_RETURN_OPTS);
      bool gre          = RETURN_OPTS[0].m_found;
      bool l2           = RETURN_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_return = gre ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE : ServiceGroup::L2;
        if (!status.is_ok())
          Ignored_Opt_Errors(zret.note(status), SVC_PROP_RETURN, prop.getSourceLine());
      } else {
        zret.note(status).note(Severity::INFO, "Defaulting to GRE return.");
      }
    }

    svc.m_cache_assign = ServiceGroup::HASH_ONLY; // default
    if ((prop = svc_cfg[SVC_PROP_ASSIGN]).hasValue()) {
      ts::Errata status = load_option_set(prop, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      bool hash         = ASSIGN_OPTS[0].m_found;
      bool mask         = ASSIGN_OPTS[1].m_found;
      if (hash || mask) {
        svc.m_cache_assign = hash ? mask ? ServiceGroup::HASH_OR_MASK : ServiceGroup::HASH_ONLY : ServiceGroup::MASK_ONLY;
        if (!status.is_ok())
          Ignored_Opt_Errors(zret.note(status), SVC_PROP_ASSIGN, prop.getSourceLine());
      } else {
        status.note(Severity::INFO, "Defaulting to hash assignment only.");
        List_Valid_Opts(zret.note(status), prop.getName(), src_line, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      }
    }
  }
  return std::move(zret);
}

} // namespace wccp
