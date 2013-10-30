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
#include "libts.h"
#include "HTTP.h"

static bool
is_inkeylist(const char * key, ...)
{
  va_list ap;

  if (unlikely(key == NULL || key[0] == '\0')) {
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
parse_define_directive(const char * directive, BUILD_TABLE_INFO * bti, char * errbuf, size_t errbufsize)
{
  bool flg;
  acl_filter_rule * rp;
  acl_filter_rule ** rpp;
  const char * cstr = NULL;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }
  if (bti->argc < 1) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have filter parameter(s)", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  flg = ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) ? true : false;
  // coverity[alloc_arg]
  if ((cstr = remap_validate_filter_args(&rp, bti->argv, bti->argc, errbuf, errbufsize)) == NULL && rp) {
    if (flg) {                  // new filter - add to list
      Debug("url_rewrite", "[parse_directive] new rule \"%s\" was created", bti->paramv[1]);
      for (rpp = &bti->rules_list; *rpp; rpp = &((*rpp)->next));
      (*rpp = rp)->name(bti->paramv[1]);
    }
    Debug("url_rewrite", "[parse_directive] %d argument(s) were added to rule \"%s\"", bti->argc, bti->paramv[1]);
    rp->add_argv(bti->argc, bti->argv);       // store string arguments for future processing
  }

  return cstr;
}

static const char *
parse_delete_directive(const char * directive, BUILD_TABLE_INFO * bti, char * errbuf, size_t errbufsize)
{
  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  acl_filter_rule::delete_byname(&bti->rules_list, (const char *) bti->paramv[1]);
  return NULL;
}

static const char *
parse_activate_directive(const char * directive, BUILD_TABLE_INFO * bti, char * errbuf, size_t errbufsize)
{
  acl_filter_rule * rp;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) {
    snprintf(errbuf, errbufsize, "Undefined filter \"%s\" in directive \"%s\"", bti->paramv[1], directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  acl_filter_rule::requeue_in_active_list(&bti->rules_list, rp);
  return NULL;
}

static const char *
parse_deactivate_directive(const char * directive, BUILD_TABLE_INFO * bti, char * errbuf, size_t errbufsize)
{
  acl_filter_rule * rp;

  if (bti->paramc < 2) {
    snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) {
    snprintf(errbuf, errbufsize, "Undefined filter \"%s\" in directive \"%s\"", bti->paramv[1], directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }

  acl_filter_rule::requeue_in_passive_list(&bti->rules_list, rp);
  return NULL;
}

struct remap_directive
{
  const char * name;
  const char * (*parser)(const char *, BUILD_TABLE_INFO *, char *, size_t);
};

static const remap_directive directives[] = {

  { ".definefilter", parse_define_directive},
  { ".deffilter", parse_define_directive},
  { ".defflt", parse_define_directive},

  { ".deletefilter", parse_delete_directive},
  { ".delfilter", parse_delete_directive},
  { ".delflt", parse_delete_directive},

  { ".usefilter", parse_activate_directive},
  { ".activefilter", parse_activate_directive},
  { ".activatefilter", parse_activate_directive},

  { ".unusefilter", parse_deactivate_directive},
  { ".deactivatefilter", parse_deactivate_directive},
  { ".unactivefilter", parse_deactivate_directive},
  { ".deuseflt", parse_deactivate_directive},
  { ".unuseflt", parse_deactivate_directive},

};

const char *
remap_parse_directive(BUILD_TABLE_INFO *bti, char * errbuf, size_t errbufsize)
{
  const char * directive = NULL;

  // Check arguments
  if (unlikely(!bti || !errbuf || errbufsize <= 0 || !bti->paramc || (directive = bti->paramv[0]) == NULL)) {
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
  return (const char *) errbuf;
}

const char *
remap_validate_filter_args(acl_filter_rule ** rule_pp, char ** argv, int argc, char * errStrBuf, size_t errStrBufSize)
{
  acl_filter_rule *rule;
  unsigned long ul;
  char *argptr, tmpbuf[1024];
  src_ip_info_t *ipi;
  int i, j, m;
  bool new_rule_flg = false;

  if (!rule_pp) {
    Debug("url_rewrite", "[validate_filter_args] Invalid argument(s)");
    return (const char *) "Invalid argument(s)";
  }

  if (is_debug_tag_set("url_rewrite")) {
    printf("validate_filter_args: ");
    for (i = 0; i < argc; i++)
      printf("\"%s\" ", argv[i]);
    printf("\n");
  }

  if ((rule = *rule_pp) == NULL) {
    rule = NEW(new acl_filter_rule());
    if (unlikely((*rule_pp = rule) == NULL)) {
      Debug("url_rewrite", "[validate_filter_args] Memory allocation error");
      return (const char *) "Memory allocation Error";
    }
    new_rule_flg = true;
    Debug("url_rewrite", "[validate_filter_args] new acl_filter_rule class was created during remap rule processing");
  }

  for (i = 0; i < argc; i++) {
    if ((ul = remap_check_option(&argv[i], 1, 0, NULL, &argptr)) == 0) {
      Debug("url_rewrite", "[validate_filter_args] Unknow remap option - %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Unknown option - \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = NULL;
      }
      return (const char *) errStrBuf;
    }
    if (!argptr || !argptr[0]) {
      Debug("url_rewrite", "[validate_filter_args] Empty argument in %s", argv[i]);
      snprintf(errStrBuf, errStrBufSize, "Empty argument in \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = NULL;
      }
      return (const char *) errStrBuf;
    }

    if (ul & REMAP_OPTFLG_METHOD) {     /* "method=" option */
      if (rule->method_cnt >= ACL_FILTER_MAX_METHODS) {
        Debug("url_rewrite", "[validate_filter_args] Too many \"method=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"method=\" filters!", ACL_FILTER_MAX_METHODS);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char *) errStrBuf;
      }
      // Please remember that the order of hash idx creation is very important and it is defined
      // in HTTP.cc file
      if (!strcasecmp(argptr, "CONNECT"))
        m = HTTP_WKSIDX_CONNECT;
      else if (!strcasecmp(argptr, "DELETE"))
        m = HTTP_WKSIDX_DELETE;
      else if (!strcasecmp(argptr, "GET"))
        m = HTTP_WKSIDX_GET;
      else if (!strcasecmp(argptr, "HEAD"))
        m = HTTP_WKSIDX_HEAD;
      else if (!strcasecmp(argptr, "ICP_QUERY"))
        m = HTTP_WKSIDX_ICP_QUERY;
      else if (!strcasecmp(argptr, "OPTIONS"))
        m = HTTP_WKSIDX_OPTIONS;
      else if (!strcasecmp(argptr, "POST"))
        m = HTTP_WKSIDX_POST;
      else if (!strcasecmp(argptr, "PURGE"))
        m = HTTP_WKSIDX_PURGE;
      else if (!strcasecmp(argptr, "PUT"))
        m = HTTP_WKSIDX_PUT;
      else if (!strcasecmp(argptr, "TRACE"))
        m = HTTP_WKSIDX_TRACE;
      else if (!strcasecmp(argptr, "PUSH"))
        m = HTTP_WKSIDX_PUSH;
      else {
        Debug("url_rewrite", "[validate_filter_args] Unknown method value %s", argptr);
        snprintf(errStrBuf, errStrBufSize, "Unknown method \"%s\"", argptr);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char *) errStrBuf;
      }
      for (j = 0; j < rule->method_cnt; j++) {
        if (rule->method_array[j] == m) {
          m = 0;
          break;                /* we already have it in the list */
        }
      }
      if ((j = m) != 0) {
        j = j - HTTP_WKSIDX_CONNECT;    // get method index
        if (j<0 || j>= ACL_FILTER_MAX_METHODS) {
          Debug("url_rewrite", "[validate_filter_args] Incorrect method index! Method sequence in HTTP.cc is broken");
          snprintf(errStrBuf, errStrBufSize, "Incorrect method index %d", j);
          errStrBuf[errStrBufSize - 1] = 0;
          if (new_rule_flg) {
            delete rule;
            *rule_pp = NULL;
          }
          return (const char *) errStrBuf;
        }
        rule->method_idx[j] = m;
        rule->method_array[rule->method_cnt++] = m;
        rule->method_valid = 1;
      }
    } else if (ul & REMAP_OPTFLG_SRC_IP) {      /* "src_ip=" option */
      if (rule->src_ip_cnt >= ACL_FILTER_MAX_SRC_IP) {
        Debug("url_rewrite", "[validate_filter_args] Too many \"src_ip=\" filters");
        snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"src_ip=\" filters!", ACL_FILTER_MAX_SRC_IP);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char *) errStrBuf;
      }
      ipi = &rule->src_ip_array[rule->src_ip_cnt];
      if (ul & REMAP_OPTFLG_INVERT)
        ipi->invert = true;
      ink_strlcpy(tmpbuf, argptr, sizeof(tmpbuf));
      // important! use copy of argument
      if (ExtractIpRange(tmpbuf, &ipi->start.sa, &ipi->end.sa) != NULL) {
        Debug("url_rewrite", "[validate_filter_args] Unable to parse IP value in %s", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unable to parse IP value in %s", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char*) errStrBuf;
      }
      for (j = 0; j < rule->src_ip_cnt; j++) {
        if (rule->src_ip_array[j].start == ipi->start && rule->src_ip_array[j].end == ipi->end) {
          ipi->reset();
          ipi = NULL;
          break;                /* we have the same src_ip in the list */
        }
      }
      if (ipi) {
        rule->src_ip_cnt++;
        rule->src_ip_valid = 1;
      }
    } else if (ul & REMAP_OPTFLG_ACTION) {      /* "action=" option */
      if (is_inkeylist(argptr, "0", "off", "deny", "disable", NULL)) {
        rule->allow_flag = 0;
      } else if (is_inkeylist(argptr, "1", "on", "allow", "enable", NULL)) {
        rule->allow_flag = 1;
      } else {
        Debug("url_rewrite", "[validate_filter_args] Unknown argument \"%s\"", argv[i]);
        snprintf(errStrBuf, errStrBufSize, "Unknown argument \"%s\"", argv[i]);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char *) errStrBuf;
      }
    }
  }

  if (is_debug_tag_set("url_rewrite"))
    rule->print();

  return NULL;                  /* success */
}

unsigned long
remap_check_option(char *argv[], int argc, unsigned long findmode, int *_ret_idx, char **argptr)
{
  unsigned long ret_flags = 0;
  int idx = 0;

  if (argptr)
    *argptr = NULL;
  if (argv && argc > 0) {
    for (int i = 0; i < argc; i++) {
      if (!strcasecmp(argv[i], "map_with_referer")) {
        if ((findmode & REMAP_OPTFLG_MAP_WITH_REFERER) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_MAP_WITH_REFERER;
      } else if (!strncasecmp(argv[i], "plugin=", 7)) {
        if ((findmode & REMAP_OPTFLG_PLUGIN) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_PLUGIN;
      } else if (!strncasecmp(argv[i], "pparam=", 7)) {
        if ((findmode & REMAP_OPTFLG_PPARAM) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_PPARAM;
      } else if (!strncasecmp(argv[i], "method=", 7)) {
        if ((findmode & REMAP_OPTFLG_METHOD) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_METHOD;
      } else if (!strncasecmp(argv[i], "src_ip=~", 8)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][8];
        ret_flags |= (REMAP_OPTFLG_SRC_IP | REMAP_OPTFLG_INVERT);
      } else if (!strncasecmp(argv[i], "src_ip=", 7)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_SRC_IP;
      } else if (!strncasecmp(argv[i], "action=", 7)) {
        if ((findmode & REMAP_OPTFLG_ACTION) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_ACTION;
      } else if (!strncasecmp(argv[i], "mapid=", 6)) {
        if ((findmode & REMAP_OPTFLG_MAP_ID) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][6];
        ret_flags |= REMAP_OPTFLG_MAP_ID;
      }

      if ((findmode & ret_flags) && !argptr) {
        if (_ret_idx)
          *_ret_idx = idx;
        return ret_flags;
      }

    }
  }
  if (_ret_idx)
    *_ret_idx = idx;
  return ret_flags;
}
