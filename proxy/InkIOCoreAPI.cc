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

#include "inktomi++.h"
#include "api/ts/InkAPIPrivateIOCore.h"
#if defined(solaris) && !defined(__GNUC__)
#include "P_EventSystem.h" // I_EventSystem.h
#include "P_Net.h"         // I_Net.h
#else
#include "I_EventSystem.h"
#include "I_Net.h"
#endif
#include "I_Cache.h"
#include "I_HostDB.h"

TSReturnCode
sdk_sanity_check_mutex(TSMutex mutex)
{
#ifdef DEBUG
  if (mutex == NULL || mutex == TS_ERROR_PTR)
    return TS_ERROR;
  ProxyMutex *mutexp = (ProxyMutex *) mutex;
  if (mutexp->m_refcount < 0)
    return TS_ERROR;
  if (mutexp->nthread_holding < 0)
    return TS_ERROR;
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(mutex);
  return TS_SUCCESS;
#endif
}


TSReturnCode
sdk_sanity_check_hostlookup_structure(TSHostLookupResult data)
{
#ifdef DEBUG
  if (data == NULL || data == TS_ERROR_PTR)
    return TS_ERROR;
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(data);
  return TS_SUCCESS;
#endif
}

TSReturnCode
sdk_sanity_check_iocore_structure(void *data)
{
#ifdef DEBUG
  if (data == NULL || data == TS_ERROR_PTR)
    return TS_ERROR;
  return TS_SUCCESS;
#else
  NOWARN_UNUSED(data);
  return TS_SUCCESS;
#endif
}


////////////////////////////////////////////////////////////////////
//
// Threads
//
////////////////////////////////////////////////////////////////////

struct INKThreadInternal:public EThread
{

#if !defined (_WIN32)
  INKThreadInternal()
  :EThread(DEDICATED, -1)
  {
  }
#endif

  TSThreadFunc func;
  void *data;
};

static void *
ink_thread_trampoline(void *data)
{
  INKThreadInternal *thread;
  void *retval;

  thread = (INKThreadInternal *) data;

  thread->set_specific();

  retval = thread->func(thread->data);

  delete thread;

  return retval;
}

/*
 * INKqa12653. Return TSThread or NULL if error
 */
TSThread
TSThreadCreate(TSThreadFunc func, void *data)
{
  INKThreadInternal *thread;

  thread = NEW(new INKThreadInternal);

#if !defined (_WIN32)
  ink_assert(thread->event_types == 0);
#endif

  thread->func = func;
  thread->data = data;

  if (!(ink_thread_create(ink_thread_trampoline, (void *) thread, 1))) {
    return (TSThread) NULL;
  }

  return (TSThread) thread;
}

TSThread
TSThreadInit()
{
  INKThreadInternal *thread;

  thread = NEW(new INKThreadInternal);

#ifdef DEBUG
  if (thread == NULL)
    return (TSThread) NULL;
#endif

  thread->set_specific();

  return thread;
}

TSReturnCode
TSThreadDestroy(TSThread thread)
{
  if (sdk_sanity_check_iocore_structure(thread) != TS_SUCCESS)
    return TS_ERROR;

  INKThreadInternal *ithread = (INKThreadInternal *) thread;
  delete ithread;
  return TS_SUCCESS;
}

TSThread
TSThreadSelf(void)
{
  TSThread ithread = (TSThread) this_ethread();
#ifdef DEBUG
  if (ithread == NULL)
    return (TSThread) NULL;
#endif
  return ithread;
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
  if (sdk_sanity_check_mutex((TSMutex) mutexp) != TS_SUCCESS)
    return (TSMutex) TS_ERROR_PTR;
  return (TSMutex) mutexp;
//    return (TSMutex*) new_ProxyMutex ();
}

/* The following two APIs are for Into work, actually, APIs of Mutex
   should allow plugins to manually increase or decrease the refcount
   of the mutex pointer, plugins may want more control of the creation
   and destroy of the mutex.*/

TSMutex
TSMutexCreateInternal()
{
  ProxyMutex *new_mutex = new_ProxyMutex();
  new_mutex->refcount_inc();
  return (TSMutex *) new_mutex;
}

int
TSMutexCheck(TSMutex mutex)
{
  ProxyMutex *mutexp = (ProxyMutex *) mutex;
  if (mutexp->m_refcount < 0)
    return -1;
  if (mutexp->nthread_holding < 0)
    return -1;
  return 1;
}

TSReturnCode
TSMutexLock(TSMutex mutexp)
{
  if (sdk_sanity_check_mutex(mutexp) != TS_SUCCESS)
    return TS_ERROR;

  MUTEX_TAKE_LOCK((ProxyMutex *) mutexp, this_ethread());
  return TS_SUCCESS;
}


TSReturnCode
TSMutexLockTry(TSMutex mutexp, int *lock)
{
  if (sdk_sanity_check_mutex(mutexp) != TS_SUCCESS)
    return TS_ERROR;

  *lock = MUTEX_TAKE_TRY_LOCK((ProxyMutex *) mutexp, this_ethread());
  return TS_SUCCESS;
}

TSReturnCode
TSMutexUnlock(TSMutex mutexp)
{
  if (sdk_sanity_check_mutex(mutexp) != TS_SUCCESS)
    return TS_ERROR;

  MUTEX_UNTAKE_LOCK((ProxyMutex *) mutexp, this_ethread());
  return TS_SUCCESS;
}

/* VIOs */

TSReturnCode
TSVIOReenable(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  vio->reenable();
  return TS_SUCCESS;
}

TSIOBuffer
TSVIOBufferGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return (TSIOBuffer) TS_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return vio->get_writer();
}

TSIOBufferReader
TSVIOReaderGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return (TSIOBufferReader) TS_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return vio->get_reader();
}

int64
TSVIONBytesGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->nbytes;
}

TSReturnCode
TSVIONBytesSet(TSVIO viop, int64 nbytes)
{
  if ((sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS) || nbytes < 0)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  vio->nbytes = nbytes;
  return TS_SUCCESS;
}

int64
TSVIONDoneGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->ndone;
}

TSReturnCode
TSVIONDoneSet(TSVIO viop, int64 ndone)
{
  if ((sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS) || ndone < 0)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  vio->ndone = ndone;
  return TS_SUCCESS;
}

int64
TSVIONTodoGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return TS_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->ntodo();
}

TSCont
TSVIOContGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return (TSCont) TS_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (TSCont) vio->_cont;
}

TSVConn
TSVIOVConnGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return (TSVConn) TS_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (TSVConn) vio->vc_server;
}

TSMutex
TSVIOMutexGet(TSVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != TS_SUCCESS)
    return (TSVConn) TS_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (TSMutex) ((ProxyMutex *) vio->mutex);
}

/* High Resolution Time */

ink_hrtime
INKBasedTimeGet()
{
  return ink_get_based_hrtime();
}

/* UDP Connection Interface */

TSAction
INKUDPBind(TSCont contp, unsigned int ip, int port)
{
  FORCE_PLUGIN_MUTEX(contp);
  return (udpNet.UDPBind((Continuation *) contp, port, ip, INK_ETHERNET_MTU_SIZE, INK_ETHERNET_MTU_SIZE));
}

TSAction
INKUDPSendTo(TSCont contp, INKUDPConn udp, unsigned int ip, int port, char *data, int64 len)
{
  FORCE_PLUGIN_MUTEX(contp);
  UDPPacket *packet = new_UDPPacket();
  UDPConnection *conn = (UDPConnection *) udp;

  packet->to.sin_family = PF_INET;
  packet->to.sin_port = htons(port);
  packet->to.sin_addr.s_addr = ip;

  IOBufferBlock *blockp = new_IOBufferBlock();
  blockp->alloc(BUFFER_SIZE_INDEX_32K);

  if (len > index_to_buffer_size(BUFFER_SIZE_INDEX_32K)) {
    len = index_to_buffer_size(BUFFER_SIZE_INDEX_32K) - 1;
  }

  memcpy(blockp->start(), data, len);
  blockp->fill(len);

  packet->append_block((IOBufferBlock *) blockp);
  /* (Jinsheng 11/27/00) set connection twice which causes:
     FATAL: ../../../proxy/iocore/UDPPacket.h:136:
     failed assert `!m_conn` */

  /* packet->setConnection ((UDPConnection *)udp); */
  return conn->send((Continuation *) contp, packet);
}


TSAction
INKUDPRecvFrom(TSCont contp, INKUDPConn udp)
{
  FORCE_PLUGIN_MUTEX(contp);
  UDPConnection *conn = (UDPConnection *) udp;
  return conn->recv((Continuation *) contp);
}

int
INKUDPConnFdGet(INKUDPConn udp)
{
  UDPConnection *conn = (UDPConnection *) udp;
  return conn->getFd();
}

/* UDP Packet */
INKUDPPacket
INKUDPPacketCreate()
{
  UDPPacket *packet = new_UDPPacket();
  return ((INKUDPPacket) packet);
}

TSIOBufferBlock
INKUDPPacketBufferBlockGet(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  return ((TSIOBufferBlock) p->getIOBlockChain());
}

unsigned int
INKUDPPacketFromAddressGet(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  return (p->from.sin_addr.s_addr);
}

int
INKUDPPacketFromPortGet(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  return (ntohs(p->from.sin_port));
}

INKUDPConn
INKUDPPacketConnGet(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  return ((INKUDPConn) p->getConnection());
}

void
INKUDPPacketDestroy(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  p->free();
}

/* Packet Queue */

INKUDPPacket
INKUDPPacketGet(INKUDPacketQueue queuep)
{
  if (queuep != NULL) {
    UDPPacket *packet;
    Queue<UDPPacket> *qp = (Queue<UDPPacket> *)queuep;
    packet = qp->pop();
    return (packet);
  } else
    return (NULL);
}


/* Buffers */

TSIOBuffer
TSIOBufferCreate()
{
  MIOBuffer *b = new_empty_MIOBuffer();
  if (sdk_sanity_check_iocore_structure(b) != TS_SUCCESS) {
    return (TSIOBuffer) TS_ERROR_PTR;
  }

  return (TSIOBuffer *) b;
}

TSIOBuffer
TSIOBufferSizedCreate(TSIOBufferSizeIndex index)
{
  if ((index<TS_IOBUFFER_SIZE_INDEX_128) || (index> TS_IOBUFFER_SIZE_INDEX_32K)) {
    return (TSIOBuffer) TS_ERROR_PTR;
  }

  MIOBuffer *b = new_MIOBuffer(index);
  if (sdk_sanity_check_iocore_structure(b) != TS_SUCCESS) {
    return (TSIOBuffer) TS_ERROR_PTR;
  }

  return (TSIOBuffer *) b;
}

TSReturnCode
TSIOBufferDestroy(TSIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS)
    return TS_ERROR;

  free_MIOBuffer((MIOBuffer *) bufp);
  return TS_SUCCESS;
}

TSIOBufferBlock
TSIOBufferStart(TSIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS)
    return (TSIOBufferBlock) TS_ERROR_PTR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  IOBufferBlock *blk = b->get_current_block();

  if (!blk || (blk->write_avail() == 0)) {
    b->add_block();
  }
  blk = b->get_current_block();

  // simply return error_ptr
  // ink_assert (blk != NULL);
  // ink_debug_assert (blk->write_avail () > 0);
#ifdef DEBUG
  if (blk == NULL || (blk->write_avail() <= 0))
    return (TSIOBufferBlock) TS_ERROR_PTR;
#endif

  return (TSIOBufferBlock) blk;
}

TSReturnCode
TSIOBufferAppend(TSIOBuffer bufp, TSIOBufferBlock blockp)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS))
    return TS_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  IOBufferBlock *blk = (IOBufferBlock *) blockp;

  b->append_block(blk);
  return TS_SUCCESS;
}

int64
TSIOBufferCopy(TSIOBuffer bufp, TSIOBufferReader readerp, int64 length, int64 offset)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS) || length < 0 || offset < 0)
    return TS_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  IOBufferReader *r = (IOBufferReader *) readerp;

  return b->write(r, length, offset);
}

int64
TSIOBufferWrite(TSIOBuffer bufp, const void *buf, int64 length)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) || (buf == NULL) || (length < 0)) {
    return TS_ERROR;
  }

  MIOBuffer *b = (MIOBuffer *) bufp;
  return b->write(buf, length);
}

// not in SDK3.0
void
TSIOBufferReaderCopy(TSIOBufferReader readerp, const void *buf, int64 length)
{
  IOBufferReader *r = (IOBufferReader *) readerp;
  r->memcpy(buf, length);
}

TSReturnCode
TSIOBufferProduce(TSIOBuffer bufp, int64 nbytes)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) || nbytes < 0)
    return TS_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  b->fill(nbytes);
  return TS_SUCCESS;
}

TSIOBufferData
TSIOBufferDataCreate(void *data, int64 size, TSIOBufferDataFlags flags)
{
#ifdef DEBUG
  if (data == NULL || data == TS_ERROR_PTR || size <= 0 ||
      ((flags != TS_DATA_ALLOCATE) && (flags != TS_DATA_MALLOCED) && (flags != TS_DATA_CONSTANT)))
    return (TSIOBufferData) TS_ERROR_PTR;
#endif
  // simply return error_ptr
  //ink_assert (size > 0);

  switch (flags) {
  case TS_DATA_ALLOCATE:
    ink_assert(data == NULL);
    return (TSIOBufferData) new_IOBufferData(iobuffer_size_to_index(size));

  case TS_DATA_MALLOCED:
    ink_assert(data != NULL);
    return (TSIOBufferData) new_xmalloc_IOBufferData(data, size);

  case TS_DATA_CONSTANT:
    ink_assert(data != NULL);
    return (TSIOBufferData) new_constant_IOBufferData(data, size);
  }
  // simply return error_ptr
  // ink_assert (!"not reached");
  return (TSIOBufferData) TS_ERROR_PTR;
}

TSIOBufferBlock
TSIOBufferBlockCreate(TSIOBufferData datap, int64 size, int64 offset)
{
  if ((sdk_sanity_check_iocore_structure(datap) != TS_SUCCESS) || size < 0 || offset < 0)
    return (TSIOBufferBlock) TS_ERROR;

  IOBufferData *d = (IOBufferData *) datap;
  return (TSIOBufferBlock) new_IOBufferBlock(d, size, offset);
}

// dev API, not exposed
TSReturnCode
TSIOBufferBlockDestroy(TSIOBufferBlock blockp)
{
  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  blk->free();
  return TS_SUCCESS;
}

TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock blockp)
{
  if (sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS) {
    return (TSIOBuffer) TS_ERROR_PTR;
  }

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return (TSIOBufferBlock) ((IOBufferBlock *) blk->next);
}

// dev API, not exposed
int64
TSIOBufferBlockDataSizeGet(TSIOBufferBlock blockp)
{
  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return (blk->read_avail());
}

const char *
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64 *avail)
{
  if ((sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS))
    return (const char *) TS_ERROR_PTR;

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  IOBufferReader *reader = (IOBufferReader *) readerp;
  char *p;

  p = blk->start();
  if (avail) {
    *avail = blk->read_avail();
  }

  if (blk == reader->block) {
    p += reader->start_offset;
    if (avail) {
      *avail -= reader->start_offset;
      if (*avail < 0) {
        *avail = 0;
      }
    }
  }

  return (const char *) p;
}

int64
TSIOBufferBlockReadAvail(TSIOBufferBlock blockp, TSIOBufferReader readerp)
{
  if ((sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS))
    return TS_ERROR;

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  IOBufferReader *reader = (IOBufferReader *) readerp;
  int64 avail;

  avail = blk->read_avail();

  if (blk == reader->block) {
    avail -= reader->start_offset;
    if (avail < 0) {
      avail = 0;
    }
  }

  return avail;
}

char *
TSIOBufferBlockWriteStart(TSIOBufferBlock blockp, int64 *avail)
{
  if (sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS)
    return (char *) TS_ERROR_PTR;

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  if (avail) {
    *avail = blk->write_avail();
  }
  return blk->end();
}

int64
TSIOBufferBlockWriteAvail(TSIOBufferBlock blockp)
{
  if (sdk_sanity_check_iocore_structure(blockp) != TS_SUCCESS) {
    return TS_ERROR;
  }

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return blk->write_avail();
}

TSReturnCode
TSIOBufferWaterMarkGet(TSIOBuffer bufp, int64 *water_mark)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) || (water_mark == NULL)) {
    return TS_ERROR;
  }

  MIOBuffer *b = (MIOBuffer *) bufp;
  *water_mark = b->water_mark;
  return TS_SUCCESS;
}

TSReturnCode
TSIOBufferWaterMarkSet(TSIOBuffer bufp, int64 water_mark)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS) || water_mark < 0)
    return TS_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  b->water_mark = water_mark;
  return TS_SUCCESS;
}

TSIOBufferReader
TSIOBufferReaderAlloc(TSIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != TS_SUCCESS)
    return (TSIOBufferReader) TS_ERROR_PTR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  TSIOBufferReader readerp = (TSIOBufferReader) b->alloc_reader();

#ifdef DEBUG
  if (readerp == NULL)
    return (TSIOBufferReader) TS_ERROR_PTR;
#endif
  return readerp;
}

TSIOBufferReader
TSIOBufferReaderClone(TSIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS)
    return (TSIOBufferReader) TS_ERROR_PTR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  return (TSIOBufferReader) r->clone();
}

TSReturnCode
TSIOBufferReaderFree(TSIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS)
    return TS_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  r->mbuf->dealloc_reader(r);
  return TS_SUCCESS;
}

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS)
    return (TSIOBufferBlock) TS_ERROR_PTR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  if (r->block != NULL) {
    r->skip_empty_blocks();
  }
  return (TSIOBufferBlock) r->block;
}

TSReturnCode
TSIOBufferReaderConsume(TSIOBufferReader readerp, int64 nbytes)
{
  if ((sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS) || nbytes < 0)
    return TS_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  r->consume(nbytes);
  return TS_SUCCESS;
}

int64
TSIOBufferReaderAvail(TSIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != TS_SUCCESS)
    return TS_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  return r->read_avail();
}
