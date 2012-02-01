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

static bool g_initialized = false;
static bool g_message_initialized = false;
static RecModeT g_mode_type = RECM_NULL;

#define REC_LOCAL
#include "P_RecCore.i"
#undef  REC_LOCAL

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
  REC_NOWARN_UNUSED(data);
  textBuffer tb(65536);
  while (1) {
    send_push_message();
    RecSyncStatsFile();
    if (RecSyncConfigToTB(&tb) == REC_ERR_OKAY) {
      int nbytes;
      RecDebug(DL_Note, "Writing '%s'", g_rec_config_fpath);
      RecHandle h_file = RecFileOpenW(g_rec_config_fpath);
      RecFileWrite(h_file, tb.bufPtr(), tb.spaceUsed(), &nbytes);
      RecFileClose(h_file);
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
  REC_NOWARN_UNUSED(data);
  textBuffer *tb = NEW(new textBuffer(65536));
  Rollback *rb;

  while (1) {
    send_push_message();
    RecSyncStatsFile();
    if (RecSyncConfigToTB(tb) == REC_ERR_OKAY) {
      if (configFiles->getRollbackObj(REC_CONFIG_FILE, &rb)) {
        RecDebug(DL_Note, "Rollback: '%s'", REC_CONFIG_FILE);
        version_t ver = rb->getCurrentVersion();
        if ((rb->updateVersion(tb, ver, -1, false)) != OK_ROLLBACK) {
          RecDebug(DL_Note, "Rollback failed: '%s'", REC_CONFIG_FILE);
        }
      } else {
        int nbytes;
        RecDebug(DL_Note, "Writing '%s'", g_rec_config_fpath);
        RecHandle h_file = RecFileOpenW(g_rec_config_fpath);
        RecFileWrite(h_file, tb->bufPtr(), tb->spaceUsed(), &nbytes);
        RecFileClose(h_file);
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
  REC_NOWARN_UNUSED(data);
  while (true) {
    RecExecConfigUpdateCbs();
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

  if (RecMessageInit(RECM_SERVER) == REC_ERR_FAIL) {
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
