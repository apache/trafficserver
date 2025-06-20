/** @file
 *
 *  Remap configuration file parsing.
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

#include "proxy/http/remap/AclFiltering.h"
#include "swoc/swoc_file.h"

#include "proxy/http/remap/RemapConfig.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "proxy/ReverseProxy.h"
#include "tscore/Layout.h"
#include "proxy/hdrs/HTTP.h"
#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "tscore/ink_cap.h"
#include "tscore/Tokenizer.h"
#include "tscore/Filenames.h"
#include "proxy/IPAllow.h"
#include "proxy/http/remap/PluginFactory.h"

using namespace std::literals;

#define modulePrefix "[ReverseProxy]"

load_remap_file_func load_remap_file_cb = nullptr;

namespace
{
DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};
DbgCtl dbg_ctl_remap_plugin{"remap_plugin"};
DbgCtl dbg_ctl_url_rewrite_regex{"url_rewrite_regex"};
} // end anonymous namespace

/**
  Returns the length of the URL.

  Will replace the terminator with a '/' if this is a full URL and
  there are no '/' in it after the host.  This ensures that class
  URL parses the URL correctly.

*/
static int
UrlWhack(char *toWhack, int *origLength)
{
  int   length = strlen(toWhack);
  char *tmp;
  *origLength = length;

  // Check to see if this a full URL
  tmp = strstr(toWhack, "://");
  if (tmp != nullptr) {
    if (strchr(tmp + 3, '/') == nullptr) {
      toWhack[length] = '/';
      length++;
    }
  }
  return length;
}

/**
  Cleanup *char[] array - each item in array must be allocated via
  ats_malloc or similar "x..." function.

*/
static void
clear_xstr_array(char *v[], size_t vsize)
{
  for (unsigned i = 0; i < vsize; i++) {
    v[i] = static_cast<char *>(ats_free_null(v[i]));
  }
}

BUILD_TABLE_INFO::BUILD_TABLE_INFO()

{
  memset(this->paramv, 0, sizeof(this->paramv));
  memset(this->argv, 0, sizeof(this->argv));
}

BUILD_TABLE_INFO::~BUILD_TABLE_INFO()
{
  this->reset();
}

void
BUILD_TABLE_INFO::reset()
{
  this->paramc = this->argc = 0;
  clear_xstr_array(this->paramv, sizeof(this->paramv) / sizeof(char *));
  clear_xstr_array(this->argv, sizeof(this->argv) / sizeof(char *));
}

void
BUILD_TABLE_INFO::clear_acl_rules_list()
{
  // clean up any leftover named filter rules
  auto *rp = rules_list;
  while (rp != nullptr) {
    auto *tmp = rp->next;
    delete rp;
    rp = tmp;
  }
}

static const char *
process_filter_opt(url_mapping *mp, const BUILD_TABLE_INFO *bti, char *errStrBuf, int errStrBufSize)
{
  acl_filter_rule *rp, **rpp;
  const char      *errStr = nullptr;

  if (unlikely(!mp || !bti || !errStrBuf || errStrBufSize <= 0)) {
    Dbg(dbg_ctl_url_rewrite, "[process_filter_opt] Invalid argument(s)");
    return "[process_filter_opt] Invalid argument(s)";
  }
  // ACLs are processed in this order:
  // 1. A remap.config ACL line for an individual remap rule.
  // 2. All named ACLs in remap.config.
  // 3. Rules as specified in ip_allow.yaml.
  if (!errStr && (bti->remap_optflg & REMAP_OPTFLG_ALL_FILTERS) != 0) {
    Dbg(dbg_ctl_url_rewrite, "[process_filter_opt] Add per remap filter");
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
      ;
    }
    errStr = remap_validate_filter_args(rpp, bti->argv, bti->argc, errStrBuf, errStrBufSize, bti->behavior_policy);
  }

  for (rp = bti->rules_list; rp; rp = rp->next) {
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
      ;
    }
    if (rp->active_queue_flag) {
      Dbg(dbg_ctl_url_rewrite, "[process_filter_opt] Add active main filter \"%s\" (argc=%d)",
          rp->filter_name ? rp->filter_name : "<nullptr>", rp->argc);
      for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      if ((errStr = remap_validate_filter_args(rpp, rp->argv, rp->argc, errStrBuf, errStrBufSize, bti->behavior_policy)) !=
          nullptr) {
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

  return errStr;
}

static bool
is_inkeylist(const char *key, ...)
{
  va_list ap;

  if (unlikely(key == nullptr || key[0] == '\0')) {
    return false;
  }

  va_start(ap, key);

  const char *str = va_arg(ap, const char *);
  while (str) {
    if (!strcasecmp(key, str)) {
      va_end(ap);
      return true;
    }

    str = va_arg(ap, const char *);
  }

  va_end(ap);
  return false;
}

static const char *
parse_define_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  bool             flg;
  acl_filter_rule *rp;
  const char      *cstr = nullptr;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }
  if (bti->argc < 1) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have filter parameter(s)", directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  flg = ((rp = acl_filter_rule::find_byname(bti->rules_list, bti->paramv[1])) == nullptr) ? true : false;
  // coverity[alloc_arg]
  if ((cstr = remap_validate_filter_args(&rp, bti->argv, bti->argc, errbuf, errbufsize, bti->behavior_policy)) == nullptr && rp) {
    if (flg) { // new filter - add to list
      acl_filter_rule **rpp = nullptr;
      Dbg(dbg_ctl_url_rewrite, "[parse_directive] new rule \"%s\" was created", bti->paramv[1]);
      for (rpp = &bti->rules_list; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      (*rpp = rp)->name(bti->paramv[1]);
    }
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %d argument(s) were added to rule \"%s\"", bti->argc, bti->paramv[1]);
    rp->add_argv(bti->argc, bti->argv); // store string arguments for future processing
  }

  return cstr;
}

static const char *
parse_delete_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  acl_filter_rule::delete_byname(&bti->rules_list, bti->paramv[1]);
  return nullptr;
}

static const char *
parse_activate_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  acl_filter_rule *rp;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  // Check if for ip_allow filter
  if (strcmp(bti->paramv[1], "ip_allow") == 0) {
    bti->ip_allow_check_enabled_p = true;
    return nullptr;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, bti->paramv[1])) == nullptr) {
    snprintf(errbuf, errbufsize, R"(Undefined filter "%s" in directive "%s")", bti->paramv[1], directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  acl_filter_rule::requeue_in_active_list(&bti->rules_list, rp);
  return nullptr;
}

static const char *
parse_deactivate_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  acl_filter_rule *rp;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  // Check if for ip_allow filter
  if (strcmp(bti->paramv[1], "ip_allow") == 0) {
    bti->ip_allow_check_enabled_p = false;
    return nullptr;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, bti->paramv[1])) == nullptr) {
    snprintf(errbuf, errbufsize, R"(Undefined filter "%s" in directive "%s")", bti->paramv[1], directive);
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
    return errbuf;
  }

  acl_filter_rule::requeue_in_passive_list(&bti->rules_list, rp);
  return nullptr;
}

static void
free_directory_list(int n_entries, struct dirent **entrylist)
{
  for (int i = 0; i < n_entries; ++i) {
    free(entrylist[i]);
  }

  free(entrylist);
}

static const char *
parse_remap_fragment(const char *path, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  // We need to create a new bti so that we don't clobber any state in the parent parse, but we want
  // to keep the ACL rules from the parent because ACLs must be global across the full set of config
  // files.
  BUILD_TABLE_INFO nbti;
  bool             success;

  if (access(path, R_OK) == -1) {
    snprintf(errbuf, errbufsize, "%s: %s", path, strerror(errno));
    return errbuf;
  }

  nbti.rules_list = bti->rules_list;
  nbti.rewrite    = bti->rewrite;

  Dbg(dbg_ctl_url_rewrite, "[%s] including remap configuration from %s", __func__, path);
  success = remap_parse_config_bti(path, &nbti);

  // The sub-parse might have updated the rules list, so push it up to the parent parse.
  bti->rules_list = nbti.rules_list;

  if (success) {
    // register the included file with the management subsystem so that we can correctly
    // reload them when they change
    load_remap_file_cb(ts::filename::REMAP, path);
  } else {
    snprintf(errbuf, errbufsize, "failed to parse included file %s", path);
    return errbuf;
  }

  return nullptr;
}

static const char *
parse_include_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have a path argument", directive);
    Dbg(dbg_ctl_url_rewrite, "[%s] %s", __func__, errbuf);
    return errbuf;
  }

  for (unsigned i = 1; i < static_cast<unsigned>(bti->paramc); ++i) {
    ats_scoped_str path;
    const char    *errmsg = nullptr;

    // The included path is relative to SYSCONFDIR, just like remap.config is.
    path = RecConfigReadConfigPath(nullptr, bti->paramv[i]);

    if (ink_file_is_directory(path)) {
      struct dirent **entrylist;
      int             n_entries;

      n_entries = scandir(path, &entrylist, nullptr, alphasort);
      if (n_entries == -1) {
        snprintf(errbuf, errbufsize, "failed to open %s: %s", path.get(), strerror(errno));
        return errbuf;
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

        errmsg = parse_remap_fragment(subpath, bti, errbuf, errbufsize);
        if (errmsg != nullptr) {
          break;
        }
      }

      free_directory_list(n_entries, entrylist);

    } else {
      errmsg = parse_remap_fragment(path, bti, errbuf, errbufsize);
    }

    if (errmsg) {
      return errmsg;
    }
  }

  return nullptr;
}

struct remap_directive {
  const char *name;
  const char *(*parser)(const char *, BUILD_TABLE_INFO *, char *, size_t);
};

static const remap_directive directives[] = {

  {".definefilter",     parse_define_directive    },
  {".deffilter",        parse_define_directive    },
  {".defflt",           parse_define_directive    },

  {".deletefilter",     parse_delete_directive    },
  {".delfilter",        parse_delete_directive    },
  {".delflt",           parse_delete_directive    },

  {".usefilter",        parse_activate_directive  },
  {".activefilter",     parse_activate_directive  },
  {".activatefilter",   parse_activate_directive  },
  {".useflt",           parse_activate_directive  },

  {".unusefilter",      parse_deactivate_directive},
  {".deactivatefilter", parse_deactivate_directive},
  {".unactivefilter",   parse_deactivate_directive},
  {".deuseflt",         parse_deactivate_directive},
  {".unuseflt",         parse_deactivate_directive},

  {".include",          parse_include_directive   },
};

const char *
remap_parse_directive(BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  const char *directive = nullptr;

  // Check arguments
  if (unlikely(!bti || !errbuf || errbufsize == 0 || !bti->paramc || (directive = bti->paramv[0]) == nullptr)) {
    Dbg(dbg_ctl_url_rewrite, "[parse_directive] Invalid argument(s)");
    return "Invalid argument(s)";
  }

  for (unsigned i = 0; i < countof(directives); ++i) {
    if (strcmp(directive, directives[i].name) == 0) {
      return directives[i].parser(directive, bti, errbuf, errbufsize);
    }
  }

  snprintf(errbuf, errbufsize, "Unknown directive \"%s\"", directive);
  Dbg(dbg_ctl_url_rewrite, "[parse_directive] %s", errbuf);
  return errbuf;
}

const char *
remap_validate_filter_args(acl_filter_rule **rule_pp, const char *const *argv, int argc, char *errStrBuf, size_t errStrBufSize,
                           ACLBehaviorPolicy behavior_policy)
{
  acl_filter_rule *rule;
  int              i, j;
  bool             new_rule_flg = false;

  if (!rule_pp) {
    Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Invalid argument(s)");
    return "Invalid argument(s)";
  }

  if (dbg_ctl_url_rewrite.on()) {
    printf("validate_filter_args: ");
    for (i = 0; i < argc; i++) {
      printf("\"%s\" ", argv[i]);
    }
    printf("\n");
  }

  if ((rule = *rule_pp) == nullptr) {
    rule = new acl_filter_rule();
    if (unlikely((*rule_pp = rule) == nullptr)) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Memory allocation error");
      return "Memory allocation Error";
    }
    new_rule_flg = true;
    Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] new acl_filter_rule class was created during remap rule processing");
  }

  bool action_flag = false;
  for (i = 0; i < argc; i++) {
    unsigned long ul;
    bool          hasarg;

    const char *argptr;
    if ((ul = remap_check_option(&argv[i], 1, 0, nullptr, &argptr)) == 0) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unknown remap option - %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Unknown option - \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = nullptr;
      }
      return errStrBuf;
    }

    // Every filter operator requires an argument except @internal.
    hasarg = !(ul & REMAP_OPTFLG_INTERNAL);

    if (hasarg && (!argptr || !argptr[0])) {
      Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Empty argument in %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Empty argument in \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = nullptr;
      }
      return errStrBuf;
    }

    if (ul & REMAP_OPTFLG_METHOD) { /* "method=" option */
      // Please remember that the order of hash idx creation is very important and it is defined
      // in HTTP.cc file. 0 in our array is the first method, CONNECT
      int m = hdrtoken_tokenize(argptr, strlen(argptr), nullptr) - HTTP_WKSIDX_CONNECT;

      if (m >= 0 && m < HTTP_WKSIDX_METHODS_CNT) {
        rule->standard_method_lookup[m] = true;
      } else {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Using nonstandard method [%s]", argptr);
        rule->nonstandard_methods.insert(argptr);
      }
      rule->method_restriction_enabled = true;
    }

    if (ul & REMAP_OPTFLG_SRC_IP) { /* "src_ip=" option */
      if (rule->src_ip_cnt >= ACL_FILTER_MAX_SRC_IP) {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"src_ip=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"src_ip=\" filters!", ACL_FILTER_MAX_SRC_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      src_ip_info_t *ipi = &rule->src_ip_array[rule->src_ip_cnt];
      if (ul & REMAP_OPTFLG_INVERT) {
        ipi->invert = true;
      }
      std::string_view arg{argptr};
      if (arg == "all") {
        ipi->match_all_addresses = true;
      } else if (ats_ip_range_parse(arg, ipi->start, ipi->end) != 0) {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unable to parse IP value in %s", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unable to parse IP value in %s", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      for (j = 0; j < rule->src_ip_cnt; j++) {
        if (rule->src_ip_array[j].start == ipi->start && rule->src_ip_array[j].end == ipi->end) {
          ipi->reset();
          ipi = nullptr;
          break; /* we have the same src_ip in the list */
        }
      }
      if (ipi) {
        rule->src_ip_cnt++;
        rule->src_ip_valid = 1;
      }
    }

    if (ul & REMAP_OPTFLG_SRC_IP_CATEGORY) { /* "src_ip_category=" option */
      if (rule->src_ip_category_cnt >= ACL_FILTER_MAX_SRC_IP) {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"src_ip_category=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"src_ip_category=\" filters!", ACL_FILTER_MAX_SRC_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      src_ip_category_info_t *ipi = &rule->src_ip_category_array[rule->src_ip_category_cnt];
      ipi->category.assign(argptr);
      if (ul & REMAP_OPTFLG_INVERT) {
        ipi->invert = true;
      }
      for (j = 0; j < rule->src_ip_category_cnt; j++) {
        if (rule->src_ip_category_array[j].category == ipi->category) {
          ipi->reset();
          ipi = nullptr;
          break; /* we have the same src_ip_category in the list */
        }
      }
      if (ipi) {
        rule->src_ip_category_cnt++;
        rule->src_ip_category_valid = 1;
      }
    }

    if (ul & REMAP_OPTFLG_IN_IP) { /* "in_ip=" option */
      if (rule->in_ip_cnt >= ACL_FILTER_MAX_IN_IP) {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Too many \"in_ip=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"in_ip=\" filters!", ACL_FILTER_MAX_IN_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      src_ip_info_t *ipi = &rule->in_ip_array[rule->in_ip_cnt];
      if (ul & REMAP_OPTFLG_INVERT) {
        ipi->invert = true;
      }
      // important! use copy of argument
      std::string_view arg{argptr};
      if (arg == "all") {
        ipi->match_all_addresses = true;
      } else if (ats_ip_range_parse(arg, ipi->start, ipi->end) != 0) {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unable to parse IP value in %s", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unable to parse IP value in %s", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      for (j = 0; j < rule->in_ip_cnt; j++) {
        if (rule->in_ip_array[j].start == ipi->start && rule->in_ip_array[j].end == ipi->end) {
          ipi->reset();
          ipi = nullptr;
          break; /* we have the same src_ip in the list */
        }
      }
      if (ipi) {
        rule->in_ip_cnt++;
        rule->in_ip_valid = 1;
      }
    }

    if (ul & REMAP_OPTFLG_ACTION) { /* "action=" option */
      if (action_flag) {
        std::string_view err = "Only one @action= is allowed per remap ACL";
        Dbg(dbg_ctl_url_rewrite, "%s", err.data());
        snprintf(errStrBuf, errStrBufSize, "%s", err.data());
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
      action_flag = true;
      if (behavior_policy == ACLBehaviorPolicy::ACL_BEHAVIOR_MODERN) {
        // With the new matching policy, we don't allow the legacy "allow" and
        // "deny" actions. Users must transition to either add_allow/add_deny or
        // set_allow/set_deny.
        if (is_inkeylist(argptr, "allow", "deny", nullptr)) {
          Dbg(
            dbg_ctl_url_rewrite,
            R"([validate_filter_args] "allow" and "deny" are no longer valid. Use add_allow/add_deny or set_allow/set_deny: "%s"")",
            argv[i]);
          snprintf(errStrBuf, errStrBufSize,
                   R"("allow" and "deny" are no longer valid. Use add_allow/add_deny or set_allow/set_deny: "%s"")", argv[i]);
          errStrBuf[errStrBufSize - 1] = 0;
          if (new_rule_flg) {
            delete rule;
            *rule_pp = nullptr;
          }
          return errStrBuf;
        }
      }
      if (is_inkeylist(argptr, "add_allow", "add_deny", nullptr)) {
        rule->add_flag = 1;
      } else {
        rule->add_flag = 0;
      }
      // Remove "deny" from this list when MATCH_ON_IP_AND_METHOD is removed in 11.x.
      if (is_inkeylist(argptr, "0", "off", "deny", "set_deny", "add_deny", "disable", nullptr)) {
        rule->allow_flag = 0;
        // Remove "allow" from this list when MATCH_ON_IP_AND_METHOD is removed in 11.x.
      } else if (is_inkeylist(argptr, "1", "on", "allow", "set_allow", "add_allow", "enable", nullptr)) {
        rule->allow_flag = 1;
      } else {
        Dbg(dbg_ctl_url_rewrite, "[validate_filter_args] Unknown argument \"%s\"", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unknown argument \"%s\"", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return errStrBuf;
      }
    }

    if (ul & REMAP_OPTFLG_INTERNAL) {
      rule->internal = 1;
    }
  }

  if (dbg_ctl_url_rewrite.on()) {
    rule->print();
  }

  return nullptr; /* success */
}

unsigned long
remap_check_option(const char *const *argv, int argc, unsigned long findmode, int *_ret_idx, const char **argptr)
{
  unsigned long ret_flags = 0;
  int           idx       = 0;

  if (argptr) {
    *argptr = nullptr;
  }
  if (argv && argc > 0) {
    for (int i = 0; i < argc; i++) {
      if (!strcasecmp(argv[i], "map_with_referer")) {
        if ((findmode & REMAP_OPTFLG_MAP_WITH_REFERER) != 0) {
          idx = i;
        }
        ret_flags |= REMAP_OPTFLG_MAP_WITH_REFERER;
      } else if (!strncasecmp(argv[i], "plugin=", 7)) {
        if ((findmode & REMAP_OPTFLG_PLUGIN) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_PLUGIN;
      } else if (!strncasecmp(argv[i], "pparam=", 7)) {
        if ((findmode & REMAP_OPTFLG_PPARAM) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_PPARAM;
      } else if (!strncasecmp(argv[i], "method=", 7)) {
        if ((findmode & REMAP_OPTFLG_METHOD) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_METHOD;
      } else if (!strncasecmp(argv[i], "src_ip=~", 8)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][8];
        }
        ret_flags |= (REMAP_OPTFLG_SRC_IP | REMAP_OPTFLG_INVERT);
      } else if (!strncasecmp(argv[i], "src_ip_category=~", 17)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP_CATEGORY) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][17];
        }
        ret_flags |= (REMAP_OPTFLG_SRC_IP_CATEGORY | REMAP_OPTFLG_INVERT);
      } else if (!strncasecmp(argv[i], "src_ip=", 7)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_SRC_IP;
      } else if (!strncasecmp(argv[i], "src_ip_category=", 16)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP_CATEGORY) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][16];
        }
        ret_flags |= REMAP_OPTFLG_SRC_IP_CATEGORY;
      } else if (!strncasecmp(argv[i], "in_ip=~", 7)) {
        if ((findmode & REMAP_OPTFLG_IN_IP) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= (REMAP_OPTFLG_IN_IP | REMAP_OPTFLG_INVERT);
      } else if (!strncasecmp(argv[i], "in_ip=", 6)) {
        if ((findmode & REMAP_OPTFLG_IN_IP) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][6];
        }
        ret_flags |= REMAP_OPTFLG_IN_IP;
      } else if (!strncasecmp(argv[i], "action=", 7)) {
        if ((findmode & REMAP_OPTFLG_ACTION) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_ACTION;
      } else if (!strncasecmp(argv[i], "mapid=", 6)) {
        if ((findmode & REMAP_OPTFLG_MAP_ID) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][6];
        }
        ret_flags |= REMAP_OPTFLG_MAP_ID;
      } else if (!strncasecmp(argv[i], "internal", 8)) {
        if ((findmode & REMAP_OPTFLG_INTERNAL) != 0) {
          idx = i;
        }
        ret_flags |= REMAP_OPTFLG_INTERNAL;
      } else if (!strncasecmp(argv[i], "strategy=", 9)) {
        if ((findmode & REMAP_OPTFLG_STRATEGY) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][9];
        }
        ret_flags |= REMAP_OPTFLG_STRATEGY;
      } else {
        Warning("ignoring invalid remap option '%s'", argv[i]);
      }

      if ((findmode & ret_flags) && !argptr) {
        if (_ret_idx) {
          *_ret_idx = idx;
        }
        return ret_flags;
      }
    }
  }
  if (_ret_idx) {
    *_ret_idx = idx;
  }
  return ret_flags;
}

/**
 * @brief loads a remap plugin
 *
 * @pparam mp url mapping
 * @pparam errbuf error buffer
 * @pparam errbufsize size of the error buffer
 * @pparam jump_to_argc
 * @pparam plugin_found_at
 * @return success - true, failure - false
 */
bool
remap_load_plugin(const char *const *argv, int argc, url_mapping *mp, char *errbuf, int errbufsize, int jump_to_argc,
                  int *plugin_found_at, UrlRewrite *rewrite)
{
  char       *c, *err;
  const char *new_argv[1024];
  char       *pargv[1024];
  int         idx  = 0;
  int         parc = 0;
  *plugin_found_at = 0;

  memset(pargv, 0, sizeof(pargv));
  memset(new_argv, 0, sizeof(new_argv));

  ink_assert(static_cast<unsigned>(argc) < countof(new_argv));

  if (jump_to_argc != 0) {
    argc  -= jump_to_argc;
    int i  = 0;
    while (argv[i + jump_to_argc]) {
      new_argv[i] = argv[i + jump_to_argc];
      i++;
    }
    argv = &new_argv[0];
    if (!remap_check_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx)) {
      return false;
    }
  } else {
    if (unlikely(!mp || (remap_check_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx) & REMAP_OPTFLG_PLUGIN) == 0)) {
      snprintf(errbuf, errbufsize, "Can't find remap plugin keyword or \"url_mapping\" is nullptr");
      return false; /* incorrect input data - almost impossible case */
    }
  }

  if (unlikely((c = (char *)strchr(argv[idx], (int)'=')) == nullptr || !(*(++c)))) {
    snprintf(errbuf, errbufsize, "Can't find remap plugin file name in \"@%s\"", argv[idx]);
    return false; /* incorrect input data */
  }

  Dbg(dbg_ctl_remap_plugin, "using path %s for plugin", c);

  /* Prepare remap plugin parameters from the config */
  if ((err = mp->fromURL.string_get(nullptr)) == nullptr) {
    snprintf(errbuf, errbufsize, "Can't load fromURL from URL class");
    return false;
  }
  pargv[parc++] = ats_strdup(err);
  ats_free(err);

  if ((err = mp->toURL.string_get(nullptr)) == nullptr) {
    snprintf(errbuf, errbufsize, "Can't load toURL from URL class");
    return false;
  }
  pargv[parc++] = ats_strdup(err);
  ats_free(err);

  bool plugin_encountered = false;
  // how many plugin parameters we have for this remapping
  for (idx = 0; idx < argc && parc < static_cast<int>(countof(pargv) - 1); idx++) {
    if (plugin_encountered && !strncasecmp("plugin=", argv[idx], 7) && argv[idx][7]) {
      *plugin_found_at = idx;
      break; // if there is another plugin, lets deal with that later
    }

    if (!strncasecmp("plugin=", argv[idx], 7)) {
      plugin_encountered = true;
    }

    if (!strncasecmp("pparam=", argv[idx], 7) && argv[idx][7]) {
      pargv[parc++] = const_cast<char *>(&(argv[idx][7]));
    }
  }

  Dbg(dbg_ctl_url_rewrite, "Viewing all parameters for config line");
  for (int k = 0; k < argc; k++) {
    Dbg(dbg_ctl_url_rewrite, "Argument %d: %s", k, argv[k]);
  }

  Dbg(dbg_ctl_url_rewrite, "Viewing parsed plugin parameters for %s: [%d]", c, *plugin_found_at);
  for (int k = 0; k < parc; k++) {
    Dbg(dbg_ctl_url_rewrite, "Argument %d: %s", k, pargv[k]);
  }

  RemapPluginInst *pi = nullptr;
  std::string      error;
  {
    uint32_t elevate_access = 0;
    elevate_access          = RecGetRecordInt("proxy.config.plugin.load_elevated").value_or(0);
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    pi = rewrite->pluginFactory.getRemapPlugin(swoc::file::path(const_cast<const char *>(c)), parc, pargv, error,
                                               isPluginDynamicReloadEnabled());
  } // done elevating access

  bool result = true;
  if (nullptr == pi) {
    snprintf(errbuf, errbufsize, "%s", error.c_str());
    result = false;
  } else {
    mp->add_plugin_instance(pi);
  }

  ats_free(pargv[0]); // fromURL
  ats_free(pargv[1]); // toURL

  return result;
}
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
  int              captures;

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

bool
remap_parse_config_bti(const char *path, BUILD_TABLE_INFO *bti)
{
  char        errBuf[1024];
  char        errStrBuf[1024];
  const char *errStr;

  Tokenizer whiteTok(" \t");

  // Vars to parse line in file
  char       *tok_state, *cur_line, *cur_line_tmp;
  int         cur_line_size, cln = 0; // Our current line number
  ParseResult rparse;

  // Vars to build the mapping
  std::string_view fromScheme{}, toScheme{};
  std::string_view fromHost{}, toHost{};
  char            *map_from, *map_from_start;
  char            *map_to, *map_to_start;
  const char      *tmp; // Appease the DEC compiler
  char            *fromHost_lower     = nullptr;
  char            *fromHost_lower_ptr = nullptr;
  char             fromHost_lower_buf[1024];
  url_mapping     *new_mapping = nullptr;
  mapping_type     maptype;
  referer_info    *ri;
  int              origLength;
  int              length;
  int              tok_count;

  UrlRewrite::RegexMapping *reg_map;
  bool                      is_cur_mapping_regex;
  const char               *type_id_str;

  std::error_code ec;
  std::string     content{swoc::file::load(swoc::file::path{path}, ec)};
  if (ec.value() == ENOENT) { // a missing file is ok - treat as empty, no rules.
    return true;
  }
  if (ec.value()) {
    Warning("Failed to open remapping configuration file %s - %s", path, strerror(ec.value()));
    return false;
  }

  Dbg(dbg_ctl_url_rewrite, "[BuildTable] UrlRewrite::BuildTable()");

  ACLBehaviorPolicy behavior_policy = ACLBehaviorPolicy::ACL_BEHAVIOR_LEGACY;
  if (!UrlRewrite::get_acl_behavior_policy(behavior_policy)) {
    Warning("Failed to get ACL matching policy.");
    return false;
  }
  bti->behavior_policy = behavior_policy;

  for (cur_line = tokLine(content.data(), &tok_state, '\\'); cur_line != nullptr;) {
    reg_map      = nullptr;
    new_mapping  = nullptr;
    errStrBuf[0] = 0;
    bti->reset();

    // Strip leading whitespace
    while (*cur_line && isascii(*cur_line) && isspace(*cur_line)) {
      ++cur_line;
    }

    if ((cur_line_size = strlen(cur_line)) <= 0) {
      cur_line = tokLine(nullptr, &tok_state, '\\');
      ++cln;
      continue;
    }

    // Strip trailing whitespace
    cur_line_tmp = cur_line + cur_line_size - 1;
    while (cur_line_tmp != cur_line && isascii(*cur_line_tmp) && isspace(*cur_line_tmp)) {
      *cur_line_tmp = '\0';
      --cur_line_tmp;
    }

    if (strlen(cur_line) <= 0 || *cur_line == '#' || *cur_line == '\0') {
      cur_line = tokLine(nullptr, &tok_state, '\\');
      ++cln;
      continue;
    }

    Dbg(dbg_ctl_url_rewrite, "[BuildTable] Parsing: \"%s\"", cur_line);

    tok_count = whiteTok.Initialize(cur_line, (SHARE_TOKS | ALLOW_SPACES));

    for (int j = 0; j < tok_count; j++) {
      if ((const_cast<char *>(whiteTok[j]))[0] == '@') {
        if ((const_cast<char *>(whiteTok[j]))[1]) {
          bti->argv[bti->argc++] = ats_strdup(&(((char *)whiteTok[j])[1]));
        }
      } else {
        bti->paramv[bti->paramc++] = ats_strdup((char *)whiteTok[j]);
      }
    }

    // Initial verification for number of arguments
    if (bti->paramc < 1 || (bti->paramc < 3 && bti->paramv[0][0] != '.') || bti->paramc > BUILD_TABLE_MAX_ARGS) {
      snprintf(errStrBuf, sizeof(errStrBuf), "malformed line %d in file %s", cln + 1, path);
      errStr = errStrBuf;
      goto MAP_ERROR;
    }
    // just check all major flags/optional arguments
    bti->remap_optflg = remap_check_option(bti->argv, bti->argc);

    // Check directive keywords (starting from '.')
    if (bti->paramv[0][0] == '.') {
      if ((errStr = remap_parse_directive(bti, errBuf, sizeof(errBuf))) != nullptr) {
        snprintf(errStrBuf, sizeof(errStrBuf), "error on line %d - %s", cln + 1, errStr);
        errStr = errStrBuf;
        goto MAP_ERROR;
      }
      // We skip the rest of the parsing here.
      cur_line = tokLine(nullptr, &tok_state, '\\');
      ++cln;
      continue;
    }

    is_cur_mapping_regex = (strncasecmp("regex_", bti->paramv[0], 6) == 0);
    type_id_str          = is_cur_mapping_regex ? (bti->paramv[0] + 6) : bti->paramv[0];

    // Check to see whether is a reverse or forward mapping
    if (!strcasecmp("reverse_map", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - mapping_type::REVERSE_MAP");
      maptype = mapping_type::REVERSE_MAP;
    } else if (!strcasecmp("map", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - %s",
          ((bti->remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? "mapping_type::FORWARD_MAP" :
                                                                       "mapping_type::FORWARD_MAP_REFERER");
      maptype =
        ((bti->remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? mapping_type::FORWARD_MAP : mapping_type::FORWARD_MAP_REFERER;
    } else if (!strcasecmp("redirect", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - mapping_type::PERMANENT_REDIRECT");
      maptype = mapping_type::PERMANENT_REDIRECT;
    } else if (!strcasecmp("redirect_temporary", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - mapping_type::TEMPORARY_REDIRECT");
      maptype = mapping_type::TEMPORARY_REDIRECT;
    } else if (!strcasecmp("map_with_referer", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - mapping_type::FORWARD_MAP_REFERER");
      maptype = mapping_type::FORWARD_MAP_REFERER;
    } else if (!strcasecmp("map_with_recv_port", type_id_str)) {
      Dbg(dbg_ctl_url_rewrite, "[BuildTable] - mapping_type::FORWARD_MAP_WITH_RECV_PORT");
      maptype = mapping_type::FORWARD_MAP_WITH_RECV_PORT;
    } else {
      snprintf(errStrBuf, sizeof(errStrBuf), "unknown mapping type at line %d", cln + 1);
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    new_mapping = new url_mapping();

    // apply filter rules if we have to
    if (process_filter_opt(new_mapping, bti, errStrBuf, sizeof(errStrBuf)) != nullptr) {
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    // update sticky flag
    bti->accept_check_p = bti->accept_check_p && bti->ip_allow_check_enabled_p;

    new_mapping->map_id = 0;
    if ((bti->remap_optflg & REMAP_OPTFLG_MAP_ID) != 0) {
      int idx = 0;
      int ret = remap_check_option(bti->argv, bti->argc, REMAP_OPTFLG_MAP_ID, &idx);
      if (ret & REMAP_OPTFLG_MAP_ID) {
        char *c             = strchr(bti->argv[idx], static_cast<int>('='));
        new_mapping->map_id = static_cast<unsigned int>(atoi(++c));
      }
    }

    map_from = bti->paramv[1];
    length   = UrlWhack(map_from, &origLength);

    // FIX --- what does this comment mean?
    //
    // URL::create modified map_from so keep a point to
    //   the beginning of the string
    if ((tmp = (map_from_start = map_from)) != nullptr && length > 2 && tmp[length - 1] == '/' && tmp[length - 2] == '/') {
      new_mapping->unique  = true;
      length              -= 2;
    }

    new_mapping->fromURL.create(nullptr);
    rparse = new_mapping->fromURL.parse_regex(tmp, length);

    map_from_start[origLength] = '\0'; // Unwhack

    if (rparse != ParseResult::DONE) {
      snprintf(errStrBuf, sizeof(errStrBuf), "malformed From URL: %.*s", length, tmp);
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    map_to       = bti->paramv[2];
    length       = UrlWhack(map_to, &origLength);
    map_to_start = map_to;
    tmp          = map_to;

    new_mapping->toURL.create(nullptr);
    rparse                   = new_mapping->toURL.parse_no_host_check(std::string_view(tmp, length));
    map_to_start[origLength] = '\0'; // Unwhack

    if (rparse != ParseResult::DONE) {
      snprintf(errStrBuf, sizeof(errStrBuf), "malformed To URL: %.*s", length, tmp);
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    fromScheme = new_mapping->fromURL.scheme_get();
    // If the rule is "/" or just some other relative path
    //   we need to default the scheme to http
    if (fromScheme.empty()) {
      new_mapping->fromURL.scheme_set(std::string_view{URL_SCHEME_HTTP});
      fromScheme                        = new_mapping->fromURL.scheme_get();
      new_mapping->wildcard_from_scheme = true;
    }
    toScheme = new_mapping->toURL.scheme_get();

    // Include support for HTTPS scheme
    // includes support for FILE scheme
    if ((fromScheme != std::string_view{URL_SCHEME_HTTP} && fromScheme != std::string_view{URL_SCHEME_HTTPS} &&
         fromScheme != std::string_view{URL_SCHEME_FILE} && fromScheme != std::string_view{URL_SCHEME_TUNNEL} &&
         fromScheme != std::string_view{URL_SCHEME_WS} && fromScheme != std::string_view{URL_SCHEME_WSS}) ||
        (toScheme != std::string_view{URL_SCHEME_HTTP} && toScheme != std::string_view{URL_SCHEME_HTTPS} &&
         toScheme != std::string_view{URL_SCHEME_TUNNEL} && toScheme != std::string_view{URL_SCHEME_WS} &&
         toScheme != std::string_view{URL_SCHEME_WSS})) {
      errStr = "only http, https, ws, wss, and tunnel remappings are supported";
      goto MAP_ERROR;
    }

    // If mapping from WS or WSS we must map out to WS or WSS
    if ((fromScheme == std::string_view{URL_SCHEME_WSS} || fromScheme == std::string_view{URL_SCHEME_WS}) &&
        (toScheme != std::string_view{URL_SCHEME_WSS} && toScheme != std::string_view{URL_SCHEME_WS})) {
      errStr = "WS or WSS can only be mapped out to WS or WSS.";
      goto MAP_ERROR;
    }

    // Check if a tag is specified.
    if (bti->paramv[3] != nullptr) {
      if (maptype == mapping_type::FORWARD_MAP_REFERER) {
        new_mapping->filter_redirect_url = ats_strdup(bti->paramv[3]);
        if (!strcasecmp(bti->paramv[3], "<default>") || !strcasecmp(bti->paramv[3], "default") ||
            !strcasecmp(bti->paramv[3], "<default_redirect_url>") || !strcasecmp(bti->paramv[3], "default_redirect_url")) {
          new_mapping->default_redirect_url = true;
        }
        new_mapping->redir_chunk_list = redirect_tag_str::parse_format_redirect_url(bti->paramv[3]);
        for (int j = bti->paramc; j > 4; j--) {
          if (bti->paramv[j - 1] != nullptr) {
            char refinfo_error_buf[1024];
            bool refinfo_error = false;

            ri = new referer_info(bti->paramv[j - 1], &refinfo_error, refinfo_error_buf, sizeof(refinfo_error_buf));
            if (refinfo_error) {
              snprintf(errStrBuf, sizeof(errStrBuf), "%s Incorrect Referer regular expression \"%s\" at line %d - %s", modulePrefix,
                       bti->paramv[j - 1], cln + 1, refinfo_error_buf);
              Error("%s", errStrBuf);
              delete ri;
              ri = nullptr;
            }

            if (ri && ri->negative) {
              if (ri->any) {
                new_mapping->optional_referer = true; /* referer header is optional */
                delete ri;
                ri = nullptr;
              } else {
                new_mapping->negative_referer = true; /* we have negative referer in list */
              }
            }
            if (ri) {
              ri->next                  = new_mapping->referer_list;
              new_mapping->referer_list = ri;
            }
          }
        }
      } else {
        new_mapping->tag = ats_strdup(&(bti->paramv[3][0]));
      }
    }

    // Check to see the fromHost remapping is a relative one
    fromHost = new_mapping->fromURL.host_get();
    if (fromHost.empty()) {
      if (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
          maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT) {
        if (*map_from_start != '/') {
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

    reg_map = nullptr;
    if (is_cur_mapping_regex) {
      reg_map = new UrlRewrite::RegexMapping();
      if (!process_regex_mapping_config(fromHost_lower, new_mapping, reg_map)) {
        errStr = "could not process regex mapping config line";
        goto MAP_ERROR;
      }
      Dbg(dbg_ctl_url_rewrite_regex, "Configured regex rule for host [%s]", fromHost_lower);
    }

    // If a TS receives a request on a port which is set to tunnel mode
    // (ie, blind forwarding) and a client connects directly to the TS,
    // then the TS will use its IPv4 address and remap rules given
    // to send the request to its proper destination.
    // See HttpTransact::HandleBlindTunnel().
    // Therefore, for a remap rule like "map tunnel://hostname..."
    // in remap.config, we also needs to convert hostname to its IPv4 addr
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

            if (bti->paramv[3] != nullptr) {
              u_mapping->tag = ats_strdup(&(bti->paramv[3][0]));
            }

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
    if ((bti->remap_optflg & REMAP_OPTFLG_STRATEGY) != 0 &&
        (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
         maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT)) {
      const char *strategy = strchr(bti->argv[0], static_cast<int>('='));
      if (strategy == nullptr) {
        errStr = "missing 'strategy' name argument, unable to add mapping rule";
        goto MAP_ERROR;
      } else {
        strategy++;
        new_mapping->strategy = bti->rewrite->strategyFactory->strategyInstance(strategy);
        if (!new_mapping->strategy) {
          snprintf(errStrBuf, sizeof(errStrBuf), "no strategy named '%s' is defined in the config", strategy);
          errStr = errStrBuf;
          goto MAP_ERROR;
        }
        Dbg(dbg_ctl_url_rewrite_regex, "mapped the 'strategy' named %s", strategy);
      }
    }

    // Check "remap" plugin options and load .so object
    if ((bti->remap_optflg & REMAP_OPTFLG_PLUGIN) != 0 &&
        (maptype == mapping_type::FORWARD_MAP || maptype == mapping_type::FORWARD_MAP_REFERER ||
         maptype == mapping_type::FORWARD_MAP_WITH_RECV_PORT)) {
      if ((remap_check_option(bti->argv, bti->argc, REMAP_OPTFLG_PLUGIN, &tok_count) & REMAP_OPTFLG_PLUGIN) != 0) {
        int plugin_found_at = 0;
        int jump_to_argc    = 0;

        // this loads the first plugin
        if (!remap_load_plugin(bti->argv, bti->argc, new_mapping, errStrBuf, sizeof(errStrBuf), 0, &plugin_found_at,
                               bti->rewrite)) {
          Dbg(dbg_ctl_remap_plugin, "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
          errStr = errStrBuf;
          goto MAP_ERROR;
        }
        // this loads any subsequent plugins (if present)
        while (plugin_found_at) {
          jump_to_argc += plugin_found_at;
          if (!remap_load_plugin(bti->argv, bti->argc, new_mapping, errStrBuf, sizeof(errStrBuf), jump_to_argc, &plugin_found_at,
                                 bti->rewrite)) {
            Dbg(dbg_ctl_remap_plugin, "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
            errStr = errStrBuf;
            goto MAP_ERROR;
          }
        }
      }
    }

    // Now add the mapping to appropriate container
    if (!bti->rewrite->InsertMapping(maptype, new_mapping, reg_map, fromHost_lower, is_cur_mapping_regex)) {
      errStr = "unable to add mapping rule to lookup table";
      goto MAP_ERROR;
    }

    fromHost_lower_ptr = static_cast<char *>(ats_free_null(fromHost_lower_ptr));

    cur_line = tokLine(nullptr, &tok_state, '\\');
    ++cln;
    continue;

  // Deal with error / warning scenarios
  MAP_ERROR:

    snprintf(errBuf, sizeof(errBuf), "%s failed to add remap rule at %s line %d: %s", modulePrefix, path, cln + 1, errStr);
    Error("%s", errBuf);

    delete reg_map;
    delete new_mapping;
    return false;
  } /* end of while(cur_line != nullptr) */

  IpAllow::enableAcceptCheck(bti->accept_check_p);
  return true;
}

bool
remap_parse_config(const char *path, UrlRewrite *rewrite)
{
  BUILD_TABLE_INFO bti;

  /* If this happens to be a config reload, the list of loaded remap plugins is non-empty, and we
   * can signal all these plugins that a reload has begun. */
  rewrite->pluginFactory.indicatePreReload();

  bti.rewrite = rewrite;
  bool status = remap_parse_config_bti(path, &bti);

  /* Now after we parsed the configuration and (re)loaded plugins and plugin instances
   * accordingly notify all plugins that we are done */
  rewrite->pluginFactory.indicatePostReload(status);

  bti.clear_acl_rules_list();

  return status;
}
