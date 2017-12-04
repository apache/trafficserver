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

#pragma once

#include <list>
#include "AclFiltering.h"
#include "tscore/MemArena.h"
#include "tscore/ts_file.h"

class UrlRewrite;
class url_mapping;

// remap rule types
static constexpr ts::TextView REMAP_REGEX_PREFIX{"regex_"};
static constexpr ts::TextView REMAP_MAP_TAG{"map"};
static constexpr ts::TextView REMAP_REDIRECT_TAG{"redirect"};
static constexpr ts::TextView REMAP_TEMPORARY_REDIRECT_TAG{"redirect_temporary"};
static constexpr ts::TextView REMAP_REVERSE_MAP_TAG{"reverse_map"};
static constexpr ts::TextView REMAP_WITH_REFERER_TAG{"map_with_referer"};
static constexpr ts::TextView REMAP_WITH_RECV_PORT_TAG{"map_with_recv_port"};

static constexpr ts::TextView REMAP_FILTER_IP_ALLOW_TAG{"ip_allow"};

/** Configuration load time information for building the remap table.

    This is not a persistent structure, it is used only while the configuration is being loaded.
*/
struct RemapBuilder {
  using self_type = RemapBuilder; ///< Self reference type.
  using TextView  = ts::TextView;
  /// List of parameter, the direct remap values in a configuration rule.
  using ParamList = std::vector<std::string_view>;
  /// List of arguments, which are modifiers for the configuration rule. Almost all require a
  /// value, therefore this list a list of (key,value) tuples.
  using ArgList = std::vector<RemapArg>;
  /// Used to report errors. Need to switch to @c Errata if I ever get the updated version back ported here.
  using ErrBuff = ts::FixedBufferWriter;

  /** Constructor.
   *
   * @param url_rewriter The persistent store of remap information.
   */
  RemapBuilder(UrlRewrite *url_rewriter) : rewrite(url_rewriter) {}

  RemapBuilder(const RemapBuilder &) = delete;
  RemapBuilder &operator=(const RemapBuilder &) = delete;

  /** Parse the configuration file.
   *
   * @param path Path to the configuration file.
   * @param rewriter The persistent store of remap information.
   * @return @c true if the file was parsed successfully, @c false if not.
   *
   * This is a static function, it creates a temporary instance of this class to do the parsing
   * and puts the persistent data in @a rewriter.
   */
  static bool parse_config(ts::file::path const &path, UrlRewrite * rewriter);

  /** Parse and load a remap configuration file.
   *
   * @param path Path to the file.
   * @param errw Error reporting buffer.
   * @return An error string on failure, an emtpy view on success.
   *
   * This is used to handle included configuration files.
   */
  TextView parse_remap_fragment(ts::file::path const &path, RemapBuilder::ErrBuff &errw);

  TextView process_filter_opt(url_mapping &mapping, ErrBuff &errw);

  /** Handle a directive.
   *
   * @param errw Error buffer.
   * @return An error on failure, an empty view on success.
   *
   * This determines the type of directive and dispatches it to the correct handler.
   */
  TextView parse_directive(ErrBuff &errw);

  /** Parse a filter definition.
   *
   * @param directive The directive token.
   * @param errw Error reporting buffer.
   * @return An error, or an empty view if successful.
   *
   * This updates the filter list with a new named filter based on the parsed data.
   */
  TextView parse_define_directive(TextView directive, ErrBuff &errw);

  /** Parse a filter delete.
   *
   * @param directive The directive token.
   * @param errw Error reporting buffer.
   * @return An error, or an empty view if successful.
   */
  TextView parse_delete_directive(TextView directive, ErrBuff &errw);

  /** Parse a filter activation.
   *
   * @param directive The directive token.
   * @param errw Error reporting buffer.
   * @return An error, or an empty view if successful.
   */
  TextView parse_activate_directive(TextView directive, ErrBuff &errw);

  /** Parse a filter deactivation.
   *
   * @param directive The directive token.
   * @param errw Error reporting buffer.
   * @return An error, or an empty view if successful.
   */
  TextView parse_deactivate_directive(TextView directive, ErrBuff &errw);

  /** Parse include directive.
   *
   * @param directive The directive token.
   * @param errw Error reporting buffer.
   * @return An error, or an empty view if successful.
   */
  TextView parse_include_directive(TextView directive, ErrBuff &errw);

  /** Load the plugins for the current arguments.
   *
   * @param errw Error reporting buffer.
   * @return An error string, or an empty view on success.
   *
   * This walks the argument list, finding plugin arguments and loading the specified plugin.
   */
  TextView load_plugins(ErrBuff & errw);

  /** Find a filter by name.
   *
   * @param name Name of filter.
   * @return A pointer to the filter, or @c nullptr if not found.
   */
  RemapFilter *find_filter(TextView name);

  void clear();

  // These are views in to the configuration file. Copies are made when storing them in the
  // persistent remap store (generally by assignment to a @c std::string). It is critical that
  // the result can be passed to the plugin API as a C string.
  ParamList paramv; ///< Parameter list.
  ArgList argv;     ///< Argument list.

  bool ip_allow_check_enabled_p = true;
  bool accept_check_p           = true;
  UrlRewrite *rewrite           = nullptr; // Pointer to the UrlRewrite object we are parsing for.
  RemapFilterList filters;
  std::list<RemapFilter *> active_filters;
  ts::MemArena arena{512};

  /** Copy the @a token to the local memory arena and return the localized data.
   *
   * @param token String to localize.
   * @return A new view of the localized string.
   *
   * This adds a null terminator so the view can be used as a C string.
   */
  TextView localize(TextView token);
};

const char *remap_validate_filter_args(RemapFilter **rule_pp, const char **argv, int argc, char *errStrBuf, size_t errStrBufSize);

bool remap_parse_config(const char *path, UrlRewrite *rewrite);

extern std::function<void(char const *)> load_remap_file_cb;
