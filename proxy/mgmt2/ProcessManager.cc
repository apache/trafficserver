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
 * ProcessManager.cc
 *   The Process Manager of the management system. File contains the
 * member function defs and thread loop for the process manager.
 *
 * $Date: 2007-10-05 16:56:44 $
 *
 *
 */

#include "inktomi++.h"
#undef HTTP_CACHE
#include "InkAPIInternal.h"
#include "MgmtUtils.h"
#define _PROCESS_MANAGER
#include "ProcessManager.h"

#include "ink_apidefs.h"
#include "MgmtSocket.h"

#ifndef DEFAULT_LOCAL_STATE_DIRECTORY // FIXME: consolidate defines
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#endif

/*
 * Global ProcessManager
 */
inkcoreapi ProcessManager *pmgmt = NULL;

void syslog_thr_init();

/*
 * startProcessManager(...)
 *   The start function and thread loop for the process manager.
 */
void *
startProcessManager(void *arg)
{
  void *ret = arg;


  syslog_thr_init();

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

ProcessManager::ProcessManager(bool rlm, char *mpath, ProcessRecords * rd):
BaseManager(), require_lm(rlm), mgmt_sync_key(0), record_data(rd), local_manager_sockfd(0)
{
  ink_strncpy(pserver_path, DEFAULT_LOCAL_STATE_DIRECTORY, sizeof(pserver_path));
  mgmt_signal_queue = create_queue();

  // Set temp. process/manager timeout. Will be reconfigure later.
  // Making the process_manager thread a spinning thread to start traffic server
  // as quickly as possible. Will reset this timeout when reconfigure()
  timeout = 0;

}                               /* End ProcessManager::ProcessManager */


// This function must be call after RecProcessInitMessage() has been invoked,
// otherwise, REC_readInteger would result in randome values.
#ifdef DEBUG_MGMT
static void *drainBackDoor(void *arg);
#endif

void
ProcessManager::reconfigure()
{
  bool found;
  int enable_mgmt_port = 0;
  timeout = (int)
    REC_readInteger("proxy.config.process_manager.timeout", &found);
  timeout = 5;
  ink_assert(found);

  enable_mgmt_port = (int)
    REC_readInteger("proxy.config.process_manager.enable_mgmt_port", &found);
  ink_assert(found);

#ifdef DEBUG_MGMT
  if (enable_mgmt_port) {
    ink_thread_create(drainBackDoor, 0);
  }
#endif /* DEBUG_MGMT */
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

  mh = (MgmtMessageHdr *) xmalloc(sizeof(MgmtMessageHdr) + data_len);
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

    Debug("pmgmt", "[ProcessManager] ==> Processing event id '%d'\n", mh->msg_id);
    if (mh->data_len > 0) {
      executeMgmtCallback(mh->msg_id, (char *) mh + sizeof(MgmtMessageHdr), mh->data_len);
    } else {
      executeMgmtCallback(mh->msg_id, NULL, 0);
    }
    if (mh->msg_id == MGMT_EVENT_SHUTDOWN) {
      /* 3com does not want these messages to be seen */
      /* Actually one instance of this message is made visible */

      mgmt_log(stderr, "[ProcessManager::processEventQueue] Shutdown msg received, exiting\n");
      _exit(0);
    }                           /* Exit on shutdown */
    xfree(mh);
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

#ifndef _WIN32
    if (require_lm && mgmt_write_pipe(local_manager_sockfd, (char *) mh, sizeof(MgmtMessageHdr) + mh->data_len) <= 0) {
#else

#error "[ewong] need to port the new messaging mechanism to windows!"

    if (require_lm && mgmt_write_pipe(local_manager_hpipe, tmp, strlen(tmp)) != 0) {
#endif
      mgmt_fatal(stderr, "[ProcessManager::processSignalQueue] Error writing message!");
      //ink_assert(enqueue(mgmt_signal_queue, mh));
    } else {
      xfree(mh);
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
  pid_t pid;
  char *sync_key_raw = NULL;

#ifndef _WIN32
  int servlen;
  struct sockaddr_un serv_addr;

  /* Setup Connection to LocalManager */
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;

  snprintf(message, sizeof(message), "%s/%s", pserver_path, LM_CONNECTION_SERVER);
  ink_strncpy(serv_addr.sun_path, message, sizeof(serv_addr.sun_path));
#if (HOST_OS == darwin) || (HOST_OS == freebsd)
  servlen = sizeof(sockaddr_un);
#else
  servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);
#endif
  if ((local_manager_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Unable to create socket\n");
  }

  if (fcntl(local_manager_sockfd, F_SETFD, 1) < 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Unable to set close-on-exec\n");
  }

  if ((connect(local_manager_sockfd, (struct sockaddr *) &serv_addr, servlen)) < 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Connect failed\n");
  }

  /* Say HI! and give your name(pid). */
  pid = record_data->pid;
  data_len = sizeof(pid_t);
  mh_full = (MgmtMessageHdr *) alloca(sizeof(MgmtMessageHdr) + data_len);
  mh_full->msg_id = MGMT_SIGNAL_PID;
  mh_full->data_len = data_len;
  memcpy((char *) mh_full + sizeof(MgmtMessageHdr), &(pid), data_len);
  if (mgmt_write_pipe(local_manager_sockfd, (char *) mh_full, sizeof(MgmtMessageHdr) + data_len) <= 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error writing message!\n");
  }

  /* Read SYNC_KEY from manager */
  if (mgmt_read_pipe(local_manager_sockfd, (char *) &mh_hdr, sizeof(MgmtMessageHdr)) <= 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error reading sem message!\n");
  } else {
    // coverity[uninit_use]
    mh_full = (MgmtMessageHdr *) alloca(sizeof(MgmtMessageHdr) + mh_hdr.data_len);
    memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
    sync_key_raw = (char *) mh_full + sizeof(MgmtMessageHdr);
    if (mgmt_read_pipe(local_manager_sockfd, sync_key_raw, mh_hdr.data_len) < 0) {
      mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error reading sem message!\n");
    }
  }

#else

#error "[ewong] need to port the new messaging mechanism to windows!"

  sprintf(message, "\\\\.\\pipe\\traffic_server_%s", LM_CONNECTION_SERVER);
  local_manager_hpipe = CreateFile(message, GENERIC_READ | GENERIC_WRITE,
                                   0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, NULL);

  if (local_manager_hpipe == INVALID_HANDLE_VALUE) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error opening named pipe: %s\n", ink_last_err());
  }

  /* Say HI! and give your name(pid). */
  sprintf(message, "pid: %ld", record_data->pid);
  if (mgmt_write_pipe(local_manager_hpipe, message, strlen(message)) != 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error writing message! %s\n", ink_last_err());
  }

  if (mgmt_read_pipe(local_manager_hpipe, message, 1024) < 0) {
    mgmt_fatal(stderr, "[ProcessManager::initLMConnection] Error reading sem message! %s\n", ink_last_err());
  }
#endif

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

#ifndef _WIN32
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
          mgmt_fatal(stderr, "[ProcessManager::pollLMConnection] Error in read!");
        }
      } else if (res < 0) {
        mgmt_fatal(stderr, "[ProcessManager::pollLMConnection] Error in read!");
      }
      // handle EOF
      if (res == 0) {
        ink_close_socket(local_manager_sockfd);
        mgmt_fatal(stderr, "[ProcessManager::pollLMConnection] Lost Manager EOF!");
      }

    } else if (num < 0) {       /* Error */
      mgmt_elog(stderr, "[ProcessManager::pollLMConnection] select failed or was interrupted (%d)\n", errno);
    }

  }
#else

#error "[ewong] need to port the new messaging mechanism to windows!"

  char message[1024];
  DWORD bytesAvail = 0;
  BOOL status = PeekNamedPipe(local_manager_hpipe, NULL, 0, NULL, &bytesAvail, NULL);

  if (status != FALSE && bytesAvail != 0) {
    res = mgmt_read_pipe(local_manager_hpipe, message, 1024);

    if (res < 0) {              /* Error */
      status = FALSE;
    } else {
      Debug("pmgmt", "[ProcessManager::pollLMConnection] Message: '%s'\n", message);
      handleMgmtMsgFromLM(message);
    }
  } else {
    // avoid tight poll loop -- select() in Unix version above times out if no data.
    mgmt_sleep_msec(poll_timeout.tv_sec * 1000 + poll_timeout.tv_usec / 1000);
  }
  if (status == FALSE) {
    CloseHandle(local_manager_hpipe);
    mgmt_fatal(stderr, "[ProcessManager::pollLMConnection] Lost Manager! %s\n", ink_last_err());
  }
#endif // !_WIN32

}                               /* End ProcessManager::pollLMConnection */

void
ProcessManager::handleMgmtMsgFromLM(MgmtMessageHdr * mh)
{
  char *data_raw = (char *) mh + sizeof(MgmtMessageHdr);

  if (!record_data->ignore_manager) {   /* Check if we are speaking to the manager */
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
      if (data_raw != NULL && data_raw[0] != '\0') {
        global_config_cbs->invoke(data_raw);
      }
      break;
    case MGMT_EVENT_HTTP_CLUSTER_DELTA:
      signalMgmtEntity(MGMT_EVENT_HTTP_CLUSTER_DELTA, data_raw);
      break;
    case MGMT_EVENT_CONFIG_FILE_UPDATE:
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
    default:
      mgmt_elog(stderr, "[ProcessManager::pollLMConnection] unknown type %d\n", mh->msg_id);
      break;
    }
  }
}

bool
ProcessManager::addPluginCounter(const char *name, MgmtIntCounter value)
{
  if (record_data->addPluginCounter(name, value) == true) {
    char msg[512];
    sprintf(msg, "%s %d %lld", name, INK_COUNTER, value);
    signalManager(MGMT_SIGNAL_PLUGIN_ADD_REC, msg);
    return true;
  }

  return false;
}

bool
ProcessManager::addPluginInteger(const char *name, MgmtInt value)
{
  if (record_data->addPluginInteger(name, value) == true) {
    char msg[512];
    sprintf(msg, "%s %d %lld", name, INK_INT, value);
    pmgmt->signalManager(MGMT_SIGNAL_PLUGIN_ADD_REC, msg);
    return true;
  }

  return false;
}

bool
ProcessManager::addPluginFloat(const char *name, MgmtFloat value)
{
  if (record_data->addPluginFloat(name, value) == true) {
    char msg[512];
    sprintf(msg, "%s %d %.5f", name, INK_FLOAT, value);
    pmgmt->signalManager(MGMT_SIGNAL_PLUGIN_ADD_REC, msg);
    return true;
  }

  return false;
}

bool
ProcessManager::addPluginString(const char *name, MgmtString value)
{
  if (record_data->addPluginString(name, value) == true) {
    char msg[512];
    sprintf(msg, "%s %d %s", name, INK_STRING, value);
    pmgmt->signalManager(MGMT_SIGNAL_PLUGIN_ADD_REC, msg);
    return true;
  }

  return false;
}

#ifdef DEBUG_MGMT
static bool checkBackDoorP(int req_fd, char *message);
/*
 * drainBackDoor(...)
 *   This function is blocking, it never returns. It is meant to allow for
 * continuous draining of the network.
 */
static void *
drainBackDoor(void *arg)
{
  bool found;
  int port, fd, one = 1;
  //
  // Don't allocate the message buffer on the stack..
  //
  const int message_size = 61440;
  char *message = new char[message_size];
  fd_set fdlist;
  struct sockaddr_in cli_addr, serv_addr;
  void *ret = arg;

  while (!pmgmt) {
    mgmt_sleep_sec(1);
  }

  port = (int)
    REC_readInteger("proxy.config.process_manager.mgmt_port", &found);
  if (!found) {
    mgmt_log(stderr, "[drainBackDoor] Unable to get mgmt port config variable\n");
  }

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    mgmt_log(stderr, "[drainBackDoor] Unable to create socket\n");
    return ret;
  }

  if (fcntl(fd, F_SETFD, 1) < 0) {
    mgmt_fatal(stderr, "[drainBackDoor] Unable to set close-on-exec\n");
  }

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int)) < 0) {
    mgmt_log(stderr, "[drainBackDoor] Unable to setsockopt\n");
    return ret;
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  if ((bind(fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0) {
    mgmt_log(stderr, "[drainBackDoor] Unable to bind socket\n");
    return ret;
  }

  if ((listen(fd, 10)) < 0) {
    mgmt_log(stderr, "[drainBackDoor] Unable to listen on socket\n");
    return ret;
  }

  for (;;) {                    /* Loop draining mgmt network */

    memset(message, 0, message_size);
    FD_ZERO(&fdlist);
    FD_SET(fd, &fdlist);
    mgmt_select(FD_SETSIZE, &fdlist, NULL, NULL, NULL);

    if (FD_ISSET(fd, &fdlist)) {        /* Request */
      int clilen = sizeof(cli_addr);
      int req_fd = accept(fd, (struct sockaddr *) &cli_addr, &clilen);

      if (fcntl(req_fd, F_SETFD, 1) < 0) {
        mgmt_elog(stderr, "[drainBackDoor] Unable to set close on exec flag\n");
        ink_close_socket(req_fd);
        continue;
      }

      if (req_fd < 0) {
        mgmt_elog(stderr, "[drainBackDoor] Request accept failed\n");
        continue;
      } else {

        if (mgmt_readline(req_fd, message, 61440) > 0 && !checkBackDoorP(req_fd, message)) {    /* Heh... */
          mgmt_elog(stderr, "[drainBackDoor] Received unknown message: '%s'\n", message);
          ink_close_socket(req_fd);
          continue;
        }
      }
      ink_close_socket(req_fd);
    }
  }
  return ret;
}                               /* End drainBackDoor */


/*
 * checkBackDoorP(...)
 *   Function checks for "backdoor" inktomi commands on dedicated back door port.
 */
static bool
checkBackDoorP(int req_fd, char *message)
{
  char reply[4096];

  if (strstr(message, "read ")) {
    int id;
    char variable[1024];
    RecordType type;
    InkHashTableValue hash_value;

    if (sscanf(message, "read %s\n", variable) != 1) {
      mgmt_elog(stderr, "[CBDP] Bad message(%d) '%s'\n", __LINE__, message);
      return false;
    }

    if (pmgmt->record_data->idofRecord(variable, &id, &type)) {

      ink_mutex_acquire(&(pmgmt->record_data->mutex[type]));
      if (pmgmt->record_data->record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
        Records *the_records = (Records *) hash_value;
        switch (the_records->recs[id].stype) {
        case INK_COUNTER:
          sprintf(reply, "\nRecord '%s' Val: '%lld'\n", the_records->recs[id].name,
                      the_records->recs[id].data.counter_data);
          break;
        case INK_INT:
          sprintf(reply, "\nRecord: '%s' Val: '%lld'\n", the_records->recs[id].name,
                      the_records->recs[id].data.int_data);
          break;
        case INK_LLONG:
          sprintf(reply, "\nRecord: '%s' Val: '%lld'\n", the_records->recs[id].name,
                      the_records->recs[id].data.llong_data);
          break;
        case INK_FLOAT:
          sprintf(reply, "\nRecord: '%s' Val: '%f'\n", the_records->recs[id].name,
                  the_records->recs[id].data.float_data);
          break;
        case INK_STRING:
          if (the_records->recs[id].name) {
            sprintf(reply, "\nRecord: '%s' Val: '%s'\n", the_records->recs[id].name,
                    the_records->recs[id].data.string_data);
          } else {
            sprintf(reply, "\nRecord: '%s' Val: NULL\n", the_records->recs[id].name);
          }
          break;
        default:
          break;
        }
        mgmt_writeline(req_fd, reply, strlen(reply));
      }
      ink_mutex_release(&(pmgmt->record_data->mutex[type]));
    } else {
      mgmt_elog(stderr, "[checkBackDoorP] Unknown variable requested '%s'\n", variable);
    }
    return true;
  } else if (strstr(message, "write ")) {
    int id;
    char variable[1024], value[1024];
    RecordType type;

    if (sscanf(message, "write %s %s", variable, value) != 2) {
      mgmt_elog(stderr, "[CBDP] Bad message(%d) '%s'\n", __LINE__, message);
      return false;
    }
    if (pmgmt->record_data->idofRecord(variable, &id, &type)) {
      switch (pmgmt->record_data->typeofRecord(id, type)) {
      case INK_COUNTER:
        pmgmt->record_data->setCounter(id, type, ink_atoll(value));
        break;
      case INK_INT:
        pmgmt->record_data->setInteger(id, type, ink_atoll(value));
        break;
      case INK_LLONG:
        pmgmt->record_data->setLLong(id, type, ink_atoll(value));
        break;
      case INK_FLOAT:
        pmgmt->record_data->setFloat(id, type, atof(value));
        break;
      case INK_STRING:
        pmgmt->record_data->setString(id, type, value);
        break;
      default:
        break;
      }
      strcpy(reply, "\nRecord Updated\n\n");
      mgmt_writeline(req_fd, reply, strlen(reply));

    } else {
      mgmt_elog(stderr, "[checkBackDoorP] Assignment to unknown variable requested '%s'\n", variable);
    }
    return true;
  } else if (strstr(message, "signal ")) {
    int id;
    char value[1024];
    RecordType type;

    if (sscanf(message, "signal %s", value) != 1) {
      mgmt_elog(stderr, "[CBDP] Bad message(%d) '%s'\n", __LINE__, message);
      return false;
    }
    if (pmgmt->record_data->idofRecord(value, &id, &type) && type == CONFIG) {
      ink_mutex_acquire(&(pmgmt->record_data->mutex[CONFIG]));
      pmgmt->record_data->config_data.recs[id].changed = true;
      ink_mutex_release(&(pmgmt->record_data->mutex[CONFIG]));
    } else {
      mgmt_elog(stderr, "[checkBackDoorP] Unknown signal change: '%s'\n", value);
    }
    return true;
  } else if (strstr(message, "toggle_ignore")) {

    if (pmgmt->record_data->ignore_manager) {
      mgmt_log(stderr, "[checkBackDoorP] Now ignoring lm\n");
      pmgmt->record_data->ignore_manager = true;
    } else {
      mgmt_log(stderr, "[checkBackDoorP] Now listening to lm\n");
      pmgmt->record_data->ignore_manager = false;
    }
    return true;
  } else if (strstr(message, "shutdown")) {
    char *reply = "[checkBackDoorP] Shutting down\n";
    pmgmt->signalMgmtEntity(MGMT_EVENT_SHUTDOWN);
    mgmt_writeline(req_fd, reply, strlen(reply));
    return true;
  }
  return false;
}                               /* End checkBackDoorP */

#endif /* DEBUG_MGMT */
