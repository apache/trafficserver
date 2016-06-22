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

  ClusterCacheInternal.h
****************************************************************************/

#ifndef __P_CLUSTERCACHEINTERNAL_H__
#define __P_CLUSTERCACHEINTERNAL_H__
#include "P_ClusterCache.h"
#include "I_OneWayTunnel.h"

//
// Compilation Options
//
#define CACHE_USE_OPEN_VIO 0 // EXPERIMENTAL: not fully tested
#define DO_REPLICATION 0     // EXPERIMENTAL: not fully tested

//
// Constants
//
#define META_DATA_FAST_ALLOC_LIMIT 1
#define CACHE_CLUSTER_TIMEOUT HRTIME_MSECONDS(5000)
#define CACHE_RETRY_PERIOD HRTIME_MSECONDS(10)
#define REMOTE_CONNECT_HASH (16 * 1024)

//
// Macros
//
#define FOLDHASH(_ip, _seq) (_seq % REMOTE_CONNECT_HASH)
#define ALIGN_DOUBLE(_p) ((((uintptr_t)(_p)) + 7) & ~7)
#define ALLOCA_DOUBLE(_sz) ALIGN_DOUBLE(alloca((_sz) + 8))

//
// Testing
//
#define TEST(_x)
//#define TEST(_x) _x

//#define TTEST(_x)
// fprintf(stderr, _x " at: %d\n",
//      ((unsigned int)(ink_get_hrtime()/HRTIME_MSECOND)) % 1000)
#define TTEST(_x)

//#define TIMEOUT_TEST(_x) _x
#define TIMEOUT_TEST(_x)

extern int cache_migrate_on_demand;
extern int ET_CLUSTER;
//
// Compile time options.
//
// Only one of PROBE_LOCAL_CACHE_FIRST or PROBE_LOCAL_CACHE_LAST
// should be set.  These indicate that the local cache should be
// probed at this point regardless of the dedicated location of the
// object.  Note, if the owning machine goes down the local machine
// will be probed anyway.
//
#define PROBE_LOCAL_CACHE_FIRST DO_REPLICATION
#define PROBE_LOCAL_CACHE_LAST false

//
// This continuation handles all cache cluster traffic, on both
// sides (state machine client and cache server)
//
struct CacheContinuation;
typedef int (CacheContinuation::*CacheContHandler)(int, void *);
struct CacheContinuation : public Continuation {
  enum {
    MagicNo = 0x92183123,
  };
  int magicno;
  void *callback_data;
  void *callback_data_2;
  INK_MD5 url_md5;
  Event *timeout;
  Action action;
  ClusterMachine *target_machine;
  int probe_depth;
  ClusterMachine *past_probes[CONFIGURATION_HISTORY_PROBE_DEPTH];
  ink_hrtime start_time;
  ClusterMachine *from;
  ClusterHandler *ch;
  VConnection *cache_vc;
  bool cache_read;
  int result;       // return event code
  int result_error; // error code associated with event
  ClusterVCToken token;
  unsigned int seq_number;
  uint16_t cfl_flags; // Request flags; see CFL_XXX defines
  CacheFragType frag_type;
  int nbytes;
  unsigned int target_ip;
  int request_opcode;
  bool request_purge;
  bool local_lookup_only;
  bool no_reply_message;
  bool request_timeout; // timeout occurred before
  //   op complete
  bool expect_cache_callback;

  // remove_and_delete() specific data
  bool use_deferred_callback;

  // open_read/write data

  time_t pin_in_cache;

  // setMsgBufferLen(), allocMsgBuffer() and freeMsgBuffer() data

  Ptr<IOBufferData> rw_buf_msg;
  int rw_buf_msg_len;

  // open data

  ClusterVConnection *read_cluster_vc;
  ClusterVConnection *write_cluster_vc;
  int cluster_vc_channel;
  ClusterVCToken open_local_token;

  // Readahead on open read specific data

  int caller_buf_freebytes; // remote bufsize for
  //  initial data
  VIO *readahead_vio;
  IOBufferReader *readahead_reader;
  Ptr<IOBufferBlock> readahead_data;
  bool have_all_data; // all object data in response

  CacheHTTPInfo cache_vc_info;
  OneWayTunnel *tunnel;
  Ptr<ProxyMutex> tunnel_mutex;
  CacheContinuation *tunnel_cont;
  bool tunnel_closed;
  Action *cache_action;
  Event *lookup_open_write_vc_event;

  // Incoming data generated from unmarshaling request/response ops

  Arena ic_arena;
  CacheHTTPHdr ic_request;
  CacheHTTPHdr ic_response;
  CacheLookupHttpConfig *ic_params;
  CacheHTTPInfo ic_old_info;
  CacheHTTPInfo ic_new_info;
  Ptr<IOBufferData> ic_hostname;
  int ic_hostname_len;

  // debugging
  int cache_op_ClusterFunction;

  int lookupEvent(int event, void *d);
  int probeLookupEvent(int event, void *d);
  int remoteOpEvent(int event, Event *e);
  int replyLookupEvent(int event, void *d);
  int replyOpEvent(int event, VConnection *vc);
  int handleReplyEvent(int event, Event *e);
  int callbackEvent(int event, Event *e);
  int setupVCdataRead(int event, VConnection *vc);
  int VCdataRead(int event, VIO *target_vio);
  int setupReadWriteVC(int, VConnection *);
  ClusterVConnection *lookupOpenWriteVC();
  int lookupOpenWriteVCEvent(int, Event *);
  int localVCsetupEvent(int event, ClusterVConnection *vc);
  void insert_cache_callback_user(ClusterVConnection *, int, void *);
  int insertCallbackEvent(int, Event *);
  void callback_user(int result, void *d);
  void defer_callback_result(int result, void *d);
  int callbackResultEvent(int event, Event *e);
  void setupReadBufTunnel(VConnection *, VConnection *);
  int tunnelClosedEvent(int event, void *);
  int remove_and_delete(int, Event *);

  inline void
  setMsgBufferLen(int l, IOBufferData *b = 0)
  {
    ink_assert(rw_buf_msg == 0);
    ink_assert(rw_buf_msg_len == 0);

    rw_buf_msg     = b;
    rw_buf_msg_len = l;
  }

  inline int
  getMsgBufferLen()
  {
    return rw_buf_msg_len;
  }

  inline void
  allocMsgBuffer()
  {
    ink_assert(rw_buf_msg == 0);
    ink_assert(rw_buf_msg_len);
    if (rw_buf_msg_len <= DEFAULT_MAX_BUFFER_SIZE) {
      rw_buf_msg = new_IOBufferData(buffer_size_to_index(rw_buf_msg_len, MAX_BUFFER_SIZE_INDEX));
    } else {
      rw_buf_msg = new_xmalloc_IOBufferData(ats_malloc(rw_buf_msg_len), rw_buf_msg_len);
    }
  }

  inline char *
  getMsgBuffer()
  {
    ink_assert(rw_buf_msg);
    return rw_buf_msg->data();
  }

  inline IOBufferData *
  getMsgBufferIOBData()
  {
    return rw_buf_msg;
  }

  inline void
  freeMsgBuffer()
  {
    if (rw_buf_msg) {
      rw_buf_msg     = 0;
      rw_buf_msg_len = 0;
    }
  }

  inline void
  free()
  {
    token.clear();

    if (cache_vc_info.valid()) {
      cache_vc_info.destroy();
    }
    // Deallocate unmarshaled data
    if (ic_params) {
      delete ic_params;
      ic_params = 0;
    }
    if (ic_request.valid()) {
      ic_request.clear();
    }
    if (ic_response.valid()) {
      ic_response.clear();
    }
    if (ic_old_info.valid()) {
      ic_old_info.destroy();
    }
    if (ic_new_info.valid()) {
      ic_new_info.destroy();
    }
    ic_arena.reset();
    freeMsgBuffer();

    tunnel_mutex   = 0;
    readahead_data = 0;
    ic_hostname    = 0;
  }

  CacheContinuation()
    : Continuation(NULL),
      magicno(MagicNo),
      callback_data(0),
      callback_data_2(0),
      timeout(0),
      target_machine(0),
      probe_depth(0),
      start_time(0),
      cache_read(false),
      result(0),
      result_error(0),
      seq_number(0),
      cfl_flags(0),
      frag_type(CACHE_FRAG_TYPE_NONE),
      nbytes(0),
      target_ip(0),
      request_opcode(0),
      request_purge(false),
      local_lookup_only(0),
      no_reply_message(0),
      request_timeout(0),
      expect_cache_callback(true),
      use_deferred_callback(0),
      pin_in_cache(0),
      rw_buf_msg_len(0),
      read_cluster_vc(0),
      write_cluster_vc(0),
      cluster_vc_channel(0),
      caller_buf_freebytes(0),
      readahead_vio(0),
      readahead_reader(0),
      have_all_data(false),
      cache_vc_info(),
      tunnel(0),
      tunnel_cont(0),
      tunnel_closed(0),
      lookup_open_write_vc_event(0),
      ic_arena(),
      ic_request(),
      ic_response(),
      ic_params(0),
      ic_old_info(),
      ic_new_info(),
      ic_hostname_len(0),
      cache_op_ClusterFunction(0)
  {
    token.clear();
    SET_HANDLER((CacheContHandler)&CacheContinuation::remoteOpEvent);
  }

  inline static bool
  is_ClusterThread(EThread *et)
  {
    int etype = ET_CLUSTER;
    int i;
    for (i = 0; i < eventProcessor.n_threads_for_type[etype]; ++i) {
      if (et == eventProcessor.eventthread[etype][i]) {
        return true;
      }
    }
    return false;
  }

  // Static class member functions
  static int init();
  static CacheContinuation *cacheContAllocator_alloc();
  static void cacheContAllocator_free(CacheContinuation *);
  inkcoreapi static Action *callback_failure(Action *, int, int, CacheContinuation *this_cc = 0);
  static Action *do_remote_lookup(Continuation *, const CacheKey *, CacheContinuation *, CacheFragType, const char *, int);
  inkcoreapi static Action *do_op(Continuation *, ClusterMachine *, void *, int, char *, int, int nbytes = -1, MIOBuffer *b = 0);
  static int setup_local_vc(char *data, int data_len, CacheContinuation *cc, ClusterMachine *mp, Action **);
  static void disposeOfDataBuffer(void *buf);
  static int handleDisposeEvent(int event, CacheContinuation *cc);
  static int32_t getObjectSize(VConnection *, int, CacheHTTPInfo *);
};

/////////////////////////////////////////
// Cache OP specific args for do_op()  //
/////////////////////////////////////////

// Bit definitions for cfl_flags.
// Note: Limited to 16 bits
#define CFL_OVERWRITE_ON_WRITE (1 << 1)
#define CFL_REMOVE_USER_AGENTS (1 << 2) // Historical, now unused
#define CFL_REMOVE_LINK (1 << 3)        // Historical, now unused
#define CFL_LOPENWRITE_HAVE_OLDINFO (1 << 4)
#define CFL_ALLOW_MULTIPLE_WRITES (1 << 5)
#define CFL_MAX (1 << 15)

struct CacheOpArgs_General {
  const INK_MD5 *url_md5;
  time_t pin_in_cache; // open_write() specific arg
  CacheFragType frag_type;
  uint16_t cfl_flags;

  CacheOpArgs_General() : url_md5(NULL), pin_in_cache(0), frag_type(CACHE_FRAG_TYPE_NONE), cfl_flags(0) {}
};

struct CacheOpArgs_Link {
  INK_MD5 *from;
  INK_MD5 *to;
  uint16_t cfl_flags; // see CFL_XXX defines
  CacheFragType frag_type;

  CacheOpArgs_Link() : from(NULL), to(NULL), cfl_flags(0), frag_type(CACHE_FRAG_TYPE_NONE) {}
};

struct CacheOpArgs_Deref {
  INK_MD5 *md5;
  uint16_t cfl_flags; // see CFL_XXX defines
  CacheFragType frag_type;

  CacheOpArgs_Deref() : md5(NULL), cfl_flags(0), frag_type(CACHE_FRAG_TYPE_NONE) {}
};

///////////////////////////////////
// Over the wire message formats //
///////////////////////////////////
struct CacheLookupMsg : public ClusterMessageHeader {
  INK_MD5 url_md5;
  uint32_t seq_number;
  uint32_t frag_type;
  Alias32 moi;
  enum {
    MIN_VERSION                  = 1,
    MAX_VERSION                  = 1,
    CACHE_LOOKUP_MESSAGE_VERSION = MAX_VERSION,
  };
  CacheLookupMsg(uint16_t vers = CACHE_LOOKUP_MESSAGE_VERSION) : ClusterMessageHeader(vers), seq_number(0), frag_type(0)
  {
    moi.u32 = 0;
  }

  //////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CACHE_LOOKUP_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)ALIGN_DOUBLE(offsetof(CacheLookupMsg, moi));
  }
  void
  init(uint16_t vers = CACHE_LOOKUP_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap32(&seq_number);
      ats_swap32(&frag_type);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_long : public ClusterMessageHeader {
  uint8_t opcode;
  uint8_t frag_type;
  uint16_t cfl_flags; // see CFL_XXX defines
  INK_MD5 url_md5;
  uint32_t seq_number;
  uint32_t nbytes;
  uint32_t data;   // used by open_write()
  int32_t channel; // used by open interfaces
  ClusterVCToken token;
  int32_t buffer_size; // used by open read interface
  Alias32 moi;
  enum {
    MIN_VERSION                   = 1,
    MAX_VERSION                   = 1,
    CACHE_OP_LONG_MESSAGE_VERSION = MAX_VERSION,
  };
  CacheOpMsg_long(uint16_t vers = CACHE_OP_LONG_MESSAGE_VERSION)
    : ClusterMessageHeader(vers),
      opcode(0),
      frag_type(0),
      cfl_flags(0),
      seq_number(0),
      nbytes(0),
      data(0),
      channel(0),
      buffer_size(0)
  {
    moi.u32 = 0;
  }

  //////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CACHE_OP_LONG_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)ALIGN_DOUBLE(offsetof(CacheOpMsg_long, moi));
  }
  void
  init(uint16_t vers = CACHE_OP_LONG_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
      ats_swap32(&nbytes);
      ats_swap32(&data);
      ats_swap32((uint32_t *)&channel);
      token.SwapBytes();
      ats_swap32((uint32_t *)&buffer_size);
      ats_swap32((uint32_t *)&frag_type);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_short : public ClusterMessageHeader {
  uint8_t opcode;
  uint8_t frag_type;  // currently used by open_write() (low level)
  uint16_t cfl_flags; // see CFL_XXX defines
  INK_MD5 md5;
  uint32_t seq_number;
  uint32_t nbytes;
  uint32_t data;        // currently used by open_write() (low level)
  int32_t channel;      // used by open interfaces
  ClusterVCToken token; // used by open interfaces
  int32_t buffer_size;  // used by open read interface

  // Variable portion of message
  Alias32 moi;
  enum {
    MIN_VERSION                    = 1,
    MAX_VERSION                    = 1,
    CACHE_OP_SHORT_MESSAGE_VERSION = MAX_VERSION,
  };
  CacheOpMsg_short(uint16_t vers = CACHE_OP_SHORT_MESSAGE_VERSION)
    : ClusterMessageHeader(vers),
      opcode(0),
      frag_type(0),
      cfl_flags(0),
      seq_number(0),
      nbytes(0),
      data(0),
      channel(0),
      buffer_size(0)
  {
    moi.u32 = 0;
  }

  //////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CACHE_OP_SHORT_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)ALIGN_DOUBLE(offsetof(CacheOpMsg_short, moi));
  }
  void
  init(uint16_t vers = CACHE_OP_SHORT_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for INK_MD5");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
      ats_swap32(&nbytes);
      ats_swap32(&data);
      if (opcode == CACHE_OPEN_READ) {
        ats_swap32((uint32_t *)&buffer_size);
        ats_swap32((uint32_t *)&channel);
        token.SwapBytes();
      }
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpMsg_short_2 : public ClusterMessageHeader {
  uint8_t opcode;
  uint8_t frag_type;
  uint16_t cfl_flags; // see CFL_XXX defines
  INK_MD5 md5_1;
  INK_MD5 md5_2;
  uint32_t seq_number;
  Alias32 moi;
  enum {
    MIN_VERSION                      = 1,
    MAX_VERSION                      = 1,
    CACHE_OP_SHORT_2_MESSAGE_VERSION = MAX_VERSION,
  };
  CacheOpMsg_short_2(uint16_t vers = CACHE_OP_SHORT_2_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), opcode(0), frag_type(0), cfl_flags(0), seq_number(0)
  {
    moi.u32 = 0;
  }
  //////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CACHE_OP_SHORT_2_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)ALIGN_DOUBLE(offsetof(CacheOpMsg_short_2, moi));
  }
  void
  init(uint16_t vers = CACHE_OP_SHORT_2_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ink_release_assert(!"No byte swap for MD5_1");
      ink_release_assert(!"No byte swap for MD5_2");
      ats_swap16(&cfl_flags);
      ats_swap32(&seq_number);
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

struct CacheOpReplyMsg : public ClusterMessageHeader {
  uint32_t seq_number;
  int32_t result;
  ClusterVCToken token;
  bool is_ram_cache_hit; // Entire object was from ram cache
  Alias32 moi;           // Used by CACHE_OPEN_READ & CACHE_LINK reply
  enum {
    MIN_VERSION                    = 1,
    MAX_VERSION                    = 1,
    CACHE_OP_REPLY_MESSAGE_VERSION = MAX_VERSION,
  };
  CacheOpReplyMsg(uint16_t vers = CACHE_OP_REPLY_MESSAGE_VERSION)
    : ClusterMessageHeader(vers), seq_number(0), result(0), is_ram_cache_hit(false)
  {
    moi.u32 = 0;
  }

  //////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return CACHE_OP_REPLY_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    return (int)ALIGN_DOUBLE(offsetof(CacheOpReplyMsg, moi));
  }
  void
  init(uint16_t vers = CACHE_OP_REPLY_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
    if (NeedByteSwap()) {
      ats_swap32(&seq_number);
      ats_swap32((uint32_t *)&result);
      token.SwapBytes();
    }
  }
  //////////////////////////////////////////////////////////////////////////
};

inline int
maxval(int a, int b)
{
  return ((a > b) ? a : b);
}

inline int
op_to_sizeof_fixedlen_msg(int op)
{
  switch (op) {
  case CACHE_LOOKUP_OP: {
    return CacheLookupMsg::sizeof_fixedlen_msg();
  }
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG: {
    ink_release_assert(!"op_to_sizeof_fixedlen_msg() op not supported");
    return 0;
  }
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER: {
    return CacheOpMsg_short::sizeof_fixedlen_msg();
  }
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER_LONG:
  case CACHE_OPEN_WRITE_LONG: {
    return CacheOpMsg_long::sizeof_fixedlen_msg();
  }
  case CACHE_UPDATE:
  case CACHE_REMOVE:
  case CACHE_DEREF: {
    return CacheOpMsg_short::sizeof_fixedlen_msg();
  }
  case CACHE_LINK: {
    return CacheOpMsg_short_2::sizeof_fixedlen_msg();
  }
  default: {
    ink_release_assert(!"op_to_sizeof_fixedlen_msg() unknown op");
    return 0;
  }
  } // End of switch
}

//////////////////////////////////////////////////////////////////////////////

static inline bool
event_is_lookup(int event)
{
  switch (event) {
  default:
    return false;
  case CACHE_EVENT_LOOKUP:
  case CACHE_EVENT_LOOKUP_FAILED:
    return true;
  }
}

static inline bool
event_is_open(int event)
{
  switch (event) {
  default:
    return false;
  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_OPEN_WRITE:
    return true;
  }
}

static inline bool
op_is_read(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG:
    return true;
  default:
    return false;
  }
}

static inline bool
op_is_shortform(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_BUFFER:
    return true;
  default:
    return false;
  }
}

static inline int
op_failure(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_LONG:
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
    return CACHE_EVENT_OPEN_WRITE_FAILED;

  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG:
    return CACHE_EVENT_OPEN_READ_FAILED;

  case CACHE_UPDATE:
    return CACHE_EVENT_UPDATE_FAILED;
  case CACHE_REMOVE:
    return CACHE_EVENT_REMOVE_FAILED;
  case CACHE_LINK:
    return CACHE_EVENT_LINK_FAILED;
  case CACHE_DEREF:
    return CACHE_EVENT_DEREF_FAILED;
  }
  return -1;
}

static inline int
op_needs_marshalled_coi(int opcode)
{
  switch (opcode) {
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_READ:
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_REMOVE:
  case CACHE_LINK:
  case CACHE_DEREF:
    return 0;

  case CACHE_OPEN_WRITE_LONG:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_READ_BUFFER_LONG:
  case CACHE_UPDATE:
    return 0;

  default:
    return 0;
  }
}

static inline int
event_reply_may_have_moi(int event)
{
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_LINK:
  case CACHE_EVENT_LINK_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED:
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_REMOVE_FAILED:
  case CACHE_EVENT_UPDATE_FAILED:
  case CACHE_EVENT_DEREF_FAILED:
    return true;
  default:
    return false;
  }
}

static inline int
event_is_failure(int event)
{
  switch (event) {
  case CACHE_EVENT_LOOKUP_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED:
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_UPDATE_FAILED:
  case CACHE_EVENT_REMOVE_FAILED:
  case CACHE_EVENT_LINK_FAILED:
  case CACHE_EVENT_DEREF_FAILED:
    return true;
  default:
    return false;
  }
}

#endif // __CLUSTERCACHEINTERNAL_H__
