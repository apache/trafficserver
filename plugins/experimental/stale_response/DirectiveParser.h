/** @file

  Parse Cache-Control directives.

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

#pragma once

#include <ctime>
#include "swoc/TextView.h"

/** Parses the directives of a Cache-Control HTTP field value. */
class DirectiveParser
{
public:
  DirectiveParser() = default;

  /** Construct a parser from a Cache-Control value.
   * @param[in] CacheControlValue The value of a Cache-Control header field.
   */
  DirectiveParser(swoc::TextView CacheControlValue);

  /** Merge the directives from another parser into this one.
   *
   * If a directive is present in both parsers, the value from the other parser is
   * used.
   *
   * @param[in] other The parser to merge into this one.
   */
  void merge(DirectiveParser const &other);

  /** Get the value of the max-age Cache-Control directive.
   *
   * @return The value of the max-age Cache-Control directive, or -1 if the
   * directive was not present.
   */
  time_t
  get_max_age() const
  {
    return _max_age;
  };

  /** Get the value of the stale-while-revalidate Cache-Control directive.
   *
   * @return The value of the stale-while-revalidate Cache-Control directive,
   * or -1 if the directive was not present.
   */
  time_t
  get_stale_while_revalidate() const
  {
    return _stale_while_revalidate_value;
  }

  /** Get the value of the stale-if-error Cache-Control directive.
   *
   * @return The value of the stale-if-error Cache-Control directive, or -1 if
   * the directive was not present.
   */
  time_t
  get_stale_if_error() const
  {
    return _stale_if_error_value;
  };

private:
  /** The value of the max-age Cache-Control directive.
   *
   * A negative value indicates that the directive was not present. RFC 9111
   * section 1.2.2 specifies that delta-seconds, used by max-age, must be
   * non-negative, so this should be safe.
   */
  time_t _max_age = -1;

  /// The value of the stale-while-revalidate Cache-Control directive.
  time_t _stale_while_revalidate_value = -1;

  /// The value of the stale-if-error Cache-Control directive.
  time_t _stale_if_error_value = -1;
};
