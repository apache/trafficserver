/** @file

   Catch-based tests for reading the persistent stats snapshot in RecMessage.cc

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

#include <catch2/catch_test_macros.hpp>

#include "../P_RecFile.h"
#include "../P_RecMessage.h"
#include "../P_RecUtils.h"
#include "tscore/ink_memory.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace
{
std::filesystem::path
snap_path(const char *name)
{
  return std::filesystem::temp_directory_path() / (std::string("test_RecMessage.") + std::to_string(getpid()) + "." + name);
}

// Write a snapshot laid out the way RecSnapFileWrite() does: version bytes, then the message.
void
write_snap(const std::filesystem::path &path, const void *msg, size_t len)
{
  const char    version[VERSION_HDR_SIZE] = {'V', '1', '0', '0', '\0'};
  std::ofstream out(path, std::ios::binary | std::ios::trunc);

  out.write(version, sizeof(version));
  out.write(static_cast<const char *>(msg), len);
}
} // namespace

TEST_CASE("RecMessageReadFromDisk reads back what RecMessageWriteToDisk wrote", "[librecords][RecMessage]")
{
  auto      path = snap_path("roundtrip");
  RecRecord rec;

  RecRecordInit(&rec);
  rec.rec_type     = RECT_PROCESS;
  rec.data_type    = RECD_INT;
  rec.name         = "proxy.process.test.snapshot";
  rec.data.rec_int = 42;

  RecMessage *out = RecMessageMarshal_Realloc(RecMessageAlloc(RECG_NULL), &rec);
  REQUIRE(RecMessageWriteToDisk(out, path.c_str()) == REC_ERR_OKAY);
  RecMessageFree(out);

  RecMessage *in = RecMessageReadFromDisk(path.c_str());
  REQUIRE(in != nullptr);

  RecMessageItr itr;
  RecRecord    *got = nullptr;
  REQUIRE(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_OKAY);
  CHECK(std::string(got->name) == rec.name);
  CHECK(got->data.rec_int == 42);
  CHECK(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_FAIL);

  ats_free(in);
  std::filesystem::remove(path);
}

TEST_CASE("RecMessageReadFromDisk rejects a corrupt snapshot", "[librecords][RecMessage]")
{
  RecMessageHdr hdr{};

  SECTION("every header field out of range")
  {
    const int msg_type = 16;

    std::memcpy(&hdr.msg_type, &msg_type, sizeof(msg_type));
    hdr.o_start = 1 << 24;
    hdr.o_write = 16;
    hdr.o_end   = 0;
  }
  SECTION("start offset past the header")
  {
    hdr.o_start = sizeof(RecMessageHdr) + 4096;
    hdr.o_write = hdr.o_end = hdr.o_start + 64;
  }
  SECTION("end offset before the start offset")
  {
    hdr.o_start = sizeof(RecMessageHdr);
    hdr.o_write = hdr.o_end = 8;
  }
  SECTION("end offset past the end of the file")
  {
    hdr.o_start = sizeof(RecMessageHdr);
    hdr.o_write = hdr.o_end = hdr.o_start + 4096;
    hdr.entries             = 1;
  }

  auto path = snap_path("corrupt");
  write_snap(path, &hdr, sizeof(hdr));
  CHECK(RecMessageReadFromDisk(path.c_str()) == nullptr);
  std::filesystem::remove(path);
}

TEST_CASE("RecMessageReadFromDisk rejects a truncated header", "[librecords][RecMessage]")
{
  RecMessageHdr hdr{};

  hdr.o_start = sizeof(RecMessageHdr);
  hdr.o_write = hdr.o_end = sizeof(RecMessageHdr);

  auto path = snap_path("truncated");
  write_snap(path, &hdr, sizeof(hdr) / 2);
  CHECK(RecMessageReadFromDisk(path.c_str()) == nullptr);
  std::filesystem::remove(path);
}
