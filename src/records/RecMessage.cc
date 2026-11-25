/** @file

  Record message definitions

  @par License

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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_align.h"

#include "P_RecCore.h"
#include "P_RecFile.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "P_RecCore.h"
#include "tscore/Layout.h"
#include "swoc/MemSpan.h"

#include <cstring>
#include <type_traits>

//-------------------------------------------------------------------------
// RecMessageAlloc
//-------------------------------------------------------------------------
RecMessage *
RecMessageAlloc(RecMessageT msg_type, int initial_size)
{
  RecMessage *msg;

  msg = static_cast<RecMessage *>(ats_malloc(sizeof(RecMessageHdr) + initial_size));
  memset(msg, 0, sizeof(RecMessageHdr) + initial_size);
  msg->msg_type = msg_type;
  msg->o_start  = sizeof(RecMessageHdr);
  msg->o_write  = sizeof(RecMessageHdr);
  msg->o_end    = sizeof(RecMessageHdr) + initial_size;
  msg->entries  = 0;

  return msg;
}

//-------------------------------------------------------------------------
// RecMessageFree
//-------------------------------------------------------------------------

int
RecMessageFree(RecMessage *msg)
{
  ats_free(msg);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageMarshal_Realloc
//-------------------------------------------------------------------------
RecMessage *
RecMessageMarshal_Realloc(RecMessage *msg, const RecRecord *record)
{
  int               msg_ele_size;
  int               rec_name_len         = -1;
  int               rec_data_str_len     = -1;
  int               rec_data_def_str_len = -1;
  int               rec_cfg_chk_len      = -1;
  RecMessageEleHdr *ele_hdr;
  RecRecord        *r;
  char             *p;

  // find out how much space we need
  msg_ele_size = sizeof(RecMessageEleHdr) + sizeof(RecRecord);
  if (record->name) {
    rec_name_len  = strlen(record->name) + 1;
    msg_ele_size += rec_name_len;
  }
  if (record->data_type == RECD_STRING) {
    if (record->data.rec_string) {
      rec_data_str_len  = strlen(record->data.rec_string) + 1;
      msg_ele_size     += rec_data_str_len;
    }
    if (record->data_default.rec_string) {
      rec_data_def_str_len  = strlen(record->data_default.rec_string) + 1;
      msg_ele_size         += rec_data_def_str_len;
    }
  }
  if (REC_TYPE_IS_CONFIG(record->rec_type) && (record->config_meta.check_expr)) {
    rec_cfg_chk_len  = strlen(record->config_meta.check_expr) + 1;
    msg_ele_size    += rec_cfg_chk_len;
  }
  // XXX: this is NOT 8 byte alignment
  // msg_ele_size = 5;
  // (msg_ele_size + 7) & ~7 == 5 !!!
  // msg_ele_size = (msg_ele_size + 7) & ~7;       // 8 byte alignment

  msg_ele_size = INK_ALIGN_DEFAULT(msg_ele_size); // 8 byte alignment
  // get some space in our buffer
  while (msg->o_end - msg->o_write < msg_ele_size) {
    int realloc_size = (msg->o_end - msg->o_start) * 2;
    msg              = static_cast<RecMessage *>(ats_realloc(msg, sizeof(RecMessageHdr) + realloc_size));
    msg->o_end       = msg->o_start + realloc_size;
  }
  ele_hdr = reinterpret_cast<RecMessageEleHdr *>(reinterpret_cast<char *>(msg) + msg->o_write);
  // The following memset() is pretty CPU intensive, replacing it with something
  // like the below would reduce CPU usage a fair amount. /leif.
  // *((char*)msg + msg->o_write) = 0;
  memset(reinterpret_cast<char *>(msg) + msg->o_write, 0, msg->o_end - msg->o_write);
  msg->o_write += msg_ele_size;

  // store the record
  ele_hdr->magic  = REC_MESSAGE_ELE_MAGIC;
  ele_hdr->o_next = msg->o_write;
  p               = reinterpret_cast<char *>(ele_hdr) + sizeof(RecMessageEleHdr);
  memcpy(p, record, sizeof(RecRecord));
  r  = reinterpret_cast<RecRecord *>(p);
  p += sizeof(RecRecord);
  if (rec_name_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_name_len);
    memcpy(p, record->name, rec_name_len);
    r->name  = (char *)((uintptr_t)p - (uintptr_t)r);
    p       += rec_name_len;
  }
  if (rec_data_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_data_str_len);
    memcpy(p, record->data.rec_string, rec_data_str_len);
    r->data.rec_string  = (char *)((uintptr_t)p - (uintptr_t)r);
    p                  += rec_data_str_len;
  }
  if (rec_data_def_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_data_def_str_len);
    memcpy(p, record->data_default.rec_string, rec_data_def_str_len);
    r->data_default.rec_string  = (char *)((uintptr_t)p - (uintptr_t)r);
    p                          += rec_data_def_str_len;
  }
  if (rec_cfg_chk_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_cfg_chk_len);
    memcpy(p, record->config_meta.check_expr, rec_cfg_chk_len);
    r->config_meta.check_expr = (char *)((uintptr_t)p - (uintptr_t)r);
  }

  msg->entries += 1;

  return msg;
}

//-------------------------------------------------------------------------
// RecMessageUnmarshalFirst
//-------------------------------------------------------------------------

int
RecMessageUnmarshalFirst(RecMessage *msg, RecMessageItr *itr, RecRecord **record)
{
  itr->ele_hdr = reinterpret_cast<RecMessageEleHdr *>(reinterpret_cast<char *>(msg) + msg->o_start);
  itr->next    = 1;

  return RecMessageUnmarshalNext(msg, nullptr, record);
}

//-------------------------------------------------------------------------
// RecMessageUnmarshalNext
//-------------------------------------------------------------------------

// The buffer comes from malloc, so an element offset that is a multiple of INK_MIN_ALIGN is enough to align both
// structures read from it. The header size keeps every offset the writer produces a multiple too.
static_assert(sizeof(RecMessageHdr) % INK_MIN_ALIGN == 0);
static_assert(alignof(RecMessageEleHdr) <= INK_MIN_ALIGN && alignof(RecRecord) <= INK_MIN_ALIGN);
static_assert(sizeof(RecMessageEleHdr) % alignof(RecRecord) == 0);

namespace
{
// A message read back from records.snap is only as sound as the file, so every offset in it must land inside
// the buffer, which is o_end bytes long.
bool
fits_in_message(const RecMessage *msg, intptr_t offset, intptr_t len)
{
  return offset >= msg->o_start && offset <= msg->o_end && len <= msg->o_end - offset;
}

// The string offsets are relative to the record and become pointers only once the string is known to end
// inside the message.
template <typename Char>
bool
resolve_string(RecMessage *msg, const RecRecord *r, Char *&str)
{
  if (str == nullptr) {
    return true;
  }

  // Bound rel before adding it, so a wild offset from the file cannot overflow the sum.
  intptr_t const rel = reinterpret_cast<intptr_t>(str);
  if (rel < 0 || rel > msg->o_end) {
    return false;
  }
  intptr_t const offset = (reinterpret_cast<const char *>(r) - reinterpret_cast<const char *>(msg)) + rel;
  if (!fits_in_message(msg, offset, 1)) {
    return false;
  }

  char *p = reinterpret_cast<char *>(msg) + offset;
  if (memchr(p, '\0', msg->o_end - offset) == nullptr) {
    return false;
  }
  str = p;
  return true;
}

// RecT and RecDataT have no fixed underlying type, so loading a field from the file as the enum is undefined when its
// bytes hold a value outside the enum's range. Copy the bytes out as the underlying integer and check that instead.
template <typename Enum>
std::underlying_type_t<Enum>
stored_value(const Enum *field)
{
  std::underlying_type_t<Enum> value;
  std::memcpy(&value, field, sizeof(value));
  return value;
}
} // namespace

int
RecMessageUnmarshalNext(RecMessage *msg, RecMessageItr *itr, RecRecord **record)
{
  RecMessageEleHdr *eh;
  RecRecord        *r;
  intptr_t          offset;
  intptr_t const    ele_size = sizeof(RecMessageEleHdr) + sizeof(RecRecord);

  auto corrupt = [](const char *why) {
    Warning("Persistent statistics file records.snap is corrupted (%s). Ignoring the rest of the file\n", why);
    return REC_ERR_FAIL;
  };

  if (itr == nullptr) {
    if (msg->entries == 0) {
      return REC_ERR_FAIL;
    }
    offset = msg->o_start;
  } else {
    if (itr->next >= msg->entries) {
      return REC_ERR_FAIL;
    }
    // The next element must start after this element's header and record, or it would overlap a record whose
    // string offsets have already been turned into pointers.
    intptr_t const current = reinterpret_cast<char *>(itr->ele_hdr) - reinterpret_cast<char *>(msg);
    offset                 = itr->ele_hdr->o_next;
    if (offset < current + ele_size) {
      return corrupt("element overlaps the previous one");
    }
  }

  if (!fits_in_message(msg, offset, ele_size)) {
    return corrupt("element outside the snapshot");
  }
  // RecMessageMarshal_Realloc() starts the first element right after the header and rounds every element up to
  // INK_MIN_ALIGN. At any other offset the element header or the record behind it is read misaligned.
  if (offset % INK_MIN_ALIGN != 0) {
    return corrupt("misaligned element");
  }
  eh = reinterpret_cast<RecMessageEleHdr *>(reinterpret_cast<char *>(msg) + offset);
  if (itr != nullptr) {
    itr->ele_hdr  = eh;
    itr->next    += 1;
  }

  // If the file is corrupt, ignore the rest of the file.
  if (eh->magic != REC_MESSAGE_ELE_MAGIC) {
    return corrupt("bad element magic");
  }

  r = reinterpret_cast<RecRecord *>(reinterpret_cast<char *>(eh) + sizeof(RecMessageEleHdr));

  // RecSyncStatsFile() writes only stats. RecReadStatsFile() would restore any other kind as a new record, where an
  // unknown kind asserts in RecSetRecord() and a configuration kind aborts the later registration of that stat.
  if (!REC_TYPE_IS_STAT(stored_value(&r->rec_type))) {
    return corrupt("not a stat record");
  }
  if (!resolve_string(msg, r, r->name)) {
    return corrupt("bad record name");
  }
  // RecSetRecord() reads RECD_NULL data as a string pointer, which the bytes from the file are not, and RecDataSet()
  // asserts on any type past RECD_COUNTER.
  auto const data_type = stored_value(&r->data_type);
  if (data_type <= RECD_NULL || data_type >= RECD_MAX) {
    return corrupt("bad data type");
  }
  if (data_type == RECD_STRING) {
    if (!resolve_string(msg, r, r->data.rec_string) || !resolve_string(msg, r, r->data_default.rec_string)) {
      return corrupt("bad string value");
    }
  }

  *record = r;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageReadFromDisk
//-------------------------------------------------------------------------

RecMessage *
RecMessageReadFromDisk(const char *fpath)
{
  RecMessageHdr msg_hdr;
  RecMessage   *msg = nullptr;
  RecHandle     h_file;
  int           bytes_read;
  struct stat   file_stat;

  if ((h_file = RecFileOpenR(fpath)) == REC_HANDLE_INVALID) {
    goto Lerror;
  }
  if (RecSnapFileRead(h_file, reinterpret_cast<char *>(&msg_hdr), sizeof(RecMessageHdr), &bytes_read) == REC_ERR_FAIL ||
      bytes_read != static_cast<int>(sizeof(RecMessageHdr))) {
    goto Lerror;
  }
  if (fstat(h_file, &file_stat) != 0) {
    goto Lerror;
  }
  // RecMessageWriteToDisk() puts the message, header included, after the version bytes, with
  // o_start at the end of the header and o_end at the end of the message.
  if (msg_hdr.o_start != static_cast<int>(sizeof(RecMessageHdr)) || msg_hdr.o_end < msg_hdr.o_start ||
      msg_hdr.o_end > file_stat.st_size - static_cast<off_t>(VERSION_HDR_SIZE)) {
    goto Lerror;
  }
  msg = static_cast<RecMessage *>(ats_malloc(msg_hdr.o_end));
  if (RecSnapFileRead(h_file, reinterpret_cast<char *>(msg), msg_hdr.o_end, &bytes_read) == REC_ERR_FAIL ||
      bytes_read != msg_hdr.o_end) {
    goto Lerror;
  }
  // Every later bound comes from the header in msg, so it must be the one checked above, not a copy of a file
  // that changed between the two reads.
  if (memcmp(msg, &msg_hdr, sizeof(msg_hdr)) != 0) {
    goto Lerror;
  }

  goto Ldone;

Lerror:
  ats_free(msg);
  msg = nullptr;

Ldone:
  if (h_file != REC_HANDLE_INVALID) {
    RecFileClose(h_file);
  }

  return msg;
}

//-------------------------------------------------------------------------
// RecMessageWriteToDisk
//-------------------------------------------------------------------------

int
RecMessageWriteToDisk(RecMessage *msg, const char *fpath)
{
  int       msg_size;
  RecHandle h_file;
  int       bytes_written;

  // Cap the message (e.g. when we read it, o_end should reflect the
  // size of the new buffer that we write to disk, not the size of the
  // buffer in memory).
  msg->o_end = msg->o_write;

  msg_size = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
  if ((h_file = RecFileOpenW(fpath)) != REC_HANDLE_INVALID) {
    if (RecSnapFileWrite(h_file, reinterpret_cast<char *>(msg), msg_size, &bytes_written) == REC_ERR_FAIL) {
      RecFileClose(h_file);
      return REC_ERR_FAIL;
    }
    RecFileClose(h_file);
  }

  return REC_ERR_OKAY;
}
