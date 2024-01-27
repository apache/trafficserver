/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BodyData.h"

#include <catch.hpp>
#include <string>
#include <string_view>

DEF_DBG_CTL(PLUGIN_TAG_BODY)

std::string_view DATA_PACKET0{"The quick brown fox jumps over the lazy dog"};
std::string_view DATA_PACKET1{"Now is the time for all good men to come to the aid of their country"};

TEST_CASE("Body Data")
{
  BodyData pBody;
  REQUIRE(pBody.getSize() == 0);
  REQUIRE(pBody.getChunkCount() == 0);
  char const *pData = 0;
  int64_t dataLen   = 0;
  REQUIRE(pBody.getChunk(0, &pData, &dataLen) == false);

  // First chunk.
  pBody.addChunk(DATA_PACKET0.data(), DATA_PACKET0.size());
  REQUIRE(pBody.getSize() == static_cast<int64_t>(DATA_PACKET0.size()));
  REQUIRE(pBody.getChunkCount() == 1);
  pBody.getChunk(0, &pData, &dataLen);
  std::string dataRead0(pData, dataLen);
  std::string dataOrig0(DATA_PACKET0.data(), DATA_PACKET0.size());
  REQUIRE(dataRead0 == dataOrig0);

  // Second chunk.
  pBody.addChunk(DATA_PACKET1.data(), DATA_PACKET1.size());
  REQUIRE(pBody.getSize() == static_cast<int64_t>(DATA_PACKET0.size() + DATA_PACKET1.size()));
  REQUIRE(pBody.getChunkCount() == 2);
  pBody.getChunk(1, &pData, &dataLen);
  std::string dataRead1(pData, dataLen);
  std::string dataOrig1(DATA_PACKET1.data(), DATA_PACKET1.size());
  REQUIRE(dataRead1 == dataOrig1);

  REQUIRE(pBody.removeChunk(0) == true);
  REQUIRE(pBody.removeChunk(0) == false);
  REQUIRE(pBody.removeChunk(1) == true);
  REQUIRE(pBody.removeChunk(1) == false);
  REQUIRE(pBody.removeChunk(97) == false);
}
