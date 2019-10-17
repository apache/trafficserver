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

/*
 * This file contains all the functions exported by the IOCore to the SDK.
 * Any IOCore symbol accessed by a plugin directly should be called in this
 * file to ensure that it gets exported as a global symbol in TS
 */

#include "tscore/ink_platform.h"
#include "ts/ts.h"
#include "ts/InkAPIPrivateIOCore.h"
#if defined(solaris) && !defined(__GNUC__)
#include "P_EventSystem.h" // I_EventSystem.h
#include "P_Net.h"         // I_Net.h
#else
#include "I_EventSystem.h"
#include "I_Net.h"
#endif
#include "I_Cache.h"
#include "I_HostDB.h"

// This assert is for internal API use only.
#if TS_USE_FAST_SDK
#define sdk_assert(EX) (void)(EX)
#else
#define sdk_assert(EX) ((void)((EX) ? (void)0 : _TSReleaseAssert(#EX, __FILE__, __LINE__)))
#endif

TSReturnCode
sdk_sanity_check_mutex(TSMutex mutex)
{
  if (mutex == nullptr) {
    return TS_ERROR;
  }

  ProxyMutex *mutexp = reinterpret_cast<ProxyMutex *>(mutex);

  if (mutexp->refcount() < 0) {
    return TS_ERROR;
  }
  if (mutexp->nthread_holding < 0) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_hostlookup_structure(TSHostLookupResult data)
{
  if (data == nullptr) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
sdk_sanity_check_iocore_structure(void *data)
{
  if (data == nullptr) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

// From InkAPI.cc
TSReturnCode sdk_sanity_check_continuation(TSCont cont);
TSReturnCode sdk_sanity_check_null_ptr(void const *ptr);

////////////////////////////////////////////////////////////////////
//
// Threads
//
////////////////////////////////////////////////////////////////////
struct INKThreadInternal : public EThread {
  INKThreadInternal() : EThread(DEDICATED, -1)
  {
    ink_mutex_init(&completion.lock);
    ink_cond_init(&completion.signal);
  }

  ~INKThreadInternal() override
  {
    ink_mutex_destroy(&completion.lock);
    ink_cond_destroy(&completion.signal);
  }

  TSThreadFunc func = nullptr;
  void *data        = nullptr;

  struct {
    ink_mutex lock;
    ink_cond signal;
    bool done = false;
  } completion;
};

static void *
ink_thread_trampoline(void *data)
{
  void *retval;
  INKThreadInternal *ithread = static_cast<INKThreadInternal *>(data);

  ithread->set_specific();
  retval = ithread->func(ithread->data);

  ink_mutex_acquire(&ithread->completion.lock);

  ithread->completion.done = true;
  ink_cond_broadcast(&ithread->completion.signal);

  ink_mutex_release(&ithread->completion.lock);
  return retval;
}

/*
 * INKqa12653. Return TSThread or NULL if error
 */
TSThread
TSThreadCreate(TSThreadFunc func, void *data)
{
  INKThreadInternal *thread;
  ink_thread tid = 0;

  thread = new INKThreadInternal;

  ink_assert(thread->event_types == 0);
  ink_assert(thread->mutex->thread_holding == thread);

  thread->func = func;
  thread->data = data;

  ink_thread_create(&tid, ink_thread_trampoline, (void *)thread, 1, 0, nullptr);
  if (!tid) {
    return (TSThread) nullptr;
  }

  return reinterpret_cast<TSThread>(thread);
}

// Wait for a thread to complete. When a thread calls TSThreadCreate,
// it becomes the owner of the thread's mutex. Since only the thread
// that locked a mutex should be allowed to unlock it (a condition
// that is enforced for PTHREAD_MUTEX_ERRORCHECK), if the application
// needs to delete the thread, it must first wait for the thread to
// complete.
void
TSThreadWait(TSThread thread)
{
  sdk_assert(sdk_sanity_check_iocore_structure(thread) == TS_SUCCESS);
  INKThreadInternal *ithread = reinterpret_cast<INKThreadInternal *>(thread);

  ink_mutex_acquire(&ithread->completion.lock);

  if (ithread->completion.done == false) {
    ink_cond_wait(&ithread->completion.signal, &ithread->completion.lock);
  }

  ink_mutex_release(&ithread->completion.lock);
}

TSThread
TSThreadInit()
{
  INKThreadInternal *thread;

  thread = new INKThreadInternal;

#ifdef DEBUG
  if (thread == nullptr) {
    return (TSThread) nullptr;
  }
#endif

  thread->set_specific();

  return reinterpret_cast<TSThread>(thread);
}

void
TSThreadDestroy(TSThread thread)
{
  sdk_assert(sdk_sanity_check_iocore_structure(thread) == TS_SUCCESS);

  INKThreadInternal *ithread = reinterpret_cast<INKThreadInternal *>(thread);

  // The thread must be destroyed by the same thread that created
  // it because that thread is holding the thread mutex.
  ink_release_assert(ithread->mutex->thread_holding == ithread);

  // If this thread was created by TSThreadCreate() rather than
  // TSThreadInit, then we must not destroy it before it's done.
  if (ithread->func) {
    ink_release_assert(ithread->completion.done == true);
  }

  delete ithread;
}

TSThread
TSThreadSelf(void)
{
  TSThread ithread = (TSThread)this_ethread();
  return ithread;
}

TSEventThread
TSEventThreadSelf(void)
{
  return reinterpret_cast<TSEventThread>(this_event_thread());
}

////////////////////////////////////////////////////////////////////
//
// Mutexes
//
////////////////////////////////////////////////////////////////////
TSMutex
TSMutexCreate()
{
  ProxyMutex *mutexp = new_ProxyMutex();
  mutexp->refcount_inc();

  // TODO: Remove this when allocations can never fail.
  sdk_assert(sdk_sanity_check_mutex((TSMutex)mutexp) == TS_SUCCESS);

  return (TSMutex)mutexp;
}

void
TSMutexDestroy(TSMutex m)
{
  sdk_assert(sdk_sanity_check_mutex(m) == TS_SUCCESS);
  ProxyMutex *mutexp = reinterpret_cast<ProxyMutex *>(m);
  // Decrement the refcount added in TSMutexCreate.  Delete if this
  // was the last ref count
  if (mutexp && mutexp->refcount_dec() == 0) {
    mutexp->free();
  }
}

/* The following two APIs are for Into work, actually, APIs of Mutex
   should allow plugins to manually increase or decrease the refcount
   of the mutex pointer, plugins may want more control of the creation
   and destroy of the mutex.*/
TSMutex
TSMutexCreateInternal()
{
  ProxyMutex *new_mutex = new_ProxyMutex();

  // TODO: Remove this when allocations can never fail.
  sdk_assert(sdk_sanity_check_mutex((TSMutex)new_mutex) == TS_SUCCESS);

  new_mutex->refcount_inc();
  return reinterpret_cast<TSMutex>(new_mutex);
}

int
TSMutexCheck(TSMutex mutex)
{
  ProxyMutex *mutexp = (ProxyMutex *)mutex;

  if (mutexp->refcount() < 0) {
    return -1;
  }
  if (mutexp->nthread_holding < 0) {
    return -1;
  }
  return 1;
}

void
TSMutexLock(TSMutex mutexp)
{
  sdk_assert(sdk_sanity_check_mutex(mutexp) == TS_SUCCESS);
  Ptr<ProxyMutex> proxy_mutex(reinterpret_cast<ProxyMutex *>(mutexp));
  MUTEX_TAKE_LOCK(proxy_mutex, this_ethread());
}

TSReturnCode
TSMutexLockTry(TSMutex mutexp)
{
  sdk_assert(sdk_sanity_check_mutex(mutexp) == TS_SUCCESS);
  Ptr<ProxyMutex> proxy_mutex(reinterpret_cast<ProxyMutex *>(mutexp));
  return (MUTEX_TAKE_TRY_LOCK(proxy_mutex, this_ethread()) ? TS_SUCCESS : TS_ERROR);
}

void
TSMutexUnlock(TSMutex mutexp)
{
  sdk_assert(sdk_sanity_check_mutex(mutexp) == TS_SUCCESS);
  Ptr<ProxyMutex> proxy_mutex(reinterpret_cast<ProxyMutex *>(mutexp));
  MUTEX_UNTAKE_LOCK(proxy_mutex, this_ethread());
}

/* VIOs */

void
TSVIOReenable(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  vio->reenable();
}

TSIOBuffer
TSVIOBufferGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return reinterpret_cast<TSIOBuffer>(vio->get_writer());
}

TSIOBufferReader
TSVIOReaderGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return reinterpret_cast<TSIOBufferReader>(vio->get_reader());
}

int64_t
TSVIONBytesGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return vio->nbytes;
}

void
TSVIONBytesSet(TSVIO viop, int64_t nbytes)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);
  sdk_assert(nbytes >= 0);

  VIO *vio    = (VIO *)viop;
  vio->nbytes = nbytes;
}

int64_t
TSVIONDoneGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return vio->ndone;
}

void
TSVIONDoneSet(TSVIO viop, int64_t ndone)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);
  sdk_assert(ndone >= 0);

  VIO *vio   = (VIO *)viop;
  vio->ndone = ndone;
}

int64_t
TSVIONTodoGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return vio->ntodo();
}

TSCont
TSVIOContGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return (TSCont)vio->cont;
}

TSVConn
TSVIOVConnGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return (TSVConn)vio->vc_server;
}

TSMutex
TSVIOMutexGet(TSVIO viop)
{
  sdk_assert(sdk_sanity_check_iocore_structure(viop) == TS_SUCCESS);

  VIO *vio = (VIO *)viop;
  return reinterpret_cast<TSMutex>(vio->mutex.get());
}

/* High Resolution Time */

ink_hrtime
INKBasedTimeGet()
{
  return Thread::get_hrtime();
}

/* UDP Connection Interface */

TSAction
INKUDPBind(TSCont contp, unsigned int ip, int port)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);

  struct sockaddr_in addr;
  ats_ip4_set(&addr, ip, htons(port));

  return reinterpret_cast<TSAction>(
    udpNet.UDPBind((Continuation *)contp, ats_ip_sa_cast(&addr), INK_ETHERNET_MTU_SIZE, INK_ETHERNET_MTU_SIZE));
}

TSAction
INKUDPSendTo(TSCont contp, INKUDPConn udp, unsigned int ip, int port, char *data, int64_t len)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);
  UDPPacket *packet   = new_UDPPacket();
  UDPConnection *conn = (UDPConnection *)udp;

  ats_ip4_set(&packet->to, ip, htons(port));

  IOBufferBlock *blockp = new_IOBufferBlock();
  blockp->alloc(BUFFER_SIZE_INDEX_32K);

  if (len > index_to_buffer_size(BUFFER_SIZE_INDEX_32K)) {
    len = index_to_buffer_size(BUFFER_SIZE_INDEX_32K) - 1;
  }

  memcpy(blockp->start(), data, len);
  blockp->fill(len);

  packet->append_block((IOBufferBlock *)blockp);
  /* (Jinsheng 11/27/00) set connection twice which causes:
     FATAL: ../../../proxy/iocore/UDPPacket.h:136:
     failed assert `!m_conn` */

  /* packet->setConnection ((UDPConnection *)udp); */
  return reinterpret_cast<TSAction>(conn->send((Continuation *)contp, packet));
}

TSAction
INKUDPRecvFrom(TSCont contp, INKUDPConn udp)
{
  sdk_assert(sdk_sanity_check_continuation(contp) == TS_SUCCESS);

  FORCE_PLUGIN_SCOPED_MUTEX(contp);
  UDPConnection *conn = (UDPConnection *)udp;
  return reinterpret_cast<TSAction>(conn->recv((Continuation *)contp));
}

int
INKUDPConnFdGet(INKUDPConn udp)
{
  UDPConnection *conn = (UDPConnection *)udp;
  return conn->getFd();
}

/* UDP Packet */
INKUDPPacket
INKUDPPacketCreate()
{
  UDPPacket *packet = new_UDPPacket();
  return ((INKUDPPacket)packet);
}

TSIOBufferBlock
INKUDPPacketBufferBlockGet(INKUDPPacket packet)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)packet) == TS_SUCCESS);

  UDPPacket *p = (UDPPacket *)packet;
  return ((TSIOBufferBlock)p->getIOBlockChain());
}

unsigned int
INKUDPPacketFromAddressGet(INKUDPPacket packet)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)packet) == TS_SUCCESS);

  UDPPacket *p = (UDPPacket *)packet;
  return ats_ip4_addr_cast(&p->from);
}

int
INKUDPPacketFromPortGet(INKUDPPacket packet)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)packet) == TS_SUCCESS);

  UDPPacket *p = (UDPPacket *)packet;
  return ats_ip_port_host_order(&p->from);
}

INKUDPConn
INKUDPPacketConnGet(INKUDPPacket packet)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)packet) == TS_SUCCESS);

  UDPPacket *p = (UDPPacket *)packet;
  return ((INKUDPConn)p->getConnection());
}

void
INKUDPPacketDestroy(INKUDPPacket packet)
{
  sdk_assert(sdk_sanity_check_null_ptr((void *)packet) == TS_SUCCESS);

  UDPPacket *p = (UDPPacket *)packet;
  p->free();
}

/* Packet Queue */

INKUDPPacket
INKUDPPacketGet(INKUDPacketQueue queuep)
{
  if (queuep != nullptr) {
    UDPPacket *packet;
    Queue<UDPPacket> *qp = (Queue<UDPPacket> *)queuep;

    packet = qp->pop();
    return (packet);
  }

  return nullptr;
}

/* Buffers */

TSIOBuffer
TSIOBufferCreate()
{
  MIOBuffer *b = new_empty_MIOBuffer();

  // TODO: Should remove this when memory allocations can't fail.
  sdk_assert(sdk_sanity_check_iocore_structure(b) == TS_SUCCESS);
  return reinterpret_cast<TSIOBuffer>(b);
}

TSIOBuffer
TSIOBufferSizedCreate(TSIOBufferSizeIndex index)
{
  sdk_assert((index >= TS_IOBUFFER_SIZE_INDEX_128) && (index <= TS_IOBUFFER_SIZE_INDEX_32K));

  MIOBuffer *b = new_MIOBuffer(index);

  // TODO: Should remove this when memory allocations can't fail.
  sdk_assert(sdk_sanity_check_iocore_structure(b) == TS_SUCCESS);
  return reinterpret_cast<TSIOBuffer>(b);
}

void
TSIOBufferDestroy(TSIOBuffer bufp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  free_MIOBuffer((MIOBuffer *)bufp);
}

TSIOBufferBlock
TSIOBufferStart(TSIOBuffer bufp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);

  MIOBuffer *b       = (MIOBuffer *)bufp;
  IOBufferBlock *blk = b->get_current_block();

  if (!blk || (blk->write_avail() == 0)) {
    b->add_block();
  }
  blk = b->get_current_block();

  // TODO: Remove when memory allocations can't fail.
  sdk_assert(sdk_sanity_check_null_ptr((void *)blk) == TS_SUCCESS);

  return (TSIOBufferBlock)blk;
}

int64_t
TSIOBufferCopy(TSIOBuffer bufp, TSIOBufferReader readerp, int64_t length, int64_t offset)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);
  sdk_assert((length >= 0) && (offset >= 0));

  MIOBuffer *b      = (MIOBuffer *)bufp;
  IOBufferReader *r = (IOBufferReader *)readerp;

  return b->write(r, length, offset);
}

int64_t
TSIOBufferWrite(TSIOBuffer bufp, const void *buf, int64_t length)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_null_ptr((void *)buf) == TS_SUCCESS);
  sdk_assert(length >= 0);

  MIOBuffer *b = (MIOBuffer *)bufp;
  return b->write(buf, length);
}

int64_t
TSIOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
  auto r{reinterpret_cast<IOBufferReader *>(readerp)};
  char *limit = r->memcpy(buf, length, 0);
  return limit - static_cast<char *>(buf);
}

void
TSIOBufferProduce(TSIOBuffer bufp, int64_t nbytes)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  sdk_assert(nbytes >= 0);

  MIOBuffer *b = (MIOBuffer *)bufp;
  b->fill(nbytes);
}

// dev API, not exposed
void
TSIOBufferBlockDestroy(TSIOBufferBlock blockp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);

  IOBufferBlock *blk = (IOBufferBlock *)blockp;
  blk->free();
}

TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock blockp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);

  IOBufferBlock *blk = (IOBufferBlock *)blockp;
  return (TSIOBufferBlock)(blk->next.get());
}

// dev API, not exposed
int64_t
TSIOBufferBlockDataSizeGet(TSIOBufferBlock blockp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);

  IOBufferBlock *blk = (IOBufferBlock *)blockp;
  return (blk->read_avail());
}

const char *
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64_t *avail)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferBlock *blk     = (IOBufferBlock *)blockp;
  IOBufferReader *reader = (IOBufferReader *)readerp;
  char *p;

  p = blk->start();
  if (avail) {
    *avail = blk->read_avail();
  }

  if (reader->block.get() == blk) {
    p += reader->start_offset;
    if (avail) {
      *avail -= reader->start_offset;
      if (*avail < 0) {
        *avail = 0;
      }
    }
  }

  return (const char *)p;
}

int64_t
TSIOBufferBlockReadAvail(TSIOBufferBlock blockp, TSIOBufferReader readerp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferBlock *blk     = (IOBufferBlock *)blockp;
  IOBufferReader *reader = (IOBufferReader *)readerp;
  int64_t avail;

  avail = blk->read_avail();

  if (reader->block.get() == blk) {
    avail -= reader->start_offset;
    if (avail < 0) {
      avail = 0;
    }
  }

  return avail;
}

char *
TSIOBufferBlockWriteStart(TSIOBufferBlock blockp, int64_t *avail)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);

  IOBufferBlock *blk = (IOBufferBlock *)blockp;

  if (avail) {
    *avail = blk->write_avail();
  }
  return blk->end();
}

int64_t
TSIOBufferBlockWriteAvail(TSIOBufferBlock blockp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(blockp) == TS_SUCCESS);

  IOBufferBlock *blk = (IOBufferBlock *)blockp;
  return blk->write_avail();
}

int64_t
TSIOBufferWaterMarkGet(TSIOBuffer bufp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);

  MIOBuffer *b = (MIOBuffer *)bufp;
  return b->water_mark;
}

void
TSIOBufferWaterMarkSet(TSIOBuffer bufp, int64_t water_mark)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);
  sdk_assert(water_mark >= 0);

  MIOBuffer *b  = (MIOBuffer *)bufp;
  b->water_mark = water_mark;
}

TSIOBufferReader
TSIOBufferReaderAlloc(TSIOBuffer bufp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(bufp) == TS_SUCCESS);

  MIOBuffer *b             = (MIOBuffer *)bufp;
  TSIOBufferReader readerp = (TSIOBufferReader)b->alloc_reader();

  // TODO: Should remove this when memory allocation can't fail.
  sdk_assert(sdk_sanity_check_null_ptr((void *)readerp) == TS_SUCCESS);
  return readerp;
}

TSIOBufferReader
TSIOBufferReaderClone(TSIOBufferReader readerp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferReader *r = (IOBufferReader *)readerp;
  return (TSIOBufferReader)r->clone();
}

void
TSIOBufferReaderFree(TSIOBufferReader readerp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferReader *r = (IOBufferReader *)readerp;
  r->mbuf->dealloc_reader(r);
}

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader readerp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferReader *r = (IOBufferReader *)readerp;

  if (r->block) {
    r->skip_empty_blocks();
  }

  return reinterpret_cast<TSIOBufferBlock>(r->get_current_block());
}

void
TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes)
{
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);
  sdk_assert(nbytes >= 0);

  IOBufferReader *r = (IOBufferReader *)readerp;
  r->consume(nbytes);
}

int64_t
TSIOBufferReaderAvail(TSIOBufferReader readerp)
{
  sdk_assert(sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS);

  IOBufferReader *r = (IOBufferReader *)readerp;
  return r->read_avail();
}
