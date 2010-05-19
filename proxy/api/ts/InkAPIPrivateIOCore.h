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

#include <string>
#include <vector>
#include "StatAPITypes.h"

enum INKContInternalMagic_t
{
  INKCONT_INTERN_MAGIC_ALIVE = 0x00009631,
  INKCONT_INTERN_MAGIC_DEAD = 0xDEAD9631
};

class INKContInternal:public DummyVConnection
{
public:
  INKContInternal();
  INKContInternal(INKEventFunc funcp, INKMutex mutexp);

  void init(INKEventFunc funcp, INKMutex mutexp);
  virtual void destroy();

  void handle_event_count(int event);
  int handle_event(int event, void *edata);
  void setName(const char *name);
  const char *getName();
  void statCallsMade(INKHttpHookID hook_id);
  bool isStatsEnabled() { return stats_enabled; }

public:
  void *mdata;
  INKEventFunc m_event_func;
  volatile int m_event_count;
  volatile int m_closed;
  int m_deletable;
  int m_deleted;
  //INKqa07670: Nokia memory leak bug fix
  INKContInternalMagic_t m_free_magic;

  std::string cont_name;
  std::vector<HistogramStats> cont_time_stats;
  std::vector<uint32_t> cont_calls;
  bool stats_enabled;

};


enum INKApiDataType
{
  INK_API_DATA_READ_VIO = VCONNECTION_API_DATA_BASE,
  INK_API_DATA_WRITE_VIO,
  INK_API_DATA_OUTPUT_VC,
  INK_API_DATA_CLOSED
};


class INKVConnInternal:public INKContInternal
{
public:
  INKVConnInternal();
  INKVConnInternal(INKEventFunc funcp, INKMutex mutexp);

  void init(INKEventFunc funcp, INKMutex mutexp);
  virtual void destroy();

  int handle_event(int event, void *edata);

  VIO *do_io_read(Continuation *c, int64 nbytes, MIOBuffer *buf);

  VIO *do_io_write(Continuation *c, int64 nbytes, IOBufferReader *buf, bool owner = false);

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

//
// FORCE_PLUGIN_MUTEX -- define 'UNSAFE_FORCE_MUTEX' if you
// do *not* want the locking macro to be thread safe.
// Otherwise, access during 'null-mutex' case will be serialized
// in a locking manner (too bad for the net threads).
//


#define UNSAFE_FORCE_MUTEX

#ifdef UNSAFE_FORCE_MUTEX
#define LOCK_MONGO_MUTEX
#define UNLOCK_MONGO_MUTEX
#define MUX_WARNING(p) \
INKDebug ("sdk","(SDK) null mutex detected in critical region (mutex created)"); \
INKDebug ("sdk","(SDK) please create continuation [%p] with mutex", (p));
#else
static ink_mutex big_mux;

#define MUX_WARNING(p) 1
#define LOCK_MONGO_MUTEX   ink_mutex_acquire (&big_mux)
#define UNLOCK_MONGO_MUTEX ink_mutex_release (&big_mux)
#endif

#define FORCE_PLUGIN_MUTEX(_c) \
  MutexLock ml; \
  LOCK_MONGO_MUTEX; \
  if (( (INKContInternal*)_c)->mutex == NULL) { \
      ( (INKContInternal*)_c)->mutex = new_ProxyMutex(); \
      UNLOCK_MONGO_MUTEX; \
	  MUX_WARNING(_c); \
      MUTEX_SET_AND_TAKE_LOCK(ml, ((INKContInternal*)_c)->mutex, this_ethread()); \
  } else { \
      UNLOCK_MONGO_MUTEX; \
      MUTEX_SET_AND_TAKE_LOCK(ml, ((INKContInternal*)_c)->mutex, this_ethread()); \
  }

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

  INKReturnCode sdk_sanity_check_mutex(INKMutex);
  INKReturnCode sdk_sanity_check_hostlookup_structure(INKHostLookupResult);
  INKReturnCode sdk_sanity_check_iocore_structure(void *);

/* ----------------------------------------------------------------------
 *
 * Interfaces for Raft project
 *
 * ---------------------------------------------------------------------- */

  inkapi INKMutex INKMutexCreateInternal(void);
  inkapi int INKMutexCheck(INKMutex mutex);


/* IOBuffer */
  inkapi void INKIOBufferReaderCopy(INKIOBufferReader, char *, int);
  inkapi int INKIOBufferBlockDataSizeGet(INKIOBufferBlock blockp);
  inkapi INKReturnCode INKIOBufferBlockDestroy(INKIOBufferBlock blockp);


  typedef void *INKUDPPacket;
  typedef void *INKUDPacketQueue;
  typedef void *INKUDPConn;
/* ===== UDP Connections ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKAction INKUDPBind(INKCont contp, unsigned int ip, int port);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKAction INKUDPSendTo(INKCont contp, INKUDPConn udp, unsigned int ip, int port, char *buf, int len);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKAction INKUDPRecvFrom(INKCont contp, INKUDPConn udp);

/****************************************************************************
 *  Return file descriptor.
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi int INKUDPConnFdGet(INKUDPConn udp);

/* ===== UDP Packet ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKUDPPacket INKUDPPacketCreate();

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKIOBufferBlock INKUDPPacketBufferBlockGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi unsigned int INKUDPPacketFromAddressGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi int INKUDPPacketFromPortGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKUDPConn INKUDPPacketConnGet(INKUDPPacket packet);

/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi void INKUDPPacketDestroy(INKUDPPacket packet);

/* ===== Packet Queue ===== */
/****************************************************************************
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi INKUDPPacket INKUDPPacketGet(INKUDPacketQueue queuep);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* __INK_API_PRIVATE_IOCORE_H__ */
