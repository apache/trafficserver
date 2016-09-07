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

  ClusterCache.cc
****************************************************************************/

#include "P_Cluster.h"

#ifdef DEBUG
#define CLUSTER_TEST_DEBUG 1
#endif

#ifdef ENABLE_TIME_TRACE
int callback_time_dist[TIME_DIST_BUCKETS_SIZE];
int cache_callbacks = 0;

int rmt_callback_time_dist[TIME_DIST_BUCKETS_SIZE];
int rmt_cache_callbacks = 0;

int lkrmt_callback_time_dist[TIME_DIST_BUCKETS_SIZE];
int lkrmt_cache_callbacks = 0;

int cntlck_acquire_time_dist[TIME_DIST_BUCKETS_SIZE];
int cntlck_acquire_events = 0;

int open_delay_time_dist[TIME_DIST_BUCKETS_SIZE];
int open_delay_events = 0;

#endif // ENABLE_TIME_TRACE

// default will be read from config
int cache_migrate_on_demand = false;

/////////////////
// Static Data //
/////////////////
static ClassAllocator<CacheContinuation> cacheContAllocator("cacheContAllocator");

static Queue<CacheContinuation> remoteCacheContQueue[REMOTE_CONNECT_HASH];
static Ptr<ProxyMutex> remoteCacheContQueueMutex[REMOTE_CONNECT_HASH];

// 0 is an illegal sequence number
#define CACHE_NO_RESPONSE 0
static int cluster_sequence_number = 1;

#ifdef CLUSTER_TEST_DEBUG
static ink_hrtime cache_cluster_timeout = HRTIME_SECONDS(65536);
#else
static ink_hrtime cache_cluster_timeout = CACHE_CLUSTER_TIMEOUT;
#endif

///////////////////
// Declarations  //
///////////////////
static CacheContinuation *find_cache_continuation(unsigned int, unsigned int);

static unsigned int new_cache_sequence_number();

#define DOT_SEPARATED(_x) \
  ((unsigned char *)&(_x))[0], ((unsigned char *)&(_x))[1], ((unsigned char *)&(_x))[2], ((unsigned char *)&(_x))[3]

#define ET_CACHE_CONT_SM ET_NET
#define ALLOW_THREAD_STEAL true

/**********************************************************************/
#ifdef CACHE_MSG_TRACE
/**********************************************************************/

/**********************************************************************/
// Debug trace support for cache RPC messages
/**********************************************************************/

#define MAX_TENTRIES 4096
struct traceEntry {
  unsigned int seqno;
  int op;
  char *type;
};
struct traceEntry recvTraceTable[MAX_TENTRIES];
struct traceEntry sndTraceTable[MAX_TENTRIES];

static recvTraceTable_index = 0;
static sndTraceTable_index  = 0;

void
log_cache_op_msg(unsigned int seqno, int op, char *type)
{
  int t                   = ink_atomic_increment(&recvTraceTable_index, 1);
  int n                   = recvTraceTable_index % MAX_TENTRIES;
  recvTraceTable[n].seqno = seqno;
  recvTraceTable[n].op    = op;
  recvTraceTable[n].type  = type;
}

void
log_cache_op_sndmsg(unsigned int seqno, int op, char *type)
{
  int t                  = ink_atomic_increment(&sndTraceTable_index, 1);
  int n                  = sndTraceTable_index % MAX_TENTRIES;
  sndTraceTable[n].seqno = seqno;
  sndTraceTable[n].op    = op;
  sndTraceTable[n].type  = type;
}

void
dump_recvtrace_table()
{
  int n;
  printf("\n");
  for (n = 0; n < MAX_TENTRIES; ++n)
    printf("[%d] seqno=%d, op=%d type=%s\n", n, recvTraceTable[n].seqno, recvTraceTable[n].op,
           recvTraceTable[n].type ? recvTraceTable[n].type : "");
}

void
dump_sndtrace_table()
{
  int n;
  printf("\n");
  for (n = 0; n < MAX_TENTRIES; ++n)
    printf("[%d] seqno=%d, op=%d type=%s\n", n, sndTraceTable[n].seqno, sndTraceTable[n].op,
           sndTraceTable[n].type ? sndTraceTable[n].type : "");
}

/**********************************************************************/
#endif // CACHE_MSG_TRACE
/**********************************************************************/

///////////////////////////////////////////////////////////////////////
// Cluster write VC cache.
///////////////////////////////////////////////////////////////////////
//
// In the event that a remote open read fails (HTTP only), an
// open write is issued and if successful a open write connection
// is returned for the open read.  We cache the open write VC and
// resolve the subsequent open write locally from the write VC cache
// using the INK_MD5 of the URL.
// Note that this is a global per node cache.
///////////////////////////////////////////////////////////////////////

class ClusterVConnectionCache
{
public:
  ClusterVConnectionCache() { memset(hash_event, 0, sizeof(hash_event)); }
  void init();
  int MD5ToIndex(INK_MD5 *p);
  int insert(INK_MD5 *, ClusterVConnection *);
  ClusterVConnection *lookup(INK_MD5 *);

public:
  struct Entry {
    LINK(Entry, link);
    bool mark_for_delete;
    INK_MD5 key;
    ClusterVConnection *vc;

    Entry() : mark_for_delete(0), vc(0) {}
    ~Entry() {}
  };

  enum {
    MAX_TABLE_ENTRIES = 256, // must be power of 2
    SCAN_INTERVAL     = 10   // seconds
  };
  Queue<Entry> hash_table[MAX_TABLE_ENTRIES];
  Ptr<ProxyMutex> hash_lock[MAX_TABLE_ENTRIES];
  Event *hash_event[MAX_TABLE_ENTRIES];
};

static ClassAllocator<ClusterVConnectionCache::Entry> ClusterVCCacheEntryAlloc("ClusterVConnectionCache::Entry");

ClusterVConnectionCache *GlobalOpenWriteVCcache = 0;

/////////////////////////////////////////////////////////////////
// Perform periodic purges of ClusterVConnectionCache entries
/////////////////////////////////////////////////////////////////
class ClusterVConnectionCacheEvent : public Continuation
{
public:
  ClusterVConnectionCacheEvent(ClusterVConnectionCache *c, int n) : Continuation(new_ProxyMutex()), cache(c), hash_index(n)
  {
    SET_HANDLER(&ClusterVConnectionCacheEvent::eventHandler);
  }
  int eventHandler(int, Event *);

private:
  ClusterVConnectionCache *cache;
  int hash_index;
};

void
ClusterVConnectionCache::init()
{
  int n;
  ClusterVConnectionCacheEvent *eh;

  for (n = 0; n < MAX_TABLE_ENTRIES; ++n) {
    hash_lock[n] = new_ProxyMutex();
  }
  for (n = 0; n < MAX_TABLE_ENTRIES; ++n) {
    // Setup up periodic purge events on each hash list

    eh            = new ClusterVConnectionCacheEvent(this, n);
    hash_event[n] = eventProcessor.schedule_in(eh, HRTIME_SECONDS(ClusterVConnectionCache::SCAN_INTERVAL), ET_CACHE_CONT_SM);
  }
}
inline int
ClusterVConnectionCache::MD5ToIndex(INK_MD5 *p)
{
  uint64_t i = p->fold();
  int32_t h, l;

  h = i >> 32;
  l = i & 0xFFFFFFFF;
  return ((h ^ l) % MAX_TABLE_ENTRIES) & (MAX_TABLE_ENTRIES - 1);
}

int
ClusterVConnectionCache::insert(INK_MD5 *key, ClusterVConnection *vc)
{
  int index = MD5ToIndex(key);
  Entry *e;
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  MUTEX_TRY_LOCK(lock, hash_lock[index], thread);
  if (!lock.is_locked()) {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_INSERT_LOCK_MISSES_STAT);
    return 0; // lock miss, retry later

  } else {
    // Add entry to list

    e      = ClusterVCCacheEntryAlloc.alloc();
    e->key = *key;
    e->vc  = vc;
    hash_table[index].enqueue(e);
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_INSERTS_STAT);
  }
  return 1; // Success
}

ClusterVConnection *
ClusterVConnectionCache::lookup(INK_MD5 *key)
{
  int index = MD5ToIndex(key);
  Entry *e;
  ClusterVConnection *vc = 0;
  EThread *thread        = this_ethread();
  ProxyMutex *mutex      = thread->mutex;

  MUTEX_TRY_LOCK(lock, hash_lock[index], thread);
  if (!lock.is_locked()) {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_LOCK_MISSES_STAT);
    return vc; // lock miss, retry later

  } else {
    e = hash_table[index].head;
    while (e) {
      if (*key == e->key) { // Hit
        vc = e->vc;
        hash_table[index].remove(e);
        ClusterVCCacheEntryAlloc.free(e);
        CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_HITS_STAT);
        return vc;

      } else {
        e = e->link.next;
      }
    }
  }
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_MISSES_STAT);
  return (ClusterVConnection *)-1; // Miss
}

int
ClusterVConnectionCacheEvent::eventHandler(int /* event ATS_UNUSED */, Event *e)
{
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_SCANS_STAT);
  MUTEX_TRY_LOCK(lock, cache->hash_lock[hash_index], this_ethread());
  if (!lock.is_locked()) {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_SCAN_LOCK_MISSES_STAT);
    e->schedule_in(HRTIME_MSECONDS(10));
    return EVENT_DONE;
  }
  // Perform purge action on unreferenced VC(s).

  ClusterVConnectionCache::Entry *entry;
  ClusterVConnectionCache::Entry *next_entry;
  entry = cache->hash_table[hash_index].head;

  while (entry) {
    if (entry->mark_for_delete) {
      next_entry = entry->link.next;

      cache->hash_table[hash_index].remove(entry);
      entry->vc->allow_remote_close();
      entry->vc->do_io(VIO::CLOSE);

      ClusterVCCacheEntryAlloc.free(entry);
      entry = next_entry;
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_CACHE_PURGES_STAT);

    } else {
      entry->mark_for_delete = true;
      entry                  = entry->link.next;
    }
  }

  // Setup for next purge event

  e->schedule_in(HRTIME_SECONDS(ClusterVConnectionCache::SCAN_INTERVAL), ET_CACHE_CONT_SM);
  return EVENT_DONE;
}

///////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////
// init()
//   Global initializations for CacheContinuation
////////////////////////////////////////////////////
int
CacheContinuation::init()
{
  int n;
  for (n                         = 0; n < REMOTE_CONNECT_HASH; ++n)
    remoteCacheContQueueMutex[n] = new_ProxyMutex();

  GlobalOpenWriteVCcache = new ClusterVConnectionCache;
  GlobalOpenWriteVCcache->init();
  return 0;
}

///////////////////////////////////////////////////////////////////////
// do_op()
//   Main function to do a cluster cache operation
///////////////////////////////////////////////////////////////////////
Action *
CacheContinuation::do_op(Continuation *c, ClusterMachine *mp, void *args, int user_opcode, char *data, int data_len, int nbytes,
                         MIOBuffer *b)
{
  CacheContinuation *cc = 0;
  Action *act           = 0;
  char *msg             = 0;
  ClusterHandler *ch    = mp->pop_ClusterHandler();

  /////////////////////////////////////////////////////////////////////
  // Unconditionally map open read buffer interfaces to open read.
  // open read buffer interfaces are now deprecated.
  /////////////////////////////////////////////////////////////////////
  int opcode = user_opcode;
  switch (opcode) {
  case CACHE_OPEN_READ_BUFFER:
    opcode = CACHE_OPEN_READ;
    break;
  case CACHE_OPEN_READ_BUFFER_LONG:
    opcode = CACHE_OPEN_READ_LONG;
    break;
  default:
    break;
  }

  if (!ch)
    goto no_send_exit;

  if (c) {
    cc                   = cacheContAllocator_alloc();
    cc->ch               = ch;
    cc->target_machine   = mp;
    cc->request_opcode   = opcode;
    cc->mutex            = c->mutex;
    cc->action           = c;
    cc->action.cancelled = false;
    cc->start_time       = Thread::get_hrtime();
    cc->from             = mp;
    cc->result           = op_failure(opcode);
    SET_CONTINUATION_HANDLER(cc, (CacheContHandler)&CacheContinuation::remoteOpEvent);
    act = &cc->action;

    // set up sequence number so we can find this continuation

    cc->target_ip  = mp->ip;
    cc->seq_number = new_cache_sequence_number();

    // establish timeout for cache op

    unsigned int hash = FOLDHASH(cc->target_ip, cc->seq_number);
    MUTEX_TRY_LOCK(queuelock, remoteCacheContQueueMutex[hash], this_ethread());
    if (!queuelock.is_locked()) {
      // failed to acquire lock: no problem, retry later
      cc->timeout = eventProcessor.schedule_in(cc, CACHE_RETRY_PERIOD, ET_CACHE_CONT_SM);
    } else {
      remoteCacheContQueue[hash].enqueue(cc);
      MUTEX_RELEASE(queuelock);
      cc->timeout = eventProcessor.schedule_in(cc, cache_cluster_timeout, ET_CACHE_CONT_SM);
    }
  }
  //
  // Determine the type of the "Over The Wire" (OTW) message header and
  //   initialize it.
  //
  Debug("cache_msg", "do_op opcode=%d seqno=%d Machine=%p data=%p datalen=%d mio=%p", opcode,
        (c ? cc->seq_number : CACHE_NO_RESPONSE), mp, data, data_len, b);

  switch (opcode) {
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG: {
    ink_release_assert(!"write buffer not supported");
    break;
  }
  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG: {
    ink_release_assert(!"read buffer not supported");
    break;
  }
  case CACHE_OPEN_WRITE:
  case CACHE_OPEN_READ: {
    ink_release_assert(c != NULL);
    //////////////////////
    // Use short format //
    //////////////////////
    if (!data) {
      data_len = op_to_sizeof_fixedlen_msg(opcode);
      data     = (char *)ALLOCA_DOUBLE(data_len);
    }
    msg                 = (char *)data;
    CacheOpMsg_short *m = (CacheOpMsg_short *)msg;
    m->init();
    m->opcode     = opcode;
    m->cfl_flags  = ((CacheOpArgs_General *)args)->cfl_flags;
    m->md5        = *((CacheOpArgs_General *)args)->url_md5;
    cc->url_md5   = m->md5;
    m->seq_number = (c ? cc->seq_number : CACHE_NO_RESPONSE);
    m->frag_type  = ((CacheOpArgs_General *)args)->frag_type;
    if (opcode == CACHE_OPEN_WRITE) {
      m->nbytes = nbytes;
      m->data   = (uint32_t)((CacheOpArgs_General *)args)->pin_in_cache;
    } else {
      m->nbytes = 0;
      m->data   = 0;
    }

    if (opcode == CACHE_OPEN_READ) {
      //
      // Set upper limit on initial data received with response
      // for open read response
      //
      m->buffer_size = DEFAULT_MAX_BUFFER_SIZE;
    } else {
      m->buffer_size = 0;
    }

    //
    // Establish the local VC
    //
    int res = setup_local_vc(msg, data_len, cc, mp, &act);
    if (!res) {
      /////////////////////////////////////////////////////
      // Unable to setup local VC, request aborted.
      // Remove request from pending list and deallocate.
      /////////////////////////////////////////////////////
      cc->remove_and_delete(0, (Event *)0);
      return act;

    } else if (res != -1) {
      ///////////////////////////////////////
      // VC established, send request
      ///////////////////////////////////////
      break;

    } else {
      //////////////////////////////////////////////////////
      // Unable to setup VC, delay required, await callback
      //////////////////////////////////////////////////////
      goto no_send_exit;
    }
  }

  case CACHE_OPEN_READ_LONG:
  case CACHE_OPEN_WRITE_LONG: {
    ink_release_assert(c != NULL);
    //////////////////////
    // Use long format  //
    //////////////////////
    msg                = data;
    CacheOpMsg_long *m = (CacheOpMsg_long *)msg;
    m->init();
    m->opcode     = opcode;
    m->cfl_flags  = ((CacheOpArgs_General *)args)->cfl_flags;
    m->url_md5    = *((CacheOpArgs_General *)args)->url_md5;
    cc->url_md5   = m->url_md5;
    m->seq_number = (c ? cc->seq_number : CACHE_NO_RESPONSE);
    m->nbytes     = nbytes;
    m->data       = (uint32_t)((CacheOpArgs_General *)args)->pin_in_cache;
    m->frag_type  = (uint32_t)((CacheOpArgs_General *)args)->frag_type;

    if (opcode == CACHE_OPEN_READ_LONG) {
      //
      // Set upper limit on initial data received with response
      // for open read response
      //
      m->buffer_size = DEFAULT_MAX_BUFFER_SIZE;
    } else {
      m->buffer_size = 0;
    }
    //
    // Establish the local VC
    //
    int res = setup_local_vc(msg, data_len, cc, mp, &act);
    if (!res) {
      /////////////////////////////////////////////////////
      // Unable to setup local VC, request aborted.
      // Remove request from pending list and deallocate.
      /////////////////////////////////////////////////////
      cc->remove_and_delete(0, (Event *)0);
      return act;

    } else if (res != -1) {
      ///////////////////////////////////////
      // VC established, send request
      ///////////////////////////////////////
      break;

    } else {
      //////////////////////////////////////////////////////
      // Unable to setup VC, delay required, await callback
      //////////////////////////////////////////////////////
      goto no_send_exit;
    }
  }
  case CACHE_UPDATE:
  case CACHE_REMOVE:
  case CACHE_DEREF: {
    //////////////////////
    // Use short format //
    //////////////////////
    msg                 = data;
    CacheOpMsg_short *m = (CacheOpMsg_short *)msg;
    m->init();
    m->opcode    = opcode;
    m->frag_type = ((CacheOpArgs_Deref *)args)->frag_type;
    m->cfl_flags = ((CacheOpArgs_Deref *)args)->cfl_flags;
    if (opcode == CACHE_DEREF)
      m->md5 = *((CacheOpArgs_Deref *)args)->md5;
    else
      m->md5      = *((CacheOpArgs_General *)args)->url_md5;
    m->seq_number = (c ? cc->seq_number : CACHE_NO_RESPONSE);
    break;
  }
  case CACHE_LINK: {
    ////////////////////////
    // Use short_2 format //
    ////////////////////////
    msg                   = data;
    CacheOpMsg_short_2 *m = (CacheOpMsg_short_2 *)msg;
    m->init();
    m->opcode     = opcode;
    m->cfl_flags  = ((CacheOpArgs_Link *)args)->cfl_flags;
    m->md5_1      = *((CacheOpArgs_Link *)args)->from;
    m->md5_2      = *((CacheOpArgs_Link *)args)->to;
    m->seq_number = (c ? cc->seq_number : CACHE_NO_RESPONSE);
    m->frag_type  = ((CacheOpArgs_Link *)args)->frag_type;
    break;
  }
  default:
    msg = 0;
    break;
  }
#ifdef CACHE_MSG_TRACE
  log_cache_op_sndmsg((c ? cc->seq_number : CACHE_NO_RESPONSE), 0, "do_op");
#endif
  clusterProcessor.invoke_remote(
    ch, op_needs_marshalled_coi(opcode) ? CACHE_OP_MALLOCED_CLUSTER_FUNCTION : CACHE_OP_CLUSTER_FUNCTION, (char *)msg, data_len);

no_send_exit:
  if (c) {
    return act;
  } else {
    return (Action *)0;
  }
}

int
CacheContinuation::setup_local_vc(char *data, int data_len, CacheContinuation *cc, ClusterMachine *mp, Action **act)
{
  bool read_op   = op_is_read(cc->request_opcode);
  bool short_msg = op_is_shortform(cc->request_opcode);

  // Alloc buffer, copy message and attach to continuation
  cc->setMsgBufferLen(data_len);
  cc->allocMsgBuffer();
  memcpy(cc->getMsgBuffer(), data, data_len);

  SET_CONTINUATION_HANDLER(cc, (CacheContHandler)&CacheContinuation::localVCsetupEvent);

  if (short_msg) {
    Debug("cache_proto", "open_local-s (%s) seqno=%d", (read_op ? "R" : "W"), ((CacheOpMsg_short *)data)->seq_number);
  } else {
    Debug("cache_proto", "open_local-l (%s) seqno=%d", (read_op ? "R" : "W"), ((CacheOpMsg_long *)data)->seq_number);
  }

  // Create local VC
  ClusterVConnection *vc;

  if (!read_op && (cc->request_opcode == CACHE_OPEN_WRITE_LONG)) {
    // Determine if the open_write has already been established.
    vc = cc->lookupOpenWriteVC();

  } else {
    vc = clusterProcessor.open_local(cc, mp, cc->open_local_token,
                                     (CLUSTER_OPT_ALLOW_IMMEDIATE | (read_op ? CLUSTER_OPT_CONN_READ : CLUSTER_OPT_CONN_WRITE)));
  }
  if (!vc) {
    // Error, abort request
    if (short_msg) {
      Debug("cache_proto", "0open_local-s (%s) failed, seqno=%d", (read_op ? "R" : "W"), ((CacheOpMsg_short *)data)->seq_number);
    } else {
      Debug("cache_proto", "1open_local-l (%s) failed, seqno=%d", (read_op ? "R" : "W"), ((CacheOpMsg_long *)data)->seq_number);
    }
    cc->freeMsgBuffer();
    if (cc->timeout)
      cc->timeout->cancel();
    cc->timeout = NULL;

    // Post async failure callback on a different continuation.
    *act = callback_failure(&cc->action, (read_op ? CACHE_EVENT_OPEN_READ_FAILED : CACHE_EVENT_OPEN_WRITE_FAILED), 0);
    return 0;

  } else if (vc != CLUSTER_DELAYED_OPEN) {
    // We have established the VC
    if (read_op) {
      cc->read_cluster_vc = vc;
    } else {
      cc->write_cluster_vc = vc;
    }
    cc->cluster_vc_channel = vc->channel;
    vc->current_cont       = cc;

    if (short_msg) {
      CacheOpMsg_short *ms = (CacheOpMsg_short *)data;
      ms->channel          = vc->channel;
      ms->token            = cc->open_local_token;
      Debug("cache_proto", "0open_local-s (%s) success, seqno=%d chan=%d token=%d,%d VC=%p", (read_op ? "R" : "W"), ms->seq_number,
            vc->channel, ms->token.ip_created, ms->token.sequence_number, vc);
    } else {
      CacheOpMsg_long *ml = (CacheOpMsg_long *)data;
      ml->channel         = vc->channel;
      ml->token           = cc->open_local_token;
      Debug("cache_proto", "1open_local-l (%s) success, seqno=%d chan=%d token=%d,%d VC=%p", (read_op ? "R" : "W"), ml->seq_number,
            vc->channel, ml->token.ip_created, ml->token.sequence_number, vc);
    }
    cc->freeMsgBuffer();
    SET_CONTINUATION_HANDLER(cc, (CacheContHandler)&CacheContinuation::remoteOpEvent);
    return 1;

  } else {
    //////////////////////////////////////////////////////
    // Unable to setup VC, delay required, await callback
    //////////////////////////////////////////////////////
    return -1;
  }
}

ClusterVConnection *
CacheContinuation::lookupOpenWriteVC()
{
  ///////////////////////////////////////////////////////////////
  // See if we already have an open_write ClusterVConnection
  // which was established in a previous remote open_read which
  // failed.
  ///////////////////////////////////////////////////////////////
  ClusterVConnection *vc;
  CacheOpMsg_long *ml = (CacheOpMsg_long *)getMsgBuffer();

  vc = GlobalOpenWriteVCcache->lookup(&ml->url_md5);

  if (vc == ((ClusterVConnection *)0)) {
    // Retry lookup
    SET_CONTINUATION_HANDLER(this, (CacheContHandler)&CacheContinuation::lookupOpenWriteVCEvent);
    //
    // Note: In the lookupOpenWriteVCEvent handler, we use EVENT_IMMEDIATE
    //       to distinguish the lookup retry from a request timeout
    //       which uses EVENT_INTERVAL.
    //
    lookup_open_write_vc_event = eventProcessor.schedule_imm(this, ET_CACHE_CONT_SM);

  } else if (vc != ((ClusterVConnection *)-1)) {
    // Hit, found open_write VC in cache.
    // Post open_write completion by simulating a
    // remote cache op result message.

    vc->action_ = action; // establish new continuation

    SET_CONTINUATION_HANDLER(this, (CacheContHandler)&CacheContinuation::localVCsetupEvent);
    this->handleEvent(CLUSTER_EVENT_OPEN_EXISTS, vc);

    CacheOpReplyMsg msg;
    int msglen;

    msglen         = CacheOpReplyMsg::sizeof_fixedlen_msg();
    msg.result     = CACHE_EVENT_OPEN_WRITE;
    msg.seq_number = seq_number;
    msg.token      = vc->token;

    cache_op_result_ClusterFunction(ch, (void *)&msg, msglen);

  } else {
    // Miss, establish local VC and send remote open_write request

    SET_CONTINUATION_HANDLER(this, (CacheContHandler)&CacheContinuation::localVCsetupEvent);
    vc = clusterProcessor.open_local(this, from, open_local_token, (CLUSTER_OPT_ALLOW_IMMEDIATE | CLUSTER_OPT_CONN_WRITE));
    if (!vc) {
      this->handleEvent(CLUSTER_EVENT_OPEN_FAILED, 0);

    } else if (vc != CLUSTER_DELAYED_OPEN) {
      this->handleEvent(CLUSTER_EVENT_OPEN, vc);
    }
  }
  return CLUSTER_DELAYED_OPEN; // force completion in callback
}

int
CacheContinuation::lookupOpenWriteVCEvent(int event, Event *e)
{
  if (event == EVENT_IMMEDIATE) {
    // Retry open_write VC lookup
    lookupOpenWriteVC();

  } else {
    lookup_open_write_vc_event->cancel();
    SET_CONTINUATION_HANDLER(this, (CacheContHandler)&CacheContinuation::localVCsetupEvent);
    this->handleEvent(event, e);
  }
  return EVENT_DONE;
}

int
CacheContinuation::remove_and_delete(int /* event ATS_UNUSED */, Event *e)
{
  unsigned int hash = FOLDHASH(target_ip, seq_number);
  MUTEX_TRY_LOCK(queuelock, remoteCacheContQueueMutex[hash], this_ethread());
  if (queuelock.is_locked()) {
    if (remoteCacheContQueue[hash].in(this)) {
      remoteCacheContQueue[hash].remove(this);
    }
    MUTEX_RELEASE(queuelock);
    if (use_deferred_callback)
      callback_failure(&action, result, result_error, this);
    else
      cacheContAllocator_free(this);

  } else {
    SET_HANDLER((CacheContHandler)&CacheContinuation::remove_and_delete);
    if (!e) {
      timeout = eventProcessor.schedule_in(this, cache_cluster_timeout, ET_CACHE_CONT_SM);
    } else {
      e->schedule_in(cache_cluster_timeout);
    }
  }
  return EVENT_DONE;
}

int
CacheContinuation::localVCsetupEvent(int event, ClusterVConnection *vc)
{
  ink_assert(magicno == (int)MagicNo);
  ink_assert(getMsgBuffer());
  bool short_msg = op_is_shortform(request_opcode);
  bool read_op   = op_is_read(request_opcode);

  if (event == EVENT_INTERVAL) {
    Event *e          = (Event *)vc;
    unsigned int hash = FOLDHASH(target_ip, seq_number);

    MUTEX_TRY_LOCK(queuelock, remoteCacheContQueueMutex[hash], e->ethread);
    if (!queuelock.is_locked()) {
      e->schedule_in(CACHE_RETRY_PERIOD);
      return EVENT_CONT;
    }

    if (!remoteCacheContQueue[hash].in(this)) {
      ////////////////////////////////////////////////////
      // Not yet queued on outstanding operations list
      ////////////////////////////////////////////////////
      remoteCacheContQueue[hash].enqueue(this);
      ink_assert(timeout == e);
      MUTEX_RELEASE(queuelock);
      e->schedule_in(cache_cluster_timeout);
      return EVENT_CONT;

    } else {
      /////////////////////////////////////////////////////
      // Timeout occurred
      /////////////////////////////////////////////////////
      remoteCacheContQueue[hash].remove(this);
      MUTEX_RELEASE(queuelock);
      Debug("cluster_timeout", "0cluster op timeout %d", seq_number);
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_REMOTE_OP_TIMEOUTS_STAT);
      timeout = (Event *)1; // Note timeout
      /////////////////////////////////////////////////////////////////
      // Note: Failure callback is sent now, but the deallocation of
      //       the CacheContinuation is deferred until we receive the
      //       open_local() callback.
      /////////////////////////////////////////////////////////////////
      if (!action.cancelled)
        action.continuation->handleEvent((read_op ? CACHE_EVENT_OPEN_READ_FAILED : CACHE_EVENT_OPEN_WRITE_FAILED), 0);
      return EVENT_DONE;
    }

  } else if (((event == CLUSTER_EVENT_OPEN) || (event == CLUSTER_EVENT_OPEN_EXISTS)) &&
             (((ptrdiff_t)timeout & (ptrdiff_t)1) == 0)) {
    ink_hrtime now;
    now = Thread::get_hrtime();
    CLUSTER_SUM_DYN_STAT(CLUSTER_OPEN_DELAY_TIME_STAT, now - start_time);
    LOG_EVENT_TIME(start_time, open_delay_time_dist, open_delay_events);
    if (read_op) {
      read_cluster_vc = vc;
    } else {
      write_cluster_vc = vc;
    }
    cluster_vc_channel = vc->channel;
    vc->current_cont   = this;

    if (short_msg) {
      CacheOpMsg_short *ms = (CacheOpMsg_short *)getMsgBuffer();
      ms->channel          = vc->channel;
      ms->token            = open_local_token;

      Debug("cache_proto", "2open_local-s (%s) success, seqno=%d chan=%d token=%d,%d VC=%p", (read_op ? "R" : "W"), ms->seq_number,
            vc->channel, ms->token.ip_created, ms->token.sequence_number, vc);

    } else {
      CacheOpMsg_long *ml = (CacheOpMsg_long *)getMsgBuffer();
      ml->channel         = vc->channel;
      ml->token           = open_local_token;

      Debug("cache_proto", "3open_local-l (%s) success, seqno=%d chan=%d token=%d,%d VC=%p", (read_op ? "R" : "W"), ml->seq_number,
            vc->channel, ml->token.ip_created, ml->token.sequence_number, vc);
    }
    SET_HANDLER((CacheContHandler)&CacheContinuation::remoteOpEvent);

    if (event != CLUSTER_EVENT_OPEN_EXISTS) {
      // Send request message
      clusterProcessor.invoke_remote(
        ch, (op_needs_marshalled_coi(request_opcode) ? CACHE_OP_MALLOCED_CLUSTER_FUNCTION : CACHE_OP_CLUSTER_FUNCTION),
        (char *)getMsgBuffer(), getMsgBufferLen());
    }

  } else {
    int send_failure_callback = 1;

    if (((ptrdiff_t)timeout & (ptrdiff_t)1) == 0) {
      if (short_msg) {
        Debug("cache_proto", "2open_local-s (%s) failed, seqno=%d", (read_op ? "R" : "W"),
              ((CacheOpMsg_short *)getMsgBuffer())->seq_number);
      } else {
        Debug("cache_proto", "3open_local-l (%s) failed, seqno=%d", (read_op ? "R" : "W"),
              ((CacheOpMsg_long *)getMsgBuffer())->seq_number);
      }

    } else {
      Debug("cache_proto", "4open_local cancelled due to timeout, seqno=%d", seq_number);
      this->timeout = 0;

      // Deallocate VC if successfully acquired

      if (event == CLUSTER_EVENT_OPEN) {
        vc->pending_remote_fill = 0;
        vc->remote_closed       = 1; // avoid remote close msg
        vc->do_io(VIO::CLOSE);
      }
      send_failure_callback = 0; // already sent.
    }

    if (this->timeout)
      this->timeout->cancel();
    this->timeout = NULL;

    freeMsgBuffer();
    if (send_failure_callback) {
      //
      // Action corresponding to "this" already sent back to user,
      //   use "this" to establish the failure callback after
      //   removing ourselves from the active list.
      //
      this->use_deferred_callback = true;
      this->result                = (read_op ? CACHE_EVENT_OPEN_READ_FAILED : CACHE_EVENT_OPEN_WRITE_FAILED);
      this->result_error          = 0;
      remove_and_delete(0, (Event *)0);

    } else {
      cacheContAllocator_free(this);
    }
    return EVENT_DONE;
  }
  // Free message
  freeMsgBuffer();

  return EVENT_DONE;
}

///////////////////////////////////////////////////////////////////////////
// cache_op_ClusterFunction()
//   On the receiving side, handle a general cluster cache operation
///////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// Marshaling functions for OTW message headers
////////////////////////////////////////////////////////////////////////

inline CacheOpMsg_long *
unmarshal_CacheOpMsg_long(void *data, int NeedByteSwap)
{
  if (NeedByteSwap)
    ((CacheOpMsg_long *)data)->SwapBytes();
  return (CacheOpMsg_long *)data;
}

inline CacheOpMsg_short *
unmarshal_CacheOpMsg_short(void *data, int NeedByteSwap)
{
  if (NeedByteSwap)
    ((CacheOpMsg_short *)data)->SwapBytes();
  return (CacheOpMsg_short *)data;
}

inline CacheOpMsg_short_2 *
unmarshal_CacheOpMsg_short_2(void *data, int NeedByteSwap)
{
  if (NeedByteSwap)
    ((CacheOpMsg_short_2 *)data)->SwapBytes();
  return (CacheOpMsg_short_2 *)data;
}

// init_from_long() support routine for cache_op_ClusterFunction()
inline void
init_from_long(CacheContinuation *cont, CacheOpMsg_long *msg, ClusterMachine *m)
{
  cont->no_reply_message   = (msg->seq_number == CACHE_NO_RESPONSE);
  cont->seq_number         = msg->seq_number;
  cont->cfl_flags          = msg->cfl_flags;
  cont->from               = m;
  cont->url_md5            = msg->url_md5;
  cont->cluster_vc_channel = msg->channel;
  cont->frag_type          = (CacheFragType)msg->frag_type;
  if ((cont->request_opcode == CACHE_OPEN_WRITE_LONG) || (cont->request_opcode == CACHE_OPEN_READ_LONG)) {
    cont->pin_in_cache = (time_t)msg->data;
  } else {
    cont->pin_in_cache = 0;
  }
  cont->token  = msg->token;
  cont->nbytes = (((int)msg->nbytes < 0) ? 0 : msg->nbytes);

  if (cont->request_opcode == CACHE_OPEN_READ_LONG) {
    cont->caller_buf_freebytes = msg->buffer_size;
  } else {
    cont->caller_buf_freebytes = 0;
  }
}

// init_from_short() support routine for cache_op_ClusterFunction()
inline void
init_from_short(CacheContinuation *cont, CacheOpMsg_short *msg, ClusterMachine *m)
{
  cont->no_reply_message   = (msg->seq_number == CACHE_NO_RESPONSE);
  cont->seq_number         = msg->seq_number;
  cont->cfl_flags          = msg->cfl_flags;
  cont->from               = m;
  cont->url_md5            = msg->md5;
  cont->cluster_vc_channel = msg->channel;
  cont->token              = msg->token;
  cont->nbytes             = (((int)msg->nbytes < 0) ? 0 : msg->nbytes);
  cont->frag_type          = (CacheFragType)msg->frag_type;

  if (cont->request_opcode == CACHE_OPEN_WRITE) {
    cont->pin_in_cache = (time_t)msg->data;
  } else {
    cont->pin_in_cache = 0;
  }

  if (cont->request_opcode == CACHE_OPEN_READ) {
    cont->caller_buf_freebytes = msg->buffer_size;
  } else {
    cont->caller_buf_freebytes = 0;
  }
}

// init_from_short_2() support routine for cache_op_ClusterFunction()
inline void
init_from_short_2(CacheContinuation *cont, CacheOpMsg_short_2 *msg, ClusterMachine *m)
{
  cont->no_reply_message = (msg->seq_number == CACHE_NO_RESPONSE);
  cont->seq_number       = msg->seq_number;
  cont->cfl_flags        = msg->cfl_flags;
  cont->from             = m;
  cont->url_md5          = msg->md5_1;
  cont->frag_type        = (CacheFragType)msg->frag_type;
}

void
cache_op_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  ////////////////////////////////////////////////////////
  // Note: we are running on the ET_CLUSTER thread
  ////////////////////////////////////////////////////////
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CACHE_OUTSTANDING_STAT);

  int opcode;
  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;

  if (mh->GetMsgVersion() != CacheOpMsg_long::CACHE_OP_LONG_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"cache_op_ClusterFunction() bad msg version");
  }
  opcode = ((CacheOpMsg_long *)data)->opcode;

  // If necessary, create a continuation to reflect the response back

  CacheContinuation *c = CacheContinuation::cacheContAllocator_alloc();
  c->mutex             = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, c->mutex, this_ethread());
  c->request_opcode = opcode;
  c->token.clear();
  c->start_time = Thread::get_hrtime();
  c->ch         = ch;
  SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::replyOpEvent);

  switch (opcode) {
  case CACHE_OPEN_WRITE_BUFFER:
  case CACHE_OPEN_WRITE_BUFFER_LONG:
    ink_release_assert(!"cache_op_ClusterFunction WRITE_BUFFER not supported");
    break;

  case CACHE_OPEN_READ_BUFFER:
  case CACHE_OPEN_READ_BUFFER_LONG:
    ink_release_assert(!"cache_op_ClusterFunction READ_BUFFER not supported");
    break;

  case CACHE_OPEN_READ: {
    CacheOpMsg_short *msg = unmarshal_CacheOpMsg_short(data, mh->NeedByteSwap());
    init_from_short(c, msg, ch->machine);
    Debug("cache_msg", "cache_op-s op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
    //
    // Establish the remote side of the ClusterVConnection
    //
    c->write_cluster_vc = clusterProcessor.connect_local((Continuation *)0, &c->token, c->cluster_vc_channel,
                                                         (CLUSTER_OPT_IMMEDIATE | CLUSTER_OPT_CONN_READ));
    if (!c->write_cluster_vc) {
      // Unable to setup channel, abort processing.
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CHAN_INUSE_STAT);
      Debug("chan_inuse", "1Remote chan=%d inuse tok.ip=%u.%u.%u.%u tok.seqno=%d seqno=%d", c->cluster_vc_channel,
            DOT_SEPARATED(c->token.ip_created), c->token.sequence_number, c->seq_number);

      // Send cluster op failed reply
      c->replyOpEvent(CACHE_EVENT_OPEN_READ_FAILED, (VConnection *)-ECLUSTER_CHANNEL_INUSE);
      break;

    } else {
      c->write_cluster_vc->current_cont = c;
    }
    ink_release_assert(c->write_cluster_vc != CLUSTER_DELAYED_OPEN);
    ink_release_assert((opcode == CACHE_OPEN_READ) || c->write_cluster_vc->pending_remote_fill);

    SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::setupVCdataRead);
    Debug("cache_proto", "0read op, seqno=%d chan=%d bufsize=%d token=%d,%d", msg->seq_number, msg->channel, msg->buffer_size,
          msg->token.ip_created, msg->token.sequence_number);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_open_read");
#endif
    CacheKey key(msg->md5);

    char *hostname = NULL;
    int host_len   = len - op_to_sizeof_fixedlen_msg(opcode);
    if (host_len) {
      hostname = (char *)msg->moi.byte;
    }
    Cache *call_cache = caches[c->frag_type];
    c->cache_action   = call_cache->open_read(c, &key, c->frag_type, hostname, host_len);
    break;
  }
  case CACHE_OPEN_READ_LONG: {
    // Cache needs message data, copy it.
    c->setMsgBufferLen(len);
    c->allocMsgBuffer();
    memcpy(c->getMsgBuffer(), (char *)data, len);

    int flen             = CacheOpMsg_long::sizeof_fixedlen_msg();
    CacheOpMsg_long *msg = unmarshal_CacheOpMsg_long(c->getMsgBuffer(), mh->NeedByteSwap());
    init_from_long(c, msg, ch->machine);
    Debug("cache_msg", "cache_op-l op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_open_read_long");
#endif
    //
    // Establish the remote side of the ClusterVConnection
    //
    c->write_cluster_vc = clusterProcessor.connect_local((Continuation *)0, &c->token, c->cluster_vc_channel,
                                                         (CLUSTER_OPT_IMMEDIATE | CLUSTER_OPT_CONN_READ));
    if (!c->write_cluster_vc) {
      // Unable to setup channel, abort processing.
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CHAN_INUSE_STAT);
      Debug("chan_inuse", "2Remote chan=%d inuse tok.ip=%u.%u.%u.%u tok.seqno=%d seqno=%d", c->cluster_vc_channel,
            DOT_SEPARATED(c->token.ip_created), c->token.sequence_number, c->seq_number);

      // Send cluster op failed reply
      c->replyOpEvent(CACHE_EVENT_OPEN_READ_FAILED, (VConnection *)-ECLUSTER_CHANNEL_INUSE);
      break;

    } else {
      c->write_cluster_vc->current_cont = c;
    }
    ink_release_assert(c->write_cluster_vc != CLUSTER_DELAYED_OPEN);
    ink_release_assert((opcode == CACHE_OPEN_READ_LONG) || c->write_cluster_vc->pending_remote_fill);

    SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::setupReadWriteVC);
    Debug("cache_proto", "1read op, seqno=%d chan=%d bufsize=%d token=%d,%d", msg->seq_number, msg->channel, msg->buffer_size,
          msg->token.ip_created, msg->token.sequence_number);

    const char *p = (const char *)msg + flen;
    int moi_len   = len - flen;
    int res;

    ink_assert(moi_len > 0);

    // Unmarshal CacheHTTPHdr
    res = c->ic_request.unmarshal((char *)p, moi_len, NULL);
    ink_assert(res > 0);
    ink_assert(c->ic_request.valid());
    c->request_purge =
      c->ic_request.method_get_wksidx() == HTTP_WKSIDX_PURGE || c->ic_request.method_get_wksidx() == HTTP_WKSIDX_DELETE;
    moi_len -= res;
    p += res;
    ink_assert(moi_len > 0);
    // Unmarshal CacheLookupHttpConfig
    c->ic_params = new (CacheLookupHttpConfigAllocator.alloc()) CacheLookupHttpConfig();
    res          = c->ic_params->unmarshal(&c->ic_arena, (const char *)p, moi_len);
    ink_assert(res > 0);

    moi_len -= res;
    p += res;

    CacheKey key(msg->url_md5);

    char *hostname = NULL;
    int host_len   = 0;

    if (moi_len) {
      hostname = (char *)p;
      host_len = moi_len;

      // Save hostname and attach it to the continuation since we may
      //  need it if we convert this to an open_write.

      c->ic_hostname     = new_IOBufferData(iobuffer_size_to_index(host_len));
      c->ic_hostname_len = host_len;

      memcpy(c->ic_hostname->data(), hostname, host_len);
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a         = call_cache->open_read(c, &key, &c->ic_request, c->ic_params, c->frag_type, hostname, host_len);
    // Get rid of purify warnings since 'c' can be freed by open_read.
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }
  case CACHE_OPEN_WRITE: {
    CacheOpMsg_short *msg = unmarshal_CacheOpMsg_short(data, mh->NeedByteSwap());
    init_from_short(c, msg, ch->machine);
    Debug("cache_msg", "cache_op-s op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_open_write");
#endif
    //
    // Establish the remote side of the ClusterVConnection
    //
    c->read_cluster_vc = clusterProcessor.connect_local((Continuation *)0, &c->token, c->cluster_vc_channel,
                                                        (CLUSTER_OPT_IMMEDIATE | CLUSTER_OPT_CONN_WRITE));
    if (!c->read_cluster_vc) {
      // Unable to setup channel, abort processing.
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CHAN_INUSE_STAT);
      Debug("chan_inuse", "3Remote chan=%d inuse tok.ip=%u.%u.%u.%u tok.seqno=%d seqno=%d", c->cluster_vc_channel,
            DOT_SEPARATED(c->token.ip_created), c->token.sequence_number, c->seq_number);

      // Send cluster op failed reply
      c->replyOpEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (VConnection *)-ECLUSTER_CHANNEL_INUSE);
      break;

    } else {
      c->read_cluster_vc->current_cont = c;
    }
    ink_release_assert(c->read_cluster_vc != CLUSTER_DELAYED_OPEN);

    CacheKey key(msg->md5);

    char *hostname = NULL;
    int host_len   = len - op_to_sizeof_fixedlen_msg(opcode);
    if (host_len) {
      hostname = (char *)msg->moi.byte;
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a =
      call_cache->open_write(c, &key, c->frag_type, !!(c->cfl_flags & CFL_OVERWRITE_ON_WRITE), c->pin_in_cache, hostname, host_len);
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }
  case CACHE_OPEN_WRITE_LONG: {
    // Cache needs message data, copy it.
    c->setMsgBufferLen(len);
    c->allocMsgBuffer();
    memcpy(c->getMsgBuffer(), (char *)data, len);

    int flen             = CacheOpMsg_long::sizeof_fixedlen_msg();
    CacheOpMsg_long *msg = unmarshal_CacheOpMsg_long(c->getMsgBuffer(), mh->NeedByteSwap());
    init_from_long(c, msg, ch->machine);
    Debug("cache_msg", "cache_op-l op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_open_write_long");
#endif
    //
    // Establish the remote side of the ClusterVConnection
    //
    c->read_cluster_vc = clusterProcessor.connect_local((Continuation *)0, &c->token, c->cluster_vc_channel,
                                                        (CLUSTER_OPT_IMMEDIATE | CLUSTER_OPT_CONN_WRITE));
    if (!c->read_cluster_vc) {
      // Unable to setup channel, abort processing.
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CHAN_INUSE_STAT);
      Debug("chan_inuse", "4Remote chan=%d inuse tok.ip=%u.%u.%u.%u tok.seqno=%d seqno=%d", c->cluster_vc_channel,
            DOT_SEPARATED(c->token.ip_created), c->token.sequence_number, c->seq_number);

      // Send cluster op failed reply
      c->replyOpEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (VConnection *)-ECLUSTER_CHANNEL_INUSE);
      break;

    } else {
      c->read_cluster_vc->current_cont = c;
    }
    ink_release_assert(c->read_cluster_vc != CLUSTER_DELAYED_OPEN);

    CacheHTTPInfo *ci = 0;
    const char *p     = (const char *)msg + flen;
    int res           = 0;
    int moi_len       = len - flen;

    if (moi_len && c->cfl_flags & CFL_LOPENWRITE_HAVE_OLDINFO) {
      // Unmarshal old CacheHTTPInfo
      res = HTTPInfo::unmarshal((char *)p, moi_len, NULL);
      ink_assert(res > 0);
      c->ic_old_info.get_handle((char *)p, moi_len);
      ink_assert(c->ic_old_info.valid());
      ci = &c->ic_old_info;
    }
    if (c->cfl_flags & CFL_ALLOW_MULTIPLE_WRITES) {
      ink_assert(!ci);
      ci = (CacheHTTPInfo *)CACHE_ALLOW_MULTIPLE_WRITES;
    }
    moi_len -= res;
    p += res;

    CacheKey key(msg->url_md5);
    char *hostname = NULL;

    if (moi_len) {
      hostname = (char *)p;
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a         = call_cache->open_write(c, &key, ci, c->pin_in_cache, NULL, c->frag_type, hostname, moi_len);
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }
  case CACHE_REMOVE: {
    CacheOpMsg_short *msg = unmarshal_CacheOpMsg_short(data, mh->NeedByteSwap());
    init_from_short(c, msg, ch->machine);
    Debug("cache_msg", "cache_op op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_remove");
#endif
    CacheKey key(msg->md5);

    char *hostname = NULL;
    int host_len   = len - op_to_sizeof_fixedlen_msg(opcode);
    if (host_len) {
      hostname = (char *)msg->moi.byte;
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a         = call_cache->remove(c, &key, c->frag_type, hostname, host_len);
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }
  case CACHE_LINK: {
    CacheOpMsg_short_2 *msg = unmarshal_CacheOpMsg_short_2(data, mh->NeedByteSwap());
    init_from_short_2(c, msg, ch->machine);
    Debug("cache_msg", "cache_op op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_link");
#endif

    CacheKey key1(msg->md5_1);
    CacheKey key2(msg->md5_2);

    char *hostname = NULL;
    int host_len   = len - op_to_sizeof_fixedlen_msg(opcode);
    if (host_len) {
      hostname = (char *)msg->moi.byte;
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a         = call_cache->link(c, &key1, &key2, c->frag_type, hostname, host_len);
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }
  case CACHE_DEREF: {
    CacheOpMsg_short *msg = unmarshal_CacheOpMsg_short(data, mh->NeedByteSwap());
    init_from_short(c, msg, ch->machine);
    Debug("cache_msg", "cache_op op=%d seqno=%d data=%p len=%d machine=%p", opcode, c->seq_number, data, len, ch->machine);
#ifdef CACHE_MSG_TRACE
    log_cache_op_msg(msg->seq_number, len, "cache_op_deref");
#endif

    CacheKey key(msg->md5);

    char *hostname = NULL;
    int host_len   = len - op_to_sizeof_fixedlen_msg(opcode);
    if (host_len) {
      hostname = (char *)msg->moi.byte;
    }

    Cache *call_cache = caches[c->frag_type];
    Action *a         = call_cache->deref(c, &key, c->frag_type, hostname, host_len);
    if (a != ACTION_RESULT_DONE) {
      c->cache_action = a;
    }
    break;
  }

  default: {
    ink_release_assert(0);
  }
  } // End of switch
}

void
cache_op_malloc_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  cache_op_ClusterFunction(ch, data, len);
  // We own the message data, free it back to the Cluster subsystem
  clusterProcessor.free_remote_data((char *)data, len);
}

int
CacheContinuation::setupVCdataRead(int event, VConnection *vc)
{
  ink_assert(magicno == (int)MagicNo);
  //
  // Setup the initial data read for the given Cache VC.
  // This data is sent back in the response message.
  //
  if (event == CACHE_EVENT_OPEN_READ) {
    //////////////////////////////////////////
    // Allocate buffer and initiate read.
    //////////////////////////////////////////
    Debug("cache_proto", "setupVCdataRead CACHE_EVENT_OPEN_READ seqno=%d", seq_number);
    ink_release_assert(caller_buf_freebytes);
    SET_HANDLER((CacheContHandler)&CacheContinuation::VCdataRead);

    int64_t size_index = iobuffer_size_to_index(caller_buf_freebytes);
    MIOBuffer *buf     = new_MIOBuffer(size_index);
    readahead_reader   = buf->alloc_reader();

    MUTEX_TRY_LOCK(lock, mutex, this_ethread()); // prevent immediate callback
    readahead_vio = vc->do_io_read(this, caller_buf_freebytes, buf);
    return EVENT_DONE;

  } else {
    // Error case, deflect processing to replyOpEvent.
    SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
    return handleEvent(event, vc);
  }
}

int
CacheContinuation::VCdataRead(int event, VIO *target_vio)
{
  ink_release_assert(magicno == (int)MagicNo);
  ink_release_assert(readahead_vio == target_vio);

  VConnection *vc = target_vio->vc_server;
  int reply       = CACHE_EVENT_OPEN_READ;
  int32_t object_size;

  switch (event) {
  case VC_EVENT_EOS: {
    if (!target_vio->ndone) {
      // Doc with zero byte body, handle as read failure
      goto read_failed;
    }
    // Fall through
  }
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    int clone_bytes;
    int current_ndone = target_vio->ndone;

    ink_assert(current_ndone);
    ink_assert(current_ndone <= readahead_reader->read_avail());

    object_size   = getObjectSize(vc, request_opcode, &cache_vc_info);
    have_all_data = ((object_size <= caller_buf_freebytes) && (object_size == current_ndone));

    // Use no more than the caller's max buffer limit

    clone_bytes = current_ndone;
    if (!have_all_data) {
      if (current_ndone > caller_buf_freebytes) {
        clone_bytes = caller_buf_freebytes;
      }
    }
    // Clone data

    IOBufferBlock *tail;
    readahead_data =
      clone_IOBufferBlockList(readahead_reader->get_current_block(), readahead_reader->start_offset, clone_bytes, &tail);

    if (have_all_data) {
      // Close VC, since no more data and also to avoid VC_EVENT_EOS

      MIOBuffer *mbuf = target_vio->buffer.writer();
      vc->do_io(VIO::CLOSE);
      free_MIOBuffer(mbuf);
      readahead_vio = 0;
    }
    SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
    handleEvent(reply, vc);
    return EVENT_CONT;
  }
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  default: {
  read_failed:
    // Read failed, deflect to replyOpEvent.

    MIOBuffer *mbuf = target_vio->buffer.writer();
    vc->do_io(VIO::CLOSE);
    free_MIOBuffer(mbuf);
    readahead_vio = 0;
    reply         = CACHE_EVENT_OPEN_READ_FAILED;

    SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
    handleEvent(reply, (VConnection *)-ECLUSTER_ORB_DATA_READ);
    return EVENT_DONE;
  }
  } // End of switch
}

int
CacheContinuation::setupReadWriteVC(int event, VConnection *vc)
{
  // Only handles OPEN_READ_LONG processing.

  switch (event) {
  case CACHE_EVENT_OPEN_READ: {
    // setup readahead

    SET_HANDLER((CacheContHandler)&CacheContinuation::setupVCdataRead);
    return handleEvent(event, vc);
    break;
  }
  case CACHE_EVENT_OPEN_READ_FAILED: {
    if (frag_type == CACHE_FRAG_TYPE_HTTP && !request_purge) {
      // HTTP open read failed, attempt open write now to avoid an additional
      //  message round trip

      CacheKey key(url_md5);

      Cache *call_cache = caches[frag_type];
      Action *a = call_cache->open_write(this, &key, 0, pin_in_cache, NULL, frag_type, ic_hostname ? ic_hostname->data() : NULL,
                                         ic_hostname_len);
      if (a != ACTION_RESULT_DONE) {
        cache_action = a;
      }
    } else {
      SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
      return handleEvent(CACHE_EVENT_OPEN_READ_FAILED, 0);
    }
    break;
  }
  case CACHE_EVENT_OPEN_WRITE: {
    // Convert from read to write connection

    ink_assert(!read_cluster_vc && write_cluster_vc);
    read_cluster_vc = write_cluster_vc;
    read_cluster_vc->set_type(CLUSTER_OPT_CONN_WRITE);
    write_cluster_vc = 0;

    SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
    return handleEvent(event, vc);
    break;
  }
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  default: {
    SET_HANDLER((CacheContHandler)&CacheContinuation::replyOpEvent);
    return handleEvent(CACHE_EVENT_OPEN_READ_FAILED, 0);
    break;
  }
  } // end of switch

  return EVENT_DONE;
}

/////////////////////////////////////////////////////////////////////////
// replyOpEvent()
//   Reflect the (local) reply back to the (remote) requesting node.
/////////////////////////////////////////////////////////////////////////
int
CacheContinuation::replyOpEvent(int event, VConnection *cvc)
{
  ink_assert(magicno == (int)MagicNo);
  Debug("cache_proto", "replyOpEvent(this=%p,event=%d,VC=%p)", this, event, cvc);
  ink_hrtime now;
  now = Thread::get_hrtime();
  CLUSTER_SUM_DYN_STAT(CLUSTER_CACHE_CALLBACK_TIME_STAT, now - start_time);
  LOG_EVENT_TIME(start_time, callback_time_dist, cache_callbacks);
  ink_release_assert(expect_cache_callback);
  expect_cache_callback = false; // make sure we are called back exactly once

  result                        = event;
  bool open                     = event_is_open(event);
  bool read_op                  = op_is_read(request_opcode);
  bool open_read_now_open_write = false;

  // Reply message initializations
  CacheOpReplyMsg rmsg;
  CacheOpReplyMsg *msg = &rmsg;
  msg->result          = event;

  if ((request_opcode == CACHE_OPEN_READ_LONG) && cvc && (event == CACHE_EVENT_OPEN_WRITE)) {
    //////////////////////////////////////////////////////////////////////////
    // open read failed, but open write succeeded, set result to
    // CACHE_EVENT_OPEN_READ_FAILED and make result token non zero to
    // signal to the remote node that we have established a write connection.
    //////////////////////////////////////////////////////////////////////////
    msg->result              = CACHE_EVENT_OPEN_READ_FAILED;
    open_read_now_open_write = true;
  }

  msg->seq_number = seq_number;
  int flen        = CacheOpReplyMsg::sizeof_fixedlen_msg(); // include token
  int len         = 0;
  int vers        = 0;

  int results_expected = 1;

  if (no_reply_message) // CACHE_NO_RESPONSE request
    goto free_exit;

  if (open) {
    // prepare for CACHE_OPEN_EVENT

    results_expected = 2;
    cache_vc         = cvc;
    cache_read       = (event == CACHE_EVENT_OPEN_READ);

    if (read_op && !open_read_now_open_write) {
      ink_release_assert(write_cluster_vc->pending_remote_fill);
      ink_assert(have_all_data || (readahead_vio == &((CacheVC *)cache_vc)->vio));
      Debug("cache_proto", "connect_local success seqno=%d have_all_data=%d", seq_number, (have_all_data ? 1 : 0));

      if (have_all_data) {
        msg->token.clear(); // Tell sender no conn established
        write_cluster_vc->type = VC_CLUSTER_WRITE;
      } else {
        msg->token = token; // Tell sender conn established
        setupReadBufTunnel(cache_vc, write_cluster_vc);
      }

    } else {
      Debug("cache_proto", "cache_open [%s] success seqno=%d", (cache_read ? "R" : "W"), seq_number);
      msg->token = token; // Tell sender conn established

      OneWayTunnel *pOWT = OneWayTunnel::OneWayTunnel_alloc();
      pOWT->init(read_cluster_vc, cache_vc, NULL, nbytes ? nbytes : DEFAULT_MAX_BUFFER_SIZE, this->mutex);
      read_cluster_vc->allow_remote_close();
      results_expected--;
    }

    // For cache reads, marshal the associated CacheHTTPInfo in the reply
    if (cache_read) {
      int res;

      msg->is_ram_cache_hit = ((CacheVC *)cache_vc)->is_ram_cache_hit();

      if (!cache_vc_info.valid()) {
        (void)getObjectSize(cache_vc, request_opcode, &cache_vc_info);
      }
      // Determine data length and allocate
      len                    = cache_vc_info.marshal_length();
      CacheOpReplyMsg *reply = (CacheOpReplyMsg *)ALLOCA_DOUBLE(flen + len);

      // Initialize reply message header
      *reply = *msg;

      // Marshal response data into reply message
      res = cache_vc_info.marshal((char *)reply + flen, len);
      ink_assert(res >= 0 && res <= len);

      // Make reply message the current message
      msg = reply;
    }

  } else {
    Debug("cache_proto", "cache operation failed result=%d seqno=%d (this=%p)", event, seq_number, this);
    msg->token.clear(); // Tell sender no conn established

    // Reallocate reply message, allowing for marshalled data
    len += sizeof(int32_t);
    CacheOpReplyMsg *reply = (CacheOpReplyMsg *)ALLOCA_DOUBLE(flen + len);

    // Initialize reply message header
    *reply = *msg;

    if (request_opcode != CACHE_LINK) {
      //
      // open read/write failed, close preallocated VC
      //
      if (read_cluster_vc) {
        read_cluster_vc->remote_closed = 1; // avoid remote close msg
        read_cluster_vc->do_io(VIO::CLOSE);
      }
      if (write_cluster_vc) {
        write_cluster_vc->pending_remote_fill = 0;
        write_cluster_vc->remote_closed       = 1; // avoid remote close msg
        write_cluster_vc->do_io(VIO::CLOSE);
      }
      reply->moi.u32 = (int32_t)((uintptr_t)cvc & 0xffffffff); // code describing failure
    }
    // Make reply message the current message
    msg = reply;
  }
  CLUSTER_DECREMENT_DYN_STAT(CLUSTER_CACHE_OUTSTANDING_STAT);

//
// Send reply message
//
#ifdef CACHE_MSG_TRACE
  log_cache_op_sndmsg(msg->seq_number, 0, "replyOpEvent");
#endif
  vers = CacheOpReplyMsg::protoToVersion(from->msg_proto_major);
  if (vers == CacheOpReplyMsg::CACHE_OP_REPLY_MESSAGE_VERSION) {
    if (read_op) {
      // Transmit reply message and object data in same cluster message
      Debug("cache_proto", "Sending reply/data seqno=%d buflen=%" PRId64, seq_number,
            readahead_data ? bytes_IOBufferBlockList(readahead_data, 1) : 0);
      clusterProcessor.invoke_remote_data(ch, CACHE_OP_RESULT_CLUSTER_FUNCTION, (void *)msg, (flen + len), readahead_data,
                                          cluster_vc_channel, &token, &CacheContinuation::disposeOfDataBuffer, (void *)this,
                                          CLUSTER_OPT_STEAL);
    } else {
      Debug("cache_proto", "Sending reply seqno=%d, (this=%p)", seq_number, this);
      clusterProcessor.invoke_remote(ch, CACHE_OP_RESULT_CLUSTER_FUNCTION, (void *)msg, (flen + len), CLUSTER_OPT_STEAL);
    }

  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"replyOpEvent() bad msg version");
  }

free_exit:
  results_expected--;
  if (results_expected <= 0) {
    Debug("cache_proto", "replyOpEvent: freeing this=%p", this);
    cacheContAllocator_free(this);
  }
  return EVENT_DONE;
}

void
CacheContinuation::setupReadBufTunnel(VConnection *cache_read_vc, VConnection *cluster_write_vc)
{
  ////////////////////////////////////////////////////////////
  // Setup OneWayTunnel and tunnel close event handler.
  // Used in readahead processing on open read connections.
  ////////////////////////////////////////////////////////////
  tunnel_cont        = cacheContAllocator_alloc();
  tunnel_cont->mutex = this->mutex;
  SET_CONTINUATION_HANDLER(tunnel_cont, (CacheContHandler)&CacheContinuation::tunnelClosedEvent);
  int64_t ravail = bytes_IOBufferBlockList(readahead_data, 1);

  tunnel_mutex  = tunnel_cont->mutex;
  tunnel_closed = false;

  tunnel = OneWayTunnel::OneWayTunnel_alloc();
  readahead_reader->consume(ravail); // allow for bytes sent in initial reply
  tunnel->init(cache_read_vc, cluster_write_vc, tunnel_cont, readahead_vio, readahead_reader);
  tunnel_cont->action      = this;
  tunnel_cont->tunnel      = tunnel;
  tunnel_cont->tunnel_cont = tunnel_cont;

  // Disable cluster_write_vc
  ((ClusterVConnection *)cluster_write_vc)->write.enabled = 0;

  // Disable cache read VC
  readahead_vio->nbytes = readahead_vio->ndone;

  /////////////////////////////////////////////////////////////////////
  // At this point, the OneWayTunnel is blocked awaiting a reenable
  // on both the source and target VCs. Reenable occurs after the
  // message containing the initial data and open read reply are sent.
  /////////////////////////////////////////////////////////////////////
}

///////////////////////////////////////////////////////////////////////
// Tunnnel exited event handler, used for readahead on open read.
///////////////////////////////////////////////////////////////////////
int
CacheContinuation::tunnelClosedEvent(int /* event ATS_UNUSED */, void *c)
{
  ink_assert(magicno == (int)MagicNo);
  // Note: We are called with the tunnel_mutex held.
  CacheContinuation *tc = (CacheContinuation *)c;
  ink_release_assert(tc->tunnel_cont == tc);
  CacheContinuation *real_cc = (CacheContinuation *)tc->action.continuation;

  if (real_cc) {
    // Notify the real continuation of the tunnel closed event
    real_cc->tunnel        = 0;
    real_cc->tunnel_cont   = 0;
    real_cc->tunnel_closed = true;
  }
  OneWayTunnel::OneWayTunnel_free(tc->tunnel);
  cacheContAllocator_free(tc);

  return EVENT_DONE;
}

////////////////////////////////////////////////////////////
// Retry DisposeOfDataBuffer continuation
////////////////////////////////////////////////////////////
struct retryDisposeOfDataBuffer;
typedef int (retryDisposeOfDataBuffer::*rtryDisOfDBufHandler)(int, void *);
struct retryDisposeOfDataBuffer : public Continuation {
  CacheContinuation *c;

  int
  handleRetryEvent(int event, Event *e)
  {
    if (CacheContinuation::handleDisposeEvent(event, c) == EVENT_DONE) {
      delete this;
      return EVENT_DONE;
    } else {
      e->schedule_in(HRTIME_MSECONDS(10));
      return EVENT_CONT;
    }
  }
  retryDisposeOfDataBuffer(CacheContinuation *cont) : Continuation(new_ProxyMutex()), c(cont)
  {
    SET_HANDLER((rtryDisOfDBufHandler)&retryDisposeOfDataBuffer::handleRetryEvent);
  }
};

//////////////////////////////////////////////////////////////////
// Callback from cluster to dispose of data passed in
// call to invoke_remote_data().
//////////////////////////////////////////////////////////////////
void
CacheContinuation::disposeOfDataBuffer(void *d)
{
  ink_assert(d);
  CacheContinuation *cc = (CacheContinuation *)d;
  ink_assert(cc->have_all_data || cc->readahead_vio);
  ink_assert(cc->have_all_data || (cc->readahead_vio == &((CacheVC *)cc->cache_vc)->vio));

  if (cc->have_all_data) {
    //
    // All object data resides in the buffer, no OneWayTunnel
    // started and the Cache VConnection has already been closed.
    // Close write_cluster_vc and set remote close to avoid send of
    // close message to remote node.
    //
    cc->write_cluster_vc->pending_remote_fill = 0;
    cc->write_cluster_vc->remote_closed       = 1;
    cc->write_cluster_vc->do_io(VIO::CLOSE);
    cc->readahead_data = 0;

    cacheContAllocator_free(cc);

  } else {
    cc->write_cluster_vc->pending_remote_fill = 0;
    cc->write_cluster_vc->allow_remote_close();
    if (handleDisposeEvent(0, cc) == EVENT_CONT) {
      // Setup retry continuation.
      retryDisposeOfDataBuffer *retryCont = new retryDisposeOfDataBuffer(cc);
      eventProcessor.schedule_in(retryCont, HRTIME_MSECONDS(10), ET_CALL);
    }
  }
}

int
CacheContinuation::handleDisposeEvent(int /* event ATS_UNUSED */, CacheContinuation *cc)
{
  ink_assert(cc->magicno == (int)MagicNo);
  MUTEX_TRY_LOCK(lock, cc->tunnel_mutex, this_ethread());
  if (lock.is_locked()) {
    // Write of initial object data is complete.

    if (!cc->tunnel_closed) {
      // Start tunnel by reenabling source and target VCs.

      cc->tunnel->vioSource->nbytes = getObjectSize(cc->tunnel->vioSource->vc_server, cc->request_opcode, 0);
      cc->tunnel->vioSource->reenable_re();

      // Tunnel may be closed by vioSource->reenable_re(),
      // we should check it again here:
      if (!cc->tunnel_closed) {
        cc->tunnel->vioTarget->reenable();

        // Tell tunnel event we are gone
        cc->tunnel_cont->action.continuation = 0;
      }
    }
    cacheContAllocator_free(cc);
    return EVENT_DONE;

  } else {
    // Lock acquire failed, retry operation.
    return EVENT_CONT;
  }
}

/////////////////////////////////////////////////////////////////////////////
// cache_op_result_ClusterFunction()
//   Invoked on the machine which initiated a remote op, this
//   unmarshals the result and calls a continuation in the requesting thread.
/////////////////////////////////////////////////////////////////////////////
void
cache_op_result_ClusterFunction(ClusterHandler *ch, void *d, int l)
{
  ////////////////////////////////////////////////////////
  // Note: we are running on the ET_CACHE_CONT_SM thread
  ////////////////////////////////////////////////////////

  // Copy reply message data
  Ptr<IOBufferData> iob = make_ptr(new_IOBufferData(iobuffer_size_to_index(l)));
  memcpy(iob->data(), (char *)d, l);
  char *data = iob->data();
  int flen, len = l;
  CacheHTTPInfo ci;
  CacheOpReplyMsg *msg     = (CacheOpReplyMsg *)data;
  int32_t op_result_error  = 0;
  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;

  if (mh->GetMsgVersion() != CacheOpReplyMsg::CACHE_OP_REPLY_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"cache_op_result_ClusterFunction() bad msg version");
  }

  flen = CacheOpReplyMsg::sizeof_fixedlen_msg();
  if (mh->NeedByteSwap())
    msg->SwapBytes();

  Debug("cluster_cache", "received cache op result, seqno=%d result=%d", msg->seq_number, msg->result);

  // If applicable, unmarshal any response data
  if ((len > flen) && event_reply_may_have_moi(msg->result)) {
    switch (msg->result) {
    case CACHE_EVENT_OPEN_READ: {
      char *p = (char *)msg + flen;
      int res;

      // Unmarshal CacheHTTPInfo
      res = HTTPInfo::unmarshal(p, len, NULL);
      ci.get_handle(p, len);
      ink_assert(res > 0);
      ink_assert(ci.valid());
      break;
    }
    case CACHE_EVENT_LINK:
    case CACHE_EVENT_LINK_FAILED:
      break;
    case CACHE_EVENT_OPEN_READ_FAILED:
    case CACHE_EVENT_OPEN_WRITE_FAILED:
    case CACHE_EVENT_REMOVE_FAILED:
    case CACHE_EVENT_UPDATE_FAILED:
    case CACHE_EVENT_DEREF_FAILED: {
      // Unmarshal the error code
      ink_assert(((len - flen) == sizeof(int32_t)));
      op_result_error = msg->moi.u32;
      if (mh->NeedByteSwap())
        ats_swap32((uint32_t *)&op_result_error);
      op_result_error = -op_result_error;
      break;
    }
    default: {
      ink_release_assert(!"invalid moi data for received msg");
      break;
    }
    } // end of switch
  }
  // See if this response is still expected (expected case == yes)

  unsigned int hash = FOLDHASH(ch->machine->ip, msg->seq_number);
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  if (MUTEX_TAKE_TRY_LOCK(remoteCacheContQueueMutex[hash], thread)) {
    // Find it in pending list

    CacheContinuation *c = find_cache_continuation(msg->seq_number, ch->machine->ip);
    if (!c) {
      // Reply took to long, response no longer expected.
      MUTEX_UNTAKE_LOCK(remoteCacheContQueueMutex[hash], thread);
      Debug("cluster_timeout", "0cache reply timeout: %d", msg->seq_number);
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT);
      if (ci.valid())
        ci.destroy();
      return;
    }

    // Update remote ram cache hit flag
    if (msg->result == CACHE_EVENT_OPEN_READ)
      c->read_cluster_vc->set_ram_cache_hit(msg->is_ram_cache_hit);

    // Try to send the message

    MUTEX_TRY_LOCK(lock, c->mutex, thread);

    // Failed to acquire lock, defer

    if (!lock.is_locked()) {
      MUTEX_UNTAKE_LOCK(remoteCacheContQueueMutex[hash], thread);
      goto Lretry;
    }
    c->result_error = op_result_error;

    // send message, release lock

    c->freeMsgBuffer();
    if (ci.valid()) {
      // Unmarshaled CacheHTTPInfo contained in reply message, copy it.
      c->setMsgBufferLen(len, iob);
      c->ic_new_info = ci;
    }
    msg->seq_number = len; // HACK ALERT: reusing variable
    c->handleEvent(CACHE_EVENT_RESPONSE_MSG, data);

  } else {
  // Failed to wake it up, defer by creating a timed continuation

  Lretry:
    CacheContinuation *c = CacheContinuation::cacheContAllocator_alloc();
    c->mutex             = new_ProxyMutex();
    c->seq_number        = msg->seq_number;
    c->target_ip         = ch->machine->ip;
    SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::handleReplyEvent);
    c->start_time = Thread::get_hrtime();
    c->result     = msg->result;
    if (event_is_open(msg->result))
      c->token = msg->token;
    if (ci.valid()) {
      // Unmarshaled CacheHTTPInfo contained in reply message, copy it.
      c->setMsgBufferLen(len, iob);
      c->ic_new_info = ci;
    }
    c->result_error = op_result_error;
    eventProcessor.schedule_in(c, CACHE_RETRY_PERIOD, ET_CACHE_CONT_SM);
  }
}

////////////////////////////////////////////////////////////////////////
// handleReplyEvent()
//   If we cannot acquire any of the locks to handle the response
//   inline, it is defered and later handled by this function.
////////////////////////////////////////////////////////////////////////
int
CacheContinuation::handleReplyEvent(int event, Event *e)
{
  (void)event;

  // take lock on outstanding message queue

  EThread *t        = e->ethread;
  unsigned int hash = FOLDHASH(target_ip, seq_number);

  if (!MUTEX_TAKE_TRY_LOCK(remoteCacheContQueueMutex[hash], t)) {
    e->schedule_in(CACHE_RETRY_PERIOD);
    return EVENT_CONT;
  }

  LOG_EVENT_TIME(start_time, cntlck_acquire_time_dist, cntlck_acquire_events);

  // See if this response is still expected

  CacheContinuation *c = find_cache_continuation(seq_number, target_ip);
  if (c) {
    // Acquire the lock to the continuation mutex

    MUTEX_TRY_LOCK(lock, c->mutex, e->ethread);
    if (!lock.is_locked()) {
      // If we fail to acquire the lock, reschedule

      MUTEX_UNTAKE_LOCK(remoteCacheContQueueMutex[hash], t);
      e->schedule_in(CACHE_RETRY_PERIOD);
      return EVENT_CONT;
    }

    // If unmarshalled CacheHTTPInfo exists, pass it along

    if (ic_new_info.valid()) {
      c->freeMsgBuffer();
      c->setMsgBufferLen(getMsgBufferLen(), getMsgBufferIOBData());
      c->ic_new_info = ic_new_info;
      ic_new_info.clear();
    }
    // send message, release lock

    c->handleEvent(CACHE_EVENT_RESPONSE, this);

  } else {
    MUTEX_UNTAKE_LOCK(remoteCacheContQueueMutex[hash], t);
    Debug("cluster_timeout", "cache reply timeout: %d", seq_number);
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT);
  }

  // Free this continuation

  cacheContAllocator_free(this);
  return EVENT_DONE;
}

//////////////////////////////////////////////////////////////////////////
// remoteOpEvent()
//   On the requesting node, handle the timeout and response to the user.
//   There may be two CacheContinuations involved:
//    1) One waiting to respond to the user.
//       This case is CACHE_EVENT_RESPONSE_MSG which is handled
//       inline (without delay).
//    2) One which is carrying the response from the remote machine which
//       has been delayed for a lock.  This case is CACHE_EVENT_RESPONSE.
//////////////////////////////////////////////////////////////////////////
int
CacheContinuation::remoteOpEvent(int event_code, Event *e)
{
  ink_assert(magicno == (int)MagicNo);
  int event = event_code;
  ink_hrtime now;
  if (start_time) {
    int res;
    if (event != EVENT_INTERVAL) {
      if (event == CACHE_EVENT_RESPONSE) {
        CacheContinuation *ccont = (CacheContinuation *)e;
        res                      = ccont->result;
      } else {
        CacheOpReplyMsg *rmsg = (CacheOpReplyMsg *)e;
        res                   = rmsg->result;
      }
      if ((res == CACHE_EVENT_LOOKUP) || (res == CACHE_EVENT_LOOKUP_FAILED)) {
        now = Thread::get_hrtime();
        CLUSTER_SUM_DYN_STAT(CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT, now - start_time);
        LOG_EVENT_TIME(start_time, lkrmt_callback_time_dist, lkrmt_cache_callbacks);
      } else {
        now = Thread::get_hrtime();
        CLUSTER_SUM_DYN_STAT(CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT, now - start_time);
        LOG_EVENT_TIME(start_time, rmt_callback_time_dist, rmt_cache_callbacks);
      }
    }
    start_time = 0;
  }
  // for CACHE_EVENT_RESPONSE/XXX the lock was acquired at the higher level
  intptr_t return_error  = 0;
  ClusterVCToken *pToken = NULL;

retry:

  switch (event) {
  default:
    ink_assert(!"bad case");
    return EVENT_DONE;

  case EVENT_INTERVAL: {
    unsigned int hash = FOLDHASH(target_ip, seq_number);

    MUTEX_TRY_LOCK(queuelock, remoteCacheContQueueMutex[hash], e->ethread);
    if (!queuelock.is_locked()) {
      e->schedule_in(CACHE_RETRY_PERIOD);
      return EVENT_CONT;
    }
    // we are not yet enqueued on the list of outstanding operations

    if (!remoteCacheContQueue[hash].in(this)) {
      remoteCacheContQueue[hash].enqueue(this);
      ink_assert(timeout == e);
      MUTEX_RELEASE(queuelock);
      e->schedule_in(cache_cluster_timeout);
      return EVENT_CONT;
    }
    // a timeout has occurred

    if (find_cache_continuation(seq_number, target_ip)) {
      // Valid timeout
      MUTEX_RELEASE(queuelock);

      Debug("cluster_timeout", "cluster op timeout %d", seq_number);
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_REMOTE_OP_TIMEOUTS_STAT);
      request_timeout = true;
      timeout         = 0;
      //
      // Post error completion now and defer deallocation of
      // the continuation until we receive the reply or the
      // target node goes down.
      //
      if (!action.cancelled)
        action.continuation->handleEvent(result, (void *)-ECLUSTER_OP_TIMEOUT);
      action.cancelled = 1;

      if (target_machine->dead) {
        event = CACHE_EVENT_RESPONSE_MSG;
        goto retry;
      } else {
        timeout = e;
        e->schedule_in(cache_cluster_timeout);
        return EVENT_DONE;
      }

    } else {
      // timeout not expected for continuation; log and ignore
      MUTEX_RELEASE(queuelock);
      Debug("cluster_timeout", "unknown cluster op timeout %d", seq_number);
      Note("Unexpected CacheCont timeout, [%u.%u.%u.%u] seqno=%d", DOT_SEPARATED(target_ip), seq_number);
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_REMOTE_OP_TIMEOUTS_STAT);
      return EVENT_DONE;
    }
  }

  case CACHE_EVENT_RESPONSE:
  case CACHE_EVENT_RESPONSE_MSG: {
    // the response has arrived, cancel timeout

    if (timeout) {
      timeout->cancel();
      timeout = 0;
    }
    // remove from the pending queue
    unsigned int hash = FOLDHASH(target_ip, seq_number);

    remoteCacheContQueue[hash].remove(this);
    MUTEX_UNTAKE_LOCK(remoteCacheContQueueMutex[hash], this_ethread());
    // Fall through
  }

  case CACHE_EVENT_RESPONSE_RETRY: {
    // determine result code

    CacheContinuation *c = (CacheContinuation *)e;
    CacheOpReplyMsg *msg = (CacheOpReplyMsg *)e;
    if (event == CACHE_EVENT_RESPONSE_MSG) {
      result = (request_timeout ? result : msg->result);
      pToken = (request_timeout ? &token : &msg->token);
    } else if (event == CACHE_EVENT_RESPONSE) {
      result = (request_timeout ? result : c->result);
      pToken = &c->token;
    } else if (event == CACHE_EVENT_RESPONSE_RETRY) {
      pToken = &token;
    } else {
      ink_release_assert(!"remoteOpEvent bad event code");
    }

    // handle response

    if (result == CACHE_EVENT_LOOKUP) {
      callback_user(result, 0);
      return EVENT_DONE;

    } else if (event_is_open(result)) {
      bool read_op = ((request_opcode == CACHE_OPEN_READ) || (request_opcode == CACHE_OPEN_READ_LONG));
      if (read_op) {
        ink_release_assert(read_cluster_vc->pending_remote_fill > 1);
        read_cluster_vc->pending_remote_fill = 0;

        have_all_data = pToken->is_clear(); // no conn implies all data
        if (have_all_data) {
          read_cluster_vc->have_all_data = 1;
        } else {
          read_cluster_vc->have_all_data = 0;
        }
        // Move CacheHTTPInfo reply data into VC
        read_cluster_vc->marshal_buf = this->getMsgBufferIOBData();
        read_cluster_vc->alternate   = this->ic_new_info;
        this->ic_new_info.clear();
        ink_release_assert(read_cluster_vc->alternate.object_size_get());

        if (!action.cancelled) {
          ClusterVConnection *target_vc = read_cluster_vc;
          callback_user(result, target_vc); // "this" is deallocated
          target_vc->allow_remote_close();
        } else {
          read_cluster_vc->allow_remote_close();
          read_cluster_vc->do_io(VIO::ABORT);
          cacheContAllocator_free(this);
        }

      } else {
        ink_assert(result == CACHE_EVENT_OPEN_WRITE);
        ink_assert(!pToken->is_clear());

        ClusterVConnection *result_vc = write_cluster_vc;
        if (!action.cancelled) {
          callback_user(result, result_vc);
          result_vc->allow_remote_close();
        } else {
          result_vc->allow_remote_close();
          result_vc->do_io(VIO::ABORT);
          cacheContAllocator_free(this);
        }
      }
      return EVENT_DONE;
    }
    break;
  } // End of case
  } // End of switch

  // Handle failure cases

  if (result == CACHE_EVENT_LOOKUP_FAILED) {
    // check for local probes

    ClusterMachine *m = cluster_machine_at_depth(cache_hash(url_md5));

    // if the current configuration indicates that this
    // machine is the master (or the owner machine has failed), go to
    // the local machine.  Also if PROBE_LOCAL_CACHE_LAST.
    //
    int len        = getMsgBufferLen();
    char *hostname = (len ? getMsgBuffer() : 0);

    if (!m || PROBE_LOCAL_CACHE_LAST) {
      SET_HANDLER((CacheContHandler)&CacheContinuation::probeLookupEvent);
      CacheKey key(url_md5);

      Cache *call_cache = caches[frag_type];
      call_cache->lookup(this, &key, frag_type, hostname, len);
      return EVENT_DONE;
    }
    if (PROBE_LOCAL_CACHE_FIRST) {
      callback_user(CACHE_EVENT_LOOKUP_FAILED, 0);
    } else {
      SET_HANDLER((CacheContHandler)&CacheContinuation::probeLookupEvent);
      CacheKey key(url_md5);

      Cache *call_cache = caches[frag_type];
      call_cache->lookup(this, &key, frag_type, hostname, len);
    }
    return EVENT_DONE;

  } else {
    // Handle failure of all ops except for lookup

    ClusterVConnection *cacheable_vc = 0;
    if ((request_opcode == CACHE_OPEN_READ_LONG) && !pToken->is_clear()) {
      ink_assert(read_cluster_vc && !write_cluster_vc);
      //
      // OPEN_READ_LONG has failed, but the remote node was able to
      // establish an OPEN_WRITE_LONG connection.
      // Convert the cluster read VC to a write VC and insert it
      // into the global write VC cache.  This will allow us to
      // locally resolve the subsequent OPEN_WRITE_LONG request.
      //

      // Note: We do not allow remote close on this VC while
      //       it resides in cache
      //
      read_cluster_vc->set_type(CLUSTER_OPT_CONN_WRITE);
      // FIX ME. ajitb 12/21/99
      // Compiler bug in CC: WorkShop Compilers 5.0 98/12/15 C++ 5.0
      // Does not accept assignment of ((Continuation *) NULL)
      {
        Continuation *temp       = NULL;
        read_cluster_vc->action_ = temp;
      }
      if (!GlobalOpenWriteVCcache->insert(&url_md5, read_cluster_vc)) {
        // Unable to insert VC into cache, try later
        cacheable_vc = read_cluster_vc;
      }
      read_cluster_vc = 0;
    }
    if (read_cluster_vc) {
      read_cluster_vc->remote_closed = 0; // send remote close
      read_cluster_vc->allow_remote_close();
      read_cluster_vc->do_io(VIO::ABORT);
      read_cluster_vc = 0;
    }
    if (write_cluster_vc) {
      write_cluster_vc->remote_closed = 0; // send remote close
      write_cluster_vc->allow_remote_close();
      write_cluster_vc->do_io(VIO::ABORT);
      write_cluster_vc = 0;
    }
    if (!request_timeout) {
      if (!return_error) {
        return_error = result_error;
      }
      if (cacheable_vc) {
        insert_cache_callback_user(cacheable_vc, result, (void *)return_error);
      } else {
        callback_user(result, (void *)return_error);
      }
    } else {
      // callback already made at timeout, just free continuation
      if (cacheable_vc) {
        cacheable_vc->allow_remote_close();
        cacheable_vc->do_io(VIO::CLOSE);
        cacheable_vc = 0;
      }
      cacheContAllocator_free(this);
    }
    return EVENT_DONE;
  }
}

//////////////////////////////////////////////////////////////////////////
// probeLookupEvent()
//   After a local probe, return the response to the client and cleanup.
//////////////////////////////////////////////////////////////////////////

int
CacheContinuation::probeLookupEvent(int event, void * /* d ATS_UNUSED */)
{
  ink_assert(magicno == (int)MagicNo);
  callback_user(event, 0);
  return EVENT_DONE;
}

///////////////////////////////////////////////////////////
// lookupEvent()
//   Result of a local lookup for PROBE_LOCAL_CACHE_FIRST
///////////////////////////////////////////////////////////
int
CacheContinuation::lookupEvent(int /* event ATS_UNUSED */, void * /* d ATS_UNUSED */)
{
  ink_release_assert(!"Invalid call CacheContinuation::lookupEvent");
  return EVENT_DONE;
}

//////////////////////////////////////////////////////////////////////////
// do_remote_lookup()
//   If the object is supposed to be on a remote machine, probe there.
//   Returns: Non zero (Action *) if a probe was initiated
//            Zero (Action *) if no probe
//////////////////////////////////////////////////////////////////////////
Action *
CacheContinuation::do_remote_lookup(Continuation *cont, const CacheKey *key, CacheContinuation *c, CacheFragType ft,
                                    const char *hostname, int hostname_len)
{
  int probe_depth                                                = 0;
  ClusterMachine *past_probes[CONFIGURATION_HISTORY_PROBE_DEPTH] = {0};
  int mlen            = op_to_sizeof_fixedlen_msg(CACHE_LOOKUP_OP) + ((hostname && hostname_len) ? hostname_len : 0);
  CacheLookupMsg *msg = (CacheLookupMsg *)ALLOCA_DOUBLE(mlen);
  msg->init();

  if (key) {
    msg->url_md5 = *key;
  } else {
    ink_assert(c);
    msg->url_md5 = c->url_md5;
  }

  ClusterMachine *m = NULL;

  if (cache_migrate_on_demand) {
    m = cluster_machine_at_depth(cache_hash(msg->url_md5), c ? &c->probe_depth : &probe_depth, c ? c->past_probes : past_probes);
  } else {
    // If migrate-on-demand is off, do not probe beyond one level.

    if (c && c->probe_depth)
      return (Action *)0;
    m = cluster_machine_at_depth(cache_hash(msg->url_md5));
    if (c)
      c->probe_depth = 1;
  }

  if (!m)
    return (Action *)0;
  ClusterHandler *ch = m->pop_ClusterHandler();
  if (!ch)
    return (Action *)0;

  // If we do not have a continuation, build one

  if (!c) {
    c              = cacheContAllocator_alloc();
    c->mutex       = cont->mutex;
    c->probe_depth = probe_depth;
    memcpy(c->past_probes, past_probes, sizeof(past_probes));
  }
  c->ch = ch;
  // Save hostname data in case we need to do a local lookup.
  if (hostname && hostname_len) {
    // Alloc buffer, copy hostname data and attach to continuation
    c->setMsgBufferLen(hostname_len);
    c->allocMsgBuffer();
    memcpy(c->getMsgBuffer(), hostname, hostname_len);
  }

  c->url_md5          = msg->url_md5;
  c->action.cancelled = false;
  c->action           = cont;
  c->start_time       = Thread::get_hrtime();
  SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::remoteOpEvent);
  c->result = CACHE_EVENT_LOOKUP_FAILED;

  // set up sequence number so we can find this continuation

  c->target_ip    = m->ip;
  c->seq_number   = new_cache_sequence_number();
  msg->seq_number = c->seq_number;
  c->frag_type    = ft;
  msg->frag_type  = ft;

  // establish timeout for lookup

  unsigned int hash = FOLDHASH(c->target_ip, c->seq_number);
  MUTEX_TRY_LOCK(queuelock, remoteCacheContQueueMutex[hash], this_ethread());
  if (!queuelock.is_locked()) {
    // failed to acquire lock: no problem, retry later
    c->timeout = eventProcessor.schedule_in(c, CACHE_RETRY_PERIOD, ET_CACHE_CONT_SM);
  } else {
    remoteCacheContQueue[hash].enqueue(c);
    MUTEX_RELEASE(queuelock);
    c->timeout = eventProcessor.schedule_in(c, cache_cluster_timeout, ET_CACHE_CONT_SM);
  }

  char *data;
  int len;
  int vers = CacheLookupMsg::protoToVersion(m->msg_proto_major);

  if (vers == CacheLookupMsg::CACHE_LOOKUP_MESSAGE_VERSION) {
    msg->seq_number = c->seq_number;
    data            = (char *)msg;
    len             = mlen;
    if (hostname && hostname_len) {
      memcpy(msg->moi.byte, hostname, hostname_len);
    }
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheLookupMsg bad msg version");
  }

// send the message

#ifdef CACHE_MSG_TRACE
  log_cache_op_sndmsg(msg.seq_number, 0, "cache_lookup");
#endif
  clusterProcessor.invoke_remote(c->ch, CACHE_LOOKUP_CLUSTER_FUNCTION, data, len);
  return &c->action;
}

////////////////////////////////////////////////////////////////////////////
// cache_lookup_ClusterFunction()
//   This function is invoked on a remote machine to do a remote lookup.
//   It unmarshals the URL and does a local lookup, with its own
//   continuation set to CacheContinuation::replyLookupEvent()
////////////////////////////////////////////////////////////////////////////
void
cache_lookup_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  (void)len;
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  ////////////////////////////////////////////////////////
  // Note: we are running on the ET_CLUSTER thread
  ////////////////////////////////////////////////////////

  CacheLookupMsg *msg      = (CacheLookupMsg *)data;
  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;

  if (mh->GetMsgVersion() != CacheLookupMsg::CACHE_LOOKUP_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"cache_lookup_ClusterFunction() bad msg version");
  }

  if (mh->NeedByteSwap())
    msg->SwapBytes();

  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CACHE_OUTSTANDING_STAT);

  CacheContinuation *c = CacheContinuation::cacheContAllocator_alloc();
  c->mutex             = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, c->mutex, this_ethread());
  c->no_reply_message = (msg->seq_number == CACHE_NO_RESPONSE);
  c->seq_number       = msg->seq_number;
  c->from             = ch->machine;
  c->url_md5          = msg->url_md5;
  SET_CONTINUATION_HANDLER(c, (CacheContHandler)&CacheContinuation::replyLookupEvent);

  CacheKey key(msg->url_md5);
#ifdef CACHE_MSG_TRACE
  log_cache_op_msg(msg->seq_number, 0, "cache_lookup");
#endif

  // Extract hostname data if passed.

  char *hostname;
  int hostname_len = len - op_to_sizeof_fixedlen_msg(CACHE_LOOKUP_OP);
  hostname         = (hostname_len ? (char *)msg->moi.byte : 0);

  // Note: Hostname data invalid after return from lookup
  Cache *call_cache = caches[msg->frag_type];
  call_cache->lookup(c, &key, (CacheFragType)msg->frag_type, hostname, hostname_len);
}

/////////////////////////////////////////////////////////////////////////
// replyLookupEvent()
//   This function handles the result of a lookup on a remote machine.
//   It packages up the result and sends it back to the calling machine.
/////////////////////////////////////////////////////////////////////////
int
CacheContinuation::replyLookupEvent(int event, void * /* d ATS_UNUSED */)
{
  ink_hrtime now;
  now = Thread::get_hrtime();
  CLUSTER_SUM_DYN_STAT(CLUSTER_CACHE_CALLBACK_TIME_STAT, now - start_time);
  LOG_EVENT_TIME(start_time, callback_time_dist, cache_callbacks);

  int vers = CacheOpReplyMsg::protoToVersion(from->msg_proto_major);
  if (vers == CacheOpReplyMsg::CACHE_OP_REPLY_MESSAGE_VERSION) {
    CacheOpReplyMsg *msg;
    int flen = CacheOpReplyMsg::sizeof_fixedlen_msg();
    msg      = (CacheOpReplyMsg *)ALLOCA_DOUBLE(flen);
    msg->init();
    CLUSTER_DECREMENT_DYN_STAT(CLUSTER_CACHE_OUTSTANDING_STAT);
    int len = flen - sizeof(msg->token);

    if (!no_reply_message) {
      msg->seq_number = seq_number;
      msg->result     = event;
#ifdef CACHE_MSG_TRACE
      log_cache_op_sndmsg(seq_number, event, "cache_result");
#endif
      clusterProcessor.invoke_remote(ch, CACHE_OP_RESULT_CLUSTER_FUNCTION, msg, len);
    }
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"replyLookupEvent() bad msg version");
  }

  // Free up everything

  cacheContAllocator_free(this);
  return EVENT_DONE;
}

int32_t
CacheContinuation::getObjectSize(VConnection *vc, int opcode, CacheHTTPInfo *ret_ci)
{
  CacheHTTPInfo *ci   = 0;
  int64_t object_size = 0;

  if ((opcode == CACHE_OPEN_READ_LONG) || (opcode == CACHE_OPEN_READ_BUFFER_LONG)) {
    ((CacheVC *)vc)->get_http_info(&ci);
    if (ci) {
      object_size = ci->object_size_get();

    } else {
      ci          = 0;
      object_size = 0;
    }

  } else {
    object_size = ((CacheVC *)vc)->get_object_size();
  }

  if (ret_ci && !ret_ci->valid()) {
    CacheHTTPInfo new_ci;
    new_ci.create();
    if (ci) {
      // Initialize copy
      new_ci.copy(ci);
    } else {
      new_ci.object_size_set(object_size);
    }
    new_ci.m_alt->m_writeable = 1;
    ret_ci->copy_shallow(&new_ci);
  }
  ink_release_assert(object_size);
  return object_size;
}

//////////////////////////////////////////////////////////////////////////
// insert_cache_callback_user()
//  Insert write VC into global cache prior to performing user callback.
//////////////////////////////////////////////////////////////////////////
void
CacheContinuation::insert_cache_callback_user(ClusterVConnection *vc, int res, void *e)
{
  if (GlobalOpenWriteVCcache->insert(&url_md5, vc)) {
    // Inserted
    callback_user(res, e);

  } else {
    // Unable to insert, try later
    result          = res;
    callback_data   = e;
    callback_data_2 = (void *)vc;
    SET_HANDLER((CacheContHandler)&CacheContinuation::insertCallbackEvent);
    eventProcessor.schedule_imm(this, ET_CACHE_CONT_SM);
  }
}

int
CacheContinuation::insertCallbackEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (GlobalOpenWriteVCcache->insert(&url_md5, (ClusterVConnection *)callback_data_2)) {
    // Inserted
    callback_user(result, callback_data);

  } else {
    // Unable to insert, try later
    eventProcessor.schedule_imm(this, ET_CACHE_CONT_SM);
  }
  return EVENT_DONE;
}

///////////////////////////////////////////////////////////////////
// callback_user()
//  Invoke handleEvent on the given continuation (cont) with
//    considerations for Action.
///////////////////////////////////////////////////////////////////
void
CacheContinuation::callback_user(int res, void *e)
{
  EThread *et = this_ethread();

  if (!is_ClusterThread(et)) {
    MUTEX_TRY_LOCK(lock, mutex, et);
    if (lock.is_locked()) {
      if (!action.cancelled) {
        action.continuation->handleEvent(res, e);
      }
      cacheContAllocator_free(this);

    } else {
      // Unable to acquire lock, retry later
      defer_callback_result(res, e);
    }
  } else {
    // Can not post completion on ET_CLUSTER thread.
    defer_callback_result(res, e);
  }
}

void
CacheContinuation::defer_callback_result(int r, void *e)
{
  result        = r;
  callback_data = e;
  SET_HANDLER((CacheContHandler)&CacheContinuation::callbackResultEvent);
  eventProcessor.schedule_imm(this, ET_CACHE_CONT_SM);
}

int
CacheContinuation::callbackResultEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (!action.cancelled)
    action.continuation->handleEvent(result, callback_data);
  cacheContAllocator_free(this);
  return EVENT_DONE;
}

//-----------------------------------------------------------------
// CacheContinuation static member functions
//-----------------------------------------------------------------

///////////////////////////////////////////////////////////////////////
// cacheContAllocator_alloc()
///////////////////////////////////////////////////////////////////////
CacheContinuation *
CacheContinuation::cacheContAllocator_alloc()
{
  return cacheContAllocator.alloc();
}

///////////////////////////////////////////////////////////////////////
// cacheContAllocator_free()
///////////////////////////////////////////////////////////////////////
void
CacheContinuation::cacheContAllocator_free(CacheContinuation *c)
{
  ink_assert(c->magicno == (int)MagicNo);
  //  ink_assert(!c->cache_op_ClusterFunction);
  c->magicno = -1;
#ifdef ENABLE_TIME_TRACE
  c->start_time = 0;
#endif
  c->free();
  c->mutex = NULL;
  // FIX ME. ajitb 12/21/99
  // Compiler bug in CC: WorkShop Compilers 5.0 98/12/15 C++ 5.0
  // Does not accept assignment of ((Continuation *) NULL)
  {
    Continuation *temp = NULL;
    c->action          = temp;
  }
  c->tunnel_mutex = NULL;
  cacheContAllocator.free(c);
}

/////////////////////////////////////////////////////////////////////////
// callback_failure()
//   Post error completion using a continuation.
/////////////////////////////////////////////////////////////////////////
Action *
CacheContinuation::callback_failure(Action *a, int result, int err, CacheContinuation *this_cc)
{
  CacheContinuation *cc;
  if (!this_cc) {
    cc         = cacheContAllocator_alloc();
    cc->mutex  = a->mutex;
    cc->action = *a;

  } else {
    cc = this_cc;
  }
  cc->result       = result;
  cc->result_error = err;
  SET_CONTINUATION_HANDLER(cc, (CacheContHandler)&CacheContinuation::callbackEvent);
  eventProcessor.schedule_imm(cc, ET_CACHE_CONT_SM);
  return &cc->action;
}

///////////////////////////////////////////////////////////////////////
// callbackEvent()
//  Invoke callback and deallocate continuation.
///////////////////////////////////////////////////////////////////////
int
CacheContinuation::callbackEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (!action.cancelled)
    action.continuation->handleEvent(result, (void *)(intptr_t)result_error);
  cacheContAllocator_free(this);
  return EVENT_DONE;
}

//------------------------------------------------------------------
// File static functions
//------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////
// find_cache_continuation()
//   Find a currently pending cache continuation expecting a response.
//   Requires taking the lock on the remoteCacheContQueueMutex first.
////////////////////////////////////////////////////////////////////////
static CacheContinuation *
find_cache_continuation(unsigned int seq_number, unsigned int from_ip)
{
  unsigned int hash        = FOLDHASH(from_ip, seq_number);
  CacheContinuation *c     = NULL;
  CacheContinuation *lastc = NULL;
  for (c = (CacheContinuation *)remoteCacheContQueue[hash].head; c; c = (CacheContinuation *)c->link.next) {
    if (seq_number == c->seq_number && from_ip == c->target_ip) {
      if (lastc) {
        ink_release_assert(c->link.prev == lastc);
      } else {
        ink_release_assert(!c->link.prev);
      }
      break;
    }

    lastc = c;
  }
  return c;
}

/////////////////////////////////////////////////////////////////////////////
// new_cache_sequence_number()
//  Generate unique request sequence numbers
/////////////////////////////////////////////////////////////////////////////
static unsigned int
new_cache_sequence_number()
{
  unsigned int res = 0;

  do {
    res = (unsigned int)ink_atomic_increment(&cluster_sequence_number, 1);
  } while (!res);

  return res;
}

/***************************************************************************/
#ifdef OMIT
/***************************************************************************/
/////////////////////////////////////////////////////////////////////////////
// forwardEvent()
//   for migrate-on-demand, make a connection between the
//   the node which has the object and the node which should have it.
//
//   prepared for either OPEN_READ (from current owner)
//   or OPEN_WRITE (from new owner)
/////////////////////////////////////////////////////////////////////////////
int
CacheContinuation::forwardEvent(int event, VConnection *c)
{
  int ret    = EVENT_CONT;
  cluster_vc = 0;

  cache_read = false;
  switch (event) {
  default:
    ink_assert(!"bad case");
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    ret = EVENT_DONE;
    break;
  case CACHE_EVENT_OPEN_WRITE:
    cluster_vc = c;
    break;
  case CACHE_EVENT_OPEN_READ_FAILED:
    cache_read = true;
    ret        = EVENT_DONE;
    break;
  case CACHE_EVENT_OPEN_READ:
    cache_read = true;
    cluster_vc = c;
    break;
  }
  SET_HANDLER((CacheContHandler)&CacheContinuation::forwardWaitEvent);
  return ret;
}

////////////////////////////////////////////////////////////////////////
// forwardWaitEvent()
//   For migrate-on-demand, make a connection as above (forwardEvent)
//   second either OPEN_READ or OPEN_WRITE,
//   the data for the first is stored in (cluster_vc,cache_read)
////////////////////////////////////////////////////////////////////////
int
CacheContinuation::forwardWaitEvent(int event, VConnection *c)
{
  int ret         = EVENT_CONT;
  int res         = CACHE_EVENT_OPEN_READ_FAILED;
  void *res_data  = NULL;
  VConnection *vc = NULL;

  switch (event) {
  default:
    ink_assert(!"bad case");
  case CACHE_EVENT_OPEN_WRITE_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED:
    ret = EVENT_DONE;
    break;
  case CACHE_EVENT_OPEN_WRITE:
  case CACHE_EVENT_OPEN_READ:
    vc = c;
    break;
  }
  VConnection *read_vc  = (cache_read ? cluster_vc : vc);
  VConnection *write_vc = (!cache_read ? cluster_vc : vc);

  res      = read_vc ? CACHE_EVENT_OPEN_READ : CACHE_EVENT_OPEN_READ_FAILED;
  res_data = read_vc;

  // if the read and write are sucessful, tunnel the read to the write
  if (read_vc && write_vc) {
    res_data = new VCTee(read_vc, write_vc, vio);
    if (vio) { // CACHE_EVENT_OPEN_READ_VIO
      res      = event;
      res_data = &((VCTee *)read_vc)->vio;
    }
  }
  // if the read is sucessful return it to the user
  //
  c->handleEvent(res, res_data);
  return ret;
}

/////////////////////////////////////////////////////////////////////
// tunnelEvent()
//   If the reply requires data, tunnel the data from the cache
//   to the cluster.
/////////////////////////////////////////////////////////////////////
int
CacheContinuation::tunnelEvent(int event, VConnection *vc)
{
  int ret       = EVENT_DONE;
  int flen      = CacheOpReplyMsg::sizeof_fixedlen_msg(); // include token
  int len       = 0;
  bool read_buf = ((request_opcode == CACHE_OPEN_READ_BUFFER) || (request_opcode == CACHE_OPEN_READ_BUFFER_LONG));
  ink_release_assert(!read_buf);

  CacheOpReplyMsg rmsg;
  CacheOpReplyMsg *msg = &rmsg;
  msg->result          = result;
  msg->seq_number      = seq_number;
  msg->token           = token;
  int expect_reply     = 1;

  if (event == CLUSTER_EVENT_OPEN) {
    if (cache_read) {
      if (read_buf) {
        ink_assert(have_all_data || (readahead_vio == &((CacheVConnection *)cluster_vc)->vio));
        write_cluster_vc = (ClusterVConnection *)vc;

        if (have_all_data) {
          msg->token.clear(); // Tell sender no conn established
        } else {
          msg->token = token; // Tell sender conn established
          setupReadBufTunnel(cluster_vc, vc);
        }

      } else {
        OneWayTunnel *pOWT = OneWayTunnel::OneWayTunnel_alloc();
        pOWT->init(cluster_vc, vc, NULL, nbytes, this->mutex);
        --expect_reply;
      }

      ////////////////////////////////////////////////////////
      // cache_read requires CacheHTTPInfo in reply message.
      ////////////////////////////////////////////////////////
      int res;
      CacheHTTPInfo *ci;

      if (!cache_vc_info) {
        // OPEN_READ case
        (void)getObjectSize(cluster_vc, request_opcode, &cache_vc_info);
      }
      ci = cache_vc_info;

      // Determine data length and allocate
      len                    = ci->marshal_length();
      CacheOpReplyMsg *reply = (CacheOpReplyMsg *)ALLOCA_DOUBLE(flen + len);

      // Initialize reply message header
      *reply = *msg;

      // Marshal response data into reply message
      res = ci->marshal((char *)reply->moi.byte, len);
      ink_assert(res > 0);

      // Make reply message the current message
      msg = reply;

    } else {
      OneWayTunnel *pOWT = OneWayTunnelAllocator.alloc();
      pOWT->init(vc, cluster_vc, NULL, nbytes, this->mutex);
      --expect_reply;
    }
    ret = EVENT_CONT;
  } else {
    ink_release_assert(event == CLUSTER_EVENT_OPEN_FAILED);
    msg->result = CACHE_EVENT_SET_FAILED(result);

    if (read_buf) {
      Debug("cluster_timeout", "unable to make cluster connection2");
      initial_buf     = 0; // Do not send data
      initial_bufsize = 0;

      if (!have_all_data) {
        // Shutdown cache connection and free MIOBuffer
        MIOBuffer *mbuf = readahead_vio->buffer.writer();
        cluster_vc->do_io(VIO::CLOSE);
        free_MIOBuffer(mbuf);
      }
    } else {
      Debug("cluster_timeout", "unable to make cluster connection2A");
      cluster_vc->do_io(VIO::CLOSE);
    }
    len = 0 - (int)sizeof(msg->token);
    --expect_reply;
  }

  int vers = CacheOpReplyMsg::protoToVersion(from->msg_proto_major);
  if (vers == CacheOpReplyMsg::CACHE_OP_REPLY_MESSAGE_VERSION) {
    if (read_buf) {
      // Transmit reply message and object data in same cluster message
      clusterProcessor.invoke_remote_data(from, CACHE_OP_RESULT_CLUSTER_FUNCTION, (void *)msg, (flen + len), initial_buf,
                                          initial_bufsize, cluster_vc_channel, &token, &CacheContinuation::disposeOfDataBuffer,
                                          (void *)this, CLUSTER_OPT_STEAL);

    } else {
      clusterProcessor.invoke_remote(from, CACHE_OP_RESULT_CLUSTER_FUNCTION, (void *)msg, (flen + len), CLUSTER_OPT_STEAL);
    }
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"tunnelEvent() bad msg version");
  }
  if (expect_reply <= 0)
    cacheContAllocator_free(this);
  return ret;
}

/////////////////////////////////////////////////////////////////////
// remoteConnectEvent()
//   If this was an open, make a connection on this side before
//   responding to the user.
/////////////////////////////////////////////////////////////////////
int
CacheContinuation::remoteConnectEvent(int event, VConnection *cvc)
{
  ClusterVConnection *vc = (ClusterVConnection *)cvc;

  if (event == CLUSTER_EVENT_OPEN) {
    if (result == CACHE_EVENT_OPEN_READ) {
      // Move CacheHTTPInfo reply data into VC
      vc->alternate = this->ic_new_info;
      this->ic_new_info.clear();
    }
    callback_user(result, vc);
    return EVENT_CONT;
  } else {
    Debug("cluster_cache", "unable to make cluster connection");
    callback_user(CACHE_EVENT_SET_FAILED(result), vc);
    return EVENT_DONE;
  }
}

/***************************************************************************/
#endif // OMIT
/***************************************************************************/

// End of ClusterCache.cc
