/** @file

  A rpc server.

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

#include "mgmtapi.h"
#include "ts/I_Layout.h"
#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "utils/MgmtUtils.h"
#include "utils/MgmtSocket.h"
#include "utils/MgmtMarshall.h"
#include "ServerControl.h"

static RecBool disable_modification = false;

MgmtServer::MgmtServer(mode_t rpc_mode) : mode(rpc_mode)
{
  client_cons = new Clients();
}

void
MgmtServer::start()
{
  ink_event_system_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  eventProcessor.start(1); // just start one thread for now.

  Debug("mgmt_server", "starting rpc server ...");
  ink_thread_create(&server_thread, serverCtrlMain, &accept_con_socket, 0, 0, nullptr);
}

void
MgmtServer::stop()
{
  Debug("mgmt_server", "stopping rpc server ...");
  cleanup();
  ink_thread_kill(server_thread, SIGINT);

  ink_thread_join(server_thread);
  server_thread = ink_thread_null();

  delete client_cons;
}

int
MgmtServer::bindSocket(const char *path)
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string apisock(Layout::relative_to(rundir, path));
  Debug("mgmt_server", "binding to socket %s with mode %d", apisock.c_str(), mode);

  accept_con_socket = bind_unix_domain_socket(apisock.c_str(), mode);
  return accept_con_socket;
}

int
MgmtServer::acceptNewConnection(int fd)
{
  struct sockaddr_in clientAddr;
  socklen_t clientLen = sizeof(clientAddr);
  int new_sockfd      = mgmt_accept(fd, (struct sockaddr *)&clientAddr, &clientLen);

  Debug("mgmt_server", "established connection to fd %d", new_sockfd);
  return mgmt_server->client_cons->insert(new_sockfd);
}

void *
MgmtServer::serverCtrlMain(void *arg)
{
  int *res       = static_cast<int *>(arg);
  int con_socket = *res;
  int ret;

  if (con_socket == ts::NO_FD) {
    Fatal("mgmt_server not bound to a socket. please call bindSocket(). exiting ...");
  }

  fd_set selectFDs; // for select call
  int fds_ready;    // stores return value for select
  struct timeval timeout;

  // loops until TM dies; waits for and processes requests from clients
  while (true) {
    // LINUX: to prevent hard-spin of CPU,  reset timeout on each loop
    timeout.tv_sec  = TIMEOUT_SECS;
    timeout.tv_usec = 0;

    FD_ZERO(&selectFDs);

    if (con_socket >= 0) {
      FD_SET(con_socket, &selectFDs);
    }

    // we need a local set of the clients to avoid aquiring the mutex. 
    std::unordered_set<int> clients = mgmt_server->client_cons->clients();

    // add all clients to select read set.
    for (auto const &client_fd : clients) {
      if (client_fd >= 0) {
        FD_SET(client_fd, &selectFDs);
        Debug("mgmt_server", "adding fd %d to select set", client_fd);
      }
    }

    // select call - timeout is set so we can check events at regular intervals
    fds_ready = mgmt_select(FD_SETSIZE, &selectFDs, (fd_set *)nullptr, (fd_set *)nullptr, &timeout);

    // check if have any connections or requests
    while (fds_ready > 0) {
      RecGetRecordBool("proxy.config.disable_configuration_modification", &disable_modification);

      if (con_socket >= 0 && FD_ISSET(con_socket, &selectFDs)) { // New client connection
        --fds_ready;
        ret = mgmt_server->acceptNewConnection(con_socket);
        if (ret < 0) { // error
          Debug("mgmt_server", "error adding connection with code %d", ret);
        }
      }

      if (fds_ready > 0) { // Request from remote API client
        --fds_ready;

        for (auto const &client_fd : clients) {
          Debug("mgmt_server", "We have a remote client request!");

          if (client_fd && FD_ISSET(client_fd, &selectFDs)) { // determine which client request came from.
            ret = mgmt_server->handleIncomingMsg(client_fd);

            if (ret != TS_ERR_OKAY) {
              Debug("mgmt_server", "error sending response to (%d) with code (%d)", client_fd, ret);
              mgmt_server->client_cons->remove(client_fd);
              continue;
            }
          }
        }
      } // end - Request from remote API Client
    }   // end - while (fds_ready > 0)
  }     // end - while(true)

  // if we get here something's wrong, just clean up
  Debug("mgmt_server", "CLOSING AND SHUTTING DOWN OPERATIONS");
  close_socket(con_socket);

  ink_thread_exit(nullptr);
  return nullptr;
}

// Pulls socket off the wire and gives data to callback function
TSMgmtError
MgmtServer::handleIncomingMsg(int fd)
{
  TSMgmtError ret;
  int id        = 0;
  void *req     = nullptr;
  size_t reqlen = 0;

  // pull the message off the socket
  ret = loadBuffer(fd, &req, &reqlen);
  if (ret == TS_ERR_NET_READ) {
    Debug("mgmt_server", "error - couldn't read from socket %d . dropping message.", fd);
    ats_free(req);
    return TS_ERR_FAIL;
  }

  // read the message id.
  id = getCallbackID(req, reqlen);
  if (id < 0) {
    Debug("mgmt_server", "error unable to parse msg. got %d", id);
    ats_free(req);
    return TS_ERR_FAIL;
  }

  // strip the message key away. keep only the actual parameters.
  MgmtMarshallInt ignore;
  ssize_t keylen = mgmt_message_length(&ignore);
  if (keylen == -1) {
    Debug("mgmt_server", "couldn't strip id from message");
    ats_free(req);
    return TS_ERR_FAIL;
  }

  void *parameters = ats_malloc(reqlen - keylen);
  ssize_t plen     = reqlen - keylen;
  ink_assert(plen >= 0);

  memcpy(parameters, static_cast<uint8_t *>(req) + keylen, plen);
  ats_free(req);

  ret = executeCallback(id, fd, parameters, plen); // callback executor will deallocate the memory.
  if (ret != TS_ERR_OKAY) {
    Debug("mgmt_server", "couldn't execute callback with id %d", id);
    return TS_ERR_FAIL;
  }

  // normally, the parameters buffer should be freed by the callback executor
  return TS_ERR_OKAY;
}

// reads rpc message into a buffer.
TSMgmtError
MgmtServer::loadBuffer(int fd, void **buf, size_t *read_len)
{
  MgmtMarshallData msg;

  *buf      = nullptr;
  *read_len = 0;

  // pull the message off the socket.
  if (mgmt_message_read(fd, &msg) == -1) {
    return TS_ERR_NET_READ;
  }

  // We should never receive an empty payload.
  if (msg.ptr == nullptr) {
    return TS_ERR_NET_READ;
  }

  *buf      = msg.ptr;
  *read_len = msg.len;
  Debug("mgmt_server", "read message length = %zd", msg.len);
  return TS_ERR_OKAY;
}

void
MgmtServer::registerControlCallback(int key, ControlCallback cb)
{
  // we need this additional layer of indirection to hold regular functions, 
  // function pointers and lambdas.
  ControlCallback func = cb;
  if(callbacks.find(key) != callbacks.end()) { // existing callback is a fatal. 
    Fatal("mgmt_server there exists a callback handler for %d please use a different key", key);
  }
  callbacks.insert(std::make_pair(key, func));
}

int
MgmtServer::getCallbackID(void *buf, size_t buf_len)
{
  MgmtMarshallInt id;
  if (mgmt_message_parse(buf, buf_len, &id) == -1) {
    return -1;
  }
  Debug("mgmt_server", "executing callback with id: %d", static_cast<int>(id));
  return static_cast<int>(id);
}

void
MgmtServer::cleanup()
{
  // Just closes all the connections. 
  for (auto const &client_fd : client_cons->connections) {
    if (client_fd >= 0) {
      close_socket(client_fd);
    }
  }
}

TSMgmtError
MgmtServer::executeCallback(int key, int fd, void *buf, size_t len)
{
  // Schedules in a CallbackExecutor to handle the incoming request. 
  auto callback = callbacks.find(key);

  if (callback != callbacks.end()) {
    Debug("mgmt_server", "scheduling in callback %d", key);
    CallbackExecutor *c = new CallbackExecutor(callback->second, fd, buf, len, key);
    eventProcessor.schedule_imm(c);
  } else { // no handler. 
    Debug("mgmt_server", "no callback for signal %d", key);
    ats_free(buf);
    return TS_ERR_FAIL;
  }

  return TS_ERR_OKAY;
}

int
MgmtServer::Clients::insert(int fd)
{
  if (fd < 0)
    return fd;

  ink_mutex_acquire(&mutex);
  if (connections.find(fd) == connections.end()) {
    connections.insert(fd);
  } else {
    ink_mutex_release(&mutex);
    return -1;
  }
  ink_mutex_release(&mutex);
  return 0;
}

void
MgmtServer::Clients::remove(int fd)
{
  ink_mutex_acquire(&mutex);
  connections.erase(fd);
  ink_mutex_release(&mutex);
}

/// Need constructor and destructor for ink_mutex.
MgmtServer::Clients::Clients()
{
  ink_mutex_init(&mutex);
}

MgmtServer::Clients::~Clients()
{
  ink_mutex_acquire(&mutex);
  ink_mutex_release(&mutex);
  ink_mutex_destroy(&mutex);
}

int
MgmtServer::CallbackExecutor::mgmt_callback(int event, Event *e)
{
  // execute the callback
  Debug("mgmt_server", "executing callback %d", op);
  TSMgmtError err = cb(fd, buf, len);
  Debug("mgmt_server", "callback result code %d", static_cast<int>(err));
  ats_free(buf); // memory no longer needed.

  MgmtMarshallInt ecode = err;
  err                   = mgmt_server->respond(fd, op, &ecode);
  if (err != TS_ERR_OKAY) {
    // unfortunately can't do much here except log it. don't want a fatal because TS might
    // just be down in which case we don't want to kill TM.
    Debug("mgmt_server", "couldn't send response to fd %d with code %d", fd, err);
  }

  delete this;
  return EVENT_DONE;
}
