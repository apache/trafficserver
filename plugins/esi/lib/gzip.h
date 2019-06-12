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

#include <string>
#include <list>

// Defines for various GZIP constants
static const int COMPRESSION_LEVEL = 6;
static const int ZLIB_MEM_LEVEL    = 8;

static const int GZIP_HEADER_SIZE  = 10;
static const int GZIP_TRAILER_SIZE = 8;

static const char MAGIC_BYTE_1 = 0x1f;
static const char MAGIC_BYTE_2 = 0x8b;
static const char OS_TYPE      = 3; // Unix

static const int BUF_SIZE = 1 << 15; // 32k buffer

namespace EsiLib
{
struct ByteBlock {
  const char *data;
  int data_len;
  ByteBlock(const char *d = nullptr, int d_len = 0) : data(d), data_len(d_len){};
};

typedef std::list<ByteBlock> ByteBlockList;

bool gzip(const ByteBlockList &blocks, std::string &cdata);

inline bool
gzip(const char *data, int data_len, std::string &cdata)
{
  ByteBlockList blocks;
  blocks.push_back(ByteBlock(data, data_len));
  return gzip(blocks, cdata);
}

typedef std::list<std::string> BufferList;

bool gunzip(const char *data, int data_len, BufferList &buf_list);
} // namespace EsiLib
