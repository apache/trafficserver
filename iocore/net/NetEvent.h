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

#pragma once

#include "I_EventSystem.h"

class NetHandler;

// this class is used to NetHandler to hide some detail of NetEvent.
// To combine the `UDPConenction` and `NetEvent`. NetHandler should
// callback to net_read_io or net_write_io when net event happen.
class NetEvent
{
public:
  NetEvent() = default;
  virtual ~NetEvent() {}
  virtual void net_read_io(NetHandler *nh, EThread *lthread)  = 0;
  virtual void net_write_io(NetHandler *nh, EThread *lthread) = 0;
  virtual void free(EThread *t)                               = 0;

  // since we want this class to be independent from VConnection, Continutaion. There should be
  // a pure virtual function which connect sub class and NetHandler.
  virtual int callback(int event = CONTINUATION_EVENT_NONE, void *data = nullptr) = 0;

  // Duplicate with `NetVConnection::set_inactivity_timeout`
  // TODO: more abstraction.
  virtual void set_inactivity_timeout(ink_hrtime timeout_in)         = 0;
  virtual void set_default_inactivity_timeout(ink_hrtime timeout_in) = 0;
  virtual bool is_default_inactivity_timeout()                       = 0;

  // get this vc's thread
  virtual EThread *get_thread() = 0;

  // Close when EventIO close;
  virtual int close() = 0;

  bool has_error() const;
  void set_error_from_socket();

  // get fd
  virtual int get_fd()                   = 0;
  virtual Ptr<ProxyMutex> &get_mutex()   = 0;
  virtual ContFlags &get_control_flags() = 0;

  EventIO ep{};
  NetState read{};
  NetState write{};

  int closed     = 0;
  int error      = 0;
  NetHandler *nh = nullptr;

  ink_hrtime inactivity_timeout_in      = 0;
  ink_hrtime active_timeout_in          = 0;
  ink_hrtime next_inactivity_timeout_at = 0;
  ink_hrtime next_activity_timeout_at   = 0;
  ink_hrtime submit_time                = 0;

  bool default_inactivity_timeout = false;

  LINK(NetEvent, open_link);
  LINK(NetEvent, cop_link);
  LINKM(NetEvent, read, ready_link)
  SLINKM(NetEvent, read, enable_link)
  LINKM(NetEvent, write, ready_link)
  SLINKM(NetEvent, write, enable_link)
  LINK(NetEvent, keep_alive_queue_link);
  LINK(NetEvent, active_queue_link);

  /// Values for @a f.shutdown
  static constexpr unsigned SHUTDOWN_READ  = 1;
  static constexpr unsigned SHUTDOWN_WRITE = 2;

  union {
    unsigned int flags = 0;
    struct {
      unsigned int got_local_addr : 1;
      unsigned int shutdown : 2;
    } f;
  };
};

inline bool
NetEvent::has_error() const
{
  return error != 0;
}

inline void
NetEvent::set_error_from_socket()
{
  socklen_t errlen = sizeof(error);
  getsockopt(this->get_fd(), SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
}
