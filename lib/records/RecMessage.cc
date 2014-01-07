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

#include "libts.h"

#include "P_RecFile.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "I_Layout.h"

static bool g_message_initialized = false;
static RecMessageRecvCb g_recv_cb = NULL;
static void *g_recv_cookie = NULL;

//-------------------------------------------------------------------------
//
// REC_BUILD_STAND_ALONE IMPLEMENTATION
//
//-------------------------------------------------------------------------
#if defined (REC_BUILD_STAND_ALONE)

extern RecModeT g_mode_type;

static LLQ *g_send_llq = NULL;
static LLQ *g_recv_llq = NULL;

//-------------------------------------------------------------------------
// send_thr
//-------------------------------------------------------------------------

static void *
send_thr(void *data)
{
  int msg_size;
  RecMessageHdr *msg_hdr;
  RecHandle h_pipe = (RecHandle)(intptr_t)data;
  while (true) {
    // dequeue will block if there's nothing in the queue
    msg_hdr = (RecMessageHdr *) dequeue(g_send_llq);
    msg_size = (msg_hdr->o_end - msg_hdr->o_start) + sizeof(RecMessageHdr);
    if (RecPipeWrite(h_pipe, (char *) msg_hdr, msg_size) == REC_ERR_FAIL) {
      ink_release_assert("Pipe write failed, message lost");
    }
    ats_free(msg_hdr);
  }
  return NULL;
}

//-------------------------------------------------------------------------
// recv_thr
//-------------------------------------------------------------------------

static void *
recv_thr(void *data)
{
  int msg_size = 0;
  RecMessageHdr msg_hdr;
  RecMessage *msg;
  RecHandle h_pipe = (RecHandle)(intptr_t)data;
  while (true) {
    if (RecPipeRead(h_pipe, (char *) (&msg_hdr), sizeof(RecMessageHdr)) == REC_ERR_FAIL) {
      ink_release_assert("Pipe read failed");
    }
    msg = (RecMessage *)ats_malloc((msg_hdr.o_end - msg_hdr.o_start) + sizeof(RecMessageHdr));
    memcpy(msg, &msg_hdr, sizeof(RecMessageHdr));
    if (RecPipeRead(h_pipe, (char *) (msg) + msg_hdr.o_start, msg_hdr.o_end - msg_hdr.o_start) == REC_ERR_FAIL) {
      ink_release_assert("Pipe read failed");
    }
    msg_size = msg_hdr.o_end - msg_hdr.o_start + sizeof(RecMessageHdr);
    enqueue(g_recv_llq, msg);
  }
  return NULL;
}

//-------------------------------------------------------------------------
// accept_thr
//-------------------------------------------------------------------------

static void *
accept_thr(void *data)
{
  xptr<char> rundir(RecConfigReadRuntimeDir());
  RecHandle h_pipe;
  h_pipe = RecPipeCreate(rundir, REC_PIPE_NAME);
  ink_thread_create(send_thr, (void *) h_pipe);
  ink_thread_create(recv_thr, (void *) h_pipe);
  return NULL;
}

//-------------------------------------------------------------------------
// recv_cb_thr
//-------------------------------------------------------------------------

static void *
recv_cb_thr(void *data)
{
  RecMessage *msg;
  while (true) {
    if (g_recv_cb) {
      msg = (RecMessage *) dequeue(g_recv_llq);
      RecMessageRecvThis(0, (char *) msg, 0);
      ats_free(msg);
    }
  }
  return NULL;
}

//-------------------------------------------------------------------------
// RecMessageInit
//-------------------------------------------------------------------------

int
RecMessageInit()
{

  RecHandle h_pipe;

  if (g_message_initialized) {
    return REC_ERR_OKAY;
  }

  /*
   * g_mode_type should be initialized by
   * RecLocalInit() or RecProcessInit() earlier.
   */
  ink_assert(g_mode_type != RECM_NULL);

  g_send_llq = create_queue();
  g_recv_llq = create_queue();

  switch (g_mode_type) {
  case RECM_CLIENT:
    h_pipe = RecPipeConnect(Layout::get()->runtimedir, REC_PIPE_NAME);
    if (h_pipe == REC_HANDLE_INVALID) {
      return REC_ERR_FAIL;
    }
    ink_thread_create(send_thr, (void *) h_pipe);
    ink_thread_create(recv_thr, (void *) h_pipe);
    break;
  case RECM_SERVER:
    ink_thread_create(accept_thr, NULL);
    break;
  case RECM_NULL:
  case RECM_STAND_ALONE:
  default:
    ink_assert(!"Unexpected RecModeT type");
    break;
  }

  ink_thread_create(recv_cb_thr, NULL);

  g_message_initialized = true;

  return REC_ERR_OKAY;

}

//-------------------------------------------------------------------------
// RecMessageSend
//-------------------------------------------------------------------------

int
RecMessageSend(RecMessage * msg)
{

  RecMessage *msg_cpy;
  int msg_cpy_size;

  // Make a copy of the record, but truncate it to the size actually used
  if (g_mode_type == RECM_CLIENT || g_mode_type == RECM_SERVER) {
    msg_cpy_size = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
    msg_cpy = (RecMessage *)ats_malloc(msg_cpy_size);
    memcpy(msg_cpy, msg, msg_cpy_size);
    msg_cpy->o_end = msg_cpy->o_write;
    enqueue(g_send_llq, (void *) msg_cpy);
  }

  return REC_ERR_OKAY;

}

//-------------------------------------------------------------------------
//
// REC_BUILD_MGMT IMPLEMENTATION
//
//-------------------------------------------------------------------------
#elif defined (REC_BUILD_MGMT)

#if defined(LOCAL_MANAGER)
#include "LocalManager.h"
#elif defined(PROCESS_MANAGER)
#include "ProcessManager.h"
#else
#error "Required #define not specificed; expected LOCAL_MANAGER or PROCESS_MANAGER"
#endif

//-------------------------------------------------------------------------
// RecMessageInit
//-------------------------------------------------------------------------

int
RecMessageInit()
{
  if (g_message_initialized) {
    return REC_ERR_OKAY;
  }

  /*
   * g_mode_type should be initialized by
   * RecLocalInit() or RecProcessInit() earlier.
   */
  ink_assert(g_mode_type != RECM_NULL);

#if defined (LOCAL_MANAGER)
  lmgmt->registerMgmtCallback(MGMT_SIGNAL_LIBRECORDS, RecMessageRecvThis, NULL);
#elif defined(PROCESS_MANAGER)
  pmgmt->registerMgmtCallback(MGMT_EVENT_LIBRECORDS, RecMessageRecvThis, NULL);
#endif

  g_message_initialized = true;
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageSend
//-------------------------------------------------------------------------

int
RecMessageSend(RecMessage * msg)
{
  int msg_size;

  if (!g_message_initialized)
    return REC_ERR_OKAY;

  // Make a copy of the record, but truncate it to the size actually used
  if (g_mode_type == RECM_CLIENT || g_mode_type == RECM_SERVER) {
    msg->o_end = msg->o_write;
    msg_size = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
#if defined (LOCAL_MANAGER)
    lmgmt->signalEvent(MGMT_EVENT_LIBRECORDS, (char *) msg, msg_size);
#elif defined(PROCESS_MANAGER)
    pmgmt->signalManager(MGMT_SIGNAL_LIBRECORDS, (char *) msg, msg_size);
#endif
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
//
// STUB IMPLEMENTATION
//
//-------------------------------------------------------------------------
#elif defined (REC_BUILD_STUB)
#else
#error "Required #define not specificed; expected REC_BUILD_STAND_ALONE, REC_BUILD_MGMT, or REC_BUILD_STUB"
#endif

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
  msg->o_start = sizeof(RecMessageHdr);
  msg->o_write = sizeof(RecMessageHdr);
  msg->o_end = sizeof(RecMessageHdr) + initial_size;
  msg->entries = 0;

  return msg;
}

//-------------------------------------------------------------------------
// RecMessageFree
//-------------------------------------------------------------------------

int
RecMessageFree(RecMessage * msg)
{
  ats_free(msg);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageMarshal_Realloc
//-------------------------------------------------------------------------
RecMessage *
RecMessageMarshal_Realloc(RecMessage * msg, const RecRecord * record)
{
  int msg_ele_size;
  int rec_name_len = -1;
  int rec_data_str_len = -1;
  int rec_data_def_str_len = -1;
  int rec_cfg_chk_len = -1;
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

  msg_ele_size = INK_ALIGN_DEFAULT(msg_ele_size);  // 8 byte alignmenet
  // get some space in our buffer
  while (msg->o_end - msg->o_write < msg_ele_size) {
    int realloc_size = (msg->o_end - msg->o_start) * 2;
    msg = (RecMessage *)ats_realloc(msg, sizeof(RecMessageHdr) + realloc_size);
    msg->o_end = msg->o_start + realloc_size;
  }
  ele_hdr = (RecMessageEleHdr *) ((char *) msg + msg->o_write);
  // The following memset() is pretty CPU intensive, replacing it with something
  // like the below would reduce CPU usage a fair amount. /leif.
  // *((char*)msg + msg->o_write) = 0;
  memset((char *) msg + msg->o_write, 0, msg->o_end - msg->o_write);
  msg->o_write += msg_ele_size;

  // store the record
  ele_hdr->magic = REC_MESSAGE_ELE_MAGIC;
  ele_hdr->o_next = msg->o_write;
  p = (char *) ele_hdr + sizeof(RecMessageEleHdr);
  memcpy(p, record, sizeof(RecRecord));
  r = (RecRecord *) p;
  p += sizeof(RecRecord);
  if (rec_name_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t) p - (uintptr_t) msg)) >= (uintptr_t) rec_name_len);
    memcpy(p, record->name, rec_name_len);
    r->name = (char *) ((uintptr_t) p - (uintptr_t) r);
    p += rec_name_len;
  }
  if (rec_data_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t) p - (uintptr_t) msg)) >= (uintptr_t) rec_data_str_len);
    memcpy(p, record->data.rec_string, rec_data_str_len);
    r->data.rec_string = (char *) ((uintptr_t) p - (uintptr_t) r);
    p += rec_data_str_len;
  }
  if (rec_data_def_str_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t) p - (uintptr_t) msg)) >= (uintptr_t) rec_data_def_str_len);
    memcpy(p, record->data_default.rec_string, rec_data_def_str_len);
    r->data_default.rec_string = (char *) ((uintptr_t) p - (uintptr_t) r);
    p += rec_data_def_str_len;
  }
  if (rec_cfg_chk_len != -1) {
    ink_assert((msg->o_end - ((uintptr_t) p - (uintptr_t) msg)) >= (uintptr_t) rec_cfg_chk_len);
    memcpy(p, record->config_meta.check_expr, rec_cfg_chk_len);
    r->config_meta.check_expr = (char *) ((uintptr_t) p - (uintptr_t) r);
    p += rec_cfg_chk_len;
  }

  msg->entries += 1;

  return msg;
}

//-------------------------------------------------------------------------
// RecMessageUnmarshalFirst
//-------------------------------------------------------------------------

int
RecMessageUnmarshalFirst(RecMessage * msg, RecMessageItr * itr, RecRecord ** record)
{
  itr->ele_hdr = (RecMessageEleHdr *) ((char *) msg + msg->o_start);
  itr->next = 1;

  return RecMessageUnmarshalNext(msg, NULL, record);
}

//-------------------------------------------------------------------------
// RecMessageUnmarshalNext
//-------------------------------------------------------------------------

int
RecMessageUnmarshalNext(RecMessage * msg, RecMessageItr * itr, RecRecord ** record)
{
  RecMessageEleHdr *eh;
  RecRecord *r;

  if (itr == NULL) {
    if (msg->entries == 0) {
      return REC_ERR_FAIL;
    } else {
      eh = (RecMessageEleHdr *) ((char *) msg + msg->o_start);
    }
  } else {
    if (itr->next >= msg->entries) {
      return REC_ERR_FAIL;
    }
    itr->ele_hdr = (RecMessageEleHdr *) ((char *) (msg) + itr->ele_hdr->o_next);
    itr->next += 1;
    eh = itr->ele_hdr;
  }

  ink_assert(eh->magic == REC_MESSAGE_ELE_MAGIC);

  // If the file is corrupt, ignore the the rest of the file.
  if (eh->magic != REC_MESSAGE_ELE_MAGIC) {
    Warning("Persistent statistics file records.stat is corrupted. Ignoring the rest of the file\n");
    return REC_ERR_FAIL;
  }

  r = (RecRecord *) ((char *) eh + sizeof(RecMessageEleHdr));

  if (r->name) {
    r->name = (char *) r + (intptr_t) (r->name);
  }
  if (r->data_type == RECD_STRING) {
    if (r->data.rec_string) {
      r->data.rec_string = (char *) r + (intptr_t) (r->data.rec_string);
    }
    if (r->data_default.rec_string) {
      r->data_default.rec_string = (char *) r + (intptr_t) (r->data_default.rec_string);
    }
  }
  if (REC_TYPE_IS_CONFIG(r->rec_type) && (r->config_meta.check_expr)) {
    r->config_meta.check_expr = (char *) r + (intptr_t) (r->config_meta.check_expr);
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
  g_recv_cb = recv_cb;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecMessageRecvThis
//-------------------------------------------------------------------------

void *
RecMessageRecvThis(void *cookie, char *data_raw, int data_len)
{
  RecMessage *msg = (RecMessage *) data_raw;
  g_recv_cb(msg, msg->msg_type, g_recv_cookie);
  return NULL;
}

//-------------------------------------------------------------------------
// RecMessageReadFromDisk
//-------------------------------------------------------------------------

RecMessage *
RecMessageReadFromDisk(const char *fpath)
{
  RecMessageHdr msg_hdr;
  RecMessage *msg = NULL;
  RecHandle h_file;
  int bytes_read;

  if ((h_file = RecFileOpenR(fpath)) == REC_HANDLE_INVALID) {
    goto Lerror;
  }
  if (RecFileRead(h_file, (char *) (&msg_hdr), sizeof(RecMessageHdr), &bytes_read) == REC_ERR_FAIL) {
    goto Lerror;
  }
  msg = (RecMessage *)ats_malloc((msg_hdr.o_end - msg_hdr.o_start) + sizeof(RecMessageHdr));
  memcpy(msg, &msg_hdr, sizeof(RecMessageHdr));
  if (RecFileRead(h_file, (char *) (msg) + msg_hdr.o_start,
                  msg_hdr.o_end - msg_hdr.o_start, &bytes_read) == REC_ERR_FAIL) {
    goto Lerror;
  }

  goto Ldone;

Lerror:
  ats_free(msg);
  msg = NULL;

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
    if (RecFileWrite(h_file, (char *) msg, msg_size, &bytes_written) == REC_ERR_FAIL) {
      RecFileClose(h_file);
      return REC_ERR_FAIL;
    }
    RecFileClose(h_file);
  }

  return REC_ERR_OKAY;
}
