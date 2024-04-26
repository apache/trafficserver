/** @file

  Manage body data for the plugin.

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

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ts_wrap.h"

// this determines size of pointer array alloc/reallocs
constexpr unsigned int c_chunk_add_count = 64;

#define PLUGIN_TAG_BODY "stale_response_body"

EXT_DBG_CTL(PLUGIN_TAG_BODY)

/*-----------------------------------------------------------------------------------------------*/
// This struct needs to newed not malloced
struct BodyData {
  BodyData();
  ~BodyData() = default;

  bool addChunk(const char *start, int64_t size);
  size_t
  getChunkCount() const
  {
    return chunk_list.size();
  }
  bool getChunk(uint32_t chunk_index, const char **start, int64_t *size) const;
  bool removeChunk(uint32_t chunk);
  int64_t
  getSize() const
  {
    return total_size;
  }

  bool     intercept_active = false;
  bool     key_hash_active  = false;
  uint32_t key_hash         = 0;

private:
  struct Chunk {
    Chunk(int64_t size, std::vector<char> &&start) : size(size), start(std::move(start)) {}

    int64_t           size = 0;
    std::vector<char> start;
  };
  int64_t            total_size = 0;
  std::vector<Chunk> chunk_list;
};

/*-----------------------------------------------------------------------------------------------*/
inline BodyData::BodyData()
{
  chunk_list.reserve(c_chunk_add_count);
}

/*-----------------------------------------------------------------------------------------------*/
inline bool
BodyData::addChunk(const char *start, int64_t size)
{
  assert(start != nullptr && size >= 0);
  chunk_list.emplace_back(size, std::vector<char>(start, start + size));
  total_size += size;
  return true;
}

/*-----------------------------------------------------------------------------------------------*/
inline bool
BodyData::getChunk(uint32_t chunk_index, const char **start, int64_t *size) const
{
  assert(start != nullptr && size != nullptr);
  bool bGood = false;
  if (chunk_index < chunk_list.size()) {
    bGood  = true;
    *size  = chunk_list[chunk_index].size;
    *start = chunk_list[chunk_index].start.data();
  } else {
    *size  = 0;
    *start = nullptr;
  }
  return bGood;
}

/*-----------------------------------------------------------------------------------------------*/
inline bool
BodyData::removeChunk(uint32_t chunk)
{
  bool bGood = false;
  if (chunk < chunk_list.size() && !chunk_list[chunk].start.empty()) {
    bGood = true;
    chunk_list[chunk].start.clear();
  }
  return bGood;
}

/*-----------------------------------------------------------------------------------------------*/
