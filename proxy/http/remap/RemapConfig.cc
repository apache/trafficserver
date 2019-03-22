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
#include "RemapTextBuilder.h"
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
    key   = key;
    value = data;
    type  = spot->type;
  }
}

void
RemapTextBuilder::clear()
{
  paramv.resize(0);
  argv.resize(0);
  arg_types.reset();
  _stash.discard();
}

TextView
RemapTextBuilder::add_filter_arg(RemapArg const &arg, RemapFilter *filter, RemapTextBuilder::ErrBuff &errw)
{
  switch (arg.type) {
  case RemapArg::METHOD: {
    auto text{arg.value};
    if ('~' == *text) {
      ++text;
      filter->set_method_match_inverted(true);
    }
    while (text) {
      auto method = text.take_prefix_at(";,"_sv);
      if (method.empty()) {
        continue;
      }
      filter->add_method(method);
    }
  } break;
  case RemapArg::PROXY_ADDR: {
    auto range{arg.value};
    bool invert_p = false;
    IpAddr min, max;
    if (*range == '~') {
      invert_p = true;
      ++range;
    }
    if (0 == ats_ip_range_parse(range, min, max)) {
      if (invert_p) {
        filter->mark_proxy_addr_inverted(min, max);
      } else {
        filter->mark_proxy_addr(min, max);
      }
    } else {
      errw.print("malformed IP address '{}' in argument '{}'", range, arg.key);
      return errw.view();
    }
  } break;
  case RemapArg::SRC_ADDR: {
    auto range{arg.value};
    bool invert_p = false;
    IpAddr min, max;
    if (*range == '~') {
      invert_p = true;
      ++range;
    }
    if (0 == ats_ip_range_parse(range, min, max)) {
      if (invert_p) {
        filter->mark_src_addr_inverted(min, max);
      } else {
        filter->mark_src_addr(min, max);
      }
    } else {
      errw.print("malformed IP address '{}' in argument '{}'", range, arg.key);
      return errw.view();
    }
  } break;
  case RemapArg::INTERNAL:
    filter->set_internal_check(RemapFilter::REQUIRE_TRUE);
    break;
  default:
    break;
  }

  return {};
}

TextView
RemapTextBuilder::parse_define_directive(TextView directive, RemapTextBuilder::ErrBuff &errw)
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

  if (this->find_filter(paramv[1])) {
    errw.print("Redefinition of filter '{}'", paramv[1]);
    return errw.view();
  }

  std::unique_ptr<RemapFilter> filter{new RemapFilter};
  filter->set_name(paramv[1]);
  // Add the arguments for the filter.
  for (auto const &arg : argv) {
    auto result = this->add_filter_arg(arg, filter.get(), errw);
    if (!result.empty()) {
      return result;
    }
  }

  _filters.append(filter.release());

  return {};
}

TextView
RemapTextBuilder::parse_delete_directive(TextView directive, ErrBuff &errw)
{
  if (paramv.size() < 2) {
    errw.print("Directive '{}' must have name argument", directive);
    Debug("url_rewrite", "[parse_directive] %.*s", static_cast<int>(errw.size()), errw.data());
    return errw.view();
  }

  if (auto spot = this->find_filter(paramv[1]); spot) {
    this->_filters.erase(spot);
  }
  return {};
}

TextView
RemapTextBuilder::parse_activate_directive(TextView directive, ErrBuff &errw)
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
  _active_filters.push_back(filter);
  return nullptr;
}

TextView
RemapTextBuilder::parse_deactivate_directive(TextView directive, ErrBuff &errw)
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

  auto spot = std::find_if(_active_filters.rbegin(), _active_filters.rend(), [=](auto f) -> bool { return f == filter; });
  if (spot != _active_filters.rend()) {
    _active_filters.erase(spot.base());
  }
  return {};
}

TextView
RemapTextBuilder::parse_remap_fragment(ts::file::path const &path, RemapTextBuilder::ErrBuff &errw)
{
  // Need a child builder to avoid clobbering state in @a this builder. The filters need to be
  // loaned to the child so that global filters can be used and new filters are available later.
  RemapTextBuilder builder{_rewriter};
  bool success;

  // Move it over for now.
  builder._filters = std::move(this->_filters);

  Debug("url_rewrite", "[%s] including remap configuration from %s", __func__, path.c_str());
  success = builder.parse_config(path, _rewriter);

  // Child is done, bring back the filters.
  this->_filters = std::move(builder._filters);

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
RemapTextBuilder::parse_include_directive(TextView directive, ErrBuff &errw)
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
  TextView (RemapTextBuilder::*parser)(TextView, RemapTextBuilder::ErrBuff &);
};

static const std::array<remap_directive, 16> directives = {{

  {".definefilter", &RemapTextBuilder::parse_define_directive},
  {".deffilter", &RemapTextBuilder::parse_define_directive},
  {".defflt", &RemapTextBuilder::parse_define_directive},

  {".deletefilter", &RemapTextBuilder::parse_delete_directive},
  {".delfilter", &RemapTextBuilder::parse_delete_directive},
  {".delflt", &RemapTextBuilder::parse_delete_directive},

  {".usefilter", &RemapTextBuilder::parse_activate_directive},
  {".activefilter", &RemapTextBuilder::parse_activate_directive},
  {".activatefilter", &RemapTextBuilder::parse_activate_directive},
  {".useflt", &RemapTextBuilder::parse_activate_directive},

  {".unusefilter", &RemapTextBuilder::parse_deactivate_directive},
  {".deactivatefilter", &RemapTextBuilder::parse_deactivate_directive},
  {".unactivefilter", &RemapTextBuilder::parse_deactivate_directive},
  {".deuseflt", &RemapTextBuilder::parse_deactivate_directive},
  {".unuseflt", &RemapTextBuilder::parse_deactivate_directive},

  {".include", &RemapTextBuilder::parse_include_directive},
}};

TextView
RemapTextBuilder::parse_directive(ErrBuff &errw)
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
RemapTextBuilder::load_plugins(url_mapping *mp, ErrBuff &errw)
{
  std::vector<char const *> plugin_argv;
  unsigned idx = 0;

  while (idx < argv.size()) {
    RemapArg const &arg{argv[idx++]};

    if (RemapArg::PLUGIN != arg.type) {
      continue; // not a plugin, move on.
    }

    if (arg.value.empty()) {
      errw.print("Plugin file name not found for argument '{}'", arg.key);
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
    arg_text.print("{}", mp->toURL);
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
    auto result = this->load_plugin(mp, std::move(path), plugin_argv.size(), plugin_argv.data());
    if (!result.is_ok()) {
      errw.print("{}", result);
      return errw.view();
    }
  }
  return 0;
}

TextView
RemapTextBuilder::parse_rule(int line_no, ErrBuff &errw)
{
  TextView errMsg; // error message returned from other parsing logic.

  bool alarm_already = false;

  // Vars to build the mapping
  const char *fromScheme, *toScheme;
  int fromSchemeLen, toSchemeLen;
  url_mapping *new_mapping = nullptr;
  mapping_type maptype;
  UrlRewrite::RegexMapping *rxp_map = nullptr;
  TextView tag; // Rule tag, if any.

  // If something goes wrong, log the error and clean up.
  ts::PostScript cleanup([&]() -> void {
    ts::LocalBufferWriter<1024> lw;
    lw.print(R"("{}" failed to add remap rule at line {} : {})", modulePrefix, line_no, errw.view());
    ConfigError(lw.view(), alarm_already);
    delete new_mapping;
    delete rxp_map;
  });

  bool is_cur_mapping_regex_p = REMAP_REGEX_PREFIX.isNoCasePrefixOf(paramv[0]);
  TextView type_id_str        = paramv[0].substr(is_cur_mapping_regex_p ? REMAP_REGEX_PREFIX.size() : 0);

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
    return errw.view();
  }

  new_mapping = new url_mapping;

  // Set up direct filters.
  if ((arg_types & FILTER_TYPES).any()) {
    RemapFilter *f = nullptr;
    for (auto const &arg : argv) {
      if (arg.type == RemapArg::ACTION) {
        f            = new RemapFilter;
        auto err_msg = add_filter_arg(arg, f, errw);
        if (!err_msg.empty()) {
          return err_msg;
        }
        _rewriter->filters.append(f);
        new_mapping->filters.push_back(f);
      } else if (FILTER_TYPES[arg.type]) {
        if (nullptr == f) {
          errw.print("remap filter option '@{}={}' without a preceeding '@{}' on line {}- ignored", arg.key, arg.value,
                     RemapArg::ACTION, line_no);
          log_warning(errw);
          errw.reset();
        }
        auto err_msg = add_filter_arg(arg, f, errw);
        if (!err_msg.empty()) {
          return err_msg;
        }
      }
    }
  }
  // Add active filters - note these go in reverse order, to keep the predence ordering correct.
  // The mapping needs them in precedence order, while @c _active_filters is a stack and therefore
  // in reverse precdence order.
  new_mapping->filters.reserve(new_mapping->filters.size() + _active_filters.size());
  new_mapping->filters.insert(new_mapping->filters.end(), _active_filters.rbegin(), _active_filters.rend());

  // update sticky flag
  accept_check_p = accept_check_p && ip_allow_check_enabled_p;

  if (arg_types[RemapArg::MAP_ID]) {
    auto spot = std::find_if(argv.begin(), argv.end(), [](auto const &arg) -> bool { return RemapArg::MAP_ID == arg.type; });
    new_mapping->map_id = ts::svtoi(spot->value);
  }

  auto map_from_url = normalize_url(paramv[1]);

  new_mapping->fromURL.create(nullptr);
  auto rparse = new_mapping->fromURL.parse_no_path_component_breakdown(map_from_url.data(), map_from_url.size());

  if (rparse != PARSE_RESULT_DONE) {
    errw.print(R"(Malformed url "{}" in from URL on line {})", map_from_url, line_no);
    return errw.view();
  }

  auto map_to_url = normalize_url(paramv[2]);

  new_mapping->toURL.create(nullptr);
  rparse = new_mapping->toURL.parse_no_path_component_breakdown(map_to_url.data(), map_to_url.size());

  if (rparse != PARSE_RESULT_DONE) {
    errw.print(R"(Malformed url "{}" in from URL on line {})", map_to_url, line_no);
    return errw.view();
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
    return errw.view();
  }

  // If mapping from WS or WSS we must map out to WS or WSS
  if ((fromScheme == URL_SCHEME_WSS || fromScheme == URL_SCHEME_WS) && (toScheme != URL_SCHEME_WSS && toScheme != URL_SCHEME_WS)) {
    errw.print("WS or WSS can only be mapped out to WS or WSS.");
    return errw.view();
  }

  // Check if a tag is specified.
  if (paramv.size() >= 4) {
    tag = stash(paramv[3]);
    if (maptype == FORWARD_MAP_REFERER) {
      new_mapping->filter_redirect_url = tag;
      if (0 == strcasecmp(tag, "<default>") || 0 == strcasecmp(tag, "default") || 0 == strcasecmp(tag, "<default_redirect_url>") ||
          0 == strcasecmp(tag, "default_redirect_url")) {
        new_mapping->default_redirect_url = true;
      }
      RedirectChunk::parse(new_mapping->filter_redirect_url, new_mapping->redirect_chunks);
      for (auto spot = paramv.rbegin(), limit = paramv.rend() - 3; spot < limit; ++spot) {
        if (!spot->empty()) {
          RefererInfo ri;
          TextView ref_text = stash(*spot);
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
      if (paramv[1][0] != '/') {
        errw.write("relative remappings must begin with a /");
        return errw.view();
      }
    } else {
      errw.write("remap source in reverse mappings requires a hostname");
      return errw.view();
    }
  }

  if (new_mapping->toURL.host_get().empty()) {
    errw.write("The remap destinations require a hostname");
    return errw.view();
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
    auto result = parse_regex_rewrite(new_mapping, fromHost);
    if (!result.is_ok()) {
      errw.print("could not process regex mapping config line - {}", result.errata());
      return errw.view();
    }
    rxp_map = result;
    Debug("url_rewrite_regex", "Configured regex rule for host [%.*s]", static_cast<int>(fromHost.size()), fromHost.data());
  }

  // If a TS receives a request on a port which is set to tunnel mode (ie, blind forwarding) and a
  // client connects directly to the TS, then the TS will use its IPv4 address and remap rules given
  // to send the request to its proper destination. See HttpTransact::HandleBlindTunnel().
  // Therefore, for a remap rule like "map tunnel://hostname..." in remap.config, we also needs to
  // convert hostname to its IPv4 addr and gives a new remap rule with the IPv4 addr.

  if ((maptype == FORWARD_MAP || maptype == FORWARD_MAP_REFERER || maptype == FORWARD_MAP_WITH_RECV_PORT) &&
      fromScheme == URL_SCHEME_TUNNEL && (fromHost[0] < '0' || fromHost[0] > '9')) {
    auto result = insert_ancillary_tunnel_rules(new_mapping->fromURL, new_mapping->toURL, maptype, tag);
    if (!result.is_ok()) {
      errw.print("{}", result);
      return errw.view();
    }
  }

  // Now add the mapping to appropriate container
  // WRONG - fromHost.data() is not null terminated. Need to fix InsertMapping.
  if (!_rewriter->InsertMapping(maptype, new_mapping, rxp_map, fromHost.data(), is_cur_mapping_regex_p)) {
    errw.write("unable to add mapping rule to lookup table");
    return errw.view();
  }

  cleanup.release();
  return {};
}

bool
RemapTextBuilder::parse_config(ts::file::path const &path, UrlRewrite *rewriter)
{
  ts::LocalBufferWriter<1024> errw; // Used as the error buffer later.
  TextView errMsg;                  // error message returned from other parsing logic.
  unsigned line_no = 0;             // current line #
  self_type builder{rewriter};

  // Read the entire file.
  std::error_code ec;
  std::string content{ts::file::load(path, ec)};
  if (ec) {
    log_warning(errw.print(R"(Error: '{}' while loading '{}')", ec, path));
    return false;
  }

  if (ts::TextView{path.view()}.take_suffix_at('.') == "yaml" || ts::TextView::npos != content.find(RemapYAMLBuilder::ROOT_KEY)) {
    auto result = RemapYAMLBuilder::parse(rewriter, content);
    std::cout << result;
    return result.is_ok();
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

    auto result = builder.parse_rule(line_no, errw);
    if (!result.empty()) {
      return false;
    }
  }

  return true;
}

/* end of while(cur_line != nullptr) */

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
  return RemapTextBuilder::parse_config(ts::file::path{path}, rewrite);
}
