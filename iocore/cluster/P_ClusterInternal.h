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

  ClusterInternal.h
****************************************************************************/
#include "P_ClusterCache.h"

#ifndef _P_ClusterInternal_h
#define _P_ClusterInternal_h

/*************************************************************************/
// Compilation Options
/*************************************************************************/
#define CLUSTER_THREAD_STEALING 1
#define CLUSTER_TOMCAT 1
#define CLUSTER_STATS 1

#define ALIGN_DOUBLE(_p) ((((uintptr_t)(_p)) + 7) & ~7)
#define ALLOCA_DOUBLE(_sz) ALIGN_DOUBLE(alloca((_sz) + 8))

/*************************************************************************/
// Configuration Parameters
/*************************************************************************/
// Note: MAX_TCOUNT must be power of 2
#define MAX_TCOUNT 128
#define CONTROL_DATA (128 * 1024)
#define READ_BANK_BUF_SIZE DEFAULT_MAX_BUFFER_SIZE
#define READ_BANK_BUF_INDEX (DEFAULT_BUFFER_SIZES - 1)
#define ALLOC_DATA_MAGIC 0xA5 // 8 bits in size
#define READ_LOCK_SPIN_COUNT 1
#define WRITE_LOCK_SPIN_COUNT 1

// Unix specific optimizations
// #define CLUSTER_IMMEDIATE_NETIO       1

// (see ClusterHandler::mainClusterEvent)
// this is equivalent to a max of 0.7 seconds
#define CLUSTER_BUCKETS 64
#define CLUSTER_PERIOD HRTIME_MSECONDS(10)

// Per instance maximum time allotted to cluster thread
#define CLUSTER_MAX_RUN_TIME HRTIME_MSECONDS(100)
// Per instance maximum time allotted to thread stealing
#define CLUSTER_MAX_THREAD_STEAL_TIME HRTIME_MSECONDS(10)

// minimum number of channels to allocate
#define MIN_CHANNELS 4096
#define MAX_CHANNELS ((32 * 1024) - 1) // 15 bits in Descriptor

#define CLUSTER_CONTROL_CHANNEL 0
#define LAST_DEDICATED_CHANNEL 0

#define CLUSTER_PHASES 1

#define CLUSTER_INITIAL_PRIORITY CLUSTER_PHASES
// how often to retry connect to machines which are supposed to be in the
// cluster
#define CLUSTER_BUMP_LENGTH 1
#define CLUSTER_MEMBER_DELAY HRTIME_SECONDS(1)
// How long to leave an unconnected ClusterVConnection waiting
// Note: assumes (CLUSTER_CONNECT_TIMEOUT == 2 * CACHE_CLUSTER_TIMEOUT)
#ifdef CLUSTER_TEST_DEBUG
#define CLUSTER_CONNECT_TIMEOUT HRTIME_SECONDS(65536)
#else
#define CLUSTER_CONNECT_TIMEOUT HRTIME_SECONDS(10)
#endif
#define CLUSTER_CONNECT_RETRY HRTIME_MSECONDS(20)
#define CLUSTER_RETRY HRTIME_MSECONDS(10)
#define CLUSTER_DELAY_BETWEEN_WRITES HRTIME_MSECONDS(10)

// Force close on cluster channel if no activity detected in this interval
#ifdef CLUSTER_TEST_DEBUG
#define CLUSTER_CHANNEL_INACTIVITY_TIMEOUT (65536 * HRTIME_SECONDS(60))
#else
#define CLUSTER_CHANNEL_INACTIVITY_TIMEOUT (10 * HRTIME_SECONDS(60))
#endif

// Defines for work deferred to ET_NET threads
#define COMPLETION_CALLBACK_PERIOD HRTIME_MSECONDS(10)
#define MAX_COMPLETION_CALLBACK_EVENTS 16

// ClusterHandler::mainClusterEvent() thread active state
#define CLUSTER_ACTIVE 1
#define CLUSTER_NOT_ACTIVE 0

// defines for ClusterHandler::remote_closed
#define FORCE_CLOSE_ON_OPEN_CHANNEL -2

// defines for machine_config_change()
#define MACHINE_CONFIG 0
#define CLUSTER_CONFIG 1

// Debug interface category definitions
#define CL_NOTE "cluster_note"
#define CL_WARN "cluster_warn"
#define CL_PROTO "cluster_proto"
#define CL_TRACE "cluster_trace"

/*************************************************************************/
// Constants
/*************************************************************************/
#define MAX_FAST_CONTROL_MESSAGE 504                   // 512 - 4 (cluster func #) - 4 align
#define SMALL_CONTROL_MESSAGE MAX_FAST_CONTROL_MESSAGE // copied instead
                                                       //  of vectored
#define WRITE_MESSAGE_ALREADY_BUILT -1

#define MAGIC_COUNT(_x)                                                                                              \
  (0xBADBAD ^ ~(uint32_t)_x.msg.count ^ ~(uint32_t)_x.msg.descriptor_cksum ^ ~(uint32_t)_x.msg.control_bytes_cksum ^ \
   ~(uint32_t)_x.msg.unused ^ ~((uint32_t)_x.msg.control_bytes << 16) ^ _x.sequence_number)

#define DOUBLE_ALIGN(_x) ((((uintptr_t)_x) + 7) & ~7)

/*************************************************************************/
// Testing Defines
/*************************************************************************/
#define MISS_TEST 0
#define TEST_PARTIAL_WRITES 0
#define TEST_PARTIAL_READS 0
#define TEST_TIMING 0
#define TEST_READ_LOCKS_MISSED 0
#define TEST_WRITE_LOCKS_MISSED 0
#define TEST_ENTER_EXIT 0
#define TEST_ENTER_EXIT 0

//
// Timing testing
//
#if TEST_TIMING
#define TTTEST(_x) fprintf(stderr, _x " at: %u\n", ((unsigned int)(ink_get_hrtime() / HRTIME_MSECOND)) % 1000)
#define TTEST(_x) fprintf(stderr, _x " for: %d at: %u\n", vc->channel, ((unsigned int)(ink_get_hrtime() / HRTIME_MSECOND)) % 1000)
#define TIMEOUT_TESTS(_s, _d)                                                \
  if (*(int *)_d == 8)                                                       \
    fprintf(stderr, _s " lookup  %d\n", *(int *)(_d + 20));                  \
  else if (*(int *)_d == 10)                                                 \
    fprintf(stderr, _s " op %d %d\n", *(int *)(_d + 36), *(int *)(_d + 40)); \
  else if (*(int *)_d == 11)                                                 \
  fprintf(stderr, _s " rop %d %d\n", *(int *)(_d + 4), *(int *)(_d + 8))
#else
#define TTTEST(_x)
#define TTEST(_x)
#define TIMEOUT_TESTS(_x, _y)
#endif

#if (TEST_READ_LOCKS_MISSED || TEST_WRITE_LOCKS_MISSED)
static unsigned int test_cluster_locks_missed = 0;
static test_cluster_lock_might_fail()
{
  return (!(rand_r(&test_cluster_locks_missed) % 13));
}
#endif
#if TEST_READ_LOCKS_MISSED
#define TEST_READ_LOCK_MIGHT_FAIL test_cluster_lock_might_fail()
#else
#define TEST_READ_LOCK_MIGHT_FAIL false
#endif
#if TEST_WRITE_LOCKS_MISSED
#define TEST_WRITE_LOCK_MIGHT_FAIL test_cluster_lock_might_fail()
#else
#define TEST_WRITE_LOCK_MIGHT_FAIL false
#endif

#if TEST_ENTER_EXIT
struct enter_exit_class {
  int *outv;
  enter_exit_class(int *in, int *out) : outv(out) { (*in)++; }
  ~enter_exit_class() { (*outv)++; }
};

#define enter_exit(_x, _y) enter_exit_class a(_x, _y)
#else
#define enter_exit(_x, _y)
#endif

#define DOT_SEPARATED(_x) \
  ((unsigned char *)&(_x))[0], ((unsigned char *)&(_x))[1], ((unsigned char *)&(_x))[2], ((unsigned char *)&(_x))[3]

//
// RPC message for CLOSE_CHANNEL_CLUSTER_FUNCTION
//
struct CloseMessage : public ClusterMessageHeader {
  uint32_t channel;
  int32_t status;
  int32_t lerrno;
  uint32_t sequence_number;

  enum {
    MIN_VERSION                = 1,
    MAX_VERSION                = 1,
    CLOSE_CHAN_MESSAGE_VERSION = MAX_VERSION,
  };

  CloseMessage(uint16_t vers = CLOSE_CHAN_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), channel(0), status(0), lerrno(0), sequence_number(0)
  {
  }
  ////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CLOSE_CHAN_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return sizeof(CloseMessage);
  }
  void
  init(uint16_t vers = CLOSE_CHAN_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&channel);
      ats_swap32((uint32_t *)&status);
      ats_swap32((uint32_t *)&lerrno);
      ats_swap32(&sequence_number);
    }
  }
  ////////////////////////////////////////////////////////////////////////////
};

//
// RPC message for MACHINE_LIST_CLUSTER_FUNCTION
//
struct MachineListMessage : public ClusterMessageHeader {
  uint32_t n_ip;                     // Valid entries in ip[]
  uint32_t ip[CLUSTER_MAX_MACHINES]; // variable length data

  enum {
    MIN_VERSION                  = 1,
    MAX_VERSION                  = 1,
    MACHINE_LIST_MESSAGE_VERSION = MAX_VERSION,
  };

  MachineListMessage() : ClusterMessageHeader(MACHINE_LIST_MESSAGE_VERSION), n_ip(0) { memset(ip, 0, sizeof(ip)); }
  ////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return MACHINE_LIST_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return sizeof(ClusterMessageHeader);
  }
  void
  init(uint16_t vers = MACHINE_LIST_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    ats_swap32(&n_ip);
  }
  ////////////////////////////////////////////////////////////////////////////
};

//
// RPC message for SET_CHANNEL_DATA_CLUSTER_FUNCTION
//
struct SetChanDataMessage : public ClusterMessageHeader {
  uint32_t channel;
  uint32_t sequence_number;
  uint32_t data_type; // enum CacheDataType
  char data[4];

  enum {
    MIN_VERSION                      = 1,
    MAX_VERSION                      = 1,
    SET_CHANNEL_DATA_MESSAGE_VERSION = MAX_VERSION,
  };

  SetChanDataMessage(uint16_t vers = SET_CHANNEL_DATA_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), channel(0), sequence_number(0), data_type(0)
  {
    memset(data, 0, sizeof(data));
  }
  ////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return SET_CHANNEL_DATA_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    SetChanDataMessage *p = 0;
    return (int)DOUBLE_ALIGN((int64_t)((char *)&p->data[0] - (char *)p));
  }
  void
  init(uint16_t vers = SET_CHANNEL_DATA_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&channel);
      ats_swap32(&sequence_number);
      ats_swap32(&data_type);
    }
  }
  ////////////////////////////////////////////////////////////////////////////
};

//
// RPC message for SET_CHANNEL_PIN_CLUSTER_FUNCTION
//
struct SetChanPinMessage : public ClusterMessageHeader {
  uint32_t channel;
  uint32_t sequence_number;
  uint32_t pin_time;

  enum {
    MIN_VERSION                     = 1,
    MAX_VERSION                     = 1,
    SET_CHANNEL_PIN_MESSAGE_VERSION = MAX_VERSION,
  };

  SetChanPinMessage(uint16_t vers = SET_CHANNEL_PIN_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), channel(0), sequence_number(0), pin_time(0)
  {
  }
  ////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return SET_CHANNEL_PIN_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)sizeof(SetChanPinMessage);
  }
  void
  init(uint16_t vers = SET_CHANNEL_PIN_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&channel);
      ats_swap32(&sequence_number);
      ats_swap32(&pin_time);
    }
  }
  ////////////////////////////////////////////////////////////////////////////
};

//
// RPC message for SET_CHANNEL_PRIORITY_CLUSTER_FUNCTION
//
struct SetChanPriorityMessage : public ClusterMessageHeader {
  uint32_t channel;
  uint32_t sequence_number;
  uint32_t disk_priority;

  enum {
    MIN_VERSION                          = 1,
    MAX_VERSION                          = 1,
    SET_CHANNEL_PRIORITY_MESSAGE_VERSION = MAX_VERSION,
  };

  SetChanPriorityMessage(uint16_t vers = SET_CHANNEL_PRIORITY_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), channel(0), sequence_number(0), disk_priority(0)
  {
  }
  ////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return SET_CHANNEL_PRIORITY_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)sizeof(SetChanPriorityMessage);
  }
  void
  init(uint16_t vers = SET_CHANNEL_PRIORITY_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&channel);
      ats_swap32(&sequence_number);
      ats_swap32(&disk_priority);
    }
  }
  ////////////////////////////////////////////////////////////////////////////
};

inline void
SetHighBit(int *val)
{
  *val |= (1 << ((sizeof(int) * 8) - 1));
}

inline void
ClearHighBit(int *val)
{
  *val &= ~(1 << ((sizeof(int) * 8) - 1));
}

inline int
IsHighBitSet(int *val)
{
  return (*val & (1 << ((sizeof(int) * 8) - 1)));
}

/////////////////////////////////////////////////////////////////
// ClusterAccept -- Handle cluster connect events from peer
//                  cluster nodes.
/////////////////////////////////////////////////////////////////
class ClusterAccept : public Continuation
{
public:
  ClusterAccept(int *, int, int);
  void Init();
  void ShutdownDelete();
  int ClusterAcceptEvent(int, void *);
  int ClusterAcceptMachine(NetVConnection *);

  ~ClusterAccept();

private:
  int *p_cluster_port;
  int socket_send_bufsize;
  int socket_recv_bufsize;
  int current_cluster_port;
  Action *accept_action;
  Event *periodic_event;
};

// VC++ 5.0 special
struct ClusterHandler;
typedef int (ClusterHandler::*ClusterContHandler)(int, void *);

struct OutgoingControl;
typedef int (OutgoingControl::*OutgoingCtrlHandler)(int, void *);

struct ClusterVConnection;
typedef int (ClusterVConnection::*ClusterVConnHandler)(int, void *);

// Library  declarations
extern void cluster_set_priority(ClusterHandler *, ClusterVConnState *, int);
extern void cluster_lower_priority(ClusterHandler *, ClusterVConnState *);
extern void cluster_raise_priority(ClusterHandler *, ClusterVConnState *);
extern void cluster_schedule(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_reschedule(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_reschedule_offset(ClusterHandler *, ClusterVConnection *, ClusterVConnState *, int);
extern void cluster_disable(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_update_priority(ClusterHandler *, ClusterVConnection *, ClusterVConnState *, int64_t, int64_t);
#define CLUSTER_BUMP_NO_REMOVE -1
extern void cluster_bump(ClusterHandler *, ClusterVConnectionBase *, ClusterVConnState *, int);

extern IOBufferBlock *clone_IOBufferBlockList(IOBufferBlock *, int, int, IOBufferBlock **);
extern IOBufferBlock *consume_IOBufferBlockList(IOBufferBlock *, int64_t);
extern int64_t bytes_IOBufferBlockList(IOBufferBlock *, int64_t);

// ClusterVConnection declarations
extern void clusterVCAllocator_free(ClusterVConnection *vc);
extern ClassAllocator<ClusterVConnection> clusterVCAllocator;
extern ClassAllocator<ByteBankDescriptor> byteBankAllocator;

// Cluster configuration declarations
extern int cluster_port;
// extern void * machine_config_change(void *, void *);
int machine_config_change(const char *, RecDataT, RecData, void *);
extern void do_machine_config_change(void *, const char *);

// Cluster API support functions
extern void clusterAPI_init();
extern void machine_online_APIcallout(int);
extern void machine_offline_APIcallout(int);
#endif /* _ClusterInternal_h */
