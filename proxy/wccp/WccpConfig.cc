# include "WccpLocal.h"
# include <sstream>
# include <libconfig.h++>
# include <arpa/inet.h>
# include <iostream>
# include <errno.h>

// Support that must go in the standard namespace.
namespace std {
ostream& operator << ( ostream& s, libconfig::Setting::Type const& t ) {
  switch (t) {
  case libconfig::Setting::TypeString: s << "string"; break;
  case libconfig::Setting::TypeInt: s << "integer"; break;
  case libconfig::Setting::TypeInt64: s << "integer (64 bit)"; break;
  case libconfig::Setting::TypeFloat: s << "floating point"; break;
  case libconfig::Setting::TypeBoolean: s << "boolean"; break;
  case libconfig::Setting::TypeArray: s << "array"; break;
  case libconfig::Setting::TypeList: s << "list"; break;
  case libconfig::Setting::TypeGroup: s << "group"; break;
  default: s << "*unknown*"; break;
  };
  return s;
}
} // namespace std

// WCCP related things that are file local.
namespace {
using namespace Wccp;

// Scratch global list of seed router addresses.
// Yeah, not thread safe, but it's just during configuration load.
std::vector<ats::uint32> Seed_Router;

// Names used for various elements and properties.
static char const * const SVC_NAME = "service";

static char const * const SVC_PROP_ID = "id";
static char const * const SVC_PROP_TYPE = "type";
static char const * const SVC_PROP_PRIORITY = "priority";
static char const * const SVC_PROP_PROTOCOL = "protocol";
static char const * const SVC_PROP_FLAGS = "flags";
static char const * const SVC_PROP_PRIMARY_HASH = "primary-hash";
static char const * const SVC_PROP_ALT_HASH = "alt-hash";
static char const * const SVC_PROP_PORTS = "ports";
static char const * const SVC_PROP_PORT_TYPE = "port-type";
static char const * const SVC_PROP_SECURITY = "security";
static char const * const SVC_PROP_ROUTERS = "routers";
static char const * const SVC_PROP_FORWARD = "forward";
static char const * const SVC_PROP_RETURN = "return";
static char const * const SVC_PROP_ASSIGN = "assignment";

static char const * const SECURITY_PROP_OPTION = "option";
static char const * const SECURITY_PROP_KEY = "key";

/// Helper structure for processing configuration strings.
struct CfgString {
  char const* m_text; ///< Text value of the option.
  bool m_found; ///< String was found.
};
typedef std::vector<CfgString> CfgOpts;

# define N_OPTS(x) (sizeof(x) / sizeof(*x))

CfgString FORWARD_OPTS[] = { { "gre" } , { "l2" } };
size_t const N_FORWARD_OPTS = sizeof(FORWARD_OPTS)/sizeof(*FORWARD_OPTS);

CfgString RETURN_OPTS[] = { { "gre" } , { "l2" } };
size_t const N_RETURN_OPTS = sizeof(RETURN_OPTS)/sizeof(*RETURN_OPTS);

CfgString ASSIGN_OPTS[] = { { "hash" } , { "mask" } };

CfgString HASH_OPTS[] = { { "src_ip" } , { "dst_ip" } , { "src_port" } , { "dst_port" } };

ats::Errata::Code code_max(ats::Errata const& err) {
  ats::Errata::Code zret = std::numeric_limits<ats::Errata::Code::raw_type>::min();
  ats::Errata::const_iterator spot = err.begin();
  ats::Errata::const_iterator limit = err.end();
  for ( ; spot != limit ; ++spot )
    zret = std::max(zret, spot->getCode());
  return zret;
}

ats::Errata::Message File_Syntax_Error(int line, char const* text) {
  std::ostringstream out;
  out << "Service configuration error. Line "
      << line
      << ": " << text
    ;
  return ats::Errata::Message(1, LVL_FATAL, out.str());
}

ats::Errata::Message File_Read_Error(char const* text) {
  std::ostringstream out;
  out << "Failed to parse configuration file."
      << ": " << text
    ;
  return ats::Errata::Message(2, LVL_FATAL, out.str());
}

ats::Errata::Message Unable_To_Create_Service_Group(int line) {
  std::ostringstream out;
  out << "Unable to create service group at line " << line
      << " because of configuration errors."
    ;
  return ats::Errata::Message(23, LVL_FATAL, out.str());
}

ats::Errata::Message Services_Not_Found() {
  return ats::Errata::Message(3, LVL_INFO, "No services found in configuration.");
}

ats::Errata::Message Services_Not_A_Sequence() {
  return ats::Errata::Message(4, LVL_INFO, "The 'services' setting was not a list nor array.");
}

ats::Errata::Message Service_Not_A_Group(int line) {
  std::ostringstream out;
  out << "'" << SVC_NAME << "' must be a group at line "
      << line << "."
    ;
  return ats::Errata::Message(5, LVL_WARN, out.str());
}

ats::Errata::Message Service_Type_Defaulted(Wccp::ServiceGroup::Type type, int line) {
  std::ostringstream out;
  out << "'type' not found in " << SVC_NAME << " at line "
      << line << "' -- defaulting to "
      << ( type == Wccp::ServiceGroup::STANDARD ? "STANDARD" : "DYNAMIC" )
    ;
  return ats::Errata::Message(6, LVL_INFO, out.str());
}

ats::Errata::Message Service_Type_Invalid(char const* text, int line) {
  std::ostringstream out;
  out << "Service type '" << text 
      << "' at line " << line
      << " invalid. Must be \"STANDARD\" or \"DYNAMIC\""
    ;
  return ats::Errata::Message(7, LVL_WARN, out.str());
}

ats::Errata::Message Prop_Not_Found(char const* prop_name, char const* group_name, int line) {
  std::ostringstream out;
  out << "Required '" << prop_name << "' property not found in '"
      << group_name << "' at line " << line << "."
    ;
  return ats::Errata::Message(8, LVL_WARN, out.str());
}

ats::Errata::Message Prop_Invalid_Type(
  libconfig::Setting& prop_cfg,
  libconfig::Setting::Type expected
) {
  std::ostringstream out;
  out << "'" << prop_cfg.getName() << "' at line " << prop_cfg.getSourceLine()
      << " is of type '" << prop_cfg.getType()
      << "' instead of required type '" << expected << "'."
    ;
  return ats::Errata::Message(9, LVL_WARN, out.str());
}

ats::Errata::Message Prop_List_Invalid_Type(
  libconfig::Setting& elt_cfg, ///< List element.
  libconfig::Setting::Type expected
) {
  std::ostringstream out;
  out << "Element ";
  if (elt_cfg.getName()) out << "'" << elt_cfg.getName() << "'";
  else out << elt_cfg.getIndex();
  out << " at line " << elt_cfg.getSourceLine()
      << " in the aggregate property '" << elt_cfg.getParent().getName()
      << "' is of type '" << elt_cfg.getType()
      << "' instead of required type '" << expected << "'."
    ;
  return ats::Errata::Message(9, LVL_WARN, out.str());
}

ats::Errata::Message Svc_Prop_Out_Of_Range(
  char const* name,
  libconfig::Setting& elt_cfg,
  int v, int min, int max
) {
  std::ostringstream out;
  out << "Service property '" << name
      << "' at line " << elt_cfg.getSourceLine()
      << " has a value " << v
      << " that is not in the allowed range of "
      << min << ".." << max << "."
    ;
  return ats::Errata::Message(10, LVL_WARN, out.str());
}

ats::Errata::Message Svc_Prop_Ignored(char const* name, int line) {
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line
      << " ignored because the service is of type standard."
    ;
  return ats::Errata::Message(11, LVL_INFO, out.str());
}

ats::Errata::Message Svc_Flags_No_Hash_Set(int line) {
  std::ostringstream out;
  out << "Service flags have no hash set at line " << line
    ;
  return ats::Errata::Message(12, LVL_WARN, out.str());
}

ats::Errata::Message Svc_Flags_Ignored(int line) {
  std::ostringstream out;
  out << "Invalid service flags at line  " << line
      << " ignored."
    ;
  return ats::Errata::Message(13, LVL_INFO, out.str());
}

ats::Errata::Message Svc_Ports_Too_Many(int line, int n) {
  std::ostringstream out;
  out << "Excess ports ignored at line " << line
      << ". " << n << " ports specified, only" 
      << Wccp::ServiceGroup::N_PORTS << " supported."
    ;
  return ats::Errata::Message(14, LVL_INFO, out.str());
}

ats::Errata::Message Svc_Ports_Malformed(int line) {
  std::ostringstream out;
  out << "Port value ignored (not a number) at line " << line
      << "."
    ;
  return ats::Errata::Message(15, LVL_INFO, out.str());
}

ats::Errata::Message Svc_Ports_None_Valid(int line) {
  std::ostringstream out;
  out << "A '" << SVC_PROP_PORTS << "' property was found at line "
      << line << " but none of the ports were valid."
    ;
  return  ats::Errata::Message(17, LVL_WARN, out.str());
}

ats::Errata::Message Svc_Ports_Not_Found(int line) {
  std::ostringstream out;
  out << "Ports not found in service at line " << line
      << ". Ports must be defined for a dynamic service.";
  return  ats::Errata::Message(18, LVL_WARN, out.str());
}

ats::Errata::Message Svc_Prop_Ignored_In_Standard(const char* name, int line) {
  std::ostringstream out;
  out << "Service property '" << name << "' at line " << line
      << " ignored because the service is of type STANDARD."
    ;
  return ats::Errata::Message(19, LVL_INFO, out.str());
}

ats::Errata::Message Security_Opt_Invalid(char const* text, int line) {
  std::ostringstream out;
  out << "Security option '" << text
      << "' at line " << line
      << " is invalid. It must be 'none' or 'md5'."
    ;
  return ats::Errata::Message(20, LVL_WARN, out.str());
}

ats::Errata::Message Value_Malformed(char const* name, char const* text, int line) {
  std::ostringstream out;
  out << "'" << name << "' value '" << text
      << "' malformed at line " << line << "."
    ;
  return ats::Errata::Message(21, LVL_WARN, out.str());
}

ats::Errata::Message No_Valid_Routers(int line) {
  std::ostringstream out;
  out << "No valid IP address for routers found for Service Group at line "
      << line << "."
    ;
  return ats::Errata::Message(22, LVL_WARN, out.str());
}

ats::Errata::Message Ignored_Option_Value(
  char const* text,
  char const* name,
  int line
) {
  std::ostringstream out;
  out << "Value '" << text << "' at line " << line
      << " was ignored because it is not a valid option for '"
      << name << "'."
    ;
  return ats::Errata::Message(24, LVL_INFO, out.str());
}

ats::Errata::Message Ignored_Opt_Errors(
  char const* name,
  int line
) {
  std::ostringstream out;
  out << "Errors in  '" << name << "' at line " << line
      << " were ignored."
    ;
  return ats::Errata::Message(28, LVL_INFO, out.str());
}

ats::Errata::Message List_Valid_Opts(
  char const* name,
  int line,
  CfgString* values,
  size_t n
) {
  std::ostringstream out;
  out << "Valid values for the '" << name << "' property at line " << line
      << " are: "
    ;
  out << '"' << values[0].m_text << '"';
  for ( size_t i = 1 ; i < n ; ++i )
    out << ", \"" << values[i].m_text << '"';
  out << '.';
  return ats::Errata::Message(29, LVL_INFO, out.str());
}

ats::Errata::Message Port_Type_Invalid(char const* text, int line) {
  std::ostringstream out;
  out << "Value '" << text
      << "' at line " << line
      << "for property '" << SVC_PROP_PORT_TYPE
      << "' is invalid. It must be 'src' or 'dst'."
    ;
  return ats::Errata::Message(30, LVL_WARN, out.str());
}

} // anon namespace

namespace Wccp {

ats::Errata
load_option_set(libconfig::Setting& setting, CfgString* opts, size_t count) {
  ats::Errata zret;
  CfgString* spot;
  CfgString* limit = opts + count;
  char const* name = setting.getName();
  int src_line = setting.getSourceLine();

  // Clear all found flags.
  for ( spot = opts ; spot < limit ; ++spot ) spot->m_found = false;

  // Walk through the strings in the setting.
  if (setting.isAggregate()) {
    int nr = setting.getLength();
    bool list_opts = false;
    for ( int i = 0 ; i < nr ; ++i ) {
      libconfig::Setting& item = setting[i];
      if (libconfig::Setting::TypeString == item.getType()) {
        char const* text = static_cast<char const*>(item);
        for ( spot = opts ; spot < limit ; ++spot ) {
          if (0 == strcasecmp(text, spot->m_text)) {
            spot->m_found = true;
            break;
          }
        }
        if (spot >= limit) {
          zret.push(Ignored_Option_Value(text, name, item.getSourceLine()));
          list_opts = true;
        }
      } else {
        zret.push(Prop_Invalid_Type(setting, libconfig::Setting::TypeString));
      }
    }
    if (list_opts)
      zret.push(List_Valid_Opts(name, src_line, opts, count));
  } else {
    zret.push(Prop_Invalid_Type(setting, libconfig::Setting::TypeArray));
  }
  return zret;
}

/** On success this returns a non @c NULL pointer if the MD5 option is
    set.  In that case the pointer points at the MD5 key.  Otherwise
    the option was none and the pointer is @c NULL
 */
ats::Rv<char const*>
load_security (
  libconfig::Setting& setting ///< Security setting.
) {
  ats::Rv<char const*> zret;
  int src_line;
  char const* text;

  zret = 0;

  src_line = setting.getSourceLine();
  if (libconfig::Setting::TypeGroup == setting.getType()) {
    if (setting.exists(SECURITY_PROP_OPTION)) {
      libconfig::Setting& opt = setting[SECURITY_PROP_OPTION];
      if (libconfig::Setting::TypeString == opt.getType()) {
        text = static_cast<char const*>(opt);
        if (0 == strcasecmp("none", text)) {
        } else if (0 == strcasecmp("md5", text)) {
          if (setting.exists(SECURITY_PROP_KEY)) {
            libconfig::Setting& key = setting[SECURITY_PROP_KEY];
            if (libconfig::Setting::TypeString == key.getType()) {
              zret = static_cast<char const*>(key);
            } else {
              zret.push(Prop_Invalid_Type(key, libconfig::Setting::TypeString));
            }
          } else {
            zret.push(Prop_Not_Found(SECURITY_PROP_KEY, SVC_PROP_SECURITY, src_line));
          }
        } else {
          zret.push(Security_Opt_Invalid(text, opt.getSourceLine()));
        }
      } else {
        zret.push(Prop_Invalid_Type(opt, libconfig::Setting::TypeString));
      }
    } else {
      zret.push(Prop_Not_Found(SECURITY_PROP_OPTION, SVC_PROP_SECURITY, src_line));
    }
  } else {
    zret.push(Prop_Invalid_Type(setting, libconfig::Setting::TypeGroup));
  }
  return zret;
}

/// Process a router address list.
ats::Errata
load_routers (
  libconfig::Setting& setting, ///< Source of addresses.
  std::vector<uint32>& addrs ///< Output list
) {
  ats::Errata zret;
  int src_line;
  char const* text;
  static char const * const NAME = "IPv4 Address";

  src_line = setting.getSourceLine();
  if (setting.isAggregate()) {
    int nr = setting.getLength();
    for ( int i = 0 ; i < nr ; ++i ) {
      libconfig::Setting& addr_cfg = setting[i];
      int addr_line = addr_cfg.getSourceLine();
      in_addr addr;
      if (libconfig::Setting::TypeString == addr_cfg.getType()) {
        text = static_cast<char const*>(addr_cfg);
        if (inet_aton(text, &addr)) addrs.push_back(addr.s_addr);
        else zret.push(Value_Malformed(NAME, text, addr_line));
      } else {
        zret.push(Prop_List_Invalid_Type(addr_cfg, libconfig::Setting::TypeString));
      }
    }
  } else {
    zret.push(Prop_Invalid_Type(setting, libconfig::Setting::TypeArray));
  }
  return zret;
}

ats::Errata
CacheImpl::loadServicesFromFile(char const* path) {
  ats::Errata zret;
  libconfig::Config cfg;
  int src_line = 0; // scratch for local source line caching.
  std::vector<uint32> routers; // scratch per service loop.

  // Can we read and parse the file?
  try {
    cfg.readFile(path);
  } catch (libconfig::ParseException & x) {
    return File_Syntax_Error(x.getLine(), x.getError());
  } catch (std::exception const& x) {
    return File_Read_Error(x.what());
  }

  // No point in going on from here.
  if (!cfg.exists("services")) return Services_Not_Found();

  libconfig::Setting& svc_list = cfg.lookup("services");
  if (!svc_list.isAggregate()) return Services_Not_A_Sequence();

  // Check for global (default) security setting.
  if (cfg.exists(SVC_PROP_SECURITY)) {
    libconfig::Setting& security = cfg.lookup(SVC_PROP_SECURITY);
    ats::Rv<char const*> rv = load_security(security);
    if (rv.isOK()) this->useMD5Security(rv);
    else zret.join(rv.errata());
  }

  if (cfg.exists(SVC_PROP_ROUTERS)) {
    libconfig::Setting& routers = cfg.lookup(SVC_PROP_ROUTERS);
    zret.join(load_routers(routers, Seed_Router));
  }

  int idx, nsvc;
  for ( idx = 0, nsvc = svc_list.getLength() ; idx < nsvc ; ++idx ) {
    int x; // scratch int.
    char const* text; // scratch text.
    char const* md5_key = 0;
    SecurityOption security_style = SECURITY_NONE;
    bool use_group_local_security = false;
    libconfig::Setting& svc_cfg = svc_list[idx];
    int svc_line = svc_cfg.getSourceLine();
    ServiceGroup svc_info;

    if (!svc_cfg.isGroup()) {
      zret.push(Service_Not_A_Group(svc_line));
      continue;
    }

    // Get the service ID.
    if (svc_cfg.exists(SVC_PROP_ID)) {
      libconfig::Setting& id_prop = svc_cfg[SVC_PROP_ID];
      if (id_prop.isNumber()) {
        x = static_cast<int>(id_prop);
        if (0 <= x && x <= 255)
          svc_info.setSvcId(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, id_prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(id_prop, libconfig::Setting::TypeInt));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_ID, SVC_NAME, svc_line));
    }

    // Service type.
    if (svc_cfg.exists(SVC_PROP_TYPE)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_TYPE];
      if (libconfig::Setting::TypeString == prop.getType()) {
        text = static_cast<char const*>(prop);
        if (0 == strcasecmp("DYNAMIC", text))
          svc_info.setSvcType(ServiceGroup::DYNAMIC);
        else if (0 == strcasecmp("STANDARD", text))
          svc_info.setSvcType(ServiceGroup::STANDARD);
        else
          zret.push(Service_Type_Invalid(text, prop.getSourceLine()));
      } else {
        zret.push(Prop_Invalid_Type(prop, libconfig::Setting::TypeString));
      }
    } else { // default type based on ID.
      ServiceGroup::Type svc_type =
        svc_info.getSvcId() <= ServiceGroup::RESERVED
          ? ServiceGroup::STANDARD
          : ServiceGroup::DYNAMIC
        ;
      svc_info.setSvcType(svc_type);
      zret.push(Service_Type_Defaulted(svc_type, svc_line));
    }

    // Get the protocol.
    if (svc_cfg.exists(SVC_PROP_PROTOCOL)) {
      libconfig::Setting& proto_prop = svc_cfg[SVC_PROP_PROTOCOL];
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PROTOCOL, proto_prop.getSourceLine()));
      } else if (proto_prop.isNumber()) {
        x = static_cast<int>(proto_prop);
        if (0 <= x && x <= 255)
          svc_info.setProtocol(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, proto_prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(proto_prop, libconfig::Setting::TypeInt));
      }
    } else if (svc_info.getSvcType() != ServiceGroup::STANDARD) {
      // Required if it's not standard / predefined.
      zret.push(Prop_Not_Found(SVC_PROP_PROTOCOL, SVC_NAME, svc_line));
    }

    // Get the priority.
    svc_info.setPriority(0); // OK to default to this value.
    if (svc_cfg.exists(SVC_PROP_PRIORITY)) {
      libconfig::Setting& pri_prop = svc_cfg[SVC_PROP_PRIORITY];
      if (svc_info.getSvcType() == ServiceGroup::STANDARD) {
        zret.push(Svc_Prop_Ignored(SVC_PROP_PRIORITY, pri_prop.getSourceLine()));
      } else if (pri_prop.isNumber()) {
        x = static_cast<int>(pri_prop);
        if (0 <= x && x <= 255)
          svc_info.setPriority(x);
        else
          zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_ID, pri_prop, x, 0, 255));
      } else {
        zret.push(Prop_Invalid_Type(pri_prop, libconfig::Setting::TypeInt));
      }
    }

    // Service flags.
    svc_info.setFlags(0);

    if (svc_cfg.exists(SVC_PROP_PRIMARY_HASH)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_PRIMARY_HASH];
      ats::Errata status = load_option_set(prop, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32 f = 0;
      src_line = prop.getSourceLine();
      for ( size_t i = 0 ; i < N_OPTS(HASH_OPTS) ; ++i )
        if (HASH_OPTS[i].m_found) f |= ServiceGroup::SRC_IP_HASH << i;
      if (f) {
        svc_info.enableFlags(f);
        if (!status) zret.push(Ignored_Opt_Errors(SVC_PROP_PRIMARY_HASH, src_line).set(status));
      } else {
        zret.push(List_Valid_Opts(SVC_PROP_PRIMARY_HASH, src_line, HASH_OPTS, N_OPTS(HASH_OPTS)).set(status));
      }
    } else {
      zret.push(Prop_Not_Found(SVC_PROP_PRIMARY_HASH, SVC_NAME, svc_line));
    }

    if (svc_cfg.exists(SVC_PROP_ALT_HASH)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_ALT_HASH];
      ats::Errata status = load_option_set(prop, HASH_OPTS, N_OPTS(HASH_OPTS));
      uint32 f = 0;
      src_line = prop.getSourceLine();
      for ( size_t i = 0 ; i < N_OPTS(HASH_OPTS) ; ++i )
        if (HASH_OPTS[i].m_found) f |= ServiceGroup::SRC_IP_ALT_HASH << i;
      if (f) svc_info.enableFlags(f);
      if (!status) zret.push(Ignored_Opt_Errors(SVC_PROP_ALT_HASH, src_line).set(status));
    }

    if (svc_cfg.exists(SVC_PROP_PORT_TYPE)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_PORT_TYPE];
      src_line = prop.getSourceLine();
      if (libconfig::Setting::TypeString == prop.getType()) {
        text = static_cast<char const*>(prop);
        if (0 == strcasecmp("src", text))
          svc_info.enableFlags(ServiceGroup::PORTS_SOURCE);
        else if (0 == strcasecmp("dst", text))
          svc_info.disableFlags(ServiceGroup::PORTS_SOURCE);
        else
          zret.push(Port_Type_Invalid(text, src_line));
      } else {
        zret.push(Prop_Invalid_Type(prop, libconfig::Setting::TypeString));
      }
    }

    // Ports for service.
    svc_info.clearPorts();
    if (svc_cfg.exists(SVC_PROP_PORTS)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_PORTS];
      src_line = prop.getSourceLine();
      if (ServiceGroup::STANDARD == svc_info.getSvcType()) {
        zret.push(Svc_Prop_Ignored_In_Standard(SVC_PROP_PORTS, src_line));
      } else {
        if ( prop.isAggregate() ) {
          int nport = prop.getLength();
          int pidx, sidx;
          bool malformed_error = false;
          // Clip to maximum protocol allowed ports.
          if (nport > ServiceGroup::N_PORTS) {
            zret.push(Svc_Ports_Too_Many(src_line, nport));
            nport = ServiceGroup::N_PORTS;
          }
          // Step through the ports.
          for ( pidx = sidx = 0 ; pidx < nport ; ++pidx ) {
            libconfig::Setting& port_cfg = prop[pidx];
            if (port_cfg.isNumber()) {
              x = static_cast<int>(port_cfg);
              if (0 <= x && x <= 65535)
                svc_info.setPort(sidx++, x);
              else
                zret.push(Svc_Prop_Out_Of_Range(SVC_PROP_PORTS, port_cfg, x, 0, 65535));
            } else if (!malformed_error) { // only report this once.
              zret.push(Svc_Ports_Malformed(src_line));
              malformed_error = true;
            }
          }
          if (sidx) svc_info.enableFlags(ServiceGroup::PORTS_DEFINED);
          else zret.push(Svc_Ports_None_Valid(src_line));
        } else {
          zret.push(Prop_Invalid_Type(prop, libconfig::Setting::TypeArray));
        }
      }
    } else if (ServiceGroup::STANDARD != svc_info.getSvcType()) {
      zret.push(Svc_Ports_Not_Found(svc_line));
    }

    // Security option for this service group.
    if (svc_cfg.exists(SVC_PROP_SECURITY)) {
      ats::Rv<char const*> security = load_security(svc_cfg[SVC_PROP_SECURITY]);
      if (security.isOK()) {
        use_group_local_security = true;
        if (security.result()) {
          md5_key = security;
          security_style = SECURITY_MD5;
        } else {
          security_style = SECURITY_NONE;
        }
      }
      zret.join(security.errata());
    }

    // Get any group specific routers.
    routers.clear(); // reset list.
    if (svc_cfg.exists(SVC_PROP_ROUTERS)) {
      libconfig::Setting& rtr_cfg = svc_cfg[SVC_PROP_ROUTERS];
      ats::Errata status = load_routers(rtr_cfg, routers);
      if (!status)
        zret.push(ats::Errata::Message(23, LVL_INFO, "Router specification invalid.").set(status));
    }
    if (!routers.size() && !Seed_Router.size())
      zret.push(No_Valid_Routers(svc_line));

    // See if can proceed with service group creation.
    ats::Errata::Code code = code_max(zret);
    if (code >= LVL_WARN) {
      zret = Unable_To_Create_Service_Group(svc_line).set(zret);
      return zret;
    }

    // Properties after this are optional so we can proceed if they fail.
    GroupData& svc = this->defineServiceGroup(svc_info);
    // Add seed routers.
    std::vector<uint32>::iterator rspot, rlimit;
    for ( rspot = routers.begin(), rlimit = routers.end() ; rspot != rlimit ; ++rspot )
      svc.seedRouter(*rspot);
    for ( rspot = Seed_Router.begin(), rlimit = Seed_Router.end() ; rspot != rlimit ; ++rspot )
      svc.seedRouter(*rspot);

    if (use_group_local_security)
      svc.setSecurity(security_style).setKey(md5_key);

    // Look for optional properties.

    svc.m_packet_forward = ServiceGroup::GRE; // default
    if (svc_cfg.exists(SVC_PROP_FORWARD)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_FORWARD];
      ats::Errata status = load_option_set(prop, FORWARD_OPTS, N_FORWARD_OPTS);
      bool gre = FORWARD_OPTS[0].m_found;
      bool l2 = FORWARD_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_forward = 
          gre
            ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE
            : ServiceGroup::L2
          ;
        if (!status.isOK())
          zret.push(Ignored_Opt_Errors(SVC_PROP_FORWARD, prop.getSourceLine()).set(status));
      } else {
        zret.push(ats::Errata::Message(26, LVL_INFO, "Defaulting to GRE forwarding.").set(status));
      }
    }

    svc.m_packet_return = ServiceGroup::GRE; // default.
    if (svc_cfg.exists(SVC_PROP_RETURN)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_RETURN];
      ats::Errata status = load_option_set(prop, RETURN_OPTS, N_RETURN_OPTS);
      bool gre = RETURN_OPTS[0].m_found;
      bool l2 = RETURN_OPTS[1].m_found;
      if (gre || l2) {
        svc.m_packet_return =
          gre
            ? l2 ? ServiceGroup::GRE_OR_L2 : ServiceGroup::GRE
            : ServiceGroup::L2
          ;
        if (!status.isOK()) zret.push(Ignored_Opt_Errors(SVC_PROP_RETURN, prop.getSourceLine()).set(status));
      } else {
        zret.push(ats::Errata::Message(26, LVL_INFO, "Defaulting to GRE return.").set(status));
      }
    }

    svc.m_cache_assign = ServiceGroup::HASH_ONLY; // default
    if (svc_cfg.exists(SVC_PROP_ASSIGN)) {
      libconfig::Setting& prop = svc_cfg[SVC_PROP_ASSIGN];
      ats::Errata status = load_option_set(prop, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS));
      bool hash = ASSIGN_OPTS[0].m_found;
      bool mask = ASSIGN_OPTS[1].m_found;
      if (hash || mask) {
        svc.m_cache_assign =
          hash
            ? mask ? ServiceGroup::HASH_OR_MASK : ServiceGroup::HASH_ONLY
            : ServiceGroup::MASK_ONLY
          ;
        if (!status.isOK()) zret.push(Ignored_Opt_Errors(SVC_PROP_ASSIGN, prop.getSourceLine()).set(status));
      } else {
        status.push(ats::Errata::Message(26, LVL_INFO, "Defaulting to hash assignment only."));
        zret.push(List_Valid_Opts(SVC_PROP_ASSIGN, src_line, ASSIGN_OPTS, N_OPTS(ASSIGN_OPTS)).set(status));
      }
    }
  }
  return zret;
}

} // namespace.
