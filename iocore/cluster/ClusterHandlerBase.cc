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

  ClusterHandlerBase.cc
****************************************************************************/

#include "P_Cluster.h"

extern int cluster_receive_buffer_size;
extern int cluster_send_buffer_size;
extern uint32_t cluster_sockopt_flags;
extern uint32_t cluster_packet_mark;
extern uint32_t cluster_packet_tos;
extern int num_of_cluster_threads;

///////////////////////////////////////////////////////////////
// Incoming message continuation for periodic callout threads
///////////////////////////////////////////////////////////////

ClusterCalloutContinuation::ClusterCalloutContinuation(struct ClusterHandler *ch) : Continuation(0), _ch(ch)
{
  mutex = new_ProxyMutex();
  SET_HANDLER((ClstCoutContHandler)&ClusterCalloutContinuation::CalloutHandler);
}

ClusterCalloutContinuation::~ClusterCalloutContinuation()
{
  mutex = 0;
}

int
ClusterCalloutContinuation::CalloutHandler(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  return _ch->process_incoming_callouts(this->mutex);
}

/*************************************************************************/
// ClusterControl member functions (Internal Class)
/*************************************************************************/
ClusterControl::ClusterControl()
  : Continuation(NULL), len(0), size_index(-1), real_data(0), data(0), free_proc(0), free_proc_arg(0), iob_block(0)
{
}

void
ClusterControl::real_alloc_data(int read_access, bool align_int32_on_non_int64_boundary)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  ink_assert(!data);
  if ((len + DATA_HDR + sizeof(int32_t)) <= DEFAULT_MAX_BUFFER_SIZE) {
    size_index = buffer_size_to_index(len + DATA_HDR + sizeof(int32_t), MAX_BUFFER_SIZE_INDEX);
    iob_block  = new_IOBufferBlock();
    iob_block->alloc(size_index); // aligns on 8 byte boundary
    real_data = (int64_t *)iob_block->buf();

    if (align_int32_on_non_int64_boundary) {
      data = ((char *)real_data) + sizeof(int32_t) + DATA_HDR;
    } else {
      data = ((char *)real_data) + DATA_HDR;
    }
  } else {
    int size   = sizeof(int64_t) * (((len + DATA_HDR + sizeof(int32_t) + sizeof(int64_t) - 1) / sizeof(int64_t)) + 1);
    size_index = -1;
    iob_block  = new_IOBufferBlock();
    iob_block->alloc(BUFFER_SIZE_FOR_XMALLOC(size));
    real_data = (int64_t *)iob_block->buf();

    if (align_int32_on_non_int64_boundary) {
      data = (char *)DOUBLE_ALIGN(real_data) + sizeof(int32_t) + DATA_HDR;
    } else {
      data = (char *)DOUBLE_ALIGN(real_data) + DATA_HDR;
    }
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_ALLOC_DATA_NEWS_STAT);
  }

  // IOBufferBlock adjustments
  if (read_access) {
    // Make iob_block->read_avail() == len
    iob_block->fill((char *)data - (char *)real_data);    // skip header
    iob_block->consume((char *)data - (char *)real_data); // skip header
    iob_block->fill(len);
  } else {
    // Make iob_block->write_avail() == len
    iob_block->fill((char *)data - (char *)real_data);    // skip header
    iob_block->consume((char *)data - (char *)real_data); // skip header
    iob_block->_buf_end = iob_block->end() + len;
  }

  // Write size_index, magic number and 'this' in leading bytes
  char *size_index_ptr = (char *)data - DATA_HDR;
  *size_index_ptr      = size_index;
  ++size_index_ptr;

  *size_index_ptr = (char)ALLOC_DATA_MAGIC;
  ++size_index_ptr;

  void *val = (void *)this;
  memcpy(size_index_ptr, (char *)&val, sizeof(void *));
}

void
ClusterControl::free_data()
{
  if (data && iob_block) {
    if (free_proc) {
      // Free memory via callback proc
      (*free_proc)(free_proc_arg);
      iob_block = 0; // really free memory
      return;
    }
    if (real_data) {
      ink_release_assert(*(((uint8_t *)data) - DATA_HDR + 1) == (uint8_t)ALLOC_DATA_MAGIC);
      *(((uint8_t *)data) - DATA_HDR + 1) = (uint8_t)~ALLOC_DATA_MAGIC;

      ink_release_assert(*(((char *)data) - DATA_HDR) == size_index);
    } else {
      // malloc'ed memory, not alloced via real_alloc_data().
      // Data will be ats_free()'ed when IOBufferBlock is freed
    }
    iob_block = 0; // free memory
  }
}

/*************************************************************************/
// IncomingControl member functions (Internal Class)
/*************************************************************************/
IncomingControl *
IncomingControl::alloc()
{
  return inControlAllocator.alloc();
}

IncomingControl::IncomingControl() : recognized_time(0)
{
}

void
IncomingControl::freeall()
{
  free_data();
  inControlAllocator.free(this);
}

/*************************************************************************/
// OutgoingControl member functions (Internal Class)
/*************************************************************************/
OutgoingControl *
OutgoingControl::alloc()
{
  return outControlAllocator.alloc();
}

OutgoingControl::OutgoingControl() : ch(NULL), submit_time(0)
{
}

int
OutgoingControl::startEvent(int event, Event *e)
{
  //
  // This event handler is used by ClusterProcessor::invoke_remote()
  // to delay (CLUSTER_OPT_DELAY) the enqueuing of the control message.
  //
  (void)event;
  (void)e;
  // verify that the machine has not gone down
  if (!ch || !ch->thread)
    return EVENT_DONE;

  int32_t cluster_fn = *(int32_t *)this->data;
  int32_t pri        = ClusterFuncToQpri(cluster_fn);
  ink_atomiclist_push(&ch->outgoing_control_al[pri], (void *)this);

  return EVENT_DONE;
}

void
OutgoingControl::freeall()
{
  free_data();
  outControlAllocator.free(this);
}

/*************************************************************************/
// ClusterState member functions (Internal Class)
/*************************************************************************/
ClusterState::ClusterState(ClusterHandler *c, bool read_chan)
  : Continuation(0),
    ch(c),
    read_channel(read_chan),
    do_iodone_event(false),
    n_descriptors(0),
    sequence_number(0),
    to_do(0),
    did(0),
    n_iov(0),
    io_complete(1),
    io_complete_event(0),
    v(0),
    bytes_xfered(0),
    last_ndone(0),
    total_bytes_xfered(0),
    iov(NULL),
    iob_iov(NULL),
    byte_bank(NULL),
    n_byte_bank(0),
    byte_bank_size(0),
    missed(0),
    missed_msg(false),
    read_state_t(READ_START),
    write_state_t(WRITE_START)
{
  mutex = new_ProxyMutex();
  if (read_channel) {
    state = ClusterState::READ_START;
    SET_HANDLER(&ClusterState::doIO_read_event);
  } else {
    state = ClusterState::WRITE_START;
    SET_HANDLER(&ClusterState::doIO_write_event);
  }
  last_time  = HRTIME_SECONDS(0);
  start_time = HRTIME_SECONDS(0);
  int size;
  //
  // Note: we allocate space for maximum iovec(s), descriptor(s)
  //       and small control message data.
  //

  //////////////////////////////////////////////////
  // Place an invalid page in front of iovec data.
  //////////////////////////////////////////////////
  size_t pagesize = ats_pagesize();
  size            = ((MAX_TCOUNT + 1) * sizeof(IOVec)) + (2 * pagesize);
  iob_iov         = new_IOBufferData(BUFFER_SIZE_FOR_XMALLOC(size));
  char *addr      = (char *)align_pointer_forward(iob_iov->data(), pagesize);

  iov = (IOVec *)(addr + pagesize);

  ///////////////////////////////////////////////////
  // Place an invalid page in front of message data.
  ///////////////////////////////////////////////////
  size                     = sizeof(ClusterMsgHeader) + (MAX_TCOUNT + 1) * sizeof(Descriptor) + CONTROL_DATA + (2 * pagesize);
  msg.iob_descriptor_block = new_IOBufferBlock();
  msg.iob_descriptor_block->alloc(BUFFER_SIZE_FOR_XMALLOC(size));

  addr = (char *)align_pointer_forward(msg.iob_descriptor_block->data->data(), pagesize);

  addr = addr + pagesize;
  memset(addr, 0, size - (2 * pagesize));
  msg.descriptor = (Descriptor *)(addr + sizeof(ClusterMsgHeader));

  mbuf = new_empty_MIOBuffer();
}

ClusterState::~ClusterState()
{
  mutex = 0;
  if (iov) {
    iob_iov = 0; // Free memory
  }

  if (msg.descriptor) {
    msg.iob_descriptor_block = 0; // Free memory
  }
  // Deallocate IO Core structures
  int n;
  for (n = 0; n < MAX_TCOUNT; ++n) {
    block[n] = 0;
  }
  free_empty_MIOBuffer(mbuf);
  mbuf = 0;
}

void
ClusterState::build_do_io_vector()
{
  //
  // Construct the do_io_xxx data structures allowing transfer
  // of the data described by the iovec structure.
  //
  int bytes_to_xfer = 0;
  int n;
  IOBufferBlock *last_block = 0;

  mbuf->clear();

  // Build the IOBufferBlock chain.

  for (n = 0; n < n_iov; ++n) {
    bytes_to_xfer += iov[n].iov_len;

    if (last_block) {
      last_block->next = block[n];
    }
    last_block = block[n];
    while (last_block->next) {
      last_block = last_block->next;
    }
  }
  mbuf->_writer = block[0];
  ink_release_assert(bytes_to_xfer == to_do);
  ink_assert(bytes_to_xfer == bytes_IOBufferBlockList(mbuf->_writer, !read_channel));
}

#ifdef CLUSTER_TOMCAT
#define REENABLE_IO()                          \
  if (!ch->on_stolen_thread && !io_complete) { \
    v->reenable_re();                          \
  }

#else // !CLUSTER_TOMCAT

#ifdef CLUSTER_IMMEDIATE_NETIO
#define REENABLE_IO()                                     \
  if (!io_complete) {                                     \
    ((NetVConnection *)v->vc_server)->reenable_re_now(v); \
  }

#else // !CLUSTER_IMMEDIATE_NETIO

#define REENABLE_IO() \
  if (!io_complete) { \
    v->reenable_re(); \
  }
#endif // !CLUSTER_IMMEDIATE_NETIO

#endif // !CLUSTER_TOMCAT

int
ClusterState::doIO()
{
  ink_release_assert(io_complete);
#if !defined(CLUSTER_IMMEDIATE_NETIO)
  MUTEX_TRY_LOCK(lock, this->mutex, this_ethread());
  if (!lock.is_locked()) {
    return 0; // unable to initiate operation
  }
#endif

  if (!ch->net_vc) {
    // Node has gone down, simulate successful transfer
    io_complete = 1;
    bytes_xfered += to_do;
    to_do = 0;
    return 1;
  }
  //
  // Setup and initiate or resume Cluster i/o request to the NetProcessor.
  //
  if ((to_do && (io_complete_event == VC_EVENT_READ_READY)) || (io_complete_event == VC_EVENT_WRITE_READY)) {
    if (read_channel) {
      // Partial read case
      ink_assert(v->buffer.writer()->current_write_avail() == to_do);

    } else {
      // Partial write case
      ink_assert(v->buffer.reader()->read_avail() == to_do);
    }

    // Resume operation
    v->nbytes = to_do + did;
    ink_release_assert(v->nbytes > v->ndone);

    io_complete       = false;
    io_complete_event = 0;
    REENABLE_IO();

  } else {
    // Start new do_io_xxx operation.
    // Initialize globals

    io_complete       = false;
    io_complete_event = 0;
    bytes_xfered      = 0;
    last_ndone        = 0;

    build_do_io_vector();

    if (read_channel) {
      ink_assert(mbuf->current_write_avail() == to_do);
#ifdef CLUSTER_IMMEDIATE_NETIO
      v = ch->net_vc->do_io_read_now(this, to_do, mbuf);
#else
      v = ch->net_vc->do_io_read(this, to_do, mbuf);
#endif
      REENABLE_IO();

    } else {
      IOBufferReader *r = mbuf->alloc_reader();
      r->block          = mbuf->_writer;
      ink_assert(r->read_avail() == to_do);
#ifdef CLUSTER_IMMEDIATE_NETIO
      v = ch->net_vc->do_io_write_now(this, to_do, r);
#else
      v = ch->net_vc->do_io_write(this, to_do, r);
#endif
      REENABLE_IO();
    }
  }
  return 1; // operation initiated
}

int
ClusterState::doIO_read_event(int event, void *d)
{
  ink_release_assert(!io_complete);
  if (!v) {
    v = (VIO *)d; // Immediate callback on first NetVC read
  }
  ink_assert((VIO *)d == v);

  switch (event) {
  case VC_EVENT_READ_READY: {
    // Disable read processing
    v->nbytes = v->ndone;
    // fall through
  }
  case VC_EVENT_READ_COMPLETE: {
    bytes_xfered = v->ndone - last_ndone;
    if (bytes_xfered) {
      total_bytes_xfered += bytes_xfered;
      did += bytes_xfered;
      to_do -= bytes_xfered;
    }
    last_ndone        = v->ndone;
    io_complete_event = event;
    INK_WRITE_MEMORY_BARRIER;

    io_complete = 1;
    IOComplete();

    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  default: {
    io_complete_event = event;
    INK_WRITE_MEMORY_BARRIER;

    io_complete = -1;
    IOComplete();
    break;
  }
  } // End of switch

  return EVENT_DONE;
}

int
ClusterState::doIO_write_event(int event, void *d)
{
  ink_release_assert(!io_complete);
  if (!v) {
    v = (VIO *)d; // Immediate callback on first NetVC write
  }
  ink_assert((VIO *)d == v);

  switch (event) {
  case VC_EVENT_WRITE_READY:
#ifdef CLUSTER_IMMEDIATE_NETIO
  {
    // Disable write processing
    v->nbytes = v->ndone;
    // fall through
  }
#endif
  case VC_EVENT_WRITE_COMPLETE: {
    bytes_xfered = v->ndone - last_ndone;
    if (bytes_xfered) {
      total_bytes_xfered += bytes_xfered;
      did += bytes_xfered;
      to_do -= bytes_xfered;
    }
    last_ndone = v->ndone;
#ifdef CLUSTER_IMMEDIATE_NETIO
    io_complete_event = event;
    INK_WRITE_MEMORY_BARRIER;

    io_complete = 1;
    IOComplete();
#else
    if (event == VC_EVENT_WRITE_COMPLETE) {
      io_complete_event = event;
      INK_WRITE_MEMORY_BARRIER;

      io_complete = 1;
      IOComplete();
    } else {
      if (bytes_xfered) {
        v->reenable_re(); // Immediate action
      } else {
        v->reenable();
      }
      return EVENT_DONE;
    }
#endif
    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  default: {
    io_complete_event = event;
    INK_WRITE_MEMORY_BARRIER;

    io_complete = -1;
    IOComplete();
    break;
  }
  } // End of switch

  return EVENT_DONE;
}

void
ClusterState::IOComplete()
{
  // If no thread appears (approximate check) to be holding
  // the ClusterHandler mutex (no cluster processing in progress)
  // and immediate i/o completion events are allowed,
  // start i/o completion processing.

  if (do_iodone_event && !ch->mutex->thread_holding) {
    MUTEX_TRY_LOCK(lock, ch->mutex, this_ethread());
    if (lock.is_locked()) {
      ch->handleEvent(EVENT_IMMEDIATE, (void *)0);
    } else {
      eventProcessor.schedule_imm_signal(ch, ET_CLUSTER);
    }
  }
}

int
ClusterHandler::cluster_signal_and_update(int event, ClusterVConnection *vc, ClusterVConnState *s)
{
  s->vio._cont->handleEvent(event, &s->vio);

  if (vc->closed) {
    if (!vc->write_list && !vc->write_bytes_in_transit) {
      close_ClusterVConnection(vc);
    }
    return EVENT_DONE;
  } else {
    ink_assert((event != VC_EVENT_ERROR) || ((event == VC_EVENT_ERROR) && vc->closed));
    return EVENT_CONT;
  }
}

int
ClusterHandler::cluster_signal_and_update_locked(int event, ClusterVConnection *vc, ClusterVConnState *s)
{
  // should assert we have s->vio.mutex
  s->vio._cont->handleEvent(event, &s->vio);

  if (vc->closed) {
    if (!vc->write_list && !vc->write_bytes_in_transit) {
      close_free_lock(vc, s);
    }
    return EVENT_DONE;
  } else
    return EVENT_CONT;
}

int
ClusterHandler::cluster_signal_error_and_update(ClusterVConnection *vc, ClusterVConnState *s, int lerrno)
{
  s->enabled = 0;
  vc->lerrno = lerrno;
  return cluster_signal_and_update(VC_EVENT_ERROR, vc, s);
}

bool
ClusterHandler::check_channel(int c)
{
  //
  // Check to see that there is enough room to store channel c
  //
  while (n_channels <= c) {
    int old_channels = n_channels;
    if (!n_channels) {
      n_channels = MIN_CHANNELS;
    } else {
      if ((n_channels * 2) <= MAX_CHANNELS) {
        n_channels = n_channels * 2;
      } else {
        return false; // Limit exceeded
      }
    }
    // Allocate ClusterVConnection table entries
    channels = (ClusterVConnection **)ats_realloc(channels, n_channels * sizeof(ClusterVConnection *));

    // Allocate ChannelData table entries
    channel_data = (struct ChannelData **)ats_realloc(channel_data, n_channels * sizeof(struct ChannelData *));

    for (int i = old_channels; i < n_channels; i++) {
      if (local_channel(i)) {
        if (i > LAST_DEDICATED_CHANNEL) {
          channels[i]     = (ClusterVConnection *)1; // mark as invalid
          channel_data[i] = (struct ChannelData *)ats_malloc(sizeof(struct ChannelData));
          memset(channel_data[i], 0, sizeof(struct ChannelData));
          channel_data[i]->channel_number = i;
          free_local_channels.enqueue(channel_data[i]);
        } else {
          channels[i]     = NULL;
          channel_data[i] = NULL;
        }
      } else {
        channels[i]     = NULL;
        channel_data[i] = NULL;
      }
    }
  }
  return true; // OK
}

int
ClusterHandler::alloc_channel(ClusterVConnection *vc, int requested)
{
  //
  // Allocate a channel
  //
  struct ChannelData *cdp = 0;
  int i                   = requested;

  if (!i) {
    int loops = 1;
    do {
      cdp = free_local_channels.dequeue();
      if (!cdp) {
        if (!check_channel(n_channels)) {
          return -2; // Limit exceeded
        }
      } else {
        ink_assert(cdp == channel_data[cdp->channel_number]);
        i = cdp->channel_number;
        break;
      }
    } while (loops--);

    ink_release_assert(i != 0);                                 // required
    ink_release_assert(channels[i] == (ClusterVConnection *)1); // required
    Debug(CL_TRACE, "alloc_channel local chan=%d VC=%p", i, vc);

  } else {
    if (!check_channel(i)) {
      return -2; // Limit exceeded
    }
    if (channels[i]) {
      Debug(CL_TRACE, "alloc_channel remote inuse chan=%d VC=%p", i, vc);
      return -1; // channel in use
    } else {
      Debug(CL_TRACE, "alloc_channel remote chan=%d VC=%p", i, vc);
    }
  }
  channels[i] = vc;
  vc->channel = i;
  return i;
}

void
ClusterHandler::free_channel(ClusterVConnection *vc)
{
  //
  // Free a channel
  //
  int i = vc->channel;
  if (i > LAST_DEDICATED_CHANNEL && channels[i] == vc) {
    if (local_channel(i)) {
      channels[i] = (ClusterVConnection *)1;
      free_local_channels.enqueue(channel_data[i]);
      Debug(CL_TRACE, "free_channel local chan=%d VC=%p", i, vc);
    } else {
      channels[i] = 0;
      Debug(CL_TRACE, "free_channel remote chan=%d VC=%p", i, vc);
    }
  }
  vc->channel = 0;
}

int
ClusterHandler::machine_down()
{
  char textbuf[sizeof("255.255.255.255:65535")];

  if (dead) {
    return EVENT_DONE;
  }
//
// Looks like this machine dropped out of the cluster.
// Deal with it.
// Fatal read/write errors on the node to node connection along
// with failure of the cluster membership check in the periodic event
// result in machine_down().
//
#ifdef LOCAL_CLUSTER_TEST_MODE
  Note("machine down %u.%u.%u.%u:%d", DOT_SEPARATED(ip), port);
#else
  Note("machine down %u.%u.%u.%u:%d", DOT_SEPARATED(ip), id);
#endif
  machine_offline_APIcallout(ip);
  snprintf(textbuf, sizeof(textbuf), "%hhu.%hhu.%hhu.%hhu:%d", DOT_SEPARATED(ip), port);
  RecSignalManager(REC_SIGNAL_MACHINE_DOWN, textbuf);
  if (net_vc) {
    net_vc->do_io(VIO::CLOSE);
    net_vc = 0;
  }
  // Cancel pending cluster reads and writes
  read.io_complete  = -1;
  write.io_complete = -1;

  MUTEX_TAKE_LOCK(the_cluster_config_mutex, this_ethread());
  ClusterConfiguration *c      = this_cluster()->current_configuration();
  machine->clusterHandlers[id] = NULL;
  if ((--machine->now_connections == 0) && c->find(ip, port)) {
    ClusterConfiguration *cc = configuration_remove_machine(c, machine);
    CLUSTER_DECREMENT_DYN_STAT(CLUSTER_NODES_STAT);
    this_cluster()->configurations.push(cc);
    machine->dead = true;
  }
  MUTEX_UNTAKE_LOCK(the_cluster_config_mutex, this_ethread());
  MachineList *cc = the_cluster_config();
  if (cc && cc->find(ip, port) && connector) {
    Debug(CL_NOTE, "cluster connect retry for %hhu.%hhu.%hhu.%hhu", DOT_SEPARATED(ip));
    clusterProcessor.connect(ip, port, id);
  }
  return zombify(); // defer deletion of *this
}

int
ClusterHandler::zombify(Event * /* e ATS_UNUSED */)
{
  //
  // Node associated with *this is declared down, setup the event to cleanup
  // and defer deletion of *this
  //
  dead = true;
  if (cluster_periodic_event) {
    cluster_periodic_event->cancel(this);
    cluster_periodic_event = NULL;
  }
  clm->cancel_monitor();

  SET_HANDLER((ClusterContHandler)&ClusterHandler::protoZombieEvent);
  //
  // At this point, allow the caller (either process_read/write to complete)
  // prior to performing node down actions.
  //
  eventProcessor.schedule_in(this, HRTIME_SECONDS(1), ET_CLUSTER);
  return EVENT_DONE;
}

int
ClusterHandler::connectClusterEvent(int event, Event *e)
{
  if ((event == EVENT_IMMEDIATE) || (event == EVENT_INTERVAL)) {
    //
    // Attempt connect to target node and if successful, setup the event
    // to initiate the node to node connection protocol.
    // Initiated via ClusterProcessor::connect().
    //
    MachineList *cc = the_cluster_config();
    if (!machine)
      machine = new ClusterMachine(hostname, ip, port);
#ifdef LOCAL_CLUSTER_TEST_MODE
    if (!(cc && cc->find(ip, port))) {
#else
    if (this_cluster_machine()->ip == machine->ip || !(cc && cc->find(ip, port))) {
#endif
      if (this_cluster_machine()->ip != machine->ip)
        Debug(CL_NOTE, "cluster connect aborted, machine %u.%u.%u.%u not in cluster", DOT_SEPARATED(machine->ip));
      delete machine;
      machine = NULL;
      delete this;
      return EVENT_DONE;
    }
    // Connect to cluster member
    Debug(CL_NOTE, "connect_re from %u.%u.%u.%u to %u.%u.%u.%u", DOT_SEPARATED(this_cluster_machine()->ip),
          DOT_SEPARATED(machine->ip));
    ip = machine->ip;

    NetVCOptions opt;
    opt.socket_send_bufsize = cluster_send_buffer_size;
    opt.socket_recv_bufsize = cluster_receive_buffer_size;
    opt.sockopt_flags       = cluster_sockopt_flags;
    opt.packet_mark         = cluster_packet_mark;
    opt.packet_tos          = cluster_packet_tos;
    opt.etype               = ET_CLUSTER;
    opt.addr_binding        = NetVCOptions::INTF_ADDR;
    opt.local_ip            = this_cluster_machine()->ip;

    struct sockaddr_in addr;
    ats_ip4_set(&addr, machine->ip, htons(machine->cluster_port ? machine->cluster_port : cluster_port));

    // TODO: Should we check the Action* returned here?
    netProcessor.connect_re(this, ats_ip_sa_cast(&addr), &opt);
    return EVENT_DONE;
  } else {
    if (event == NET_EVENT_OPEN) {
      net_vc = (NetVConnection *)e;
      SET_HANDLER((ClusterContHandler)&ClusterHandler::startClusterEvent);
      eventProcessor.schedule_imm(this, ET_CLUSTER);
      return EVENT_DONE;

    } else {
      eventProcessor.schedule_in(this, CLUSTER_MEMBER_DELAY);
      return EVENT_CONT;
    }
  }
}

int
ClusterHandler::startClusterEvent(int event, Event *e)
{
  char textbuf[sizeof("255.255.255.255:65535")];

  // Perform the node to node connection establish protocol.

  (void)event;
  ink_assert(!read_vcs);
  ink_assert(!write_vcs);

  if (event == EVENT_IMMEDIATE) {
    if (cluster_connect_state == ClusterHandler::CLCON_INITIAL) {
      cluster_connect_state = ClusterHandler::CLCON_SEND_MSG;
    } else {
      ink_release_assert(!"startClusterEvent, EVENT_IMMEDIATE not expected");
    }
  } else {
    ink_release_assert(event == EVENT_INTERVAL);
  }

  for (;;) {
    switch (cluster_connect_state) {
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_INITIAL:
      ////////////////////////////////////////////////////////////////////////////
      {
        ink_release_assert(!"Invalid state [CLCON_INITIAL]");
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_SEND_MSG:
      ////////////////////////////////////////////////////////////////////////////
      {
// Send initial message.
#ifdef LOCAL_CLUSTER_TEST_MODE
        nodeClusteringVersion._port = cluster_port;
#endif
        cluster_connect_state = ClusterHandler::CLCON_SEND_MSG_COMPLETE;
        if (connector)
          nodeClusteringVersion._id = id;
        build_data_vector((char *)&nodeClusteringVersion, sizeof(nodeClusteringVersion), false);
        if (!write.doIO()) {
          // i/o not initiated, delay and retry
          cluster_connect_state = ClusterHandler::CLCON_SEND_MSG;
          eventProcessor.schedule_in(this, CLUSTER_PERIOD, ET_CLUSTER);
          return EVENT_DONE;
        }
        break;
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_SEND_MSG_COMPLETE:
      ////////////////////////////////////////////////////////////////////////////
      {
        if (write.io_complete) {
          if ((write.io_complete < 0) || ((size_t)write.did < sizeof(nodeClusteringVersion))) {
            Debug(CL_NOTE, "unable to write to cluster node %u.%u.%u.%u: %d", DOT_SEPARATED(ip), write.io_complete_event);
            cluster_connect_state = ClusterHandler::CLCON_ABORT_CONNECT;
            break; // goto next state
          }
          // Write OK, await message from peer node.
          build_data_vector((char *)&clusteringVersion, sizeof(clusteringVersion), true);
          cluster_connect_state = ClusterHandler::CLCON_READ_MSG;
          break;
        } else {
          // Delay and check for i/o completion
          eventProcessor.schedule_in(this, CLUSTER_PERIOD, ET_CLUSTER);
          return EVENT_DONE;
        }
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_READ_MSG:
      ////////////////////////////////////////////////////////////////////////////
      {
        cluster_connect_state = ClusterHandler::CLCON_READ_MSG_COMPLETE;
        if (!read.doIO()) {
          // i/o not initiated, delay and retry
          cluster_connect_state = ClusterHandler::CLCON_READ_MSG;
          eventProcessor.schedule_in(this, CLUSTER_PERIOD, ET_CLUSTER);
          return EVENT_DONE;
        }
        break;
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_READ_MSG_COMPLETE:
      ////////////////////////////////////////////////////////////////////////////
      {
        if (read.io_complete) {
          if (read.io_complete < 0) {
            // Read error, abort connect
            cluster_connect_state = ClusterHandler::CLCON_ABORT_CONNECT;
            break; // goto next state
          }
          if ((size_t)read.did < sizeof(clusteringVersion)) {
            // Partial read, resume read.
            cluster_connect_state = ClusterHandler::CLCON_READ_MSG;
            break;
          }
          cluster_connect_state = ClusterHandler::CLCON_VALIDATE_MSG;
          break;
        } else {
          // Delay and check for i/o completion
          eventProcessor.schedule_in(this, CLUSTER_PERIOD, ET_CLUSTER);
          return EVENT_DONE;
        }
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_VALIDATE_MSG:
      ////////////////////////////////////////////////////////////////////////////
      {
        int proto_major = -1;
        int proto_minor = -1;

        clusteringVersion.AdjustByteOrder();
        /////////////////////////////////////////////////////////////////////////
        // Determine the message protocol major version to use, by stepping down
        // from current to the minimium level until a match is found.
        // Derive the minor number as follows, if the current (major, minor)
        // is the current node (major, minor) use the given minor number.
        // Otherwise, minor number is zero.
        /////////////////////////////////////////////////////////////////////////
        for (int major = clusteringVersion._major; major >= clusteringVersion._min_major; --major) {
          if ((major >= nodeClusteringVersion._min_major) && (major <= nodeClusteringVersion._major)) {
            proto_major = major;
          }
        }
        if (proto_major > 0) {
          ///////////////////////////
          // Compute minor version
          ///////////////////////////
          if (proto_major == clusteringVersion._major) {
            proto_minor = clusteringVersion._minor;

            if (proto_minor != nodeClusteringVersion._minor)
              Warning("Different clustering minor versions (%d,%d) for node %u.%u.%u.%u, continuing", proto_minor,
                      nodeClusteringVersion._minor, DOT_SEPARATED(ip));
          } else {
            proto_minor = 0;
          }

        } else {
          Warning("Bad cluster major version range (%d-%d) for node %u.%u.%u.%u connect failed", clusteringVersion._min_major,
                  clusteringVersion._major, DOT_SEPARATED(ip));
          cluster_connect_state = ClusterHandler::CLCON_ABORT_CONNECT;
          break; // goto next state
        }

#ifdef LOCAL_CLUSTER_TEST_MODE
        port = clusteringVersion._port & 0xffff;
#endif
        if (!connector)
          id = clusteringVersion._id & 0xffff;

        machine->msg_proto_major = proto_major;
        machine->msg_proto_minor = proto_minor;

        if (eventProcessor.n_threads_for_type[ET_CLUSTER] != num_of_cluster_threads) {
          cluster_connect_state = ClusterHandler::CLCON_ABORT_CONNECT;
          break;
        }

        thread = eventProcessor.eventthread[ET_CLUSTER][id % num_of_cluster_threads];
        if (net_vc->thread == thread) {
          cluster_connect_state = CLCON_CONN_BIND_OK;
          break;
        } else {
          cluster_connect_state = ClusterHandler::CLCON_CONN_BIND_CLEAR;
        }
      }

    case ClusterHandler::CLCON_CONN_BIND_CLEAR: {
      UnixNetVConnection *vc = (UnixNetVConnection *)net_vc;
      MUTEX_TRY_LOCK(lock, vc->nh->mutex, e->ethread);
      MUTEX_TRY_LOCK(lock1, vc->mutex, e->ethread);
      if (lock.is_locked() && lock1.is_locked()) {
        vc->ep.stop();
        vc->nh->open_list.remove(vc);
        vc->thread = NULL;
        if (vc->nh->read_ready_list.in(vc))
          vc->nh->read_ready_list.remove(vc);
        if (vc->nh->write_ready_list.in(vc))
          vc->nh->write_ready_list.remove(vc);
        if (vc->read.in_enabled_list)
          vc->nh->read_enable_list.remove(vc);
        if (vc->write.in_enabled_list)
          vc->nh->write_enable_list.remove(vc);

        // CLCON_CONN_BIND handle in bind vc->thread (bind thread nh)
        cluster_connect_state = ClusterHandler::CLCON_CONN_BIND;
        thread->schedule_in(this, CLUSTER_PERIOD);
        return EVENT_DONE;
      } else {
        // CLCON_CONN_BIND_CLEAR handle in origin vc->thread (origin thread nh)
        vc->thread->schedule_in(this, CLUSTER_PERIOD);
        return EVENT_DONE;
      }
    }

    case ClusterHandler::CLCON_CONN_BIND: {
      //
      NetHandler *nh         = get_NetHandler(e->ethread);
      UnixNetVConnection *vc = (UnixNetVConnection *)net_vc;
      MUTEX_TRY_LOCK(lock, nh->mutex, e->ethread);
      MUTEX_TRY_LOCK(lock1, vc->mutex, e->ethread);
      if (lock.is_locked() && lock1.is_locked()) {
        if (vc->read.in_enabled_list)
          nh->read_enable_list.push(vc);
        if (vc->write.in_enabled_list)
          nh->write_enable_list.push(vc);

        vc->nh             = nh;
        vc->thread         = e->ethread;
        PollDescriptor *pd = get_PollDescriptor(e->ethread);
        if (vc->ep.start(pd, vc, EVENTIO_READ | EVENTIO_WRITE) < 0) {
          cluster_connect_state = ClusterHandler::CLCON_DELETE_CONNECT;
          break; // goto next state
        }

        nh->open_list.enqueue(vc);
        cluster_connect_state = ClusterHandler::CLCON_CONN_BIND_OK;
      } else {
        thread->schedule_in(this, CLUSTER_PERIOD);
        return EVENT_DONE;
      }
    }

    case ClusterHandler::CLCON_CONN_BIND_OK: {
      int failed = 0;

      // include this node into the cluster configuration
      MUTEX_TAKE_LOCK(the_cluster_config_mutex, this_ethread());
      MachineList *cc = the_cluster_config();
      if (cc && cc->find(ip, port)) {
        ClusterConfiguration *c = this_cluster()->current_configuration();
        ClusterMachine *m       = c->find(ip, port);

        if (!m) { // this first connection
          ClusterConfiguration *cconf = configuration_add_machine(c, machine);
          CLUSTER_INCREMENT_DYN_STAT(CLUSTER_NODES_STAT);
          this_cluster()->configurations.push(cconf);
        } else {
          // close new connection if old connections is exist
          if (id >= m->num_connections || m->clusterHandlers[id]) {
            failed = -2;
            MUTEX_UNTAKE_LOCK(the_cluster_config_mutex, this_ethread());
            goto failed;
          }
          machine = m;
        }
        machine->now_connections++;
        machine->clusterHandlers[id] = this;
        machine->dead                = false;
        dead                         = false;
      } else {
        Debug(CL_NOTE, "cluster connect aborted, machine %u.%u.%u.%u:%d not in cluster", DOT_SEPARATED(ip), port);
        failed = -1;
      }
      MUTEX_UNTAKE_LOCK(the_cluster_config_mutex, this_ethread());
    failed:
      if (failed) {
        if (failed == -1) {
          if (++configLookupFails <= CONFIG_LOOKUP_RETRIES) {
            thread->schedule_in(this, CLUSTER_PERIOD);
            return EVENT_DONE;
          }
        }
        cluster_connect_state = ClusterHandler::CLCON_DELETE_CONNECT;
        break; // goto next state
      }

      this->needByteSwap = !clusteringVersion.NativeByteOrder();
      machine_online_APIcallout(ip);

      // Signal the manager
      snprintf(textbuf, sizeof(textbuf), "%hhu.%hhu.%hhu.%hhu:%d", DOT_SEPARATED(ip), port);
      RecSignalManager(REC_SIGNAL_MACHINE_UP, textbuf);
#ifdef LOCAL_CLUSTER_TEST_MODE
      Note("machine up %hhu.%hhu.%hhu.%hhu:%d, protocol version=%d.%d", DOT_SEPARATED(ip), port, clusteringVersion._major,
           clusteringVersion._minor);
#else
      Note("machine up %hhu.%hhu.%hhu.%hhu:%d, protocol version=%d.%d", DOT_SEPARATED(ip), id, clusteringVersion._major,
           clusteringVersion._minor);
#endif

      read_vcs  = new Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_read_link>[CLUSTER_BUCKETS];
      write_vcs = new Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_write_link>[CLUSTER_BUCKETS];
      SET_HANDLER((ClusterContHandler)&ClusterHandler::beginClusterEvent);

      // enable schedule_imm() on i/o completion (optimization)
      read.do_iodone_event  = true;
      write.do_iodone_event = true;

      cluster_periodic_event = thread->schedule_every(this, -CLUSTER_PERIOD);

      // Startup the periodic events to process entries in
      //  external_incoming_control.

      int procs_online    = ink_number_of_processors();
      int total_callbacks = min(procs_online, MAX_COMPLETION_CALLBACK_EVENTS);
      for (int n = 0; n < total_callbacks; ++n) {
        callout_cont[n]   = new ClusterCalloutContinuation(this);
        callout_events[n] = eventProcessor.schedule_every(callout_cont[n], COMPLETION_CALLBACK_PERIOD, ET_NET);
      }

      // Start cluster interconnect load monitoring

      if (!clm) {
        clm = new ClusterLoadMonitor(this);
        clm->init();
      }
      return EVENT_DONE;
    }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_ABORT_CONNECT:
      ////////////////////////////////////////////////////////////////////////////
      {
        if (connector) {
          Debug(CL_NOTE, "cluster connect retry for %u.%u.%u.%u", DOT_SEPARATED(ip));
          // check for duplicate cluster connect
          clusterProcessor.connect(ip, port, id, true);
        }
        cluster_connect_state = ClusterHandler::CLCON_DELETE_CONNECT;
        break; // goto next state
      }
    ////////////////////////////////////////////////////////////////////////////
    case ClusterHandler::CLCON_DELETE_CONNECT:
      ////////////////////////////////////////////////////////////////////////////
      {
        // No references possible, so just delete it.
        delete machine;
        machine = NULL;
        delete this;
        Debug(CL_NOTE, "Failed cluster connect, deleting");
        return EVENT_DONE;
      }
    ////////////////////////////////////////////////////////////////////////////
    default:
      ////////////////////////////////////////////////////////////////////////////
      {
        Warning("startClusterEvent invalid state %d", cluster_connect_state);
        ink_release_assert(!"ClusterHandler::startClusterEvent invalid state");
        return EVENT_DONE;
      }

    } // End of switch
  }   // End of for
  return EVENT_DONE;
}

int
ClusterHandler::beginClusterEvent(int /* event ATS_UNUSED */, Event *e)
{
// Establish the main periodic Cluster event
#ifdef CLUSTER_IMMEDIATE_NETIO
  build_poll(false);
#endif
  SET_HANDLER((ClusterContHandler)&ClusterHandler::mainClusterEvent);
  return handleEvent(EVENT_INTERVAL, e);
}

int
ClusterHandler::zombieClusterEvent(int event, Event *e)
{
  //
  // The ZOMBIE state is entered when the handler may still be referenced
  // by short running tasks (one scheduling quanta).  The object is delayed
  // after some unreasonably long (in comparison) time.
  //
  (void)event;
  (void)e;
  delete this; // I am out of here
  return EVENT_DONE;
}

int
ClusterHandler::protoZombieEvent(int /* event ATS_UNUSED */, Event *e)
{
  //
  // Node associated with *this is declared down.
  // After cleanup is complete, setup handler to delete *this
  // after NO_RACE_DELAY
  //
  bool failed      = false;
  ink_hrtime delay = CLUSTER_MEMBER_DELAY * 5;
  EThread *t       = e ? e->ethread : this_ethread();
  head_p item;

  /////////////////////////////////////////////////////////////////
  // Complete pending i/o operations
  /////////////////////////////////////////////////////////////////
  mainClusterEvent(EVENT_INTERVAL, e);

  item.data = external_incoming_open_local.head.data;
  if (TO_PTR(FREELIST_POINTER(item)) || delayed_reads.head || pw_write_descriptors_built || pw_freespace_descriptors_built ||
      pw_controldata_descriptors_built) {
    // Operations still pending, retry later
    if (e) {
      e->schedule_in(delay);
      return EVENT_CONT;
    } else {
      eventProcessor.schedule_in(this, delay, ET_CLUSTER);
      return EVENT_DONE;
    }
  }
  ///////////////////////////////////////////////////////////////
  // Deallocate current read control data
  ///////////////////////////////////////////////////////////////
  IncomingControl *ic;
  while ((ic = incoming_control.dequeue())) {
    failed    = true;
    ic->mutex = NULL;
    ic->freeall();
  }

  /////////////////////////////////////////////////////////////////
  // Post error completion on all active read/write VC(s) and
  // deallocate closed VC(s).
  /////////////////////////////////////////////////////////////////
  for (int i = 0; i < n_channels; i++) {
    ClusterVConnection *vc = channels[i];
    if (VALID_CHANNEL(vc)) {
      if (!vc->closed && vc->read.vio.op == VIO::READ) {
        MUTEX_TRY_LOCK(lock, vc->read.vio.mutex, t);
        if (lock.is_locked()) {
          cluster_signal_error_and_update(vc, &vc->read, 0);
        } else {
          failed = true;
        }
      }
      vc = channels[i];
      if (VALID_CHANNEL(vc) && !vc->closed && vc->write.vio.op == VIO::WRITE) {
        MUTEX_TRY_LOCK(lock, vc->write.vio.mutex, t);
        if (lock.is_locked()) {
          cluster_signal_error_and_update(vc, &vc->write, 0);
        } else {
          failed = true;
        }
      }
      vc = channels[i];
      if (VALID_CHANNEL(vc)) {
        if (vc->closed) {
          vc->ch                     = 0;
          vc->write_list             = 0;
          vc->write_list_tail        = 0;
          vc->write_list_bytes       = 0;
          vc->write_bytes_in_transit = 0;
          close_ClusterVConnection(vc);
        } else {
          failed = true;
        }
      }
    }
  }

  ///////////////////////////////////////////////////////////////
  // Empty the external_incoming_control queue before aborting
  //   the completion callbacks.
  ///////////////////////////////////////////////////////////////
  item.data = external_incoming_control.head.data;
  if (TO_PTR(FREELIST_POINTER(item)) == NULL) {
    for (int n = 0; n < MAX_COMPLETION_CALLBACK_EVENTS; ++n) {
      if (callout_cont[n]) {
        MUTEX_TRY_LOCK(lock, callout_cont[n]->mutex, t);
        if (lock.is_locked()) {
          callout_events[n]->cancel(callout_cont[n]);
          callout_events[n] = 0;
          delete callout_cont[n];
          callout_cont[n] = 0;
        } else {
          failed = true;
        }
      }
    }
  } else {
    failed = true;
  }

  if (!failed) {
    Debug("cluster_down", "ClusterHandler zombie [%u.%u.%u.%u]", DOT_SEPARATED(ip));
    SET_HANDLER((ClusterContHandler)&ClusterHandler::zombieClusterEvent);
    delay = NO_RACE_DELAY;
  }
  if (e) {
    e->schedule_in(delay);
    return EVENT_CONT;
  } else {
    eventProcessor.schedule_in(this, delay, ET_CLUSTER);
    return EVENT_DONE;
  }
}

int dump_verbose = 0;

int
ClusterHandler::compute_active_channels()
{
  ClusterHandler *ch = this;
  int active_chans   = 0;

  for (int i = LAST_DEDICATED_CHANNEL + 1; i < ch->n_channels; i++) {
    ClusterVConnection *vc = ch->channels[i];
    if (VALID_CHANNEL(vc) && (vc->iov_map != CLUSTER_IOV_NOT_OPEN)) {
      ++active_chans;
      if (dump_verbose) {
        printf("ch[%d] vc=0x%p remote_free=%d last_local_free=%d\n", i, vc, vc->remote_free, vc->last_local_free);
        printf("  r_bytes=%d r_done=%d w_bytes=%d w_done=%d\n", (int)vc->read.vio.nbytes, (int)vc->read.vio.ndone,
               (int)vc->write.vio.nbytes, (int)vc->write.vio.ndone);
      }
    }
  }
  return active_chans;
}

void
ClusterHandler::dump_internal_data()
{
  if (!message_blk) {
    message_blk = new_IOBufferBlock();
    message_blk->alloc(MAX_IOBUFFER_SIZE);
  }
  int r;
  int n               = 0;
  char *b             = message_blk->data->data();
  unsigned int b_size = message_blk->data->block_size();

  r = snprintf(&b[n], b_size - n, "Host: %hhu.%hhu.%hhu.%hhu\n", DOT_SEPARATED(ip));
  n += r;

  r =
    snprintf(&b[n], b_size - n, "chans: %d vc_writes: %" PRId64 " write_bytes: %" PRId64 "(d)+%" PRId64 "(c)=%" PRId64 "\n",
             compute_active_channels(), _vc_writes, _vc_write_bytes, _control_write_bytes, _vc_write_bytes + _control_write_bytes);

  n += r;
  r = snprintf(&b[n], b_size - n, "dw: missed_lock: %d not_enabled: %d wait_remote_fill: %d no_active_vio: %d\n", _dw_missed_lock,
               _dw_not_enabled, _dw_wait_remote_fill, _dw_no_active_vio);

  n += r;
  r = snprintf(&b[n], b_size - n, "dw: not_enabled_or_no_write: %d set_data_pending: %d no_free_space: %d\n",
               _dw_not_enabled_or_no_write, _dw_set_data_pending, _dw_no_free_space);

  n += r;
  r = snprintf(&b[n], b_size - n, "fw: missed_lock: %d not_enabled: %d wait_remote_fill: %d no_active_vio: %d\n", _fw_missed_lock,
               _fw_not_enabled, _fw_wait_remote_fill, _fw_no_active_vio);

  n += r;
  r = snprintf(&b[n], b_size - n, "fw: not_enabled_or_no_read: %d\n", _fw_not_enabled_or_no_read);

  n += r;
  r = snprintf(&b[n], b_size - n, "rd(%d): st:%d rh:%d ahd:%d sd:%d rd:%d ad:%d sda:%d rda:%d awd:%d p:%d c:%d\n",
               _process_read_calls, _n_read_start, _n_read_header, _n_read_await_header, _n_read_setup_descriptor,
               _n_read_descriptor, _n_read_await_descriptor, _n_read_setup_data, _n_read_data, _n_read_await_data,
               _n_read_post_complete, _n_read_complete);

  n += r;
  r = snprintf(&b[n], b_size - n, "wr(%d): st:%d set:%d ini:%d wait:%d post:%d comp:%d\n", _process_write_calls, _n_write_start,
               _n_write_setup, _n_write_initiate, _n_write_await_completion, _n_write_post_complete, _n_write_complete);

  n += r;
  ink_release_assert((n + 1) <= BUFFER_SIZE_FOR_INDEX(MAX_IOBUFFER_SIZE));
  Note("%s", b);
  clear_cluster_stats();
}

void
ClusterHandler::dump_write_msg(int res)
{
  // Debug support for inter cluster message trace
  Alias32 x;
  x.u32 = (uint32_t)((struct sockaddr_in *)(net_vc->get_remote_addr()))->sin_addr.s_addr;

  fprintf(stderr, "[W] %hhu.%hhu.%hhu.%hhu SeqNo=%u, Cnt=%d, CntlCnt=%d Todo=%d, Res=%d\n", x.byte[0], x.byte[1], x.byte[2],
          x.byte[3], write.sequence_number, write.msg.count, write.msg.control_bytes, write.to_do, res);
  for (int i = 0; i < write.msg.count; ++i) {
    fprintf(stderr, "   d[%i] Type=%d, Chan=%d, SeqNo=%d, Len=%u\n", i, (write.msg.descriptor[i].type ? 1 : 0),
            (int)write.msg.descriptor[i].channel, (int)write.msg.descriptor[i].sequence_number, write.msg.descriptor[i].length);
  }
}

void
ClusterHandler::dump_read_msg()
{
  // Debug support for inter cluster message trace
  Alias32 x;
  x.u32 = (uint32_t)((struct sockaddr_in *)(net_vc->get_remote_addr()))->sin_addr.s_addr;

  fprintf(stderr, "[R] %hhu.%hhu.%hhu.%hhu  SeqNo=%u, Cnt=%d, CntlCnt=%d\n", x.byte[0], x.byte[1], x.byte[2], x.byte[3],
          read.sequence_number, read.msg.count, read.msg.control_bytes);
  for (int i = 0; i < read.msg.count; ++i) {
    fprintf(stderr, "   d[%i] Type=%d, Chan=%d, SeqNo=%d, Len=%u\n", i, (read.msg.descriptor[i].type ? 1 : 0),
            (int)read.msg.descriptor[i].channel, (int)read.msg.descriptor[i].sequence_number, read.msg.descriptor[i].length);
  }
}

// End of  ClusterHandlerBase.cc
