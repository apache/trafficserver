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

#include <string_view>
#include <bitset>

#include "swoc/TextView.h"
#include "swoc/Errata.h"

#include "UrlRewrite.h"

/// Base class for remap config builders.
class RemapBuilder
{
  using self_type = RemapBuilder;
  using TextView  = swoc::TextView;

public:
  /** Create a regex rewrite object.
   *
   * @param mapping Base mapping container.
   * @param target_host Host name as a regular expression.
   * @return A new regex mapping, or errors.
   */
  swoc::Rv<UrlRewrite::RegexMapping *> parse_regex_rewrite(url_mapping *mapping, TextView target_host);
};
