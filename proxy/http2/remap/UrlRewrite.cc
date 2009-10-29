/** @file

  A brief file description

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

#include "UrlRewrite.h"
#include "Main.h"
#include "Error.h"
#include "P_EventSystem.h"
#include "StatSystem.h"
#include "P_Cache.h"
#include "Config.h"
#include "ReverseProxy.h"
#include "MatcherUtils.h"
#include "Tokenizer.h"
#include "RemapAPI.h"

#include "ink_string.h"


//
//  Moved from util.cc...
//
unsigned long
check_remap_option(char *argv[], int argc, unsigned long findmode = 0, int *_ret_idx = NULL, char **argptr = NULL)
{
  unsigned long ret_flags = 0;
  int idx = 0;

  if (argptr)
    *argptr = NULL;
  if (argv && argc > 0) {
    for (int i = 0; i < argc; i++) {
      if (!ink_string_fast_strcasecmp(argv[i], "map_with_referer")) {
        if ((findmode & REMAP_OPTFLG_MAP_WITH_REFERER) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_MAP_WITH_REFERER;
      } else if (!ink_string_fast_strncasecmp(argv[i], "plugin=", 7)) {
        if ((findmode & REMAP_OPTFLG_PLUGIN) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_PLUGIN;
      } else if (!ink_string_fast_strncasecmp(argv[i], "pparam=", 7)) {
        if ((findmode & REMAP_OPTFLG_PPARAM) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_PPARAM;
      } else if (!ink_string_fast_strncasecmp(argv[i], "method=", 7)) {
        if ((findmode & REMAP_OPTFLG_METHOD) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_METHOD;
      } else if (!ink_string_fast_strncasecmp(argv[i], "src_ip=~", 8)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][8];
        ret_flags |= (REMAP_OPTFLG_SRC_IP | REMAP_OPTFLG_INVERT);
      } else if (!ink_string_fast_strncasecmp(argv[i], "src_ip=", 7)) {
        if ((findmode & REMAP_OPTFLG_SRC_IP) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_SRC_IP;
      } else if (!ink_string_fast_strncasecmp(argv[i], "action=", 7)) {
        if ((findmode & REMAP_OPTFLG_ACTION) != 0)
          idx = i;
        if (argptr)
          *argptr = &argv[i][7];
        ret_flags |= REMAP_OPTFLG_ACTION;
      } else if (!ink_string_fast_strcasecmp(argv[i], "no_negative_cache")) {
        if ((findmode & REMAP_OPTFLG_NONEGCACHE) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_NONEGCACHE;
      } else if (!ink_string_fast_strcasecmp(argv[i], "pristine_host_hdr=1")) {
        if ((findmode & REMAP_OPTFLG_PRISTINEHOST_HDR_ENABLED) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_PRISTINEHOST_HDR_ENABLED;
      } else if (!ink_string_fast_strcasecmp(argv[i], "pristine_host_hdr=0")) {
        if ((findmode & REMAP_OPTFLG_PRISTINEHOST_HDR_DISABLED) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_PRISTINEHOST_HDR_DISABLED;
      } else if (!ink_string_fast_strcasecmp(argv[i], "chunking_enabled=1")) {
        if ((findmode & REMAP_OPTFLG_CHUNKING_ENABLED) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_CHUNKING_ENABLED;
      } else if (!ink_string_fast_strcasecmp(argv[i], "chunking_enabled=0")) {
        if ((findmode & REMAP_OPTFLG_CHUNKING_DISABLED) != 0)
          idx = i;
        ret_flags |= REMAP_OPTFLG_CHUNKING_DISABLED;
      } else if (!ink_string_fast_strncasecmp(argv[i], "mapid=", 6)) {
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

/**
  Determines where we are in a situation where a virtual path is
  being mapped to a server home page. If it is, we set a special flag
  instructing us to be on the lookout for the need to send a redirect
  to if the request URL is a object, opposed to a directory. We need
  the redirect for an object so that the browser is aware that it is
  real accessing a directory (albeit a virtual one).

*/
void
SetHomePageRedirectFlag(url_mapping * new_mapping)
{
  int fromLen, toLen;
  const char *from_path = new_mapping->fromURL.path_get(&fromLen);
  const char *to_path = new_mapping->toURL.path_get(&toLen);

  new_mapping->homePageRedirect = (from_path && !to_path) ? true : false;
}


// ===============================================================================
static int
is_inkeylist(char *key, ...)
{
  int i, idx, retcode = 0;

  if (likely(key && key[0])) {
    char tmpkey[512];
    va_list ap;
    va_start(ap, key);
    for (i = 0; i < (int) (sizeof(tmpkey) - 2) && (tmpkey[i] = *key++) != 0;)
      if (tmpkey[i] != '_' && tmpkey[i] != '.')
        i++;
    tmpkey[i] = 0;

    if (tmpkey[0]) {
      char *str = va_arg(ap, char *);
      for (idx = 1; str; idx++) {
        if (!strcasecmp(tmpkey, str)) {
          retcode = idx;
          break;
        }
        str = va_arg(ap, char *);
      }
    }
    va_end(ap);
  }
  return retcode;
}

/**
  Cleanup *char[] array - each item in array must be allocated via
  xmalloc or similar "x..." function.

*/
static void
clear_xstr_array(char *v[], int vsize)
{
  if (v && vsize > 0) {
    for (int i = 0; i < vsize; i++) {
      if (v[i]) {
        v[i] = (char *) xfree_null(v[i]);
      }
    }
  }
}

static const char *
validate_filter_args(acl_filter_rule ** rule_pp, char **argv, int argc, char *errStrBuf, int errStrBufSize)
{
  acl_filter_rule *rule;
  unsigned long ul;
  char *argptr;
  SRC_IP_INFO *ipi;
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
    if ((ul = check_remap_option(&argv[i], 1, 0, NULL, &argptr)) == 0) {
      Debug("url_rewrite", "[validate_filter_args] Unknow remap option - %s", argv[i]);
      ink_snprintf(errStrBuf, errStrBufSize, "Unknown option - \"%s\"", argv[i]);
      errStrBuf[errStrBufSize - 1] = 0;
      if (new_rule_flg) {
        delete rule;
        *rule_pp = NULL;
      }
      return (const char *) errStrBuf;
    }
    if (!argptr || !argptr[0]) {
      Debug("url_rewrite", "[validate_filter_args] Empty argument in %s", argv[i]);
      ink_snprintf(errStrBuf, errStrBufSize, "Empty argument in \"%s\"", argv[i]);
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
        ink_snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"method=\" filters!", ACL_FILTER_MAX_METHODS);
        errStrBuf[errStrBufSize - 1] = 0;
        if (new_rule_flg) {
          delete rule;
          *rule_pp = NULL;
        }
        return (const char *) errStrBuf;
      }
      // Please remember that the order of hash idx creation is very important and it is defined
      // in HTTP.cc file
      if (!ink_string_fast_strcasecmp(argptr, "CONNECT"))
        m = HTTP_WKSIDX_CONNECT;
      else if (!ink_string_fast_strcasecmp(argptr, "DELETE"))
        m = HTTP_WKSIDX_DELETE;
      else if (!ink_string_fast_strcasecmp(argptr, "GET"))
        m = HTTP_WKSIDX_GET;
      else if (!ink_string_fast_strcasecmp(argptr, "HEAD"))
        m = HTTP_WKSIDX_HEAD;
      else if (!ink_string_fast_strcasecmp(argptr, "ICP_QUERY"))
        m = HTTP_WKSIDX_ICP_QUERY;
      else if (!ink_string_fast_strcasecmp(argptr, "OPTIONS"))
        m = HTTP_WKSIDX_OPTIONS;
      else if (!ink_string_fast_strcasecmp(argptr, "POST"))
        m = HTTP_WKSIDX_POST;
      else if (!ink_string_fast_strcasecmp(argptr, "PURGE"))
        m = HTTP_WKSIDX_PURGE;
      else if (!ink_string_fast_strcasecmp(argptr, "PUT"))
        m = HTTP_WKSIDX_PUT;
      else if (!ink_string_fast_strcasecmp(argptr, "TRACE"))
        m = HTTP_WKSIDX_TRACE;
      else if (!ink_string_fast_strcasecmp(argptr, "PUSH"))
        m = HTTP_WKSIDX_PUSH;
      else {
        Debug("url_rewrite", "[validate_filter_args] Unknown method value %s", argptr);
        ink_snprintf(errStrBuf, errStrBufSize, "Unknown method \"%s\"", argptr);
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
          ink_snprintf(errStrBuf, errStrBufSize, "Incorrect method index %d", j);
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
        ink_snprintf(errStrBuf, errStrBufSize, "Defined more than %d \"src_ip=\" filters!", ACL_FILTER_MAX_SRC_IP);
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
        ink_snprintf(errStrBuf, errStrBufSize, "Unknown argument \"%s\"", argv[i]);
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


static const char *
parse_directive(BUILD_TABLE_INFO * bti, char *errbuf, int errbufsize)
{
  bool flg;
  char *directive = NULL;
  acl_filter_rule *rp, **rpp;
  const char *cstr = NULL;

  // Check arguments
  if (unlikely(!bti || !errbuf || errbufsize <= 0 || !bti->paramc || (directive = bti->paramv[0]) == NULL)) {
    Debug("url_rewrite", "[parse_directive] Invalid argument(s)");
    return "Invalid argument(s)";
  }

  Debug("url_rewrite", "[parse_directive] Start processing \"%s\" directive", directive);

  if (directive[0] != '.' || directive[1] == 0) {
    ink_snprintf(errbuf, errbufsize, "Invalid directive \"%s\"", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }
  if (is_inkeylist(&directive[1], "definefilter", "deffilter", "defflt", NULL)) {
    if (bti->paramc < 2) {
      ink_snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    if (bti->argc < 1) {
      ink_snprintf(errbuf, errbufsize, "Directive \"%s\" must have filter parameter(s)", directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }

    flg = ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) ? true : false;
    // coverity[alloc_arg]
    if ((cstr = validate_filter_args(&rp, bti->argv, bti->argc, errbuf, errbufsize)) == NULL && rp) {
      if (flg)                  // new filter - add to list
      {
        Debug("url_rewrite", "[parse_directive] new rule \"%s\" was created", bti->paramv[1]);
        for (rpp = &bti->rules_list; *rpp; rpp = &((*rpp)->next));
        (*rpp = rp)->name(bti->paramv[1]);
      }
      Debug("url_rewrite", "[parse_directive] %d argument(s) were added to rule \"%s\"", bti->argc, bti->paramv[1]);
      rp->add_argv(bti->argc, bti->argv);       // store string arguments for future processing
    }
  } else if (is_inkeylist(&directive[1], "deletefilter", "delfilter", "delflt", NULL)) {
    if (bti->paramc < 2) {
      ink_snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    acl_filter_rule::delete_byname(&bti->rules_list, (const char *) bti->paramv[1]);
  } else if (is_inkeylist(&directive[1], "usefilter", "activefilter", "activatefilter", "useflt", NULL)) {
    if (bti->paramc < 2) {
      ink_snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) {
      ink_snprintf(errbuf, errbufsize, "Undefined filter \"%s\" in directive \"%s\"", bti->paramv[1], directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    acl_filter_rule::requeue_in_active_list(&bti->rules_list, rp);
  } else
    if (is_inkeylist(&directive[1], "unusefilter", "deactivatefilter", "unactivefilter", "deuseflt", "unuseflt", NULL))
  {
    if (bti->paramc < 2) {
      ink_snprintf(errbuf, errbufsize, "Directive \"%s\" must have name argument", directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    if ((rp = acl_filter_rule::find_byname(bti->rules_list, (const char *) bti->paramv[1])) == NULL) {
      ink_snprintf(errbuf, errbufsize, "Undefined filter \"%s\" in directive \"%s\"", bti->paramv[1], directive);
      Debug("url_rewrite", "[parse_directive] %s", errbuf);
      return (const char *) errbuf;
    }
    acl_filter_rule::requeue_in_passive_list(&bti->rules_list, rp);
  } else {
    ink_snprintf(errbuf, errbufsize, "Unknown directive \"%s\"", directive);
    Debug("url_rewrite", "[parse_directive] %s", errbuf);
    return (const char *) errbuf;
  }
  return cstr;
}


static const char *
process_filter_opt(url_mapping * mp, BUILD_TABLE_INFO * bti, char *errStrBuf, int errStrBufSize)
{
  acl_filter_rule *rp, **rpp;
  const char *errStr = NULL;

  if (unlikely(!mp || !bti || !errStrBuf || errStrBufSize <= 0)) {
    Debug("url_rewrite", "[process_filter_opt] Invalid argument(s)");
    return (const char *) "[process_filter_opt] Invalid argument(s)";
  }
  for (rp = bti->rules_list; rp; rp = rp->next) {
    if (rp->active_queue_flag) {
      Debug("url_rewrite", "[process_filter_opt] Add active main filter \"%s\" (argc=%d)",
            rp->filter_name ? rp->filter_name : "<NULL>", rp->argc);
      for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next));
      if ((errStr = validate_filter_args(rpp, rp->argv, rp->argc, errStrBuf, errStrBufSize)) != NULL)
        break;
    }
  }
  if (!errStr && (bti->remap_optflg & REMAP_OPTFLG_ALL_FILTERS) != 0) {
    Debug("url_rewrite", "[process_filter_opt] Add per remap filter");
    for (rpp = &mp->filter; *rpp; rpp = &((*rpp)->next));
    errStr = validate_filter_args(rpp, bti->argv, bti->argc, errStrBuf, errStrBufSize);
  }
  return errStr;
}


UrlRewrite::UrlRewrite(char *file_var_in)
:lookup_table(NULL), reverse_table(NULL), permanent_redirect_table(NULL),
temporary_redirect_table(NULL), nohost_rules(0), reverse_proxy(0), pristine_host_hdr(0), backdoor_enabled(0),
mgmt_autoconf_port(0), default_to_pac(0), default_to_pac_port(0), file_var(NULL), ts_name(NULL),
http_default_redirect_url(NULL), num_rules_forward(0), num_rules_reverse(0), num_rules_redirect_permanent(0),
num_rules_redirect_temporary(0), remap_pi_list(NULL)
{
  char *config_file = NULL;

  ink_assert(file_var_in != NULL);
  this->file_var = xstrdup(file_var_in);
  config_file_path[0] = '\0';

  REVERSE_ReadConfigStringAlloc(config_file, file_var_in);

  if (config_file == NULL) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to find proxy.config.url_remap.filename");
    Warning("%s Unable to locate remap.config.  No remappings in effect", modulePrefix);
    return;
  }

  this->ts_name = NULL;
  REVERSE_ReadConfigStringAlloc(this->ts_name, tsname_var);
  if (this->ts_name == NULL) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to read proxy.config.proxy_name");
    Warning("%s Unable to determine proxy name.  Incorrect redirects could be generated", modulePrefix);
    this->ts_name = xstrdup("");
  }

  this->http_default_redirect_url = NULL;
  REVERSE_ReadConfigStringAlloc(this->http_default_redirect_url, http_default_redirect_var);
  if (this->http_default_redirect_url == NULL) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, "Unable to read proxy.config.http.referer_default_redirect");
    Warning("%s Unable to determine default redirect url for \"referer\" filter.", modulePrefix);
    this->http_default_redirect_url = xstrdup("http://www.apache.org");
  }

  REVERSE_ReadConfigInteger(reverse_proxy, reverse_var);
  REVERSE_ReadConfigInteger(mgmt_autoconf_port, ac_port_var);
  REVERSE_ReadConfigInteger(default_to_pac, default_to_pac_var);
  REVERSE_ReadConfigInteger(default_to_pac_port, default_to_pac_port_var);
  REVERSE_ReadConfigInteger(pristine_host_hdr, pristine_hdr_var);
  REVERSE_ReadConfigInteger(url_remap_mode, url_remap_mode_var);
  REVERSE_ReadConfigInteger(backdoor_enabled, backdoor_var);

  ink_strncpy(config_file_path, system_config_directory, sizeof(config_file_path));
  strncat(config_file_path, "/", sizeof(config_file_path) - strlen(config_file_path) - 1);
  strncat(config_file_path, config_file, sizeof(config_file_path) - strlen(config_file_path) - 1);
  xfree(config_file);

  if (this->BuildTable() != 0) {
    Warning("something failed during BuildTable() -- check your remap plugins!");
  }

  if (is_debug_tag_set("url_rewrite"))
    Print();
}


UrlRewrite::~UrlRewrite()
{
  xfree(this->file_var);
  xfree(this->ts_name);
  xfree(this->http_default_redirect_url);

  DestroyTable(lookup_table);
  DestroyTable(reverse_table);
  DestroyTable(permanent_redirect_table);
  DestroyTable(temporary_redirect_table);

  if (remap_pi_list) {
    remap_pi_list->delete_my_list();
    remap_pi_list = NULL;
  }
}

/** Sets the reverse proxy flag. */
void
UrlRewrite::SetReverseFlag(int flag)
{
  reverse_proxy = flag;
  if (is_debug_tag_set("url_rewrite"))
    Print();
}

/** Sets the reverse proxy flag. */
void
UrlRewrite::SetPristineFlag(int flag)
{
  pristine_host_hdr = flag;
  if (is_debug_tag_set("url_rewrite"))
    Print();
}

/**
  Allocaites via new, and setups the default mapping to the PAC generator
  port which is used to serve the PAC (proxy autoconfig) file.

*/
url_mapping *
UrlRewrite::SetupPacMapping()
{
  const char *from_url = "http:///";
  const char *local_url = "http://127.0.0.1/";

  url_mapping *mapping;
  int state = 0;
  int error = 0;
  int pac_generator_port;

  mapping = new url_mapping;

  mapping->fromURL.create(NULL);
  mapping->fromURL.parse(from_url, strlen(from_url));

  state = error = 0;
  mapping->toURL.create(NULL);
  mapping->toURL.parse(local_url, strlen(local_url));

  pac_generator_port = (default_to_pac_port < 0) ? mgmt_autoconf_port : default_to_pac_port;

  mapping->toURL.port_set(pac_generator_port);

  return mapping;
}

/**
  Allocaites via new, and adds a mapping like this map /ink/rh
  http://{backdoor}/ink/rh

  These {backdoor} things are then rewritten in a request-hdr hook.  (In the
  future it might make sense to move the rewriting into HttpSM directly.)

*/
url_mapping *
UrlRewrite::SetupBackdoorMapping()
{
  const char from_url[] = "/ink/rh";
  const char to_url[] = "http://{backdoor}/ink/rh";

  url_mapping *mapping = new url_mapping;

  mapping->fromURL.create(NULL);
  mapping->fromURL.parse(from_url, sizeof(from_url) - 1);
  mapping->fromURL.scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);

  mapping->toURL.create(NULL);
  mapping->toURL.parse(to_url, sizeof(to_url) - 1);

  return mapping;
}

/** Deallocated a hash table and all the url_mappings in it. */
void
UrlRewrite::DestroyTable(InkHashTable * h_table)
{
  InkHashTableEntry *ht_entry;
  InkHashTableIteratorState ht_iter;
  url_mapping *value;
  url_mapping *next;

  if (h_table != NULL) {        // Iterate over the hash tabel freeing up the all the url_mappings
    //   contained with in
    for (ht_entry = ink_hash_table_iterator_first(h_table, &ht_iter); ht_entry != NULL;) {
      for (value = (url_mapping *) ink_hash_table_entry_value(h_table, ht_entry); value; value = next) {
        next = value->next;
        delete value;
      }
      ht_entry = ink_hash_table_iterator_next(h_table, &ht_iter);
    }
    ink_hash_table_destroy(h_table);
  }
}

/** Debugging Method. */
void
UrlRewrite::Print()
{
  printf("URL Rewrite table with %d entries\n", num_rules_forward +
         num_rules_reverse + num_rules_redirect_temporary + num_rules_redirect_permanent);
  printf("  Reverse Proxy is %s\n", (reverse_proxy == 0) ? "Off" : "On");

  if (lookup_table != NULL) {
    printf("  Forward Mapping Table with %d entries\n", num_rules_forward);
    PrintTable(lookup_table);
  }
  if (reverse_table != NULL) {
    printf("  Reverse Mapping Table with %d entries\n", num_rules_reverse);
    PrintTable(reverse_table);
  }
  if (permanent_redirect_table != NULL) {
    printf("  Permanent Redirect Mapping Table with %d entries\n", num_rules_redirect_permanent);
    PrintTable(permanent_redirect_table);
  }
  if (temporary_redirect_table != NULL) {
    printf("  Temporary Redirect Mapping Table with %d entries\n", num_rules_redirect_temporary);
    PrintTable(temporary_redirect_table);
  }
  if (http_default_redirect_url != NULL) {
    printf("  Referer filter default redirect URL: \"%s\"\n", http_default_redirect_url);
  }
}

/** Debugging method. */
void
UrlRewrite::PrintTable(InkHashTable * h_table)
{
  InkHashTableEntry *ht_entry;
  InkHashTableIteratorState ht_iter;
  url_mapping *value;
  char from_url_buf[2048], to_url_buf[2048];

  for (ht_entry = ink_hash_table_iterator_first(h_table, &ht_iter); ht_entry != NULL;) {
    for (value = (url_mapping *) ink_hash_table_entry_value(h_table, ht_entry); value; value = value->next) {
      value->fromURL.string_get_buf(from_url_buf, (int) sizeof(from_url_buf));
      value->toURL.string_get_buf(to_url_buf, (int) sizeof(to_url_buf));
      printf("\t %s %s=> %s %s <%s> [plugins %s enabled; running with %d plugins]\n", from_url_buf,
             value->unique ? "(unique)" : "", to_url_buf,
             value->homePageRedirect ? "(R)" : "", value->tag ? value->tag : "",
             value->_plugin_count > 0 ? "are" : "not", value->_plugin_count);
    }
    ht_entry = ink_hash_table_iterator_next(h_table, &ht_iter);
  }
}

/**
  If a remapping is found, returns a pointer to it otherwise NULL is
  returned.

*/
url_mapping *
UrlRewrite::TableLookup(InkHashTable * h_table, URL * request_url,
                        int request_port, const char *request_host, int host_len, char *tag)
{
  url_mapping *ht_entry, *um;
  ums_helper *lh;
  char host_lower_buf[1024], *request_host_lower, *xfree_buf_ptr;
  const char *from_path, *request_path, *request_url_scheme;
  URL *map_from;
  int ht_result, tmp, from_path_len, request_path_len;

  if (unlikely(!request_host || !request_url || host_len < 0))
    return NULL;

  // Canonicalize the string to lower case so that lookup
  //   is case insensitive
  if (unlikely((host_len + 1) > (int) sizeof(host_lower_buf))) {
    if (unlikely((request_host_lower = (xfree_buf_ptr = ((char *) xmalloc(host_len + 1)))) == NULL))
      return NULL;              // impossible
  } else {
    request_host_lower = &host_lower_buf[0];
    xfree_buf_ptr = 0;          /* try to minimize memory allocation */
  }

  // Copy and tolower    
  for (tmp = 0; tmp < host_len; tmp++) {
    if ((ht_result = (request_host_lower[tmp] = request_host[tmp])) >= 'A' && ht_result <= 'Z') {
      request_host_lower[tmp] = (ht_result + ('a' - 'A'));
    }
  }
  request_host_lower[host_len] = 0;

  ht_result = ink_hash_table_lookup(h_table, request_host_lower, (void **) &ht_entry);

  if (unlikely(xfree_buf_ptr))
    xfree(xfree_buf_ptr);

  if (likely(ht_result && ht_entry)) {
    request_path = request_url->path_get(&request_path_len);    // do it only once
    request_url_scheme = request_url->scheme_get(&tmp);

    for (um = ht_entry; um; um = um->next_root_schema) {
      if (um->fromURL.scheme_get(&tmp) == request_url_scheme ||
          (request_url_scheme == URL_SCHEME_HTTPS && um->wildcard_from_scheme))
        break;
    }
    if (unlikely((ht_entry = um) == NULL))
      return NULL;              /* we don't have such scheme in our list */

    if (likely((lh = ht_entry->lookup_helper) != NULL)) {
      if (unlikely(!request_path))      // extreme case - return first best match from empty list or NULL
      {
        return lh->lookup_best_empty(request_host, request_port, tag);
      }
      // request_path != NULL
      return lh->lookup_best_notempty(ht_entry, request_host, request_port, request_path, request_path_len, tag);
    }
    // backup search method - old search without lookup_helper
    // Search through the chain of remappings for this hostname
    //   until we find one that matches.  We take the first matching
    //   remap because the chain is stored in the order of the configuration
    //   file and earlier entries in the file take precedence over later
    //   entries
    for (; ht_entry; ht_entry = ht_entry->next) { 
      // If the incoming request has no tag but the entry does, or both
      // have tags that do not match, then we do NOT have a match.
      bool tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;

      map_from = &ht_entry->fromURL;
      if (tags_match &&
          (request_url_scheme == map_from->scheme_get(&tmp) ||
           (request_url_scheme == URL_SCHEME_HTTPS && ht_entry->wildcard_from_scheme)) &&
          // If the request had no determinable host do not check
          //  the port since the map_from URL will not have a port
          //  number other than the default port
          (*request_host == '\0' || request_port == map_from->port_get())) {    // Port and scheme match so check the path
        from_path = map_from->path_get(&from_path_len);

        if (ht_entry->unique) {
          if ((!from_path && !request_path) || (from_path && request_path &&
                                                from_path_len == request_path_len &&
                                                !strncmp(from_path, request_path, from_path_len)))
            return ht_entry;
        } else if (!from_path || (request_path && request_path_len >= from_path_len &&
                                  !strncmp(from_path, request_path, from_path_len)))
          return ht_entry;
      }
    }                           /* end for */
  }
  return NULL;
}

/**
  Modifies the requestUrl to reflect the mapping defined by mapPtr.  It is
  assumed that mapPtr to points to a mapping that matched the requestURL.

*/
int
UrlRewrite::DoRemap(HttpTransact::State * s, HTTPHdr * request_header, url_mapping * mapPtr, URL * request_url,
                    char **redirect, host_hdr_info * hh_ptr)
{
  // Plugin trackers.
  remap_plugin_info *plugin = NULL;
  //  int plugin_retcode = 0;
  bool plugin_modified_host = false;
  bool plugin_modified_port = false;
  bool plugin_modified_path = false;

  char origURLBuf[1024 * 4];
  int origURLBufSize;

  const char *requestPath;
  int requestPathLen;
  int requestPort;

  URL *map_from = &mapPtr->fromURL;
  const char *fromPath;
  int fromPathLen;

  URL *map_to = &mapPtr->toURL;
  const char *toHost;
  const char *toPath;
  int toPathLen;
  int toHostLen;

  int redirect_host_len;
  const char *redirect_host;

  // Debugging vars
  bool debug_on = false;

  int retcode = 0;              // 0 - no redirect, !=0 - redirected

  if (s) {                      // it is important - we must copy "no_negative_cache" flag before possible plugin call
    s->no_negative_cache = mapPtr->no_negative_cache;
    s->pristine_host_hdr = mapPtr->pristine_host_hdr;
    s->remap_chunking_enabled = mapPtr->chunking_enabled;
  }

  requestPath = request_url->path_get(&requestPathLen);
  requestPort = request_url->port_get();

  fromPath = map_from->path_get(&fromPathLen);

  toHost = map_to->host_get(&toHostLen);
  toPath = map_to->path_get(&toPathLen);

  if (redirect)
    *redirect = NULL;

  if (is_debug_tag_set("url_rewrite"))
    debug_on = true;

  Debug("url_rewrite", "Remapping rule id: %d matched", mapPtr->map_id);

  if (request_header)
    plugin = mapPtr->get_plugin(0);

  if (plugin || debug_on)
    request_url->string_get_buf(origURLBuf, sizeof(origURLBuf), &origURLBufSize);

#if 0                           // Dead code here?
  if (plugin) {
    bool do_x_proto_check = true;
    TSRemapRequestInfo rri;

    // Get request port
    rri.request_port = requestPort;

    // Set request path
    rri.request_path = requestPath;
    rri.request_path_size = requestPathLen;

    // Get request query
    rri.request_query = request_url->query_get(&rri.request_query_size);

    // Get toURL host
    rri.remap_to_host = toHost;
    rri.remap_to_host_size = toHostLen;

    // Set "remap from" path
    rri.remap_from_path = fromPath;
    rri.remap_from_path_size = fromPathLen;

    // Set "remap to" path        
    rri.remap_to_path = toPath;
    rri.remap_to_path_size = toPathLen;

    rri.size = sizeof(rri);
    rri.orig_url = &origURLBuf[0];
    rri.orig_url_size = origURLBufSize;

    rri.remap_from_port = map_from->port_get();
    rri.remap_to_port = map_to->port_get();

    // Set the schemes for this map
    if (unlikely(mapPtr->wildcard_from_scheme)) {
      rri.from_scheme = request_url->scheme_get(&rri.from_scheme_len);
    } else {
      rri.from_scheme = map_from->scheme_get(&rri.from_scheme_len);
    }
    rri.to_scheme = map_to->scheme_get(&rri.to_scheme_len);
    rri.require_ssl = -1;       // By default, the plugin will not modify the to_scheme

    // Used to see if plugin changed anything.
    rri.new_port = (rri.new_host_size = (rri.new_path_size = (rri.redirect_url_size = (rri.new_query_size = 0))));

    // Get fromURL Host
    rri.remap_from_host = map_from->host_get(&rri.remap_from_host_size);
    // Get request Host
    rri.request_host = request_url->host_get(&rri.request_host_size);
    // Copy client IP address
    rri.client_ip = s ? s->client_info.ip : 0;

    // Get Cookie header        
    if (request_header->presence(MIME_PRESENCE_COOKIE)) {
      rri.request_cookie = request_header->value_get(MIME_FIELD_COOKIE, MIME_LEN_COOKIE, &rri.request_cookie_size);
    } else {
      rri.request_cookie = NULL;
      rri.request_cookie_size = 0;
    }
    // Prepare State for the future
    if (s) {
      s->fp_tsremap_os_response = plugin->fp_tsremap_os_response;
      s->remap_plugin_instance = mapPtr->remap_plugin_instance;
    }

    plugin_retcode =
      plugin->fp_tsremap_remap(mapPtr->remap_plugin_instance, s ? (rhandle) (s->state_machine) : NULL, &rri);

    // First step after plugin remap must be "redirect url" check
    if (redirect && plugin_retcode && rri.redirect_url_size > 0) {
      *redirect =
        rri.redirect_url[0] ? xstrndup(rri.redirect_url, rri.redirect_url_size) : xstrdup("http://www.apache.org");
      return rri.redirect_url_size;     // we must return redirect buffer immeditely
    }

    if (plugin_retcode) {
      // Modify the host
      if (rri.new_host_size > 0) {
        request_url->host_set(rri.new_host, rri.new_host_size);
        plugin_modified_host = true;
      }
      // Modify the port, but only if it's different than the request port
      if (rri.new_port != 0) {
        if (requestPort != rri.new_port)
          request_url->port_set(rri.new_port);
        plugin_modified_port = true;
      }
      // Modify the path
      if (rri.new_path_size != 0) {
        if (rri.new_path_size < 0)
          request_url->path_set(NULL, 0);
        else
          request_url->path_set(rri.new_path, rri.new_path_size);
        plugin_modified_path = true;
      }
      // Finally, update the query string. This has a special case, where a negative
      // size value means to remove the query string entirely.
      if (rri.new_query_size != 0) {
        if (rri.new_query_size < 0)
          request_url->query_set(NULL, 0);
        else
          request_url->query_set(rri.new_query, rri.new_query_size);
      }
      // If require_ssl is set, make sure our toScheme is SSL'ified. It's done this way
      // to avoid dealing with weirness (say, a plugin trying to modify the "toScheme"
      // from FTP to HTTPS).
      if (rri.require_ssl != -1) {
        // TODO add other protocols here if/when necessary
        if (rri.require_ssl == 1)       // Plugin wish to turn on SSL (if not already set)
        {
          if ((rri.to_scheme_len == URL_LEN_HTTP) && (rri.to_scheme == URL_SCHEME_HTTP)) {
            request_url->scheme_set(URL_SCHEME_HTTPS, URL_LEN_HTTPS);
            do_x_proto_check = false;
            Debug("url_rewrite", "Plugin changed protocol from HTTP to HTTPS");
          }
        } else                  // Plugin wish to turn off SSL (if already set)
        {
          if ((rri.to_scheme_len == URL_LEN_HTTPS) && (rri.to_scheme == URL_SCHEME_HTTPS)) {
            request_url->scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
            do_x_proto_check = false;
            Debug("url_rewrite", "Plugin changed protocol from HTTPS to HTTP");
          }
        }
      }
      // Check to see if this a cross protocol mapping
      //   If so we need to create a new URL since the URL*
      //   we get from the request_hdr really points to a subclass
      //   of URL that is specific to the protocol
      //
      if (do_x_proto_check && (rri.from_scheme != rri.to_scheme)) {
        request_url->scheme_set(rri.to_scheme, rri.to_scheme_len);
        if (is_debug_tag_set("url_rewrite")) {
          char tmp_buf[2048];
          Debug("url_rewrite", "Cross protocol mapping to %s in plugin",
                request_url->string_get_buf(tmp_buf, (int) sizeof(tmp_buf)));
        }
      }
    }
  } else {
    // Handle cross protocol mapping when there are no remap plugin(s).
    // This is almost identical to above, but has to fetch the schemes.
    int to_len, from_len;
    const char *to_scheme = map_to->scheme_get(&to_len);

    if (to_scheme != request_url->scheme_get(&from_len)) {
      request_url->scheme_set(to_scheme, to_len);
      if (is_debug_tag_set("url_rewrite")) {
        char tmp_buf[2048];
        Debug("url_rewrite", "Cross protocol mapping to %s",
              request_url->string_get_buf(tmp_buf, (int) sizeof(tmp_buf)));
      }
    }
  }
#endif

  // Fall back to "remap" maps if plugin didn't change things already
  if (!plugin_modified_host)
    request_url->host_set(toHost, toHostLen);

  if (!plugin_modified_port && (requestPort != map_to->port_get()))
    request_url->port_set(map_to->port_get_raw());

  // Extra byte is potentially needed for prefix path '/'.
  // Added an extra 3 so that TS wouldn't crash in the field.
  // Allocate a large buffer to avoid problems.
  // Need to figure out why we need the 3 bytes or 512 bytes.
  if (!plugin_modified_path) {
    char newPathTmp[TSREMAP_RRI_MAX_PATH_SIZE];
    char *newPath;
    char *newPathAlloc = NULL;
    int newPathLen = 0;
    int newPathLenNeed = (requestPathLen - fromPathLen) + toPathLen + 512;

    if (newPathLenNeed > TSREMAP_RRI_MAX_PATH_SIZE) {
      newPath = (newPathAlloc = (char *) xmalloc(newPathLenNeed));
      if (debug_on)
        memset(newPath, 0, newPathLenNeed);
    } else {
      newPath = &newPathTmp[0];
      if (debug_on)
        memset(newPath, 0, TSREMAP_RRI_MAX_PATH_SIZE);
    }

    *newPath = 0;

    // Purify load run with QT in a reverse proxy indicated
    // a UMR/ABR/MSE in the line where we do a *newPath == '/' and the strncpy
    // that follows it.  The problem occurs if
    // requestPathLen,fromPathLen,toPathLen are all 0; in this case, we never
    // initialize newPath, but still de-ref it in *newPath == '/' comparison.
    // The memset fixes that problem.

    if (toPath) {
      memcpy(newPath, toPath, toPathLen);
      newPathLen += toPathLen;
    }
    // We might need to insert a trailing slash in the new portion of the path
    // if more will be added and none is present and one will be needed.
    if (!fromPathLen && requestPathLen && toPathLen && *(newPath + newPathLen - 1) != '/') {
      *(newPath + newPathLen) = '/';
      newPathLen++;
    }

    if (requestPath) {
      //avoid adding another trailing slash if the requestPath already had one and so does the toPath
      if (requestPathLen < fromPathLen) {
        if (toPathLen && requestPath[requestPathLen - 1] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      } else {
        if (toPathLen && requestPath[fromPathLen] == '/' && toPath[toPathLen - 1] == '/') {
          fromPathLen++;
        }
      }
      // copy the end of the path past what has been mapped
      if ((requestPathLen - fromPathLen) > 0) {
        // strncpy(newPath + newPathLen, requestPath + fromPathLen, requestPathLen - fromPathLen);
        memcpy(newPath + newPathLen, requestPath + fromPathLen, requestPathLen - fromPathLen);
        newPathLen += (requestPathLen - fromPathLen);
      }
    }
    // We need to remove the leading slash in newPath if one is
    // present.
    if (*newPath == '/') {
      memmove(newPath, newPath + 1, --newPathLen);
    }

    request_url->path_set(newPath, newPathLen);

    if (mapPtr->homePageRedirect && fromPathLen == requestPathLen && redirect) {
      URL redirect_url;
      redirect_url.create(NULL);
      redirect_url.copy(request_url);

      ink_assert(fromPathLen > 0);

      // Extra byte for trailing '/' in redirect
      if (newPathLen > 0 && newPath[newPathLen - 1] != '/') {
        newPath[newPathLen] = '/';
        newPath[++newPathLen] = '\0';
        redirect_url.path_set(newPath, newPathLen);
      }
      // If we have host header information,
      //   put it back into redirect URL
      //
      if (hh_ptr != NULL) {
        redirect_url.host_set(hh_ptr->request_host, hh_ptr->host_len);
        if (redirect_url.port_get() != hh_ptr->request_port)
          redirect_url.port_set(hh_ptr->request_port);
      }
      // If request came in without a host, send back
      //  the redirect with the name the proxy is known by
      if ((redirect_host = redirect_url.host_get(&redirect_host_len)) == NULL)
        redirect_url.host_set(ts_name, strlen(ts_name));

      if ((*redirect = redirect_url.string_get(NULL)) != NULL)
        retcode = strlen(*redirect);

      if (debug_on)
        Debug("url_rewrite", "Redirected %s to %.*s", origURLBuf, retcode, *redirect);

      redirect_url.destroy();
    } else {
      if (debug_on)
        Debug("url_rewrite", "Remapped %s to %.*s via remap.config", origURLBuf, newPathLen, newPath);
    }


    if (unlikely(newPathAlloc))
      xfree(newPathAlloc);
  }

  return retcode;
}

/** Used to do the backwards lookups. */
bool UrlRewrite::ReverseMap(HTTPHdr * response_header, char *tag)
{
  const char *
    location_hdr;
  URL
    location_url;
  int
    loc_length;
  bool
    remap_found = false;
  const char *
    host;
  int
    host_len;

  url_mapping *
    map;
  char *
    new_loc_hdr;
  int
    new_loc_length;

  if (unlikely(num_rules_reverse == 0)) {
    ink_assert(reverse_table == NULL);
    return false;
  }

  location_hdr = response_header->value_get(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, &loc_length);

  if (location_hdr == NULL) {
    Debug("url_rewrite", "Reverse Remap called with empty location header");
    return false;
  }

  location_url.create(NULL);
  location_url.parse(location_hdr, loc_length);

  host = location_url.host_get(&host_len);
  map = TableLookup(reverse_table, &location_url, location_url.port_get(), host, host_len, tag);

  if (map != NULL) {            /* HDR FIX ME
                                   Debug("url_rewrite", "Location header before rewrite: %s",
                                   response_header->value_get(MIME_FIELD_LOCATION));
                                 */

    remap_found = true;
    DoRemap(NULL, NULL, map, &location_url);

    new_loc_hdr = location_url.string_get(NULL, &new_loc_length);
    response_header->value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, new_loc_hdr, new_loc_length);
    xfree(new_loc_hdr);

    /* HDR FIX ME
       Debug("url_rewrite", "Location header after rewrite: %s",
       response_header->value_get(MIME_FIELD_LOCATION));
     */
  }

  location_url.destroy();
  return remap_found;
}

/** Perform fast ACL filtering. */
void
UrlRewrite::PerformACLFiltering(HttpTransact::State * s, url_mapping * map)
{
}


bool
  UrlRewrite::Remap(HttpTransact::State * s, HTTPHdr * request_header,
                    char **redirect_url, char **orig_url, char *tag, unsigned int filter_mask)
{
  URL *request_url = NULL;
  url_mapping *map = NULL;

  // Vars for building a new host header
  //
  //   Host buf length is a static buffer
  //     size is MAXDNAME (max hostname length) + 12 for
  //             port length, one for the ':' and one
  //             for the string terminator
  //
  const int host_buf_len = MAXDNAME + 12 + 1 + 1;
  char host_hdr_buf[host_buf_len], tmp_referer_buf[4096], tmp_redirect_buf[4096], tmp_buf[2048], *c;
  const char *remapped_host, *request_url_host;
  int remapped_host_len, request_url_host_len, remapped_port, tmp;
  int from_len;
  bool remap_found = false;
  bool proxy_request = false;
  host_hdr_info *hh_ptr = NULL;
  host_hdr_info hh_info;
  referer_info *ri;

  ink_assert(redirect_url != NULL);

  if (unlikely(num_rules_forward == 0)) {
    ink_assert(lookup_table == NULL);
    return false;
  }
  // Since are called before request validity checking
  //  occurs, make sure that we have both a valid request
  //  header and a valid URL
  //
  if (unlikely(!request_header || (request_url = request_header->url_get()) == NULL || !request_url->valid())) {
    return false;
  }

  request_url_host = request_url->host_get(&request_url_host_len);
  if (request_url_host_len > 0 || reverse_proxy == 0) { // Proxy request.  Use the information from the URL on the
    //  request line.  (Note: we prefer the information in
    //  the request URL since some user-agents send broken
    //  host headers.)
    proxy_request = true;
    map = TableLookup(lookup_table, request_url, request_url->port_get(), request_url_host, request_url_host_len, tag);
  } else {                      // Server request.  Use the host header to figure out where
    //    it goes
    int host_len, host_hdr_len;
    const char *host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_hdr_len);
    if (!host_hdr) {
      host_hdr = "";
      host_hdr_len = 0;
    }
    char *tmp = (char *) memchr(host_hdr, ':', host_hdr_len);
    int request_port;

    if (tmp == NULL) {
      host_len = host_hdr_len;
      // Get the default port from URL structure
      request_port = request_url->port_get();
    } else {
      host_len = tmp - host_hdr;
      request_port = ink_atoi(tmp + 1, host_hdr_len - host_len);

      // If atoi fails, try the default for the
      //   protocol
      if (request_port == 0) {
        request_port = request_url->port_get();
      }
    }

    map = TableLookup(lookup_table, request_url, request_port, host_hdr, host_len, tag);

    // Save this information for passing to DoRemap
    hh_info.host_len = host_len;
    hh_info.request_host = host_hdr;
    hh_info.request_port = request_port;
    hh_ptr = &hh_info;

    // If no rules match, check empty host rules since
    //   they function as default rules for server requests
    if (map == NULL && nohost_rules && *host_hdr != '\0') {
      map = TableLookup(lookup_table, request_url, 0, "", 0, tag);
    }
  }

  if (map) {                    // Make a copy of the original URL. It is up to the callee to free this [which is t_state->unmapped_request_url freed inside HttpTransact::State::destroy()]
    if (orig_url) {             // We need to insert the host so that we have an accurate URL
      if (proxy_request == false) {
        request_url->host_set(hh_info.request_host, hh_info.host_len);

        // Only set the port if we need to so default ports
        //  do show up in URLs
        if (request_url->port_get() != hh_info.request_port) {
          request_url->port_set(hh_info.request_port);
        }
      }
      *orig_url = request_url->string_get_ref(NULL);
    }
    // Perform the actual URL rewrite
    if (unlikely(DoRemap(s, request_header, map, request_url, redirect_url, hh_ptr) && *redirect_url != NULL)) {
      return false;             // There is a redirect, return now
    }
    // Do fast ACL filtering (it is safe to check map here)
    PerformACLFiltering(s, map);

    //printf("****************** PerformACLFiltering = %s ************\n",s->client_connection_enabled ? "ENABLED" : "DISABLED");

    // vl: Check referer filtering rules
    if ((filter_mask & URL_REMAP_FILTER_REFERER) != 0 && (ri = map->referer_list) != 0) {
      const char *referer_hdr = 0;
      int referer_len = 0;
      bool enabled_flag = map->optional_referer ? true : false;

      if (request_header->presence(MIME_PRESENCE_REFERER) &&
          (referer_hdr = request_header->value_get(MIME_FIELD_REFERER, MIME_LEN_REFERER, &referer_len)) != NULL) {
        if (referer_len >= (int) sizeof(tmp_referer_buf))
          referer_len = (int) (sizeof(tmp_referer_buf) - 1);
        memcpy(tmp_referer_buf, referer_hdr, referer_len);
        tmp_referer_buf[referer_len] = 0;
        for (enabled_flag = false; ri; ri = ri->next) {
          if (ri->any) {
            enabled_flag = true;
            if (!map->negative_referer)
              break;
          } else if (ri->regx_valid && !regexec(&ri->regx, tmp_referer_buf, 0, NULL, 0)) {
            enabled_flag = ri->negative ? false : true;
            break;
          }
        }
      }

      if (!enabled_flag) {
        if (!map->default_redirect_url) {
          if ((filter_mask & URL_REMAP_FILTER_REDIRECT_FMT) != 0 && map->redir_chunk_list) {
            redirect_tag_str *rc;

            tmp_redirect_buf[(tmp = 0)] = 0;
            for (rc = map->redir_chunk_list; rc; rc = rc->next) {
              c = 0;
              switch (rc->type) {
              case 's':
                c = rc->chunk_str;
                break;
              case 'r':
                c = (referer_len && referer_hdr) ? &tmp_referer_buf[0] : 0;
                break;
              case 'f':
              case 't':
                remapped_host =
                  (rc->type == 'f') ? map->fromURL.string_get_buf(tmp_buf, (int) sizeof(tmp_buf),
                                                                  &from_len) : map->toURL.string_get_buf(tmp_buf, (int)
                                                                                                         sizeof
                                                                                                         (tmp_buf),
                                                                                                         &from_len);
                if (remapped_host && from_len > 0)
                  c = &tmp_buf[0];
                break;
              case 'o':
                c = *orig_url;
                break;
              };
              if (c && tmp < (int) (sizeof(tmp_redirect_buf) - 1)) {
                tmp += ink_snprintf(&tmp_redirect_buf[tmp], sizeof(tmp_redirect_buf) - tmp, "%s", c);
              }
            }
            tmp_redirect_buf[sizeof(tmp_redirect_buf) - 1] = 0;
            *redirect_url = xstrdup(tmp_redirect_buf);
          }
        } else
          *redirect_url = xstrdup(http_default_redirect_url);

        if (*redirect_url == NULL) {
          *redirect_url = xstrdup(map->filter_redirect_url ? map->filter_redirect_url : http_default_redirect_url);
        }

        return false;
      }
    }

    remap_found = true;

    // We also need to rewrite the "Host:" header if it exists and
    //   pristine host hdr is not enabled
    int host_len;
    const char *host_hdr = request_header->value_get(MIME_FIELD_HOST,
                                                     MIME_LEN_HOST, &host_len);

    if (host_hdr != NULL &&
        ((pristine_host_hdr <= 0 && s->pristine_host_hdr <= 0) ||
         (pristine_host_hdr > 0 && s->pristine_host_hdr == 0))) {
      remapped_host = request_url->host_get(&remapped_host_len);
      remapped_port = request_url->port_get();

      // Debug code to print out old host header.  This was easier before
      //  the header conversion.  Now we have to copy to gain null
      //  termination for the Debug() call
      if (is_debug_tag_set("url_rewrite")) {
        int old_host_hdr_len;
        char *old_host_hdr = (char *) request_header->value_get(MIME_FIELD_HOST,
                                                                MIME_LEN_HOST,
                                                                &old_host_hdr_len);

        if (old_host_hdr) {
          old_host_hdr = xstrndup(old_host_hdr, old_host_hdr_len);
          Debug("url_rewrite", "Host Header before rewrite %s", old_host_hdr);
          xfree(old_host_hdr);
        }
      }
      //
      // Create the new host header field being careful that our
      //   temporary buffer has adequate length
      //
      if (host_buf_len > remapped_host_len) {
        tmp = remapped_host_len;
        memcpy(host_hdr_buf, remapped_host, remapped_host_len);
        tmp += ink_snprintf(host_hdr_buf + remapped_host_len, host_buf_len - remapped_host_len - 1,
                            ":%d", remapped_port);
      } else {
        tmp = host_buf_len;
      }

      // It is possible that the hostname is too long.  If it is punt,
      //   and remove the host header.  If it is too long the HostDB
      //   won't be able to resolve it and the request will not go
      //   through
      if (tmp >= host_buf_len) {
        request_header->field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
        Debug("url_rewrite", "Host Header too long after rewrite");
      } else {
        Debug("url_rewrite", "Host Header after rewrite %s", host_hdr_buf);
        request_header->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host_hdr_buf, tmp);
      }
    }
  }

  return remap_found;
}

/**
  Determines if a redirect is to occur and if so, figures out what the
  redirect is. This was plaguiarized from UrlRewrite::Remap. redirect_url
  ought to point to the new, mapped URL when the function exists.

*/
mapping_type UrlRewrite::Remap_redirect(HTTPHdr * request_header, char **redirect_url, char **orig_url, char *tag)
{
  URL *
    request_url;
  url_mapping *
    permanent_redirect = NULL, *temporary_redirect = NULL, *map = NULL;
  mapping_type
    mappingType = NONE;
  const char *
    host = NULL;
  char
    tmp_buf[2048];
  int
    host_len = 0, request_port = 0;
  bool
    prt,
    trt;                        // existence of permanent and temporary redirect tables, respectively

  // Host header for DoRemap
  host_hdr_info *
    hh_ptr = NULL;
  host_hdr_info
    hh_info;

  ink_assert(redirect_url != NULL);

  if (redirect_url == NULL) {
    Debug("url_rewrite", "redirect_url is invalid.  UrlRewrite::Remap_redirect bailing out.");
    return NONE;
  }

  *redirect_url = NULL;

  prt = (num_rules_redirect_permanent != 0);
  trt = (num_rules_redirect_temporary != 0);

  if (prt + trt == 0)
    return NONE;

  // Since are called before request validity checking
  //  occurs, make sure that we have both a valid request
  //  header and a valid URL
  //
  if (request_header == NULL) {
    Debug("url_rewrite", "request_header was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return NONE;
  }
  request_url = request_header->url_get();
  if (!request_url->valid()) {
    Debug("url_rewrite", "request_url was invalid.  UrlRewrite::Remap_redirect bailing out.");
    return NONE;
  }

  if (is_debug_tag_set("url_rewrite")) {
    request_url->string_get_buf(tmp_buf, (int) sizeof(tmp_buf));
    Debug("url_rewrite", "%s request in remap_redirect", tmp_buf);
  }
  // You can find information about forward rules, etc. in the remap.config file
  // Debug("url_rewrite", "Forward Rules: #%d", num_rules_forward);
  // Debug("url_rewrite", "Reverse Rules: #%d", num_rules_reverse);
  // Debug("url_rewrite", "Permanent Redirect Rules: #%d", num_rules_redirect_permanent);
  // Debug("url_rewrite", "Temporary Redirect Rules: #%d", num_rules_redirect_temporary);

  host = request_url->host_get(&host_len);
  request_port = request_url->port_get();

  if (host_len == 0 && reverse_proxy != 0) {    // Server request.  Use the host header to figure out where
    // it goes.  Host header parsing is same as in ::Remap
    int
      host_hdr_len;
    const char *
      host_hdr = request_header->value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &host_hdr_len);

    if (!host_hdr) {
      host_hdr = "";
      host_hdr_len = 0;
    }

    const char *
      tmp = (const char *) memchr(host_hdr, ':', host_hdr_len);

    if (tmp == NULL) {
      host_len = host_hdr_len;
    } else {
      host_len = tmp - host_hdr;
      request_port = ink_atoi(tmp + 1, host_hdr_len - host_len);

      // If atoi fails, try the default for the
      //   protocol
      if (request_port == 0) {
        request_port = request_url->port_get();
      }
    }

    host = host_hdr;

    memset(&hh_info, 0, sizeof(hh_info));
    // Save this information for passing to DoRemap
    hh_info.host_len = host_len;
    hh_info.request_host = host_hdr;
    hh_info.request_port = request_port;
    hh_ptr = &hh_info;
  }
  // Temporary Redirects have precedence over Permanent Redirects
  // the rationale behind this is that network administrators might
  // want quick redirects and not want to worry about all the existing
  // permanent rules
  if (prt) {
    permanent_redirect = TableLookup(permanent_redirect_table, request_url, request_port, host, host_len, tag);
  }
  if (trt) {
    temporary_redirect = TableLookup(temporary_redirect_table, request_url, request_port, host, host_len, tag);
  }
  if (temporary_redirect != NULL) {
    mappingType = TEMPORARY_REDIRECT;
    map = temporary_redirect;
  } else if (permanent_redirect != NULL) {
    mappingType = PERMANENT_REDIRECT;
    map = permanent_redirect;
  }

  if (map != NULL) {
    *orig_url = NULL;

    // Make a copy of the request url so that we can munge it
    //   for the redirect
    URL
      rurl;
    rurl.create(NULL);
    rurl.copy(request_url);

    // Perform the actual URL rewrite
    if (!DoRemap(NULL, NULL, map, &rurl, redirect_url, hh_ptr)) {
      *redirect_url = rurl.string_get(NULL);
    } else {                    // vl: Do nothing because of "redirect_url" was created inside DoRemap!
      // There is potential memory leack was here (in the original version)
      /* nope */ ;
    }
    rurl.destroy();

    ink_assert((mappingType == PERMANENT_REDIRECT) || (mappingType == TEMPORARY_REDIRECT));
    Debug("url_rewrite", "New URL: %s", *redirect_url);
    return mappingType;
  }
  ink_assert(mappingType == NONE);
  return NONE;
}

/**
  Takes off any trailing slashes on the path of a URL. We need to this in
  order to normalize our URLs for reverse proxy.

*/
void
UrlRewrite::RemoveTrailingSlash(URL * url)
{
  ink_assert(url != NULL);
  Debug("url_rewrite", "Removing trailing slash!");
  int path_length;
  const char *orig_path = url->path_get(&path_length);
  char *new_path;

  if (orig_path != NULL) {
    if (path_length > 0 && orig_path[path_length - 1] == '/') {
      if (path_length == 1) {
        url->path_set("", 0);
      } else {
        new_path = xstrndup(orig_path, path_length);
        new_path[path_length - 1] = '\0';
        url->path_set(new_path, path_length - 1);
        xfree(new_path);
      }
    }
  }
}

/**
  Returns the length of the URL.

  Will replace the terminator with a '/' if this is a full URL and
  there are no '/' in it after the the host.  This ensures that class
  URL parses the URL correctly.

*/
int
UrlRewrite::UrlWhack(char *toWhack, int *origLength)
{
  int length = strlen(toWhack);
  char *tmp;
  *origLength = length;

  // Check to see if this a full URL
  tmp = strstr(toWhack, "://");
  if (tmp != NULL) {
    if (strchr(tmp + 3, '/') == NULL) {
      toWhack[length] = '/';
      length++;
    }
  }
  return length;
}

/**
  Reads the configuration file and creates a new hash table.

  @return zero on success and non-zero on failure.

*/
int
UrlRewrite::BuildTable()
{
  BUILD_TABLE_INFO bti;
  char *file_buf, errBuf[1024], errStrBuf[1024];
  Tokenizer whiteTok(" \t");
  bool alarm_already = false;
  const char *errStr;

  // Vars to parse line in file
  char *tok_state, *cur_line, *cur_line_tmp;
  int rparse, cur_line_size, j, cln = 0;        // Our current line number

  // Vars to build the mapping
  const char *fromScheme, *toScheme;
  int fromSchemeLen, toSchemeLen;
  const char *fromHost, *toHost;
  int fromHostLen, toHostLen;
  char *map_from, *map_from_start;
  char *map_to, *map_to_start;
  const char *tmp;              // Appease the DEC compiler
  char *fromHost_lower = NULL;
  char *fromHost_lower_ptr = NULL;
  char fromHost_lower_buf[1024];
  url_mapping *new_mapping;
  mapping_type maptype;
  referer_info *ri;
  int origLength;
  int length;
  int state, error;
  int tok_count;

  ink_assert(lookup_table == NULL);
  ink_assert(reverse_table == NULL);
  ink_assert(permanent_redirect_table == NULL);
  ink_assert(temporary_redirect_table == NULL);
  ink_assert(num_rules_forward == 0);
  ink_assert(num_rules_reverse == 0);
  ink_assert(num_rules_redirect_permanent == 0);
  ink_assert(num_rules_redirect_temporary == 0);

  memset(&bti, 0, sizeof(bti));

  if ((file_buf = readIntoBuffer(config_file_path, modulePrefix, NULL)) == NULL) {
    ink_error("Can't load remapping configuration file - %s", config_file_path);
    return 1;
  }

  lookup_table = ink_hash_table_create(InkHashTableKeyType_String);
  reverse_table = ink_hash_table_create(InkHashTableKeyType_String);
  permanent_redirect_table = ink_hash_table_create(InkHashTableKeyType_String);
  temporary_redirect_table = ink_hash_table_create(InkHashTableKeyType_String);

  bti.paramc = (bti.argc = 0);
  memset(bti.paramv, 0, sizeof(bti.paramv));
  memset(bti.argv, 0, sizeof(bti.argv));

  Debug("url_rewrite", "[BuildTable] UrlRewrite::BuildTable()");

  for (cur_line = tokLine(file_buf, &tok_state); cur_line != NULL;) {
    errStrBuf[0] = 0;
    clear_xstr_array(bti.paramv, sizeof(bti.paramv) / sizeof(char *));
    clear_xstr_array(bti.argv, sizeof(bti.argv) / sizeof(char *));
    bti.paramc = (bti.argc = 0);

    // Strip leading whitespace
    while (*cur_line && isascii(*cur_line) && isspace(*cur_line))
      cur_line++;

    if ((cur_line_size = strlen((char *) cur_line)) <= 0) {
      cur_line = tokLine(NULL, &tok_state);
      cln++;
      continue;
    }
    // Strip trailing whitespace
    cur_line_tmp = cur_line + cur_line_size - 1;
    while (cur_line_tmp != cur_line && isascii(*cur_line_tmp) && isspace(*cur_line_tmp)) {
      *cur_line_tmp = '\0';
      cur_line_tmp--;
    }

    if ((cur_line_size = strlen((char *) cur_line)) <= 0 || *cur_line == '#' || *cur_line == '\0') {
      cur_line = tokLine(NULL, &tok_state);
      cln++;
      continue;
    }

    Debug("url_rewrite", "[BuildTable] Parsing: \"%s\"", cur_line);

    tok_count = whiteTok.Initialize(cur_line, SHARE_TOKS);

    for (j = 0; j < tok_count; j++) {
      if (((char *) whiteTok[j])[0] == '@') {
        if (((char *) whiteTok[j])[1])
          bti.argv[bti.argc++] = xstrdup(&(((char *) whiteTok[j])[1]));
      } else {
        bti.paramv[bti.paramc++] = xstrdup((char *) whiteTok[j]);
      }
    }

    // Initial verification for number of arguments
    if (bti.paramc<1 || (bti.paramc < 3 && bti.paramv[0][0] != '.') || bti.paramc> BUILD_TABLE_MAX_ARGS) {
      ink_snprintf(errBuf, sizeof(errBuf), "%s Malformed line %d in file %s", modulePrefix, cln + 1, config_file_path);
      Debug("url_rewrite", "[BuildTable] %s", errBuf);
      SignalError(errBuf, alarm_already);
      cur_line = tokLine(NULL, &tok_state);
      cln++;
      continue;
    }
    // just check all major flags/optional arguments
    bti.remap_optflg = check_remap_option(bti.argv, bti.argc);


    // Check directive keywords (starting from '.')
    if (bti.paramv[0][0] == '.') {
      if ((errStr = parse_directive(&bti, errStrBuf, sizeof(errStrBuf))) != NULL) {
        ink_snprintf(errBuf, sizeof(errBuf) - 1, "%s Error on line %d - %s", modulePrefix, cln + 1, errStr);
        Debug("url_rewrite", "[BuildTable] %s", errBuf);
        SignalError(errBuf, alarm_already);
      }
      cur_line = tokLine(NULL, &tok_state);
      cln++;
      continue;
    }
    // Check to see whether is a reverse or forward mapping
    if (!strcasecmp("reverse_map", bti.paramv[0])) {
      Debug("url_rewrite", "[BuildTable] - REVERSE_MAP");
      maptype = REVERSE_MAP;
    } else if (!strcasecmp("map", bti.paramv[0])) {
      Debug("url_rewrite", "[BuildTable] - %s",
            ((bti.remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? "FORWARD_MAP" : "FORWARD_MAP_REFERER");
      maptype = ((bti.remap_optflg & REMAP_OPTFLG_MAP_WITH_REFERER) == 0) ? FORWARD_MAP : FORWARD_MAP_REFERER;
    } else if (!strcasecmp("redirect", bti.paramv[0])) {
      Debug("url_rewrite", "[BuildTable] - PERMANENT_REDIRECT");
      maptype = PERMANENT_REDIRECT;
    } else if (!strcasecmp("redirect_temporary", bti.paramv[0])) {
      Debug("url_rewrite", "[BuildTable] - TEMPORARY_REDIRECT");
      maptype = TEMPORARY_REDIRECT;
    } else if (!strcasecmp("map_with_referer", bti.paramv[0])) {
      Debug("url_rewrite", "[BuildTable] - FORWARD_MAP_REFERER");
      maptype = FORWARD_MAP_REFERER;
    } else {
      ink_snprintf(errBuf, sizeof(errBuf) - 1, "%s Unknown mapping type at line %d", modulePrefix, cln + 1);
      Debug("url_rewrite", "[BuildTable] - %s", errBuf);
      SignalError(errBuf, alarm_already);
      cur_line = tokLine(NULL, &tok_state);
      cln++;
      continue;
    }

    new_mapping = NEW(new url_mapping);

    // apply filter rules if we have to
    if ((errStr = process_filter_opt(new_mapping, &bti, errStrBuf, sizeof(errStrBuf))) != NULL) {
      goto MAP_ERROR;
    }
    // apply "no_negative_cache" if we have to
    if ((bti.remap_optflg & REMAP_OPTFLG_NONEGCACHE) != 0)
      new_mapping->no_negative_cache = true;
    if ((bti.remap_optflg & REMAP_OPTFLG_PRISTINEHOST_HDR_ENABLED) != 0)
      new_mapping->pristine_host_hdr = 1;
    if ((bti.remap_optflg & REMAP_OPTFLG_PRISTINEHOST_HDR_DISABLED) != 0)
      new_mapping->pristine_host_hdr = 0;
    if ((bti.remap_optflg & REMAP_OPTFLG_CHUNKING_ENABLED) != 0)
      new_mapping->chunking_enabled = 1;
    if ((bti.remap_optflg & REMAP_OPTFLG_CHUNKING_DISABLED) != 0)
      new_mapping->chunking_enabled = 0;

    new_mapping->map_id = 0;
    if ((bti.remap_optflg & REMAP_OPTFLG_MAP_ID) != 0) {
      int idx = 0;
      char *c;
      int ret = check_remap_option(bti.argv, bti.argc, REMAP_OPTFLG_MAP_ID, &idx);
      if (ret & REMAP_OPTFLG_MAP_ID) {
        c = strchr(bti.argv[idx], (int) '=');
        new_mapping->map_id = (unsigned int) atoi(++c);
      }
    }

    map_from = bti.paramv[1];
    state = (error = 0);
    length = UrlWhack(map_from, &origLength);

    // FIX --- what does this comment mean?
    //
    // URL::create modified map_from so keep a point to
    //   the beginning of the string
    if ((tmp = (map_from_start = map_from)) != NULL && length > 2 && tmp[length - 1] == '/' && tmp[length - 2] == '/') {
      new_mapping->unique = true;
      length -= 2;
    }

    new_mapping->fromURL.create(NULL);
    rparse = new_mapping->fromURL.parse_no_path_component_breakdown(tmp, length);

    map_from_start[origLength] = '\0';  // Unwhack

    if (rparse != PARSE_DONE) {
      errStr = "Malformed From URL";
      goto MAP_ERROR;
    }

    map_to = bti.paramv[2];
    state = error = 0;
    length = UrlWhack(map_to, &origLength);
    map_to_start = map_to;
    tmp = map_to;

    new_mapping->toURL.create(NULL);
    rparse = new_mapping->toURL.parse_no_path_component_breakdown(tmp, length);
    map_to_start[origLength] = '\0';    // Unwhack

    if (rparse != PARSE_DONE) {
      errStr = "Malformed To URL";
      goto MAP_ERROR;
    }

    fromScheme = new_mapping->fromURL.scheme_get(&fromSchemeLen);
    // If the rule is "/" or just some other relative path
    //   we need to default the scheme to http
    if (fromScheme == NULL || fromSchemeLen == 0) {
      new_mapping->fromURL.scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
      fromScheme = new_mapping->fromURL.scheme_get(&fromSchemeLen);
      new_mapping->wildcard_from_scheme = true;
    }
    toScheme = new_mapping->toURL.scheme_get(&toSchemeLen);

    // Include support for HTTPS scheme
    // includes support for FILE scheme
    if ((fromScheme != URL_SCHEME_HTTP && fromScheme != URL_SCHEME_HTTPS &&
         fromScheme != URL_SCHEME_FTP && fromScheme != URL_SCHEME_FILE &&
         fromScheme != URL_SCHEME_RTSP && fromScheme != URL_SCHEME_TUNNEL &&
         fromScheme != URL_SCHEME_MMS && fromScheme != URL_SCHEME_MMSU &&
         fromScheme != URL_SCHEME_MMST) ||
        (toScheme != URL_SCHEME_HTTP && toScheme != URL_SCHEME_HTTPS &&
         toScheme != URL_SCHEME_FTP && toScheme != URL_SCHEME_RTSP &&
         toScheme != URL_SCHEME_TUNNEL && toScheme != URL_SCHEME_MMS &&
         toScheme != URL_SCHEME_MMSU && toScheme != URL_SCHEME_MMST)) {
      errStr = "Only http, https, ftp, rtsp, mms, and tunnel remappings are supported";
      goto MAP_ERROR;
    }
    // Check if a tag is specified.
    if (bti.paramv[3] != NULL) {
      if (maptype == FORWARD_MAP_REFERER) {
        new_mapping->filter_redirect_url = xstrdup(bti.paramv[3]);
        if (!strcasecmp(bti.paramv[3], "<default>") || !strcasecmp(bti.paramv[3], "default") ||
            !strcasecmp(bti.paramv[3], "<default_redirect_url>") || !strcasecmp(bti.paramv[3], "default_redirect_url"))
          new_mapping->default_redirect_url = true;
        new_mapping->redir_chunk_list = redirect_tag_str::parse_format_redirect_url(bti.paramv[3]);
        for (int j = bti.paramc; j > 4; j--) {
          if (bti.paramv[j - 1] != NULL) {
            char refinfo_error_buf[1024];
            bool refinfo_error = false;

            ri =
              NEW(new
                  referer_info((char *) bti.paramv[j - 1], &refinfo_error, refinfo_error_buf,
                               sizeof(refinfo_error_buf)));
            if (refinfo_error) {
              ink_snprintf(errBuf, sizeof(errBuf), "%s Incorrect Referer regular expression \"%s\" at line %d - %s",
                           modulePrefix, bti.paramv[j - 1], cln + 1, refinfo_error_buf);
              SignalError(errBuf, alarm_already);
              delete ri;
              ri = 0;
            }

            if (ri && ri->negative) {
              if (ri->any) {
                new_mapping->optional_referer = true;   /* referer header is optional */
                delete ri;
                ri = 0;
              } else {
                new_mapping->negative_referer = true;   /* we have negative referer in list */
              }
            }
            if (ri) {
              ri->next = new_mapping->referer_list;
              new_mapping->referer_list = ri;
            }
          }
        }
      } else {
        new_mapping->tag = xstrdup(&(bti.paramv[3][0]));
      }
    }
    // Check to see the fromHost remapping is a relative one
    fromHost = new_mapping->fromURL.host_get(&fromHostLen);
    if (fromHost == NULL || fromHostLen <= 0) {
      if (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER) {
        if (*map_from_start != '/') {
          errStr = "Relative remappings must begin with a /";
          goto MAP_ERROR;
        } else {
          fromHost = "";
          fromHostLen = 0;
        }
      } else {
        errStr = "Remap source in reverse mappings requires a hostname";
        goto MAP_ERROR;
      }
    }

    toHost = new_mapping->toURL.host_get(&toHostLen);
    if (toHost == NULL || toHostLen <= 0) {
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


    /* 
       commenting these out
     */
    // RemoveTrailingSlash(&new_mapping->toURL);
    // RemoveTrailingSlash(&new_mapping->fromURL);


    if (unlikely(fromHostLen >= (int) sizeof(fromHost_lower_buf))) {
      if (unlikely((fromHost_lower = (fromHost_lower_ptr = (char *) xmalloc(fromHostLen + 1))) == NULL)) {
        fromHost_lower = &fromHost_lower_buf[0];
        fromHostLen = (int) (sizeof(fromHost_lower_buf) - 1);
      }
    } else {
      fromHost_lower = &fromHost_lower_buf[0];
    }
    // Canonicalize the hostname by making it lower case
    memcpy(fromHost_lower, fromHost, fromHostLen);
    fromHost_lower[fromHostLen] = 0;
    LowerCaseStr(fromHost_lower);


    switch (maptype) {
    case FORWARD_MAP:
    case FORWARD_MAP_REFERER:
      TableInsert(lookup_table, new_mapping, fromHost_lower);
      num_rules_forward++;
      SetHomePageRedirectFlag(new_mapping);
      break;
    case REVERSE_MAP:
      TableInsert(reverse_table, new_mapping, fromHost_lower);
      num_rules_reverse++;
      new_mapping->homePageRedirect = false;
      break;
    case PERMANENT_REDIRECT:
      TableInsert(permanent_redirect_table, new_mapping, fromHost_lower);
      num_rules_redirect_permanent++;
      break;
    case TEMPORARY_REDIRECT:
      TableInsert(temporary_redirect_table, new_mapping, fromHost_lower);
      num_rules_redirect_temporary++;
      break;
    default:
      break;
    };

    // If a TS receives a request on a port which is set to tunnel mode
    // (ie, blind forwarding) and a client connects directly to the TS,
    // then the TS will use its IPv4 address and remap rules given
    // to send the request to its proper destination.
    // See HttpTransact::HandleBlindTunnel().
    // Therefore, for a remap rule like "map tunnel://hostname..."
    // in remap.config, we also needs to convert hostname to its IPv4 addr
    // and gives a new remap rule with the IPv4 addr.
    if ((maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER) &&
        fromScheme == URL_SCHEME_TUNNEL && (fromHost_lower[0]<'0' || fromHost_lower[0]> '9')) {
      ink_gethostbyname_r_data d;
      struct hostent *h;
      h = ink_gethostbyname_r(fromHost_lower, &d);
      // We only handle IPv4 addresses which are 4-byte long.
      if ((h != NULL) && (h->h_length == 4)) {
        char ipv4_name[128];
        url_mapping *u_mapping;
        for (int i = 0; h->h_addr_list[i] != NULL; i++) {
          ipv4_name[0] = '\0';
          int tmp = ink_snprintf(ipv4_name, sizeof(ipv4_name), "%d.%d.%d.%d",
                                 (unsigned char) h->h_addr_list[i][0],
                                 (unsigned char) h->h_addr_list[i][1],
                                 (unsigned char) h->h_addr_list[i][2],
                                 (unsigned char) h->h_addr_list[i][3]);
          if (tmp > 0 && tmp < (int) sizeof(ipv4_name)) {       // Create a new url mapping with the IPv4 address.
            u_mapping = NEW(new url_mapping);
            u_mapping->fromURL.create(NULL);
            u_mapping->fromURL.copy(&new_mapping->fromURL);
            u_mapping->fromURL.host_set(ipv4_name, strlen(ipv4_name));
            u_mapping->toURL.create(NULL);
            u_mapping->toURL.copy(&new_mapping->toURL);
            if (bti.paramv[3] != NULL)
              u_mapping->tag = xstrdup(&(bti.paramv[3][0]));
            TableInsert(lookup_table, u_mapping, ipv4_name);
            num_rules_forward++;
            SetHomePageRedirectFlag(u_mapping);
          }
        }
      }
    }

    // For a remap rule like "map mms://proxy.com/ mms://origin_server/"
    // or "reverse_map mms://origin_server/ mms://proxy.com/",
    // we convert proxy.com to its IPv4 addr and gives a new remap rule
    // with the IPv4 addr.
    if ((maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER) &&
        (fromScheme == URL_SCHEME_MMS ||
         (fromScheme == URL_SCHEME_HTTP && new_mapping->tag && strncmp(new_mapping->tag, "WMT", 3) == 0)) &&
        (fromHost_lower[0]<'0' || fromHost_lower[0]> '9')) {
      ink_gethostbyname_r_data d;
      struct hostent *h;
      h = ink_gethostbyname_r(fromHost_lower, &d);
      // We only handle IPv4 addresses which are 4-byte long.
      if ((h != NULL) && (h->h_length == 4)) {
        char ipv4_name[128];
        url_mapping *u_mapping;
        for (int i = 0; h->h_addr_list[i] != NULL; i++) {
          ipv4_name[0] = '\0';
          int tmp;
          tmp = ink_snprintf(ipv4_name, sizeof(ipv4_name), "%d.%d.%d.%d",
                             (unsigned char) h->h_addr_list[i][0],
                             (unsigned char) h->h_addr_list[i][1],
                             (unsigned char) h->h_addr_list[i][2], (unsigned char) h->h_addr_list[i][3]);
          if (tmp > 0 && tmp < (int) sizeof(ipv4_name)) {
            // Create a new url mapping with the IPv4 address.
            u_mapping = NEW(new url_mapping);
            u_mapping->fromURL.create(NULL);
            u_mapping->fromURL.copy(&new_mapping->fromURL);
            u_mapping->fromURL.host_set(ipv4_name, strlen(ipv4_name));
            u_mapping->toURL.create(NULL);
            u_mapping->toURL.copy(&new_mapping->toURL);
            if (bti.paramv[3] != NULL)
              u_mapping->tag = xstrdup(&(bti.paramv[3][0]));
            TableInsert(lookup_table, u_mapping, ipv4_name);
            num_rules_forward++;
            SetHomePageRedirectFlag(u_mapping);
          }
        }
      }
    }
    if (maptype == REVERSE_MAP && fromScheme == URL_SCHEME_MMS && (toHost[0]<'0' || toHost[0]> '9')) {
      ink_gethostbyname_r_data d;
      struct hostent *h;
      char *toHost_null_ended = xstrndup(toHost, toHostLen);

      h = ink_gethostbyname_r(toHost_null_ended, &d);
      // We only handle IPv4 addresses which are 4-byte long.
      if ((h != NULL) && (h->h_length == 4)) {
        char ipv4_name[128];
        url_mapping *u_mapping;
        for (int i = 0; h->h_addr_list[i] != NULL; i++) {
          ipv4_name[0] = '\0';
          int tmp;
          tmp = ink_snprintf(ipv4_name, sizeof(ipv4_name), "%d.%d.%d.%d",
                             (unsigned char) h->h_addr_list[i][0],
                             (unsigned char) h->h_addr_list[i][1],
                             (unsigned char) h->h_addr_list[i][2], (unsigned char) h->h_addr_list[i][3]);
          if (tmp > 0 && tmp < (int) sizeof(ipv4_name)) {       // Create a new url mapping with the IPv4 address.
            u_mapping = NEW(new url_mapping);
            u_mapping->fromURL.create(NULL);
            u_mapping->fromURL.copy(&new_mapping->fromURL);
            u_mapping->toURL.create(NULL);
            u_mapping->toURL.copy(&new_mapping->toURL);
            u_mapping->toURL.host_set(ipv4_name, strlen(ipv4_name));
            if (bti.paramv[3] != NULL)
              u_mapping->tag = xstrdup(&(bti.paramv[3][0]));
            TableInsert(reverse_table, u_mapping, fromHost_lower);
            num_rules_reverse++;
            u_mapping->homePageRedirect = false;
          }
        }
      }
      xfree(toHost_null_ended);
    }
#if 0
    char *dFrom = new_mapping->fromURL.string_get(NULL);
    char *dTo = new_mapping->toURL.string_get(NULL);

    Debug("url_rewrite", "[BuildTable] Added From URL \"%s\"", dFrom);
    Debug("url_rewrite", "[BuildTable] Added To URL \"%s\"", dTo);

    xfree(dFrom);
    xfree(dTo);
#endif

    // Check "remap" plugin options and load .so object
    if ((bti.remap_optflg & REMAP_OPTFLG_PLUGIN) != 0 && (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER)) {
      if ((check_remap_option(bti.argv, bti.argc, REMAP_OPTFLG_PLUGIN, &tok_count) & REMAP_OPTFLG_PLUGIN) != 0) {
        int plugin_found_at = 0;

        // this loads the first plugin
        if (load_remap_plugin(bti.argv, bti.argc, new_mapping, errStrBuf, sizeof(errStrBuf), 0, &plugin_found_at)) {
          Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
          Debug("url_rewrite", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
          ink_error(errStrBuf);
          errStr = errStrBuf;
          goto MAP_ERROR;
        }
        //this loads any subsequent plugins (if present)
        while (plugin_found_at) {
          int ret = load_remap_plugin(bti.argv, bti.argc, new_mapping, errStrBuf, sizeof(errStrBuf), plugin_found_at,
                                      &plugin_found_at);
          if (ret) {
            Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
            Debug("url_rewrite", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
            ink_error(errStrBuf);
            errStr = errStrBuf;
            goto MAP_ERROR;
          }
        }
      }
    }

    if (unlikely(fromHost_lower_ptr))
      fromHost_lower_ptr = (char *) xfree_null(fromHost_lower_ptr);

    cur_line = tokLine(NULL, &tok_state);
    cln++;
    continue;

  MAP_ERROR:
    ink_snprintf(errBuf, sizeof(errBuf), "%s %s at line %d", modulePrefix, errStr, cln + 1);
    SignalError(errBuf, alarm_already);
    if (new_mapping) {
      delete new_mapping;
      new_mapping = NULL;
    }
    cur_line = tokLine(NULL, &tok_state);
    cln++;
    return 1;
  }                             /* end of while(cur_line != NULL) */

  clear_xstr_array(bti.paramv, sizeof(bti.paramv) / sizeof(char *));
  clear_xstr_array(bti.argv, sizeof(bti.argv) / sizeof(char *));
  bti.paramc = (bti.argc = 0);

  // Add the mapping for backdoor urls if enabled.
  // This needs to be before the default PAC mapping for ""
  // since this is more specific
  if (unlikely(backdoor_enabled)) {
    new_mapping = SetupBackdoorMapping();
    TableInsert(lookup_table, new_mapping, "");
    num_rules_forward++;
  }
  // Add the default mapping to the manager PAC file
  //  if we need it
  if (default_to_pac) {
    new_mapping = SetupPacMapping();
    TableInsert(lookup_table, new_mapping, "");
    num_rules_forward++;
  }
  // Destroy unused tables
  if (num_rules_forward == 0) {
    lookup_table = ink_hash_table_destroy(lookup_table);
  } else {
    if (ink_hash_table_isbound(lookup_table, "")) {
      nohost_rules = 1;
    }
  }

  if (num_rules_reverse == 0) {
    reverse_table = ink_hash_table_destroy(reverse_table);
  }

  if (num_rules_redirect_permanent == 0) {
    permanent_redirect_table = ink_hash_table_destroy(permanent_redirect_table);
  }

  if (num_rules_redirect_temporary == 0) {
    temporary_redirect_table = ink_hash_table_destroy(temporary_redirect_table);
  }

  xfree(file_buf);

  CreateLookupHelper(lookup_table);
  CreateLookupHelper(reverse_table);
  CreateLookupHelper(permanent_redirect_table);
  CreateLookupHelper(temporary_redirect_table);

  return 0;
}

/**
  Create lookup helper info inside first url_mapping in hash list.
  Since remap pattern can be different for each host we should adjust
  lookup parameters for each hash hit list.

*/
void
UrlRewrite::CreateLookupHelper(InkHashTable * h_table)
{
  InkHashTableEntry *ht_entry;
  InkHashTableIteratorState ht_iter;
  url_mapping *um_root, *um, *ul, **uppe, **uppu;
  ums_helper *lh;
  const char *from_path;
  int from_path_len, i;

  if (h_table) {
    for (ht_entry = ink_hash_table_iterator_first(h_table, &ht_iter); ht_entry != NULL;) {
      if (likely((um_root = (url_mapping *) ink_hash_table_entry_value(h_table, ht_entry)) != NULL)) {
        for (um = um_root->next; um; um = um->next) {
          from_path = um->fromURL.scheme_get(&i);
          for (ul = um_root; ul; ul = ul->next_root_schema) {
            if (ul->fromURL.scheme_get(&from_path_len) == from_path && i == from_path_len)
              break;
          }
          if (likely(ul)) {
            for (uppu = &ul->next_schema; *uppu; uppu = &((*uppu)->next_schema));
          } else {
            for (uppu = &um_root->next_root_schema; *uppu; uppu = &((*uppu)->next_root_schema));
          }
          *uppu = um;
        }
        // create lookup helper for each scheme
        for (um = um_root; um; um = um->next_root_schema) {
          if (unlikely(um->lookup_helper)) {
            delete um->lookup_helper;
            um->lookup_helper = NULL;
          }
          um->lookup_helper = (lh = NEW(new ums_helper()));
          lh->min_path_size = 1024 * 256;

          uppe = &lh->empty_list;
          uppu = &lh->unique_list;

          for (ul = um; ul; ul = ul->next_schema) {
            // #1. check unique flag
            if (ul->unique) {
              *uppu = ul;
              uppu = &ul->next_unique;
            }
            // #2. check min & max path size
            if (NULL == (from_path = ul->fromURL.path_get(&from_path_len))) {
              *uppe = ul;
              uppe = &ul->next_empty;
              from_path_len = 0;
            }
            if (lh->max_path_size < from_path_len)
              lh->max_path_size = from_path_len;
            if (lh->min_path_size > from_path_len) {
              lh->min_path_size = from_path_len;
            }
            if (unlikely(lh->min_path_size > lh->max_path_size))
              lh->min_path_size = lh->max_path_size;
            if (ul->tag)
              lh->tag_present = true;
            lh->map_cnt++;
          }
          // create hash table only if empty and unique lists are empty
          if (!lh->empty_list && !lh->unique_list && lh->min_path_size > 0 && lh->map_cnt > 1) {
            if (lh->init_hash_table() && lh->load_hash_table(um) && lh->hash_table->max_hit_level > 3) {
              lh->delete_hash_table();
              lh->init_hash_table(lh->map_cnt << 1);
              lh->load_hash_table(um);
            }
          }
        }
      }
      ht_entry = ink_hash_table_iterator_next(h_table, &ht_iter);
    }
  }
}

/**
  Inserts arg mapping in h_table with key src_host chaining the mapping
  of existing entries bound to src_host if necessary.

*/
void
UrlRewrite::TableInsert(InkHashTable * h_table, url_mapping * mapping, char *src_host)
{
  char src_host_tmp_buf[1];
  url_mapping *ht_contents;

  if (!src_host) {
    src_host = &src_host_tmp_buf[0];
    src_host_tmp_buf[0] = 0;
  }
  // Insert the new_mapping into hash table
  if (ink_hash_table_lookup(h_table, src_host, (void **) &ht_contents)) {       // There is already a mapping so chain the entries
    //   Be sure to preserve the order of the file so that we can resolve
    //    conflicting directives
    if (ht_contents != NULL) {
      for (; ht_contents->next; ht_contents = ht_contents->next);
      (ht_contents->next = mapping)->next = NULL;
    }
  } else {
    ink_hash_table_insert(h_table, src_host, mapping);
  }
}

url_mapping_ext *
UrlRewrite::forwardTableLookupExt(URL * request_url, int request_port, const char *request_host, int host_len,
                                  char *tag)
{
  if (lookup_table) {
    url_mapping *m = TableLookup(lookup_table, request_url, request_port,
                                 request_host, host_len, tag);

    return NEW(new url_mapping_ext(m));
  }
  return NULL;
}


url_mapping_ext *
UrlRewrite::reverseTableLookupExt(URL * request_url, int request_port, const char *request_host, int host_len,
                                  char *tag)
{
  if (reverse_table) {
    url_mapping *m = TableLookup(reverse_table, request_url, request_port,
                                 request_host, host_len, tag);

    return NEW(new url_mapping_ext(m));
  }
  return NULL;
}



int
UrlRewrite::load_remap_plugin(char *argv[], int argc, url_mapping * mp, char *errbuf, int errbufsize, int jump_to_argc,
                              int *plugin_found_at)
{
  *plugin_found_at = 0;
  const char *plugin_default_path = "/home/trafficserver/libexec/yts/";
  TSREMAP_INTERFACE ri;
  struct stat stat_buf;
  remap_plugin_info *pi;
  char *c, *err, tmpbuf[2048], *parv[1024], default_path[1024];
  char *new_argv[1024];
  int idx = 0, retcode = 0;
  int parc = 0;

  memset(parv, 0, sizeof(parv));
  memset(new_argv, 0, sizeof(new_argv));

  tmpbuf[0] = 0;
  memset(default_path, 0, 1024);

  if (jump_to_argc != 0) {
    argc -= jump_to_argc;
    int i = 0;
    while (argv[i + jump_to_argc]) {
      new_argv[i] = argv[i + jump_to_argc];
      i++;
    }
    argv = new_argv;
    if (!check_remap_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx)) {
      return -1;
    }
  } else {
    if (unlikely(!mp || (check_remap_option(argv, argc, REMAP_OPTFLG_PLUGIN, &idx) & REMAP_OPTFLG_PLUGIN) == 0)) {
      ink_snprintf(errbuf, errbufsize, "Can't find remap plugin keyword or \"url_mapping\" is NULL");
      return -1;                /* incorrect input data - almost impossible case */
    }
  }

  if (unlikely((c = strchr(argv[idx], (int) '=')) == NULL || !(*(++c)))) {
    ink_snprintf(errbuf, errbufsize, "Can't find remap plugin file name in \"@%s\"", argv[idx]);
    return -2;                  /* incorrect input data */
  }

  if (strlen(c) + strlen(plugin_default_path) > 1023) {
    Debug("remap_plugin", "way too large a path specified for remap plugin");
  }

  strncat(default_path, plugin_default_path, strlen(plugin_default_path));
  strncat(default_path, c, strlen(c));
  default_path[strlen(c) + strlen(plugin_default_path)] = '\0';

  Debug("remap_plugin", "attempting to stat default plugin path: %s", default_path);

  if (stat(&default_path[0], &stat_buf) == 0) {
    Debug("remap_plugin", "stat successful on %s using that", default_path);
    c = &default_path[0];
  } else if (stat(c, &stat_buf) != 0) {
    ink_snprintf(errbuf, errbufsize, "Can't find remap plugin file \"%s\"", c);
    return -3;                  /* incorrect input data */
  }

  Debug("remap_plugin", "using path %s for plugin", c);

  if (!remap_pi_list || (pi = remap_pi_list->find_by_path(c)) == 0) {
    pi = NEW(new remap_plugin_info(c));
    if (!remap_pi_list) {
      remap_pi_list = pi;
    } else {
      remap_pi_list->add_to_list(pi);
    }
    Debug("remap_plugin", "New remap plugin info created for \"%s\"", c);

    if ((pi->dlh = dlopen(c, RTLD_NOW)) == NULL) {
      err = dlerror();
      ink_snprintf(errbuf, errbufsize, "Can't load plugin \"%s\" - %s", c, err ? err : "Unknown dlopen() error");
      return -4;
    }
    pi->fp_tsremap_init = (_tsremap_init *) dlsym(pi->dlh, TSREMAP_FUNCNAME_INIT);
    pi->fp_tsremap_done = (_tsremap_done *) dlsym(pi->dlh, TSREMAP_FUNCNAME_DONE);
    pi->fptsremap_new_instance = (_tsremap_new_instance *) dlsym(pi->dlh, TSREMAP_FUNCNAME_NEW_INSTANCE);
    pi->fp_tsremap_delete_instance = (_tsremap_delete_instance *) dlsym(pi->dlh, TSREMAP_FUNCNAME_DELETE_INSTANCE);
    pi->fp_tsremap_remap = (_tsremap_remap *) dlsym(pi->dlh, TSREMAP_FUNCNAME_REMAP);
    pi->fp_tsremap_os_response = (_tsremap_os_response *) dlsym(pi->dlh, TSREMAP_FUNCNAME_OS_RESPONSE);

    if (!pi->fp_tsremap_init) {
      ink_snprintf(errbuf, errbufsize, "Can't find \"%s\" function in remap plugin \"%s\"", TSREMAP_FUNCNAME_INIT, c);
      retcode = -10;
    } else if (!pi->fptsremap_new_instance) {
      ink_snprintf(errbuf, errbufsize, "Can't find \"%s\" function in remap plugin \"%s\"",
                   TSREMAP_FUNCNAME_NEW_INSTANCE, c);
      retcode = -11;
    } else if (!pi->fp_tsremap_remap) {
      ink_snprintf(errbuf, errbufsize, "Can't find \"%s\" function in remap plugin \"%s\"", TSREMAP_FUNCNAME_REMAP, c);
      retcode = -12;
    }
    if (retcode) {
      if (errbuf && errbufsize > 0)
        Debug("remap_plugin", "%s", errbuf);
      dlclose(pi->dlh);
      pi->dlh = NULL;
      return retcode;
    }
    memset(&ri, 0, sizeof(ri));
    ri.size = sizeof(ri);
    ri.tsremap_version = TSREMAP_VERSION;
    ri.fp_tsremap_interface = NULL;     /* we don't need it now */

    if ((retcode = pi->fp_tsremap_init(&ri, tmpbuf, sizeof(tmpbuf) - 1)) != 0) {
      Error("Failed to initialize plugin %s (non-zero retval) ... bailing out", pi->path);
      exit(-1);                 //see my comment re: exit() about 60 lines down
      ink_snprintf(errbuf, errbufsize, "Remap plugin initialization error - %d:%s", retcode,
                   tmpbuf[0] ? tmpbuf : "Unknown error");
      dlclose(pi->dlh);
      pi->dlh = NULL;
      return -20;
    }
    Debug("remap_plugin", "Remap plugin \"%s\" - initialization completed", c);
  }

  if (!pi->dlh) {
    ink_snprintf(errbuf, errbufsize, "Can't load plugin \"%s\"", c);
    return -5;
  }

  if ((err = mp->fromURL.string_get(NULL)) == NULL) {
    ink_snprintf(errbuf, errbufsize, "Can't load fromURL from URL class");
    return -6;
  }
  parv[parc++] = xstrdup(err);
  xfree(err);

  if ((err = mp->toURL.string_get(NULL)) == NULL) {
    ink_snprintf(errbuf, errbufsize, "Can't load toURL from URL class");
    return -6;
  }
  parv[parc++] = xstrdup(err);
  xfree(err);

  bool plugin_encountered = false;
  // how many plugin parameters we have for this remapping
  for (idx = 0; idx < argc && parc < (int) ((sizeof(parv) / sizeof(char *)) - 1); idx++) {

    if (plugin_encountered && !strncasecmp("plugin=", argv[idx], 7) && argv[idx][7]) {
      *plugin_found_at = idx;
      break;                    //if there is another plugin, lets deal with that later
    }

    if (!strncasecmp("plugin=", argv[idx], 7)) {
      plugin_encountered = true;
    }

    if (!strncasecmp("pparam=", argv[idx], 7) && argv[idx][7]) {
      parv[parc++] = &(argv[idx][7]);
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

  ihandle *ih = mp->get_another_instance(pi);
  Debug("remap_plugin", "creating new plugin instance");
  retcode = pi->fptsremap_new_instance(parc, parv, ih, tmpbuf, sizeof(tmpbuf) - 1);
  Debug("remap_plugin", "done creating new plugin instance");

  xfree(parv[0]);               // fromURL
  xfree(parv[1]);               // toURL

  if (retcode != 0) {
    mp->delete_instance(pi);
    ink_snprintf(errbuf, errbufsize, "Can't create new remap instance for plugin \"%s\" - %s", c,
                 tmpbuf[0] ? tmpbuf : "Unknown plugin error");
    Error("Failed to create new instance for plugin %s (non-zero retval)... bailing out", pi->path);
                /**
		 * fail here, otherwise we *will* fail later
		 * and that's some jacked backtrace inside CreateTableLookup [see bug 2316658]
		 * at least this one will be obvious
		 * We *really* don't want to continue when a plugin failed to init. We can't 
		 * guarantee we are remapping what the user thought we were going to remap.
		 * using something nice like exit() would be more ideal, but this should be 
		 * caught in development, anyway.
     **/
    exit(-1);
    return -6;
  }

  mp->add_plugin(pi);

  return 0;
}
