/** @file

  User agent control by static IP address.

  This enables specifying the set of methods usable by a user agent based on the remove IP address
  for a user agent connection.

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

#include "proxy/IPAllow.h"
#include "records/RecCore.h"
#include "swoc/Errata.h"
#include "swoc/TextView.h"
#include "tscore/Filenames.h"
#include "tscore/ink_memory.h"
#include "tsutil/ts_errata.h"

#include "swoc/Vectray.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_ex.h"
#include "swoc/bwf_ip.h"

#include "tsutil/YamlCfg.h"

using swoc::TextView;

using swoc::BufferWriter;
using swoc::bwf::Spec;

namespace
{
DbgCtl dbg_ctl_ip_allow("ip_allow");
}

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, Spec const & /* spec ATS_UNUSED */, IpAllow const *obj)
{
  return w.print("{}[{}]", obj->MODULE_NAME, obj->get_config_file().c_str());
}

} // namespace swoc

enum class AclOp {
  ALLOW, ///< Allow access.
  DENY,  ///< Deny access.
};

const IpAllow::Record IpAllow::ALLOW_ALL_RECORD(ALL_METHOD_MASK);
const IpAllow::ACL    IpAllow::DENY_ALL_ACL;

size_t IpAllow::configid       = 0;
bool   IpAllow::accept_check_p = true; // initializing global flag for fast deny

static ConfigUpdateHandler<IpAllow> *ipAllowUpdate;

//
//   Begin API functions
//
swoc::TextView
IpAllow::localize(swoc::TextView src)
{
  auto span = _arena.alloc(src.size() + 1).rebind<char>(); // always make a C-str if copying.
  memcpy(span.data(), src.data(), src.size());
  span[src.size()] = '\0';
  return span.remove_suffix(1); // don't put the extra terminating nul in the view.
}

void
IpAllow::startup()
{
  // Should not have been initialized before
  ink_assert(IpAllow::configid == 0);

  ipAllowUpdate = new ConfigUpdateHandler<IpAllow>();
  ipAllowUpdate->attach("proxy.config.cache.ip_allow.filename");
  ipAllowUpdate->attach("proxy.config.cache.ip_categories.filename");

  reconfigure();

  ConfigInfo *config = configProcessor.get(configid);
  if (config == nullptr) {
    configid = configProcessor.set(
      configid, new self_type("proxy.config.cache.ip_allow.filename", "proxy.config.cache.ip_categories.filename"));
    Warning("%s not loaded; All IP Addresses will be blocked.", ts::filename::IP_ALLOW);
  }
}

void
IpAllow::reconfigure()
{
  self_type *new_table;

  Note("%s loading ...", ts::filename::IP_ALLOW);

  new_table = new self_type("proxy.config.cache.ip_allow.filename", "proxy.config.cache.ip_categories.filename");
  // IP rules need categories, so load them first (if they exist).
  if (auto errata = new_table->BuildCategories(); !errata.is_ok()) {
    std::string text;
    swoc::bwprint(text, "{} failed to load\n{}", new_table->ip_categories_config_file, errata);
    Error("%s", text.c_str());
    delete new_table;
    return;
  }
  if (auto errata = new_table->BuildTable(); !errata.is_ok()) {
    std::string text;
    swoc::bwprint(text, "{} failed to load\n{}", ts::filename::IP_ALLOW, errata);
    if (errata.severity() <= ERRATA_ERROR) {
      Error("%s", text.c_str());
    } else {
      Fatal("%s", text.c_str());
    }
    delete new_table;
    return;
  }
  configid = configProcessor.set(configid, new_table);
  Note("%s finished loading", ts::filename::IP_ALLOW);
}

IpAllow *
IpAllow::acquire()
{
  return static_cast<IpAllow *>(configProcessor.get(configid));
}

void
IpAllow::release(IpAllow *config)
{
  configProcessor.release(configid, config);
}

void
IpAllow::release()
{
  configProcessor.release(configid, this);
}

bool
IpAllow::ip_category_contains_addr(std::string const &category, swoc::IPAddr const &addr)
{
  self_type *self = acquire();
  auto const spot = self->ip_category_map.find(category);
  if (spot == self->ip_category_map.end()) {
    return false;
  }
  auto const &space = spot->second;
  bool const  found = space.find(addr) != space.end();
  self->release();
  return found;
}

IpAllow::ACL
IpAllow::match(swoc::IPAddr const &addr, match_key_t key)
{
  self_type    *self   = acquire();
  Record const *record = nullptr;
  if (match_key_t::SRC_ADDR == key) {
    if (auto spot = self->_src_map.find(addr); spot != self->_src_map.end()) {
      auto r = std::get<1>(*spot);
      // Special case - if checking in accept is enabled and the record is a deny all,
      // then return a missing record instead to force an immediate deny. Otherwise it's delayed
      // until after remap, to allow remap rules to tweak the result.
      if (!(accept_check_p && r->_method_mask == 0 && r->_nonstandard_methods.empty())) {
        record = r;
      }
    }
  } else if (auto spot = self->_dst_map.find(addr); spot != self->_dst_map.end()) {
    record = std::get<1>(*spot);
  }

  if (record == nullptr) {
    self->release(); // no record, don't keep a reference to the config.
    return {};
  }

  return ACL{record, self}; // Note this keeps the config in memory.
}

//
//   End API functions
//

IpAllow::IpAllow(const char *ip_allow_config_var, const char *ip_categories_config_var)
  : ip_allow_config_file(ats_scoped_str(RecConfigReadConfigPath(ip_allow_config_var)).get())
{
  int matching_policy = 0;
  matching_policy     = RecGetRecordInt("proxy.config.url_remap.acl_behavior_policy").value_or(0);
  if (matching_policy == 0) {
    this->_is_legacy_action_policy = true;
  } else {
    this->_is_legacy_action_policy = false;
  }
  std::string const path = RecConfigReadConfigPath(ip_categories_config_var);
  if (!path.empty()) {
    ip_categories_config_file = ats_scoped_str(path).get();
  }
}

BufferWriter &
bwformat(BufferWriter &w, Spec const & /* spec ATS_UNUSED */, IpAllow::IpMap const &map)
{
  w.print("{} entries", map.count());
  for (auto const &spot : map) {
    auto const *r = std::get<1>(spot);
    w.print("\n  Line {}: {} methods=", r->_src_line, std::get<0>(spot));
    uint32_t mask = IpAllow::ALL_METHOD_MASK & r->_method_mask;
    if (IpAllow::ALL_METHOD_MASK == mask) {
      w.write("ALL");
    } else if (0 == mask) {
      w.write("NONE");
    } else {
      bool     leader    = false; // need leading vbar?
      uint32_t test_mask = 1;     // mask for current method.
      for (int i = 0; i < HTTP_WKSIDX_METHODS_CNT; ++i, test_mask <<= 1) {
        if (mask & test_mask) {
          w.print("{}{}", swoc::bwf::If(leader, "|"), hdrtoken_index_to_wks(i + HTTP_WKSIDX_CONNECT));
          leader = true;
        }
      }
    }

    if (!r->_nonstandard_methods.empty()) {
      w.print(" {}=", r->_deny_nonstandard_methods ? IpAllow::YAML_VALUE_ACTION_ALLOW : IpAllow::YAML_VALUE_ACTION_DENY);
      bool leader = false; // need leading vbar?
      for (auto const &name : r->_nonstandard_methods) {
        w.print("{}{}", swoc::bwf::If(leader, "|"), name);
        leader = true;
      }
    }
  }
  return w;
}

void
IpAllow::DebugMap(const IpMap &map) const
{
  std::string out;
  out.resize(8192);
  swoc::bwprint(out, "{}", map);
  Dbg(dbg_ctl_ip_allow, "%s", out.c_str());
}

void
IpAllow::Print() const
{
  Dbg(dbg_ctl_ip_allow, "Printing src map");
  DebugMap(_src_map);
  Dbg(dbg_ctl_ip_allow, "Printing dest map");
  DebugMap(_dst_map);
}

swoc::Errata
IpAllow::BuildTable()
{
  // Table should be empty
  ink_assert(_src_map.count() == 0 && _dst_map.count() == 0);

  std::error_code ec;
  std::string     content{swoc::file::load(ip_allow_config_file, ec)};
  swoc::Errata    errata;
  if (ec.value() == 0) {
    try {
      errata = this->YAMLBuildTable(content);
    } catch (std::exception &ex) {
      return swoc::Errata(ec, ERRATA_ERROR, "{} - Invalid config: {}", this, ex.what());
    }
    if (!errata.is_ok()) {
      errata.note("While parsing config file");
      return errata;
    }

    if (_src_map.count() == 0 && _dst_map.count() == 0) {
      return swoc::Errata(ERRATA_ERROR, "{} - No entries found. All IP Addresses will be blocked", this);
    }

    if (dbg_ctl_ip_allow.on()) {
      Print();
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} Failed to load {}. All IP Addresses will be blocked", this, ec);
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadMethod(const YAML::Node &node, Record &rec)
{
  swoc::TextView                   value{node.Scalar()};
  swoc::Vectray<swoc::TextView, 8> names;
  // Process a single token. Required to deal with the variable number of tokens.
  auto parse_method = [&](swoc::TextView value) -> void {
    if (0 == strcasecmp(value, YAML_VALUE_METHODS_ALL)) {
      rec._method_mask = ALL_METHOD_MASK;
    } else {
      int method_idx = hdrtoken_tokenize(value.data(), value.size());
      if (HTTP_WKSIDX_CONNECT <= method_idx && method_idx < HTTP_WKSIDX_CONNECT + HTTP_WKSIDX_METHODS_CNT) {
        rec._method_mask |= ACL::MethodIdxToMask(method_idx);
      } else {
        names.push_back(value);
        Dbg(dbg_ctl_ip_allow, "Found nonstandard method '%.*s' at line %d", int(value.size()), value.data(), node.Mark().line);
      }
    }
  };

  if (node.IsScalar()) {
    parse_method(swoc::TextView(node.Scalar()));
  } else if (node.IsSequence()) {
    for (auto const &elt : node) {
      if (elt.IsScalar()) {
        parse_method(swoc::TextView(elt.Scalar()));
        if (rec._method_mask == ALL_METHOD_MASK) {
          break; // we're done here, nothing else matters.
        }
      } else {
        return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, all values for '{}' must be strings.", this, elt.Mark(),
                            YAML_TAG_METHODS);
      }
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, value for '{}' must be a single string or a list of strings.", this,
                        node.Mark(), YAML_TAG_METHODS);
  }

  // copy over to local memory if it's not all methods and there are non-standard ones.
  if (rec._method_mask != ALL_METHOD_MASK && !names.empty()) {
    rec._nonstandard_methods = _arena.alloc_span<swoc::TextView>(names.size());
    for (unsigned idx = 0; idx < names.size(); ++idx) {
      rec._nonstandard_methods[idx] = this->localize(names[idx]);
    }
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadIPAddrRange(const YAML::Node &node, IpMap *map, IpAllow::Record const *record)
{
  if (!node.IsScalar()) {
    return swoc::Errata(ERRATA_ERROR, "{} Expected IP address range at {}, found non-literal.", this, node.Mark());
  }

  swoc::TextView ip_range(node.Scalar());
  if (swoc::IPRange r; r.load(ip_range)) {
    map->fill(r, record);
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - '{}' is not a valid range.", this, node.Mark(), node.Scalar());
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadIPCategory(const YAML::Node &node, IpMap *map, IpAllow::Record const *record)
{
  if (!node.IsScalar()) {
    return swoc::Errata(ERRATA_ERROR, "{} Expected IP address category at {}, found non-literal.", this, node.Mark());
  }
  std::string const &category(node.Scalar());
  if (auto spot = ip_category_map.find(category); spot != ip_category_map.end()) {
    for (auto const &range : spot->second) {
      map->fill(range.range_view(), record);
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - '{}' is not category with a defined range.", this, node.Mark(), category);
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadEntry(const YAML::Node &entry)
{
  AclOp      op = AclOp::DENY; // "shut up", I explained to the compiler.
  YAML::Node node;
  auto       record = _arena.make<Record>();
  IpMap     *map    = nullptr; // src or dst map.

  if (!entry.IsMap()) {
    return swoc::Errata(ERRATA_ERROR, "{} {} - ACL items must be maps.", this, entry.Mark());
  }

  if (YAML::Node apply_node{entry[YAML_TAG_APPLY]}; apply_node) {
    if (apply_node.IsScalar()) {
      swoc::TextView value{apply_node.Scalar()};
      if (0 == strcasecmp(value, YAML_VALUE_APPLY_IN)) {
        map = &_src_map;
      } else if (0 == strcasecmp(value, YAML_VALUE_APPLY_OUT)) {
        map = &_dst_map;
      } else {
        return swoc::Errata(ERRATA_ERROR, R"("{}" value at {} must be "{}" or "{}")", YAML_TAG_APPLY, entry.Mark(),
                            YAML_VALUE_APPLY_IN, YAML_VALUE_APPLY_OUT);
      }
    } else {
      return swoc::Errata(ERRATA_ERROR, R"("{}" value at {} must be a scalar, "{}" or "{}")", YAML_TAG_APPLY, entry.Mark(),
                          YAML_VALUE_APPLY_IN, YAML_VALUE_APPLY_OUT);
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, R"(Object at {} must have a "{}" key.)", entry.Mark(), YAML_TAG_APPLY);
  }

  if (node = entry[YAML_TAG_ACTION]; node) {
    if (node.IsScalar()) {
      swoc::TextView value(node.Scalar());
      if (!this->_is_legacy_action_policy &&
          (value == YAML_VALUE_ACTION_ALLOW_OLD_NAME || value == YAML_VALUE_ACTION_DENY_OLD_NAME)) {
        return swoc::Errata(
          ERRATA_FATAL, R"(Legacy action name of "{}" detected at {}. Use "set_allow" or "set_deny" instead of "allow" or "deny".)",
          value, entry.Mark());
      }
      if (value == YAML_VALUE_ACTION_ALLOW || value == YAML_VALUE_ACTION_ALLOW_OLD_NAME) {
        op = AclOp::ALLOW;
      } else if (value == YAML_VALUE_ACTION_DENY || value == YAML_VALUE_ACTION_DENY_OLD_NAME) {
        op = AclOp::DENY;
      } else {
        return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, value for tag '{}' must be '{}' or '{}'", this, node.Mark(),
                            YAML_TAG_ACTION, YAML_VALUE_ACTION_ALLOW, YAML_VALUE_ACTION_DENY);
      }
    } else {
      return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, value for tag '{}' must be a string", this, node.Mark(),
                          YAML_TAG_ACTION);
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, required '{}' key not found.", this, entry.Mark(), YAML_TAG_ACTION);
  }

  if (entry[YAML_TAG_IP_ADDRS] && entry[YAML_TAG_IP_CATEGORIES]) {
    return swoc::Errata(ERRATA_ERROR, "{} {} - '{}' and '{}' cannot both be used in the same rule.", this, entry.Mark(),
                        YAML_TAG_IP_ADDRS, YAML_TAG_IP_CATEGORIES);
  }
  if (YAML::Node addr_node = entry[YAML_TAG_IP_ADDRS]; addr_node) {
    bool marked_p = false;
    if (addr_node.IsSequence()) {
      for (auto const &n : addr_node) {
        if (auto errata = this->YAMLLoadIPAddrRange(n, map, record); errata.is_ok()) {
          marked_p = true;
        } else {
          errata.note(R"(In record at {})", entry.Mark());
          return errata;
        }
      }
    } else {
      if (auto errata = this->YAMLLoadIPAddrRange(addr_node, map, record); errata.is_ok()) {
        marked_p = true;
      } else {
        errata.note(R"(In record at {})", entry.Mark());
        return errata;
      }
    }
    if (!marked_p) {
      return swoc::Errata(ERRATA_ERROR, "No valid addresses for rule at {}", node.Mark());
    }
  } else if (YAML::Node category_node = entry[YAML_TAG_IP_CATEGORIES]; category_node) {
    bool marked_p = false;
    if (category_node.IsSequence()) {
      for (auto const &n : category_node) {
        if (auto errata = this->YAMLLoadIPCategory(n, map, record); errata.is_ok()) {
          marked_p = true;
        } else {
          errata.note(R"(In record at {})", entry.Mark());
          return errata;
        }
      }
    } else {
      if (auto errata = this->YAMLLoadIPCategory(category_node, map, record); errata.is_ok()) {
        marked_p = true;
      } else {
        errata.note(R"(In record at {})", entry.Mark());
        return errata;
      }
    }
    if (!marked_p) {
      return swoc::Errata(ERRATA_ERROR, "No valid IP category for rule at {}", node.Mark());
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - item ignored, required '{}' or '{}' key not found.", this, entry.Mark(),
                        YAML_TAG_IP_ADDRS, YAML_TAG_IP_CATEGORIES);
  }

  if (auto methodNode = entry[YAML_TAG_METHODS]) {
    // methods are specified.
    if (auto errata = this->YAMLLoadMethod(methodNode, *record); !errata.is_ok()) {
      return errata;
    }
  } else {
    record->_method_mask = ALL_METHOD_MASK;
  }

  if (op == AclOp::DENY) {
    record->_method_mask              = ALL_METHOD_MASK & ~record->_method_mask;
    record->_deny_nonstandard_methods = true;
  }

  record->_src_line = entry.Mark().line;
  return {};
}

swoc::Errata
IpAllow::YAMLBuildTable(std::string const &content)
{
  YAML::Node root{YAML::Load(content)};
  if (!root.IsMap()) {
    return swoc::Errata("{} - top level object was not a map. All IP Addresses will be blocked", this);
  }

  // IP categories are optional. Load them if specified. Note that the rules,
  // if they use categories, depend upon the categories being defined. So the
  // categories have to be processed first before the rules are.
  YAML::Node categories{root[YAML_TAG_CATEGORY_ROOT.data()]};
  if (auto errata = this->YAMLLoadCategoryRoot(categories); !errata.is_ok()) {
    return errata;
  }

  // Now load the IPAllow rules.
  YAML::Node rules{root[YAML_TAG_ROOT.data()]};
  if (!rules) {
    return swoc::Errata("{} - root tag '{}' not found. All IP Addresses will be blocked", this, YAML_TAG_ROOT);
  } else if (rules.IsSequence()) {
    for (auto const &entry : rules) {
      if (auto errata = this->YAMLLoadEntry(entry); !errata.is_ok()) {
        return errata;
      }
    }
  } else if (rules.IsMap()) {
    return this->YAMLLoadEntry(rules); // singleton, just load it.
  } else {
    return swoc::Errata("{} - root tag '{}' is not an map or sequence. All IP Addresses will be blocked", this, YAML_TAG_ROOT);
  }
  return {};
}

swoc::Errata
IpAllow::BuildCategories()
{
  std::error_code ec;
  if (ip_categories_config_file.empty()) {
    return {};
  }

  Note("%s loading categores file %s ...", ts::filename::IP_ALLOW, ip_categories_config_file.c_str());
  std::string  content{swoc::file::load(ip_categories_config_file, ec)};
  swoc::Errata errata;
  if (ec.value() == 0) {
    try {
      errata = this->YAMLBuildCategories(content);
    } catch (std::exception &ex) {
      return swoc::Errata(ec, ERRATA_ERROR, "{} - Invalid IP Categories {} content: {}", this, ip_categories_config_file,
                          ex.what());
    }
    if (!errata.is_ok()) {
      errata.note("While parsing ip categories file: {}", ip_categories_config_file);
      return errata;
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} Failed to load {}", this, ec);
  }
  Note("%s done loading categores file %s ...", ts::filename::IP_ALLOW, ip_categories_config_file.c_str());
  return {};
}

swoc::Errata
IpAllow::YAMLBuildCategories(std::string const &content)
{
  YAML::Node root{YAML::Load(content)};
  YAML::Node categories{root[YAML_TAG_CATEGORY_ROOT.data()]};
  if (auto errata = this->YAMLLoadCategoryRoot(categories); !errata.is_ok()) {
    return errata;
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadCategoryRoot(const YAML::Node &categories)
{
  if (categories) {
    if (!categories.IsSequence()) {
      return swoc::Errata("{} - '{}' tag must be a sequence of maps. All IP Addresses will be blocked", this,
                          YAML_TAG_CATEGORY_ROOT);
    }
    for (auto const &category : categories) {
      if (!category.IsMap()) {
        return swoc::Errata("{} - '{}' tag must be a sequence of maps. All IP Addresses will be blocked", this,
                            YAML_TAG_CATEGORY_ROOT);
      }
      if (auto errata = this->YAMLLoadCategoryDefinition(category); !errata.is_ok()) {
        return errata;
      }
    }
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadCategoryDefinition(const YAML::Node &entry)
{
  /* Parse this into ip_category_map:
   *
   *   - name: <category name>
   *     ip_addrs:
   *       - <ip range>
   *       - <ip range>
   *       - <ip range>
   */
  if (!entry.IsMap()) {
    return swoc::Errata(ERRATA_ERROR, "{} {} - Category definition must be a map.", this, entry.Mark());
  }

  if (auto name_node = entry[YAML_TAG_CATEGORY_NAME]; name_node) {
    if (!name_node.IsScalar()) {
      return swoc::Errata(ERRATA_ERROR, "{} {} - Category name must be a string.", this, name_node.Mark());
    }
    std::string const &name(name_node.Scalar());
    if (auto ip_addrs_node = entry[YAML_TAG_CATEGORY_IP_ADDRS]; ip_addrs_node) {
      if (ip_addrs_node.IsSequence()) {
        for (auto const &ip_addr_node : ip_addrs_node) {
          if (auto errata = this->YAMLLoadCategoryIpRange(ip_addr_node, ip_category_map[name]); !errata.is_ok()) {
            errata.note(R"(In category definition at {})", entry.Mark());
            return errata;
          }
        }
      } else {
        if (auto errata = this->YAMLLoadCategoryIpRange(ip_addrs_node, ip_category_map[name]); !errata.is_ok()) {
          errata.note(R"(In category definition at {})", entry.Mark());
          return errata;
        }
      }
    } else { // No ip_addrs.
      return swoc::Errata(ERRATA_ERROR, "{} {} - IP Addresses must be specified.", this, entry.Mark());
    }
  } else { // No name
    return swoc::Errata(ERRATA_ERROR, "{} {} - Category name must be specified.", this, entry.Mark());
  }
  return {};
}

swoc::Errata
IpAllow::YAMLLoadCategoryIpRange(const YAML::Node &node, swoc::IPSpace<bool> &space)
{
  if (!node.IsScalar()) {
    return swoc::Errata(ERRATA_ERROR, "{} Expected IP address range for category at {}, found non-literal.", this, node.Mark());
  }

  swoc::TextView ip_range(node.Scalar());
  if (swoc::IPRange r; r.load(ip_range)) {
    space.fill(r, true);
  } else {
    return swoc::Errata(ERRATA_ERROR, "{} {} - '{}' is not a valid range.", this, node.Mark(), node.Scalar());
  }

  return {};
}
