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

#include <array>
#include <tuple>

#include "swoc/bwf_std.h"
#include "ts_swoc_bwf_aux.h"

#include "RemapYAMLBuilder.h"
#include "UrlMapping.h"
#include "UrlRewrite.h"
#include "tscore/I_Layout.h"

using swoc::BufferWriter;
using swoc::TextView;
using std::string_view;

namespace YAML
{
// This enables efficient conversions between TextView and a YAML node.

template <> struct convert<swoc::TextView> {
  static Node
  encode(swoc::TextView const &tv)
  {
    Node zret;
    zret = std::string(tv.data(), tv.size());
    return zret;
  }
  static bool
  decode(const Node &node, swoc::TextView &tv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    tv.assign(node.Scalar());
    return true;
  }
};

} // namespace YAML

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, YAML::Mark const &mark)
{
  return w.print("Line {}", mark.line);
}

} // namespace swoc

namespace
{
// YAML parsing helpers.

/** Parse an unsigned value.
 *
 * @param [out] erratum Error reporting.
 * @param parent Node which contains the value node.
 * @param key Member key for the value node.
 * @param n [out] Instance to update with value.
 */
void
yaml_parse_unsigned(swoc::Errata &erratum, YAML::Node const &parent, swoc::TextView key, unsigned &n)
{
  if (parent[key]) {
    auto node{parent[key]};
    if (!node.IsScalar()) {
      erratum.error(R"(Value "{}" for key "{}" is not a number at {})", key, node.Mark());
    } else {
      swoc::TextView text{swoc::TextView{node.Scalar()}.trim_if(&isspace)};
      if (!text.empty()) {
        swoc::TextView parsed;
        auto x = svtou(text, &parsed);
        if (parsed.size() == text.size()) {
          n = x;
        } else {
          erratum.error(R"(Value "{}" for key "{}" is not a number at {})", text, key, node.Mark());
        }
      } else {
        erratum.error(R"(Value "{}" for key "{}" is not a number at {})", text, key, node.Mark());
      }
    }
  }
}

/** Parse an IP port.
 *
 * @param [out] erratum Error reporting.
 * @param parent Node which contains the value node.
 * @param key Member key for the value node.
 * @param n [out] Instance to update with value.
 */
swoc::Errata
yaml_parse_ip_port(YAML::Node const &node, in_port_t &n)
{
  static constexpr in_port_t MAX{std::numeric_limits<in_port_t>::max()};
  static const std::string RANGE_MSG{swoc::bwstring(R"(an integer in the range {}..{})", 1, MAX)};

  swoc::Errata erratum;

  if (!node.IsScalar()) {
    erratum.error(R"(Value at {} must be {})", node.Mark(), RANGE_MSG);
  } else {
    swoc::TextView text{swoc::TextView{node.Scalar()}.trim_if(&isspace)};
    if (!text.empty()) {
      swoc::TextView parsed;
      auto x = svtou(text, &parsed);
      if (parsed.size() == text.size()) {
        if (0 < x && x <= MAX) {
          n = x;
        } else {
          erratum.error(R"(Value at {} must be {})", node.Mark(), RANGE_MSG);
        }
      } else {
        erratum.error(R"(Value at {} must be {})", node.Mark(), RANGE_MSG);
      }
    } else {
      erratum.error(R"(Value at {} must be {})", node.Mark(), RANGE_MSG);
    }
  }
  return erratum;
}

swoc::Errata
yaml_parse_ip_range(YAML::Node const &node, IpAddr &min, IpAddr &max)
{
  swoc::Errata erratum;

  if (node.IsScalar()) {
    if (0 != ats_ip_range_parse(node.Scalar(), min, max)) {
      erratum.error(R"(Value "{}" at {} is not a valid IP address range)", node.Mark(), node.Scalar());
    }
  } else {
    erratum.error(R"(Value at {} is not a string and therefore not a valid IP address range.)", node.Scalar(), node.Mark());
  }
  return erratum;
}

} // namespace

void
RemapYAMLBuilder::parse_replacement_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url,
                                        RemapYAMLBuilder::URLOptions &opts)
{
  static constexpr swoc::TextView TO_URL_KEY{"replacement"};

  if (parent[TO_URL_KEY]) {
    auto node{parent[TO_URL_KEY]};
    if (!node.IsScalar()) {
      erratum.error("'{}' TO_URL_KEY is not a text value at {}", TO_URL_KEY, node.Mark());
    } else {
      swoc::TextView text{this->normalize_url(node.Scalar())};
      url.create(nullptr);
      if (PARSE_RESULT_DONE != url.parse_no_path_component_breakdown(text.data(), text.size())) {
        erratum.error("Malformed URL '{}' in '{}' TO_URL_KEY at {}", text, TO_URL_KEY, node.Mark());
      }
    }
  }
}

void
RemapYAMLBuilder::parse_target_url(swoc::Errata &erratum, YAML::Node const &parent, URL &url, URLOptions &opts)
{
  static constexpr swoc::TextView TARGET_URL_KEY{"target"};

  if (parent[TARGET_URL_KEY]) {
    auto node{parent[TARGET_URL_KEY]};
    if (!node.IsScalar()) {
      erratum.error("'{}' FROM_URL_KEY is not a text value at {}", TARGET_URL_KEY, node.Mark());
    } else {
      swoc::TextView text{this->normalize_url(node.Scalar())};
      url.create(nullptr);
      if (PARSE_RESULT_DONE != url.parse_no_path_component_breakdown(text.data(), text.size())) {
        erratum.error("Malformed URL '{}' in '{}' FROM_URL_KEY at {}", text, TARGET_URL_KEY, node.Mark());
      }
      if (node.Tag() == "!regexx") {
        opts[static_cast<unsigned>(RemapYAMLBuilder::URLOpt::REGEX)] = true;
      }
    }
  }
}

swoc::Errata
RemapYAMLBuilder::parse_filter_src_ip_range(YAML::Node const &node, RemapFilter *filter)
{
  swoc::Errata erratum;
  IpAddr min, max;

  if (node.IsScalar()) {
    erratum = yaml_parse_ip_range(node, min, max);
    if (erratum.is_ok()) {
      if (0 == strcasecmp(node.Tag(), YAML_NOT_TYPE)) {
        filter->mark_src_addr_inverted(min, max);
      } else {
        filter->mark_src_addr(min, max);
      }
    }
  } else {
    erratum.error(R"(Value at {} must be a string describing an IP address range.)", node.Mark());
  }
  return erratum;
}

swoc::Errata
RemapYAMLBuilder::parse_filter_proxy_ip_range(YAML::Node const &node, RemapFilter *filter)
{
  swoc::Errata erratum;
  IpAddr min, max;

  if (node.IsScalar()) {
    erratum = yaml_parse_ip_range(node, min, max);
    if (erratum.is_ok()) {
      if (0 == strcasecmp(node.Tag(), YAML_NOT_TYPE)) {
        filter->mark_proxy_addr_inverted(min, max);
      } else {
        filter->mark_proxy_addr(min, max);
      }
    }
  } else {
    erratum.error(R"(Value at {} must be a string describing an IP address range.)", node.Mark());
  }
  return erratum;
}
swoc::Rv<RemapFilter *>
RemapYAMLBuilder::parse_filter_define(YAML::Node const &node)
{
  static constexpr swoc::TextView SRC_ADDR_KEY{"src_addr"};
  static constexpr swoc::TextView PROXY_ADDR_KEY{"proxy_addr"};
  static constexpr swoc::TextView METHOD_KEY{"method"};
  static constexpr swoc::TextView ACTION_KEY{"action"};
  static constexpr swoc::TextView ACTION_ALLOW_VALUE{"allow"};
  static constexpr swoc::TextView ACTION_DENY_VALUE{"deny"};

  std::unique_ptr<RemapFilter> filter{new RemapFilter};
  swoc::Rv<RemapFilter *> zret;

  if (node[SRC_ADDR_KEY]) {
    auto map_node{node[SRC_ADDR_KEY]};

    if (map_node.IsScalar()) {
      auto result{this->parse_filter_src_ip_range(map_node, filter.get())};
      if (!result.is_ok()) {
        result.error(R"(Error in address list for "{}" key starting at {})", SRC_ADDR_KEY, node.Mark());
        zret.errata().note(result);
      }
    } else if (map_node.IsSequence()) {
      for (auto const &n : map_node) {
        auto result = this->parse_filter_src_ip_range(n, filter.get());
        if (!result.is_ok()) {
          result.error(R"(Error in address list for "{}" key starting at {})", SRC_ADDR_KEY, node.Mark());
          zret.errata().note(result);
          break;
        }
      }
    } else {
      zret.errata().error(R"(Value at {} for "{}" key must be an IP address range or array of ranges.)", SRC_ADDR_KEY, node.Mark());
    }
  };

  if (node[PROXY_ADDR_KEY]) {
    auto map_node{node[PROXY_ADDR_KEY]};

    if (map_node.IsScalar()) {
      auto result{this->parse_filter_proxy_ip_range(map_node, filter.get())};
      if (!result.is_ok()) {
        result.error(R"(Error in address list for "{}" key starting at {})", PROXY_ADDR_KEY, node.Mark());
        zret.errata().note(result);
      }
    } else if (map_node.IsSequence()) {
      for (auto const &n : map_node) {
        auto result = this->parse_filter_proxy_ip_range(n, filter.get());
        if (!result.is_ok()) {
          result.error(R"(Error in address list for "{}" key starting at {})", PROXY_ADDR_KEY, node.Mark());
          zret.errata().note(result);
          break;
        }
      }
    } else {
      zret.errata().error(R"(Value at {} for "{}" key must be an IP address range or array of ranges.)", PROXY_ADDR_KEY,
                          node.Mark());
    }
  }

  if (node[METHOD_KEY]) {
    auto n_method{node[METHOD_KEY]};

    if (0 == strcasecmp(n_method.Tag(), YAML_NOT_TYPE)) {
      filter->set_method_match_inverted(true);
    }

    if (n_method.IsScalar()) {
      filter->add_method(n_method.Scalar());
    } else if (n_method.IsSequence()) {
      for (auto const &n : n_method) {
        if (n.IsScalar()) {
          filter->add_method(n.Scalar());
        } else {
          zret.errata().error(R"(Values in an array for key "{}" at {} must be a strings.)", METHOD_KEY, node.Mark());
          break;
        }
      }
    } else {
      zret.errata().error(R"(Value for key "{}" at {} must be a string or an array of strings.)", METHOD_KEY, node.Mark());
    }
  }

  if (node[ACTION_KEY]) {
    auto n_action{node[ACTION_KEY]};
    swoc::TextView value{n_action.Scalar()};
    if (0 == strcasecmp(value, ACTION_ALLOW_VALUE)) {
    } else if (0 == strcasecmp(value, ACTION_DENY_VALUE)) {
    } else {
      zret.errata().error(R"(The value for the "{}" key at {} must be "{}" or "{}")", ACTION_KEY, n_action.Mark(),
                          ACTION_ALLOW_VALUE, ACTION_DENY_VALUE);
    }
  } else {
    zret.errata().error(R"(The "{}" key is required in the filter definition starting at {})", ACTION_KEY, node.Mark());
  }

  if (zret.is_ok()) {
    zret = filter.release();
  }
  return zret;
};

swoc::Errata
RemapYAMLBuilder::parse_filter_definitions(YAML::Node const &filters)
{
  swoc::Errata zret;

  if (filters.IsMap()) {
  } else if (filters.IsSequence()) {
  } else {
    zret.error("Filters [{} {}] must be a filter definition or an array of filter definitions", FILTER_DEFINITIONS_KEY,
               filters.Mark());
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse_plugin_define(YAML::Node const &node, url_mapping *mp)
{
  static constexpr swoc::TextView PATH_KEY{"path"};
  static constexpr swoc::TextView ARGS_KEY{"args"};

  swoc::Errata zret;
  ts::file::path path;
  std::vector<const char *> plugin_argv;

  if (node[PATH_KEY]) {
    auto path_node{node[PATH_KEY]};
    if (path_node.IsScalar()) {
      path = path_node.Scalar();
      std::error_code ec;
      if (path.is_relative()) {
        path = ts::file::path(Layout::get()->sysconfdir) / path;
      }
      auto file_stat{ts::file::status(path, ec)};
      if (ec) {
        zret.error(R"(Plugin file "{}" access error {})", path, ec);
      }
    } else {
      zret.error(R"(Value for "{}" must be a string)", PATH_KEY);
    }
  }

  if (!zret.is_ok()) {
    return zret;
  }

  if (node[ARGS_KEY]) {
    auto args_node{node[ARGS_KEY]};
    if (args_node.IsScalar()) {
      plugin_argv.push_back(_rewriter->localize(args_node.Scalar()).data());
    } else if (args_node.IsSequence()) {
      for (auto const &n : args_node) {
        if (n.IsScalar()) {
          plugin_argv.push_back(_rewriter->localize(args_node.Scalar()).data());
        } else {
          zret.error(R"(Invalid plugin argument at {} - must be strings)", n.Mark());
          break;
        }
      }
    } else {
      zret.error(R"(Plugin key "{}" must have a value that is a string or array of strings)", ARGS_KEY);
    }
  };

  if (!zret.is_ok()) {
    return zret;
  }

  zret = this->load_plugin(mp, std::move(path), plugin_argv.size(), plugin_argv.data());
  return zret;
}

swoc::Errata &
RemapYAMLBuilder::apply_rule_option(swoc::Errata &erratum, YAML::Node const &node, RuleOptions &options)
{
  static const std::array<swoc::TextView, RuleOptions{}.size()> OPTIONS{{{"reverse"}, {"proxy_port"}}};

  bool unknown_tag_p = false;

  if (node.IsScalar()) {
    swoc::TextView opt{node.as<swoc::TextView>()};
    auto spot =
      std::find_if(OPTIONS.begin(), OPTIONS.end(), [=](swoc::TextView const &tag) -> bool { return strcasecmp(tag, opt); });
    if (spot == OPTIONS.end()) {
      erratum.error("Value '{}' for rule option at {} is not a valid value.", opt, node.Mark());
      unknown_tag_p = true;
    } else {
      options[spot - OPTIONS.begin()] = true;
    }
  } else {
    erratum.error("Rule option at {} is not a string", node.Mark());
  }

  if (unknown_tag_p) {
    swoc::LocalBufferWriter<256> w;
    w.write("Rule options must be one of [");
    auto pos = w.extent();
    for (auto const &tag : OPTIONS) {
      if (w.extent() != pos) {
        w.write(',');
      }
      w.write(tag);
    }
    w.write(']');
    erratum.error(w.view());
  }

  return erratum;
}

swoc::Errata
RemapYAMLBuilder::parse_referer_match(YAML::Node const &value, url_mapping *mp)
{
  swoc::Errata zret;

  if (value.IsScalar()) {
    RefererInfo ri;
    auto rx{_rewriter->localize(value.Scalar())};
    ts::LocalBufferWriter<1024> errw;
    auto err_msg = ri.parse(rx, errw);
    if (err_msg) {
      zret.error(R"(Malformed value "{}" at {} - {}.)", rx, value.Mark(), err_msg);
    } else {
      if (ri.negative && ri.any) {
        mp->optional_referer = true;
      } else {
        if (ri.negative) {
          mp->negative_referer = true;
        }
        mp->referer_list.append(new RefererInfo(std::move(ri)));
      }
    }
  } else {
    zret.error(R"(Value at {} must be a regular expression (string).)", value.Mark());
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse_referer(YAML::Node const &node, url_mapping *mp)
{
  static constexpr swoc::TextView REDIRECT_KEY{"redirect"};
  static constexpr swoc::TextView MATCH_KEY{"match"};

  swoc::Errata zret;

  if (node[REDIRECT_KEY]) {
    auto redirect_node{node[REDIRECT_KEY]};
    if (redirect_node.IsScalar()) {
      mp->filter_redirect_url = _rewriter->localize(redirect_node.Scalar());
      RedirectChunk::parse(mp->filter_redirect_url, mp->redirect_chunks);
    } else {
      zret.error(R"(Redirect URL for key "{}" at {} in object at {} must be a URL.)", REDIRECT_KEY, redirect_node.Mark(),
                 node.Mark());
    }
  } else {
    zret.error(R"(Referer object at {} must have a "{}" key.)", node.Mark(), REDIRECT_KEY);
  }

  if (node[MATCH_KEY]) {
    auto match_node{node[MATCH_KEY]};
    if (match_node.IsScalar()) {
      zret = this->parse_referer_match(match_node, mp);
    } else if (match_node.IsSequence()) {
      for (auto const &n : match_node) {
        auto result = this->parse_referer_match(n, mp);
        if (!result.is_ok()) {
          result.error(R"(Bad value in list at {} for key "{}".)", match_node.Mark(), MATCH_KEY);
          zret.note(result);
          break;
        }
      }
    } else {
      zret.error(R"(Value for "{}" at {} in referer object at {} must be a string or an array of strings.)", MATCH_KEY,
                 match_node.Mark(), node.Mark());
    }
  } else {
    zret.error(R"(Referer object at {} must have a "{}" key.)", node.Mark(), MATCH_KEY);
  }
  return zret;
};

swoc::Errata
RemapYAMLBuilder::parse_rule_filter(YAML::Node const &node, url_mapping *mp)
{
  swoc::Errata zret;

  if (node.IsScalar()) {
    auto filter = this->find_filter(node.Scalar());
    if (filter) {
      mp->filters.push_back(filter);
    } else {
      zret.error(R"(Filter name "{}" not found at {}.)", node.Scalar(), node.Mark());
    }
  } else if (node.IsMap()) {
    auto result = this->parse_filter_define(node);
    if (result.is_ok()) {
      mp->filters.push_back(result);
    } else {
      zret = std::move(result.errata());
      zret.error(R"(Invalid filter definition at {}.)", node.Mark());
    }
  } else {
    zret.error(R"(Filter at {} must be a name or a filter definition.)", node.Mark());
  }

  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse_rule_define(YAML::Node const &rule)
{
  static constexpr swoc::TextView RULE_ID_KEY{"id"};
  static constexpr swoc::TextView PROXY_PORT_KEY{"proxy_port"};
  static constexpr swoc::TextView OPTIONS_KEY{"options"};
  static constexpr swoc::TextView PLUGINS_KEY{"plugins"};
  static constexpr swoc::TextView REFERER_KEY{"referer"};
  static constexpr swoc::TextView FILTERS_KEY{"filters"};

  swoc::Errata zret;

  RuleOptions rule_options;
  std::unique_ptr<url_mapping> mapping{new url_mapping};

  in_port_t proxy_port = 0; // proxy port, if non-zero
  URLOptions target_options;
  URLOptions replacement_options;

  this->parse_target_url(zret, rule, mapping->fromURL, target_options);
  this->parse_replacement_url(zret, rule, mapping->toURL, replacement_options);
  yaml_parse_unsigned(zret, rule, RULE_ID_KEY, mapping->map_id);
  if (rule[PROXY_PORT_KEY]) {
    auto port_node{rule[PROXY_PORT_KEY]};
    auto result{yaml_parse_ip_port(port_node, proxy_port)};
    if (!result.is_ok()) {
      result.error(R"(Bad value for "{}" key at {} in filter definition starting at {})", PROXY_PORT_KEY, port_node.Mark(),
                   rule.Mark());
    }
  }

  if (rule[OPTIONS_KEY]) {
    auto node{rule[OPTIONS_KEY]};
    if (node.IsScalar()) {
      this->apply_rule_option(zret, node, rule_options);
    } else if (node.IsSequence()) {
      for (auto const &n : node) {
        this->apply_rule_option(zret, n, rule_options);
      }
    } else {
      zret.error(R"("The value for '{}" key at {} must be a string or an array of strings.)", OPTIONS_KEY, node.Mark());
    }
  }

  auto target_host{mapping->fromURL.host_get()};

  if (rule[REDIRECT_KEY]) {
    auto redirect{rule[REDIRECT_KEY]};
    if (redirect.IsScalar()) {
      swoc::TextView value{redirect.Scalar()};
      if (0 == strcasecmp(value, REDIRECT_VALUE_TEMPORARY)) {
      } else if (0 == strcasecmp(value, REDIRECT_VALUE_PERMANENT)) {
      } else {
        zret.error(R"("The value for '{}" key at {} must "{} or "{}".)", REDIRECT_KEY, redirect.Mark(), REDIRECT_VALUE_PERMANENT,
                   REDIRECT_VALUE_TEMPORARY);
      }
    } else {
      zret.error(R"("The value for '{}" key at {} must be a string with value "{} or "{}".)", REDIRECT_KEY, redirect.Mark(),
                 REDIRECT_VALUE_PERMANENT, REDIRECT_VALUE_TEMPORARY);
    }
  };

  if (rule[REFERER_KEY]) {
    auto referer_node{rule[REFERER_KEY]};
    if (referer_node.IsMap()) {
      auto result = this->parse_referer(referer_node, mapping.get());
      if (!result.is_ok()) {
        result.error(R"(Invalid object for "{}" key at {}.)", REFERER_KEY, referer_node.Mark());
        zret.note(result);
      }
    } else {
      zret.error(R"(The "{}" key value at {} must be an object)", REFERER_KEY, referer_node.Mark());
    }
  };

  if (rule[FILTERS_KEY]) {
    auto filters_node{rule[FILTERS_KEY]};
    if (filters_node.IsSequence()) {
      for (auto const &n : filters_node) {
        auto result = this->parse_rule_filter(n, mapping.get());
        if (!result.is_ok()) {
          result.error(
            R"(Failed to add filters from "{}" key at {} in array at {} for the rule at {}.)", FILTERS_KEY, n.Mark(),
            filters_node.Mark(), rule.Mark());
          zret.note(result);
        }
      }
    } else {
      auto result = this->parse_rule_filter(filters_node, mapping.get());
      if (!result.is_ok()) {
        result.error(R"(Failed to add filters from "{}" key at {} for the rule at {}.)", FILTERS_KEY, filters_node.Mark(),
                     rule.Mark());
        zret.note(result);
      }
    }
  }

  if (rule[PLUGINS_KEY]) {
    auto plugins_node{rule[PLUGINS_KEY]};
    if (plugins_node.IsScalar()) {
      zret = this->parse_plugin_define(plugins_node, mapping.get());
    } else if (plugins_node.IsSequence()) {
      for (auto const &n : plugins_node) {
        auto result = this->parse_plugin_define(n, mapping.get());
        if (!result.is_ok()) {
          result.error(R"(Error processing plugin at {} in definitions at {} in rule at {})", n.Mark(), plugins_node.Mark(),
                       rule.Mark());
          zret.note(result);
          break;
        }
      }
    } else {
      zret.error("Plugins value must be an object or an array of objects");
    }
  }

  if (zret.is_ok()) {
    UrlRewrite::RegexMapping *regex_mapping = target_options[URLOpt::REGEX] ? new UrlRewrite::RegexMapping : nullptr;
    mapping_type rule_type{FORWARD_MAP};

    if (proxy_port) {
      rule_type = FORWARD_MAP_WITH_RECV_PORT;
    }
    _rewriter->InsertMapping(rule_type, mapping.release(), regex_mapping, nullptr, target_options[URLOpt::REGEX]);

    // If the reverse option is set, insert a reverse rule for this rule.
    if (rule_options[REVERSE_MAP]) {
      auto reverse_mapping = new url_mapping;
      reverse_mapping->fromURL.create(nullptr);
      reverse_mapping->fromURL.copy(&mapping->toURL);
      reverse_mapping->toURL.copy(&mapping->fromURL);
      _rewriter->InsertMapping(REVERSE_MAP, reverse_mapping, nullptr, nullptr, false);
    }
  }

  zret.error("Failed to parse rule definition at {}", rule.Mark());
  return zret;
};

swoc::Errata
RemapYAMLBuilder::enable_filter(swoc::TextView name)
{
  swoc::Errata zret;

  auto spot{
    std::find_if(_filters.begin(), _filters.end(), [&](RemapFilter const &f) -> bool { return 0 == strcasecmp(f.name, name); })};
  if (spot != _filters.end()) {
    _active_filters.push_back(spot);
  } else {
    zret.error(R"(Failed to enable filter "{}" - not found)", name);
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse_enable_directive(YAML::Node const &node)
{
  swoc::Errata zret;

  if (node.IsScalar()) {
    zret = this->enable_filter(node.Scalar());
  } else if (node.IsSequence()) {
    for (auto const &n : node) {
      if (n.IsScalar()) {
        zret = this->enable_filter(n.Scalar());
        if (!zret.is_ok()) {
          zret.error(R"(Malformed element in array at {})", node.Mark());
          break;
        }
      }
    }
  } else {
    zret.error(R"(Value for filter directive at {} must be a string or array of strings. )", node.Mark());
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::disable_filter(swoc::TextView name)
{
  swoc::Errata zret;

  auto spot{std::find_if(_active_filters.rbegin(), _active_filters.rend(),
                         [&](RemapFilter *f) -> bool { return 0 == strcasecmp(f->name, name); })};
  if (spot != _active_filters.rend()) {
    _active_filters.erase((++spot).base());
  } else {
    zret.error(R"(Failed to disable filter "{}" - not found)", name);
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse_disable_directive(YAML::Node const &node)
{
  swoc::Errata zret;

  if (node.IsScalar()) {
    zret = this->disable_filter(node.Scalar());
  } else if (node.IsSequence()) {
    for (auto const &n : node) {
      if (n.IsScalar()) {
        zret = this->disable_filter(n.Scalar());
        if (!zret.is_ok()) {
          zret.error(R"(Malformed element in array at {})", node.Mark());
          break;
        }
      }
    }
  } else {
    zret.error(R"(Value for filter directive at {} must be a string or array of strings. )", node.Mark());
  }
  return zret;
}
swoc::Errata
RemapYAMLBuilder::parse_directive(YAML::Node const &node)
{
  static constexpr swoc::TextView ENABLE_KEY{"enable"};
  static constexpr swoc::TextView DISABLE_KEY{"disable"};

  swoc::Errata zret;

  if (node[ENABLE_KEY]) {
    zret = this->parse_enable_directive(node[ENABLE_KEY]);
  } else if (node[DISABLE_KEY]) {
    zret = this->parse_disable_directive(node[DISABLE_KEY]);
  } else {
    zret = this->parse_rule_define(node);
  }

  return zret;
};

swoc::Errata
RemapYAMLBuilder::parse_directives(YAML::Node const &rules)
{
  swoc::Errata zret;

  if (rules.IsMap()) {
    auto rv{this->parse_directive(rules)};
    if (!rv.is_ok()) {
      zret.note(rv);
      zret.error("Rules [{} {}] was malformed.", RULE_DEFINITIONS_KEY, rules.Mark());
    }
  } else if (rules.IsSequence()) {
    for (auto const &rule : rules) {
      auto rv{this->parse_directive(rule)};
      if (!rv.is_ok()) {
        zret.note(rv);
        zret.error("Rules [{} {}] was malformed.", RULE_DEFINITIONS_KEY, rule.Mark());
      }
    }
  } else {
    zret.error("Rules [{} {}] must be a rule definition or an array of rule definitions", RULE_DEFINITIONS_KEY, rules.Mark());
  }
  return zret;
}

swoc::Errata
RemapYAMLBuilder::parse(UrlRewrite *rewriter, std::string const &content)
{
  swoc::Errata zret;
  YAML::Node top; // Top of the parsed YAML tree.
  try {
    top = YAML::Load(content);
  } catch (YAML::ParserException &ex) {
    return zret.warn("YAML parsing error: {}", ex.what());
  }

  if (!top[ROOT_KEY]) {
    return zret.warn("YAML parsing error: required root tag '{}' not found", ROOT_KEY);
  }
  auto root = top[ROOT_KEY];
  if (!root.IsMap()) {
    return zret.warn("YAML parsing error: required root tag '{}' was not an object", ROOT_KEY);
  }

  RemapYAMLBuilder builder{rewriter};

  YAML::Node filters{root[FILTER_DEFINITIONS_KEY]};
  if (filters) {
    if (!(zret = builder.parse_filter_definitions(filters)).is_ok()) {
      zret.error("YAML parsing error for '{}' tag", FILTER_DEFINITIONS_KEY);
      return zret;
    }
  }
  YAML::Node rules{root[RULE_DEFINITIONS_KEY]};
  if (rules) {
    if (!(zret = builder.parse_directives(rules)).is_ok()) {
      zret.error("YAML parsing error for '{}' tag", RULE_DEFINITIONS_KEY);
      return zret;
    }
  }
  return zret;
}
