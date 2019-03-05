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
#include "tscpp/util/PostScript.h"
#include "RemapYAMLBuilder.h"

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

inline void
ConfigError(TextView msg, bool &flag)
{
  if (!flag) {
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, msg);
    flag = true;
    Error("%.*s", static_cast<int>(msg.size()), msg.data());
  }
}

inline void
log_warning(TextView msg)
{
  Warning("%.*s", static_cast<int>(msg.size()), msg.data());
}

inline void
log_warning(ts::FixedBufferWriter const &msg)
{
  log_warning(msg.view());
}

} // namespace

RemapArg::RemapArg(ts::TextView key, ts::TextView data)
{
  auto spot = std::find_if(ArgTags.begin(), ArgTags.end(), [=](auto const &d) -> bool { return key == d.name; });
  if (spot != ArgTags.end()) {
    tag   = key;
    value = data;
    type  = spot->type;
  }
}

void
RemapBuilder::clear()
{
  paramv.resize(0);
  argv.resize(0);
  arg_types.reset();
}

RemapFilter *
RemapBuilder::find_filter(TextView name)
{
  auto spot = std::find_if(filters.begin(), filters.end(), [=](auto const &filter) { return filter.name == name; });
  return spot != filters.end() ? &(*spot) : nullptr;
}

TextView
RemapBuilder::normalize_url(RemapBuilder::TextView url)
{
  bool add_separator_p = false;
  // If it's a proxy URL and there's no separator after the host, add one.
  if (auto pos = url.find("://"_sv); pos != url.npos) {
    if (auto post_host_pos = url.substr(pos + 3).find('/'); post_host_pos == url.npos) {
      add_separator_p = true;
    }
  }
  auto url_size = url.size();
  if (add_separator_p) {
    ++url_size;
  }

  // Now localize it, with the trailing slash if needed.
  auto span = rewriter->arena.alloc(url_size + 1);
  memcpy(span.data(), url.data(), url.size());
  span.end()[-1] = '\0';
  if (add_separator_p) {
    span.end()[-2] = '/';
  }
  return {span.begin(), url_size};
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
  ts::PostScript cleanup([=]() -> void { delete filter; });
  filter->set_name(paramv[1]);
  // Add the arguments for the filter.
  for (auto const &arg : argv) {
    auto err_msg = filter->add_arg({arg.type, this->rewriter->localize(arg.tag), this->rewriter->localize(arg.value)}, errw);
    if (!err_msg.empty()) {
      return err_msg;
    }
  }

  cleanup.release();
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

  // Reverse precedence! This is the first match, at the end.
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
  RemapBuilder builder{rewriter};
  bool success;

  // Move it over for now.
  builder.filters = std::move(this->filters);

  Debug("url_rewrite", "[%s] including remap configuration from %s", __func__, path.c_str());
  success = builder.parse_config(path, rewriter);

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
    errw.print("No arguments provided for directive '{}'", token);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  for (auto const &directive : directives) {
    if (strcasecmp(token, directive.name) == 0) {
      return (this->*(directive.parser))(token, errw);
    }
  }

  errw.print(R"(Remap: unknown directive "{}")", token);
  Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
  return errw.view();
}

TextView
RemapBuilder::load_plugins(url_mapping *mp, ErrBuff &errw)
{
  TSRemapInterface ri;
  RemapPluginInfo *pi;
  std::vector<char const *> plugin_argv;
  unsigned idx = 0;
  char plugin_err_buff[2048];

  while (idx < argv.size()) {
    RemapArg const &arg{argv[idx++]};

    if (RemapArg::PLUGIN != arg.type) {
      continue; // not a plugin, move on.
    }

    if (arg.value.empty()) {
      errw.print("Plugin file name not found for argument '{}'", arg.tag);
      return errw.view();
    }

    ts::file::path path{arg.value};
    if (path.is_relative()) {
      path = ts::file::path(Layout::get()->sysconfdir) / path;
    }
    // Is the file accessible?
    std::error_code ec;
    auto file_stat{ts::file::status(path, ec)};
    if (ec) {
      errw.print("Plugin file '{}' - error {}", path, ec);
      return errw.view();
    }

    // Next sequence of @c PLUGIN_PARM args are arguments for this plugin.
    // For each one, localize to a BW for C string compatibility.
    ts::LocalBufferWriter<2048> arg_text;
    plugin_argv.resize(0);

    // From URL is first.
    plugin_argv.push_back(arg_text.data());
    auto pos = arg_text.extent();
    arg_text.print("{}", mp->fromURL);
    if (pos == arg_text.extent()) {
      errw.write("Can't load fromURL from URL class");
      return errw.view();
    }

    // Next is to URL.
    plugin_argv.push_back(arg_text.data());
    pos = arg_text.extent();
    arg_text.print("{}", mp->toUrl);
    if (pos == arg_text.extent()) {
      errw.write("Can't load toURL from URL class");
      return errw.view();
    }

    // Then any explicit plugin arguments, but only those directly following the plugin arg.
    while (idx < argv.size() && RemapArg::PLUGIN_PARAM == argv[idx].type) {
      char const *localized_arg = arg_text.data();
      arg_text.write(argv[idx].value).write('\0');
      if (arg_text.error()) {
        errw.print("Plugin parameter list too long at arg '{}'", argv[idx].value);
        return errw.view();
      }
      plugin_argv.push_back(localized_arg);
      ++idx;
    }

    Debug("remap_plugin", "Loading plugin from %s", path.c_str());

    if ((pi = RemapPluginInfo::find_by_path(path.view())) == nullptr) {
      pi = new RemapPluginInfo(std::move(path));
      RemapPluginInfo::add_to_list(pi);
      Debug("remap_plugin", "New remap plugin info created for \"%s\"", pi->path.c_str());

      // Load the plugin library and find the entry points.
      {
        auto err_pos            = errw.extent();
        uint32_t elevate_access = 0;
        REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
        ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

        if ((pi->dl_handle = dlopen(pi->path.c_str(), RTLD_NOW)) == nullptr) {
          auto dl_err_text = dlerror();
          errw.print(R"(Failed to load plugin "{}" - {})", pi->path, ts::bwf::FirstOf(dl_err_text, "*Unknown dlopen() error"));
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
          errw.print(R"(Can't find "{}" function in remap plugin "{}")", TSREMAP_FUNCNAME_INIT, pi->path);
        } else if (!pi->new_instance_cb && pi->delete_instance_cb) {
          errw.print(R"(Can't find "{}" function in remap plugin "{}" which is required if "{}" function exists)",
                     TSREMAP_FUNCNAME_NEW_INSTANCE, pi->path, TSREMAP_FUNCNAME_DELETE_INSTANCE);
        } else if (!pi->do_remap_cb) {
          errw.print(R"(Can't find "{}" function in remap plugin "{}")", TSREMAP_FUNCNAME_DO_REMAP, pi->path);
        } else if (pi->new_instance_cb && !pi->delete_instance_cb) {
          errw.print(R"(Can't find "{}" function in remap plugin "{}" which is required if "{}" function exists)",
                     TSREMAP_FUNCNAME_DELETE_INSTANCE, pi->path, TSREMAP_FUNCNAME_NEW_INSTANCE);
        }
        if (errw.extent() != err_pos) {
          Debug("remap_plugin", "%.*s", static_cast<int>(errw.size()), errw.data());
          dlclose(pi->dl_handle);
          pi->dl_handle = nullptr;
          return errw.view();
        }
        ink_zero(ri);
        ri.size            = sizeof(ri);
        ri.tsremap_version = TSREMAP_VERSION;

        plugin_err_buff[0] = '\0';
        if (pi->init_cb(&ri, plugin_err_buff, sizeof plugin_err_buff) != TS_SUCCESS) {
          errw.print(R"("Failed to initialize plugin "{}": {})", pi->path,
                     ts::bwf::FirstOf(plugin_err_buff, "Unknown plugin error"));
          return errw.view();
        }
      } // done elevating access
      Debug("remap_plugin", R"(Remap plugin "%s" - initialization completed)", pi->path.c_str());
    }

    if (!pi->dl_handle) {
      errw.print(R"(Failed to load plugin "{}")", pi->path);
      return errw.view();
    }

    if (is_debug_tag_set("url_rewrite")) {
      Debug("url_rewrite", "Viewing all parameters for config line");
      ts::LocalBufferWriter<2048> lw;
      int i = 0;
      lw.write("Rule args: ");
      pos = lw.extent();
      for (auto const &arg : argv) {
        if (pos != lw.extent()) {
          lw.write(", ");
        }
        lw.print("{}: {}={}", ++i, arg.tag, arg.value);
      }
      Debug("url_rewrite", "%.*s", static_cast<int>(lw.size()), lw.data());

      lw.reset();
      i = 0;
      lw.print("Plugin '{}' args:", path);
      pos = lw.extent();
      for (auto arg : plugin_argv) {
        if (pos != lw.extent()) {
          lw.write(", ");
        }
        lw.print("{}: {}", ++i, arg);
      }
      Debug("url_rewrite", "%.*s", static_cast<int>(lw.size()), lw.data());
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

      plugin_err_buff[0] = '\0';
      res                = pi->new_instance_cb(plugin_argv.size(), const_cast<char **>(plugin_argv.data()), &ih, plugin_err_buff,
                                sizeof(plugin_err_buff));
    }

    Debug("remap_plugin", "done creating new plugin instance");

    if (res != TS_SUCCESS) {
      errw.print(R"(Failed to create instance for plugin "{}": {})", pi->path,
                 ts::bwf::FirstOf(plugin_err_buff, "Unknown plugin error"));
      return errw.view();
    }

    mp->add_plugin(pi, ih);
  }

  return 0;
}
/** will process the regex mapping configuration and create objects in
    output argument reg_map. It assumes existing data in reg_map is
    inconsequential and will be perfunctorily null-ed;
*/
TextView
RemapBuilder::parse_regex_mapping(RemapBuilder::TextView fromHost, url_mapping *mapping, UrlRewrite::RegexMapping *rxp_map,
                                  ErrBuff &errw)
{
  int substitution_id;
  int substitution_count = 0;
  int captures;

  rxp_map->to_url_host_template.clear();
  rxp_map->n_substitutions = 0;
  rxp_map->url_map         = mapping;

  ts::PostScript cleanup([&]() -> void { rxp_map->to_url_host_template.clear(); });

  if (rxp_map->regular_expression.compile(fromHost.data()) == false) {
    errw.print("pcre_compile failed! Regex has error starting at '{}'", fromHost);
    return errw.view();
  }

  captures = rxp_map->regular_expression.get_capture_count();
  if (captures == -1) {
    errw.write("pcre_fullinfo failed!");
    return errw.view();
  }
  if (captures >= UrlRewrite::MAX_REGEX_SUBS) { // off by one for $0 (implicit capture)
    errw.print("regex has %{} capturing subpatterns (including entire regex); Max allowed: {}", captures + 1,
               UrlRewrite::MAX_REGEX_SUBS);
    return errw.view();
  }

  auto to_host = mapping->toURL.host_get();
  for (unsigned i = 0; i < to_host.size() - 1; ++i) {
    if (to_host[i] == '$') {
      if (substitution_count > UrlRewrite::MAX_REGEX_SUBS) {
        errw.print("Cannot have more than %d substitutions in mapping with host [{}]", UrlRewrite::MAX_REGEX_SUBS, fromHost);
        return errw.view();
      }
      substitution_id = to_host[i + 1] - '0';
      if ((substitution_id < 0) || (substitution_id > captures)) {
        errw.print("Substitution id [{}] has no corresponding capture pattern in regex [{}]", to_host[i + 1], fromHost);
        return errw.view();
      }
      rxp_map->substitution_markers[rxp_map->n_substitutions] = i;
      rxp_map->substitution_ids[rxp_map->n_substitutions]     = substitution_id;
      ++rxp_map->n_substitutions;
    }
  }

  // so the regex itself is stored in fromURL.host; string to match will be in the request; string
  // to use for substitutions will be in this buffer. Does this need to be localized, or is @c toUrl
  // sufficiently stable?
  rxp_map->to_url_host_template = mapping->toURL.host_get();
  return {};
}

bool
RemapBuilder::parse_config(ts::file::path const &path, UrlRewrite *rewriter)
{
  ts::LocalBufferWriter<1024> errw; // Used as the error buffer later.
  TextView errMsg;                  // error message returned from other parsing logic.
  unsigned line_no = 0;             // current line #
  self_type builder{rewriter};

  bool alarm_already = false;

  // Vars to build the mapping
  const char *fromScheme, *toScheme;
  int fromSchemeLen, toSchemeLen;
  url_mapping *new_mapping = nullptr;
  mapping_type maptype;
  UrlRewrite::RegexMapping *rxp_map = nullptr;
  TextView tag; // Rule tag, if any.

  bool is_cur_mapping_regex_p;
  TextView type_id_str;

  // Read the entire file.
  std::error_code ec;
  std::string content{ts::file::load(path, ec)};
  if (ec) {
    log_warning(errw.print(R"(Error: '{}' while loading '{}')", ec, path));
    return false;
  }

  if (ts::TextView{path.view()}.take_suffix_at('.') == "yaml" || ts::TextView::npos != content.find(RemapYAMLBuilder::ROOT_TAG)) {
    auto result = RemapYAMLBuilder::parse(rewriter, content);
    return result.is_ok();
  }

  Debug("url_rewrite", "[BuildTable] UrlRewrite::BuildTable()");

  // If something goes wrong, log the error and clean up.
  ts::PostScript cleanup([&]() -> void {
    ts::LocalBufferWriter<1024> lw;
    lw.print(R"("{}" failed to add remap rule from "{}" at line {} : {})", modulePrefix, path, line_no, errw.view());
    ConfigError(lw.view(), alarm_already);
    delete new_mapping;
    delete rxp_map;
  });

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
            builder.arg_types[arg.type] = true;
          } else {
            errw.print("Error: '{}' on line {} is not valid remap rule argument", token, line_no);
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
      return false;
    }

    // Check directive keywords (starting from '.')
    if (builder.paramv[0][0] == '.') {
      if (!(errMsg = builder.parse_directive(errw)).empty()) {
        return false;
      }
      continue;
    }
  }

  is_cur_mapping_regex_p = REMAP_REGEX_PREFIX.isNoCasePrefixOf(builder.paramv[0]);
  type_id_str            = builder.paramv[0].substr(is_cur_mapping_regex_p ? REMAP_REGEX_PREFIX.size() : 0);

  // Check to see whether is a reverse or forward mapping
  if (0 == strcasecmp(REMAP_REVERSE_MAP_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - REVERSE_MAP");
    maptype = REVERSE_MAP;
  } else if (0 == strcasecmp(REMAP_MAP_TAG, type_id_str)) {
    Debug("url_rewrite", "[BuildTable] - FORWARD_MAP");
    maptype = FORWARD_MAP;
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
    return false;
  }

  new_mapping = new url_mapping;

  // Set up direct filters.
  if ((builder.arg_types & builder.FILTER_TYPES).any()) {
    RemapFilter *f = nullptr;
    for (auto const &arg : builder.argv) {
      if (arg.type == RemapArg::ACTION) {
        f            = new RemapFilter;
        auto err_msg = f->add_arg(arg, errw);
        if (!err_msg.empty()) {
          return false;
        }
        builder.rewriter->filters.append(f);
        new_mapping->filters.push_back(f);
      } else if (builder.FILTER_TYPES[arg.type]) {
        if (nullptr == f) {
          errw.print("remap filter option '@{}={}' without a preceeding '@{}' on line {}- ignored", arg.tag, arg.value,
                     RemapArg::ACTION, line_no);
          log_warning(errw);
          errw.reset();
        }
        auto err_msg = f->add_arg(arg, errw);
        if (!err_msg.empty()) {
          return false;
        }
      }
    }
  }
  // Add active filters - note these go in reverse order, to keep the predence ordering correct.
  // The mapping needs them in precedence order, while @c active_filters is a stack and therefore
  // in reverse precdence order.
  new_mapping->filters.reserve(new_mapping->filters.size() + builder.active_filters.size());
  new_mapping->filters.insert(new_mapping->filters.end(), builder.active_filters.rbegin(), builder.active_filters.rend());

  // update sticky flag
  builder.accept_check_p = builder.accept_check_p && builder.ip_allow_check_enabled_p;

  if (builder.arg_types[RemapArg::MAP_ID]) {
    auto spot =
      std::find_if(builder.argv.begin(), builder.argv.end(), [](auto const &arg) -> bool { return RemapArg::MAP_ID == arg.type; });
    new_mapping->map_id = ts::svtoi(spot->value);
  }

  auto map_from_url = builder.normalize_url(builder.paramv[1]);

  new_mapping->fromURL.create(nullptr);
  auto rparse = new_mapping->fromURL.parse_no_path_component_breakdown(map_from_url.data(), map_from_url.size());

  if (rparse != PARSE_RESULT_DONE) {
    errw.print(R"(Malformed url "{}" in from URL on line {})", map_from_url, line_no);
    return false;
  }

  auto map_to_url = builder.normalize_url(builder.paramv[2]);

  new_mapping->toURL.create(nullptr);
  rparse = new_mapping->toURL.parse_no_path_component_breakdown(map_to_url.data(), map_to_url.size());

  if (rparse != PARSE_RESULT_DONE) {
    errw.print(R"(Malformed url "{}" in from URL on line {})", map_to_url, line_no);
    return false;
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
  if (std::none_of(VALID_SCHEMA.begin(), VALID_SCHEMA.end(), [&](auto wks) -> bool { return wks == fromScheme; }) ||
      std::none_of(VALID_SCHEMA.begin(), VALID_SCHEMA.end(), [&](auto wks) -> bool { return wks == toScheme; })) {
    errw.print("only http, https, ws, wss, and tunnel remappings are supported");
    return false;
  }

  // If mapping from WS or WSS we must map out to WS or WSS
  if ((fromScheme == URL_SCHEME_WSS || fromScheme == URL_SCHEME_WS) && (toScheme != URL_SCHEME_WSS && toScheme != URL_SCHEME_WS)) {
    errw.print("WS or WSS can only be mapped out to WS or WSS.");
    return false;
  }

  // Check if a tag is specified.
  if (builder.paramv.size() >= 4) {
    tag = builder.rewriter->localize(builder.paramv[3]);
    if (maptype == FORWARD_MAP_REFERER) {
      new_mapping->filter_redirect_url = tag;
      if (0 == strcasecmp(tag, "<default>") || 0 == strcasecmp(tag, "default") || 0 == strcasecmp(tag, "<default_redirect_url>") ||
          0 == strcasecmp(tag, "default_redirect_url")) {
        new_mapping->default_redirect_url = true;
      }
      RedirectChunk::parse(new_mapping->filter_redirect_url, new_mapping->redirect_chunks);
      for (auto spot = builder.paramv.rbegin(), limit = builder.paramv.rend() - 3; spot < limit; ++spot) {
        if (!spot->empty()) {
          RefererInfo ri;
          TextView ref_text = rewriter->localize(*spot);
          ts::LocalBufferWriter<1024> ref_errw;

          auto err_msg = ri.parse(ref_text, ref_errw);
          if (err_msg) {
            errw.print("{} Incorrect Referer regular expression \"{}\" at line {} - {}", modulePrefix, ref_text, line_no,
                       ref_errw.view());
            ConfigError(errw.view(), alarm_already);
          }

          if (ri.negative && ri.any) {
            new_mapping->optional_referer = true; /* referer header is optional */
          } else {
            if (ri.negative) {
              new_mapping->negative_referer = true; /* we have negative referer in list */
            }
            new_mapping->referer_list.append(new RefererInfo(std::move(ri)));
          }
        }
      }
    } else { // not a FORWARD_MAP_REFERER
      new_mapping->tag = tag;
    }
  }

  // Check to see the fromHost remapping is a relative one
  auto fromHost = new_mapping->fromURL.host_get();
  if (fromHost.empty()) {
    if (maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT) {
      if (builder.paramv[1][0] != '/') {
        errw.write("relative remappings must begin with a /");
        return false;
      }
    } else {
      errw.write("remap source in reverse mappings requires a hostname");
      return false;
    }
  }

  if (new_mapping->toURL.host_get().empty()) {
    errw.write("The remap destinations require a hostname");
    return false;
  }

  // Get rid of trailing slashes since they interfere
  //  with our ability to send redirects
  // You might be tempted to remove these lines but the new
  // optimized header system will introduce problems.  You
  // might get two slashes occasionally instead of one because
  // the rest of the system assumes that trailing slashes have
  // been removed.

  // set the normalized string so nobody else has to normalize this
  new_mapping->fromURL.host_set_lower(fromHost);
  fromHost = new_mapping->fromURL.host_get();

  if (is_cur_mapping_regex_p) {
    ts::LocalBufferWriter<1024> lbw;
    rxp_map      = new UrlRewrite::RegexMapping();
    auto err_msg = builder.parse_regex_mapping({fromHost.data(), fromHost.size()}, new_mapping, rxp_map, lbw);
    if (!err_msg.empty()) {
      errw.print("could not process regex mapping config line - {}", err_msg);
      return false;
    }
    Debug("url_rewrite_regex", "Configured regex rule for host [%.*s]", static_cast<int>(fromHost.size()), fromHost.data());
  }

  // If a TS receives a request on a port which is set to tunnel mode (ie, blind forwarding) and a
  // client connects directly to the TS, then the TS will use its IPv4 address and remap rules given
  // to send the request to its proper destination. See HttpTransact::HandleBlindTunnel().
  // Therefore, for a remap rule like "map tunnel://hostname..." in remap.config, we also needs to
  // convert hostname to its IPv4 addr and gives a new remap rule with the IPv4 addr.

  if ((maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT) &&
      fromScheme == URL_SCHEME_TUNNEL && (fromHost[0] < '0' || fromHost[0] > '9')) {
    addrinfo *ai_records; // returned records.
    ip_text_buffer ipb;   // buffer for address string conversion.
    char *tmp = static_cast<char *>(alloca(fromHost.size() + 1));
    memcpy(tmp, fromHost.data(), fromHost.size());
    tmp[fromHost.size()] = '\0';
    if (0 == getaddrinfo(tmp, nullptr, nullptr, &ai_records)) {
      ts::PostScript ai_cleanup{[=]() -> void { freeaddrinfo(ai_records); }};
      for (addrinfo *ai_spot = ai_records; ai_spot; ai_spot = ai_spot->ai_next) {
        if (ats_is_ip(ai_spot->ai_addr) && !ats_is_ip_any(ai_spot->ai_addr) && ai_spot->ai_protocol == IPPROTO_TCP) {
          url_mapping *u_mapping = new url_mapping;

          ats_ip_ntop(ai_spot->ai_addr, ipb, sizeof ipb);
          u_mapping->fromURL.create(nullptr);
          u_mapping->fromURL.copy(&new_mapping->fromURL);
          u_mapping->fromURL.host_set(ipb, strlen(ipb));
          u_mapping->toURL.create(nullptr);
          u_mapping->toURL.copy(&new_mapping->toURL);

          u_mapping->tag = tag;

          if (!builder.rewriter->InsertForwardMapping(maptype, u_mapping, ipb)) {
            errw.write("unable to add mapping rule to lookup table");
            delete u_mapping;
            return false;
          }
        }
      }
    }
  }

  // Now add the mapping to appropriate container
  // WRONG - fromHost.data() is not null terminated. Need to fix InsertMapping.
  if (!builder.rewriter->InsertMapping(maptype, new_mapping, rxp_map, fromHost.data(), is_cur_mapping_regex_p)) {
    errw.write("unable to add mapping rule to lookup table");
    return false;
  }

  cleanup.release();
  return true;
} /* end of while(cur_line != nullptr) */

bool
remap_parse_config(const char *path, UrlRewrite *rewrite)
{
  // Convenient place to check for valid schema init - doesn't happen often, and don't need it before
  // this. This can't be done at start up because it depends on other initialization logic.
  if (VALID_SCHEMA.empty()) {
    VALID_SCHEMA.push_back(URL_SCHEME_HTTP);
    VALID_SCHEMA.push_back(URL_SCHEME_HTTPS);
    VALID_SCHEMA.push_back(URL_SCHEME_FILE);
    VALID_SCHEMA.push_back(URL_SCHEME_TUNNEL);
  }
  // If this happens to be a config reload, the list of loaded remap plugins is non-empty, and we
  // can signal all these plugins that a reload has begun.
  RemapPluginInfo::indicate_reload();
  return RemapBuilder::parse_config(ts::file::path{path}, rewrite);
}
