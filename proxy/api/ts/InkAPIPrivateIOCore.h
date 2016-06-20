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

#ifndef __INK_API_PRIVATE_IOCORE_H__
#define __INK_API_PRIVATE_IOCORE_H__
#include "ts.h"
#if !defined(__GNUC__)
#include "I_EventSystem.h"
#include "I_Cache.h"
#include "I_Net.h"
#else
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "P_Net.h"
#endif

enum INKContInternalMagic_t {
  INKCONT_INTERN_MAGIC_ALIVE = 0x00009631,
  INKCONT_INTERN_MAGIC_DEAD  = 0xDEAD9631,
};

class INKContInternal : public DummyVConnection
{
public:
  INKContInternal();
  INKContInternal(TSEventFunc funcp, TSMutex mutexp);

  void init(TSEventFunc funcp, TSMutex mutexp);
  virtual void destroy();

  void handle_event_count(int event);
  int handle_event(int event, void *edata);

public:
  void *mdata;
  TSEventFunc m_event_func;
  volatile int m_event_count;
  volatile int m_closed;
  int m_deletable;
  int m_deleted;
  // INKqa07670: Nokia memory leak bug fix
  INKContInternalMagic_t m_free_magic;
};

class INKVConnInternal : public INKContInternal
{
public:
  INKVConnInternal();
  INKVConnInternal(TSEventFunc funcp, TSMutex mutexp);

  void init(TSEventFunc funcp, TSMutex mutexp);
  virtual void destroy();

  int handle_event(int event, void *edata);

  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);

  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false);

  void do_io_transform(VConnection *vc);

  void do_io_close(int lerrno = -1);

  void do_io_shutdown(ShutdownHowTo_t howto);

  void reenable(VIO *vio);

  void retry(unsigned int delay);

  bool get_data(int id, void *data);
  bool set_data(int id, void *data);

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
 *   to Plugin's Continuation if it trys to use the IOCore
 * Not only does the plugin have to have a mutex
 *   before entering the IO Core.  The mutex needs to be held.
 *   We now take out the mutex on each call to ensure it is
 *   held for the entire duration of the IOCore call
 ***************************************************************/
#define FORCE_PLUGIN_SCOPED_MUTEX(_c)         \
  sdk_assert(((INKContInternal *)_c)->mutex); \
  SCOPED_MUTEX_LOCK(ml, ((INKContInternal *)_c)->mutex, this_ethread());

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

TSReturnCode sdk_sanity_check_mutex(TSMutex);
TSReturnCode sdk_sanity_check_hostlookup_structure(TSHostLookupResult);
TSReturnCode sdk_sanity_check_iocore_structure(void *);

/* ----------------------------------------------------------------------
 *
 * Interfaces for Raft project
 *
 * ---------------------------------------------------------------------- */

tsapi TSMutex TSMutexCreateInternal(void);
tsapi int TSMutexCheck(TSMutex mutex);

/* IOBuffer */
tsapi void TSIOBufferReaderCopy(TSIOBufferReader readerp, const void *buf, int64_t length);
tsapi int64_t TSIOBufferBlockDataSizeGet(TSIOBufferBlock blockp);
tsapi void TSIOBufferBlockDestroy(TSIOBufferBlock blockp);
typedef void *INKUDPPacket;
typedef void *INKUDPacketQueue;
typedef void *INKUDPConn;
/* ===== UDP Connections ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi TSAction INKUDPBind(TSCont contp, unsigned int ip, int port);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi TSAction INKUDPSendTo(TSCont contp, INKUDPConn udp, unsigned int ip, int port, char *buf, int len);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi TSAction INKUDPRecvFrom(TSCont contp, INKUDPConn udp);

/****************************************************************************
 *  Return file descriptor.
 *  contact: OXYGEN
 ****************************************************************************/
tsapi int INKUDPConnFdGet(INKUDPConn udp);

/* ===== UDP Packet ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi INKUDPPacket INKUDPPacketCreate();

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi TSIOBufferBlock INKUDPPacketBufferBlockGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi unsigned int INKUDPPacketFromAddressGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi int INKUDPPacketFromPortGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi INKUDPConn INKUDPPacketConnGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi void INKUDPPacketDestroy(INKUDPPacket packet);

/* ===== Packet Queue ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
tsapi INKUDPPacket INKUDPPacketGet(INKUDPacketQueue queuep);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __INK_API_PRIVATE_IOCORE_H__ */
