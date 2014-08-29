/** @file

  File contains the member function defs and thread loop for the process manager.

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
#undef HTTP_CACHE
#include "InkAPIInternal.h"
#include "MgmtUtils.h"
#include "ProcessManager.h"

#include "ink_apidefs.h"
#include "MgmtSocket.h"
#include "I_Layout.h"

/*
 * Global ProcessManager
 */
inkcoreapi ProcessManager *pmgmt = NULL;

/*
 * startProcessManager(...)
 *   The start function and thread loop for the process manager.
 */
void *
startProcessManager(void *arg)
{
  void *ret = arg;

  while (!pmgmt) {              /* Avert race condition, thread spun during constructor */
    Debug("pmgmt", "[startProcessManager] Waiting for initialization of object...\n");
    mgmt_sleep_sec(1);
  }
  if (pmgmt->require_lm) {      /* Allow p. process to run w/o a lm */
    pmgmt->initLMConnection();
  }

  for (;;) {
    if (pmgmt->require_lm) {
      pmgmt->pollLMConnection();
    }
    pmgmt->processEventQueue();
    pmgmt->processSignalQueue();
    mgmt_sleep_sec(pmgmt->timeout);
  }
  return ret;
}                               /* End startProcessManager */

ProcessManager::ProcessManager(bool rlm):
BaseManager(), require_lm(rlm), mgmt_sync_key(0), local_manager_sockfd(0), cbtable(NULL)
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());

  ink_strlcpy(pserver_path, rundir, sizeof(pserver_path));
  mgmt_signal_queue = create_queue();

  // Set temp. process/manager timeout. Will be reconfigure later.
  // Making the process_manager thread a spinning thread to start traffic server
  // as quickly as possible. Will reset this timeout when reconfigure()
  timeout = 0;
  pid = getpid();
}                               /* End ProcessManager::ProcessManager */


void
ProcessManager::reconfigure()
{
  bool found;
  timeout = REC_readInteger("proxy.config.process_manager.timeout", &found);
  ink_assert(found);

  return;
}                               /* End ProcessManager::reconfigure */


void
ProcessManager::signalManager(int msg_id, const char *data_str)
{
  signalManager(msg_id, data_str, strlen(data_str) + 1);
  return;
}                               /* End ProcessManager::signalManager */


void
ProcessManager::signalManager(int msg_id, const char *data_raw, int data_len)
{

  MgmtMessageHdr *mh;

  mh = (MgmtMessageHdr *)ats_malloc(sizeof(MgmtMessageHdr) + data_len);
  mh->msg_id = msg_id;
  mh->data_len = data_len;
  memcpy((char *) mh + sizeof(MgmtMessageHdr), data_raw, data_len);
  ink_assert(enqueue(mgmt_signal_queue, mh));
  return;

}                               /* End ProcessManager::signalManager */


bool
ProcessManager::processEventQueue()
{
  bool ret = false;

  while (!queue_is_empty(mgmt_event_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *) dequeue(mgmt_event_queue);

    Debug("pmgmt", "[ProcessManager] ==> Processing event id '%d' payload=%d\n", mh->msg_id, mh->data_len);
    if (mh->data_len > 0) {
      executeMgmtCallback(mh->msg_id, (char *) mh + sizeof(MgmtMessageHdr), mh->data_len);
    } else {
      executeMgmtCallback(mh->msg_id, NULL, 0);
    }
    if (mh->msg_id == MGMT_EVENT_SHUTDOWN) {
      mgmt_log(stderr, "[ProcessManager::processEventQueue] Shutdown msg received, exiting\n");
      _exit(0);
    }                           /* Exit on shutdown */
    ats_free(mh);
    ret = true;
  }
  return ret;
}                               /* End ProcessManager::processEventQueue */


bool
ProcessManager::processSignalQueue()
{
  bool ret = false;

  while (!queue_is_empty(mgmt_signal_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *) dequeue(mgmt_signal_queue);

    Debug("pmgmt", "[ProcessManager] ==> Signalling local manager '%d'\n", mh->msg_id);

    if (require_lm && mgmt_write_pipe(local_manager_sockfd, (char *) mh, sizeof(MgmtMessageHdr) + mh->data_len) <= 0) {
      mgmt_fatal(stderr, errno, "[ProcessManager::processSignalQueue] Error writing message!");
      //ink_assert(enqueue(mgmt_signal_queue, mh));
    } else {
      ats_free(mh);
      ret = true;
    }
  }

  return ret;
}                               /* End ProcessManager::processSignalQueue */


void
ProcessManager::initLMConnection()
{
  char message[1024];

  MgmtMessageHdr mh_hdr;
  MgmtMessageHdr *mh_full;
  int data_len;
  char *sync_key_raw = NULL;

  int servlen;
  struct sockaddr_un serv_addr;

  /* Setup Connection to LocalManager */
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;

  snprintf(message, sizeof(message), "%s/%s", pserver_path, LM_CONNECTION_SERVER);
  ink_strlcpy(serv_addr.sun_path, message, sizeof(serv_addr.sun_path));
#if defined(darwin) || defined(freebsd)
  servlen = sizeof(sockaddr_un);
#else
  servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);
#endif
  if ((local_manager_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Unable to create socket\n");
  }

  if (fcntl(local_manager_sockfd, F_SETFD, 1) < 0) {
    mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Unable to set close-on-exec\n");
  }

  if ((connect(local_manager_sockfd, (struct sockaddr *) &serv_addr, servlen)) < 0) {
    mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Connect failed\n");
  }

  data_len = sizeof(pid_t);
  mh_full = (MgmtMessageHdr *) alloca(sizeof(MgmtMessageHdr) + data_len);
  mh_full->msg_id = MGMT_SIGNAL_PID;
  mh_full->data_len = data_len;
  memcpy((char *) mh_full + sizeof(MgmtMessageHdr), &(pid), data_len);
  if (mgmt_write_pipe(local_manager_sockfd, (char *) mh_full, sizeof(MgmtMessageHdr) + data_len) <= 0) {
    mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Error writing message!\n");
  }

  /* Read SYNC_KEY from manager */
  if (mgmt_read_pipe(local_manager_sockfd, (char *) &mh_hdr, sizeof(MgmtMessageHdr)) <= 0) {
    mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Error reading sem message!\n");
  } else {
    // coverity[uninit_use]
    mh_full = (MgmtMessageHdr *) alloca(sizeof(MgmtMessageHdr) + mh_hdr.data_len);
    memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
    sync_key_raw = (char *) mh_full + sizeof(MgmtMessageHdr);
    if (mgmt_read_pipe(local_manager_sockfd, sync_key_raw, mh_hdr.data_len) < 0) {
      mgmt_fatal(stderr, errno, "[ProcessManager::initLMConnection] Error reading sem message!\n");
    }
  }



  if (sync_key_raw)
    memcpy(&mgmt_sync_key, sync_key_raw, sizeof(mgmt_sync_key));
  Debug("pmgmt", "[ProcessManager::initLMConnection] Received key: %d\n", mgmt_sync_key);

}                               /* End ProcessManager::initLMConnection */


void
ProcessManager::pollLMConnection()
{
  int res;
  struct timeval poll_timeout;

  MgmtMessageHdr mh_hdr;
  MgmtMessageHdr *mh_full;
  char *data_raw;

  int num;
  fd_set fdlist;

  while (1) {

    // poll only
    poll_timeout.tv_sec = 0;
    poll_timeout.tv_usec = 1000;

    FD_ZERO(&fdlist);
    FD_SET(local_manager_sockfd, &fdlist);
    num = mgmt_select(FD_SETSIZE, &fdlist, NULL, NULL, &poll_timeout);
    if (num == 0) {             /* Have nothing */

      break;

    } else if (num > 0) {       /* We have a message */

      if ((res = mgmt_read_pipe(local_manager_sockfd, (char *) &mh_hdr, sizeof(MgmtMessageHdr))) > 0) {
        mh_full = (MgmtMessageHdr *) alloca(sizeof(MgmtMessageHdr) + mh_hdr.data_len);
        memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
        data_raw = (char *) mh_full + sizeof(MgmtMessageHdr);
        if ((res = mgmt_read_pipe(local_manager_sockfd, data_raw, mh_hdr.data_len)) > 0) {
          Debug("pmgmt", "[ProcessManager::pollLMConnection] Message: '%d'", mh_full->msg_id);
          handleMgmtMsgFromLM(mh_full);
        } else if (res < 0) {
          mgmt_fatal(stderr, errno, "[ProcessManager::pollLMConnection] Error in read!");
        }
      } else if (res < 0) {
        mgmt_fatal(stderr, errno, "[ProcessManager::pollLMConnection] Error in read!");
      }
      // handle EOF
      if (res == 0) {
        close_socket(local_manager_sockfd);
        mgmt_fatal(stderr, 0, "[ProcessManager::pollLMConnection] Lost Manager EOF!");
      }

    } else if (num < 0) {       /* Error */
      mgmt_elog(stderr, 0, "[ProcessManager::pollLMConnection] select failed or was interrupted (%d)\n", errno);
    }

  }

}                               /* End ProcessManager::pollLMConnection */

void
ProcessManager::handleMgmtMsgFromLM(MgmtMessageHdr * mh)
{
  char *data_raw = (char *) mh + sizeof(MgmtMessageHdr);

  switch (mh->msg_id) {
  case MGMT_EVENT_SHUTDOWN:
    signalMgmtEntity(MGMT_EVENT_SHUTDOWN);
    break;
  case MGMT_EVENT_RESTART:
    signalMgmtEntity(MGMT_EVENT_RESTART);
    break;
  case MGMT_EVENT_CLEAR_STATS:
    signalMgmtEntity(MGMT_EVENT_CLEAR_STATS);
    break;
  case MGMT_EVENT_ROLL_LOG_FILES:
    signalMgmtEntity(MGMT_EVENT_ROLL_LOG_FILES);
    break;
  case MGMT_EVENT_PLUGIN_CONFIG_UPDATE:
    if (data_raw != NULL && data_raw[0] != '\0' && this->cbtable) {
      this->cbtable->invoke(data_raw);
    }
    break;
  case MGMT_EVENT_HTTP_CLUSTER_DELTA:
    signalMgmtEntity(MGMT_EVENT_HTTP_CLUSTER_DELTA, data_raw);
    break;
  case MGMT_EVENT_CONFIG_FILE_UPDATE:
  case MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION:
    /*
      librecords -- we don't do anything in here because we are traffic_server
      and we are not the owner of proxy.config.* variables.
      Even if we trigger the sync_required bit, by
      RecSetSynRequired, the sync. message will send back to
      traffic_manager. And traffic_manager founds out that, the
      actual value of the config variable didn't changed.
      At the end, the sync_required bit is not set and we will
      never get notified and callbacks are never invoked.

      The solution is to set the sync_required bit on the
      manager side. See LocalManager::sendMgmtMsgToProcesses()
      for details.
    */
    break;
  case MGMT_EVENT_LIBRECORDS:
    signalMgmtEntity(MGMT_EVENT_LIBRECORDS, data_raw, mh->data_len);
    break;
  case MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE:
    signalMgmtEntity(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, data_raw, mh->data_len);
    break;
  default:
    mgmt_elog(stderr, 0, "[ProcessManager::pollLMConnection] unknown type %d\n", mh->msg_id);
    break;
  }
}
