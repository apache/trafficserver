/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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

#include <cstdlib>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>

#include <swoc/TextView.h>
#include <swoc/IPRange.h>

#include "ts/ts.h"

///////////////////////////////////////////////////////////////////////////
// This is a linked list of rule entries. This gets stored and parsed with the
// BgFetchConfig object.
//
class BgFetchRule
{
  using self_type = BgFetchRule;

public:
  /// Content length / size comparison.
  struct size_cmp_type {
    enum OP { LESS_THAN_OR_EQUAL, GREATER_THAN_OR_EQUAL } _op; ///< Comparison to use.
    size_t _size;                                              ///< Size for comparison.
  };

  /// Field value comparison.
  struct field_cmp_type {
    std::string _name;  ///< Field name.
    std::string _value; ///< Value to compare. A single '*' means match anything - check for field presence.
  };

  BgFetchRule(bool exc, size_cmp_type::OP op, size_t n) : _exclude(exc), _value(size_cmp_type{op, n}) {}
  BgFetchRule(bool exc, swoc::IPRange const &range) : _exclude(exc), _value(range) {}

  BgFetchRule(bool exc, swoc::TextView name, swoc::TextView value)
    : _exclude(exc), _value(field_cmp_type{std::string(name), std::string(value)})
  {
  }

  BgFetchRule(self_type &&that) = default;

  // Main evaluation entry point.
  bool bgFetchAllowed(TSHttpTxn txnp) const;
  bool check_field_configured(TSHttpTxn txnp) const;

  bool _exclude; ///< Exclusion @c true or inclusion @c false.

  /// Value type for the rule, which also indicates the type of check.
  using value_type = std::variant<std::monostate, size_cmp_type, field_cmp_type, swoc::IPRange>;
  value_type _value; ///< Value instance for checking.
};
