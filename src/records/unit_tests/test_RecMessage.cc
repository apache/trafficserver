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

TEST_CASE("RecMessageReadFromDisk rejects a corrupt snapshot header", "[librecords][RecMessage]")
{
  RecMessageHdr hdr{};

  SECTION("garbage in every header field")
  {
    const int msg_type = 16;

    std::memcpy(&hdr.msg_type, &msg_type, sizeof(msg_type));
    hdr.o_start = 1 << 24;
    hdr.o_write = 16;
    hdr.o_end   = 0;
  }
  SECTION("negative start offset")
  {
    hdr.o_start = -8;
    hdr.o_write = hdr.o_end = sizeof(RecMessageHdr);
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

namespace
{
RecMessage *
marshal_records(int count, const char *name = "proxy.process.test.snapshot")
{
  RecMessage *msg = RecMessageAlloc(RECG_NULL);

  for (int i = 0; i < count; ++i) {
    RecRecord rec;

    RecRecordInit(&rec);
    rec.rec_type     = RECT_PROCESS;
    rec.data_type    = RECD_INT;
    rec.name         = name;
    rec.data.rec_int = i;
    msg              = RecMessageMarshal_Realloc(msg, &rec);
  }
  return msg;
}

RecMessageEleHdr *
first_element(RecMessage *msg)
{
  return reinterpret_cast<RecMessageEleHdr *>(reinterpret_cast<char *>(msg) + msg->o_start);
}

RecRecord *
element_record(RecMessageEleHdr *eh)
{
  return reinterpret_cast<RecRecord *>(reinterpret_cast<char *>(eh) + sizeof(RecMessageEleHdr));
}

RecMessage *
write_and_read(RecMessage *msg, const std::filesystem::path &path)
{
  REQUIRE(RecMessageWriteToDisk(msg, path.c_str()) == REC_ERR_OKAY);
  RecMessageFree(msg);

  RecMessage *in = RecMessageReadFromDisk(path.c_str());
  REQUIRE(in != nullptr);
  return in;
}
} // namespace

TEST_CASE("RecMessageUnmarshalNext walks every element of a valid snapshot", "[librecords][RecMessage]")
{
  auto        path = snap_path("walk");
  RecMessage *msg  = RecMessageAlloc(RECG_NULL);
  RecRecord   rec;

  // One of each persisted stat type, so the data type bound must admit RECD_COUNTER, the last stored type.
  RecRecordInit(&rec);
  rec.rec_type     = RECT_PROCESS;
  rec.data_type    = RECD_INT;
  rec.name         = "proxy.process.test.int";
  rec.data.rec_int = 7;
  msg              = RecMessageMarshal_Realloc(msg, &rec);

  RecRecordInit(&rec);
  rec.rec_type       = RECT_PROCESS;
  rec.data_type      = RECD_FLOAT;
  rec.name           = "proxy.process.test.float";
  rec.data.rec_float = 2.5;
  msg                = RecMessageMarshal_Realloc(msg, &rec);

  RecRecordInit(&rec);
  rec.rec_type         = RECT_PROCESS;
  rec.data_type        = RECD_COUNTER;
  rec.name             = "proxy.process.test.counter";
  rec.data.rec_counter = 11;
  msg                  = RecMessageMarshal_Realloc(msg, &rec);

  RecMessage   *in = write_and_read(msg, path);
  RecMessageItr itr;
  RecRecord    *got = nullptr;

  REQUIRE(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_OKAY);
  CHECK(std::string(got->name) == "proxy.process.test.int");
  CHECK(got->data.rec_int == 7);

  REQUIRE(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_OKAY);
  CHECK(std::string(got->name) == "proxy.process.test.float");
  CHECK(got->data.rec_float == 2.5);

  REQUIRE(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_OKAY);
  CHECK(std::string(got->name) == "proxy.process.test.counter");
  CHECK(got->data.rec_counter == 11);

  // The walk ends because every entry was read, not because an element was rejected.
  CHECK(itr.next == in->entries);
  CHECK(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_FAIL);

  ats_free(in);
  std::filesystem::remove(path);
}

TEST_CASE("RecMessageUnmarshalNext stops at an element that does not fit the snapshot", "[librecords][RecMessage]")
{
  auto          path = snap_path("elements");
  RecMessageItr itr;
  RecRecord    *got = nullptr;

  SECTION("an entry with no element after the header")
  {
    RecMessageHdr hdr{};

    hdr.o_start = sizeof(RecMessageHdr);
    hdr.o_write = hdr.o_end = sizeof(RecMessageHdr);
    hdr.entries             = 1;
    write_snap(path, &hdr, sizeof(hdr));

    RecMessage *in = RecMessageReadFromDisk(path.c_str());
    REQUIRE(in != nullptr);
    CHECK(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a next element that does not come after the current one")
  {
    // A record without a name has no string offset to resolve, so reading the first element again would succeed;
    // only the ordering check can reject it.
    RecMessage *msg = marshal_records(2, nullptr);

    first_element(msg)->o_next = msg->o_start;

    RecMessage *in = write_and_read(msg, path);
    REQUIRE(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_OKAY);
    CHECK(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a next element past the end of the snapshot")
  {
    RecMessage *msg = marshal_records(2);

    first_element(msg)->o_next = msg->o_end;

    RecMessage *in = write_and_read(msg, path);
    REQUIRE(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_OKAY);
    CHECK(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a name offset past the end of the snapshot")
  {
    RecMessage *msg = marshal_records(1);

    element_record(first_element(msg))->name = reinterpret_cast<const char *>(static_cast<intptr_t>(msg->o_end) * 2);

    RecMessage *in = write_and_read(msg, path);
    CHECK(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a name offset that passes the offset bound but starts past the end")
  {
    RecMessage *msg = marshal_records(1);

    // The read-back o_end is o_write, so this relative offset is in range but the name it locates is not.
    element_record(first_element(msg))->name = reinterpret_cast<const char *>(static_cast<intptr_t>(msg->o_write));

    RecMessage *in = write_and_read(msg, path);
    CHECK(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a data type that is not a stored type")
  {
    RecMessage *msg = marshal_records(1);
    RecDataT    type{};

    SECTION("RECD_NULL")
    {
      type = RECD_NULL;
    }
    SECTION("RECD_MAX")
    {
      type = RECD_MAX;
    }
    element_record(first_element(msg))->data_type = type;

    RecMessage *in = write_and_read(msg, path);
    CHECK(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }
  SECTION("a name that is not terminated inside the snapshot")
  {
    RecMessage *msg = marshal_records(1);
    RecRecord  *r   = element_record(first_element(msg));
    // o_end still spans the whole allocation, but only the bytes up to o_write reach the file, so the name must
    // point at the last of those for the read-back offset to pass the bounds check and leave one unterminated byte.
    char *last = reinterpret_cast<char *>(msg) + msg->o_write - 1;

    *last   = 'x';
    r->name = reinterpret_cast<const char *>(last - reinterpret_cast<char *>(r));

    RecMessage *in = write_and_read(msg, path);
    CHECK(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_FAIL);
    ats_free(in);
  }

  std::filesystem::remove(path);
}

TEST_CASE("RecMessageUnmarshalNext rejects a misaligned element", "[librecords][RecMessage]")
{
  auto          path = snap_path("misaligned");
  RecMessageItr itr;
  RecRecord    *got   = nullptr;
  int           shift = 0;

  // Both shifts keep the element inside the snapshot and after the first one. Four bytes still aligns the element
  // header, so only the writer's own alignment rule rejects it.
  SECTION("by one byte")
  {
    shift = 1;
  }
  SECTION("by four bytes")
  {
    shift = 4;
  }

  // Records without names have no string offsets, so the shifted copy is a complete element on its own.
  RecMessage       *msg    = marshal_records(3, nullptr);
  RecMessageEleHdr *first  = first_element(msg);
  char             *second = reinterpret_cast<char *>(msg) + first->o_next;

  std::memmove(second + shift, second, sizeof(RecMessageEleHdr) + sizeof(RecRecord));
  first->o_next += shift;

  RecMessage *in = write_and_read(msg, path);
  REQUIRE(RecMessageUnmarshalFirst(in, &itr, &got) == REC_ERR_OKAY);
  CHECK(RecMessageUnmarshalNext(in, &itr, &got) == REC_ERR_FAIL);
  ats_free(in);
  std::filesystem::remove(path);
}
