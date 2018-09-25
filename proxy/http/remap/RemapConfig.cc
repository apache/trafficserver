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

#include "RemapConfig.h"
#include "UrlRewrite.h"
#include "ReverseProxy.h"
#include "tscore/I_Layout.h"
#include "HTTP.h"
#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "tscore/ink_cap.h"
#include "tscore/ink_file.h"
#include "tscore/Tokenizer.h"
#include "IPAllow.h"

#define modulePrefix "[ReverseProxy]"

static bool remap_parse_config_bti(const char *path, BUILD_TABLE_INFO *bti);

load_remap_file_func load_remap_file_cb = nullptr;

/**
  Returns the length of the URL.

  Will replace the terminator with a '/' if this is a full URL and
  there are no '/' in it after the the host.  This ensures that class
  URL parses the URL correctly.

*/
static int
UrlWhack(char *toWhack, int *origLength)
{
  int length = strlen(toWhack);
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
    v[i] = (char *)ats_free_null(v[i]);
  }
}

BUILD_TABLE_INFO::BUILD_TABLE_INFO()
  : remap_optflg(0), paramc(0), argc(0), ip_allow_check_enabled_p(true), accept_check_p(true), rules_list(nullptr), rewrite(nullptr)
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

static const char *
process_filter_opt(url_mapping *mp, const BUILD_TABLE_INFO *bti, char *errStrBuf, int errStrBufSize)
{
  acl_filter_rule *rp, **rpp;
  const char *errStr = nullptr;

  if (unlikely(!mp || !bti || !errStrBuf || errStrBufSize <= 0)) {
    Debug("url_rewrite", "[process_filter_opt] Invalid argument(s)");
    return (const char *)"[process_filter_opt] Invalid argument(s)";
  }
  for (rp = bti->rules_list; rp; rp = rp->next) {
    if (rp->active_queue_flag) {
      Debug("url_rewrite", "[process_filter_opt] Add active main filter \"%s\" (argc=%d)",
            rp->filter_name ? rp->filter_name : "<nullptr>", rp->argc);
      for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      if ((errStr = remap_validate_filter_args(rpp, (const char **)rp->argv, rp->argc, errStrBuf, errStrBufSize)) != nullptr) {
        break;
      }
    }
  }
  if (!errStr && (bti->remap_optflg & REMAP_OPTFLG_ALL_FILTERS) != 0) {
    Debug("url_rewrite", "[process_filter_opt] Add per remap filter");
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next)) {
      ;
    }
    errStr = remap_validate_filter_args(rpp, (const char **)bti->argv, bti->argc, errStrBuf, errStrBufSize);
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
  for (unsigned idx = 1; str; idx++) {
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
  bool flg;
  acl_filter_rule *rp;
  acl_filter_rule **rpp;
  const char *cstr = nullptr;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
  }
  if (bti->argc < 1) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have filter parameter(s)", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
  }

  flg = ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *)bti->paramv[1])) == nullptr) ? true : false;
  // coverity[alloc_arg]
  if ((cstr = remap_validate_filter_args(&rp, (const char **)bti->argv, bti->argc, errbuf, errbufsize)) == nullptr && rp) {
    if (flg) { // new filter - add to list
      Debug("url_rewrite", "[parse_directive] new rule \"%s\" was created", bti->paramv[1]);
      for (rpp = &bti->rules_list; *rpp; rpp = &((*rpp)->next)) {
        ;
      }
      (*rpp = rp)->name(bti->paramv[1]);
    }
    Debug("url_rewrite", "[parse_directive] %d argument(s) were added to rule \"%s\"", bti->argc, bti->paramv[1]);
    rp->add_argv(bti->argc, bti->argv); // store string arguments for future processing
  }

  return cstr;
}

static const char *
parse_delete_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
  }

  acl_filter_rule::delete_byname(&bti->rules_list, (const char *)bti->paramv[1]);
  return nullptr;
}

static const char *
parse_activate_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  acl_filter_rule *rp;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
  }

  // Check if for ip_allow filter
  if (strcmp((const char *)bti->paramv[1], "ip_allow") == 0) {
    bti->ip_allow_check_enabled_p = true;
    return nullptr;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *)bti->paramv[1])) == nullptr) {
    snprintf(errbuf, errbufsize, R"(Undefined filter "%s" in directive "%s")", bti->paramv[1], directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
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
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
  }

  // Check if for ip_allow filter
  if (strcmp((const char *)bti->paramv[1], "ip_allow") == 0) {
    bti->ip_allow_check_enabled_p = false;
    return nullptr;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *)bti->paramv[1])) == nullptr) {
    snprintf(errbuf, errbufsize, R"(Undefined filter "%s" in directive "%s")", bti->paramv[1], directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *)errbuf;
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
  bool success;

  if (access(path, R_OK) == -1) {
    snprintf(errbuf, errbufsize, "%s: %s", path, strerror(errno));
    return (const char *)errbuf;
  }

  nbti.rules_list = bti->rules_list;
  nbti.rewrite    = bti->rewrite;

  Debug("url_rewrite", "[%s] including remap configuration from %s", __func__, (const char *)path);
  success = remap_parse_config_bti(path, &nbti);

  // The sub-parse might have updated the rules list, so push it up to the parent parse.
  bti->rules_list = nbti.rules_list;

  if (success) {
    // register the included file with the management subsystem so that we can correctly
    // reload them when they change
    load_remap_file_cb((const char *)path);
  } else {
    snprintf(errbuf, errbufsize, "failed to parse included file %s", path);
    return (const char *)errbuf;
  }

  return nullptr;
}

static const char *
parse_include_directive(const char *directive, BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have a path argument", directive);
    Debug("url_rewrite", "[%s] %s", __func__, errbuf);
    return (const char *)errbuf;
  }

  for (unsigned i = 1; i < (unsigned)bti->paramc; ++i) {
    ats_scoped_str path;
    const char *errmsg = nullptr;

    // The included path is relative to SYSCONFDIR, just like remap.config is.
    path = RecConfigReadConfigPath(nullptr, bti->paramv[i]);

    if (ink_file_is_directory(path)) {
      struct dirent **entrylist;
      int n_entries;

      n_entries = scandir(path, &entrylist, nullptr, alphasort);
      if (n_entries == -1) {
        snprintf(errbuf, errbufsize, "failed to open %s: %s", path.get(), strerror(errno));
        return (const char *)errbuf;
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

  {".definefilter", parse_define_directive},
  {".deffilter", parse_define_directive},
  {".defflt", parse_define_directive},

  {".deletefilter", parse_delete_directive},
  {".delfilter", parse_delete_directive},
  {".delflt", parse_delete_directive},

  {".usefilter", parse_activate_directive},
  {".activefilter", parse_activate_directive},
  {".activatefilter", parse_activate_directive},
  {".useflt", parse_activate_directive},

  {".unusefilter", parse_deactivate_directive},
  {".deactivatefilter", parse_deactivate_directive},
  {".unactivefilter", parse_deactivate_directive},
  {".deuseflt", parse_deactivate_directive},
  {".unuseflt", parse_deactivate_directive},

  {".include", parse_include_directive},
};

const char *
remap_parse_directive(BUILD_TABLE_INFO *bti, char *errbuf, size_t errbufsize)
{
  const char *directive = nullptr;

  // Check arguments
  if (unlikely(!bti || !errbuf || errbufsize <= 0 || !bti->paramc || (directive = bti->paramv[0]) == nullptr)) {
    Debug("url_rewrite", "[parse_directive] Invalid argument(s)");
    return "Invalid argument(s)";
  }

  for (unsigned i = 0; i < countof(directives); ++i) {
    if (strcmp(directive, directives[i].name) == 0) {
      return directives[i].parser(directive, bti, errbuf, errbufsize);
    }
  }

  snprintf(errbuf, errbufsize, "Unknown directive \"%s\"", directive);
  Debug("url_rewrite", "[parse_directive] %s", errbuf);
  return (const char *)errbuf;
}

const char *
remap_validate_filter_args(acl_filter_rule **rule_pp, const char **argv, int argc, char *errStrBuf, size_t errStrBufSize)
{
  acl_filter_rule *rule;
  unsigned long ul;
  const char *argptr;
  src_ip_info_t *ipi;
  int i, j;
  bool new_rule_flg = false;

  if (!rule_pp) {
    Debug("url_rewrite", "[validate_filter_args] Invalid argument(s)");
    return (const char *)"Invalid argument(s)";
  }

  if (is_debug_tag_set("url_rewrite")) {
    printf("validate_filter_args: ");
    for (i = 0; i < argc; i++) {
      printf("\"%s\" ", argv[i]);
    }
    printf("\n");
  }

  if ((rule = *rule_pp) == nullptr) {
    rule = new acl_filter_rule();
    if (unlikely((*rule_pp = rule) == nullptr)) {
      Debug("url_rewrite", "[validate_filter_args] Memory allocation error");
      return (const char *)"Memory allocation Error";
    }
    new_rule_flg = true;
    Debug("url_rewrite", "[validate_filter_args] new acl_filter_rule class was created during remap rule processing");
  }

  for (i = 0; i < argc; i++) {
    bool hasarg;

    if ((ul = remap_check_option((const char **)&argv[i], 1, 0, nullptr, &argptr)) == 0) {
      Debug("url_rewrite", "[validate_filter_args] Unknown remap option - %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Unknown option - \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = nullptr;
      }
      return (const char *)errStrBuf;
    }

    // Every filter operator requires an argument except @internal.
    hasarg = !(ul & REMAP_OPTFLG_INTERNAL);

    if (hasarg && (!argptr || !argptr[0])) {
      Debug("url_rewrite", "[validate_filter_args] Empty argument in %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Empty argument in \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = nullptr;
      }
      return (const char *)errStrBuf;
    }

    if (ul & REMAP_OPTFLG_METHOD) { /* "method=" option */
      // Please remember that the order of hash idx creation is very important and it is defined
      // in HTTP.cc file. 0 in our array is the first method, CONNECT
      int m = hdrtoken_tokenize(argptr, strlen(argptr), nullptr) - HTTP_WKSIDX_CONNECT;

      if (m >= 0 && m < HTTP_WKSIDX_METHODS_CNT) {
        rule->standard_method_lookup[m] = true;
      } else {
        Debug("url_rewrite", "[validate_filter_args] Using nonstandard method [%s]", argptr);
        rule->nonstandard_methods.insert(argptr);
      }
      rule->method_restriction_enabled = true;
    }

    if (ul & REMAP_OPTFLG_SRC_IP) { /* "src_ip=" option */
      if (rule->src_ip_cnt >= ACL_FILTER_MAX_SRC_IP) {
        Debug("url_rewrite", "[validate_filter_args] Too many \"src_ip=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"src_ip=\" filters!", ACL_FILTER_MAX_SRC_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return (const char *)errStrBuf;
      }
      ipi = &rule->src_ip_array[rule->src_ip_cnt];
      if (ul & REMAP_OPTFLG_INVERT) {
        ipi->invert = true;
      }
      if (ats_ip_range_parse(argptr, ipi->start, ipi->end) != 0) {
        Debug("url_rewrite", "[validate_filter_args] Unable to parse IP value in %s", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unable to parse IP value in %s", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return (const char *)errStrBuf;
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

    if (ul & REMAP_OPTFLG_IN_IP) { /* "dst_ip=" option */
      if (rule->in_ip_cnt >= ACL_FILTER_MAX_IN_IP) {
        Debug("url_rewrite", "[validate_filter_args] Too many \"in_ip=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"in_ip=\" filters!", ACL_FILTER_MAX_IN_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return (const char *)errStrBuf;
      }
      ipi = &rule->in_ip_array[rule->in_ip_cnt];
      if (ul & REMAP_OPTFLG_INVERT) {
        ipi->invert = true;
      }
      // important! use copy of argument
      if (ats_ip_range_parse(argptr, ipi->start, ipi->end) != 0) {
        Debug("url_rewrite", "[validate_filter_args] Unable to parse IP value in %s", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unable to parse IP value in %s", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return (const char *)errStrBuf;
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
      if (is_inkeylist(argptr, "0", "off", "deny", "disable", nullptr)) {
        rule->allow_flag = 0;
      } else if (is_inkeylist(argptr, "1", "on", "allow", "enable", nullptr)) {
        rule->allow_flag = 1;
      } else {
        Debug("url_rewrite", "[validate_filter_args] Unknown argument \"%s\"", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unknown argument \"%s\"", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = nullptr;
        }
        return (const char *)errStrBuf;
      }
    }

    if (ul & REMAP_OPTFLG_INTERNAL) {
      rule->internal = 1;
    }
  }

  if (is_debug_tag_set("url_rewrite")) {
    rule->print();
  }

  return nullptr; /* success */
}

unsigned long
remap_check_option(const char **argv, int argc, unsigned long findmode, int *_ret_idx, const char **argptr)
{
  unsigned long ret_flags = 0;
  int idx                 = 0;

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
      } else if (!strncasecmp(argv[i], "src_ip=", 7)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0) {
          idx = i;
        }
        if (argptr) {
          *argptr = &argv[i][7];
        }
        ret_flags |= REMAP_OPTFLG_SRC_IP;
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

int
remap_load_plugin(const char **argv, int argc, url_mapping *mp, char *errbuf, int errbufsize, int jump_to_argc,
                  int *plugin_found_at)
{
  TSRemapInterface ri;
  struct stat stat_buf;
  remap_plugin_info *pi;
  char *c, *err, tmpbuf[2048], default_path[PATH_NAME_MAX];
  const char *new_argv[1024];
  char *parv[1024];
  int idx = 0, retcode = 0;
  int parc = 0;

  *plugin_found_at = 0;

  memset(parv, 0, sizeof(parv));
  memset(new_argv, 0, sizeof(new_argv));
  tmpbuf[0] = 0;

  ink_assert((unsigned)argc < countof(new_argv));

  if (jump_to_argc != 0) {
    argc -= jump_to_argc;
    int i = 0;
    while (argv[i + jump_to_argc]) {
      new_argv[i] = argv[i + jump_to_argc];
      i++;
    }
    argv = &new_argv[0];
    if (!remap_check_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx)) {
      return -1;
    }
  } else {
    if (unlikely(!mp || (remap_check_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx) & REMAP_OPTFLG_PLUGIN) == 0)) {
      snprintf(errbuf, errbufsize, "Can't find remap plugin keyword or \"url_mapping\" is nullptr");
      return -1; /* incorrect input data - almost impossible case */
    }
  }

  if (unlikely((c = (char *)strchr(argv[idx], (int)'=')) == nullptr || !(*(++c)))) {
    snprintf(errbuf, errbufsize, "Can't find remap plugin file name in \"@%s\"", argv[idx]);
    return -2; /* incorrect input data */
  }

  if (stat(c, &stat_buf) != 0) {
    ats_scoped_str plugin_default_path(RecConfigReadPluginDir());

    // Try with the plugin path instead
    if (strlen(c) + strlen(plugin_default_path) > (PATH_NAME_MAX - 1)) {
      Debug("remap_plugin", "way too large a path specified for remap plugin");
      return -3;
    }

    snprintf(default_path, PATH_NAME_MAX, "%s/%s", static_cast<char *>(plugin_default_path), c);
    Debug("remap_plugin", "attempting to stat default plugin path: %s", default_path);

    if (stat(default_path, &stat_buf) == 0) {
      Debug("remap_plugin", "stat successful on %s using that", default_path);
      c = &default_path[0];
    } else {
      snprintf(errbuf, errbufsize, "Can't find remap plugin file \"%s\"", c);
      return -3;
    }
  }

  Debug("remap_plugin", "using path %s for plugin", c);

  if (!remap_pi_list || (pi = remap_pi_list->find_by_path(c)) == nullptr) {
    pi = new remap_plugin_info(c);
    if (!remap_pi_list) {
      remap_pi_list = pi;
    } else {
      remap_pi_list->add_to_list(pi);
    }
    Debug("remap_plugin", "New remap plugin info created for \"%s\"", c);

    {
      uint32_t elevate_access = 0;
      REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
      ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

      if ((pi->dlh = dlopen(c, RTLD_NOW)) == nullptr) {
#if defined(freebsd) || defined(openbsd)
        err = (char *)dlerror();
#else
        err = dlerror();
#endif
        snprintf(errbuf, errbufsize, "Can't load plugin \"%s\" - %s", c, err ? err : "Unknown dlopen() error");
        return -4;
      }
      pi->fp_tsremap_init = reinterpret_cast<remap_plugin_info::_tsremap_init *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_INIT));
      pi->fp_tsremap_config_reload =
        reinterpret_cast<remap_plugin_info::_tsremap_config_reload *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_CONFIG_RELOAD));
      pi->fp_tsremap_done = reinterpret_cast<remap_plugin_info::_tsremap_done *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_DONE));
      pi->fp_tsremap_new_instance =
        reinterpret_cast<remap_plugin_info::_tsremap_new_instance *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_NEW_INSTANCE));
      pi->fp_tsremap_delete_instance =
        reinterpret_cast<remap_plugin_info::_tsremap_delete_instance *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_DELETE_INSTANCE));
      pi->fp_tsremap_do_remap = reinterpret_cast<remap_plugin_info::_tsremap_do_remap *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_DO_REMAP));
      pi->fp_tsremap_os_response =
        reinterpret_cast<remap_plugin_info::_tsremap_os_response *>(dlsym(pi->dlh, TSREMAP_FUNCNAME_OS_RESPONSE));

      if (!pi->fp_tsremap_init) {
        snprintf(errbuf, errbufsize, R"(Can't find "%s" function in remap plugin "%s")", TSREMAP_FUNCNAME_INIT, c);
        retcode = -10;
      } else if (!pi->fp_tsremap_new_instance && pi->fp_tsremap_delete_instance) {
        snprintf(errbuf, errbufsize,
                 R"(Can't find "%s" function in remap plugin "%s" which is required if "%s" function exists)",
                 TSREMAP_FUNCNAME_NEW_INSTANCE, c, TSREMAP_FUNCNAME_DELETE_INSTANCE);
        retcode = -11;
      } else if (!pi->fp_tsremap_do_remap) {
        snprintf(errbuf, errbufsize, R"(Can't find "%s" function in remap plugin "%s")", TSREMAP_FUNCNAME_DO_REMAP, c);
        retcode = -12;
      } else if (pi->fp_tsremap_new_instance && !pi->fp_tsremap_delete_instance) {
        snprintf(errbuf, errbufsize,
                 R"(Can't find "%s" function in remap plugin "%s" which is required if "%s" function exists)",
                 TSREMAP_FUNCNAME_DELETE_INSTANCE, c, TSREMAP_FUNCNAME_NEW_INSTANCE);
        retcode = -13;
      }
      if (retcode) {
        if (errbuf && errbufsize > 0) {
          Debug("remap_plugin", "%s", errbuf);
        }
        dlclose(pi->dlh);
        pi->dlh = nullptr;
        return retcode;
      }
      memset(&ri, 0, sizeof(ri));
      ri.size            = sizeof(ri);
      ri.tsremap_version = TSREMAP_VERSION;

      if (pi->fp_tsremap_init(&ri, tmpbuf, sizeof(tmpbuf) - 1) != TS_SUCCESS) {
        snprintf(errbuf, errbufsize, "Failed to initialize plugin \"%s\": %s", pi->path,
                 tmpbuf[0] ? tmpbuf : "Unknown plugin error");
        return -5;
      }
    } // done elevating access
    Debug("remap_plugin", "Remap plugin \"%s\" - initialization completed", c);
  }

  if (!pi->dlh) {
    snprintf(errbuf, errbufsize, "Can't load plugin \"%s\"", c);
    return -6;
  }

  if ((err = mp->fromURL.string_get(nullptr)) == nullptr) {
    snprintf(errbuf, errbufsize, "Can't load fromURL from URL class");
    return -7;
  }
  parv[parc++] = ats_strdup(err);
  ats_free(err);

  if ((err = mp->toUrl.string_get(nullptr)) == nullptr) {
    snprintf(errbuf, errbufsize, "Can't load toURL from URL class");
    return -7;
  }
  parv[parc++] = ats_strdup(err);
  ats_free(err);

  bool plugin_encountered = false;
  // how many plugin parameters we have for this remapping
  for (idx = 0; idx < argc && parc < (int)(countof(parv) - 1); idx++) {
    if (plugin_encountered && !strncasecmp("plugin=", argv[idx], 7) && argv[idx][7]) {
      *plugin_found_at = idx;
      break; // if there is another plugin, lets deal with that later
    }

    if (!strncasecmp("plugin=", argv[idx], 7)) {
      plugin_encountered = true;
    }

    if (!strncasecmp("pparam=", argv[idx], 7) && argv[idx][7]) {
      parv[parc++] = const_cast<char *>(&(argv[idx][7]));
    }
  }

  Debug("url_rewrite", "Viewing all parameters for config line");
  for (int k = 0; k < argc; k++) {
    Debug("url_rewrite", "Argument %d: %s", k, argv[k]);
  }

  Debug("url_rewrite", "Viewing parsed plugin parameters for %s: [%d]", pi->path, *plugin_found_at);
  for (int k = 0; k < parc; k++) {
    Debug("url_rewrite", "Argument %d: %s", k, parv[k]);
  }

  Debug("remap_plugin", "creating new plugin instance");

  void *ih         = nullptr;
  TSReturnCode res = TS_SUCCESS;
  if (pi->fp_tsremap_new_instance) {
#if (!defined(kfreebsd) && defined(freebsd)) || defined(darwin)
    optreset = 1;
#endif
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#endif
    opterr = 0;
    optarg = nullptr;

    res = pi->fp_tsremap_new_instance(parc, parv, &ih, tmpbuf, sizeof(tmpbuf) - 1);
  }

  Debug("remap_plugin", "done creating new plugin instance");

  ats_free(parv[0]); // fromURL
  ats_free(parv[1]); // toURL

  if (res != TS_SUCCESS) {
    snprintf(errbuf, errbufsize, "Failed to create instance for plugin \"%s\": %s", c, tmpbuf[0] ? tmpbuf : "Unknown plugin error");
    return -8;
  }

  mp->add_plugin(pi, ih);

  return 0;
}
/** will process the regex mapping configuration and create objects in
    output argument reg_map. It assumes existing data in reg_map is
    inconsequential and will be perfunctorily null-ed;
*/
static bool
process_regex_mapping_config(const char *from_host_lower, url_mapping *new_mapping, UrlRewrite::RegexMapping *reg_map)
{
  const char *str;
  int str_index;
  const char *to_host;
  int to_host_len;
  int substitution_id;
  int substitution_count = 0;
  int captures;

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

  to_host = new_mapping->toUrl.host_get(&to_host_len);
  for (int i = 0; i < (to_host_len - 1); ++i) {
    if (to_host[i] == '$') {
      if (substitution_count > UrlRewrite::MAX_REGEX_SUBS) {
        Warning("Cannot have more than %d substitutions in mapping with host [%s]", UrlRewrite::MAX_REGEX_SUBS, from_host_lower);
        goto lFail;
      }
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
  str                               = new_mapping->toUrl.host_get(&str_index); // reusing str and str_index
  reg_map->to_url_host_template_len = str_index;
  reg_map->to_url_host_template     = static_cast<char *>(ats_malloc(str_index));
  memcpy(reg_map->to_url_host_template, str, str_index);

  return true;

lFail:
  if (reg_map->to_url_host_template) {
    ats_free(reg_map->to_url_host_template);
    reg_map->to_url_host_template     = nullptr;
    reg_map->to_url_host_template_len = 0;
  }
  return false;
}

static bool
remap_parse_config_bti(const char *path, BUILD_TABLE_INFO *bti)
{
  char errBuf[1024];
  char errStrBuf[1024];
  const char *errStr;

  Tokenizer whiteTok(" \t");
  bool alarm_already = false;

  // Vars to parse line in file
  char *tok_state, *cur_line, *cur_line_tmp;
  int rparse, cur_line_size, cln = 0; // Our current line number

  // Vars to build the mapping
  const char *fromScheme, *toScheme;
  int fromSchemeLen, toSchemeLen;
  const char *fromHost, *toHost;
  int fromHostLen, toHostLen;
  char *map_from, *map_from_start;
  char *map_to, *map_to_start;
  const char *tmp; // Appease the DEC compiler
  char *fromHost_lower     = nullptr;
  char *fromHost_lower_ptr = nullptr;
  char fromHost_lower_buf[1024];
  url_mapping *new_mapping = nullptr;
  mapping_type maptype;
  referer_info *ri;
  int origLength;
  int length;
  int tok_count;

  UrlRewrite::RegexMapping *reg_map;
  bool is_cur_mapping_regex;
  const char *type_id_str;

  ats_scoped_str file_buf(readIntoBuffer(path, modulePrefix, nullptr));
  if (!file_buf) {
    Warning("can't load remapping configuration file %s", path);
    return false;
  }

  Debug("url_rewrite", "[BuildTable] UrlRewrite::BuildTable()");

  for (cur_line = tokLine(file_buf, &tok_state, '\\'); cur_line != nullptr;) {
    reg_map      = nullptr;
    new_mapping  = nullptr;
    errStrBuf[0] = 0;
    bti->reset();

    // Strip leading whitespace
    while (*cur_line && isascii(*cur_line) && isspace(*cur_line)) {
      ++cur_line;
    }

    if ((cur_line_size = strlen((char *)cur_line)) <= 0) {
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

    if ((cur_line_size = strlen((char *)cur_line)) <= 0 || *cur_line == '#' || *cur_line == '\0') {
      cur_line = tokLine(nullptr, &tok_state, '\\');
      ++cln;
      continue;
    }

    Debug("url_rewrite", "[BuildTable] Parsing: \"%s\"", cur_line);

    tok_count = whiteTok.Initialize(cur_line, (SHARE_TOKS | ALLOW_SPACES));

    for (int j = 0; j < tok_count; j++) {
      if (((char *)whiteTok[j])[0] == '@') {
        if (((char *)whiteTok[j])[1]) {
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
    bti->remap_optflg = remap_check_option((const char **)bti->argv, bti->argc);

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
      Debug("url_rewrite", "[BuildTable] - REVERSE_MAP");
      maptype = REVERSE_MAP;
    } else if (!strcasecmp("map", type_id_str)) {
      Debug("url_rewrite", "[BuildTable] - %s",
            ((bti->remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? "FORWARD_MAP" : "FORWARD_MAP_REFERER");
      maptype = ((bti->remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? FORWARD_MAP : FORWARD_MAP_REFERER;
    } else if (!strcasecmp("redirect", type_id_str)) {
      Debug("url_rewrite", "[BuildTable] - PERMANENT_REDIRECT");
      maptype = PERMANENT_REDIRECT;
    } else if (!strcasecmp("redirect_temporary", type_id_str)) {
      Debug("url_rewrite", "[BuildTable] - TEMPORARY_REDIRECT");
      maptype = TEMPORARY_REDIRECT;
    } else if (!strcasecmp("map_with_referer", type_id_str)) {
      Debug("url_rewrite", "[BuildTable] - FORWARD_MAP_REFERER");
      maptype = FORWARD_MAP_REFERER;
    } else if (!strcasecmp("map_with_recv_port", type_id_str)) {
      Debug("url_rewrite", "[BuildTable] - FORWARD_MAP_WITH_RECV_PORT");
      maptype = FORWARD_MAP_WITH_RECV_PORT;
    } else {
      snprintf(errStrBuf, sizeof(errStrBuf), "unknown mapping type at line %d", cln + 1);
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    new_mapping = new url_mapping();

    // apply filter rules if we have to
    if ((errStr = process_filter_opt(new_mapping, bti, errStrBuf, sizeof(errStrBuf))) != nullptr) {
      errStr = errStrBuf;
      goto MAP_ERROR;
    }

    // update sticky flag
    bti->accept_check_p = bti->accept_check_p && bti->ip_allow_check_enabled_p;

    new_mapping->map_id = 0;
    if ((bti->remap_optflg & REMAP_OPTFLG_MAP_ID) != 0) {
      int idx = 0;
      char *c;
      int ret = remap_check_option((const char **)bti->argv, bti->argc, REMAP_OPTFLG_MAP_ID, &idx);
      if (ret & REMAP_OPTFLG_MAP_ID) {
        c                   = strchr(bti->argv[idx], (int)'=');
        new_mapping->map_id = (unsigned int)atoi(++c);
      }
    }

    map_from = bti->paramv[1];
    length   = UrlWhack(map_from, &origLength);

    // FIX --- what does this comment mean?
    //
    // URL::create modified map_from so keep a point to
    //   the beginning of the string
    if ((tmp = (map_from_start = map_from)) != nullptr && length > 2 && tmp[length - 1] == '/' && tmp[length - 2] == '/') {
      new_mapping->unique = true;
      length -= 2;
    }

    new_mapping->fromURL.create(nullptr);
    rparse = new_mapping->fromURL.parse_no_path_component_breakdown(tmp, length);

    map_from_start[origLength] = '\0'; // Unwhack

    if (rparse != PARSE_RESULT_DONE) {
      errStr = "malformed From URL";
      goto MAP_ERROR;
    }

    map_to       = bti->paramv[2];
    length       = UrlWhack(map_to, &origLength);
    map_to_start = map_to;
    tmp          = map_to;

    new_mapping->toUrl.create(nullptr);
    rparse                   = new_mapping->toUrl.parse_no_path_component_breakdown(tmp, length);
    map_to_start[origLength] = '\0'; // Unwhack

    if (rparse != PARSE_RESULT_DONE) {
      errStr = "malformed To URL";
      goto MAP_ERROR;
    }

    fromScheme = new_mapping->fromURL.scheme_get(&fromSchemeLen);
    // If the rule is "/" or just some other relative path
    //   we need to default the scheme to http
    if (fromScheme == nullptr || fromSchemeLen == 0) {
      new_mapping->fromURL.scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
      fromScheme                        = new_mapping->fromURL.scheme_get(&fromSchemeLen);
      new_mapping->wildcard_from_scheme = true;
    }
    toScheme = new_mapping->toUrl.scheme_get(&toSchemeLen);

    // Include support for HTTPS scheme
    // includes support for FILE scheme
    if ((fromScheme != URL_SCHEME_HTTP && fromScheme != URL_SCHEME_HTTPS && fromScheme != URL_SCHEME_FILE &&
         fromScheme != URL_SCHEME_TUNNEL && fromScheme != URL_SCHEME_WS && fromScheme != URL_SCHEME_WSS) ||
        (toScheme != URL_SCHEME_HTTP && toScheme != URL_SCHEME_HTTPS && toScheme != URL_SCHEME_TUNNEL &&
         toScheme != URL_SCHEME_WS && toScheme != URL_SCHEME_WSS)) {
      errStr = "only http, https, ws, wss, and tunnel remappings are supported";
      goto MAP_ERROR;
    }

    // If mapping from WS or WSS we must map out to WS or WSS
    if ((fromScheme == URL_SCHEME_WSS || fromScheme == URL_SCHEME_WS) &&
        (toScheme != URL_SCHEME_WSS && toScheme != URL_SCHEME_WS)) {
      errStr = "WS or WSS can only be mapped out to WS or WSS.";
      goto MAP_ERROR;
    }

    // Check if a tag is specified.
    if (bti->paramv[3] != nullptr) {
      if (maptype == FORWARD_MAP_REFERER) {
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

            ri = new referer_info((char *)bti->paramv[j - 1], &refinfo_error, refinfo_error_buf, sizeof(refinfo_error_buf));
            if (refinfo_error) {
              snprintf(errStrBuf, sizeof(errStrBuf), "%s Incorrect Referer regular expression \"%s\" at line %d - %s", modulePrefix,
                       bti->paramv[j - 1], cln + 1, refinfo_error_buf);
              SignalError(errStrBuf, alarm_already);
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
    fromHost = new_mapping->fromURL.host_get(&fromHostLen);
    if (fromHost == nullptr || fromHostLen <= 0) {
      if (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT) {
        if (*map_from_start != '/') {
          errStr = "relative remappings must begin with a /";
          goto MAP_ERROR;
        } else {
          fromHost    = "";
          fromHostLen = 0;
        }
      } else {
        errStr = "remap source in reverse mappings requires a hostname";
        goto MAP_ERROR;
      }
    }

    toHost = new_mapping->toUrl.host_get(&toHostLen);
    if (toHost == nullptr || toHostLen <= 0) {
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

    if (unlikely(fromHostLen >= (int)sizeof(fromHost_lower_buf))) {
      fromHost_lower = (fromHost_lower_ptr = (char *)ats_malloc(fromHostLen + 1));
    } else {
      fromHost_lower = &fromHost_lower_buf[0];
    }
    // Canonicalize the hostname by making it lower case
    memcpy(fromHost_lower, fromHost, fromHostLen);
    fromHost_lower[fromHostLen] = 0;
    LowerCaseStr(fromHost_lower);

    // set the normalized string so nobody else has to normalize this
    new_mapping->fromURL.host_set(fromHost_lower, fromHostLen);

    reg_map = nullptr;
    if (is_cur_mapping_regex) {
      reg_map = new UrlRewrite::RegexMapping();
      if (!process_regex_mapping_config(fromHost_lower, new_mapping, reg_map)) {
        errStr = "could not process regex mapping config line";
        goto MAP_ERROR;
      }
      Debug("url_rewrite_regex", "Configured regex rule for host [%s]", fromHost_lower);
    }

    // If a TS receives a request on a port which is set to tunnel mode
    // (ie, blind forwarding) and a client connects directly to the TS,
    // then the TS will use its IPv4 address and remap rules given
    // to send the request to its proper destination.
    // See HttpTransact::HandleBlindTunnel().
    // Therefore, for a remap rule like "map tunnel://hostname..."
    // in remap.config, we also needs to convert hostname to its IPv4 addr
    // and gives a new remap rule with the IPv4 addr.
    if ((maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT) &&
        fromScheme == URL_SCHEME_TUNNEL && (fromHost_lower[0] < '0' || fromHost_lower[0] > '9')) {
      addrinfo *ai_records; // returned records.
      ip_text_buffer ipb;   // buffer for address string conversion.
      if (0 == getaddrinfo(fromHost_lower, nullptr, nullptr, &ai_records)) {
        for (addrinfo *ai_spot = ai_records; ai_spot; ai_spot = ai_spot->ai_next) {
          if (ats_is_ip(ai_spot->ai_addr) && !ats_is_ip_any(ai_spot->ai_addr) && ai_spot->ai_protocol == IPPROTO_TCP) {
            url_mapping *u_mapping;

            ats_ip_ntop(ai_spot->ai_addr, ipb, sizeof ipb);
            u_mapping = new url_mapping;
            u_mapping->fromURL.create(nullptr);
            u_mapping->fromURL.copy(&new_mapping->fromURL);
            u_mapping->fromURL.host_set(ipb, strlen(ipb));
            u_mapping->toUrl.create(nullptr);
            u_mapping->toUrl.copy(&new_mapping->toUrl);

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

    // Check "remap" plugin options and load .so object
    if ((bti->remap_optflg & REMAP_OPTFLG_PLUGIN) != 0 &&
        (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT)) {
      if ((remap_check_option((const char **)bti->argv, bti->argc, REMAP_OPTFLG_PLUGIN, &tok_count) & REMAP_OPTFLG_PLUGIN) != 0) {
        int plugin_found_at = 0;
        int jump_to_argc    = 0;

        // this loads the first plugin
        if (remap_load_plugin((const char **)bti->argv, bti->argc, new_mapping, errStrBuf, sizeof(errStrBuf), 0,
                              &plugin_found_at)) {
          Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
          errStr = errStrBuf;
          goto MAP_ERROR;
        }
        // this loads any subsequent plugins (if present)
        while (plugin_found_at) {
          jump_to_argc += plugin_found_at;
          if (remap_load_plugin((const char **)bti->argv, bti->argc, new_mapping, errStrBuf, sizeof(errStrBuf), jump_to_argc,
                                &plugin_found_at)) {
            Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
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

    fromHost_lower_ptr = (char *)ats_free_null(fromHost_lower_ptr);

    cur_line = tokLine(nullptr, &tok_state, '\\');
    ++cln;
    continue;

  // Deal with error / warning scenarios
  MAP_ERROR:

    snprintf(errBuf, sizeof(errBuf), "%s failed to add remap rule at %s line %d: %s", modulePrefix, path, cln + 1, errStr);
    SignalError(errBuf, alarm_already);

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

  // If this happens to be a config reload, the list of loaded remap plugins is non-empty, and we
  // can signal all these plugins that a reload has begun.
  if (remap_pi_list) {
    remap_pi_list->indicate_reload();
  }
  bti.rewrite = rewrite;
  return remap_parse_config_bti(path, &bti);
}
