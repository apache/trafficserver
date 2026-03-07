/*
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

/**
 * @file prefetch.h
 * @brief Background fetch classes for slice plugin.
 */

#pragma once

#include <string>

#include "ts/ts.h"
#include "Data.h"
#include "Config.h"

/**
 * @brief Represents a single background fetch.
 */
struct BgBlockFetch {
  static bool schedule(Data *const data, int blocknum, std::string_view url);

  BgBlockFetch() = default;

  bool       fetch(Data *const data);
  static int handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */);
  void       clear();

  /* This is for the actual background fetch / NetVC */
  Stage       m_stream;
  int         m_blocknum{0};
  TSCont      m_cont{nullptr};
  Config     *m_config{nullptr};
  std::string m_key;
};
