/** @file
 *
 *  Base remap configuration builder logic.
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

#pragma once

#include <string_view>
#include <bitset>

#include "UrlRewrite.h"

#include "swoc/TextView.h"
#include "swoc/Errata.h"

/// Base class for remap config builders.
class RemapBuilder
{
  using self_type = RemapBuilder;
  using TextView  = swoc::TextView;

public:
  /** Constructor.
   *
   * @param url_rewriter The persistent store of remap information.
   */
  explicit RemapBuilder(UrlRewrite *url_rewriter);

  /** Create a regex rewrite object.
   *
   * @param mapping Base mapping container.
   * @param target_host Host name as a regular expression.
   * @return A new regex mapping, or errors.
   */
  swoc::Rv<UrlRewrite::RegexMapping *> parse_regex_rewrite(url_mapping *mapping, TextView target_host);

  /** Localize a URL and, if needed, normalize it as it is  copied.
   *
   * @param url URL to normalize.
   * @return A view of the normalized URL.
   *
   * Required properties:
   * - If the URL is a full URL, the host @b must be followed by a separator ('/').
   */
  TextView normalize_url(TextView url);

  /** Find a filter by name.
   *
   * @param name Name of filter.
   * @return A pointer to the filter, or @c nullptr if not found.
   */
  RemapFilter *find_filter(TextView name);

  swoc::Errata insert_ancillary_tunnel_rules(URL &target_url, URL &replacement_url, mapping_type rule_type, TextView tag);

  /** Make a copy of @a view in local string storage.
   *
   * @param view String to copy.
   * @return A view of the copy.
   *
   * The copy is null terminated so the return value can be used as a C string.
   */
  swoc::TextView stash(swoc::TextView view);

  /** Make a copy of @a view in local string storage.
   *
   * @param view String to copy.
   * @return A view of the copy.
   *
   * The copy converted to lower case and null terminated so the return value can be used as a C
   * string.
   */
  swoc::TextView stash_lower(swoc::TextView view);

protected:
  swoc::Errata load_plugin(url_mapping *mp, ts::file::path &&path, int argc, char const **argv);

  UrlRewrite *_rewriter = nullptr; // Pointer to the UrlRewrite object we are parsing for.
  RemapFilterList _filters;
  std::list<RemapFilter *> _active_filters;
  swoc::MemArena _stash; ///< Temporary storage for localizing strings.

  bool _load_plugins_elevated_p = false;
};

namespace ts
{
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, swoc::Errata const &erratum)
{
  swoc::FixedBufferWriter fw{w.auxBuffer(), w.remaining()};
  swoc::bwformat(fw, swoc::bwf::Spec::DEFAULT, erratum);
  return w.fill(fw.size());
};
} // namespace ts

namespace swoc
{
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ts::file::path const &path)
{
  return bwformat(w, spec, path.view());
}
} // namespace swoc
