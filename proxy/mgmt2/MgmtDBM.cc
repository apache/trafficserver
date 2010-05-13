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

/**************************************
 *
 * MgmtDBM.h
 *   Mgmt DBM wrappers for batch read/write open/close operations.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 *
 */

#include "ink_config.h"
#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

#include "MgmtDBM.h"

#define MAX_SEMOP_FAILURES     3
#define SEMOP_FAILURE_WAIT_SEC 1
static int g_semop_failures = 0;

int
MgmtDBM::mgmt_batch_open()
{

  if (initialized) {
#if !defined (_WIN32)
    struct sembuf sops = { 0, -1, IPC_NOWAIT };
    int holding_pid = -1, current_val = 1000, status = -1;

    time_t start = time(NULL);
    while ((status = semop(mgmt_sem_id, &sops, 1)) < 0) {

      if (errno != EAGAIN && errno != EINTR) {
        // Temporary workaround for INKqa07162 until we get rid of
        // this semaphore completely.  There is a race condition
        // during the shutdown sequence in which traffic_manager may
        // delete the semaphore before traffic_server knows to shut
        // itself down.  Workaround is to let whomever is trying to
        // aquire the semaphore to try a few times before giving up
        // and FATAL'ing.
        if (g_semop_failures < MAX_SEMOP_FAILURES) {
          // wait for a bit, and then try to grab the semaphore again
          mgmt_sleep_sec(SEMOP_FAILURE_WAIT_SEC);
          g_semop_failures++;
        } else {
          mgmt_elog(stderr, "[MgmtDBM::mgmt_batch_open] semop P failed " "after %d retries\n", g_semop_failures);
          return 0;
        }
      }

      time_t current = time(NULL);
      if ((current - start) > 120) {
        break;
      }
    }
    if (status == -1) {

#if (HOST_OS == linux)
      union semun dummy_semun;
      holding_pid = semctl(mgmt_sem_id, 0, GETPID, dummy_semun);
      current_val = semctl(mgmt_sem_id, 0, GETVAL, dummy_semun);
#else
      holding_pid = semctl(mgmt_sem_id, 0, GETPID);
      current_val = semctl(mgmt_sem_id, 0, GETVAL);
#endif
      mgmt_fatal(stderr,
                 "[MgmtDBM::mgmt_batch_open] timeout on semop P holding_pid: %d cval: %d pid: %ld ppid: %ld\n",
                 holding_pid, current_val, getpid(), partner_process);
    }
#else
    DWORD status;

    time_t start = time(NULL);

    /* Wait for 60 secs for the semaphore */
    status = WaitForSingleObject(mgmt_hsem, 60 * 1000);

    if (status != WAIT_OBJECT_0) {
      time_t current = time(NULL);
      mgmt_elog(stderr,
                "[MgmtDBM::mgmt_batch_open] WaitForSingleObject failed after %d seconds; status=%d  error='%s'\n",
                (int) (current - start), status, ink_last_err());
      return 0;
    }
#endif /* WIN32 */

    // we have the semaphore, reset the failure count
    g_semop_failures = 0;

    if (open(db_file, 0, NULL) || lock(true)) {
      mgmt_fatal(stderr, "[MgmtDBM::mgmt_batch_open] dbm op failed\n");
    }
    opened = true;
    return 1;
  }
  return 0;
}                               /* End MgmtDBM::mgmt_batch_open */

void
MgmtDBM::mgmt_batch_close()
{

  if (initialized & opened) {
    if (unlock() || close()) {
      mgmt_fatal(stderr, "[MgmtDBM::mgmt_batch_close] dbm op failed\n");
    }
#if !defined(_WIN32)
    struct sembuf sops = { 0, 1, 0 };

    if (semop(mgmt_sem_id, &sops, 1) < 0) {
      mgmt_fatal(stderr, "[MgmtDBM::mgmt_batch_close] semop V failed\n");
    }
#else
    if (ReleaseSemaphore(mgmt_hsem, 1, NULL) == 0) {
      mgmt_fatal(stderr, "[MgmtDBM::mgmt_batch_close] ReleaseSemaphore failed\n");
    }
#endif

    opened = false;
  }
}                               /* End MgmtDBM:: mgmt_batch_close */
