/** @file

  Internal SDK stuff

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
#if !defined(__GNUC__)
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/net/Net.h"
#else
#include "../../src/iocore/eventsystem/P_EventSystem.h"
#include "../../src/iocore/net/P_Net.h"
#endif

namespace tsapi
{
namespace c
{

  enum INKContInternalMagic_t {
    INKCONT_INTERN_MAGIC_ALIVE = 0x00009631,
    INKCONT_INTERN_MAGIC_DEAD  = 0xDEAD9631,
  };

  class INKContInternal : public DummyVConnection
  {
  public:
    INKContInternal();
    INKContInternal(TSEventFunc funcp, TSMutex mutexp);

    void init(TSEventFunc funcp, TSMutex mutexp, void *context = 0);
    virtual void destroy();

    void handle_event_count(int event);
    int handle_event(int event, void *edata);

  protected:
    virtual void clear();
    virtual void free();

  public:
    void *mdata;
    TSEventFunc m_event_func;
    int m_event_count;
    int m_closed;
    int m_deletable;
    int m_deleted;
    void *m_context;
    // INKqa07670: Nokia memory leak bug fix
    INKContInternalMagic_t m_free_magic;
  };

  class INKVConnInternal : public INKContInternal
  {
  public:
    INKVConnInternal();
    INKVConnInternal(TSEventFunc funcp, TSMutex mutexp);

    void destroy() override;

    VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;

    VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;

    void do_io_transform(VConnection *vc);

    void do_io_close(int lerrno = -1) override;

    void do_io_shutdown(ShutdownHowTo_t howto) override;

    void reenable(VIO *vio) override;

    void retry(unsigned int delay);

    bool get_data(int id, void *data) override;
    bool set_data(int id, void *data) override;

  protected:
    void clear() override;
    void free() override;

  public:
    VIO m_read_vio;
    VIO m_write_vio;
    VConnection *m_output_vc;
  };

/****************************************************************
 *  IMPORTANT - READ ME
 * Any plugin using the IO Core must enter
 *   with a held mutex.  SDK 1.0, 1.1 & 2.0 did not
 *   have this restriction so we need to add a mutex
 *   to Plugin's Continuation if it tries to use the IOCore
 * Not only does the plugin have to have a mutex
 *   before entering the IO Core.  The mutex needs to be held.
 *   We now take out the mutex on each call to ensure it is
 *   held for the entire duration of the IOCore call
 ***************************************************************/
#define FORCE_PLUGIN_SCOPED_MUTEX(_c)         \
  sdk_assert(((INKContInternal *)_c)->mutex); \
  SCOPED_MUTEX_LOCK(ml, ((INKContInternal *)_c)->mutex, this_ethread());

  TSReturnCode sdk_sanity_check_mutex(TSMutex);
  TSReturnCode sdk_sanity_check_hostlookup_structure(TSHostLookupResult);
  TSReturnCode sdk_sanity_check_iocore_structure(void *);

  /* ----------------------------------------------------------------------
   *
   * Interfaces for Raft project
   *
   * ---------------------------------------------------------------------- */

  TSMutex TSMutexCreateInternal(void);
  int TSMutexCheck(TSMutex mutex);

  /* IOBuffer */
  int64_t TSIOBufferBlockDataSizeGet(TSIOBufferBlock blockp);
  void TSIOBufferBlockDestroy(TSIOBufferBlock blockp);
  using INKUDPPacket     = void *;
  using INKUDPacketQueue = void *;
  using INKUDPConn       = void *;
  /* ===== UDP Connections ===== */
  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  TSAction INKUDPBind(TSCont contp, unsigned int ip, int port);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  TSAction INKUDPSendTo(TSCont contp, INKUDPConn udp, unsigned int ip, int port, char *buf, int len);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  TSAction INKUDPRecvFrom(TSCont contp, INKUDPConn udp);

  /****************************************************************************
   *  Return file descriptor.
   *  contact: OXYGEN
   ****************************************************************************/
  int INKUDPConnFdGet(INKUDPConn udp);

  /* ===== UDP Packet ===== */
  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  INKUDPPacket INKUDPPacketCreate();

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  TSIOBufferBlock INKUDPPacketBufferBlockGet(INKUDPPacket packet);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  unsigned int INKUDPPacketFromAddressGet(INKUDPPacket packet);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  int INKUDPPacketFromPortGet(INKUDPPacket packet);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  INKUDPConn INKUDPPacketConnGet(INKUDPPacket packet);

  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  void INKUDPPacketDestroy(INKUDPPacket packet);

  /* ===== Packet Queue ===== */
  /****************************************************************************
   *  contact: OXYGEN
   ****************************************************************************/
  INKUDPPacket INKUDPPacketGet(INKUDPacketQueue queuep);

} // end namespace c
} // end namespace tsapi

using namespace ::tsapi::c;
