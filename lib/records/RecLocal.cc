/** @file

  Record local definitions

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
#include "Rollback.h"
#include "tscore/ParseRules.h"
#include "P_RecCore.h"
#include "P_RecLocal.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "P_RecFile.h"
#include "LocalManager.h"
#include "FileManager.h"

// Marks whether the message handler has been initialized.
static bool message_initialized_p = false;

//-------------------------------------------------------------------------
// i_am_the_record_owner, only used for libreclocal.a
//-------------------------------------------------------------------------
bool
i_am_the_record_owner(RecT rec_type)
{
  switch (rec_type) {
  case RECT_CONFIG:
  case RECT_NODE:
  case RECT_LOCAL:
    return true;
  case RECT_PROCESS:
  case RECT_PLUGIN:
    return false;
  default:
    ink_assert(!"Unexpected RecT type");
    return false;
  }
}

//-------------------------------------------------------------------------
// sync_thr
//-------------------------------------------------------------------------
static void *
sync_thr(void *data)
{
  FileManager *configFiles = static_cast<FileManager *>(data);

  while (true) {
    RecBool check = true;

    send_push_message();
    RecSyncStatsFile();

    // If we didn't successfully sync to disk, check whether we need to update ....
    if (check) {
      if (configFiles->isConfigStale()) {
        RecSetRecordInt("proxy.node.config.reconfigure_required", 1, REC_SOURCE_DEFAULT);
      }
    }

    usleep(REC_REMOTE_SYNC_INTERVAL_MS * 1000);
  }

  return nullptr;
}

//-------------------------------------------------------------------------
// config_update_thr
//-------------------------------------------------------------------------
static void *
config_update_thr(void * /* data */)
{
  while (true) {
    switch (RecExecConfigUpdateCbs(REC_LOCAL_UPDATE_REQUIRED)) {
    case RECU_RESTART_TS:
      RecSetRecordInt("proxy.node.config.restart_required.proxy", 1, REC_SOURCE_DEFAULT);
      break;
    case RECU_RESTART_TM:
      RecSetRecordInt("proxy.node.config.restart_required.proxy", 1, REC_SOURCE_DEFAULT);
      RecSetRecordInt("proxy.node.config.restart_required.manager", 1, REC_SOURCE_DEFAULT);
      break;
    case RECU_NULL:
    case RECU_DYNAMIC:
      break;
    }

    usleep(REC_CONFIG_UPDATE_INTERVAL_MS * 1000);
  }
  return nullptr;
}

//-------------------------------------------------------------------------
// RecMessageInit
//-------------------------------------------------------------------------
void
RecMessageInit()
{
  ink_assert(g_mode_type != RECM_NULL);
  lmgmt->registerMgmtCallback(MGMT_SIGNAL_LIBRECORDS, &RecMessageRecvThis);
  message_initialized_p = true;
}

//-------------------------------------------------------------------------
// RecLocalInit
//-------------------------------------------------------------------------
int
RecLocalInit(Diags *_diags)
{
  static bool initialized_p = false;
  ;

  if (initialized_p) {
    return REC_ERR_OKAY;
  }

  g_mode_type = RECM_SERVER;

  if (RecCoreInit(RECM_SERVER, _diags) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }

  initialized_p = true;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLocalInitMessage
//-------------------------------------------------------------------------
int
RecLocalInitMessage()
{
  static bool initialized_p = false;

  if (initialized_p) {
    return REC_ERR_OKAY;
  }

  RecMessageInit();
  if (RecMessageRegisterRecvCb(recv_message_cb, nullptr)) {
    return REC_ERR_FAIL;
  }

  initialized_p = true;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLocalStart
//-------------------------------------------------------------------------
int
RecLocalStart(FileManager *configFiles)
{
  ink_thread_create(nullptr, sync_thr, configFiles, 0, 0, nullptr);
  ink_thread_create(nullptr, config_update_thr, nullptr, 0, 0, nullptr);
  return REC_ERR_OKAY;
}

int
RecRegisterManagerCb(int id, RecManagerCb const &_fn)
{
  return lmgmt->registerMgmtCallback(id, _fn);
}

void
RecSignalManager(int id, const char *, size_t)
{
  // Signals are messages sent across the management pipe, so by definition,
  // you can't send a signal if you are a local process manager.
  RecDebug(DL_Debug, "local manager dropping signal %d", id);
}

//-------------------------------------------------------------------------
// RecMessageSend
//-------------------------------------------------------------------------

int
RecMessageSend(RecMessage *msg)
{
  int msg_size;

  if (!message_initialized_p) {
    return REC_ERR_OKAY;
  }

  // Make a copy of the record, but truncate it to the size actually used
  if (g_mode_type == RECM_CLIENT || g_mode_type == RECM_SERVER) {
    msg->o_end = msg->o_write;
    msg_size   = sizeof(RecMessageHdr) + (msg->o_write - msg->o_start);
    lmgmt->signalEvent(MGMT_EVENT_LIBRECORDS, reinterpret_cast<char *>(msg), msg_size);
  }

  return REC_ERR_OKAY;
}
