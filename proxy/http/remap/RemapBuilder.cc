/** @file
 *
 *  YAML configuration for URL rewriting.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
 *  agreements.  See the NOTICE file distributed with this work for additional information regarding
 *  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with the License.  You may
 *  obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the
 *  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "RemapBuilder.h"
#include "tscpp/util/PostScript.h"
#include "swoc/bwf_ex.h"
#include "ts_swoc_bwf_aux.h"

RemapBuilder::RemapBuilder(UrlRewrite *url_rewriter) : _rewriter(url_rewriter)
{
  MgmtInt mi = 0;
  REC_ReadConfigInteger(mi, "proxy.config.plugin.load_elevated");
  _load_plugins_elevated_p = (mi != 0);
}

RemapFilter *
RemapBuilder::find_filter(TextView name)
{
  auto spot = std::find_if(_filters.begin(), _filters.end(), [=](auto const &filter) { return filter.name == name; });
  return spot != _filters.end() ? &(*spot) : nullptr;
}

swoc::TextView
RemapBuilder::stash(swoc::TextView view)
{
  auto span = (_stash.alloc(view.size() + 1)).rebind<char>();
  memcpy(span.data(), view.data(), view.size());
  span.end()[-1] = '\0';
  return {span.begin(), view.size()};
}

swoc::TextView
RemapBuilder::stash_lower(swoc::TextView view)
{
  auto span = (_stash.alloc(view.size() + 1)).rebind<char>();
  std::transform(view.begin(), view.end(), span.begin(), &tolower);
  span.end()[-1] = '\0';
  return {span.begin(), view.size()};
}

swoc::TextView
RemapBuilder::normalize_url(TextView url)
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
  auto span = _stash.alloc(url_size + 1).rebind<char>();
  memcpy(span.data(), url.data(), url.size());
  span.end()[-1] = '\0';
  if (add_separator_p) {
    span.end()[-2] = '/';
  }
  return {span.begin(), url_size};
}

swoc::Rv<UrlRewrite::RegexMapping *>
RemapBuilder::parse_regex_rewrite(url_mapping *mapping, TextView target_host)
{
  static constexpr TextView ERROR_PREFIX{"URL rewrite regex mapping -"};
  swoc::Rv<UrlRewrite::RegexMapping *> zret{nullptr};

  std::unique_ptr<UrlRewrite::RegexMapping> regex_rewrite{new UrlRewrite::RegexMapping};

  int substitution_id;
  int substitution_count = 0;

  regex_rewrite->url_map = mapping;
  // Make sure the regex is a C string.
  _stash.require(target_host.size() + 1);
  auto span{_stash.remnant().rebind<char>()};
  memcpy(span.data(), target_host.data(), target_host.size());
  span[target_host.size()] = '\0';

  if (regex_rewrite->regular_expression.compile(span.data()) == false) {
    zret.errata().error("{} pcre_compile failed on '{}'", ERROR_PREFIX, target_host);
  } else {
    int captures = regex_rewrite->regular_expression.get_capture_count();
    if (captures == -1) {
      zret.errata().error("{} no capture groups found for '{}'", ERROR_PREFIX, target_host);
    } else if (captures >= UrlRewrite::MAX_REGEX_SUBS) { // off by one for $0 (implicit capture)
      zret.errata().error("{} more capture groups [{}] in '{}' then the maximum supported [{}]", ERROR_PREFIX, captures + 1,
                          target_host, UrlRewrite::MAX_REGEX_SUBS);
    } else {
      TextView to_host{mapping->toURL.host_get()};
      TextView::size_type offset = 0;
      while (offset < to_host.size()) {
        offset = to_host.find('$', offset);
        if (offset != TextView::npos && offset <= to_host.size() - 1 && isdigit(to_host[offset])) {
          if (substitution_count > captures) {
            zret.errata().error("{} more substitutions [{}] than capture groups [{}] in '{}'", ERROR_PREFIX, substitution_count,
                                captures, target_host);
            break;
          }
          substitution_id = to_host[offset + 1] - '0';
          if (substitution_id > captures) {
            zret.errata().error("{} capture group index {} is larger than the number of capture groups [{}] in '{}'", ERROR_PREFIX,
                                substitution_id, captures, target_host);
            break;
          }
          regex_rewrite->substitution_markers[regex_rewrite->n_substitutions] = offset;
          regex_rewrite->substitution_ids[regex_rewrite->n_substitutions]     = substitution_id;
          ++regex_rewrite->n_substitutions;
        }
      }

      // so the regex itself is stored in fromURL.host; string to match will be in the request; string
      // to use for substitutions will be in this buffer. Does this need to be localized, or is @c toURL
      // sufficiently stable?
      regex_rewrite->to_url_host_template = mapping->toURL.host_get();
    }
  }
  if (zret.is_ok()) {
    zret = regex_rewrite.release(); // release and return.
  }
  return zret;
}

swoc::Errata
RemapBuilder::insert_ancillary_tunnel_rules(URL &target_url, URL &replacement_url, mapping_type rule_type, TextView tag)
{
  addrinfo *ai_records; // returned records.
  ip_text_buffer ipb;   // buffer for address string conversion.
  auto host_name{target_url.host_get()};
  swoc::Errata zret;

  // syscall - need host as a C-string.
  char *tmp = static_cast<char *>(alloca(host_name.size() + 1));
  memcpy(tmp, host_name.data(), host_name.size());
  tmp[host_name.size()] = '\0';
  if (0 == getaddrinfo(tmp, nullptr, nullptr, &ai_records)) {
    ts::PostScript ai_cleanup{[=]() -> void { freeaddrinfo(ai_records); }};

    for (addrinfo *ai_spot = ai_records; ai_spot; ai_spot = ai_spot->ai_next) {
      if (ats_is_ip(ai_spot->ai_addr) && !ats_is_ip_any(ai_spot->ai_addr) && ai_spot->ai_protocol == IPPROTO_TCP) {
        url_mapping *u_mapping = new url_mapping;

        ats_ip_ntop(ai_spot->ai_addr, ipb, sizeof ipb);
        u_mapping->fromURL.create(nullptr);
        u_mapping->fromURL.copy(&target_url);
        u_mapping->fromURL.host_set(std::string_view(ipb));
        u_mapping->toURL.create(nullptr);
        u_mapping->toURL.copy(&replacement_url);

        u_mapping->tag = tag;

        if (!_rewriter->InsertForwardMapping(rule_type, u_mapping, ipb)) {
          zret.error("Failed to ancillary address mapping for 'tunnel' scheme.");
          delete u_mapping;
          break;
        }
      }
    }
  }
  return zret;
}

swoc::Errata
RemapBuilder::load_plugin(url_mapping *mp, ts::file::path &&path, int argc, char const **argv)
{
  swoc::Errata zret;
  RemapPluginInfo *pi = RemapPluginInfo::find_by_path(path.view());
  char plugin_err_buff[2048];

  if (!pi) {
    pi = new RemapPluginInfo(std::move(path));
    RemapPluginInfo::add_to_list(pi);
    Debug("remap_plugin", R"(New remap plugin info created for "%s")", pi->path.c_str());

    // Load the plugin library and find the entry points.
    {
      ElevateAccess access(_load_plugins_elevated_p ? ElevateAccess::FILE_PRIVILEGE : 0);

      if ((pi->dl_handle = dlopen(pi->path.c_str(), RTLD_NOW)) == nullptr) {
        auto dl_err_text = dlerror();
        zret.error(R"(Failed to load plugin "{}" - {})", pi->path, swoc::bwf::FirstOf(dl_err_text, "*Unknown dlopen() error"));
      } else {
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
          zret.error(R"(Can't find "{}" function in remap plugin "{}")", TSREMAP_FUNCNAME_INIT, pi->path);
        } else if (!pi->new_instance_cb && pi->delete_instance_cb) {
          zret.error(
            R"(Can't find "{}" function in remap plugin "{}" which is required if "{}" function exists)",
            TSREMAP_FUNCNAME_NEW_INSTANCE, pi->path, TSREMAP_FUNCNAME_DELETE_INSTANCE);
        } else if (!pi->do_remap_cb) {
          zret.error(R"(Can't find "{}" function in remap plugin "{}")", TSREMAP_FUNCNAME_DO_REMAP, pi->path);
        } else if (pi->new_instance_cb && !pi->delete_instance_cb) {
          zret.error(
            R"(Can't find "{}" function in remap plugin "{}" which is required if "{}" function exists)",
            TSREMAP_FUNCNAME_DELETE_INSTANCE, pi->path, TSREMAP_FUNCNAME_NEW_INSTANCE);
        }
        if (!zret) {
          auto err_msg{swoc::bwstring("{}", zret)};
          Debug("remap_plugin", "%s", err_msg.c_str());
          dlclose(pi->dl_handle);
          pi->dl_handle = nullptr;
        } else {
          TSRemapInterface ri;

          ink_zero(ri);
          ri.size            = sizeof(ri);
          ri.tsremap_version = TSREMAP_VERSION;

          plugin_err_buff[0] = '\0';
          if (pi->init_cb(&ri, plugin_err_buff, sizeof plugin_err_buff) != TS_SUCCESS) {
            zret.error(R"("Failed to initialize plugin "{}": {})", pi->path,
                       swoc::bwf::FirstOf(plugin_err_buff, "Unknown plugin error"));
          }
        }
      } // done elevating access
      Debug("remap_plugin", R"(Remap plugin "%s" - initialization completed)", pi->path.c_str());
    }

    if (!pi->dl_handle) {
      zret.error(R"(Failed to load plugin "{}")", pi->path);
    } else {
      if (is_debug_tag_set("url_rewrite")) {
        ts::LocalBufferWriter<2048> lw;
        lw.print(R"(Plugin "{}: args )", path);
        auto pos = lw.extent();
        for (int i = 0; i < argc; ++i) {
          if (pos != lw.extent()) {
            lw.write(", ");
          }
          lw.print(R"([{}] "{}")", i, argv[i]);
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
        res                = pi->new_instance_cb(argc, const_cast<char **>(argv), &ih, plugin_err_buff, sizeof(plugin_err_buff));
      }

      Debug("remap_plugin", "done creating new plugin instance");

      if (res != TS_SUCCESS) {
        zret.error(R"(Failed to create instance for plugin "{}": {})", pi->path,
                   swoc::bwf::FirstOf(plugin_err_buff, "Unknown plugin error"));
      } else {
        mp->add_plugin(pi, ih);
      }
    }
  }
  return zret;
}
