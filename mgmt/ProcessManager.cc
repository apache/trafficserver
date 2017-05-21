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

#include "ts/ink_platform.h"

#include "InkAPIInternal.h"
#include "MgmtUtils.h"
#include "ProcessManager.h"

#include "ts/ink_apidefs.h"
#include "MgmtSocket.h"
#include "ts/I_Layout.h"

/*
 * Global ProcessManager
 */
inkcoreapi ProcessManager *pmgmt = nullptr;

void
ProcessManager::start(std::function<void()> const &cb)
{
  Debug("pmgmt", "starting process manager");

  init = cb;

  ink_release_assert(running == 0);
  ink_atomic_increment(&running, 1);
  poll_thread = ink_thread_create(processManagerThread, NULL, 0, 0, NULL);
}

void
ProcessManager::stop()
{
  Debug("pmgmt", "stopping process manager");

  ink_release_assert(running == 1);
  ink_atomic_decrement(&running, 1);

  int tmp = local_manager_sockfd;

  local_manager_sockfd = -1;
  close_socket(tmp);

  ink_thread_join(poll_thread);
  poll_thread = ink_thread_null();

  while (!queue_is_empty(mgmt_signal_queue)) {
    char *sig = (char *)dequeue(mgmt_signal_queue);
    ats_free(sig);
  }

  ats_free(mgmt_signal_queue);
}

/*
 * processManagerThread(...)
 *   The start function and thread loop for the process manager.
 */
void *
ProcessManager::processManagerThread(void *arg)
{
  void *ret = arg;

  while (!pmgmt) { /* Avert race condition, thread spun during constructor */
    Debug("pmgmt", "waiting for initialization");
    mgmt_sleep_sec(1);
  }

  if (pmgmt->require_lm) { /* Allow p. process to run w/o a lm */
    pmgmt->initLMConnection();
  }

  if (pmgmt->init) {
    pmgmt->init();
  }

  while (pmgmt->running) {
    if (pmgmt->require_lm) {
      pmgmt->pollLMConnection();
    }

    pmgmt->processEventQueue();
    pmgmt->processSignalQueue();
    mgmt_sleep_sec(pmgmt->timeout);
  }

  return ret;
}

ProcessManager::ProcessManager(bool rlm)
  : BaseManager(), require_lm(rlm), pid(getpid()), local_manager_sockfd(0), cbtable(nullptr), max_msgs_in_a_row(1)
{
  mgmt_signal_queue = create_queue();

  // Set temp. process/manager timeout. Will be reconfigure later.
  // Making the process_manager thread a spinning thread to start traffic server
  // as quickly as possible. Will reset this timeout when reconfigure()
  timeout = 0;
}

ProcessManager::~ProcessManager()
{
  if (running) {
    stop();
  }
}

void
ProcessManager::reconfigure()
{
  max_msgs_in_a_row = MAX_MSGS_IN_A_ROW;

  if (RecGetRecordInt("proxy.config.process_manager.timeout", &timeout) != REC_ERR_OKAY) {
    // Default to 5sec if the timeout is unspecified.
    timeout = 5;
  }
}

void
ProcessManager::signalConfigFileChild(const char *parent, const char *child, unsigned int options)
{
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};

  MgmtMarshallInt mgmtopt = options;

  size_t len   = mgmt_message_length(fields, countof(fields), &parent, &child, &mgmtopt);
  void *buffer = ats_malloc(len);

  mgmt_message_marshall(buffer, len, fields, countof(fields), &parent, &child, &mgmtopt);
  signalManager(MGMT_SIGNAL_CONFIG_FILE_CHILD, (const char *)buffer, len);

  ats_free(buffer);
}

void
ProcessManager::signalManager(int msg_id, const char *data_str)
{
  signalManager(msg_id, data_str, strlen(data_str) + 1);
}

void
ProcessManager::signalManager(int msg_id, const char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;

  mh           = (MgmtMessageHdr *)ats_malloc(sizeof(MgmtMessageHdr) + data_len);
  mh->msg_id   = msg_id;
  mh->data_len = data_len;
  memcpy((char *)mh + sizeof(MgmtMessageHdr), data_raw, data_len);

  ink_release_assert(enqueue(mgmt_signal_queue, mh));
}

bool
ProcessManager::processEventQueue()
{
  bool ret = false;

  while (!queue_is_empty(mgmt_event_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *)dequeue(mgmt_event_queue);

    Debug("pmgmt", "processing event id '%d' payload=%d", mh->msg_id, mh->data_len);
    if (mh->data_len > 0) {
      executeMgmtCallback(mh->msg_id, (char *)mh + sizeof(MgmtMessageHdr), mh->data_len);
    } else {
      executeMgmtCallback(mh->msg_id, nullptr, 0);
    }

    // A shutdown message is a normal exit, so Alert rather than Fatal.
    if (mh->msg_id == MGMT_EVENT_SHUTDOWN) {
      Alert("exiting on shutdown message");
    }

    ats_free(mh);
    ret = true;
  }

  return ret;
}

bool
ProcessManager::processSignalQueue()
{
  bool ret = false;

  while (!queue_is_empty(mgmt_signal_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *)dequeue(mgmt_signal_queue);

    Debug("pmgmt", "signaling local manager with message ID %d", mh->msg_id);

    if (require_lm && mgmt_write_pipe(local_manager_sockfd, (char *)mh, sizeof(MgmtMessageHdr) + mh->data_len) <= 0) {
      Fatal("error writing message: %s", strerror(errno));
    } else {
      ats_free(mh);
      ret = true;
    }
  }

  return ret;
}

void
ProcessManager::initLMConnection()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  ats_scoped_str sockpath(Layout::relative_to(rundir, LM_CONNECTION_SERVER));

  MgmtMessageHdr *mh_full;
  int data_len;

  int servlen;
  struct sockaddr_un serv_addr;

  /* Setup Connection to LocalManager */
  memset((char *)&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;

  ink_strlcpy(serv_addr.sun_path, sockpath, sizeof(serv_addr.sun_path));
#if defined(darwin) || defined(freebsd)
  servlen = sizeof(sockaddr_un);
#else
  servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);
#endif

  if ((local_manager_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    Fatal("Unable to create socket '%s': %s", (const char *)sockpath, strerror(errno));
  }

  if (fcntl(local_manager_sockfd, F_SETFD, FD_CLOEXEC) < 0) {
    Fatal("unable to set close-on-exec flag: %s", strerror(errno));
  }

  if ((connect(local_manager_sockfd, (struct sockaddr *)&serv_addr, servlen)) < 0) {
    Fatal("failed to connect management socket '%s': %s", (const char *)sockpath, strerror(errno));
  }

  data_len          = sizeof(pid_t);
  mh_full           = (MgmtMessageHdr *)alloca(sizeof(MgmtMessageHdr) + data_len);
  mh_full->msg_id   = MGMT_SIGNAL_PID;
  mh_full->data_len = data_len;
  memcpy((char *)mh_full + sizeof(MgmtMessageHdr), &(pid), data_len);

  if (mgmt_write_pipe(local_manager_sockfd, (char *)mh_full, sizeof(MgmtMessageHdr) + data_len) <= 0) {
    Fatal("error writing message: %s", strerror(errno));
  }
}

void
ProcessManager::pollLMConnection()
{
  MgmtMessageHdr mh_hdr;
  char *data_raw;

  // Avoid getting stuck enqueuing too many requests in a row, limit to MAX_MSGS_IN_A_ROW.
  int count;
  for (count = 0; running && count < max_msgs_in_a_row; ++count) {
    int num;

    num = mgmt_read_timeout(local_manager_sockfd, 1 /* sec */, 0 /* usec */);
    if (num == 0) {
      // Nothing but a timeout. We are done for now.
      break;
    }

    if (num < 0) {
      // Socket read error.
      Debug("pmgmt", "socket select failed: %s", strerror(errno));
      continue;
    }

    if (num > 0) {
      // We have a message, try to read the message header.
      int res = mgmt_read_pipe(local_manager_sockfd, (char *)&mh_hdr, sizeof(MgmtMessageHdr));

      if (res < 0) {
        if (running) {
          // If we are no longer running, the socket might have been closed out
          // from under us. At any rate, we don't care any more.
          Fatal("socket read error: %s", strerror(errno));
        }

        break;
      }

      // Handle EOF from the manager. This is normal, so we log an Alert
      // rather than a Fatal.
      if (res == 0) {
        close_socket(local_manager_sockfd);
        Alert("exiting with EOF from process manager");
      }

      if (res > 0) {
        size_t mh_full_size     = sizeof(MgmtMessageHdr) + mh_hdr.data_len;
        MgmtMessageHdr *mh_full = (MgmtMessageHdr *)ats_malloc(mh_full_size);

        memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
        data_raw = (char *)mh_full + sizeof(MgmtMessageHdr);

        res = mgmt_read_pipe(local_manager_sockfd, data_raw, mh_hdr.data_len);
        if (res > 0) {
          Debug("pmgmt", "received message ID %d", mh_full->msg_id);
          handleMgmtMsgFromLM(mh_full);
        }

        if (res == 0) {
          close_socket(local_manager_sockfd);
          Alert("exiting with EOF from process manager");
        }

        if (res < 0) {
          if (running) {
            // If we are no longer running, the socket might have been closed out
            // from under us. At any rate, we don't care any more.
            Fatal("socket read error: %s", strerror(errno));
          }
        }

        ats_free(mh_full);
      }
    }
  }

  Debug("pmgmt", "[%s] enqueued %d of max %d messages in a row", __func__, count, max_msgs_in_a_row);
}

void
ProcessManager::handleMgmtMsgFromLM(MgmtMessageHdr *mh)
{
  char *data_raw = (char *)mh + sizeof(MgmtMessageHdr);

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
    if (data_raw != nullptr && data_raw[0] != '\0' && this->cbtable) {
      this->cbtable->invoke(data_raw);
    }
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
  case MGMT_EVENT_LIFECYCLE_MESSAGE:
    signalMgmtEntity(MGMT_EVENT_LIFECYCLE_MESSAGE, data_raw, mh->data_len);
    break;
  default:
    Warning("received unknown message ID %d\n", mh->msg_id);
    break;
  }
}
