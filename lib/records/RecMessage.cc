/** @file

  Record message definitions

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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_align.h"

#include "P_RecCore.h"
#include "P_RecFile.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "P_RecCore.h"
#include "tscore/I_Layout.h"
#include "tscpp/util/MemSpan.h"

static RecMessageRecvCb g_recv_cb = nullptr;
static void *g_recv_cookie        = nullptr;

//-------------------------------------------------------------------------
// RecMessageAlloc
//-------------------------------------------------------------------------
RecMessage *
RecMessageAlloc(RecMessageT msg_type, int initial_size)
{
  RecMessage *msg;

  msg = (RecMessage *)ats_malloc(sizeof(RecMessageHdr) + initial_size);
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
  int msg_ele_size;
  int rec_name_len         = -1;
  int rec_data_str_len     = -1;
  int rec_data_def_str_len = -1;
  int rec_cfg_chk_len      = -1;
  RecMessageEleHdr *ele_hdr;
  RecRecord *r;
  char *p;

  // find out how much space we need
  msg_ele_size = sizeof(RecMessageEleHdr) + sizeof(RecRecord);
  if (record->name) {
    rec_name_len = strlen(record->name) + 1;
    msg_ele_size += rec_name_len;
  }
  if (record->data_type == RECD_STRING) {
    if (record->data.rec_string) {
      rec_data_str_len = strlen(record->data.rec_string) + 1;
      msg_ele_size += rec_data_str_len;
    }
    if (record->data_default.rec_string) {
      rec_data_def_str_len = strlen(record->data_default.rec_string) + 1;
      msg_ele_size += rec_data_def_str_len;
    }
  }
  if (REC_TYPE_IS_CONFIG(record->rec_type) && (record->config_meta.check_expr)) {
    rec_cfg_chk_len = strlen(record->config_meta.check_expr) + 1;
    msg_ele_size += rec_cfg_chk_len;
  }
  // XXX: this is NOT 8 byte alignment
  // msg_ele_size = 5;
  // (msg_ele_size + 7) & ~7 == 5 !!!
  // msg_ele_size = (msg_ele_size + 7) & ~7;       // 8 byte alignmenet

  msg_ele_size = INK_ALIGN_DEFAULT(msg_ele_size); // 8 byte alignmenet
  // get some space in our buffer
  while (msg->o_end - msg->o_write < msg_ele_size) {
    int realloc_size = (msg->o_end - msg->o_start) * 2;
    msg              = (RecMessage *)ats_realloc(msg, sizeof(RecMessageHdr) + realloc_size);
    msg->o_end       = msg->o_start + realloc_size;
  }
  ele_hdr = (RecMessageEleHdr *)((char *)msg + msg->o_write);
  // The following memset() is pretty CPU intensive, replacing it with something
  // like the below would reduce CPU usage a fair amount. /leif.
  // *((char*)msg + msg->o_write) = 0;
  memset((char *)msg + msg->o_write, 0, msg->o_end - msg->o_write);
  msg->o_write += msg_ele_size;

  // store the record
  ele_hdr->magic  = REC_MESSAGE_ELE_MAGIC;
  ele_hdr->o_next = msg->o_write;
  p               = (char *)ele_hdr + sizeof(RecMessageEleHdr);
  memcpy(p, record, sizeof(RecRecord));
  r = (RecRecord *)p;
  p += sizeof(RecRecord);
  if (rec_name_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_name_len);
    memcpy(p, record->name, rec_name_len);
    r->name = (char *)((uintptr_t)p - (uintptr_t)r);
    p += rec_name_len;
  }
  if (rec_data_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_data_str_len);
    memcpy(p, record->data.rec_string, rec_data_str_len);
    r->data.rec_string = (char *)((uintptr_t)p - (uintptr_t)r);
    p += rec_data_str_len;
  }
  if (rec_data_def_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t)p - (uintptr_t)msg)) >= (uintptr_t)rec_data_def_str_len);
    memcpy(p, record->data_default.rec_string, rec_data_def_str_len);
    r->data_default.rec_string = (char *)((uintptr_t)p - (uintptr_t)r);
    p += rec_data_def_str_len;
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
  itr->ele_hdr = (RecMessageEleHdr *)((char *)msg + msg->o_start);
  itr->next    = 1;

  return RecMessageUnmarshalNext(msg, nullptr, record);
}

//-------------------------------------------------------------------------
// RecMessageUnmarshalNext
//-------------------------------------------------------------------------

int
RecMessageUnmarshalNext(RecMessage *msg, RecMessageItr *itr, RecRecord **record)
{
  RecMessageEleHdr *eh;
  RecRecord *r;

  if (itr == nullptr) {
    if (msg->entries == 0) {
      return REC_ERR_FAIL;
    } else {
      eh = (RecMessageEleHdr *)((char *)msg + msg->o_start);
    }
  } else {
    if (itr->next >= msg->entries) {
      return REC_ERR_FAIL;
    }
    itr->ele_hdr = (RecMessageEleHdr *)((char *)(msg) + itr->ele_hdr->o_next);
    itr->next += 1;
    eh = itr->ele_hdr;
  }

  ink_assert(eh->magic == REC_MESSAGE_ELE_MAGIC);

  // If the file is corrupt, ignore the the rest of the file.
  if (eh->magic != REC_MESSAGE_ELE_MAGIC) {
    Warning("Persistent statistics file records.stat is corrupted. Ignoring the rest of the file\n");
    return REC_ERR_FAIL;
  }

  r = (RecRecord *)((char *)eh + sizeof(RecMessageEleHdr));

  if (r->name) {
    r->name = (char *)r + (intptr_t)(r->name);
  }
  if (r->data_type == RECD_STRING) {
    if (r->data.rec_string) {
      r->data.rec_string = (char *)r + (intptr_t)(r->data.rec_string);
    }
    if (r->data_default.rec_string) {
      r->data_default.rec_string = (char *)r + (intptr_t)(r->data_default.rec_string);
    }
  }
  if (REC_TYPE_IS_CONFIG(r->rec_type) && (r->config_meta.check_expr)) {
    r->config_meta.check_expr = (char *)r + (intptr_t)(r->config_meta.check_expr);
  }

  *record = r;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageRegisterRecvCb
//-------------------------------------------------------------------------

int
RecMessageRegisterRecvCb(RecMessageRecvCb recv_cb, void *cookie)
{
  if (g_recv_cb) {
    return REC_ERR_FAIL;
  }
  g_recv_cookie = cookie;
  g_recv_cb     = recv_cb;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageRecvThis
//-------------------------------------------------------------------------

void
RecMessageRecvThis(ts::MemSpan<void> span)
{
  RecMessage *msg = static_cast<RecMessage *>(span.data());
  g_recv_cb(msg, msg->msg_type, g_recv_cookie);
}

//-------------------------------------------------------------------------
// RecMessageReadFromDisk
//-------------------------------------------------------------------------

RecMessage *
RecMessageReadFromDisk(const char *fpath)
{
  RecMessageHdr msg_hdr;
  RecMessage *msg = nullptr;
  RecHandle h_file;
  int bytes_read;

  if ((h_file = RecFileOpenR(fpath)) == REC_HANDLE_INVALID) {
    goto Lerror;
  }
  if (RecFileRead(h_file, (char *)(&msg_hdr), sizeof(RecMessageHdr), &bytes_read) == REC_ERR_FAIL) {
    goto Lerror;
  }
  msg = (RecMessage *)ats_malloc((msg_hdr.o_end - msg_hdr.o_start) + sizeof(RecMessageHdr));
  memcpy(msg, &msg_hdr, sizeof(RecMessageHdr));
  if (RecSnapFileRead(h_file, (char *)(msg) + msg_hdr.o_start, msg_hdr.o_end - msg_hdr.o_start, &bytes_read) == REC_ERR_FAIL) {
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
  int msg_size;
  RecHandle h_file;
  int bytes_written;

  // Cap the message (e.g. when we read it, o_end should reflect the
  // size of the new buffer that we write to disk, not the size of the
  // buffer in memory).
  msg->o_end = msg->o_write;

  msg_size = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
  if ((h_file = RecFileOpenW(fpath)) != REC_HANDLE_INVALID) {
    if (RecSnapFileWrite(h_file, (char *)msg, msg_size, &bytes_written) == REC_ERR_FAIL) {
      RecFileClose(h_file);
      return REC_ERR_FAIL;
    }
    RecFileClose(h_file);
  }

  return REC_ERR_OKAY;
}
