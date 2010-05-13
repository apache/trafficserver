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

/*
 *
 * MgmtDBM.h
 *   Mgmt DBM wrapper.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 *
 */

#ifndef _MGMT_DBM_H
#define _MGMT_DBM_H

#include "ink_platform.h"
#include "ink_error.h"
#include "MgmtDefs.h"
#include "SimpleDBM.h"
#include "MgmtUtils.h"
#include "Diags.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#ifdef NEED_UNION_SEMUN
union semun
{
  int val;                      /* value for SETVAL */
  struct semid_ds *buf;         /* buffer for IPC_STAT, IPC_SET */
  unsigned short int *array;    /* array for GETALL, SETALL */
  struct seminfo *__buf;        /* buffer for IPC_INFO */
};
#endif

class MgmtDBM:public SimpleDBM
{

public:

  MgmtDBM(char *fname):SimpleDBM(), mgmt_sem_id(0)
  {
    if (!(strlen(fname) < PATH_NAME_MAX)) {
      mgmt_fatal(stderr, "[MgmtDBM::MgmtDBM] File name to large: '%s'\n", fname);
    }
    partner_process = 0;
    ink_strncpy(db_file, fname, sizeof(db_file));
    initialized = false;
    opened = false;
    //coverity[uninit_member]
  }                             /* End MgmtDBM::MgmtDBM */

  ~MgmtDBM() {
  };

  int mgmt_batch_open();
  void mgmt_batch_close();

  int mgmt_get(void *key, int key_len, void **data, int *data_len)
  {
    int res = -1;
    bool needToClose = false;

    if (initialized) {

      if (opened == false) {
        if (!mgmt_batch_open()) {
          return -1;
        }
        needToClose = true;
      }

      res = get(key, key_len, data, data_len);
      if (needToClose == true) {
        mgmt_batch_close();
      }
    }
    return res;
  }                             /* End MgmtDBM::mgmt_get */

  int mgmt_put(void *key, int key_len, void *data, int data_len)
  {
    int res = -1;
    bool needToClose = false;

    if (initialized) {

      if (opened == false) {
        if (!mgmt_batch_open()) {
          return -1;
        }
        needToClose = true;
      }

      res = put(key, key_len, data, data_len);
      if (needToClose == true) {
        mgmt_batch_close();
      }
    }
    return res;
  }                             /* End MgmtDBM::mgmt_put */

  int mgmt_remove(void *key, int key_len)
  {
    int res = -1;
    bool needToClose = false;

    if (initialized) {

      if (opened == false) {
        if (!mgmt_batch_open()) {
          return -1;
        }
        needToClose = true;
      }

      res = remove(key, key_len);
      if (needToClose == true) {
        mgmt_batch_close();
      }
    }
    return res;
  }                             /* End MgmtDBM::mgmt_remove */

  int mgmt_setup(int id)
  {
    unlink(db_file);            /* This is called by LM, cleanup on restart */
#ifndef _WIN32
    if ((mgmt_sem_id = semget(id, 1, IPC_CREAT | IPC_EXCL | 0666)) < 0) {
      mgmt_log("[MgmtDBM::mgmt_setup] semget failed %d\n", id);
    } else {
      /* Set the val to be 1 */

      union semun
      {
        int val;
        struct semid_ds *buf;
        ushort *array;
      } arg;
      arg.val = 1;

      if (semctl(mgmt_sem_id, 0, SETVAL, arg) < 0) {
        mgmt_fatal(stderr, "[MgmtDBM::mgmt_setup] semctl failed\n");
      }

/*
      struct sembuf sops = { 0, 1, 0};

      if(semop(mgmt_sem_id, &sops, 1) < 0) {
	  mgmt_fatal(stderr, "[MgmtDBM::mgmt_setup] semop P failed\n");
      }
*/
      initialized = true;
    }
    return mgmt_sem_id;
#else
    int res = -1;
    char sem_name[80];
    sprintf(sem_name, "Inktomi.Traffic.Server.%d", id);
    if ((mgmt_hsem = CreateSemaphore(NULL, 1, 1, sem_name)) != NULL) {
      res = 1;
      initialized = true;
    } else {
      mgmt_log("[MgmtDBM::mgmt_setup] CreateSemaphore failed on %s: %s\n", sem_name, ink_last_err());
    }
    return res;
#endif /* !_WIN32 */
  }                             /* End MgmtDBM::mgmt_setup_sync */

  int mgmt_attach(int id)
  {
#ifndef _WIN32
    if ((mgmt_sem_id = semget(id, 1, 0666)) < 0) {
      mgmt_fatal(stderr, "[MgmtDBM::mgmt_attach] semget failed: %d\n", id);
    }
    initialized = true;
    return mgmt_sem_id;
#else
    int res = -1;
    char sem_name[80];
    sprintf(sem_name, "Inktomi.Traffic.Server.%d", id);
    if ((mgmt_hsem = OpenSemaphore(SEMAPHORE_ALL_ACCESS, NULL, sem_name)) != NULL) {
      res = 1;
      initialized = true;
    } else {
      mgmt_log("[MgmtDBM::mgmt_attach] OpenSemaphore failed %d\n", id);
    }
    return res;
#endif /* !_WIN32 */
  }                             /* End MgmtDBM::mgmt_attach */

  void mgmt_set_partner_process(pid_t pid)
  {
    /* 3com does not want these messages to be seen */
    mgmt_log("[MgmtDBM::mgmt_set_partner_process] From: %ld To: %ld\n", partner_process, pid);
    partner_process = pid;
  }

  void mgmt_cleanup()
  {
    unlink(db_file);
    union semun semun_dummy;
    if (initialized && semctl(mgmt_sem_id, 0, IPC_RMID, semun_dummy) < 0) {
      // INKqa02679 - do not call mgmt_fatal here since mgmt_fatal
      //   will end up calling this function and creating a loop
      //   of death.  We are in process of exiting anyway so it
      //   hardly matters if the sem cleanup failed
      mgmt_elog(stderr, "[MgmtDBM::mgmt_cleanup] semctl rmid failed\n");
    }
    return;
  }                             /* End MgmtDBM::mgmt_cleanup */

private:
  bool initialized;
  bool opened;                  // Not thread safe
#if !defined (_WIN32)
  int mgmt_sem_id;
#else
  HANDLE mgmt_hsem;
#endif
  char db_file[PATH_NAME_MAX + 1];
  pid_t partner_process;

};                              /* End class MgmtDBM */

#endif /* _MGMT_DBM_H */
