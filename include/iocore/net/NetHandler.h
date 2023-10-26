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

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/net/NetEvent.h"

//
// NetHandler
//
// A NetHandler handles the Network IO operations.  It maintains
// lists of operations at multiples of it's periodicity.
//

/**
  NetHandler is the processor of NetEvent for the Net sub-system. The NetHandler
  is the core component of the Net sub-system. Once started, it is responsible
  for polling socket fds and perform the I/O tasks in NetEvent.

  The NetHandler is executed periodically to perform read/write tasks for
  NetVConnection. The NetHandler::mainNetEvent() should be viewed as a part of
  EThread::execute() loop. This is the reason that Net System is a sub-system.

  By get_NetHandler(this_ethread()), you can get the NetHandler object that
  runs inside the current EThread and then @c startIO / @c stopIO which
  assign/release a NetEvent to/from NetHandler. Before you call these functions,
  holding the mutex of this NetHandler is required.

  The NetVConnection provides a set of do_io functions through which you can
  specify continuations to be called back by its NetHandler. These function
  calls do not block. Instead they return an VIO object and schedule the
  callback to the continuation passed in when there are I/O events occurred.

  Multi-thread scheduler:

  The NetHandler should be viewed as multi-threaded schedulers which process
  NetEvents from their queues. If vc wants to be managed by NetHandler, the vc
  should be derived from NetEvent. The vc can be made of NetProcessor
  (allocate_vc) either by directly adding a NetEvent to the queue
  (NetHandler::startIO), or more conveniently, calling a method service call
  (NetProcessor::connect_re) which synthesizes the NetEvent and places it in the
  queue.

  Callback event codes:

  These event codes for do_io_read and reenable(read VIO) task:
    VC_EVENT_READ_READY, VC_EVENT_READ_COMPLETE,
    VC_EVENT_EOS, VC_EVENT_ERROR

  These event codes for do_io_write and reenable(write VIO) task:
    VC_EVENT_WRITE_READY, VC_EVENT_WRITE_COMPLETE
    VC_EVENT_ERROR

  There is no event and callback for do_io_shutdown / do_io_close task.

  NetVConnection allocation policy:

  VCs are allocated by the NetProcessor and deallocated by NetHandler.
  A state machine may access the returned, non-recurring NetEvent / VIO until
  it is closed by do_io_close. For recurring NetEvent, the NetEvent may be
  accessed until it is closed. Once the NetEvent is closed, it's the
  NetHandler's responsibility to deallocate it.

  Before assign to NetHandler or after release from NetHandler, it's the
  NetEvent's responsibility to deallocate itself.

 */
class NetHandler : public Continuation, public EThread::LoopTailHandler
{
  using self_type = NetHandler; ///< Self reference type.
public:
  // @a thread and @a trigger_event are redundant - you can get the former from
  // the latter. If we don't get rid of @a trigger_event we should remove @a
  // thread.
  EThread *thread      = nullptr;
  Event *trigger_event = nullptr;
  QueM(NetEvent, NetState, read, ready_link) read_ready_list;
  QueM(NetEvent, NetState, write, ready_link) write_ready_list;
  Que(NetEvent, open_link) open_list;
  DList(NetEvent, cop_link) cop_list;
  ASLLM(NetEvent, NetState, read, enable_link) read_enable_list;
  ASLLM(NetEvent, NetState, write, enable_link) write_enable_list;
  Que(NetEvent, keep_alive_queue_link) keep_alive_queue;
  uint32_t keep_alive_queue_size = 0;
  Que(NetEvent, active_queue_link) active_queue;
  uint32_t active_queue_size = 0;

  /// configuration settings for managing the active and keep-alive queues
  struct Config {
    uint32_t max_connections_in                 = 0;
    uint32_t max_requests_in                    = 0;
    uint32_t inactive_threshold_in              = 0;
    uint32_t transaction_no_activity_timeout_in = 0;
    uint32_t keep_alive_no_activity_timeout_in  = 0;
    uint32_t default_inactivity_timeout         = 0;
    uint32_t additional_accepts                 = 0;

    /** Return the address of the first value in this struct.

        Doing updates is much easier if we treat this config struct as an array.
        Making it a method means the knowledge of which member is the first one
        is localized to this struct, not scattered about.
     */
    uint32_t &
    operator[](int n)
    {
      return *(&max_connections_in + n);
    }
  };
  /** Static global config, set and updated per process.

      This is updated asynchronously and then events are sent to the NetHandler
     instances per thread to copy to the per thread config at a convenient time.
     Because these are updated independently from the command line, the update
     events just copy a single value from the global to the local. This
     mechanism relies on members being identical types.
  */
  static Config global_config;
  Config config; ///< Per thread copy of the @c global_config
  // Active and keep alive queue values that depend on other configuration
  // values. These are never updated directly, they are computed from other
  // config values.
  uint32_t max_connections_per_thread_in = 0;
  uint32_t max_requests_per_thread_in    = 0;
  /// Number of configuration items in @c Config.
  static constexpr int CONFIG_ITEM_COUNT = sizeof(Config) / sizeof(uint32_t);
  /// Which members of @c Config the per thread values depend on.
  /// If one of these is updated, the per thread values must also be updated.
  static const std::bitset<CONFIG_ITEM_COUNT> config_value_affects_per_thread_value;
  /// Set of thread types in which nethandlers are active.
  /// This enables signaling the correct instances when the configuration is
  /// updated. Event type threads that use @c NetHandler must set the
  /// corresponding bit.
  static std::bitset<std::numeric_limits<unsigned int>::digits> active_thread_types;

  int mainNetEvent(int event, Event *data);
  int waitForActivity(ink_hrtime timeout) override;
  void process_enabled_list();
  void process_ready_list();
  void manage_keep_alive_queue();
  bool manage_active_queue(NetEvent *ne, bool ignore_queue_size);
  void add_to_keep_alive_queue(NetEvent *ne);
  void remove_from_keep_alive_queue(NetEvent *ne);
  bool add_to_active_queue(NetEvent *ne);
  void remove_from_active_queue(NetEvent *ne);
  int get_additional_accepts();

  /// Per process initialization logic.
  static void init_for_process();
  /// Update configuration values that are per thread and depend on other
  /// configuration values.
  void configure_per_thread_values();

  /**
    Start to handle read & write event on a NetEvent.
    Initial the socket fd of ne for polling system.
    Only be called when holding the mutex of this NetHandler.

    @param ne NetEvent to be managed by this NetHandler.
    @return 0 on success, ne->nh set to this NetHandler.
            -ERRNO on failure.
   */
  int startIO(NetEvent *ne);
  /**
    Stop to handle read & write event on a NetEvent.
    Remove the socket fd of ne from polling system.
    Only be called when holding the mutex of this NetHandler and must call
    stopCop(ne) first.

    @param ne NetEvent to be released.
    @return ne->nh set to nullptr.
   */
  void stopIO(NetEvent *ne);

  /**
    Start to handle active timeout and inactivity timeout on a NetEvent.
    Put the ne into open_list. All NetEvents in the open_list is checked for
    timeout by InactivityCop. Only be called when holding the mutex of this
    NetHandler and must call startIO(ne) first.

    @param ne NetEvent to be managed by InactivityCop
   */
  void startCop(NetEvent *ne);
  /**
    Stop to handle active timeout and inactivity on a NetEvent.
    Remove the ne from open_list and cop_list.
    Also remove the ne from keep_alive_queue and active_queue if its context is
    IN. Only be called when holding the mutex of this NetHandler.

    @param ne NetEvent to be released.
   */
  void stopCop(NetEvent *ne);

  // Signal the epoll_wait to terminate.
  void signalActivity() override;

  /**
    Release a ne and free it.

    @param ne NetEvent to be detached.
   */
  void free_netevent(NetEvent *ne);

  NetHandler();

  inline static DbgCtl dbg_ctl_socket{"socket"};
  inline static DbgCtl dbg_ctl_iocore_net{"iocore_net"};

private:
  void _close_ne(NetEvent *ne, ink_hrtime now, int &handle_event, int &closed, int &total_idle_time, int &total_idle_count);

  /// Static method used as the callback for runtime configuration updates.
  static int update_nethandler_config(const char *name, RecDataT, RecData data, void *);
};
