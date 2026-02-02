/** @file
 *
 *  YAML remap configuration file parsing implementation.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "proxy/http/remap/RemapYamlConfig.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <filesystem>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "tscore/Diags.h"
#include "tscore/ink_string.h"
#include "tsutil/ts_errata.h"
#include "tsutil/PostScript.h"
#include "swoc/bwf_base.h"
#include "swoc/bwf_ex.h"
#include "swoc/swoc_file.h"

#include "proxy/http/remap/UrlRewrite.h"
#include "proxy/http/remap/UrlMapping.h"
#include "proxy/http/remap/RemapConfig.h"
#include "proxy/http/remap/AclFiltering.h"
#include "records/RecCore.h"

namespace
{
DbgCtl dbg_ctl_remap_yaml{"remap_yaml"};
DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};

/** will process the regex mapping configuration and create objects in
    output argument reg_map. It assumes existing data in reg_map is
    inconsequential and will be perfunctorily null-ed;
*/
static bool
process_regex_mapping_config(const char *from_host_lower, url_mapping *new_mapping, UrlRewrite::RegexMapping *reg_map)
{
  std::string_view to_host{};
  int              to_host_len;
  int              substitution_id;
  int32_t          captures;

  reg_map->to_url_host_template     = nullptr;
  reg_map->to_url_host_template_len = 0;
  reg_map->n_substitutions          = 0;

  reg_map->url_map = new_mapping;

  // using from_host_lower (and not new_mapping->fromURL.host_get())
  // as this one will be nullptr-terminated (required by pcre_compile)
  if (reg_map->regular_expression.compile(from_host_lower) == false) {
    Warning("pcre_compile failed! Regex has error starting at %s", from_host_lower);
    goto lFail;
  }

  captures = reg_map->regular_expression.get_capture_count();
  if (captures == -1) {
    Warning("pcre_fullinfo failed!");
    goto lFail;
  }
  if (captures >= UrlRewrite::MAX_REGEX_SUBS) { // off by one for $0 (implicit capture)
    Warning("regex has %d capturing subpatterns (including entire regex); Max allowed: %d", captures + 1,
            UrlRewrite::MAX_REGEX_SUBS);
    goto lFail;
  }

  to_host     = new_mapping->toURL.host_get();
  to_host_len = static_cast<int>(to_host.length());
  for (int i = 0; i < to_host_len - 1; ++i) {
    if (to_host[i] == '$') {
      substitution_id = to_host[i + 1] - '0';
      if ((substitution_id < 0) || (substitution_id > captures)) {
        Warning("Substitution id [%c] has no corresponding capture pattern in regex [%s]", to_host[i + 1], from_host_lower);
        goto lFail;
      }
      reg_map->substitution_markers[reg_map->n_substitutions] = i;
      reg_map->substitution_ids[reg_map->n_substitutions]     = substitution_id;
      ++reg_map->n_substitutions;
    }
  }

  // so the regex itself is stored in fromURL.host; string to match
  // will be in the request; string to use for substitutions will be
  // in this buffer
  reg_map->to_url_host_template_len = to_host_len;
  reg_map->to_url_host_template     = static_cast<char *>(ats_malloc(to_host_len));
  memcpy(reg_map->to_url_host_template, to_host.data(), to_host_len);

  return true;

lFail:
  ats_free(reg_map->to_url_host_template);
  reg_map->to_url_host_template     = nullptr;
  reg_map->to_url_host_template_len = 0;

  return false;
}
} // end anonymous namespace

swoc::Errata
parse_yaml_url(const YAML::Node &node, URL &url, bool host_check, std::string_view &url_str)
{
  if (!node || !node.IsMap()) {
    return swoc::Errata("URL must be a map");
  }
  url.create(nullptr);

  // Use url first if defined
  ParseResult rparse;
  if (node["url"]) {
    url_str = node["url"].as<std::string_view>();
    if (host_check) {
      rparse = url.parse_regex(url_str);
    } else {
      rparse = url.parse_no_host_check(url_str);
    }
    if (rparse != ParseResult::DONE) {
      return swoc::Errata("malformed URL: {}", url_str);
    }

    return {};
  }

  // Build URL string from components
  if (node["scheme"]) {
    url.scheme_set(node["scheme"].as<std::string>());
  }

  if (node["host"]) {
    url.host_set(node["host"].as<std::string>());
  }

  if (node["port"]) {
    url.port_set(node["port"].as<int>());
  }

  if (node["path"]) {
    url.path_set(node["path"].as<std::string>());
  }

  return {};
}

swoc::Errata
remap_validate_yaml_filter_args(acl_filter_rule **rule_pp, const YAML::Node &node, ACLBehaviorPolicy behavior_policy)
{
  acl_filter_rule *rule;
  int              j;
  bool             new_rule_flg = false;

  if (!rule_pp) {
    Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Invalid argument(s)");
    return swoc::Errata("Invalid argument(s)");
  }

  if (dbg_ctl_url_rewrite.on()) {
    printf("validate_filter_args: ");
    for (const auto &rule : node) {
      printf("\"%s\" ", rule.first.as<std::string>().c_str());
    }
    printf("\n");
  }

  ts::PostScript free_rule([&]() -> void {
    if (new_rule_flg) {
      delete rule;
      *rule_pp = nullptr;
    }
  });

  if ((rule = *rule_pp) == nullptr) {
    rule = new acl_filter_rule();
    if (unlikely((*rule_pp = rule) == nullptr)) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Memory allocation error");
      return swoc::Errata("Memory allocation Error");
    }
    new_rule_flg = true;
    Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] new acl_filter_rule class was created during remap rule processing");
  }

  if (!node || !node.IsMap()) {
    return swoc::Errata("filters must be a map");
  }

  // Parse method
  auto parse_method = [&](const std::string &method_str) {
    int m = hdrtoken_tokenize(method_str.c_str(), method_str.length(), nullptr) - HTTP_WKSIDX_CONNECT;

    if (m >= 0 && m < HTTP_WKSIDX_METHODS_CNT) {
      rule->standard_method_lookup[m] = true;
    } else {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Using nonstandard method [%s]", method_str.c_str());
      rule->nonstandard_methods.insert(method_str);
    }
    rule->method_restriction_enabled = true;
  };

  if (node["method"]) {
    if (node["method"].IsSequence()) {
      for (const auto &method : node["method"]) {
        parse_method(method.as<std::string>());
      }
    } else {
      parse_method(node["method"].as<std::string>());
    }
  }

  // Parse src_ip (and src_ip_invert)
  auto parse_src_ip = [&](const std::string &ip_str, bool invert) -> swoc::Errata {
    if (rule->src_ip_cnt >= ACL_FILTER_MAX_SRC_IP) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"src_ip=\" filters");
      return swoc::Errata("Defined more than {} src_ip filters", ACL_FILTER_MAX_SRC_IP);
    }

    src_ip_info_t *ipi = &rule->src_ip_array[rule->src_ip_cnt];
    if (invert) {
      ipi->invert = true;
    }
    std::string_view arg{ip_str};
    if (arg == "all") {
      ipi->match_all_addresses = true;
    } else if (ats_ip_range_parse(arg, ipi->start, ipi->end) != 0) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unable to parse IP value in %s", ip_str.c_str());
      return swoc::Errata("Unable to parse IP value: {}", ip_str);
    }
    for (j = 0; j < rule->src_ip_cnt; j++) {
      if (rule->src_ip_array[j].start == ipi->start && rule->src_ip_array[j].end == ipi->end) {
        ipi->reset();
        return {};
      }
    }
    if (ipi) {
      rule->src_ip_cnt++;
      rule->src_ip_valid = 1;
    }
    return {};
  };

  if (node["src_ip"]) {
    if (node["src_ip"].IsSequence()) {
      for (const auto &src_ip : node["src_ip"]) {
        auto errata = parse_src_ip(src_ip.as<std::string>(), false);
        if (!errata.is_ok()) {
          return errata;
        }
      }
    } else {
      auto errata = parse_src_ip(node["src_ip"].as<std::string>(), false);
      if (!errata.is_ok()) {
        return errata;
      }
    }
  }

  if (node["src_ip_invert"]) {
    if (node["src_ip_invert"].IsSequence()) {
      for (const auto &src_ip_invert : node["src_ip_invert"]) {
        auto errata = parse_src_ip(src_ip_invert.as<std::string>(), true);
        if (!errata.is_ok()) {
          return errata;
        }
      }
    } else {
      auto errata = parse_src_ip(node["src_ip_invert"].as<std::string>(), true);
      if (!errata.is_ok()) {
        return errata;
      }
    }
  }

  // Parse src_ip_category (and src_ip_category_invert)
  auto parse_src_ip_category = [&](const std::string &ip_category, bool invert) -> swoc::Errata {
    if (rule->src_ip_category_cnt >= ACL_FILTER_MAX_SRC_IP) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"src_ip_category=\" filters");
      return swoc::Errata("Defined more than {} src_ip_category filters", ACL_FILTER_MAX_SRC_IP);
    }
    src_ip_category_info_t *ipi = &rule->src_ip_category_array[rule->src_ip_category_cnt];
    ipi->category.assign(ip_category);
    if (invert) {
      ipi->invert = true;
    }
    for (j = 0; j < rule->src_ip_category_cnt; j++) {
      if (rule->src_ip_category_array[j].category == ipi->category) {
        ipi->reset();
        return {};
      }
    }
    if (ipi) {
      rule->src_ip_category_cnt++;
      rule->src_ip_category_valid = 1;
    }
    return {};
  };

  if (node["src_ip_category"]) {
    auto errata = parse_src_ip_category(node["src_ip_category"].as<std::string>(), false);
    if (!errata.is_ok()) {
      return errata;
    }
  }

  if (node["src_ip_category_invert"]) {
    auto errata = parse_src_ip_category(node["src_ip_category_invert"].as<std::string>(), true);
    if (!errata.is_ok()) {
      return errata;
    }
  }

  // Parse in_ip (and in_ip_invert)
  auto parse_in_ip = [&](const std::string &in_ip, bool invert) -> swoc::Errata {
    if (rule->in_ip_cnt >= ACL_FILTER_MAX_IN_IP) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"in_ip=\" filters");
      return swoc::Errata("Defined more than {} in_ip filters", ACL_FILTER_MAX_IN_IP);
    }
    src_ip_info_t *ipi = &rule->in_ip_array[rule->in_ip_cnt];
    if (invert) {
      ipi->invert = true;
    }
    // important! use copy of argument
    std::string_view arg{in_ip};
    if (arg == "all") {
      ipi->match_all_addresses = true;
    } else if (ats_ip_range_parse(arg, ipi->start, ipi->end) != 0) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unable to parse IP value in %s", in_ip.c_str());
      return swoc::Errata("Unable to parse IP value: {}", in_ip);
    }
    for (j = 0; j < rule->in_ip_cnt; j++) {
      if (rule->in_ip_array[j].start == ipi->start && rule->in_ip_array[j].end == ipi->end) {
        ipi->reset();
        return {};
      }
    }
    if (ipi) {
      rule->in_ip_cnt++;
      rule->in_ip_valid = 1;
    }
    return {};
  };

  if (node["in_ip"]) {
    if (node["in_ip"].IsSequence()) {
      for (const auto &in_ip : node["in_ip"]) {
        auto errata = parse_in_ip(in_ip.as<std::string>(), false);
        if (!errata.is_ok()) {
          return errata;
        }
      }
    } else {
      auto errata = parse_in_ip(node["in_ip"].as<std::string>(), false);
      if (!errata.is_ok()) {
        return errata;
      }
    }
  }

  if (node["in_ip_invert"]) {
    if (node["in_ip_invert"].IsSequence()) {
      for (const auto &in_ip_invert : node["in_ip_invert"]) {
        auto errata = parse_in_ip(in_ip_invert.as<std::string>(), true);
        if (!errata.is_ok()) {
          return errata;
        }
      }
    } else {
      auto errata = parse_in_ip(node["in_ip_invert"].as<std::string>(), true);
      if (!errata.is_ok()) {
        return errata;
      }
    }
  }

  // Parse action
  if (node["action"]) {
    if (node["action"].IsSequence()) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Only one action is allowed per remap ACL");
      return swoc::Errata("Only one action is allowed per remap ACL");
    }
    std::string action_str = node["action"].as<std::string>();
    if (behavior_policy == ACLBehaviorPolicy::ACL_BEHAVIOR_MODERN) {
      // With the new matching policy, we don't allow the legacy "allow" and
      // "deny" actions. Users must transition to either add_allow/add_deny or
      // set_allow/set_deny.
      if (is_inkeylist(action_str.c_str(), "allow", "deny", nullptr)) {
        Dbg(dbg_ctl_url_rewrite,
            R"([validate_filter_args] "allow" and "deny" are no longer valid. Use add_allow/add_deny or set_allow/set_deny: "%s"")",
            action_str.c_str());
        return swoc::Errata("\"allow\" and \"deny\" are no longer valid. Use add_allow/add_deny or set_allow/set_deny: {}",
                            action_str.c_str());
      }
    }
    if (is_inkeylist(action_str.c_str(), "add_allow", "add_deny", nullptr)) {
      rule->add_flag = 1;
    } else {
      rule->add_flag = 0;
    }
    // Remove "deny" from this list when MATCH_ON_IP_AND_METHOD is removed in 11.x.
    if (is_inkeylist(action_str.c_str(), "0", "off", "deny", "set_deny", "add_deny", "disable", nullptr)) {
      rule->allow_flag = 0;
      // Remove "allow" from this list when MATCH_ON_IP_AND_METHOD is removed in 11.x.
    } else if (is_inkeylist(action_str.c_str(), "1", "on", "allow", "set_allow", "add_allow", "enable", nullptr)) {
      rule->allow_flag = 1;
    } else {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unknown argument \"%s\"", action_str.c_str());
      return swoc::Errata("Unknown action: {}", action_str);
    }
  }

  // Parse internal
  if (node["internal"] && node["internal"].as<bool>()) {
    rule->internal = 1;
  }

  if (dbg_ctl_url_rewrite.on()) {
    rule->print();
  }

  free_rule.release();
  return {};
}

swoc::Errata
parse_map_referer(const YAML::Node &node, url_mapping *url_mapping)
{
  if (!node || !node.IsMap()) {
    return swoc::Errata("redirect must be a map");
  }

  if (!node["url"]) {
    return swoc::Errata("Missing 'url' field in redirect map-with-referer");
  }
  std::string url                  = node["url"].as<std::string>();
  url_mapping->filter_redirect_url = ats_strdup(url.c_str());
  if (!strcasecmp(url.c_str(), "<default>") || !strcasecmp(url.c_str(), "default") ||
      !strcasecmp(url.c_str(), "<default_redirect_url>") || !strcasecmp(url.c_str(), "default_redirect_url")) {
    url_mapping->default_redirect_url = true;
  }
  url_mapping->redir_chunk_list = redirect_tag_str::parse_format_redirect_url(ats_strdup(url.c_str()));

  if (!node["regex"] || !node["regex"].IsSequence()) {
    return swoc::Errata("'regex' field must be sequence");
  }

  referer_info *ri;
  for (const auto &rule : node["regex"]) {
    char        refinfo_error_buf[1024];
    bool        refinfo_error = false;
    std::string regex         = rule.as<std::string>();

    ri = new referer_info(regex.c_str(), &refinfo_error, refinfo_error_buf, sizeof(refinfo_error_buf));
    if (refinfo_error) {
      delete ri;
      ri = nullptr;
      return swoc::Errata("Incorrect Referer regular expression \"{}\" - {}", regex.c_str(), refinfo_error_buf);
    }

    if (ri && ri->negative) {
      if (ri->any) {
        url_mapping->optional_referer = true; /* referer header is optional */
        delete ri;
        ri = nullptr;
      } else {
        url_mapping->negative_referer = true; /* we have negative referer in list */
      }
    }
    if (ri) {
      ri->next                  = url_mapping->referer_list;
      url_mapping->referer_list = ri;
    }
  }
  return {};
}

swoc::Errata
parse_yaml_plugins(const YAML::Node &node, url_mapping *url_mapping, BUILD_TABLE_INFO *bti)
{
  char *err;
  char *pargv[1024];
  int   parc = 0;
  memset(pargv, 0, sizeof(pargv));

  if (!node["name"]) {
    return swoc::Errata("plugin missing 'name' field");
  }

  std::string plugin_name = node["name"].as<std::string>();
  Dbg(dbg_ctl_remap_yaml, "Loading plugin: %s", plugin_name.c_str());

  /* Prepare remap plugin parameters from the config */
  if ((err = url_mapping->fromURL.string_get(nullptr)) == nullptr) {
    return swoc::Errata("Can't load fromURL from URL class");
  }
  pargv[parc++] = ats_strdup(err);
  ats_free(err);

  if ((err = url_mapping->toURL.string_get(nullptr)) == nullptr) {
    return swoc::Errata("Can't load toURL from URL class");
  }
  pargv[parc++] = ats_strdup(err);
  ats_free(err);

  // Add plugin parameters
  if (node["params"] && node["params"].IsSequence()) {
    auto &param_storage = url_mapping->getPluginParamsStorage();
    for (const auto &param : node["params"]) {
      param_storage.push_back(param.as<std::string>());
    }
    size_t param_idx = param_storage.size() - node["params"].size();
    for (size_t i = 0; i < node["params"].size() && parc < static_cast<int>(countof(pargv) - 1); ++i, ++param_idx) {
      pargv[parc++] = const_cast<char *>(param_storage[param_idx].c_str());
      Dbg(dbg_ctl_remap_yaml, "  Plugin param: %s", param_storage[param_idx].c_str());
    }
  }

  RemapPluginInst *pi = nullptr;
  std::string      error;
  {
    uint32_t elevate_access = 0;
    elevate_access          = RecGetRecordInt("proxy.config.plugin.load_elevated").value_or(0);
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    pi =
      bti->rewrite->pluginFactory.getRemapPlugin(swoc::file::path(plugin_name), parc, pargv, error, isPluginDynamicReloadEnabled());
  } // done elevating access

  if (nullptr == pi) {
    ats_free(pargv[0]);
    ats_free(pargv[1]);
    return swoc::Errata("failed to instantiate plugin ({}) to remap rule: {}", plugin_name.c_str(), error.c_str());
  } else {
    url_mapping->add_plugin_instance(pi);
  }

  ats_free(pargv[0]); // fromURL
  ats_free(pargv[1]); // toURL

  return {};
}

swoc::Errata
parse_yaml_filter_directive(const YAML::Node &node, BUILD_TABLE_INFO *bti)
{
  acl_filter_rule *rp;

  // Check for activate_filters directive
  if (node["activate_filter"]) {
    std::string filter_name = node["activate_filter"].as<std::string>();

    // Check if for ip_allow filter
    if (strcmp(filter_name.c_str(), "ip_allow") == 0) {
      bti->ip_allow_check_enabled_p = true;
      return {};
    }

    if ((rp = acl_filter_rule::find_byname(bti->rules_list, filter_name.c_str())) == nullptr) {
      Dbg(dbg_ctl_url_rewrite, "(Undefined filter '%s' in activate_filter directive)", filter_name.c_str());
      return swoc::Errata("(Undefined filter '{}' in activate_filter directive)", filter_name.c_str());
    }

    acl_filter_rule::requeue_in_active_list(&bti->rules_list, rp);
    return {};
  }

  // Check for deactivate_filters directive
  if (node["deactivate_filter"]) {
    std::string filter_name = node["deactivate_filter"].as<std::string>();

    // Check if for ip_allow filter
    if (strcmp(filter_name.c_str(), "ip_allow") == 0) {
      bti->ip_allow_check_enabled_p = false;
      return {};
    }

    if ((rp = acl_filter_rule::find_byname(bti->rules_list, filter_name.c_str())) == nullptr) {
      Dbg(dbg_ctl_url_rewrite, "(Undefined filter '%s' in deactivate_filter directive)", filter_name.c_str());
      return swoc::Errata("(Undefined filter '{}' in deactivate_filter directive)", filter_name.c_str());
    }

    acl_filter_rule::requeue_in_passive_list(&bti->rules_list, rp);
    return {};
  }

  // Check for delete_filters directive
  if (node["delete_filter"]) {
    std::string filter_name = node["delete_filter"].as<std::string>();

    acl_filter_rule::delete_byname(&bti->rules_list, filter_name.c_str());
    return {};
  }

  // Check for define_filter directive
  if (node["define_filter"]) {
    return parse_yaml_define_directive(node["define_filter"], bti);
  }

  return swoc::Errata("Not a filter directive");
}

swoc::Errata
parse_yaml_define_directive(const YAML::Node &node, BUILD_TABLE_INFO *bti)
{
  bool             flg;
  acl_filter_rule *rp;
  swoc::Errata     errata;

  if (!node || !node.IsMap()) {
    return swoc::Errata("named filters must be a map");
  }

  // When iterating over a YAML map, each element is a key-value pair
  // We expect a single-entry map here
  auto             it          = node.begin();
  std::string      filter_name = it->first.as<std::string>();
  const YAML::Node filter_spec = it->second;

  flg = ((rp = acl_filter_rule::find_byname(bti->rules_list, filter_name.c_str())) == nullptr) ? true : false;
  // coverity[alloc_arg]
  if ((errata = remap_validate_yaml_filter_args(&rp, filter_spec, bti->behavior_policy)).is_ok() && rp) {
    if (flg) { // new filter - add to list
      acl_filter_rule **rpp = nullptr;
      Dbg(dbg_ctl_url_rewrite, "[parse_directive] new rule \"%s\" was created", filter_name.c_str());
      for (rpp = &bti->rules_list; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      (*rpp = rp)->name(filter_name.c_str());
    }
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %zu argument(s) were added to rule \"%s\"", filter_spec.size(),
        filter_name.c_str());
    rp->add_node(filter_spec); // store string arguments for future processing
  }
  return errata;
}

swoc::Errata
process_yaml_filter_opt(url_mapping *mp, const YAML::Node &node, const BUILD_TABLE_INFO *bti)
{
  acl_filter_rule *rp, **rpp;
  swoc::Errata     errata;

  if (unlikely(!mp || !bti)) {
    Dbg(dbg_ctl_url_rewrite, "[process_yaml_filter_opt] Invalid argument(s)");
    return swoc::Errata("[process_yaml_filter_opt] Invalid argument(s)");
  }
  // ACLs are processed in this order:
  // 1. A remap.config ACL line for an individual remap rule.
  // 2. All named ACLs in remap.config.
  // 3. Rules as specified in ip_allow.yaml.
  if (node["acl_filter"]) {
    Dbg(dbg_ctl_url_rewrite, "[process_yaml_filter_opt] Add per remap filter");
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
      ;
    }
    errata = remap_validate_yaml_filter_args(rpp, node["acl_filter"], bti->behavior_policy);
  }

  for (rp = bti->rules_list; rp; rp = rp->next) {
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
      ;
    }
    if (rp->active_queue_flag) {
      Dbg(dbg_ctl_url_rewrite, "[process_yaml_filter_opt] Add active main filter \"%s\"",
          rp->filter_name ? rp->filter_name : "<nullptr>");
      for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      errata = remap_validate_yaml_filter_args(rpp, rp->node, bti->behavior_policy);
      if (!errata.is_ok()) {
        break;
      }
      if (auto rule = *rpp; rule) {
        // If no IP addresses are listed, treat that like `@src_ip=all`.
        if (rule->src_ip_valid == 0 && rule->src_ip_cnt == 0) {
          src_ip_info_t *ipi       = &rule->src_ip_array[rule->src_ip_cnt];
          ipi->match_all_addresses = true;
          rule->src_ip_cnt++;
          rule->src_ip_valid = 1;
        }
      }
    }
  }

  // Set the ip allow flag for this rule to the current ip allow flag state
  mp->ip_allow_check_enabled_p = bti->ip_allow_check_enabled_p;

  return errata;
}

swoc::Errata
parse_yaml_remap_fragment(const char *path, BUILD_TABLE_INFO *bti)
{
  // We need to create a new bti so that we don't clobber any state in the parent parse, but we want
  // to keep the ACL rules from the parent because ACLs must be global across the full set of config
  // files.
  BUILD_TABLE_INFO nbti;
  bool             success;

  if (access(path, R_OK) == -1) {
    return swoc::Errata("{}: {}", path, strerror(errno));
  }

  nbti.rules_list = bti->rules_list;
  nbti.rewrite    = bti->rewrite;

  Dbg(dbg_ctl_url_rewrite, "[%s] including remap configuration from %s", __func__, path);
  success = remap_parse_yaml_bti(path, &nbti);

  // The sub-parse might have updated the rules list, so push it up to the parent parse.
  bti->rules_list = nbti.rules_list;

  if (success) {
    // register the included file with the management subsystem so that we can correctly
    // reload them when they change
    load_remap_file_cb(ts::filename::REMAP, path);
  } else {
    return swoc::Errata("failed to parse included file {}", path);
  }

  return {};
}

swoc::Errata
parse_yaml_include_directive(const std::string &include_path, BUILD_TABLE_INFO *bti)
{
  ats_scoped_str path;
  swoc::Errata   errata;

  // The included path is relative to SYSCONFDIR
  path = RecConfigReadConfigPath(nullptr, include_path.c_str());

  if (ink_file_is_directory(path)) {
    struct dirent **entrylist;
    int             n_entries;

    n_entries = scandir(path, &entrylist, nullptr, alphasort);
    if (n_entries == -1) {
      return swoc::Errata("failed to open {}: {}", path.get(), strerror(errno));
    }

    for (int j = 0; j < n_entries; ++j) {
      ats_scoped_str subpath;

      if (isdot(entrylist[j]->d_name) || isdotdot(entrylist[j]->d_name)) {
        continue;
      }

      subpath = Layout::relative_to(path.get(), entrylist[j]->d_name);

      if (ink_file_is_directory(subpath)) {
        continue;
      }

      errata = parse_yaml_remap_fragment(subpath, bti);
      if (!errata.is_ok()) {
        break;
      }
    }

    free_directory_list(n_entries, entrylist);

  } else {
    errata = parse_yaml_remap_fragment(path, bti);
  }

  return errata;
}

swoc::Errata
parse_yaml_remap_rule(const YAML::Node &node, BUILD_TABLE_INFO *bti)
{
  std::string errStr;

  std::string_view fromScheme{}, toScheme{};
  std::string_view fromHost{}, toHost{};
  std::string_view fromUrl{}, toUrl{};
  std::string_view fromPath;
  char            *fromHost_lower     = nullptr;
  char            *fromHost_lower_ptr = nullptr;
  char             fromHost_lower_buf[1024];
  mapping_type     maptype;
  url_mapping     *new_mapping = nullptr;

  UrlRewrite::RegexMapping *reg_map = nullptr;
  bool                      is_cur_mapping_regex;
  const char               *type_id_str;

  swoc::Errata errata;
  const char  *valid_scheme = nullptr;

  if (!node || !node.IsMap()) {
    return swoc::Errata("remap rule must be a map");
  }

  // Parse for include directive first
  if (node["include"]) {
    return parse_yaml_include_directive(node["include"].as<std::string>(), bti);
  }

  // Parse for filter directives (activate/deactivate/delete/define)
  if (node["activate_filter"] || node["deactivate_filter"] || node["delete_filter"] || node["define_filter"]) {
    return parse_yaml_filter_directive(node, bti);
  }

  // Parse rule type
  if (!node["type"]) {
    return swoc::Errata("remap rule missing 'type' field");
  }
  std::string type_str = node["type"].as<std::string>();

  is_cur_mapping_regex = (strncasecmp("regex_", type_str.c_str(), 6) == 0);
  type_id_str          = is_cur_mapping_regex ? (type_str.c_str() + 6) : type_str.c_str();

  // Check to see whether is a reverse or forward mapping
  maptype = get_mapping_type(type_id_str, bti);
  if (maptype == mapping_type::NONE) {
    return swoc::Errata("unknown mapping type: {}", type_str);
  }

  new_mapping = new url_mapping();

  // apply filter rules if we have to
  errata = process_yaml_filter_opt(new_mapping, node, bti);
  if (!errata.is_ok()) {
    swoc::bwprint(errStr, "Failed to process filter: {}", errata);
    goto MAP_ERROR;
  }

  // update sticky flag
  bti->accept_check_p = bti->accept_check_p && bti->ip_allow_check_enabled_p;

  new_mapping->map_id = 0;
  if (node["mapid"]) {
    new_mapping->map_id = node["mapid"].as<unsigned int>();
  }

  // Parse from URL
  if (!node["from"]) {
    errStr = "remap rule missing 'from' field";
    goto MAP_ERROR;
  }

  errata = parse_yaml_url(node["from"], new_mapping->fromURL, true, fromUrl);
  if (!errata.is_ok()) {
    swoc::bwprint(errStr, "malformed From URL: {}", errata);
    goto MAP_ERROR;
  }

  // Parse to URL
  if (!node["to"]) {
    errStr = "remap rule missing 'to' field";
    goto MAP_ERROR;
  }

  errata = parse_yaml_url(node["to"], new_mapping->toURL, false, toUrl);
  if (!errata.is_ok()) {
    swoc::bwprint(errStr, "malformed To URL: {}", errata);
    goto MAP_ERROR;
  }

  // Check if valid schemes
  fromScheme = new_mapping->fromURL.scheme_get();
  toScheme   = new_mapping->toURL.scheme_get();
  if (fromScheme.empty()) {
    new_mapping->fromURL.scheme_set(std::string_view{URL_SCHEME_HTTP});
    new_mapping->wildcard_from_scheme = true;
    fromScheme                        = new_mapping->fromURL.scheme_get();
  }
  valid_scheme = is_valid_scheme(fromScheme, toScheme);
  if (valid_scheme != nullptr) {
    errStr = valid_scheme;
    goto MAP_ERROR;
  }

  // Check if map_with_referer is used
  if (node["redirect"] && maptype == mapping_type::FORWARD_MAP_REFERER) {
    errata = parse_map_referer(node["redirect"], new_mapping);
    if (!errata.is_ok()) {
      swoc::bwprint(errStr, "invalid map_with_referer: {}", errata);
      goto MAP_ERROR;
    }
  }

  // Check to see the fromHost remapping is a relative one
  fromHost = new_mapping->fromURL.host_get();
  if (fromHost.empty()) {
    if (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
        maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT) {
      fromPath = new_mapping->fromURL.path_get();
      if ((fromPath.empty() || fromPath[0] != '/') && (fromUrl.empty() || fromUrl[0] != '/')) {
        errStr = "relative remappings must begin with a /";
        goto MAP_ERROR;
      } else {
        fromHost = ""sv;
      }
    } else {
      errStr = "remap source in reverse mappings requires a hostname";
      goto MAP_ERROR;
    }
  }

  toHost = new_mapping->toURL.host_get();
  if (toHost.empty()) {
    errStr = "The remap destinations require a hostname";
    goto MAP_ERROR;
  }
  // Get rid of trailing slashes since they interfere
  //  with our ability to send redirects

  // You might be tempted to remove these lines but the new
  // optimized header system will introduce problems.  You
  // might get two slashes occasionally instead of one because
  // the rest of the system assumes that trailing slashes have
  // been removed.

  if (unlikely(fromHost.length() >= sizeof(fromHost_lower_buf))) {
    fromHost_lower = (fromHost_lower_ptr = static_cast<char *>(ats_malloc(fromHost.length() + 1)));
  } else {
    fromHost_lower = &fromHost_lower_buf[0];
  }
  // Canonicalize the hostname by making it lower case
  memcpy(fromHost_lower, fromHost.data(), fromHost.length());
  fromHost_lower[fromHost.length()] = 0;
  LowerCaseStr(fromHost_lower);

  // set the normalized string so nobody else has to normalize this
  new_mapping->fromURL.host_set({fromHost_lower, fromHost.length()});

  if (is_cur_mapping_regex) {
    reg_map = new UrlRewrite::RegexMapping();
    if (!process_regex_mapping_config(fromHost_lower, new_mapping, reg_map)) {
      errStr = "could not process regex mapping config line";
      goto MAP_ERROR;
    }
    Dbg(dbg_ctl_url_rewrite, "Configured regex rule for host [%s]", fromHost_lower);
  }

  // If a TS receives a request on a port which is set to tunnel mode
  // (ie, blind forwarding) and a client connects directly to the TS,
  // then the TS will use its IPv4 address and remap rules given
  // to send the request to its proper destination.
  // See HttpTransact::HandleBlindTunnel().
  // Therefore, for a remap with "type: map" and "scheme: tunnel",
  // we also needs to convert hostname to its IPv4 addr
  // and gives a new remap rule with the IPv4 addr.
  if ((maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
       maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT) &&
      fromScheme == std::string_view{URL_SCHEME_TUNNEL} && (fromHost_lower[0] < '0' || fromHost_lower[0] > '9')) {
    addrinfo      *ai_records; // returned records.
    ip_text_buffer ipb;        // buffer for address string conversion.
    if (0 == getaddrinfo(fromHost_lower, nullptr, nullptr, &ai_records)) {
      for (addrinfo *ai_spot = ai_records; ai_spot; ai_spot = ai_spot->ai_next) {
        if (ats_is_ip(ai_spot->ai_addr) && !ats_is_ip_any(ai_spot->ai_addr) && ai_spot->ai_protocol == IPPROTO_TCP) {
          url_mapping *u_mapping;

          ats_ip_ntop(ai_spot->ai_addr, ipb, sizeof ipb);
          u_mapping = new url_mapping;
          u_mapping->fromURL.create(nullptr);
          u_mapping->fromURL.copy(&new_mapping->fromURL);
          u_mapping->fromURL.host_set({ipb});
          u_mapping->toURL.create(nullptr);
          u_mapping->toURL.copy(&new_mapping->toURL);

          if (!bti->rewrite->InsertForwardMapping(maptype, u_mapping, ipb)) {
            errStr = "unable to add mapping rule to lookup table";
            freeaddrinfo(ai_records);
            goto MAP_ERROR;
          }
        }
      }

      freeaddrinfo(ai_records);
    }
  }

  // check for a 'strategy' and if wire it up if one exists.
  if (node["strategy"] && (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
                           maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT)) {
    std::string strategy_name = node["strategy"].as<std::string>();
    new_mapping->strategy     = bti->rewrite->strategyFactory->strategyInstance(strategy_name.c_str());
    if (new_mapping->strategy == nullptr) {
      errStr = "missing 'strategy' name argument, unable to add mapping rule";
      goto MAP_ERROR;
    }
    Dbg(dbg_ctl_url_rewrite, "mapped the 'strategy' named %s", strategy_name.c_str());
  }

  // Check "remap" plugin options and load .so object
  if (node["plugins"] && (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
                          maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT)) {
    if (!node["plugins"] || !node["plugins"].IsSequence()) {
      errStr = "plugins must be a sequence";
      goto MAP_ERROR;
    }

    for (const auto &plugin : node["plugins"]) {
      errata = parse_yaml_plugins(plugin, new_mapping, bti);
      if (!errata.is_ok()) {
        swoc::bwprint(errStr, "{}", errata);
        goto MAP_ERROR;
      }
    }
  }

  // Now add the mapping to appropriate container
  if (!bti->rewrite->InsertMapping(maptype, new_mapping, reg_map, fromHost_lower, is_cur_mapping_regex)) {
    errStr = "unable to add mapping rule to lookup table";
    goto MAP_ERROR;
  }

  ats_free_null(fromHost_lower_ptr);

  Dbg(dbg_ctl_remap_yaml, "Successfully added mapping rule");
  return {};

// Deal with error / warning scenarios
MAP_ERROR:

  Error("%s", errStr.c_str());

  delete reg_map;
  delete new_mapping;
  return swoc::Errata(errStr);
}

bool
remap_parse_yaml_bti(const char *path, BUILD_TABLE_INFO *bti)
{
  try {
    Dbg(dbg_ctl_remap_yaml, "Parsing YAML config file: %s", path);

    YAML::Node config = YAML::LoadFile(path);

    if (config.IsNull()) {
      Dbg(dbg_ctl_remap_yaml, "Empty YAML config file");
      return true; // a missing file is ok - treat as empty, no rules.
    }

    Dbg(dbg_ctl_url_rewrite, "[BuildTable] UrlRewrite::BuildTable()");

    ACLBehaviorPolicy behavior_policy = ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY;
    if (!UrlRewrite::get_acl_behavior_policy(behavior_policy)) {
      Warning("Failed to get ACL matching policy.");
      return false;
    }
    bti->behavior_policy = behavior_policy;

    // Parse global filters section (optional)
    if (config["acl_filters"] && config["acl_filters"].IsMap()) {
      for (const auto &filter_def : config["acl_filters"]) {
        auto errata = parse_yaml_define_directive(filter_def, bti);
        if (!errata.is_ok()) {
          Error("Failed to parse acl_filters section");
          return false;
        }
      }
    }

    if (config["remap"].IsNull() || !config["remap"].IsSequence()) {
      Error("Expected toplevel 'remap' key to be a sequence");
      return false;
    }

    // Parse each remap rule
    for (const auto &rule : config["remap"]) {
      // Reset bti state for each rule (but keep rules_list for named filters)
      bti->reset();

      auto errata = parse_yaml_remap_rule(rule, bti);
      if (!errata.is_ok()) {
        Error("Failed to parse remap rule");
        return false;
      }
    }

    IpAllow::enableAcceptCheck(bti->accept_check_p);

    Dbg(dbg_ctl_remap_yaml, "Successfully parsed remap.yaml config");
    return true;

  } catch (YAML::Exception &ex) {
    Error("YAML parsing error in %s: %s", path, ex.what());
  } catch (std::exception &ex) {
    Error("Exception parsing YAML config %s: %s", path, ex.what());
  }
  return false;
}

bool
remap_parse_yaml(const char *path, UrlRewrite *rewrite)
{
  BUILD_TABLE_INFO bti;

  /* If this happens to be a config reload, the list of loaded remap plugins is non-empty, and we
   * can signal all these plugins that a reload has begun. */
  rewrite->pluginFactory.indicatePreReload();

  bti.rewrite = rewrite;
  bool status = remap_parse_yaml_bti(path, &bti);

  /* Now after we parsed the configuration and (re)loaded plugins and plugin instances
   * accordingly notify all plugins that we are done */
  rewrite->pluginFactory.indicatePostReload(status);

  bti.clear_acl_rules_list();

  return status;
}
