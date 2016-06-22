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

  ClusterVConnection.cc
****************************************************************************/

#include "P_Cluster.h"
ClassAllocator<ClusterVConnection> clusterVCAllocator("clusterVCAllocator");
ClassAllocator<ByteBankDescriptor> byteBankAllocator("byteBankAllocator");

ByteBankDescriptor *
ByteBankDescriptor::ByteBankDescriptor_alloc(IOBufferBlock *iob)
{
  ByteBankDescriptor *b = byteBankAllocator.alloc();
  b->block              = iob;
  return b;
}

void
ByteBankDescriptor::ByteBankDescriptor_free(ByteBankDescriptor *b)
{
  b->block = 0;
  byteBankAllocator.free(b);
}

void
clusterVCAllocator_free(ClusterVConnection *vc)
{
  vc->mutex   = 0;
  vc->action_ = 0;
  vc->free();
  if (vc->in_vcs) {
    vc->type = VC_CLUSTER_CLOSED;
    return;
  }
  clusterVCAllocator.free(vc);
}

ClusterVConnState::ClusterVConnState() : enabled(0), priority(1), vio(VIO::NONE), queue(0), ifd(-1), delay_timeout(NULL)
{
}

ClusterVConnectionBase::ClusterVConnectionBase()
  : thread(0), closed(0), inactivity_timeout_in(0), active_timeout_in(0), inactivity_timeout(NULL), active_timeout(NULL)
{
}

#ifdef DEBUG
int ClusterVConnectionBase::enable_debug_trace = 0;
#endif

VIO *
ClusterVConnectionBase::do_io_read(Continuation *acont, int64_t anbytes, MIOBuffer *abuffer)
{
  ink_assert(!closed);
  read.vio.buffer.writer_for(abuffer);
  read.vio.op = VIO::READ;
  read.vio.set_continuation(acont);
  read.vio.nbytes    = anbytes;
  read.vio.ndone     = 0;
  read.vio.vc_server = (VConnection *)this;
  read.enabled       = 1;

  ClusterVConnection *cvc = (ClusterVConnection *)this;
  Debug("cluster_vc_xfer", "do_io_read [%s] chan %d", "", cvc->channel);
  return &read.vio;
}

VIO *
ClusterVConnectionBase::do_io_pread(Continuation * /* acont ATS_UNUSED */, int64_t /* anbytes ATS_UNUSED */,
                                    MIOBuffer * /* abuffer ATS_UNUSED */, int64_t /* off ATS_UNUSED */)
{
  return 0;
}

int
ClusterVConnection::get_header(void ** /* ptr ATS_UNUSED */, int * /*len ATS_UNUSED */)
{
  ink_assert(!"implemented");
  return -1;
}

int
ClusterVConnection::set_header(void * /* ptr ATS_UNUSED */, int /* len ATS_UNUSED */)
{
  ink_assert(!"implemented");
  return -1;
}

int
ClusterVConnection::get_single_data(void ** /* ptr ATS_UNUSED */, int * /* len ATS_UNUSED */)
{
  ink_assert(!"implemented");
  return -1;
}

VIO *
ClusterVConnectionBase::do_io_write(Continuation *acont, int64_t anbytes, IOBufferReader *abuffer, bool owner)
{
  ink_assert(!closed);
  ink_assert(!owner);
  write.vio.buffer.reader_for(abuffer);
  write.vio.op = VIO::WRITE;
  write.vio.set_continuation(acont);
  write.vio.nbytes    = anbytes;
  write.vio.ndone     = 0;
  write.vio.vc_server = (VConnection *)this;
  write.enabled       = 1;

  return &write.vio;
}

void
ClusterVConnectionBase::do_io_close(int alerrno)
{
  read.enabled  = 0;
  write.enabled = 0;
  read.vio.buffer.clear();
  write.vio.buffer.clear();
  INK_WRITE_MEMORY_BARRIER;
  if (alerrno && alerrno != -1)
    this->lerrno = alerrno;

  if (alerrno == -1) {
    closed = 1;
  } else {
    closed = -1;
  }
}

void
ClusterVConnection::reenable(VIO *vio)
{
  if (type == VC_CLUSTER_WRITE)
    ch->vcs_push(this, VC_CLUSTER_WRITE);

  ClusterVConnectionBase::reenable(vio);
}

void
ClusterVConnectionBase::reenable(VIO *vio)
{
  ink_assert(!closed);
  if (vio == &read.vio) {
    read.enabled = 1;
#ifdef DEBUG
    if (enable_debug_trace && (vio->buffer.writer() && !vio->buffer.writer()->write_avail()))
      printf("NetVConnection re-enabled for read when full\n");
#endif
  } else if (vio == &write.vio) {
    write.enabled = 1;
#ifdef DEBUG
    if (enable_debug_trace && (vio->buffer.writer() && !vio->buffer.reader()->read_avail()))
      printf("NetVConnection re-enabled for write when empty\n");
#endif
  } else {
    ink_assert(!"bad vio");
  }
}

void
ClusterVConnectionBase::reenable_re(VIO *vio)
{
  reenable(vio);
}

ClusterVConnection::ClusterVConnection(int is_new_connect_read)
  : ch(NULL),
    new_connect_read(is_new_connect_read),
    remote_free(0),
    last_local_free(0),
    channel(0),
    close_disabled(0),
    remote_closed(0),
    remote_close_disabled(1),
    remote_lerrno(0),
    in_vcs(0),
    type(0),
    start_time(0),
    last_activity_time(0),
    n_set_data_msgs(0),
    n_recv_set_data_msgs(0),
    pending_remote_fill(0),
    remote_ram_cache_hit(0),
    have_all_data(0),
    initial_data_bytes(0),
    current_cont(0),
    iov_map(CLUSTER_IOV_NOT_OPEN),
    write_list_tail(0),
    write_list_bytes(0),
    write_bytes_in_transit(0),
    alternate(),
    time_pin(0),
    disk_io_priority(0)
{
#ifdef DEBUG
  read.vio.buffer.name  = "ClusterVConnection.read";
  write.vio.buffer.name = "ClusterVConnection.write";
#endif
  SET_HANDLER((ClusterVConnHandler)&ClusterVConnection::startEvent);
}

ClusterVConnection::~ClusterVConnection()
{
  free();
}

void
ClusterVConnection::free()
{
  if (alternate.valid()) {
    alternate.destroy();
  }
  ByteBankDescriptor *d;
  while ((d = byte_bank_q.dequeue())) {
    ByteBankDescriptor::ByteBankDescriptor_free(d);
  }
  read_block             = 0;
  remote_write_block     = 0;
  marshal_buf            = 0;
  write_list             = 0;
  write_list_tail        = 0;
  write_list_bytes       = 0;
  write_bytes_in_transit = 0;
}

VIO *
ClusterVConnection::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (type == VC_CLUSTER)
    type = VC_CLUSTER_READ;
  ch->vcs_push(this, VC_CLUSTER_READ);

  return ClusterVConnectionBase::do_io_read(c, nbytes, buf);
}

VIO *
ClusterVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  if (type == VC_CLUSTER)
    type = VC_CLUSTER_WRITE;
  ch->vcs_push(this, VC_CLUSTER_WRITE);

  return ClusterVConnectionBase::do_io_write(c, nbytes, buf, owner);
}

void
ClusterVConnection::do_io_close(int alerrno)
{
  if ((type == VC_CLUSTER) && current_cont) {
    if (((CacheContinuation *)current_cont)->read_cluster_vc == this)
      type = VC_CLUSTER_READ;
    else if (((CacheContinuation *)current_cont)->write_cluster_vc == this)
      type = VC_CLUSTER_WRITE;
  }
  ch->vcs_push(this, type);

  ClusterVConnectionBase::do_io_close(alerrno);
}

int
ClusterVConnection::startEvent(int event, Event *e)
{
  //
  // Safe to call with e == NULL from the same thread.
  //
  (void)event;
  start(e ? e->ethread : (EThread *)NULL);
  return EVENT_DONE;
}

int
ClusterVConnection::mainEvent(int event, Event *e)
{
  (void)event;
  (void)e;
  ink_assert(!"unexpected event");
  return EVENT_DONE;
}

int
ClusterVConnection::start(EThread *t)
{
  //
  //  New channel connect protocol.  Establish VC locally and send the
  //  channel id to the target.  Reverse of existing connect protocol
  //
  //////////////////////////////////////////////////////////////////////////
  // In the new VC connect protocol, we always establish the local side
  // of the connection followed by the remote side.
  //
  // Read connection notes:
  // ----------------------
  // The response message now consists of the standard reply message
  // along with a portion of the  object data.  This data is always
  // transferred in the same Cluster transfer message as channel data.
  // In order to transfer data into a partially connected VC, we introduced
  // a VC "pending_remote_fill" state allowing us to move the initial data
  // using the existing user channel mechanism.
  // Initially, both sides of the connection set "pending_remote_fill".
  //
  // "pending_remote_fill" allows us to make the following assumptions.
  //   1) No free space messages are sent for VC(s) in this state.
  //   2) Writer side, the initial write data is described by
  //      vc->remote_write_block NOT by vc->write.vio.buffer, since
  //      vc->write.vio is reserved for use in the OneWayTunnel.
  //      OneWayTunnel is used when all the object data cannot be
  //      contained in the initial send buffer.
  //   3) Writer side, write vio mutex not acquired for initial data write.
  ///////////////////////////////////////////////////////////////////////////

  int status;
  if (!channel) {
#ifdef CLUSTER_TOMCAT
    Ptr<ProxyMutex> m = action_.mutex;
    if (!m) {
      m = new_ProxyMutex();
    }
#else
    Ptr<ProxyMutex> m = action_.mutex;
#endif

    // Establish the local side of the VC connection
    MUTEX_TRY_LOCK(lock, m, t);
    if (!lock.is_locked()) {
      t->schedule_in(this, CLUSTER_CONNECT_RETRY);
      return EVENT_DONE;
    }
    if (!ch) {
      if (action_.continuation) {
        action_.continuation->handleEvent(CLUSTER_EVENT_OPEN_FAILED, (void *)-ECLUSTER_NO_MACHINE);
        clusterVCAllocator_free(this);
        return EVENT_DONE;
      } else {
        // if we have been invoked immediately
        clusterVCAllocator_free(this);
        return -1;
      }
    }

    channel = ch->alloc_channel(this);
    if (channel < 0) {
      if (action_.continuation) {
        action_.continuation->handleEvent(CLUSTER_EVENT_OPEN_FAILED, (void *)-ECLUSTER_NOMORE_CHANNELS);
        clusterVCAllocator_free(this);
        return EVENT_DONE;
      } else {
        // if we have been invoked immediately
        clusterVCAllocator_free(this);
        return -1;
      }

    } else {
      Debug(CL_TRACE, "VC start alloc local chan=%d VC=%p", channel, this);
      if (new_connect_read)
        this->pending_remote_fill = 1;
    }

  } else {
    // Establish the remote side of the VC connection
    if ((status = ch->alloc_channel(this, channel)) < 0) {
      Debug(CL_TRACE, "VC start alloc remote failed chan=%d VC=%p", channel, this);
      clusterVCAllocator_free(this);
      return status; // Channel active or no more channels
    } else {
      Debug(CL_TRACE, "VC start alloc remote chan=%d VC=%p", channel, this);
      if (new_connect_read)
        this->pending_remote_fill = 1;
      this->iov_map               = CLUSTER_IOV_NONE; // disable connect timeout
    }
  }
  cluster_schedule(ch, this, &read);
  cluster_schedule(ch, this, &write);
  if (action_.continuation) {
    action_.continuation->handleEvent(CLUSTER_EVENT_OPEN, this);
  }
  mutex = NULL;
  return EVENT_DONE;
}

int
ClusterVConnection::was_closed()
{
  return (closed && !close_disabled);
}

void
ClusterVConnection::allow_close()
{
  close_disabled = 0;
}

void
ClusterVConnection::disable_close()
{
  close_disabled = 1;
}

int
ClusterVConnection::was_remote_closed()
{
  if (!byte_bank_q.head && !remote_close_disabled)
    return remote_closed;
  else
    return 0;
}

void
ClusterVConnection::allow_remote_close()
{
  remote_close_disabled = 0;
}

bool
ClusterVConnection::schedule_write()
{
  //
  // Schedule write if we have all data or current write data is
  // at least DEFAULT_MAX_BUFFER_SIZE.
  //
  if (write_list) {
    if ((closed < 0) || remote_closed) {
      // User aborted connection, dump data.

      write_list       = 0;
      write_list_tail  = 0;
      write_list_bytes = 0;

      return false;
    }

    if (closed || (write_list_bytes >= DEFAULT_MAX_BUFFER_SIZE)) {
      // No more data to write or buffer list is full, start write
      return true;
    } else {
      // Buffer list is not full, defer write
      return false;
    }
  } else {
    return false;
  }
}

void
ClusterVConnection::set_type(int options)
{
  new_connect_read = (options & CLUSTER_OPT_CONN_READ) ? 1 : 0;
  if (new_connect_read) {
    pending_remote_fill = 1;
  } else {
    pending_remote_fill = 0;
  }
}

// Overide functions in base class VConnection.
bool
ClusterVConnection::get_data(int id, void * /* data ATS_UNUSED */)
{
  switch (id) {
  case CACHE_DATA_HTTP_INFO: {
    ink_release_assert(!"ClusterVConnection::get_data CACHE_DATA_HTTP_INFO not supported");
  }
  case CACHE_DATA_KEY: {
    ink_release_assert(!"ClusterVConnection::get_data CACHE_DATA_KEY not supported");
  }
  default: {
    ink_release_assert(!"ClusterVConnection::get_data invalid id");
  }
  }
  return false;
}

void
ClusterVConnection::get_http_info(CacheHTTPInfo **info)
{
  *info = &alternate;
}

int64_t
ClusterVConnection::get_object_size()
{
  return alternate.object_size_get();
}

bool
ClusterVConnection::is_pread_capable()
{
  return false;
}

void
ClusterVConnection::set_http_info(CacheHTTPInfo *d)
{
  int flen, len;
  void *data;
  int res;
  SetChanDataMessage *m;
  SetChanDataMessage msg;

  //
  // set_http_info() is a mechanism to associate additional data with a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  // Cache semantics dictate that set_http_info() be established prior
  // to transferring any data on the ClusterVConnection.
  //
  ink_release_assert(this->write.vio.op == VIO::NONE); // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE); // should always be true

  int vers = SetChanDataMessage::protoToVersion(ch->machine->msg_proto_major);
  if (vers == SetChanDataMessage::SET_CHANNEL_DATA_MESSAGE_VERSION) {
    flen = SetChanDataMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_http_info() bad msg version");
  }

  // Create message and marshal data.

  CacheHTTPInfo *r = d;
  len              = r->marshal_length();
  data             = (void *)ALLOCA_DOUBLE(flen + len);
  memcpy((char *)data, (char *)&msg, sizeof(msg));
  m            = (SetChanDataMessage *)data;
  m->data_type = CACHE_DATA_HTTP_INFO;

  char *p = (char *)m + flen;
  res     = r->marshal(p, len);
  if (res < 0) {
    r->destroy();
    return;
  }
  r->destroy();

  m->channel         = channel;
  m->sequence_number = token.sequence_number;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_DATA_CLUSTER_FUNCTION, data, flen + len);
}

bool
ClusterVConnection::set_pin_in_cache(time_t t)
{
  SetChanPinMessage msg;

  //
  // set_pin_in_cache() is a mechanism to set an attribute on a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  //
  ink_release_assert(this->write.vio.op == VIO::NONE); // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE); // should always be true
  time_pin = t;

  int vers = SetChanPinMessage::protoToVersion(ch->machine->msg_proto_major);

  if (vers == SetChanPinMessage::SET_CHANNEL_PIN_MESSAGE_VERSION) {
    SetChanPinMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_pin_in_cache() bad msg "
                        "version");
  }
  msg.channel         = channel;
  msg.sequence_number = token.sequence_number;
  msg.pin_time        = time_pin;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_PIN_CLUSTER_FUNCTION, (char *)&msg, sizeof(msg));
  return true;
}

time_t
ClusterVConnection::get_pin_in_cache()
{
  return time_pin;
}

bool
ClusterVConnection::set_disk_io_priority(int priority)
{
  SetChanPriorityMessage msg;

  //
  // set_disk_io_priority() is a mechanism to set an attribute on a
  // open_write() ClusterVConnection.  It is only allowed after a
  // successful open_write() and prior to issuing the do_io(VIO::WRITE).
  //
  ink_release_assert(this->write.vio.op == VIO::NONE); // not true if do_io()
  //   already done
  ink_release_assert(this->read.vio.op == VIO::NONE); // should always be true
  disk_io_priority = priority;

  int vers = SetChanPriorityMessage::protoToVersion(ch->machine->msg_proto_major);

  if (vers == SetChanPriorityMessage::SET_CHANNEL_PRIORITY_MESSAGE_VERSION) {
    SetChanPriorityMessage::sizeof_fixedlen_msg();
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"ClusterVConnection::set_disk_io_priority() bad msg "
                        "version");
  }
  msg.channel         = channel;
  msg.sequence_number = token.sequence_number;
  msg.disk_priority   = priority;

  // note pending set_data() msgs on VC.
  ink_atomic_increment(&n_set_data_msgs, 1);

  clusterProcessor.invoke_remote(ch, SET_CHANNEL_PRIORITY_CLUSTER_FUNCTION, (char *)&msg, sizeof(msg));
  return true;
}

int
ClusterVConnection::get_disk_io_priority()
{
  return disk_io_priority;
}

// End of ClusterVConnection.cc
