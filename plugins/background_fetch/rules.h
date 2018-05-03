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

#include "ts/ts.h"

///////////////////////////////////////////////////////////////////////////
// This is a linked list of rule entries. This gets stored and parsed with the
// BgFetchConfig object.
//
class BgFetchRule
{
public:
  BgFetchRule(bool exc, const char *field, const char *value)
    : _exclude(exc), _field(TSstrdup(field)), _value(TSstrdup(value)), _next(nullptr)
  {
  }

  ~BgFetchRule()
  {
    delete _field;
    delete _value;
    delete _next;
  }

  // For chaining the linked list
  void
  chain(BgFetchRule *n)
  {
    _next = n;
  }

  // Main evaluation entry point.
  bool bgFetchAllowed(TSHttpTxn txnp) const;

private:
  bool _exclude;
  const char *_field;
  const char *_value;
  BgFetchRule *_next; // For the linked list
};
