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

#pragma once

#include "mgmtapi.h"
#include "I_EventSystem.h"

#include "ts/ink_mutex.h"

#include <memory> // std::unique_ptr
#include <utility> // std::forward
#include <functional>
#include <unordered_set>
#include <unordered_map>

/**
  Simple RPC server. Allows for registration of callbacks in the form (int fd, void* buf, size_t buflen). Currently, it is the
  responsibility of the callback function to deseralize the incoming message. Deserialization must be done using mgmt_marshall_parse. 

  Incoming messages should be sent using the RPCClientController interface. It is expected that message arrive in the following form.
  - 32 bit integer to indicate the size of the entire message. This is used to load the marshalled message from the socket into a buffer
  - 32 bit integer to indicate the operation type. based on the operation type, the corresponding callback handler is invoked. 
  - parameters. the following can contain anything needed to execute the RPC. 
 */
class MgmtServer
{
public:
  /// Only handler of this form can be registered. 
  using ControlCallback = std::function<TSMgmtError(int fd, void *buf, size_t len)>;

  /** 
    This holds a set of the file descriptors for all remote clients.  
   */ 
  struct Clients {
    std::unordered_set<int> connections; 
    ink_mutex mutex;

    Clients();
    ~Clients();

    std::unordered_set<int>
    clients()
    {
      return connections;
    }
    int insert(int fd);
    void remove(int fd);
  };

  /**
    CallbackExecutor
    
    Uses Continuations to schedule callbacks to be executed. The callback executor handler will deallocate the buffer 
    so any callback functions registered should not free @a buf. Each callback handler should return a TSMgmtError to 
    indiciate a success or failure. This result is always returned back to the remote client over the socket. 
   */
  struct CallbackExecutor : public Continuation {
    CallbackExecutor(ControlCallback &_cb, int _fd, void *_buf, size_t _len, int _op)
      : Continuation(new_ProxyMutex()), cb(_cb), fd(_fd), buf(_buf), len(_len), op(_op)
    {
      SET_HANDLER(&CallbackExecutor::mgmt_callback);
    }

    int mgmt_callback(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */);
    ControlCallback cb;
    int fd;
    void *buf;
    size_t len;

    MgmtMarshallInt op;
  };

  /// Creates a MgmtServer object with a @rpc_mode operating mode.
  explicit MgmtServer(mode_t rpc_mode);

  /**
    Starts the rpc server. This should only be done once all callbacks have been registered. This
    also sets up the eventProcessor to schedule in continuation callbacks. 
   */
  void start();
  void stop();

  /// Add a callback based on a key. Each key must be unique or will fatal. 
  void registerControlCallback(int key, ControlCallback cb);

  /// Sets up the file descriptor based on the socket specified in the @c path. This should be the same one used by the client.
  int bindSocket(const char *path);

  /**
    Sends a response to the file descriptor @a fd. Messages are always wrapped as a MgmtMarshallData object and contain
    a opteration type @a optype to indicate which operation the response is for. It is crucial that the optype sent is 
    the same one as the optype expected by the remote client.  Note, that we must always send a response
    to the remote client to avoid leaving it hanging. Handler functions can leverage this function to send extra data to 
    remote clients such as config info. 
   */
  template <typename... Params>
  TSMgmtError
  respond(int fd, MgmtMarshallInt optype, Params &&... params)
  {
    MgmtMarshallInt msglen;
    MgmtMarshallInt op     = static_cast<MgmtMarshallInt>(optype);
    MgmtMarshallData reply = {nullptr, 0};

    msglen = mgmt_message_length(&op, std::forward<Params>(params)...);

    ink_assert(msglen >= 0);

    reply.ptr = (char *)ats_malloc(msglen);
    reply.len = msglen;

    // Marshall the message itself.
    if (mgmt_message_marshall(reply.ptr, reply.len, &op, std::forward<Params>(params)...) == -1) {
      return TS_ERR_PARAMS;
    }

    // Send the response as the payload of a data object.
    if (mgmt_message_write(fd, &reply) == -1) {
      ats_free(reply.ptr);
      return TS_ERR_NET_WRITE;
    }

    ats_free(reply.ptr);
    return TS_ERR_OKAY;
  }

private:
  static constexpr int TIMEOUT_SECS = 5; ///< default

  mode_t mode; ///< operating mode of the rpc server. 
  Clients *client_cons; ///< set of all remote client file descriptors

  /// Main socket for accepting incoming connections. This is setup in with bindSocket(). 
  int accept_con_socket = ts::NO_FD;

  /** 
    Map of all callbacks that have been registered. A insertion to a key with an existing
    callback is fatal but a request to key without a callback will just return an error to the 
    remote client, the server will continue to run.  
   */
  std::unordered_map<int, ControlCallback> callbacks;

  /// @returns an integer indicating the key of the callback handler to be executed. 
  int getCallbackID(void *buf, size_t buf_len);

  /** 
    @returns the file descriptor of the incoming connection. Requries a @a fd for the socket 
    containing the inbound connection. 
   */ 
  int acceptNewConnection(int con_socket);

  /// Loads the entire client message into @a buf and populates @a read_len
  TSMgmtError loadBuffer(int fd, void **buf, size_t *read_len);

  /// Creates a instance of a CallbackExecutor and schedules it into the eventProcessor. 
  TSMgmtError executeCallback(int key, int fd, void *buf, size_t buf_len);

  /// Called per client request, main workflow to process the incoming message is done here. 
  TSMgmtError handleIncomingMsg(int con_fd);

  /// Just closes all the connection with the client_cons.
  void cleanup();

  /// main executing thread.
  ink_thread server_thread = ink_thread_null();
  static void *serverCtrlMain(void *arg);
};

// actual program using this must instantiate this. 
extern std::unique_ptr<MgmtServer> mgmt_server;