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

#include "libts.h"
#include "ParseRules.h"
#include "P_RecCore.h"
#include "P_RecLocal.h"
#include "P_RecMessage.h"
#include "P_RecUtils.h"
#include "P_RecFile.h"

static bool g_initialized = false;
static bool g_message_initialized = false;

//-------------------------------------------------------------------------
// i_am_the_record_owner, only used for libreclocal.a
//-------------------------------------------------------------------------
bool
i_am_the_record_owner(RecT rec_type)
{
  switch (rec_type) {
  case RECT_CONFIG:
  case RECT_NODE:
  case RECT_CLUSTER:
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
//
// REC_BUILD_STAND_ALONE IMPLEMENTATION
//
//-------------------------------------------------------------------------
#if defined (REC_BUILD_STAND_ALONE)


//-------------------------------------------------------------------------
// sync_thr
//-------------------------------------------------------------------------
static void *
sync_thr(void *data)
{
  textBuffer tb(65536);
  while (1) {
    send_push_message();
    RecSyncStatsFile();
    if (RecSyncConfigToTB(&tb) == REC_ERR_OKAY) {
      RecWriteConfigFile(&tb);
    }
    usleep(REC_REMOTE_SYNC_INTERVAL_MS * 1000);
  }
  return NULL;
}


//-------------------------------------------------------------------------
//
// REC_BUILD_MGMT IMPLEMENTATION
//
//-------------------------------------------------------------------------
#elif defined (REC_BUILD_MGMT)

#include "Main.h"


//-------------------------------------------------------------------------
// sync_thr
//-------------------------------------------------------------------------
static void *
sync_thr(void *data)
{
  textBuffer *tb = NEW(new textBuffer(65536));
  Rollback *rb;
  bool inc_version;
  bool written;

  while (1) {
    send_push_message();
    RecSyncStatsFile();
    if (RecSyncConfigToTB(tb, &inc_version) == REC_ERR_OKAY) {
      written = false;
      if (configFiles->getRollbackObj(REC_CONFIG_FILE, &rb)) {
        if (inc_version) {
          RecDebug(DL_Note, "Rollback: '%s'", REC_CONFIG_FILE);
          version_t ver = rb->getCurrentVersion();
          if ((rb->updateVersion(tb, ver, -1, false)) != OK_ROLLBACK) {
            RecDebug(DL_Note, "Rollback failed: '%s'", REC_CONFIG_FILE);
          }
          written = true;
        }
      }
      else {
        rb = NULL;
      }
      if (!written) {
        RecWriteConfigFile(tb);
        if (rb != NULL) {
          rb->setLastModifiedTime();
        }
      }
    }
    usleep(REC_REMOTE_SYNC_INTERVAL_MS * 1000);
  }
  return NULL;
}

#endif


//-------------------------------------------------------------------------
// config_update_thr
//-------------------------------------------------------------------------
static void *
config_update_thr(void *data)
{
  while (true) {
    RecExecConfigUpdateCbs(REC_LOCAL_UPDATE_REQUIRED);
    usleep(REC_CONFIG_UPDATE_INTERVAL_MS * 1000);
  }
  return NULL;
}


//-------------------------------------------------------------------------
// RecLocalInit
//-------------------------------------------------------------------------
int
RecLocalInit(Diags * _diags)
{
  if (g_initialized) {
    return REC_ERR_OKAY;
  }

  g_mode_type = RECM_SERVER;

  if (RecCoreInit(RECM_SERVER, _diags) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }

  /* -- defer RecMessageInit() until LocalManager is initialized
     if (RecMessageInit(RECM_SERVER) == REC_ERR_FAIL) {
     return REC_ERR_FAIL;
     }

     if (RecMessageRegisterRecvCb(recv_message_cb, NULL)) {
     return REC_ERR_FAIL;
     }
   */
  g_initialized = true;

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecLocalInitMessage
//-------------------------------------------------------------------------
int
RecLocalInitMessage()
{
  if (g_message_initialized) {
    return REC_ERR_OKAY;
  }

  if (RecMessageInit() == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }

  if (RecMessageRegisterRecvCb(recv_message_cb, NULL)) {
    return REC_ERR_FAIL;
  }

  g_message_initialized = true;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLocalStart
//-------------------------------------------------------------------------
int
RecLocalStart()
{
  ink_thread_create(sync_thr, NULL);
  ink_thread_create(config_update_thr, NULL);

  return REC_ERR_OKAY;
}
