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

#include "DirectiveParser.h"

#include "swoc/TextView.h"

namespace
{

swoc::TextView MAX_AGE{"max-age"};
swoc::TextView STALE_WHILE_REVALIDATE{"stale-while-revalidate"};
swoc::TextView STALE_IF_ERROR{"stale-if-error"};

} // anonymous namespace

DirectiveParser::DirectiveParser(swoc::TextView CacheControlValue)
{
  while (CacheControlValue) {
    swoc::TextView directive{CacheControlValue.take_prefix_if(&isspace)};

    // All the directives we care about have a '=' in them.
    swoc::TextView name{directive.take_prefix_at('=').trim_if(&isspace)};
    if (!name) {
      continue;
    }

    swoc::TextView value{directive.trim_if(&isspace)};
    if (!value) {
      continue;
    }
    // Directives are separated by commas, so trim if there is one.
    value.trim(',');

    if (name == MAX_AGE) {
      this->_max_age = swoc::svtoi(value);
    } else if (name == STALE_WHILE_REVALIDATE) {
      this->_stale_while_revalidate_value = swoc::svtoi(value);
    } else if (name == STALE_IF_ERROR) {
      this->_stale_if_error_value = swoc::svtoi(value);
    }
  }
}

void
DirectiveParser::merge(DirectiveParser const &other)
{
  if (other._max_age != -1) {
    this->_max_age = other._max_age;
  }
  if (other._stale_while_revalidate_value != -1) {
    this->_stale_while_revalidate_value = other._stale_while_revalidate_value;
  }
  if (other._stale_if_error_value != -1) {
    this->_stale_if_error_value = other._stale_if_error_value;
  }
}
