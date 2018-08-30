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

#include "rules.h"
#include "tscore/ink_atomic.h"

// Constants
const char PLUGIN_NAME[] = "background_fetch";

///////////////////////////////////////////////////////////////////////////
// This holds one complete background fetch rule, which is also ref-counted.
//
class BgFetchConfig
{
public:
  BgFetchConfig(TSCont cont) : _cont(cont), _rules(nullptr), _ref_count(0) { TSContDataSet(cont, static_cast<void *>(this)); }
  void
  acquire()
  {
    ink_atomic_increment(&_ref_count, 1);
  }

  void
  release()
  {
    TSDebug(PLUGIN_NAME, "ref_count is %d", _ref_count);
    if (1 >= ink_atomic_decrement(&_ref_count, 1)) {
      TSDebug(PLUGIN_NAME, "configuration deleted, due to ref-counting");
      delete this;
    }
  }

  BgFetchRule *
  getRules() const
  {
    return _rules;
  }

  TSCont
  getCont() const
  {
    return _cont;
  }

  // This parses and populates the BgFetchRule linked list (_rules).
  bool readConfig(const char *file_name);

  bool bgFetchAllowed(TSHttpTxn txnp) const;

private:
  ~BgFetchConfig()
  {
    delete _rules;
    if (_cont) {
      TSContDestroy(_cont);
    }
  }

  TSCont _cont;
  BgFetchRule *_rules;
  int _ref_count;
};
