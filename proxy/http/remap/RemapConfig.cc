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

#include <algorithm>

#include "RemapConfig.h"
#include "UrlRewrite.h"
#include "ReverseProxy.h"
#include "tscore/I_Layout.h"
#include "hdrs/HTTP.h"
#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "tscore/ink_cap.h"
#include "tscore/ts_file.h"
#include "tscore/BufferWriter.h"
#include "tscore/bwf_std_format.h"
#include "IPAllow.h"

#define modulePrefix "[ReverseProxy]"

using ts::BufferWriter;
using ts::TextView;
using std::string_view;

std::function<void(char const *)> load_remap_file_cb;

namespace
{
// Set of valid schema for the from / original URL. This must be initialized after the WKS
// table.
std::vector<char const *> VALID_SCHEMA;

bool remap_parse_config_bti(ts::file::path const &path, RemapBuilder &&bti);

inline void
ConfigError(TextView msg, bool &flag)
{
  if (!flag) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, msg);
    flag = true;
    Error("%.*s", static_cast<int>(msg.size()), msg.data());
  }
}

/**
  Returns the length of the URL.

  Will replace the terminator with a '/' if this is a full URL and
  there are no '/' in it after the the host.  This ensures that class
  URL parses the URL correctly.

*/
int
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

bool
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

} // namespace

RemapArg::RemapArg(ts::TextView key, ts::TextView data)
{
  auto spot = std::find_if(Args.begin(), Args.end(), [=](auto const &d) -> bool { return key == d.name; });
  if (spot != Args.end()) {
    tag   = key;
    value = data;
    type  = spot->type;
  }
}

// Is this needed anymore?
TextView
RemapBuilder::localize(TextView token)
{
  auto span = rewrite->arena.alloc(token.size() + 1);
  memcpy(span.data(), token.data(), token.size());
  span.end()[-1] = '\0';
  return {span.begin(), token.size()};
}

void
RemapBuilder::clear()
{
  paramv.resize(0);
  argv.resize(0);
  arena.clear();
}

RemapFilter *
RemapBuilder::find_filter(TextView name)
{
  auto spot = std::find_if(filters.begin(), filters.end(), [=](auto const &filter) { return filter.name == name; });
  return spot != filters.end() ? &(*spot) : nullptr;
}

TextView
RemapBuilder::parse_define_directive(TextView directive, RemapBuilder::ErrBuff &errw)
{
  if (paramv.size() < 2 || paramv[1].empty()) {
    errw.print("Directive \"{}\" must have name parameter", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  if (argv.size() < 1) {
    errw.print("Directive \"{}\" must have filter argument(s)", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  auto filter = this->find_filter(paramv[1]);
  if (filter) {
    errw.print("Redefinition of filter '{}'", paramv[1]);
    return errw.view();
  }
  filter = new RemapFilter;
  filter->set_name(paramv[1]);
  for (auto const &arg : argv) {
    filter->add_arg({arg.type, this->localize(arg.tag), this->localize(arg.value)});
  }
  filters.append(filter);

  return {};
}

TextView
RemapBuilder::parse_delete_directive(TextView directive, ErrBuff &errw)
{
  if (paramv.size() < 2) {
    errw.print("Directive '{}' must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  if (auto spot = this->find_filter(paramv[1]); spot) {
    this->filters.erase(spot);
  }
  return {};
}

TextView
RemapBuilder::parse_activate_directive(TextView directive, ErrBuff &errw)
{
  if (paramv.size() < 2) {
    errw.print("Directive '{}' must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  // This is a reserved name that means enable IP allow configuration checks.
  if (REMAP_FILTER_IP_ALLOW_TAG == paramv[1]) {
    ip_allow_check_enabled_p = true;
    return {};
  }

  auto filter = this->find_filter(paramv[1]);
  if (nullptr == filter) {
    errw.print("Directive '}|' - ilter '{}' not found", directive, paramv[1]);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  active_filters.push_back(filter);
  return nullptr;
}

TextView
RemapBuilder::parse_deactivate_directive(TextView directive, ErrBuff &errw)
{
  if (paramv.size() < 2) {
    errw.print("Directive '{}' must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  // This is a reserved name that means enable IP allow configuration checks.
  if (REMAP_FILTER_IP_ALLOW_TAG == paramv[1]) {
    ip_allow_check_enabled_p = false;
    return {};
  }

  auto filter = this->find_filter(paramv[1]);
  if (nullptr == filter) {
    errw.print("Directive '{}' - ilter '{}' not found", directive, paramv[1]);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  auto spot = std::find_if(active_filters.rbegin(), active_filters.rend(), [=](auto f) -> bool { return f == filter; });
  if (spot != active_filters.rend()) {
    active_filters.erase(spot.base());
  }
  return {};
}

TextView
RemapBuilder::parse_remap_fragment(ts::file::path const &path, RemapBuilder::ErrBuff &errw)
{
  // Need a child builder to avoid clobbering state in @a this builder. The filters need to be
  // loaned to the child so that global filters can be used and new filters are available later.
  RemapBuilder builder{rewrite};
  bool success;

  // Move it over for now.
  builder.filters = std::move(this->filters);

  Debug("url_rewrite", "[%s] including remap configuration from %s", __func__, path.c_str());
  success = builder.parse_config(path);

  // Child is done, bring back the filters.
  this->filters = std::move(builder.filters);

  if (success) {
    // register the included file with the management subsystem so that we can correctly
    // reload them when they change
    load_remap_file_cb(path.c_str());
  } else {
    errw.print("Failed to parse file '{}'", path);
    return errw.view();
  }

  return {};
}

TextView
RemapBuilder::parse_include_directive(TextView directive, ErrBuff &errw)
{
  TextView err_msg;

  if (paramv.size() < 2) {
    errw.print("Directive '{}' must have path", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  for (unsigned i = 1; i < paramv.size() && err_msg.empty(); ++i) {
    ts::file::path path(paramv[i]);
    if (path.is_relative()) {
      path = ts::file::path(Layout::get()->sysconfdir) / path;
    }

    std::error_code ec;
    auto stat = ts::file::status(path, ec);
    if (ec) {
      errw.print("Directive '{}' - unable to access file '{}'", directive, path);
      Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
      return errw.view();
    }

    if (ts::file::is_dir(stat)) {
      struct dirent **entrylist = nullptr;
      int n_entries;

      n_entries = scandir(path.c_str(), &entrylist, nullptr, alphasort);
      if (n_entries == -1) {
        errw.print("Failed to open '{}' {}", path, ts::bwf::Errno(errno));
        return errw.view();
      }

      for (int j = 0; j < n_entries; ++j) {
        if (isdot(entrylist[j]->d_name) || isdotdot(entrylist[j]->d_name)) {
          continue;
        }

        ts::file::path subpath{path / entrylist[j]->d_name};
        free(entrylist[j]->d_name); // clean up now, we have a local copy.

        auto substatus{ts::file::status(subpath, ec)};

        if (ec) {
          errw.print("Failed to load remap include file '{}' {}", path, ec);
          err_msg = errw.view();
          break;
        }

        if (ts::file::is_dir(substatus)) {
          continue;
        }

        err_msg = parse_remap_fragment(subpath, errw);
        if (!err_msg.empty()) {
          break;
        }
      }

      free(entrylist);

    } else {
      err_msg = this->parse_remap_fragment(path, errw);
    }
  }

  return err_msg;
}

struct remap_directive {
  TextView name;
  TextView (RemapBuilder::*parser)(TextView, RemapBuilder::ErrBuff &);
};

static const std::array<remap_directive, 16> directives = {{

  {".definefilter", &RemapBuilder::parse_define_directive},
  {".deffilter", &RemapBuilder::parse_define_directive},
  {".defflt", &RemapBuilder::parse_define_directive},

  {".deletefilter", &RemapBuilder::parse_delete_directive},
  {".delfilter", &RemapBuilder::parse_delete_directive},
  {".delflt", &RemapBuilder::parse_delete_directive},

  {".usefilter", &RemapBuilder::parse_activate_directive},
  {".activefilter", &RemapBuilder::parse_activate_directive},
  {".activatefilter", &RemapBuilder::parse_activate_directive},
  {".useflt", &RemapBuilder::parse_activate_directive},

  {".unusefilter", &RemapBuilder::parse_deactivate_directive},
  {".deactivatefilter", &RemapBuilder::parse_deactivate_directive},
  {".unactivefilter", &RemapBuilder::parse_deactivate_directive},
  {".deuseflt", &RemapBuilder::parse_deactivate_directive},
  {".unuseflt", &RemapBuilder::parse_deactivate_directive},

  {".include", &RemapBuilder::parse_include_directive},
}};

TextView
RemapBuilder::parse_directive(ErrBuff &errw)
{
  auto token{paramv[0]};

  // Check arguments
  if (paramv.empty()) {
    Debug("url_rewrite", "[parse_directive] Invalid argument(s)");
    return "Invalid argument(s)";
  }

  for (auto const &directive : directives) {
    if (strcasecmp(token, directive.name) == 0) {
      return directive.parser(token, *this, errw);
    }
  }

  errw.print("Remap: unknown directive \"{}\"", token);
  Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
  return errw.view();
}

TextView
RemapBuilder::load_plugins(ErrBuff & errw)
{
  TSRemapInterface ri;
  struct stat stat_buf;
  RemapPluginInfo *pi;
  char *c, *err, tmpbuf[2048], default_path[PATH_NAME_MAX];
  const char *new_argv[1024];
  char *parv[1024];
  int parc = 0;

  std::vector<char const*> plugin_argv;
  unsigned idx = 0;
  while (idx < argv.size()) {
    RemapArg const& arg { argv[idx] };
    ++idx;

    if (! RemapArg::PLUGIN == arg.type) {
      continue; // not a plugin, move on.
    }

    if (arg.value.empty()) {
      errw.print("Plugin file name not found for argument '{}'", arg.tag);
      return errw.view();
    }

    // First check if the file path is in the arg list.
    ts::file::path path { arg.value };
    if (path.is_relative()) {
      path = ts::file::path(Layout::get()->sysconfdir) / path;
    }
    // Is the file accessible?
    std::error_code ec;
    auto file_stat { ts::file::status(path, ec) };
    if (ec) {
      errw.print("Plugin file '{}' - error {}", path, ec);
      return errw.view();
    }

    // Next sequence of @c PLUGIN_PARM args are arguments for this plugin.
    // For each one, localize to a BW for C string compatibility.
    ts::LocalBufferWriter<2048> arg_text;
    plugin_argv.resize(0);
    while ( idx < argv.size() && RemapArg::PLUGIN_PARAM == argv[idx].type ) {
      char const* localized_arg = arg_text.data();
      arg_text.write(argv[idx].value).write('\0');
      if (arg_text.error()) {
        errw.print("Plugin parameter list too long at arg '{}'", argv[idx].value);
        return errw.view();
      }
      plugin_argv.push_back(localized_arg);
      ++idx;
    }

    Debug("remap_plugin", "Loading plugin from %s", path.c_str());

  if ((pi = RemapPluginInfo::find_by_path(path.c_str())) == nullptr) {
    pi = new RemapPluginInfo(std::move(path));
    RemapPluginInfo::add_to_list(pi);
    Debug("remap_plugin", "New remap plugin info created for \"%s\"", c);

    {
      uint32_t elevate_access = 0;
      REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
      ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

      if ((pi->dl_handle = dlopen(c, RTLD_NOW)) == nullptr) {
#if defined(freebsd) || defined(openbsd)
        err = (char *)dlerror();
#else
        err = dlerror();
#endif
        errw.print("Failed to load plugin \"{}\" - {}", pi->m_path, ts::bwf::FirstOf(err, "*Unknown dlopen() error");
        return errw.view();
      }
      pi->init_cb          = reinterpret_cast<RemapPluginInfo::Init_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_INIT));
      pi->config_reload_cb = reinterpret_cast<RemapPluginInfo::Reload_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_CONFIG_RELOAD));
      pi->done_cb          = reinterpret_cast<RemapPluginInfo::Done_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_DONE));
      pi->new_instance_cb =
        reinterpret_cast<RemapPluginInfo::New_Instance_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_NEW_INSTANCE));
      pi->delete_instance_cb =
        reinterpret_cast<RemapPluginInfo::Delete_Instance_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_DELETE_INSTANCE));
      pi->do_remap_cb    = reinterpret_cast<RemapPluginInfo::Do_Remap_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_DO_REMAP));
      pi->os_response_cb = reinterpret_cast<RemapPluginInfo::OS_Response_F *>(dlsym(pi->dl_handle, TSREMAP_FUNCNAME_OS_RESPONSE));

      if (!pi->init_cb) {
        snprintf(errbuf, errbufsize, R"(Can't find "%s" function in remap plugin "%s")", TSREMAP_FUNCNAME_INIT, c);
        retcode = -10;
      } else if (!pi->new_instance_cb && pi->delete_instance_cb) {
        snprintf(errbuf, errbufsize,
                 R"(Can't find "%s" function in remap plugin "%s" which is required if "%s" function exists)",
                 TSREMAP_FUNCNAME_NEW_INSTANCE, c, TSREMAP_FUNCNAME_DELETE_INSTANCE);
        retcode = -11;
      } else if (!pi->do_remap_cb) {
        snprintf(errbuf, errbufsize, R"(Can't find "%s" function in remap plugin "%s")", TSREMAP_FUNCNAME_DO_REMAP, c);
        retcode = -12;
      } else if (pi->new_instance_cb && !pi->delete_instance_cb) {
        snprintf(errbuf, errbufsize,
                 R"(Can't find "%s" function in remap plugin "%s" which is required if "%s" function exists)",
                 TSREMAP_FUNCNAME_DELETE_INSTANCE, c, TSREMAP_FUNCNAME_NEW_INSTANCE);
        retcode = -13;
      }
      if (retcode) {
        if (errbuf && errbufsize > 0) {
          Debug("remap_plugin", "%s", errbuf);
        }
        dlclose(pi->dl_handle);
        pi->dl_handle = nullptr;
        return retcode;
      }
      memset(&ri, 0, sizeof(ri));
      ri.size            = sizeof(ri);
      ri.tsremap_version = TSREMAP_VERSION;

      if (pi->init_cb(&ri, tmpbuf, sizeof(tmpbuf) - 1) != TS_SUCCESS) {
        snprintf(errbuf, errbufsize, "Failed to initialize plugin \"%s\": %s", pi->path.c_str(),
                 tmpbuf[0] ? tmpbuf : "Unknown plugin error");
        return -5;
      }
    } // done elevating access
    Debug("remap_plugin", "Remap plugin \"%s\" - initialization completed", c);
  }

  if (!pi->dl_handle) {
    errw.print(R"(Failed to load plugin "{}")", c);
    return errw.view();
  }

  if ((err = mp->fromURL.string_get(nullptr)) == nullptr) {
    snprintf(errbuf, errbufsize, "Can't load fromURL from URL class");
    return -7;
  }
  parv[parc++] = ats_strdup(err);

  if ((err = mp->toURL.string_get(nullptr)) == nullptr) {
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

  Debug("url_rewrite", "Viewing parsed plugin parameters for %s: [%d]", pi->path.c_str(), *plugin_found_at);
  for (int k = 0; k < parc; k++) {
    Debug("url_rewrite", "Argument %d: %s", k, parv[k]);
  }

  Debug("remap_plugin", "creating new plugin instance");

  void *ih         = nullptr;
  TSReturnCode res = TS_SUCCESS;
  if (pi->new_instance_cb) {
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

    res = pi->new_instance_cb(parc, parv, &ih, tmpbuf, sizeof(tmpbuf) - 1);
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

  to_host = new_mapping->toURL.host_get(&to_host_len);
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
  str                               = new_mapping->toURL.host_get(&str_index); // reusing str and str_index
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

bool
RemapBuilder::parse_config(ts::file::path const &path, UrlRewrite *rewriter)
{
  self_type builder{rewriter};
  ts::LocalBufferWriter<1024> errw; // Used as the error buffer later.
  TextView errMsg;                  // error message returned from other parsing logic.
  unsigned line_no = 0;             // current line #

  bool alarm_already = false;

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
  TextView type_id_str;

  // Read the entire file.
  std::error_code ec;
  std::string content{ts::file::load(path, ec)};
  if (ec) {
    errw.print("Error: '{}' while loading '{}'", ec, path);
    Warning("%.*s", static_cast<int>(errw.size()), errw.data());
    return false;
  }

  Debug("url_rewrite", "[BuildTable] UrlRewrite::BuildTable()");

  // Parse the file contents.
  TextView text{content};
  bool continued_p = false; // line is marked continued on next line.
  while (text) {
    auto line = text.take_prefix_at('\n').trim_if(&isspace);
    ++line_no;

    if (line.empty()) {
      continued_p = false;
      continue;
    } else if ('#' == *line) {
      continue; // Don't change continuation line state to allow comment between continued lines.
    }

    // If it's a continued line, don't reset the builder state, otherwise prep for the next
    // effective line.
    if (!continued_p) {
      builder.clear();
    }

    if ('\\' == line.back()) {
      continued_p = true;
      line.remove_suffix(1).rtrim_if(&isspace);
    } else {
      continued_p = false;
    }

    reg_map     = nullptr;
    new_mapping = nullptr;
    errw.reset();

    Debug("url_rewrite", "[BuildTable] Parsing: \"%.*s\"", static_cast<int>(line.size()), line.data());
    // Note these are stored as pointers in to the file content buffer, which is transient. It is
    // expected copies will be made by the @C RemapFilter instances for permanence.
    while (!line.ltrim_if(&isspace).empty()) {
      auto token = line.take_prefix_if(&isspace);

      if ('@' == *token) {
        ++token;
        if (!token.empty()) {
          auto value = token; // do it backwards because the first '=' is the separator.
          auto key   = value.take_prefix_at('=');
          RemapArg arg{key, value};
          if (arg.type != RemapArg::INVALID) {
            builder.argv.emplace_back(arg);
          } else {
            errw.print("Error: '{}' on line {} is not valid remap rule argument", token, line_no);
            Warning("%.*s", static_cast<int>(errw.size()), errw.data());
            return false;
          }
        }
      } else {
        builder.paramv.push_back(token);
      }
    }

    // Did the basic tokenizing - if it's continued, go to the next line and parse more tokens.
    // Put any validity checks or processing off until the final line.
    if (continued_p) {
      continue;
    }

    // If only tokens, it must be a filter command or it's malformed.
    if (builder.paramv.size() < 1 || (builder.paramv.size() < 3 && '.' != builder.paramv[0][0])) {
      errw.print("Remap config: line {} is malformed in file '{}'", line_no, path);
      goto MAP_ERROR;
    }

    // Check directive keywords (starting from '.')
    if (builder.paramv[0][0] == '.') {
      if (!(errMsg = builder.parse_directive(errw)).empty())
        goto MAP_ERROR;
    }
    continue;
  }

  is_cur_mapping_regex = REMAP_REGEX_PREFIX.isNoCasePrefixOf(builder.paramv[0]);
  type_id_str          = builder.paramv[0].substr(is_cur_mapping_regex ? REMAP_REGEX_PREFIX.size() : 0);

  // Check to see whether is a reverse or forward mapping
  if (0 == strcasecmp(REMAP_REVERSE_MAP_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - REVERSE_MAP");
    maptype = REVERSE_MAP;
  } else if (0 == strcasecmp(REMAP_MAP_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - %s", builder.option.flag.map_with_referrer ? "FORWARD_MAP_REFERER" : "FORWARD_MAP");
    maptype = builder.option.flag.map_with_referrer ? FORWARD_MAP_REFERER : FORWARD_MAP;
  } else if (0 == strcasecmp(REMAP_REDIRECT_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - PERMANENT_REDIRECT");
    maptype = PERMANENT_REDIRECT;
  } else if (0 == strcasecmp(REMAP_TEMPORARY_REDIRECT_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - TEMPORARY_REDIRECT");
    maptype = TEMPORARY_REDIRECT;
  } else if (0 == strcasecmp(REMAP_WITH_REFERER_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - FORWARD_MAP_REFERER");
    maptype = FORWARD_MAP_REFERER;
  } else if (0 == strcasecmp(REMAP_WITH_RECV_PORT_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - FORWARD_MAP_WITH_RECV_PORT");
    maptype = FORWARD_MAP_WITH_RECV_PORT;
  } else {
    errw.print("unknown mapping type '{}' at line {}", type_id_str, line_no);
    errMsg = errw.view();
    goto MAP_ERROR;
  }

  std::unique_ptr<url_mapping> new_mapping{new url_mapping};

  // apply filter rules if we have to
  if (!errMsg = builder.process_filter_opt(new_mapping.get(), errw).empty()) {
    goto MAP_ERROR;
  }

  // update sticky flag
  builder.accept_check_p = builder.accept_check_p && buidler.ip_allow_check_enabled_p;

  new_mapping->map_id = 0;
  if (builder.option.flag.map_id) {
    int idx = 0;
    char *c;
    int ret = remap_check_option((const char **)bti->argv, bti->argc, REMAP_OPTFLG_MAP_ID, &idx);
    if (ret & REMAP_OPTFLG_MAP_ID) {
      c                   = strchr(bti->argv[idx], (int)'=');
      new_mapping->map_id = (unsigned int)atoi(++c);
    }
  }

  map_from = builder.paramv[1];
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

  map_to       = builder.paramv[2];
  length       = UrlWhack(map_to, &origLength);
  map_to_start = map_to;
  tmp          = map_to;

  new_mapping->toURL.create(nullptr);
  rparse                   = new_mapping->toURL.parse_no_path_component_breakdown(tmp, length);
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
  toScheme = new_mapping->toURL.scheme_get(&toSchemeLen);

  // Include support for HTTPS scheme
  // includes support for FILE scheme
  if (std::none_of(VALID_SCHEMA.begin(), VALID_SCHEMA.end(), [](auto wks) -> bool { return wks == fromScheme; }) ||
      std::none_of(VALID_SCHEMA.begin(), VALID_SCHEMA.end(), [](auto wks) -> bool { return wks == toScheme; })) {
    errStr = "only http, https, ws, wss, and tunnel remappings are supported";
    goto MAP_ERROR;
  }

  // If mapping from WS or WSS we must map out to WS or WSS
  if ((fromScheme == URL_SCHEME_WSS || fromScheme == URL_SCHEME_WS) && (toScheme != URL_SCHEME_WSS && toScheme != URL_SCHEME_WS)) {
    errStr = "WS or WSS can only be mapped out to WS or WSS.";
    goto MAP_ERROR;
  }

  // Check if a tag is specified.
  auto tag = builder.paramv[3];
  if (!tag.empty) {
    if (maptype == FORWARD_MAP_REFERER) {
      new_mapping->filter_redirect_url = ats_strdup(tag);
      if (0 == strcasecmp(tag, "<default>") || 0 == strcasecmp(tag, "default") || 0 == strcasecmp(tag, "<default_redirect_url>") ||
          0 == strcasecmp(tag, "default_redirect_url")) {
        new_mapping->default_redirect_url = true;
      }
      new_mapping->redir_chunk_list = redirect_tag_str::parse_format_redirect_url(tag);
      for (auto spot = build.paramv.rbegin(), limit = build.paramv.rend() - 3; spot < limit; ++spot) {
        if (!spot->empty()) {
          ts::LocalBufferWriter<1024> ref_errw;
          bool refinfo_error = false;

          ri = new referer_info(*spot, &refinfo_error, ref_errw);
          if (refinfo_error) {
            errw.print("{} Incorrect Referer regular expression \"{}\" at line {} - {}", modulePrefix, builder.paramv[j - 1],
                       line_no, ref_errw.view());
            ConfigError(errw, alarm_already);
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
      new_mapping->tag = ats_strdup(tag);
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

  toHost = new_mapping->toURL.host_get(&toHostLen);
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

  if (unlikely(fromHostLen >= static_cast<int>(sizeof(fromHost_lower_buf)))) {
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
          u_mapping->toURL.create(nullptr);
          u_mapping->toURL.copy(&new_mapping->toURL);

          if (builder.paramv[3] != nullptr) {
            u_mapping->tag = ats_strdup(&(builder.paramv[3][0]));
          }

          if (!builder.rewrite->InsertForwardMapping(maptype, u_mapping, ipb)) {
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
  if ((builder.remap_optflg & REMAP_OPTFLG_PLUGIN) != 0 &&
      (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT)) {
    if ((remap_check_option((const char **)builder.argv, builder.argc, REMAP_OPTFLG_PLUGIN, &tok_count) & REMAP_OPTFLG_PLUGIN) !=
        0) {
      int plugin_found_at = 0;
      int jump_to_argc    = 0;

      // this loads the first plugin
      if (remap_load_plugin((const char **)builder.argv, builder.argc, new_mapping, errStrBuf, sizeof(errStrBuf), 0,
                            &plugin_found_at)) {
        Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
        errStr = errStrBuf;
        goto MAP_ERROR;
      }
      // this loads any subsequent plugins (if present)
      while (plugin_found_at) {
        jump_to_argc += plugin_found_at;
        if (remap_load_plugin((const char **)builder.argv, builder.argc, new_mapping, errStrBuf, sizeof(errStrBuf), jump_to_argc,
                              &plugin_found_at)) {
          Debug("remap_plugin", "Remap plugin load error - %s", errStrBuf[0] ? errStrBuf : "Unknown error");
          errStr = errStrBuf;
          goto MAP_ERROR;
        }
      }
    }
  }

  // Now add the mapping to appropriate container
  if (!builder.rewrite->InsertMapping(maptype, new_mapping, reg_map, fromHost_lower, is_cur_mapping_regex)) {
    errStr = "unable to add mapping rule to lookup table";
    goto MAP_ERROR;
  }

  fromHost_lower_ptr = (char *)ats_free_null(fromHost_lower_ptr);

  continue;

// Deal with error / warning scenarios
MAP_ERROR:

  snprintf(errBuf, sizeof(errBuf), "%s failed to add remap rule at %s line %d: %s", modulePrefix, path, cln + 1, errStr);
  ConfigError(errBuf, alarm_already);

  delete reg_map;
  return false;
} /* end of while(cur_line != nullptr) */

IpAllow::enableAcceptCheck(builder.accept_check_p);
return true;
}

bool
remap_parse_config(const char *path, UrlRewrite *rewrite)
{
  // Convenient place to check for valid schema init - doesn't happen often, and don't need it before
  // this.
  if (VALID_SCHEMA.empty()) {
    VALID_SCHEMA.push_back(URL_SCHEME_HTTP);
    VALID_SCHEMA.push_back(URL_SCHEME_HTTPS);
    VALID_SCHEMA.push_back(URL_SCHEM_FILE);
    VALID_SCHEMA.push_back(URL_SCHEME_TUNNEL);
  }
  // If this happens to be a config reload, the list of loaded remap plugins is non-empty, and we
  // can signal all these plugins that a reload has begun.
  RemapPluginInfo::indicate_reload();
  return RemapBuilder{rewrite}.parse_config(ts::file::path{path}, rewrite);
}
