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

/****************************************************************************

   MuxVC.h

   Description:


 ****************************************************************************/

#ifndef _MUX_VC_H_
#define _MUX_VC_H_

#include "Net.h"

class MuxVC;
class MuxGetCont;
class MuxProcessor;
class MuxPagesHandler;

#define MUX_EVENT_OPEN        2040
#define MUX_EVENT_OPEN_FAILED 2041

enum
{
  MUX_VC_CLIENT_MAGIC_ALIVE = 0xdeffc0ff,
  MUX_VC_CLIENT_MAGIC_DEAD = 0xdeadc0ff
};

enum
{
  INKMUX_PROTO_VERSION_UNKNOWN = 0,
  INKMUX_PROTO_VERSION_0_1 = 1
};

enum
{
  INKMUX_MSG_OPEN_CHANNEL = 1,
  INKMUX_MSG_CLOSE_CHANNEL = 2,
  INKMUX_MSG_SHUTDOWN_WRITE = 3,
  INKMUX_MSG_NORMAL_DATA = 4,
  INKMUX_MSG_OOB_DATA = 5,
  INKMUX_MSG_CHANNEL_RESET = 6,
  INKMUX_MSG_FLOW_CONTROL_START = 7,
  INKMUX_MSG_FLOW_CONTROL_STOP = 8
};


#define MUX_OCLOSE_CHANNEL_EVENT          1
#define MUX_OCLOSE_WRITE_EVENT            (1 << 1)
#define MUX_OCLOSE_NEED_READ_NOTIFY       (1 << 2)
#define MUX_OCLOSE_NEED_WRITE_NOTIFY      (1 << 3)

#define MUX_OCLOSE_INBOUND_MASK      (MUX_OCLOSE_CHANNEL_EVENT | MUX_OCLOSE_WRITE_EVENT)
#define MUX_OCLOSE_OUTBOUND_MASK     (MUX_OCLOSE_CHANNEL_EVENT)

#define MUX_WRITE_SHUTDOWN                1
#define MUX_WRITE_SHUTUDOWN_SEND_MSG      (1 << 1)

struct MuxMessage
{
  uint8_t version;
  uint8_t msg_type;
  uint16_t msg_len;
  int32_t client_id;
};

struct MuxClientState
{
  MuxClientState();
  VIO vio;
  int shutdown;
  volatile int enabled;
  int flow_stopped;             // flow control flag
};

class MuxClientVC:public NetVConnection
{
  friend class MuxVC;
  friend class MuxPagesHandler;
public:
    MuxClientVC();
   ~MuxClientVC();

  void init(MuxVC * mvc, int32_t id);
  void kill();

  virtual VIO *do_io_read(Continuation * c = NULL, int64_t nbytes = INT64_MAX, MIOBuffer * buf = 0);

  virtual VIO *do_io_write(Continuation * c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader * buf = 0, bool owner = false);

  virtual bool is_over_ssl()
  {
    return (false);
  }

  virtual void do_io_close(int lerrno = -1);
  virtual void do_io_shutdown(ShutdownHowTo_t howto);

  // Reenable a given vio.  The public interface is through VIO::reenable
  virtual void reenable(VIO * vio);
  virtual void reenable_re(VIO * vio);

  virtual void boost();

  // Timeouts
  void set_active_timeout(ink_hrtime timeout_in);
  void set_inactivity_timeout(ink_hrtime timeout_in);
  void cancel_active_timeout();
  void cancel_inactivity_timeout();
  ink_hrtime get_active_timeout();
  ink_hrtime get_inactivity_timeout();

  // Pure virutal functions we need to compile
  SOCKET get_socket();
  const struct sockaddr_in &get_local_addr();
  const struct sockaddr_in &get_remote_addr();
  unsigned int get_local_ip();
  int get_local_port();
  unsigned int get_remote_ip();
  int get_remote_port();

  int main_handler(int event, void *data);

  Link<MuxClientVC> link;
  int32_t id;
  uint32_t magic;

private:

  void setup_retry_event(int ms);
  void update_inactive_timeout();
  void process_retry_event();
  void process_timeout(int event_to_send);

  void process_read_state();
  int process_byte_bank();
  int process_write();
  void process_channel_close_for_read();
  void process_channel_close_for_write();
  int send_write_shutdown_message();

  bool closed;
  uint32_t other_side_closed;

  int reentrancy_count;
  bool need_boost;

  MuxVC *mux_vc;

  MuxClientState read_state;
  MuxClientState write_state;

  // The byte bank is used for overflow bytes and is
  //   under control of the MuxVC's lock
  MIOBuffer *read_byte_bank;
  IOBufferReader *byte_bank_reader;

  ink_hrtime active_timeout;
  ink_hrtime inactive_timeout;

  Event *active_event;
  Event *inactive_event;

  // Retry event is used to retry when we can't both
  //  state machine's lock and the MuxVC's VC's lock
  // It's protected under the user of the VC's lock
  //    (stored in the VIOs in the read & write state)
  //
  Event *retry_event;
};

enum
{
  MUX_VC_MAGIC_ALIVE = 0xdeffb0ff,
  MUX_VC_MAGIC_DEAD = 0xdeadb0ff
};

enum MuxReadMsgState_t
{
  MUX_READ_MSG_HEADER,
  MUX_READ_MSG_BODY
};

enum MuxConnectState
{
  MUX_NOT_CONNECTED = 0,
  MUX_NET_CONNECT_ISSUED = 1,
  MUX_WAIT_FOR_READY = 2,
  MUX_CONNECTED_ACTIVE = 3,
  MUX_CONNECT_FAILED = 4,
  MUX_CONNECTION_DROPPED = 5,
  MUX_CONNECTED_IDLE = 6,
  MUX_CONNECTED_TEARDOWN = 7
};

class MuxVC:public Continuation
{
  friend class MuxClientVC;
  friend class MuxGetCont;
  friend class MuxProcessor;
  friend class MuxPagesHandler;
public:
    MuxVC();
   ~MuxVC();

  void init();
  void init_from_accept(NetVConnection * nvc, Continuation * acceptc);
  void kill();

  MuxClientVC *new_client(int32_t id = 0);
  void remove_client(MuxClientVC * client_vc);

  int state_handle_mux(int event, void *data);
  int state_handle_mux_down(int event, void *data);
  int state_handle_connect(int event, void *data);
  int state_wait_for_ready(int event, void *data);
  int state_handle_kill(int event, void *data);
  int state_idle(int event, void *data);
  int state_remove_from_list(int event, void *data);
  int state_teardown(int event, void *data);

  bool write_high_water();

  // Establishes underlying TCP session
  Action *do_connect(Continuation * c, unsigned int ip, int port);

  // Sets accept cont for muxed sessions
  Action *set_mux_accept(Continuation * c);

  void process_clients();
  void setup_process_event(int ms);
  unsigned int get_remote_ip();
  unsigned int get_remote_port();

  bool on_list(MuxClientVC * c);        // debugging function

    Link<MuxVC> link;
private:

  void init_buffers();
  void init_io();

  void setup_connect_check();
  int state_send_init_response(int event, void *data);

  void process_read_data();
  void process_read_msg_body();
  void reset_read_msg_state();

  MuxClientVC *find_client(int32_t client_id);
  void process_control_message();
  void process_channel_open();
  void process_channel_close(MuxClientVC * client);
  void process_channel_inbound_shutdown(MuxClientVC * client);
  int enqueue_control_message(int msg_id, int32_t cid, int data_size = 0);
  void cleanup_on_error();
  int try_processor_list_remove();

  uint32_t magic;
  int32_t id;
  int32_t reentrancy_count;
  bool terminate_vc;
  bool on_mux_list;
  bool clients_notified_of_error;
  Event *process_event;

  NetVConnection *net_vc;
  VIO *read_vio;
  VIO *write_vio;

  // Vars for preventing overflow on the outbound channel
  uint64_t write_bytes_added;
  bool writes_blocked;

  Action *net_connect_action;
  Action return_connect_action;
  MuxConnectState connect_state;
  Event *retry_event;

  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  IOBufferReader *read_buffer_reader;

  MuxReadMsgState_t read_msg_state;
  int read_msg_size;
  int read_msg_ndone;
  MuxMessage current_msg_hdr;
  bool discard_read_data;

  Action return_accept_action;

  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;

  int next_client_id;
  int num_clients;
    DLL<MuxClientVC> active_clients;
};

class MuxAcceptor:public Continuation
{
public:
  MuxAcceptor();
  ~MuxAcceptor();
  void init(int port, Continuation * c);
  int accept_handler(int event, void *data);
private:
    Action * accept_action;
  Continuation *call_cont;
};

class MuxGetCont:public Continuation
{
  friend class MuxGetAction;
public:
    MuxGetCont();
   ~MuxGetCont();
  Action *init_for_new_mux(Continuation * c, unsigned int ip, int port);
  Action *init_for_lock_miss(Continuation * c, unsigned int ip, int port);
  int new_mux_handler(int event, void *data);
  int lock_miss_handler(int event, void *data);
private:
    Action return_action;
  Action *mux_action;
  MuxVC *mux_vc;
  Event *retry_event;
  unsigned int ip;
  int port;
};

class HttpAccept;

enum MuxFindResult_t
{
  MUX_FIND_FOUND,
  MUX_FIND_NOT_FOUND,
  MUX_FIND_RETRY
};

class MuxProcessor:public Processor
{
  friend class MuxGetCont;
  friend class MuxVC;
  friend class MuxPagesHandler;
public:
    MuxProcessor();
   ~MuxProcessor();
  int start();
  Action *get_mux_re(Continuation * c, unsigned int ip, int port = 0);
private:
    MuxFindResult_t find_mux_internal(Continuation * c, unsigned int ip, int port);

    Ptr<ProxyMutex> list_mutex;
    DLL<MuxVC> mux_list;
};

extern MuxProcessor muxProcessor;

#endif
