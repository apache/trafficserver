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
#include "api/include/InkAPIPrivateIOCore.h"
#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_Cache.h"
#include "I_HostDB.h"

INKReturnCode
sdk_sanity_check_mutex(INKMutex mutex)
{
#ifdef DEBUG
  if (mutex == NULL || mutex == INK_ERROR_PTR)
    return INK_ERROR;
  ProxyMutex *mutexp = (ProxyMutex *) mutex;
  if (mutexp->m_refcount < 0)
    return INK_ERROR;
  if (mutexp->nthread_holding < 0)
    return INK_ERROR;
  return INK_SUCCESS;
#endif
  return INK_SUCCESS;
}


INKReturnCode
sdk_sanity_check_hostlookup_structure(INKHostLookupResult data)
{
#ifdef DEBUG
  if (data == NULL || data == INK_ERROR_PTR)
    return INK_ERROR;
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
#endif
}

INKReturnCode
sdk_sanity_check_iocore_structure(void *data)
{
#ifdef DEBUG
  if (data == NULL || data == INK_ERROR_PTR)
    return INK_ERROR;
  return INK_SUCCESS;
#else
  return INK_SUCCESS;
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

  INKThreadFunc func;
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
 * INKqa12653. Return INKThread or NULL if error
 */
INKThread
INKThreadCreate(INKThreadFunc func, void *data)
{
  INKThreadInternal *thread;

  thread = NEW(new INKThreadInternal);

#if !defined (_WIN32)
  ink_assert(thread->event_types == 0);
#endif

  thread->func = func;
  thread->data = data;

  if (!(ink_thread_create(ink_thread_trampoline, (void *) thread, 1))) {
    return (INKThread) NULL;
  }

  return (INKThread) thread;
}

INKThread
INKThreadInit()
{
  INKThreadInternal *thread;

  thread = NEW(new INKThreadInternal);

#ifdef DEBUG
  if (thread == NULL)
    return (INKThread) NULL;
#endif

  thread->set_specific();

  return thread;
}

INKReturnCode
INKThreadDestroy(INKThread thread)
{
  if (sdk_sanity_check_iocore_structure(thread) != INK_SUCCESS)
    return INK_ERROR;

  INKThreadInternal *ithread = (INKThreadInternal *) thread;
  delete ithread;
  return INK_SUCCESS;
}

INKThread
INKThreadSelf(void)
{
  INKThread ithread = (INKThread) this_ethread();
#ifdef DEBUG
  if (ithread == NULL)
    return (INKThread) NULL;
#endif
  return ithread;
}


////////////////////////////////////////////////////////////////////
//
// Mutexes
//
////////////////////////////////////////////////////////////////////

INKMutex
INKMutexCreate()
{
  ProxyMutex *mutexp = new_ProxyMutex();
  if (sdk_sanity_check_mutex((INKMutex) mutexp) != INK_SUCCESS)
    return (INKMutex) INK_ERROR_PTR;
  return (INKMutex) mutexp;
//    return (INKMutex*) new_ProxyMutex ();
}

/* The following two APIs are for Into work, actually, APIs of Mutex
   should allow plugins to manually increase or decrease the refcount 
   of the mutex pointer, plugins may want more control of the creation
   and destroy of the mutex.*/

INKMutex
INKMutexCreateInternal()
{
  ProxyMutex *new_mutex = new_ProxyMutex();
  new_mutex->refcount_inc();
  return (INKMutex *) new_mutex;
}

int
INKMutexCheck(INKMutex mutex)
{
  ProxyMutex *mutexp = (ProxyMutex *) mutex;
  if (mutexp->m_refcount < 0)
    return -1;
  if (mutexp->nthread_holding < 0)
    return -1;
  return 1;
}

INKReturnCode
INKMutexLock(INKMutex mutexp)
{
  if (sdk_sanity_check_mutex(mutexp) != INK_SUCCESS)
    return INK_ERROR;

  MUTEX_TAKE_LOCK((ProxyMutex *) mutexp, this_ethread());
  return INK_SUCCESS;
}


INKReturnCode
INKMutexLockTry(INKMutex mutexp, int *lock)
{
  if (sdk_sanity_check_mutex(mutexp) != INK_SUCCESS)
    return INK_ERROR;

  *lock = MUTEX_TAKE_TRY_LOCK((ProxyMutex *) mutexp, this_ethread());
  return INK_SUCCESS;
}

/* deprecated in SDK3.0 */
int
INKMutexTryLock(INKMutex mutexp)
{
  return MUTEX_TAKE_TRY_LOCK((ProxyMutex *) mutexp, this_ethread());
}

INKReturnCode
INKMutexUnlock(INKMutex mutexp)
{
  if (sdk_sanity_check_mutex(mutexp) != INK_SUCCESS)
    return INK_ERROR;

  MUTEX_UNTAKE_LOCK((ProxyMutex *) mutexp, this_ethread());
  return INK_SUCCESS;
}

/* VIOs */

INKReturnCode
INKVIOReenable(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  vio->reenable();
  return INK_SUCCESS;
}

INKIOBuffer
INKVIOBufferGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return (INKIOBuffer) INK_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return vio->get_writer();
}

INKIOBufferReader
INKVIOReaderGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return (INKIOBufferReader) INK_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return vio->get_reader();
}

int
INKVIONBytesGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->nbytes;
}

INKReturnCode
INKVIONBytesSet(INKVIO viop, int nbytes)
{
  if ((sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS) || nbytes < 0)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  vio->nbytes = nbytes;
  return INK_SUCCESS;
}

int
INKVIONDoneGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->ndone;
}

INKReturnCode
INKVIONDoneSet(INKVIO viop, int ndone)
{
  if ((sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS) || ndone < 0)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  vio->ndone = ndone;
  return INK_SUCCESS;
}

int
INKVIONTodoGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return INK_ERROR;

  VIO *vio = (VIO *) viop;
  return vio->ntodo();
}

INKCont
INKVIOContGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return (INKCont) INK_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (INKCont) vio->_cont;
}

INKVConn
INKVIOVConnGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return (INKVConn) INK_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (INKVConn) vio->vc_server;
}

INKMutex
INKVIOMutexGet(INKVIO viop)
{
  if (sdk_sanity_check_iocore_structure(viop) != INK_SUCCESS)
    return (INKVConn) INK_ERROR_PTR;

  VIO *vio = (VIO *) viop;
  return (INKMutex) ((ProxyMutex *) vio->mutex);
}

/* High Resolution Time */

unsigned int
INKBasedTimeGet()
{
  return ink_get_based_hrtime();
}

double
INKBasedTimeGetD()
{
  return (double) ink_get_based_hrtime();
}

/* UDP Connection Interface */

INKAction
INKUDPBind(INKCont contp, unsigned int ip, int port)
{
  FORCE_PLUGIN_MUTEX(contp);
  return (udpNet.UDPBind((Continuation *) contp, port, ip, INK_ETHERNET_MTU_SIZE, INK_ETHERNET_MTU_SIZE));
}

INKAction
INKUDPSendTo(INKCont contp, INKUDPConn udp, unsigned int ip, int port, char *data, int len)
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


INKAction
INKUDPRecvFrom(INKCont contp, INKUDPConn udp)
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

INKIOBufferBlock
INKUDPPacketBufferBlockGet(INKUDPPacket packet)
{
  UDPPacket *p = (UDPPacket *) packet;
  return ((INKIOBufferBlock) p->getIOBlockChain());
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

INKIOBuffer
INKIOBufferCreate()
{
  MIOBuffer *b = new_empty_MIOBuffer();
  if (sdk_sanity_check_iocore_structure(b) != INK_SUCCESS) {
    return (INKIOBuffer) INK_ERROR_PTR;
  }

  return (INKIOBuffer *) b;
}

INKIOBuffer
INKIOBufferSizedCreate(INKIOBufferSizeIndex index)
{
  if ((index<INK_IOBUFFER_SIZE_INDEX_128) || (index> INK_IOBUFFER_SIZE_INDEX_32K)) {
    return (INKIOBuffer) INK_ERROR_PTR;
  }

  MIOBuffer *b = new_MIOBuffer(index);
  if (sdk_sanity_check_iocore_structure(b) != INK_SUCCESS) {
    return (INKIOBuffer) INK_ERROR_PTR;
  }

  return (INKIOBuffer *) b;
}

INKReturnCode
INKIOBufferDestroy(INKIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS)
    return INK_ERROR;

  free_MIOBuffer((MIOBuffer *) bufp);
  return INK_SUCCESS;
}

INKIOBufferBlock
INKIOBufferStart(INKIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS)
    return (INKIOBufferBlock) INK_ERROR_PTR;

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
    return (INKIOBufferBlock) INK_ERROR_PTR;
#endif

  return (INKIOBufferBlock) blk;
}

INKReturnCode
INKIOBufferAppend(INKIOBuffer bufp, INKIOBufferBlock blockp)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS))
    return INK_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  IOBufferBlock *blk = (IOBufferBlock *) blockp;

  b->append_block(blk);
  return INK_SUCCESS;
}

int
INKIOBufferCopy(INKIOBuffer bufp, INKIOBufferReader readerp, int length, int offset)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS) || length < 0 || offset < 0)
    return INK_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  IOBufferReader *r = (IOBufferReader *) readerp;

  return b->write(r, length, offset);
}

int
INKIOBufferWrite(INKIOBuffer bufp, const char *buf, int length)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) || (buf == NULL) || (length < 0)) {
    return INK_ERROR;
  }

  MIOBuffer *b = (MIOBuffer *) bufp;
  return b->write(buf, length);
}

// not in SDK3.0
void
INKIOBufferReaderCopy(INKIOBufferReader readerp, char *buf, int length)
{
  IOBufferReader *r = (IOBufferReader *) readerp;
  r->memcpy(buf, length);
}

INKReturnCode
INKIOBufferProduce(INKIOBuffer bufp, int nbytes)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) || nbytes < 0)
    return INK_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  b->fill(nbytes);
  return INK_SUCCESS;
}

INKIOBufferData
INKIOBufferDataCreate(void *data, int size, INKIOBufferDataFlags flags)
{
#ifdef DEBUG
  if (data == NULL || data == INK_ERROR_PTR || size <= 0 ||
      ((flags != INK_DATA_ALLOCATE) && (flags != INK_DATA_MALLOCED) && (flags != INK_DATA_CONSTANT)))
    return (INKIOBufferData) INK_ERROR_PTR;
#endif
  // simply return error_ptr
  //ink_assert (size > 0);

  switch (flags) {
  case INK_DATA_ALLOCATE:
    ink_assert(data == NULL);
    return (INKIOBufferData) new_IOBufferData(iobuffer_size_to_index(size));

  case INK_DATA_MALLOCED:
    ink_assert(data != NULL);
    return (INKIOBufferData) new_xmalloc_IOBufferData(data, size);

  case INK_DATA_CONSTANT:
    ink_assert(data != NULL);
    return (INKIOBufferData) new_constant_IOBufferData(data, size);
  }
  // simply return error_ptr
  // ink_assert (!"not reached");
  return (INKIOBufferData) INK_ERROR_PTR;
}

INKIOBufferBlock
INKIOBufferBlockCreate(INKIOBufferData datap, int size, int offset)
{
  if ((sdk_sanity_check_iocore_structure(datap) != INK_SUCCESS) || size < 0 || offset < 0)
    return (INKIOBufferBlock) INK_ERROR;

  IOBufferData *d = (IOBufferData *) datap;
  return (INKIOBufferBlock) new_IOBufferBlock(d, size, offset);
}

// dev API, not exposed
INKReturnCode
INKIOBufferBlockDestroy(INKIOBufferBlock blockp)
{
  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  blk->free();
  return INK_SUCCESS;
}

INKIOBufferBlock
INKIOBufferBlockNext(INKIOBufferBlock blockp)
{
  if (sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS) {
    return (INKIOBuffer) INK_ERROR_PTR;
  }

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return (INKIOBufferBlock) ((IOBufferBlock *) blk->next);
}

// dev API, not exposed
int
INKIOBufferBlockDataSizeGet(INKIOBufferBlock blockp)
{
  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return (blk->read_avail());
}

const char *
INKIOBufferBlockReadStart(INKIOBufferBlock blockp, INKIOBufferReader readerp, int *avail)
{
  if ((sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS))
    return (const char *) INK_ERROR_PTR;

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

int
INKIOBufferBlockReadAvail(INKIOBufferBlock blockp, INKIOBufferReader readerp)
{
  if ((sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS) ||
      (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS))
    return INK_ERROR;

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  IOBufferReader *reader = (IOBufferReader *) readerp;
  int avail;

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
INKIOBufferBlockWriteStart(INKIOBufferBlock blockp, int *avail)
{
  if (sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS)
    return (char *) INK_ERROR_PTR;

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  if (avail) {
    *avail = blk->write_avail();
  }
  return blk->end();
}

int
INKIOBufferBlockWriteAvail(INKIOBufferBlock blockp)
{
  if (sdk_sanity_check_iocore_structure(blockp) != INK_SUCCESS) {
    return INK_ERROR;
  }

  IOBufferBlock *blk = (IOBufferBlock *) blockp;
  return blk->write_avail();
}

INKReturnCode
INKIOBufferWaterMarkGet(INKIOBuffer bufp, int *water_mark)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) || (water_mark == NULL)) {
    return INK_ERROR;
  }

  MIOBuffer *b = (MIOBuffer *) bufp;
  *water_mark = b->water_mark;
  return INK_SUCCESS;
}

INKReturnCode
INKIOBufferWaterMarkSet(INKIOBuffer bufp, int water_mark)
{
  if ((sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS) || water_mark < 0)
    return INK_ERROR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  b->water_mark = water_mark;
  return INK_SUCCESS;
}

INKIOBufferReader
INKIOBufferReaderAlloc(INKIOBuffer bufp)
{
  if (sdk_sanity_check_iocore_structure(bufp) != INK_SUCCESS)
    return (INKIOBufferReader) INK_ERROR_PTR;

  MIOBuffer *b = (MIOBuffer *) bufp;
  INKIOBufferReader readerp = (INKIOBufferReader) b->alloc_reader();

#ifdef DEBUG
  if (readerp == NULL)
    return (INKIOBufferReader) INK_ERROR_PTR;
#endif
  return readerp;
}

INKIOBufferReader
INKIOBufferReaderClone(INKIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS)
    return (INKIOBufferReader) INK_ERROR_PTR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  return (INKIOBufferReader) r->clone();
}

INKReturnCode
INKIOBufferReaderFree(INKIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS)
    return INK_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  r->mbuf->dealloc_reader(r);
  return INK_SUCCESS;
}

INKIOBufferBlock
INKIOBufferReaderStart(INKIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS)
    return (INKIOBufferBlock) INK_ERROR_PTR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  if (r->block != NULL) {
    r->skip_empty_blocks();
  }
  return (INKIOBufferBlock) r->block;
}

INKReturnCode
INKIOBufferReaderConsume(INKIOBufferReader readerp, int nbytes)
{
  if ((sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS) || nbytes < 0)
    return INK_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  r->consume(nbytes);
  return INK_SUCCESS;
}

int
INKIOBufferReaderAvail(INKIOBufferReader readerp)
{
  if (sdk_sanity_check_iocore_structure(readerp) != INK_SUCCESS)
    return INK_ERROR;

  IOBufferReader *r = (IOBufferReader *) readerp;
  return r->read_avail();
}
