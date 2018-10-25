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

#include "InkAPIInternal.h"
#include "ProcessManager.h"

#include "tscore/ink_apidefs.h"
#include "MgmtSocket.h"
#include "tscore/I_Layout.h"

/*
 * Global ProcessManager
 */
inkcoreapi ProcessManager *pmgmt = nullptr;

// read_management_message attempts to read a message from the management
// socket. Returns -errno on error, otherwise 0. If a message was read the
// *msg pointer will be filled in with the message that was read.
static int
read_management_message(int sockfd, MgmtMessageHdr **msg)
{
  MgmtMessageHdr hdr;
  int ret;

  *msg = nullptr;

  // We have a message, try to read the message header.
  ret = mgmt_read_pipe(sockfd, reinterpret_cast<char *>(&hdr), sizeof(MgmtMessageHdr));
  switch (ret) {
  case 0:
    // Received EOF.
    return 0;
  case sizeof(MgmtMessageHdr):
    break;
  default:
    // Received -errno.
    return -errno;
  }

  size_t msg_size          = sizeof(MgmtMessageHdr) + hdr.data_len;
  MgmtMessageHdr *full_msg = (MgmtMessageHdr *)ats_malloc(msg_size);

  memcpy(full_msg, &hdr, sizeof(MgmtMessageHdr));
  char *data_raw = reinterpret_cast<char *>(full_msg) + sizeof(MgmtMessageHdr);

  ret = mgmt_read_pipe(sockfd, data_raw, hdr.data_len);
  if (ret == 0) {
    // Received EOF.
    ats_free(full_msg);
    return 0;
  } else if (ret < 0) {
    // Received -errno.
    ats_free(full_msg);
    return ret;
  } else {
    ink_release_assert(ret == hdr.data_len);
    // Received the message.
    *msg = full_msg;
    return 0;
  }
}

void
ProcessManager::start(std::function<void()> const &cb)
{
  Debug("pmgmt", "starting process manager");

  init = cb;

  ink_release_assert(running == 0);
  ink_atomic_increment(&running, 1);
  ink_thread_create(&poll_thread, processManagerThread, nullptr, 0, 0, nullptr);
}

void
ProcessManager::stop()
{
  Debug("pmgmt", "stopping process manager");

  ink_release_assert(running == 1);
  ink_atomic_decrement(&running, 1);

  int tmp;

  if (local_manager_sockfd != ts::NO_FD) {
    tmp                  = local_manager_sockfd;
    local_manager_sockfd = ts::NO_FD;
    close_socket(tmp);
  }

#if HAVE_EVENTFD
  if (wakeup_fd != ts::NO_FD) {
    tmp       = wakeup_fd;
    wakeup_fd = ts::NO_FD;
    close_socket(tmp);
  }
#endif

  ink_thread_kill(poll_thread, SIGINT);

  ink_thread_join(poll_thread);
  poll_thread = ink_thread_null();

  while (!queue_is_empty(mgmt_signal_queue)) {
    char *sig = (char *)::dequeue(mgmt_signal_queue);
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
  } else {
    return ret;
  }

  if (pmgmt->init) {
    pmgmt->init();
  }

  // Start pumping messages between the local process and the process
  // manager. This will terminate when the process manager terminates
  // or the local process calls stop(). In either case, it is likely
  // that we will first notice because we got a socket error, but in
  // the latter case, the `running` flag has already been toggled so
  // we know that we are really doing a shutdown.
  while (pmgmt->running) {
    int ret;

    if (pmgmt->require_lm) {
      ret = pmgmt->pollLMConnection();
      if (ret < 0 && pmgmt->running && !shutdown_event_system) {
        Alert("exiting with read error from process manager: %s", strerror(-ret));
      }
    }

    ret = pmgmt->processSignalQueue();
    if (ret < 0 && pmgmt->running && !shutdown_event_system) {
      Alert("exiting with write error from process manager: %s", strerror(-ret));
    }
  }

  return ret;
}

ProcessManager::ProcessManager(bool rlm)
  : BaseManager(), require_lm(rlm), pid(getpid()), local_manager_sockfd(0), cbtable(nullptr), max_msgs_in_a_row(1)
{
  mgmt_signal_queue = create_queue();

  local_manager_sockfd = ts::NO_FD;
#if HAVE_EVENTFD
  wakeup_fd = ts::NO_FD;
#endif

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

  ink_release_assert(::enqueue(mgmt_signal_queue, mh));

#if HAVE_EVENTFD
  // we don't care about the actual value of wakeup_fd, so just keep adding 1. just need to
  // wakeup the fd. also, note that wakeup_fd was initalized to non-blocking so we can
  // directly write to it without any timeout checking.
  //
  // don't tigger if MGMT_EVENT_LIBRECORD because they happen all the time
  // and don't require a quick response. for MGMT_EVENT_LIBRECORD, rely on timeouts so
  // traffic_server can spend more time doing other things/
  uint64_t one = 1;
  if (wakeup_fd != ts::NO_FD && mh->msg_id != MGMT_SIGNAL_LIBRECORDS) {
    ATS_UNUSED_RETURN(write(wakeup_fd, &one, sizeof(uint64_t))); // trigger to stop polling
  }
#endif
}

int
ProcessManager::processSignalQueue()
{
  while (!queue_is_empty(mgmt_signal_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *)::dequeue(mgmt_signal_queue);

    Debug("pmgmt", "signaling local manager with message ID %d", mh->msg_id);

    if (require_lm) {
      int ret = mgmt_write_pipe(local_manager_sockfd, (char *)mh, sizeof(MgmtMessageHdr) + mh->data_len);
      ats_free(mh);

      if (ret < 0) {
        return ret;
      }
    }
  }

  return 0;
}

void
ProcessManager::initLMConnection()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string sockpath(Layout::relative_to(rundir, LM_CONNECTION_SERVER));

  MgmtMessageHdr *mh_full;
  int data_len;

  int servlen;
  struct sockaddr_un serv_addr;

  if (sockpath.length() > sizeof(serv_addr.sun_path) - 1) {
    errno = ENAMETOOLONG;
    Fatal("Unable to create socket '%s': %s", sockpath.c_str(), strerror(errno));
  }

  /* Setup Connection to LocalManager */
  memset((char *)&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;

  ink_strlcpy(serv_addr.sun_path, sockpath.c_str(), sizeof(serv_addr.sun_path));
#if defined(darwin) || defined(freebsd)
  servlen = sizeof(sockaddr_un);
#else
  servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);
#endif

  if ((local_manager_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    Fatal("Unable to create socket '%s': %s", sockpath.c_str(), strerror(errno));
  }

  if (fcntl(local_manager_sockfd, F_SETFD, FD_CLOEXEC) < 0) {
    Fatal("unable to set close-on-exec flag: %s", strerror(errno));
  }

  if ((connect(local_manager_sockfd, (struct sockaddr *)&serv_addr, servlen)) < 0) {
    Fatal("failed to connect management socket '%s': %s", sockpath.c_str(), strerror(errno));
  }

#if HAVE_EVENTFD
  wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeup_fd < 0) {
    Fatal("unable to create wakeup eventfd. errno: %s", strerror(errno));
  }
#endif

  data_len          = sizeof(pid_t);
  mh_full           = (MgmtMessageHdr *)alloca(sizeof(MgmtMessageHdr) + data_len);
  mh_full->msg_id   = MGMT_SIGNAL_PID;
  mh_full->data_len = data_len;

  memcpy((char *)mh_full + sizeof(MgmtMessageHdr), &(pid), data_len);

  if (mgmt_write_pipe(local_manager_sockfd, (char *)mh_full, sizeof(MgmtMessageHdr) + data_len) <= 0) {
    Fatal("error writing message: %s", strerror(errno));
  }
}

int
ProcessManager::pollLMConnection()
{
  int count;
  int ready;
  struct timeval timeout;
  fd_set fdlist;

  // Avoid getting stuck enqueuing too many requests in a row, limit to MAX_MSGS_IN_A_ROW.
  for (count = 0; running && count < max_msgs_in_a_row; ++count) {
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    FD_ZERO(&fdlist);

    if (local_manager_sockfd != ts::NO_FD) {
      FD_SET(local_manager_sockfd, &fdlist);
    }

#if HAVE_EVENTFD
    if (wakeup_fd != ts::NO_FD) {
      FD_SET(wakeup_fd, &fdlist);
    }
#endif

    // wait for data on socket
    ready = mgmt_select(FD_SETSIZE, &fdlist, nullptr, nullptr, &timeout);

    switch (ready) {
    case 0:
      // Timed out.
      return 0;
    case -1:
      if (mgmt_transient_error()) {
        continue;
      }
      return -errno;
    }

    if (local_manager_sockfd != ts::NO_FD && FD_ISSET(local_manager_sockfd, &fdlist)) { /* Message from manager */
      MgmtMessageHdr *msg;

      int ret = read_management_message(local_manager_sockfd, &msg);
      if (ret < 0) {
        return ret;
      }

      // No message, we are done polling. */
      if (msg == nullptr) {
        return 0;
      }

      Debug("pmgmt", "received message ID %d", msg->msg_id);
      handleMgmtMsgFromLM(msg);
    }
#if HAVE_EVENTFD
    else if (wakeup_fd != ts::NO_FD && FD_ISSET(wakeup_fd, &fdlist)) { /* if msg, keep polling for more */
      // read or else fd will always be set.
      uint64_t ignore;
      ATS_UNUSED_RETURN(read(wakeup_fd, &ignore, sizeof(uint64_t)));
      break;
    }
#endif
  }
  Debug("pmgmt", "enqueued %d of max %d messages in a row", count, max_msgs_in_a_row);
  return 0;
}

void
ProcessManager::handleMgmtMsgFromLM(MgmtMessageHdr *mh)
{
  ink_assert(mh != nullptr);

  auto payload = mh->payload();

  Debug("pmgmt", "processing event id '%d' payload=%d", mh->msg_id, mh->data_len);
  switch (mh->msg_id) {
  case MGMT_EVENT_SHUTDOWN:
    executeMgmtCallback(MGMT_EVENT_SHUTDOWN, {});
    Alert("exiting on shutdown message");
    break;
  case MGMT_EVENT_RESTART:
    executeMgmtCallback(MGMT_EVENT_RESTART, {});
    break;
  case MGMT_EVENT_DRAIN:
    executeMgmtCallback(MGMT_EVENT_DRAIN, payload);
    break;
  case MGMT_EVENT_CLEAR_STATS:
    executeMgmtCallback(MGMT_EVENT_CLEAR_STATS, {});
    break;
  case MGMT_EVENT_HOST_STATUS_UP:
    executeMgmtCallback(MGMT_EVENT_HOST_STATUS_UP, payload);
    break;
  case MGMT_EVENT_HOST_STATUS_DOWN:
    executeMgmtCallback(MGMT_EVENT_HOST_STATUS_DOWN, payload);
    break;
  case MGMT_EVENT_ROLL_LOG_FILES:
    executeMgmtCallback(MGMT_EVENT_ROLL_LOG_FILES, {});
    break;
  case MGMT_EVENT_PLUGIN_CONFIG_UPDATE:
    if (!payload.empty() && payload.at<char>(0) != '\0' && this->cbtable) {
      this->cbtable->invoke(static_cast<char const *>(payload.data()));
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
    executeMgmtCallback(MGMT_EVENT_LIBRECORDS, payload);
    break;
  case MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE:
    executeMgmtCallback(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, payload);
    break;
  case MGMT_EVENT_LIFECYCLE_MESSAGE:
    executeMgmtCallback(MGMT_EVENT_LIFECYCLE_MESSAGE, payload);
    break;
  default:
    Warning("received unknown message ID %d\n", mh->msg_id);
    break;
  }

  ats_free(mh);
}
