/** @file

  A brief file description

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

#include "Utils.h"

namespace EsiLib
{
/** interface that stat systems should implement */
class StatSystem
{
public:
  virtual void create(int handle) = 0;
  // FIXME step should be TSMgmtInt
  virtual void increment(int handle, int step = 1) = 0;
  virtual ~StatSystem(){};
};

namespace Stats
{
  enum STAT {
    N_OS_DOCS           = 0,
    N_CACHE_DOCS        = 1,
    N_PARSE_ERRS        = 2,
    N_INCLUDES          = 3,
    N_INCLUDE_ERRS      = 4,
    N_SPCL_INCLUDES     = 5,
    N_SPCL_INCLUDE_ERRS = 6,
    MAX_STAT_ENUM       = 7
  };

  extern const char *STAT_NAMES[MAX_STAT_ENUM];
  extern int g_stat_indices[Stats::MAX_STAT_ENUM];
  extern StatSystem *g_system;

  void init(StatSystem *system);

  void increment(STAT st, int step = 1);
}; // namespace Stats
}; // namespace EsiLib
