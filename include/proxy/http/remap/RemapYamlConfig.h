/** @file
 *
 *  YAML remap configuration file parsing.
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

#pragma once

#include <map>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "swoc/Errata.h"
#include "proxy/http/remap/RemapConfig.h"
#include "proxy/hdrs/URL.h"

class acl_filter_rule;
class UrlRewrite;
class url_mapping;

// Parse URL from YAML node into URL object
swoc::Errata parse_yaml_url(const YAML::Node &node, URL &url, bool host_check, std::string_view &url_str);

// Parse and validate ACL filters from YAML node
swoc::Errata remap_validate_yaml_filter_args(acl_filter_rule **rule_pp, const YAML::Node &node, ACLBehaviorPolicy behavior_policy);

// Parse map_with_referer YAML node
swoc::Errata parse_map_referer(const YAML::Node &node, url_mapping *url_mapping);

// Parse plugin YAML node
swoc::Errata parse_yaml_plugins(const YAML::Node &node, url_mapping *url_mapping, BUILD_TABLE_INFO *bti);

// Parse filter directive YAML nodes (activate_filter|deactivate_filter|delete_filter|define_filter)
swoc::Errata parse_yaml_filter_directive(const YAML::Node &node, BUILD_TABLE_INFO *bti);

// Parse define filter directice YAML node (for inline and global filters)
swoc::Errata parse_yaml_define_directive(const YAML::Node &node, BUILD_TABLE_INFO *bti);

// Parse remap filter YAML node
swoc::Errata process_yaml_filter_opt(url_mapping *mp, const YAML::Node &node, const BUILD_TABLE_INFO *bti);

// Parse yaml subpath
swoc::Errata parse_yaml_remap_fragment(const char *path, BUILD_TABLE_INFO *bti);

// Parse include directive for remap subpaths
swoc::Errata parse_yaml_include_directive(const std::string &include_path, BUILD_TABLE_INFO *bti);

// Parse for single remap rule YAML node
swoc::Errata parse_yaml_remap_rule(const YAML::Node &node, BUILD_TABLE_INFO *bti);

// Parse remap YAML node
bool remap_parse_yaml_bti(const char *path, BUILD_TABLE_INFO *bti);

bool remap_parse_yaml(const char *path, UrlRewrite *rewrite);
