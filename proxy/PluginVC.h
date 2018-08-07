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

   PluginVC.h

   Description: Allows bi-directional transfer for data from one
      continuation to another via a mechanism that impersonates a
      NetVC.  Should implement all external attributes of NetVConnections.
      [See PluginVC.cc for further comments]


 ****************************************************************************/

#pragma once

#include "Plugin.h"
#include "P_Net.h"
#include "ts/ink_atomic.h"

class PluginVCCore;

struct PluginVCState {
  PluginVCState();
  VIO vio;
  bool shutdown;
};

inline PluginVCState::PluginVCState() : vio(), shutdown(false) {}

enum PluginVC_t {
  PLUGIN_VC_UNKNOWN,
  PLUGIN_VC_ACTIVE,
  PLUGIN_VC_PASSIVE,
};

// For the id in set_data/get_data
enum {
  PLUGIN_VC_DATA_LOCAL = TS_API_DATA_LAST,
  PLUGIN_VC_DATA_REMOTE,
};

enum {
  PLUGIN_VC_MAGIC_ALIVE = 0xaabbccdd,
  PLUGIN_VC_MAGIC_DEAD  = 0xaabbdead,
};

class PluginVC : public NetVConnection, public PluginIdentity
{
  friend class PluginVCCore;

public:
  PluginVC(PluginVCCore *core_obj);
  ~PluginVC() override;

  VIO *do_io_read(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;

  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;

  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;

  // Reenable a given vio.  The public interface is through VIO::reenable
  void reenable(VIO *vio) override;
  void reenable_re(VIO *vio) override;

  // Timeouts
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_active_timeout() override;
  void cancel_inactivity_timeout() override;
  void add_to_keep_alive_queue() override;
  void remove_from_keep_alive_queue() override;
  bool add_to_active_queue() override;
  ink_hrtime get_active_timeout() override;
  ink_hrtime get_inactivity_timeout() override;

  // Pure virutal functions we need to compile
  SOCKET get_socket() override;
  void set_local_addr() override;
  void set_remote_addr() override;
  int set_tcp_init_cwnd(int init_cwnd) override;
  int set_tcp_congestion_control(int) override;

  void apply_options() override;

  bool get_data(int id, void *data) override;
  bool set_data(int id, void *data) override;

  virtual PluginVC *
  get_other_side()
  {
    return other_side;
  }

  //@{ @name Plugin identity.
  /// Override for @c PluginIdentity.
  const char *
  getPluginTag() const override
  {
    return plugin_tag;
  }
  /// Override for @c PluginIdentity.
  int64_t
  getPluginId() const override
  {
    return plugin_id;
  }

  /// Setter for plugin tag.
  virtual void
  setPluginTag(const char *tag)
  {
    plugin_tag = tag;
  }
  /// Setter for plugin id.
  virtual void
  setPluginId(int64_t id)
  {
    plugin_id = id;
  }
  //@}

  int main_handler(int event, void *data);

private:
  void process_read_side(bool);
  void process_write_side(bool);
  void process_close();
  void process_timeout(Event **e, int event_to_send);

  // Clear the Event pointer pointed to by e
  // Cancel the action first if it is a periodic event
  void clear_event(Event **e);

  void setup_event_cb(ink_hrtime in, Event **e_ptr);

  void update_inactive_time();
  int64_t transfer_bytes(MIOBuffer *transfer_to, IOBufferReader *transfer_from, int64_t act_on);

  uint32_t magic;
  PluginVC_t vc_type;
  PluginVCCore *core_obj;

  PluginVC *other_side;

  PluginVCState read_state;
  PluginVCState write_state;

  bool need_read_process;
  bool need_write_process;

  bool closed;
  Event *sm_lock_retry_event;
  Event *core_lock_retry_event;

  bool deletable;
  int reentrancy_count;

  ink_hrtime active_timeout;
  Event *active_event;

  ink_hrtime inactive_timeout;
  ink_hrtime inactive_timeout_at;
  Event *inactive_event;

  const char *plugin_tag;
  int64_t plugin_id;
};

class PluginVCCore : public Continuation
{
  friend class PluginVC;

public:
  PluginVCCore();
  ~PluginVCCore() override;

  // Allocate a PluginVCCore object, passing the continuation which
  // will receive NET_EVENT_ACCEPT to accept the new session.
  static PluginVCCore *alloc(Continuation *acceptor);

  int state_send_accept(int event, void *data);
  int state_send_accept_failed(int event, void *data);

  void attempt_delete();

  PluginVC *connect();
  Action *connect_re(Continuation *c);
  void kill_no_connect();

  /// Set the active address.
  void set_active_addr(in_addr_t ip, ///< IPv4 address in host order.
                       int port      ///< IP Port in host order.
  );
  /// Set the active address and port.
  void set_active_addr(sockaddr const *ip ///< Address and port used.
  );
  /// Set the passive address.
  void set_passive_addr(in_addr_t ip, ///< IPv4 address in host order.
                        int port      ///< IP port in host order.
  );
  /// Set the passive address.
  void set_passive_addr(sockaddr const *ip ///< Address and port.
  );

  void set_active_data(void *data);
  void set_passive_data(void *data);

  void set_transparent(bool passive_side, bool active_side);

  /// Set the plugin ID for the internal VCs.
  void set_plugin_id(int64_t id);
  /// Set the plugin tag for the internal VCs.
  void set_plugin_tag(const char *tag);

  // The active vc is handed to the initiator of
  //   connection.  The passive vc is handled to
  //   receiver of the connection
  PluginVC active_vc;
  PluginVC passive_vc;

private:
  void init();
  void destroy();

  Continuation *connect_to;
  bool connected;

  MIOBuffer *p_to_a_buffer;
  IOBufferReader *p_to_a_reader;

  MIOBuffer *a_to_p_buffer;
  IOBufferReader *a_to_p_reader;

  IpEndpoint passive_addr_struct;
  IpEndpoint active_addr_struct;

  void *passive_data;
  void *active_data;

  static int32_t nextid;
  unsigned id;
};

inline PluginVCCore::PluginVCCore()
  : active_vc(this),
    passive_vc(this),
    connect_to(nullptr),
    connected(false),
    p_to_a_buffer(nullptr),
    p_to_a_reader(nullptr),
    a_to_p_buffer(nullptr),
    a_to_p_reader(nullptr),
    passive_data(nullptr),
    active_data(nullptr),
    id(0)
{
  memset(&active_addr_struct, 0, sizeof active_addr_struct);
  memset(&passive_addr_struct, 0, sizeof passive_addr_struct);

  id = ink_atomic_increment(&nextid, 1);
}
