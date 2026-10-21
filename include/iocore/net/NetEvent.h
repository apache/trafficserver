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

#include <atomic>

#include "tscore/List.h"
#include "tscore/TimerWheel.h"
#include "iocore/eventsystem/VIO.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/net/EventIO.h"
#include "iocore/net/ReadWriteEventIO.h"

class NetHandler;

class Event;
class NetEvent;

struct NetState {
  int             enabled = 0;
  VIO             vio;
  Link<NetEvent>  ready_link;
  SLink<NetEvent> enable_link;
  int             in_enabled_list = 0;
  int             triggered       = 0;

  NetState() : vio(VIO::NONE) {}
};

/** A NetEvent deadline. The owning thread reads it without the NetEvent's lock
 * while other threads may write it, so accesses are relaxed atomics. A new
 * deadline reaches the owner through the handoff list rearm_timer() uses, not
 * through this type. += is a load and a store, not an RMW.
 */
class NetDeadline
{
public:
  NetDeadline(ink_hrtime v = 0) : _v(v) {}
  NetDeadline(NetDeadline const &that) : _v(that.load()) {}

  NetDeadline &
  operator=(ink_hrtime v)
  {
    _v.store(v, std::memory_order_relaxed);
    return *this;
  }

  NetDeadline &
  operator=(NetDeadline const &that)
  {
    return *this = that.load();
  }

  NetDeadline &
  operator+=(ink_hrtime delta)
  {
    return *this = load() + delta;
  }

  ink_hrtime
  load() const
  {
    return _v.load(std::memory_order_relaxed);
  }

  operator ink_hrtime() const { return load(); }

private:
  std::atomic<ink_hrtime> _v;
};

// this class is used to NetHandler to hide some detail of NetEvent.
// To combine the `UDPConenction` and `NetEvent`. NetHandler should
// callback to net_read_io or net_write_io when net event happen.
class NetEvent
{
public:
  NetEvent() = default;
  virtual ~NetEvent() {}
  virtual void net_read_io(NetHandler *nh)  = 0;
  virtual void net_write_io(NetHandler *nh) = 0;
  virtual void free_thread(EThread *t)      = 0;

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
  virtual int              get_fd()            = 0;
  virtual Ptr<ProxyMutex> &get_mutex()         = 0;
  virtual ContFlags       &get_control_flags() = 0;

  ReadWriteEventIO ep{};
  NetState         read{};
  NetState         write{};

  /** Set when the connection is closed and the owning thread should free this object.

      The owning thread reads this without a lock in NetHandler::process_ready_list, and a
      migration sets it from another thread after moving the socket out, so stores release and
      loads acquire to order the move before the reclamation.
   */
  std::atomic<int> closed{0};
  int              error = 0;
  NetHandler      *nh    = nullptr;

  /** The explicitly set inactivity timeout duration in seconds.
   *
   * 0 means no timeout.
   */
  ink_hrtime inactivity_timeout_in = 0;

  /** The fallback inactivity timeout which is applied if no other timeouts are
   * set. That is, this timeout is used if inactivity_timeout_in is 0.
   *
   * A value of 0 means no timeout. A value of -1 means that no default timeout
   * has been set yet. This is initialized to -1 instead of 0 so that the
   * inactivity cop can distinguish between no value having been set and a
   * value of 0 having been set by some override plugin.
   */
  std::atomic<ink_hrtime> default_inactivity_timeout_in = -1;

  /** The active timeout duration in seconds. */
  ink_hrtime active_timeout_in = 0;

  /** The time of the next inactivity timeout. */
  NetDeadline next_inactivity_timeout_at;

  /** The time of the next activity timeout. */
  NetDeadline next_activity_timeout_at;
  ink_hrtime  submit_time = 0;

  /** Whether the current timeout is a default inactivity timeout. */
  bool use_default_inactivity_timeout = false;

  LINK(NetEvent, open_link);
  LINKM(NetEvent, read, ready_link)
  SLINKM(NetEvent, read, enable_link)
  LINKM(NetEvent, write, ready_link)
  SLINKM(NetEvent, write, enable_link)
  LINK(NetEvent, keep_alive_queue_link);
  LINK(NetEvent, active_queue_link);
  LINK(NetEvent, timer_link);
  TimerWheelHook timer_hook;

  /// Bits of @a handoff. Only the owning thread may touch its timer wheel, so
  /// other threads queue the NetEvent on NetHandler::handoff_list instead;
  /// NetHandler::process_handoffs() consumes it there.
  static constexpr uint8_t HANDOFF_QUEUED  = 1; ///< On handoff_list.
  static constexpr uint8_t HANDOFF_CLOSING = 2; ///< Closed by another thread; the owner must not free it yet.
  std::atomic<uint8_t>     handoff{0};
  SLINK(NetEvent, handoff_link);

  /** Re-arm this NetEvent's slot in its NetHandler's timer wheel.
   *
   * Required after any change that makes a deadline earlier - setting a
   * timeout, shortening one, or closing. Extending needs no call: the wheel
   * re-reads the real deadline when the bucket comes due. Safe from any thread:
   * off the owning thread it is handed to the owner and applied on its next
   * loop.
   */
  void rearm_timer();

  /** Follow a change to default_inactivity_timeout_in, which was @a previous_in.
   *
   * A deadline the default produced must move with it. Otherwise a shorter
   * override, such as a per-remap default installed after the global one armed
   * the connection, would be ignored until the old deadline: netActivity() only
   * extends deadlines and never re-arms. Keeps the idle time already accrued.
   */
  void rebase_default_inactivity_deadline(ink_hrtime previous_in);

  /** Arm the default inactivity timeout on an enabled NetEvent that has no
   * inactivity deadline, replacing any explicit timeout, and re-arm the wheel.
   *
   * The wheel only revisits scheduled NetEvents, so every path that can leave
   * an enabled NetEvent without a deadline must call this; the pre-wheel cop
   * applied the same rule to every open NetEvent once a second.
   *
   * @return @c true if the default was armed.
   */
  bool arm_default_inactivity_timeout();

  /// Values for @a f.shutdown
  static constexpr unsigned SHUTDOWN_READ  = 1;
  static constexpr unsigned SHUTDOWN_WRITE = 2;

  union {
    unsigned int flags = 0;
    struct {
      unsigned int got_local_addr : 1;
      unsigned int shutdown       : 2;
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
