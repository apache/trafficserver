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

  ClusterHandler.cc
****************************************************************************/

#define DEFINE_CLUSTER_FUNCTIONS
#include "P_Cluster.h"

/*************************************************************************/
// Global Data
/*************************************************************************/
// Initialize clusterFunction[] size
unsigned SIZE_clusterFunction = countof(clusterFunction);

// hook for testing
ClusterFunction *ptest_ClusterFunction = NULL;

// global bit buckets for closed channels
static char channel_dummy_input[DEFAULT_MAX_BUFFER_SIZE];
char channel_dummy_output[DEFAULT_MAX_BUFFER_SIZE];

// outgoing control continuations
ClassAllocator<OutgoingControl> outControlAllocator("outControlAllocator");

// incoming control descriptors
ClassAllocator<IncomingControl> inControlAllocator("inControlAllocator");

static int dump_msgs = 0;

/////////////////////////////////////////
// VERIFY_PETERS_DATA support code
/////////////////////////////////////////
#ifdef VERIFY_PETERS_DATA
#define DO_VERIFY_PETERS_DATA(_p, _l) verify_peters_data(_p, _l)
#else
#define DO_VERIFY_PETERS_DATA(_p, _l)
#endif

void
verify_peters_data(char *ap, int l)
{
  unsigned char *p = (unsigned char *)ap;
  for (int i = 0; i < l - 1; i++) {
    unsigned char x1 = p[i];
    unsigned char x2 = p[i + 1];
    x1 += 1;
    if (x1 != x2) {
      fprintf(stderr, "verify peter's data failed at %d\n", i);
      break;
    }
  }
}

/*************************************************************************/
// ClusterHandler member functions (Internal Class)
/*************************************************************************/
//
// Overview:
//  In a steady state cluster environment, all cluster nodes have an
//  established TCP socket connection to each node in the cluster.
//  An instance of the class ClusterHandler exists for each known node
//  in the cluster.  All specific node-node data/state is encapsulated
//  by this class.
//
//  ClusterHandler::mainClusterEvent() is the key periodic event which
//  drives the read/write action over the node-node socket connection.
//  A high level overview of ClusterHandler::mainClusterEvent() action is
//  as follows:
//      1) Perform cluster interconnect load monitoring functions.
//         If interconnect is overloaded, convert all remote cluster
//         operations to proxy only.
//      2) Process delayed reads.  Delayed read refers to data associated
//         with a VC (Virtual Connection) which resides in an intermediate
//         buffer and is unknown to the VC.  This is required in cases
//         where we are unable to acquire the VC mutex at the time of the
//         read from the node-node socket.  Delayed read processing
//         consists of acquiring the VC mutex and moving the data into the
//         VC and posting read completion.
//      3) Process pending read data on the node-node TCP socket.  In the
//         typical case, read processing is performed using three read
//         operations.  The actions are as follows:
//              a) read the fixed size message header
//                 (struct ClusterMsgHeader) consisting of the
//                 number of data descriptors and the size of the inline
//                 control messages following the data descriptors.
//              b) Setup buffer for data descriptors and inline control
//                 messages and issue read.
//              c) Setup read buffers and acquire applicable locks for
//                 VC/Control data described by data descriptors and issue
//                 read.
//              d) Perform read completion actions on control and VC data.
//              e) Free VC locks
//      4) Process write bank data.  Write bank data is outstanding data
//         which we were unable to push out in the last write over the
//         node-node TCP socket.  Write bank data must be successfully pushed
//         before performing any additional write processing.
//      5) Build a write message consisting of the following data:
//          1) Write data for a Virtual Connection in the current write data
//             bucket (write_vcs)
//          2) Virtual Connection free space for VCs in the current read
//             data bucket (read_vcs)
//          3) Control message data (outgoing_control)
//      6) Push write data
//
//  Thread stealing refers to executing the control message processing
//  portion of mainClusterEvent()  by a thread not associated with the
//  periodic event.  This a mechanism to avoid the latency on control
//  messages by allowing them to be pushed immediately.
//
/*************************************************************************/

ClusterHandler::ClusterHandler()
  : net_vc(0),
    thread(0),
    ip(0),
    port(0),
    hostname(NULL),
    machine(NULL),
    ifd(-1),
    id(-1),
    dead(true),
    downing(false),
    active(false),
    on_stolen_thread(false),
    n_channels(0),
    channels(NULL),
    channel_data(NULL),
    connector(false),
    cluster_connect_state(ClusterHandler::CLCON_INITIAL),
    needByteSwap(false),
    configLookupFails(0),
    cluster_periodic_event(0),
    read(this, true),
    write(this, false),
    current_time(0),
    last(0),
    last_report(0),
    n_since_last_report(0),
    last_cluster_op_enable(0),
    last_trace_dump(0),
    clm(0),
    disable_remote_cluster_ops(0),
    pw_write_descriptors_built(0),
    pw_freespace_descriptors_built(0),
    pw_controldata_descriptors_built(0),
    pw_time_expired(0),
    started_on_stolen_thread(false),
    control_message_write(false)
#ifdef CLUSTER_STATS
    ,
    _vc_writes(0),
    _vc_write_bytes(0),
    _control_write_bytes(0),
    _dw_missed_lock(0),
    _dw_not_enabled(0),
    _dw_wait_remote_fill(0),
    _dw_no_active_vio(0),
    _dw_not_enabled_or_no_write(0),
    _dw_set_data_pending(0),
    _dw_no_free_space(0),
    _fw_missed_lock(0),
    _fw_not_enabled(0),
    _fw_wait_remote_fill(0),
    _fw_no_active_vio(0),
    _fw_not_enabled_or_no_read(0),
    _process_read_calls(0),
    _n_read_start(0),
    _n_read_header(0),
    _n_read_await_header(0),
    _n_read_setup_descriptor(0),
    _n_read_descriptor(0),
    _n_read_await_descriptor(0),
    _n_read_setup_data(0),
    _n_read_data(0),
    _n_read_await_data(0),
    _n_read_post_complete(0),
    _n_read_complete(0),
    _process_write_calls(0),
    _n_write_start(0),
    _n_write_setup(0),
    _n_write_initiate(0),
    _n_write_await_completion(0),
    _n_write_post_complete(0),
    _n_write_complete(0)
#endif
{
#ifdef MSG_TRACE
  t_fd = fopen("msgtrace.log", "w");
#endif
  // we need to lead by at least 1

  min_priority = 1;
  SET_HANDLER((ClusterContHandler)&ClusterHandler::startClusterEvent);

  mutex = new_ProxyMutex();
  OutgoingControl oc;
  int n;
  for (n = 0; n < CLUSTER_CMSG_QUEUES; ++n) {
    ink_atomiclist_init(&outgoing_control_al[n], "OutGoingControlQueue", (char *)&oc.link.next - (char *)&oc);
  }

  IncomingControl ic;
  ink_atomiclist_init(&external_incoming_control, "ExternalIncomingControlQueue", (char *)&ic.link.next - (char *)&ic);

  ClusterVConnection ivc;
  ink_atomiclist_init(&external_incoming_open_local, "ExternalIncomingOpenLocalQueue", (char *)&ivc.link.next - (char *)&ivc);
  ink_atomiclist_init(&read_vcs_ready, "ReadVcReadyQueue", offsetof(ClusterVConnection, ready_alink.next));
  ink_atomiclist_init(&write_vcs_ready, "WriteVcReadyQueue", offsetof(ClusterVConnection, ready_alink.next));
  memset((char *)&callout_cont[0], 0, sizeof(callout_cont));
  memset((char *)&callout_events[0], 0, sizeof(callout_events));
}

ClusterHandler::~ClusterHandler()
{
  bool free_m = false;
  if (net_vc) {
    net_vc->do_io(VIO::CLOSE);
    net_vc = 0;
  }
  if (machine) {
    MUTEX_TAKE_LOCK(the_cluster_config_mutex, this_ethread());
    if (++machine->free_connections >= machine->num_connections)
      free_m = true;
    MUTEX_UNTAKE_LOCK(the_cluster_config_mutex, this_ethread());
    if (free_m)
      free_ClusterMachine(machine);
  }
  machine = NULL;
  ats_free(hostname);
  hostname = NULL;
  ats_free(channels);
  channels = NULL;
  if (channel_data) {
    for (int i = 0; i < n_channels; ++i) {
      if (channel_data[i]) {
        ats_free(channel_data[i]);
        channel_data[i] = 0;
      }
    }
    ats_free(channel_data);
    channel_data = NULL;
  }
  if (read_vcs)
    delete[] read_vcs;
  read_vcs = NULL;

  if (write_vcs)
    delete[] write_vcs;
  write_vcs = NULL;

  if (clm) {
    delete clm;
    clm = NULL;
  }
#ifdef CLUSTER_STATS
  message_blk = 0;
#endif
}

void
ClusterHandler::close_ClusterVConnection(ClusterVConnection *vc)
{
  //
  // Close down a ClusterVConnection
  //
  if (vc->inactivity_timeout)
    vc->inactivity_timeout->cancel(vc);
  if (vc->active_timeout)
    vc->active_timeout->cancel(vc);
  if (vc->read.queue)
    ClusterVC_remove_read(vc);
  if (vc->write.queue)
    ClusterVC_remove_write(vc);
  vc->read.vio.mutex  = NULL;
  vc->write.vio.mutex = NULL;

  ink_assert(!vc->read_locked);
  ink_assert(!vc->write_locked);
  int channel = vc->channel;
  free_channel(vc);

  if (vc->byte_bank_q.head) {
    delayed_reads.remove(vc);

    // Deallocate byte bank descriptors
    ByteBankDescriptor *d;
    while ((d = vc->byte_bank_q.dequeue())) {
      ByteBankDescriptor::ByteBankDescriptor_free(d);
    }
  }
  vc->read_block = 0;

  ink_assert(!vc->write_list);
  ink_assert(!vc->write_list_tail);
  ink_assert(!vc->write_list_bytes);
  ink_assert(!vc->write_bytes_in_transit);

  if (((!vc->remote_closed && !vc->have_all_data) || (vc->remote_closed == FORCE_CLOSE_ON_OPEN_CHANNEL)) && vc->ch) {
    CloseMessage msg;
    int vers = CloseMessage::protoToVersion(vc->ch->machine->msg_proto_major);
    void *data;
    int len;

    if (vers == CloseMessage::CLOSE_CHAN_MESSAGE_VERSION) {
      msg.channel         = channel;
      msg.status          = (vc->remote_closed == FORCE_CLOSE_ON_OPEN_CHANNEL) ? FORCE_CLOSE_ON_OPEN_CHANNEL : vc->closed;
      msg.lerrno          = vc->lerrno;
      msg.sequence_number = vc->token.sequence_number;
      data                = (void *)&msg;
      len                 = sizeof(CloseMessage);

    } else {
      //////////////////////////////////////////////////////////////
      // Create the specified down rev version of this message
      //////////////////////////////////////////////////////////////
      ink_release_assert(!"close_ClusterVConnection() bad msg version");
    }
    clusterProcessor.invoke_remote(vc->ch, CLOSE_CHANNEL_CLUSTER_FUNCTION, data, len);
  }
  ink_hrtime now = Thread::get_hrtime();
  CLUSTER_DECREMENT_DYN_STAT(CLUSTER_CONNECTIONS_OPEN_STAT);
  CLUSTER_SUM_DYN_STAT(CLUSTER_CON_TOTAL_TIME_STAT, now - vc->start_time);
  if (!local_channel(channel)) {
    CLUSTER_SUM_DYN_STAT(CLUSTER_REMOTE_CONNECTION_TIME_STAT, now - vc->start_time);
  } else {
    CLUSTER_SUM_DYN_STAT(CLUSTER_LOCAL_CONNECTION_TIME_STAT, now - vc->start_time);
  }
  clusterVCAllocator_free(vc);
}

inline bool
ClusterHandler::vc_ok_write(ClusterVConnection *vc)
{
  return (((vc->closed > 0) && (vc->write_list || vc->write_bytes_in_transit)) ||
          (!vc->closed && vc->write.enabled && vc->write.vio.op == VIO::WRITE && vc->write.vio.buffer.writer()));
}

inline bool
ClusterHandler::vc_ok_read(ClusterVConnection *vc)
{
  return (!vc->closed && vc->read.vio.op == VIO::READ && vc->read.vio.buffer.writer());
}

void
ClusterHandler::close_free_lock(ClusterVConnection *vc, ClusterVConnState *s)
{
  Ptr<ProxyMutex> m(s->vio.mutex);
  if (s == &vc->read) {
    if ((ProxyMutex *)vc->read_locked)
      MUTEX_UNTAKE_LOCK(vc->read_locked, thread);
    vc->read_locked = NULL;
  } else {
    if ((ProxyMutex *)vc->write_locked)
      MUTEX_UNTAKE_LOCK(vc->write_locked, thread);
    vc->write_locked = NULL;
  }
  close_ClusterVConnection(vc);
}

bool
ClusterHandler::build_data_vector(char *d, int len, bool read_flag)
{
  // Internal interface to general network i/o facility allowing
  // single vector read/write to static data buffer.

  ClusterState &s = (read_flag ? read : write);
  ink_assert(d);
  ink_assert(len);
  ink_assert(s.iov);

  s.msg.count       = 1;
  s.iov[0].iov_base = 0;
  s.iov[0].iov_len  = len;
  s.block[0]        = new_IOBufferBlock();
  s.block[0]->set(new_constant_IOBufferData(d, len));

  if (read_flag) {
    // Make block write_avail == len
    s.block[0]->_buf_end = s.block[0]->end() + len;
  } else {
    // Make block read_avail == len
    s.block[0]->fill(len);
  }

  s.to_do = len;
  s.did   = 0;
  s.n_iov = 1;

  return true;
}

bool
ClusterHandler::build_initial_vector(bool read_flag)
{
  //
  // Build initial read/write struct iovec and corresponding IOBufferData
  // structures from the given struct descriptor(s).
  // Required vector adjustments for partial i/o conditions is handled
  // by adjust_vector().
  //
  ///////////////////////////////////////////////////////////////////
  // Descriptor to struct iovec layout
  ///////////////////////////////////////////////////////////////////
  // Write iovec[] layout
  //    iov[0] ----> struct ClusterMsgHeader
  //    iov[1] ----> struct descriptor [count]
  //                 char short_control_messages[control_bytes]
  //
  //    iov[2] ----> struct descriptor data (element #1)
  //    ......
  //    iov[2+count] ----> struct descriptor data (element #count)
  //
  ///////////////////////////////////////////////////////////////////
  // Read iovec[] layout phase #1 read
  //    iov[0] ----> struct ClusterMsgHeader
  ///////////////////////////////////////////////////////////////////
  // Read iovec[] layout phase #2 read
  //    iov[0] ----> struct descriptor[count]
  //                 char short_control_messages[control_bytes]
  ///////////////////////////////////////////////////////////////////
  // Read iovec[] layout phase #3 read
  //    iov[0] ----> struct descriptor data (element #1)
  //    ......
  //    iov[count-1] ----> struct descriptor data (element #count)
  ///////////////////////////////////////////////////////////////////
  int i, n;
  // This isn't used.
  // MIOBuffer      *w;

  ink_hrtime now      = Thread::get_hrtime();
  ClusterState &s     = (read_flag ? read : write);
  OutgoingControl *oc = s.msg.outgoing_control.head;
  IncomingControl *ic = incoming_control.head;
  int new_n_iov       = 0;
  int to_do           = 0;
  int len;

  ink_assert(s.iov);

  if (!read_flag) {
    //////////////////////////////////////////////////////////////////////
    // Setup vector for write of header, descriptors and control data
    //////////////////////////////////////////////////////////////////////
    len                       = sizeof(ClusterMsgHeader) + (s.msg.count * sizeof(Descriptor)) + s.msg.control_bytes;
    s.iov[new_n_iov].iov_base = 0;
    s.iov[new_n_iov].iov_len  = len;
    s.block[new_n_iov]        = s.msg.get_block_header();

    // Make read_avail == len
    s.block[new_n_iov]->fill(len);

    to_do += len;
    ++new_n_iov;

  } else {
    if (s.msg.state == 0) {
      ////////////////////////////////////
      // Setup vector for read of header
      ////////////////////////////////////
      len                       = sizeof(ClusterMsgHeader);
      s.iov[new_n_iov].iov_base = 0;
      s.iov[new_n_iov].iov_len  = len;
      s.block[new_n_iov]        = s.msg.get_block_header();

      // Make write_avail == len
      s.block[new_n_iov]->_buf_end = s.block[new_n_iov]->end() + len;

      to_do += len;
      ++new_n_iov;

    } else if (s.msg.state == 1) {
      /////////////////////////////////////////////////////////
      // Setup vector for read of Descriptors+control data
      /////////////////////////////////////////////////////////
      len                       = (s.msg.count * sizeof(Descriptor)) + s.msg.control_bytes;
      s.iov[new_n_iov].iov_base = 0;
      s.iov[new_n_iov].iov_len  = len;
      s.block[new_n_iov]        = s.msg.get_block_descriptor();

      // Make write_avail == len
      s.block[new_n_iov]->_buf_end = s.block[new_n_iov]->end() + len;

      to_do += s.iov[new_n_iov].iov_len;
      ++new_n_iov;
    }
  }

  ////////////////////////////////////////////////////////////
  // Build vector for data section of the cluster message.
  // For read, we only do this if we are in data phase
  // of the read (msg.state == 2)
  //////////////////////////////////////////////////////////////
  //  Note: We are assuming that free space descriptors follow
  //        the data descriptors.
  //////////////////////////////////////////////////////////////
  for (i = 0; i < (read_flag ? ((s.msg.state >= 2) ? s.msg.count : 0) : s.msg.count); i++) {
    if (s.msg.descriptor[i].type == CLUSTER_SEND_DATA) {
      ///////////////////////////////////
      // Control channel data
      ///////////////////////////////////
      if (s.msg.descriptor[i].channel == CLUSTER_CONTROL_CHANNEL) {
        if (read_flag) {
          ///////////////////////
          // Incoming Control
          ///////////////////////
          if (!ic) {
            ic                  = IncomingControl::alloc();
            ic->recognized_time = now;
            CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CTRL_MSGS_RECVD_STAT);
            ic->len = s.msg.descriptor[i].length;
            ic->alloc_data();
            if (!ic->fast_data()) {
              CLUSTER_INCREMENT_DYN_STAT(CLUSTER_SLOW_CTRL_MSGS_RECVD_STAT);
            }
            // Mark message data as invalid
            *((uint32_t *)ic->data) = UNDEFINED_CLUSTER_FUNCTION;
            incoming_control.enqueue(ic);
          }
          s.iov[new_n_iov].iov_base = 0;
          s.iov[new_n_iov].iov_len  = ic->len;
          s.block[new_n_iov]        = ic->get_block();
          to_do += s.iov[new_n_iov].iov_len;
          ++new_n_iov;
          ic = (IncomingControl *)ic->link.next;
        } else {
          ///////////////////////
          // Outgoing Control
          ///////////////////////
          ink_assert(oc);
          s.iov[new_n_iov].iov_base = 0;
          s.iov[new_n_iov].iov_len  = oc->len;
          s.block[new_n_iov]        = oc->get_block();
          to_do += s.iov[new_n_iov].iov_len;
          ++new_n_iov;
          oc = (OutgoingControl *)oc->link.next;
        }
      } else {
        ///////////////////////////////
        // User channel data
        ///////////////////////////////
        ClusterVConnection *vc = channels[s.msg.descriptor[i].channel];

        if (VALID_CHANNEL(vc) && (s.msg.descriptor[i].sequence_number) == CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) {
          if (read_flag) {
            ink_release_assert(!vc->initial_data_bytes);
            /////////////////////////////////////
            // Try to get the read VIO mutex
            /////////////////////////////////////
            ink_release_assert(!(ProxyMutex *)vc->read_locked);
#ifdef CLUSTER_TOMCAT
            if (!vc->read.vio.mutex ||
                !MUTEX_TAKE_TRY_LOCK_FOR_SPIN(vc->read.vio.mutex, thread, vc->read.vio._cont, READ_LOCK_SPIN_COUNT))
#else
            if (!MUTEX_TAKE_TRY_LOCK_FOR_SPIN(vc->read.vio.mutex, thread, vc->read.vio._cont, READ_LOCK_SPIN_COUNT))
#endif
            {
              vc->read_locked = 0;
            } else {
              vc->read_locked = vc->read.vio.mutex;
            }

            ///////////////////////////////////////
            // Allocate read data block
            ///////////////////////////////////////
            if (s.msg.descriptor[i].length) {
              vc->iov_map = new_n_iov;
            } else {
              vc->iov_map = CLUSTER_IOV_NONE;
            }
            if (vc->pending_remote_fill || vc_ok_read(vc)) {
              //////////////////////////////////////////////////////////
              // Initial and subsequent data on open read channel.
              // Allocate IOBufferBlock.
              //////////////////////////////////////////////////////////
              ink_release_assert(s.msg.descriptor[i].length <= DEFAULT_MAX_BUFFER_SIZE);
              vc->read_block = new_IOBufferBlock();
              int64_t index  = buffer_size_to_index(s.msg.descriptor[i].length, MAX_BUFFER_SIZE_INDEX);
              vc->read_block->alloc(index);

              s.iov[new_n_iov].iov_base = 0;
              s.block[new_n_iov]        = vc->read_block->clone();

            } else {
              Debug(CL_NOTE, "dumping cluster read data");
              s.iov[new_n_iov].iov_base = 0;
              s.block[new_n_iov]        = new_IOBufferBlock();
              s.block[new_n_iov]->set(new_constant_IOBufferData(channel_dummy_input, DEFAULT_MAX_BUFFER_SIZE));
            }

            // Make block write_avail == descriptor[].length
            s.block[new_n_iov]->_buf_end = s.block[new_n_iov]->end() + s.msg.descriptor[i].length;

          } else {
            bool remote_write_fill = (vc->pending_remote_fill && vc->remote_write_block);
            // Sanity check, assert we have the lock
            if (!remote_write_fill) {
              ink_assert((ProxyMutex *)vc->write_locked);
            }
            if (vc_ok_write(vc) || remote_write_fill) {
              if (remote_write_fill) {
                s.iov[new_n_iov].iov_base = 0;
                ink_release_assert((int)s.msg.descriptor[i].length == bytes_IOBufferBlockList(vc->remote_write_block, 1));
                s.block[new_n_iov] = vc->remote_write_block;

              } else {
                s.iov[new_n_iov].iov_base = 0;
                ink_release_assert((int)s.msg.descriptor[i].length <= vc->write_list_bytes);
                s.block[new_n_iov] = vc->write_list;
                vc->write_list     = consume_IOBufferBlockList(vc->write_list, (int)s.msg.descriptor[i].length);
                vc->write_list_bytes -= (int)s.msg.descriptor[i].length;
                vc->write_bytes_in_transit += (int)s.msg.descriptor[i].length;

                vc->write_list_tail = vc->write_list;
                while (vc->write_list_tail && vc->write_list_tail->next)
                  vc->write_list_tail = vc->write_list_tail->next;
              }
            } else {
              Debug(CL_NOTE, "faking cluster write data");
              s.iov[new_n_iov].iov_base = 0;
              s.block[new_n_iov]        = new_IOBufferBlock();
              s.block[new_n_iov]->set(new_constant_IOBufferData(channel_dummy_output, DEFAULT_MAX_BUFFER_SIZE));
              // Make block read_avail == descriptor[].length
              s.block[new_n_iov]->fill(s.msg.descriptor[i].length);
            }
          }
        } else {
          // VC has been deleted, need to dump the bits...
          s.iov[new_n_iov].iov_base = 0;
          s.block[new_n_iov]        = new_IOBufferBlock();

          if (read_flag) {
            s.block[new_n_iov]->set(new_constant_IOBufferData(channel_dummy_input, DEFAULT_MAX_BUFFER_SIZE));

            // Make block write_avail == descriptor[].length
            s.block[new_n_iov]->_buf_end = s.block[new_n_iov]->end() + s.msg.descriptor[i].length;

          } else {
            s.block[new_n_iov]->set(new_constant_IOBufferData(channel_dummy_output, DEFAULT_MAX_BUFFER_SIZE));

            // Make block read_avail == descriptor[].length
            s.block[new_n_iov]->fill(s.msg.descriptor[i].length);
          }
        }
        s.iov[new_n_iov].iov_len = s.msg.descriptor[i].length;
        to_do += s.iov[new_n_iov].iov_len;
        ++new_n_iov;
      }
    }
  }
  // Release IOBufferBlock references used in previous i/o operation
  for (n = new_n_iov; n < MAX_TCOUNT; ++n) {
    s.block[n] = 0;
  }

  // Initialize i/o state variables
  s.to_do = to_do;
  s.did   = 0;
  s.n_iov = new_n_iov;
  return true;

// TODO: This is apparently dead code, I added the #if 0 to avoid compiler
// warnings, but is this really intentional??
#if 0
  // Release all IOBufferBlock references.
  for (n = 0; n < MAX_TCOUNT; ++n) {
    s.block[n] = 0;
  }
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_OP_DELAYED_FOR_LOCK_STAT);
  Debug(CL_WARN, "%s delayed for locks", read_flag ? "read" : "write");
  free_locks(read_flag, i);
  return false;
#endif
}

bool
ClusterHandler::get_read_locks()
{
  ///////////////////////////////////////////////////////////////////////
  // Reacquire locks for the request setup by build_initial_vector().
  // We are called after each read completion prior to posting completion
  ///////////////////////////////////////////////////////////////////////
  ClusterState &s = read;
  int i, n;
  int bytes_processed;
  int vec_bytes_remainder;
  int iov_done[MAX_TCOUNT];

  memset((char *)iov_done, 0, sizeof(int) * MAX_TCOUNT);

  // Compute bytes transferred on a per vector basis
  bytes_processed = s.did - s.bytes_xfered; // not including bytes in this xfer

  i = -1;
  for (n = 0; n < s.n_iov; ++n) {
    bytes_processed -= s.iov[n].iov_len;
    if (bytes_processed >= 0) {
      iov_done[n] = s.iov[n].iov_len;
    } else {
      iov_done[n] = s.iov[n].iov_len + bytes_processed;
      if (i < 0) {
        i = n; // note i/o start vector

        // Now at vector where last transfer started,
        // make considerations for the last transfer on this vector.

        vec_bytes_remainder = (s.iov[n].iov_len - iov_done[n]);
        bytes_processed     = s.bytes_xfered;

        bytes_processed -= vec_bytes_remainder;
        if (bytes_processed >= 0) {
          iov_done[n] = vec_bytes_remainder;
        } else {
          iov_done[n] = vec_bytes_remainder + bytes_processed;
          break;
        }
      } else {
        break;
      }
    }
  }
  ink_release_assert(i >= 0);

  // Start lock acquisition at the first vector where we started
  //  the last read.
  //
  //  Note: We are assuming that free space descriptors follow
  //        the data descriptors.

  for (; i < s.n_iov; ++i) {
    if ((s.msg.descriptor[i].type == CLUSTER_SEND_DATA) && (s.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL)) {
      // Only user channels require locks

      ClusterVConnection *vc = channels[s.msg.descriptor[i].channel];
      if (!VALID_CHANNEL(vc) || ((s.msg.descriptor[i].sequence_number) != CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) ||
          !vc_ok_read(vc)) {
        // Channel no longer valid, lock not needed since we
        //  already have a reference to the buffer
        continue;
      }

      ink_assert(!(ProxyMutex *)vc->read_locked);
      vc->read_locked = vc->read.vio.mutex;
      if (vc->byte_bank_q.head ||
          !MUTEX_TAKE_TRY_LOCK_FOR_SPIN(vc->read.vio.mutex, thread, vc->read.vio._cont, READ_LOCK_SPIN_COUNT)) {
        // Pending byte bank completions or lock acquire failure.

        vc->read_locked = NULL;
        continue;
      }
      // Since we now have the mutex, really see if reads are allowed.

      if (!vc_ok_read(vc)) {
        MUTEX_UNTAKE_LOCK(vc->read.vio.mutex, thread);
        vc->read_locked = NULL;
        continue;
      }
      // Lock acquire success, move read bytes into VC

      int64_t read_avail = vc->read_block->read_avail();

      if (!vc->pending_remote_fill && read_avail) {
        Debug("cluster_vc_xfer", "Deferred fill ch %d %p %" PRId64 " bytes", vc->channel, vc, read_avail);

        vc->read.vio.buffer.writer()->append_block(vc->read_block->clone());
        if (complete_channel_read(read_avail, vc)) {
          vc->read_block->consume(read_avail);
        }
      }
    }
  }
  return true; // success
}

bool
ClusterHandler::get_write_locks()
{
  ///////////////////////////////////////////////////////////////////////
  // Reacquire locks for the request setup by build_initial_vector().
  // We are called after the entire write completes prior to
  // posting completion.
  ///////////////////////////////////////////////////////////////////////
  ClusterState &s = write;
  int i;

  for (i = 0; i < s.msg.count; ++i) {
    if ((s.msg.descriptor[i].type == CLUSTER_SEND_DATA) && (s.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL)) {
      // Only user channels require locks

      ClusterVConnection *vc = channels[s.msg.descriptor[i].channel];
      if (!VALID_CHANNEL(vc) || (s.msg.descriptor[i].sequence_number) != CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) {
        // Channel no longer valid, lock not needed since we
        //  already have a reference to the buffer
        continue;
      }
      ink_assert(!(ProxyMutex *)vc->write_locked);
      vc->write_locked = vc->write.vio.mutex;
#ifdef CLUSTER_TOMCAT
      if (vc->write_locked &&
          !MUTEX_TAKE_TRY_LOCK_FOR_SPIN(vc->write.vio.mutex, thread, vc->write.vio._cont, WRITE_LOCK_SPIN_COUNT)) {
#else
      if (!MUTEX_TAKE_TRY_LOCK_FOR_SPIN(vc->write.vio.mutex, thread, vc->write.vio._cont, WRITE_LOCK_SPIN_COUNT)) {
#endif
        // write lock acquire failed, free all acquired locks and retry later
        vc->write_locked = 0;
        free_locks(CLUSTER_WRITE, i);
        return false;
      }
    }
  }
  return true;
}

void
ClusterHandler::swap_descriptor_bytes()
{
  for (int i = 0; i < read.msg.count; i++) {
    read.msg.descriptor[i].SwapBytes();
  }
}

void
ClusterHandler::process_set_data_msgs()
{
  uint32_t cluster_function_index;
  //
  // Cluster set_data messages must always be processed ahead of all
  // messages and data.  By convention, set_data messages (highest priority
  // messages) always reside in the beginning of the descriptor
  // and small control message structures.
  //

  /////////////////////////////////////////////
  // Process small control set_data messages.
  /////////////////////////////////////////////
  if (!read.msg.did_small_control_set_data) {
    char *p    = (char *)&read.msg.descriptor[read.msg.count];
    char *endp = p + read.msg.control_bytes;
    while (p < endp) {
      if (needByteSwap) {
        ats_swap32((uint32_t *)p);                     // length
        ats_swap32((uint32_t *)(p + sizeof(int32_t))); // function code
      }
      int len                = *(int32_t *)p;
      cluster_function_index = *(uint32_t *)(p + sizeof(int32_t));

      if ((cluster_function_index < (uint32_t)SIZE_clusterFunction) &&
          (cluster_function_index == SET_CHANNEL_DATA_CLUSTER_FUNCTION)) {
        clusterFunction[SET_CHANNEL_DATA_CLUSTER_FUNCTION].pfn(this, p + (2 * sizeof(uint32_t)), len - sizeof(uint32_t));
        // Mark message as processed.
        *((uint32_t *)(p + sizeof(uint32_t))) = ~*((uint32_t *)(p + sizeof(uint32_t)));
        p += (2 * sizeof(uint32_t)) + (len - sizeof(uint32_t));
        p = (char *)DOUBLE_ALIGN(p);
      } else {
        // Reverse swap since this message will be reprocessed.

        if (needByteSwap) {
          ats_swap32((uint32_t *)p);                     // length
          ats_swap32((uint32_t *)(p + sizeof(int32_t))); // function code
        }
        break; // End of set_data messages
      }
    }
    read.msg.control_data_offset        = p - (char *)&read.msg.descriptor[read.msg.count];
    read.msg.did_small_control_set_data = 1;
  }
  /////////////////////////////////////////////
  // Process large control set_data messages.
  /////////////////////////////////////////////
  if (!read.msg.did_large_control_set_data) {
    IncomingControl *ic = incoming_control.head;

    while (ic) {
      if (needByteSwap) {
        ats_swap32((uint32_t *)ic->data); // function code
      }
      cluster_function_index = *((uint32_t *)ic->data);

      if ((cluster_function_index < (uint32_t)SIZE_clusterFunction) &&
          (cluster_function_index == SET_CHANNEL_DATA_CLUSTER_FUNCTION)) {
        char *p = ic->data;
        clusterFunction[SET_CHANNEL_DATA_CLUSTER_FUNCTION].pfn(this, (void *)(p + sizeof(int32_t)), ic->len - sizeof(int32_t));

        // Reverse swap since this will be processed again for deallocation.
        if (needByteSwap) {
          ats_swap32((uint32_t *)p);                     // length
          ats_swap32((uint32_t *)(p + sizeof(int32_t))); // function code
        }
        // Mark message as processed.
        // Defer dellocation until entire read is complete.
        *((uint32_t *)p) = ~*((uint32_t *)p);

        ic = (IncomingControl *)ic->link.next;
      } else {
        // Reverse swap action this message will be reprocessed.
        if (needByteSwap) {
          ats_swap32((uint32_t *)ic->data); // function code
        }
        break;
      }
    }
    read.msg.did_large_control_set_data = 1;
  }
}

void
ClusterHandler::process_small_control_msgs()
{
  if (read.msg.did_small_control_msgs) {
    return;
  } else {
    read.msg.did_small_control_msgs = 1;
  }

  ink_hrtime now = Thread::get_hrtime();
  char *p        = (char *)&read.msg.descriptor[read.msg.count] + read.msg.control_data_offset;
  char *endp     = (char *)&read.msg.descriptor[read.msg.count] + read.msg.control_bytes;

  while (p < endp) {
    /////////////////////////////////////////////////////////////////
    // Place non cluster small incoming messages on external
    // incoming queue for processing by callout threads.
    /////////////////////////////////////////////////////////////////
    if (needByteSwap) {
      ats_swap32((uint32_t *)p);                     // length
      ats_swap32((uint32_t *)(p + sizeof(int32_t))); // function code
    }
    int len = *(int32_t *)p;
    p += sizeof(int32_t);
    uint32_t cluster_function_index = *(uint32_t *)p;
    ink_release_assert(cluster_function_index != SET_CHANNEL_DATA_CLUSTER_FUNCTION);

    if (cluster_function_index >= (uint32_t)SIZE_clusterFunction) {
      Warning("1Bad cluster function index (small control)");
      p += len;

    } else if (clusterFunction[cluster_function_index].ClusterFunc) {
      //////////////////////////////////////////////////////////////////////
      // Cluster function, can only be processed in ET_CLUSTER thread
      //////////////////////////////////////////////////////////////////////
      p += sizeof(uint32_t);
      clusterFunction[cluster_function_index].pfn(this, p, len - sizeof(int32_t));
      p += (len - sizeof(int32_t));

    } else {
      ///////////////////////////////////////////////////////
      // Non Cluster function, defer to callout threads
      ///////////////////////////////////////////////////////
      IncomingControl *ic = IncomingControl::alloc();
      ic->recognized_time = now;
      ic->len             = len;
      ic->alloc_data();
      memcpy(ic->data, p, ic->len);
      SetHighBit(&ic->len); // mark as small cntl
      ink_atomiclist_push(&external_incoming_control, (void *)ic);
      p += len;
    }
    p = (char *)DOUBLE_ALIGN(p);
  }
}

void
ClusterHandler::process_large_control_msgs()
{
  if (read.msg.did_large_control_msgs) {
    return;
  } else {
    read.msg.did_large_control_msgs = 1;
  }

  ////////////////////////////////////////////////////////////////
  // Place non cluster large incoming messages on external
  // incoming queue for processing by callout threads.
  ////////////////////////////////////////////////////////////////
  IncomingControl *ic = NULL;
  uint32_t cluster_function_index;

  while ((ic = incoming_control.dequeue())) {
    if (needByteSwap) {
      ats_swap32((uint32_t *)ic->data); // function code
    }
    cluster_function_index = *((uint32_t *)ic->data);
    ink_release_assert(cluster_function_index != SET_CHANNEL_DATA_CLUSTER_FUNCTION);

    if (cluster_function_index == (uint32_t)~SET_CHANNEL_DATA_CLUSTER_FUNCTION) {
      // SET_CHANNEL_DATA_CLUSTER_FUNCTION already processed.
      // Just do memory deallocation.

      if (!clusterFunction[SET_CHANNEL_DATA_CLUSTER_FUNCTION].fMalloced)
        ic->freeall();
      continue;
    }

    if (cluster_function_index >= (uint32_t)SIZE_clusterFunction) {
      Warning("Bad cluster function index (large control)");
      ic->freeall();

    } else if (clusterFunction[cluster_function_index].ClusterFunc) {
      // Cluster message, process in ET_CLUSTER thread
      clusterFunction[cluster_function_index].pfn(this, (void *)(ic->data + sizeof(int32_t)), ic->len - sizeof(int32_t));

      // Deallocate memory
      if (!clusterFunction[cluster_function_index].fMalloced)
        ic->freeall();

    } else {
      // Non Cluster message, process in non ET_CLUSTER thread
      ink_atomiclist_push(&external_incoming_control, (void *)ic);
    }
  }
}

void
ClusterHandler::process_freespace_msgs()
{
  if (read.msg.did_freespace_msgs) {
    return;
  } else {
    read.msg.did_freespace_msgs = 1;
  }

  int i;
  //
  // unpack CLUSTER_SEND_FREE (VC free space) messages and update
  // the free space in the target VC(s).
  //
  for (i = 0; i < read.msg.count; i++) {
    if (read.msg.descriptor[i].type == CLUSTER_SEND_FREE && read.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL) {
      int c = read.msg.descriptor[i].channel;
      if (c < n_channels && VALID_CHANNEL(channels[c]) &&
          (CLUSTER_SEQUENCE_NUMBER(channels[c]->token.sequence_number) == read.msg.descriptor[i].sequence_number)) {
        //
        // VC received freespace message, move it to the
        // current bucket, since it may have data to
        // write (WRITE_VC_PRIORITY).
        //
        channels[c]->remote_free = read.msg.descriptor[i].length;
        vcs_push(channels[c], VC_CLUSTER_WRITE);
      }
    }
  }
}

void
ClusterHandler::add_to_byte_bank(ClusterVConnection *vc)
{
  ByteBankDescriptor *bb_desc       = ByteBankDescriptor::ByteBankDescriptor_alloc(vc->read_block);
  bool pending_byte_bank_completion = vc->byte_bank_q.head ? true : false;

  // Put current byte bank descriptor on completion list
  vc->byte_bank_q.enqueue(bb_desc);

  // Start byte bank completion action if not active
  if (!pending_byte_bank_completion) {
    ClusterVC_remove_read(vc);
    delayed_reads.push(vc);
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_LEVEL1_BANK_STAT);
  } else {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_MULTILEVEL_BANK_STAT);
  }
  vc->read_block = 0;
}

void
ClusterHandler::update_channels_read()
{
  //
  // Update channels from which data has been read.
  //
  int i;
  int len;
  // This isn't used.
  // int nread = read.bytes_xfered;

  process_set_data_msgs();

  //
  // update the ClusterVConnections
  //
  for (i = 0; i < read.msg.count; i++) {
    if (read.msg.descriptor[i].type == CLUSTER_SEND_DATA && read.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL) {
      ClusterVConnection *vc = channels[read.msg.descriptor[i].channel];
      if (VALID_CHANNEL(vc) && (read.msg.descriptor[i].sequence_number) == CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) {
        vc->last_activity_time = current_time; // note activity time

        len = read.msg.descriptor[i].length;
        if (!len) {
          continue;
        }

        if (!vc->pending_remote_fill && vc_ok_read(vc) && (!((ProxyMutex *)vc->read_locked) || vc->byte_bank_q.head)) {
          //
          // Byte bank active or unable to acquire lock on VC.
          // Move data into the byte bank and attempt delivery
          // at the next periodic event.
          //
          vc->read_block->fill(len); // note bytes received
          add_to_byte_bank(vc);

        } else {
          if (vc->pending_remote_fill || ((ProxyMutex *)vc->read_locked && vc_ok_read(vc))) {
            vc->read_block->fill(len); // note bytes received
            if (!vc->pending_remote_fill) {
              vc->read.vio.buffer.writer()->append_block(vc->read_block->clone());
              vc->read_block->consume(len); // note bytes moved to user
            }
            complete_channel_read(len, vc);
          }
        }
      }
    }
  }

  // Processs control and freespace messages
  process_small_control_msgs();
  process_large_control_msgs();
  process_freespace_msgs();
}

//
// This member function is run in a non ET_CLUSTER thread, which
// performs the input message processing on behalf of ET_CLUSTER.
// Primary motivation is to allow blocking and unbounded runtime
// for message processing which cannot be done with a ET_CLUSTER thread.
//
int
ClusterHandler::process_incoming_callouts(ProxyMutex *m)
{
  ProxyMutex *mutex = m;
  ink_hrtime now;
  //
  // Atomically dequeue all active requests from the external queue and
  // move them to the local working queue.  Insertion queue order is
  // maintained.
  //
  Queue<IncomingControl> local_incoming_control;
  IncomingControl *ic_ext_next;
  IncomingControl *ic_ext;

  while (true) {
    ic_ext = (IncomingControl *)ink_atomiclist_popall(&external_incoming_control);
    if (!ic_ext)
      break;

    while (ic_ext) {
      ic_ext_next       = (IncomingControl *)ic_ext->link.next;
      ic_ext->link.next = NULL;
      local_incoming_control.push(ic_ext);
      ic_ext = ic_ext_next;
    }

    // Perform callout actions for each message.
    int small_control_msg;
    IncomingControl *ic = NULL;

    while ((ic = local_incoming_control.pop())) {
      LOG_EVENT_TIME(ic->recognized_time, inmsg_time_dist, inmsg_events);

      // Determine if this a small control message
      small_control_msg = IsHighBitSet(&ic->len);
      ClearHighBit(&ic->len); // Clear small msg flag bit

      if (small_control_msg) {
        int len                         = ic->len;
        char *p                         = ic->data;
        uint32_t cluster_function_index = *(uint32_t *)p;
        p += sizeof(uint32_t);

        if (cluster_function_index < (uint32_t)SIZE_clusterFunction) {
          ////////////////////////////////
          // Invoke processing function
          ////////////////////////////////
          ink_assert(!clusterFunction[cluster_function_index].ClusterFunc);
          clusterFunction[cluster_function_index].pfn(this, p, len - sizeof(int32_t));
          now = Thread::get_hrtime();
          CLUSTER_SUM_DYN_STAT(CLUSTER_CTRL_MSGS_RECV_TIME_STAT, now - ic->recognized_time);
        } else {
          Warning("2Bad cluster function index (small control)");
        }
        // Deallocate memory
        if (!clusterFunction[cluster_function_index].fMalloced)
          ic->freeall();

      } else {
        ink_assert(ic->len > 4);
        uint32_t cluster_function_index = *(uint32_t *)ic->data;
        bool valid_index;

        if (cluster_function_index < (uint32_t)SIZE_clusterFunction) {
          valid_index = true;
          ////////////////////////////////
          // Invoke processing function
          ////////////////////////////////
          ink_assert(!clusterFunction[cluster_function_index].ClusterFunc);
          clusterFunction[cluster_function_index].pfn(this, (void *)(ic->data + sizeof(int32_t)), ic->len - sizeof(int32_t));
          now = Thread::get_hrtime();
          CLUSTER_SUM_DYN_STAT(CLUSTER_CTRL_MSGS_RECV_TIME_STAT, now - ic->recognized_time);
        } else {
          valid_index = false;
          Warning("2Bad cluster function index (large control)");
        }
        if (valid_index && !clusterFunction[cluster_function_index].fMalloced)
          ic->freeall();
      }
    }
  }
  return EVENT_CONT;
}

void
ClusterHandler::update_channels_partial_read()
{
  //
  // We were unable to read the computed amount.  Reflect the partial
  // amount read in the associated VC read buffer data structures.
  //
  int i;
  int64_t res = read.bytes_xfered;

  if (!res) {
    return;
  }
  ink_assert(res <= read.did);

  // how much of the iov was done

  int64_t iov_done[MAX_TCOUNT];
  int64_t total        = 0;
  int64_t already_read = read.did - read.bytes_xfered;

  for (i = 0; i < read.n_iov; i++) {
    ink_release_assert(already_read >= 0);
    iov_done[i] = read.iov[i].iov_len;

    // Skip over bytes already processed
    if (already_read) {
      already_read -= iov_done[i];
      if (already_read < 0) {
        iov_done[i]  = -already_read; // bytes remaining
        already_read = 0;
      } else {
        iov_done[i] = 0;
        continue;
      }
    }
    // Adjustments for partial read for the current transfer
    res -= iov_done[i];
    if (res < 0) {
      iov_done[i] += res;
      res = 0;
    } else {
      total += iov_done[i];
    }
  }
  ink_assert(total <= read.did);

  int read_all_large_control_msgs = 0;
  //
  // update the ClusterVConnections buffer pointers
  //
  for (i = 0; i < read.msg.count; i++) {
    if (read.msg.descriptor[i].type == CLUSTER_SEND_DATA && read.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL) {
      ClusterVConnection *vc = channels[read.msg.descriptor[i].channel];
      if (VALID_CHANNEL(vc) && (read.msg.descriptor[i].sequence_number) == CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) {
        if (vc->pending_remote_fill || (vc_ok_read(vc) && (vc->iov_map != CLUSTER_IOV_NONE))) {
          vc->last_activity_time = current_time; // note activity time
          ClusterVConnState *s   = &vc->read;
          ink_assert(vc->iov_map < read.n_iov);
          int len = iov_done[vc->iov_map];

          if (len) {
            if (!read_all_large_control_msgs) {
              //
              // Since all large set_data control messages reside at the
              // beginning, all have been read if the first non-control
              // descriptor contains > 0 bytes.
              // Process them ahead of any VC data completion actions
              // followed by small control and freespace message processing.
              //
              process_set_data_msgs();
              process_small_control_msgs();
              process_freespace_msgs();
              read_all_large_control_msgs = 1;
            }
            iov_done[vc->iov_map] = 0;
            vc->read_block->fill(len); // note bytes received

            if (!vc->pending_remote_fill) {
              if ((ProxyMutex *)vc->read_locked) {
                Debug("cluster_vc_xfer", "Partial read, credit ch %d %p %d bytes", vc->channel, vc, len);
                s->vio.buffer.writer()->append_block(vc->read_block->clone());
                if (complete_channel_read(len, vc)) {
                  vc->read_block->consume(len); // note bytes moved to user
                }

              } else {
                // If we have all the data for the VC, move it
                // into the byte bank.  Otherwise, do nothing since
                // we will resume the read at this VC.

                if (len == (int)read.msg.descriptor[i].length) {
                  Debug("cluster_vc_xfer", "Partial read, byte bank move ch %d %p %d bytes", vc->channel, vc, len);
                  add_to_byte_bank(vc);
                }
              }
            } else {
              Debug("cluster_vc_xfer", "Partial remote fill read, credit ch %d %p %d bytes", vc->channel, vc, len);
              complete_channel_read(len, vc);
            }
            read.msg.descriptor[i].length -= len;
            ink_assert(((int)read.msg.descriptor[i].length) >= 0);
          }
          Debug(CL_TRACE, "partial_channel_read chan=%d len=%d", vc->channel, len);
        }
      }
    }
  }
}

bool
ClusterHandler::complete_channel_read(int len, ClusterVConnection *vc)
{
  //
  // We have processed a complete VC read request message for a channel,
  // perform completion actions.
  //
  ClusterVConnState *s = &vc->read;

  if (vc->pending_remote_fill) {
    Debug(CL_TRACE, "complete_channel_read chan=%d len=%d", vc->channel, len);
    vc->initial_data_bytes += len;
    ++vc->pending_remote_fill; // Note completion
    return (vc->closed ? false : true);
  }

  if (vc->closed)
    return false; // No action if already closed

  ink_assert((ProxyMutex *)s->vio.mutex == (ProxyMutex *)s->vio._cont->mutex);

  Debug("cluster_vc_xfer", "Complete read, credit ch %d %p %d bytes", vc->channel, vc, len);
  s->vio.ndone += len;

  if (s->vio.ntodo() <= 0) {
    s->enabled = 0;
    if (cluster_signal_and_update_locked(VC_EVENT_READ_COMPLETE, vc, s) == EVENT_DONE)
      return false;
  } else {
    if (cluster_signal_and_update_locked(VC_EVENT_READ_READY, vc, s) == EVENT_DONE)
      return false;
    if (s->vio.ntodo() <= 0)
      s->enabled = 0;
  }

  vcs_push(vc, VC_CLUSTER_READ);
  return true;
}

void
ClusterHandler::finish_delayed_reads()
{
  //
  // Process pending VC delayed reads generated in the last read from
  // the node to node connection. For explanation of "delayed read" see
  // comments at the beginning of the member functions for ClusterHandler.
  //
  ClusterVConnection *vc = NULL;
  DLL<ClusterVConnectionBase> l;
  while ((vc = (ClusterVConnection *)delayed_reads.pop())) {
    MUTEX_TRY_LOCK_SPIN(lock, vc->read.vio.mutex, thread, READ_LOCK_SPIN_COUNT);
    if (lock.is_locked()) {
      if (vc_ok_read(vc)) {
        ink_assert(!vc->read.queue);
        ByteBankDescriptor *d;

        while ((d = vc->byte_bank_q.dequeue())) {
          if (vc->read.queue) {
            // Previous complete_channel_read() put us back on the list,
            //  remove our self to process another byte bank completion
            ClusterVC_remove_read(vc);
          }
          Debug("cluster_vc_xfer", "Delayed read, credit ch %d %p %" PRId64 " bytes", vc->channel, vc,
                d->get_block()->read_avail());
          vc->read.vio.buffer.writer()->append_block(d->get_block());

          if (complete_channel_read(d->get_block()->read_avail(), vc)) {
            ByteBankDescriptor::ByteBankDescriptor_free(d);
          } else {
            ByteBankDescriptor::ByteBankDescriptor_free(d);
            break;
          }
        }
      }
    } else
      l.push(vc);
  }
  delayed_reads = l;
}

void
ClusterHandler::update_channels_written()
{
  //
  // We have sucessfully pushed the write data for the VC(s) described
  // by the descriptors.
  // Move the channels in this bucket to a new bucket.
  // Lower the priority of those with too little data and raise that of
  // those with too much data.
  //
  ink_hrtime now;
  for (int i = 0; i < write.msg.count; i++) {
    if (write.msg.descriptor[i].type == CLUSTER_SEND_DATA) {
      if (write.msg.descriptor[i].channel != CLUSTER_CONTROL_CHANNEL) {
        ClusterVConnection *vc = channels[write.msg.descriptor[i].channel];
        if (VALID_CHANNEL(vc) && (write.msg.descriptor[i].sequence_number) == CLUSTER_SEQUENCE_NUMBER(vc->token.sequence_number)) {
          if (vc->pending_remote_fill) {
            Debug(CL_TRACE, "update_channels_written chan=%d seqno=%d len=%d", write.msg.descriptor[i].channel,
                  write.msg.descriptor[i].sequence_number, write.msg.descriptor[i].length);
            vc->pending_remote_fill = 0;
            vc->remote_write_block  = 0; // free data block
            continue;                    // ignore remote write fill VC(s)
          }

          ClusterVConnState *s = &vc->write;
          int len              = write.msg.descriptor[i].length;
          vc->write_bytes_in_transit -= len;
          ink_release_assert(vc->write_bytes_in_transit >= 0);
          Debug(CL_PROTO, "(%d) data sent %d %" PRId64, write.msg.descriptor[i].channel, len, s->vio.ndone);

          if (vc_ok_write(vc)) {
            vc->last_activity_time = current_time; // note activity time
            int64_t ndone          = vc->was_closed() ? 0 : s->vio.ndone;

            if (ndone < vc->remote_free) {
              vcs_push(vc, VC_CLUSTER_WRITE);
            }
          }
        }
      } else {
        //
        // Free up outgoing control message space
        //
        OutgoingControl *oc = write.msg.outgoing_control.dequeue();
        oc->free_data();
        oc->mutex = NULL;
        now       = Thread::get_hrtime();
        CLUSTER_SUM_DYN_STAT(CLUSTER_CTRL_MSGS_SEND_TIME_STAT, now - oc->submit_time);
        LOG_EVENT_TIME(oc->submit_time, cluster_send_time_dist, cluster_send_events);
        oc->freeall();
      }
    }
  }
  //
  // For compound messages, deallocate the data and header descriptors.
  // The deallocation of the data descriptor will indirectly invoke
  // the free memory proc described in set_data.
  //
  invoke_remote_data_args *args;
  OutgoingControl *hdr_oc;
  while ((hdr_oc = write.msg.outgoing_callout.dequeue())) {
    args = (invoke_remote_data_args *)(hdr_oc->data + sizeof(int32_t));
    ink_assert(args->magicno == invoke_remote_data_args::MagicNo);

    // Free data descriptor
    args->data_oc->free_data(); // invoke memory free callback
    args->data_oc->mutex = NULL;
    args->data_oc->freeall();

    // Free descriptor
    hdr_oc->free_data();
    hdr_oc->mutex = NULL;
    now           = Thread::get_hrtime();
    CLUSTER_SUM_DYN_STAT(CLUSTER_CTRL_MSGS_SEND_TIME_STAT, now - hdr_oc->submit_time);
    LOG_EVENT_TIME(hdr_oc->submit_time, cluster_send_time_dist, cluster_send_events);
    hdr_oc->freeall();
  }
}

int
ClusterHandler::build_write_descriptors()
{
  //
  // Construct the write descriptors for VC write data in the current
  // write_vcs bucket with considerations for maximum elements per
  // write (struct iovec system maximum).
  //
  int count_bucket            = cur_vcs;
  int tcount                  = write.msg.count + 2; // count + descriptor
  int write_descriptors_built = 0;
  int valid;
  int list_len = 0;
  ClusterVConnection *vc, *vc_next;

  //
  // Build descriptors for connections with stuff to send.
  //
  vc = (ClusterVConnection *)ink_atomiclist_popall(&write_vcs_ready);
  while (vc) {
    enter_exit(&cls_build_writes_entered, &cls_writes_exited);
    vc_next              = (ClusterVConnection *)vc->ready_alink.next;
    vc->ready_alink.next = NULL;
    list_len++;
    if (VC_CLUSTER_CLOSED == vc->type) {
      vc->in_vcs = false;
      vc->type   = VC_NULL;
      clusterVCAllocator.free(vc);
      vc = vc_next;
      continue;
    }

    if (tcount >= MAX_TCOUNT) {
      vcs_push(vc, VC_CLUSTER_WRITE);
    } else {
      vc->in_vcs = false;
      cluster_reschedule_offset(this, vc, &vc->write, 0);
      tcount++;
    }
    vc = vc_next;
  }
  if (list_len) {
    CLUSTER_SUM_DYN_STAT(CLUSTER_VC_WRITE_LIST_LEN_STAT, list_len);
  }

  tcount  = write.msg.count + 2;
  vc_next = (ClusterVConnection *)write_vcs[count_bucket].head;
  while (vc_next) {
    vc      = vc_next;
    vc_next = (ClusterVConnection *)vc->write.link.next;

    if (VC_CLUSTER_CLOSED == vc->type) {
      vc->type = VC_NULL;
      clusterVCAllocator.free(vc);
      continue;
    }

    if (tcount >= MAX_TCOUNT)
      break;

    valid = valid_for_data_write(vc);
    if (-1 == valid) {
      vcs_push(vc, VC_CLUSTER_WRITE);
    } else if (valid) {
      ink_assert(vc->write_locked); // Acquired in valid_for_data_write()
      if ((vc->remote_free > (vc->write.vio.ndone - vc->write_list_bytes)) && channels[vc->channel] == vc) {
        ink_assert(vc->write_list && vc->write_list_bytes);

        int d                                   = write.msg.count;
        write.msg.descriptor[d].type            = CLUSTER_SEND_DATA;
        write.msg.descriptor[d].channel         = vc->channel;
        write.msg.descriptor[d].sequence_number = vc->token.sequence_number;
        int s                                   = vc->write_list_bytes;
        ink_release_assert(s <= MAX_CLUSTER_SEND_LENGTH);

        // Transfer no more than nbytes
        if ((vc->write.vio.ndone - s) > vc->write.vio.nbytes)
          s = vc->write.vio.nbytes - (vc->write.vio.ndone - s);

        if ((vc->write.vio.ndone - s) > vc->remote_free)
          s                            = vc->remote_free - (vc->write.vio.ndone - s);
        write.msg.descriptor[d].length = s;
        write.msg.count++;
        tcount++;
        write_descriptors_built++;

#ifdef CLUSTER_STATS
        _vc_writes++;
        _vc_write_bytes += s;
#endif

      } else {
        MUTEX_UNTAKE_LOCK(vc->write_locked, thread);
        vc->write_locked = NULL;

        if (channels[vc->channel] == vc)
          CLUSTER_INCREMENT_DYN_STAT(CLUSTER_NO_REMOTE_SPACE_STAT);
      }
    }
  }
  return (write_descriptors_built);
}

int
ClusterHandler::build_freespace_descriptors()
{
  //
  // Construct the write descriptors for VC freespace data in the current
  // read_vcs bucket with considerations for maximum elements per
  // write (struct iovec system maximum) and for pending elements already
  // in the list.
  //
  int count_bucket                = cur_vcs;
  int tcount                      = write.msg.count + 2; // count + descriptor require 2 iovec(s)
  int freespace_descriptors_built = 0;
  int s                           = 0;
  int list_len                    = 0;
  ClusterVConnection *vc, *vc_next;

  //
  // Build descriptors for available space
  //
  vc = (ClusterVConnection *)ink_atomiclist_popall(&read_vcs_ready);
  while (vc) {
    enter_exit(&cls_build_reads_entered, &cls_reads_exited);
    vc_next              = (ClusterVConnection *)vc->ready_alink.next;
    vc->ready_alink.next = NULL;
    list_len++;
    if (VC_CLUSTER_CLOSED == vc->type) {
      vc->in_vcs = false;
      vc->type   = VC_NULL;
      clusterVCAllocator.free(vc);
      vc = vc_next;
      continue;
    }

    if (tcount >= MAX_TCOUNT) {
      vcs_push(vc, VC_CLUSTER_READ);
    } else {
      vc->in_vcs = false;
      cluster_reschedule_offset(this, vc, &vc->read, 0);
      tcount++;
    }
    vc = vc_next;
  }
  if (list_len) {
    CLUSTER_SUM_DYN_STAT(CLUSTER_VC_READ_LIST_LEN_STAT, list_len);
  }

  tcount  = write.msg.count + 2;
  vc_next = (ClusterVConnection *)read_vcs[count_bucket].head;
  while (vc_next) {
    vc      = vc_next;
    vc_next = (ClusterVConnection *)vc->read.link.next;

    if (VC_CLUSTER_CLOSED == vc->type) {
      vc->type = VC_NULL;
      clusterVCAllocator.free(vc);
      continue;
    }

    if (tcount >= MAX_TCOUNT)
      break;

    s = valid_for_freespace_write(vc);
    if (-1 == s) {
      vcs_push(vc, VC_CLUSTER_READ);
    } else if (s) {
      if (vc_ok_read(vc) && channels[vc->channel] == vc) {
        // Send free space only if changed
        int d                                   = write.msg.count;
        write.msg.descriptor[d].type            = CLUSTER_SEND_FREE;
        write.msg.descriptor[d].channel         = vc->channel;
        write.msg.descriptor[d].sequence_number = vc->token.sequence_number;

        ink_assert(s > 0);
        write.msg.descriptor[d].length = s;
        vc->last_local_free            = s;
        Debug(CL_PROTO, "(%d) free space priority %d", vc->channel, vc->read.priority);
        write.msg.count++;
        tcount++;
        freespace_descriptors_built++;
      }
    }
  }
  return (freespace_descriptors_built);
}

int
ClusterHandler::build_controlmsg_descriptors()
{
  //
  // Construct the write descriptors for control message data in the
  // outgoing_control queue with considerations for maximum elements per
  // write (struct iovec system maximum) and for elements already
  // in the list.
  //
  int tcount             = write.msg.count + 2; // count + descriptor require 2 iovec(s)
  int control_msgs_built = 0;
  bool compound_msg; // msg + chan data
  //
  // Build descriptors for control messages
  //
  OutgoingControl *c = NULL;
  int control_bytes  = 0;
  int q              = 0;

  while (tcount < (MAX_TCOUNT - 1)) { // -1 to allow for compound messages
    c = outgoing_control[q].pop();
    if (!c) {
      // Move elements from global outgoing_control to local queue
      OutgoingControl *c_next;
      c = (OutgoingControl *)ink_atomiclist_popall(&outgoing_control_al[q]);
      if (c == 0) {
        if (++q >= CLUSTER_CMSG_QUEUES) {
          break;
        } else {
          continue;
        }
      }
      while (c) {
        c_next       = (OutgoingControl *)c->link.next;
        c->link.next = NULL;
        outgoing_control[q].push(c);
        c = c_next;
      }
      continue;

    } else {
      compound_msg = (*((int32_t *)c->data) == -1); // (msg+chan data)?
    }
    if (!compound_msg && c->len <= SMALL_CONTROL_MESSAGE &&
        // check if the receiving cluster function will want to malloc'ed data
        !clusterFunction[*(int32_t *)c->data].fMalloced && control_bytes + c->len + sizeof(int32_t) * 2 + 7 < CONTROL_DATA) {
      write.msg.outgoing_small_control.enqueue(c);
      control_bytes += c->len + sizeof(int32_t) * 2 + 7; // safe approximation
      control_msgs_built++;

      if (clusterFunction[*(int32_t *)c->data].post_pfn) {
        clusterFunction[*(int32_t *)c->data].post_pfn(this, c->data + sizeof(int32_t), c->len);
      }
      continue;
    }
    //
    // Build large control message descriptor
    //
    if (compound_msg) {
      // Extract out components of compound message.
      invoke_remote_data_args *cmhdr = (invoke_remote_data_args *)(c->data + sizeof(int32_t));
      OutgoingControl *oc_header     = c;
      OutgoingControl *oc_msg        = cmhdr->msg_oc;
      OutgoingControl *oc_data       = cmhdr->data_oc;

      ink_assert(cmhdr->magicno == invoke_remote_data_args::MagicNo);
      //
      // Build descriptors and order the data before the reply message.
      // Reply message processing assumes data completion action performed
      // prior to processing completion message.
      // Not an issue today since channel data is always processed first.
      //
      int d;
      d                                       = write.msg.count;
      write.msg.descriptor[d].type            = CLUSTER_SEND_DATA;
      write.msg.descriptor[d].channel         = cmhdr->dest_channel;
      write.msg.descriptor[d].length          = oc_data->len;
      write.msg.descriptor[d].sequence_number = cmhdr->token.sequence_number;

#ifdef CLUSTER_STATS
      _vc_write_bytes += oc_data->len;
#endif

      // Setup remote write fill iovec.  Remote write fills have no VIO.
      ClusterVConnection *vc = channels[cmhdr->dest_channel];

      if (VALID_CHANNEL(vc) && vc->pending_remote_fill) {
        ink_release_assert(!vc->remote_write_block);
        vc->remote_write_block = oc_data->get_block();

        // Note: No array overrun since we are bounded by (MAX_TCOUNT-1).
        write.msg.count++;
        tcount++;
        control_msgs_built++;
        d = write.msg.count;
        write.msg.outgoing_control.enqueue(oc_msg);
        write.msg.descriptor[d].type    = CLUSTER_SEND_DATA;
        write.msg.descriptor[d].channel = CLUSTER_CONTROL_CHANNEL;
        write.msg.descriptor[d].length  = oc_msg->len;

#ifdef CLUSTER_STATS
        _control_write_bytes += oc_msg->len;
#endif

        write.msg.count++;
        tcount++;
        control_msgs_built++;

        // Queue header to process buffer free memory callbacks after send.
        write.msg.outgoing_callout.enqueue(oc_header);

      } else {
        // Operation cancelled free memory.
        Warning("Pending remote read fill aborted chan=%d len=%d", cmhdr->dest_channel, oc_data->len);

        // Free compound message
        oc_header->free_data();
        oc_header->mutex = NULL;
        oc_header->freeall();

        // Free response message
        oc_msg->free_data();
        oc_msg->mutex = 0;
        oc_msg->freeall();

        // Free data descriptor
        oc_data->free_data(); // invoke memory free callback
        oc_data->mutex = 0;
        oc_data->freeall();
      }

    } else {
      write.msg.outgoing_control.enqueue(c);

      int d                           = write.msg.count;
      write.msg.descriptor[d].type    = CLUSTER_SEND_DATA;
      write.msg.descriptor[d].channel = CLUSTER_CONTROL_CHANNEL;
      write.msg.descriptor[d].length  = c->len;

#ifdef CLUSTER_STATS
      _control_write_bytes += c->len;
#endif

      write.msg.count++;
      tcount++;
      control_msgs_built++;

      if (clusterFunction[*(int32_t *)c->data].post_pfn) {
        clusterFunction[*(int32_t *)c->data].post_pfn(this, c->data + sizeof(int32_t), c->len);
      }
    }
  }
  return control_msgs_built;
}

int
ClusterHandler::add_small_controlmsg_descriptors()
{
  //
  // Move small control message data to free space after descriptors
  //
  char *p            = (char *)&write.msg.descriptor[write.msg.count];
  OutgoingControl *c = NULL;

  while ((c = write.msg.outgoing_small_control.dequeue())) {
    *(int32_t *)p = c->len;
    p += sizeof(int32_t);
    memcpy(p, c->data, c->len);
    c->free_data();
    c->mutex = NULL;
    p += c->len;
    ink_hrtime now = Thread::get_hrtime();
    CLUSTER_SUM_DYN_STAT(CLUSTER_CTRL_MSGS_SEND_TIME_STAT, now - c->submit_time);
    LOG_EVENT_TIME(c->submit_time, cluster_send_time_dist, cluster_send_events);
    c->freeall();
    p = (char *)DOUBLE_ALIGN(p);
  }
  write.msg.control_bytes = p - (char *)&write.msg.descriptor[write.msg.count];

#ifdef CLUSTER_STATS
  _control_write_bytes += write.msg.control_bytes;
#endif

  return 1;
}

struct DestructorLock {
  DestructorLock(EThread *thread)
  {
    have_lock = false;
    t         = thread;
  }
  ~DestructorLock()
  {
    if (have_lock && m) {
      Mutex_unlock(m, t);
    }
    m = 0;
  }
  EThread *t;
  Ptr<ProxyMutex> m;
  bool have_lock;
};

int
ClusterHandler::valid_for_data_write(ClusterVConnection *vc)
{
  //
  // Determine if writes are allowed on this VC
  //
  ClusterVConnState *s = &vc->write;

  ink_assert(!on_stolen_thread);
  ink_assert((ProxyMutex *)!vc->write_locked);

  //
  // Attempt to get the lock, if we miss, push vc into the future
  //
  DestructorLock lock(thread);

retry:
  if ((lock.m = s->vio.mutex)) {
    lock.have_lock = MUTEX_TAKE_TRY_LOCK_FOR_SPIN(lock.m, thread, s->vio._cont, WRITE_LOCK_SPIN_COUNT);
    if (!lock.have_lock) {
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_WRITE_LOCKED_STAT);

#ifdef CLUSTER_STATS
      _dw_missed_lock++;
#endif
      return -1;
    }
  }

  if (vc->was_closed()) {
    if (vc->schedule_write()) {
#ifdef CLUSTER_TOMCAT
      ink_assert(lock.m);
#endif
      vc->write_locked = lock.m;
      lock.m           = 0;
      lock.have_lock   = false;
      return 1;
    } else {
      if (!vc->write_bytes_in_transit) {
        close_ClusterVConnection(vc);
      }
      return 0;
    }
  }

  if (!s->enabled && !vc->was_remote_closed()) {
#ifdef CLUSTER_STATS
    _dw_not_enabled++;
#endif
    return 0;
  }

  if (vc->pending_remote_fill) {
    if (vc->was_remote_closed())
      close_ClusterVConnection(vc);

#ifdef CLUSTER_STATS
    _dw_wait_remote_fill++;
#endif
    return 0;
  }

  if (!lock.have_lock || !s->vio.mutex || !s->vio._cont) {
    if (!lock.have_lock && s->vio.mutex && s->vio._cont) {
      goto retry;
    } else {
// No active VIO
#ifdef CLUSTER_STATS
      _dw_no_active_vio++;
#endif
      return 0;
    }
  }
  //
  // If this connection has been closed remotely, send EOS
  //
  if (vc->was_remote_closed()) {
    if (!vc->write_bytes_in_transit && !vc->schedule_write()) {
      remote_close(vc, s);
    }
    return 0;
  }
  //
  // If not enabled or not WRITE
  //
  if (!s->enabled || s->vio.op != VIO::WRITE) {
    s->enabled = 0;
#ifdef CLUSTER_STATS
    _dw_not_enabled_or_no_write++;
#endif
    return 0;
  }
  //
  // If no room on the remote side or set_data() messages pending
  //
  int set_data_msgs_pending = vc->n_set_data_msgs;
  if (set_data_msgs_pending || (vc->remote_free <= (s->vio.ndone - vc->write_list_bytes))) {
    if (set_data_msgs_pending) {
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_VC_WRITE_STALL_STAT);

#ifdef CLUSTER_STATS
      _dw_set_data_pending++;
#endif

    } else {
#ifdef CLUSTER_STATS
      _dw_no_free_space++;
#endif
    }
    return 0;
  }
  //
  // Calculate amount writable
  //
  MIOBufferAccessor &buf = s->vio.buffer;

  int64_t towrite      = buf.reader()->read_avail();
  int64_t ntodo        = s->vio.ntodo();
  bool write_vc_signal = false;

  if (towrite > ntodo)
    towrite = ntodo;

  ink_assert(ntodo >= 0);
  if (ntodo <= 0) {
    cluster_signal_and_update(VC_EVENT_WRITE_COMPLETE, vc, s);
    return 0;
  }
  if (buf.writer()->write_avail() && towrite != ntodo) {
    write_vc_signal = true;
    if (cluster_signal_and_update(VC_EVENT_WRITE_READY, vc, s) == EVENT_DONE)
      return 0;
    ink_assert(s->vio.ntodo() >= 0);
    if (s->vio.ntodo() <= 0) {
      cluster_signal_and_update(VC_EVENT_WRITE_COMPLETE, vc, s);
      return 0;
    }
  }
  // Clone nbytes of vio.buffer.reader IOBufferBlock list allowing
  // write_list to contain no more than DEFAULT_MAX_BUFFER_SIZE bytes.

  Ptr<IOBufferBlock> b_list;
  IOBufferBlock *b_tail;
  int bytes_to_fill;
  int consume_bytes;

  bytes_to_fill = DEFAULT_MAX_BUFFER_SIZE - vc->write_list_bytes;

  if (towrite && bytes_to_fill) {
    consume_bytes = (towrite > bytes_to_fill) ? bytes_to_fill : towrite;
    b_list = clone_IOBufferBlockList(s->vio.buffer.reader()->block, s->vio.buffer.reader()->start_offset, consume_bytes, &b_tail);
    ink_assert(b_tail);

    // Append cloned IOBufferBlock list to VC write_list.

    if (vc->write_list_tail) {
      vc->write_list_tail->next = b_list;
    } else {
      vc->write_list = b_list;
    }
    vc->write_list_tail = b_tail;
    vc->write_list_bytes += consume_bytes;
    ink_assert(bytes_IOBufferBlockList(vc->write_list, 1) == vc->write_list_bytes);

    // We may defer the write, but tell the user we have consumed the data.

    (s->vio.buffer.reader())->consume(consume_bytes);
    s->vio.ndone += consume_bytes;
    if (s->vio.ntodo() <= 0) {
      cluster_signal_and_update_locked(VC_EVENT_WRITE_COMPLETE, vc, s);
    }
  }

  if (vc->schedule_write()) {
#ifdef CLUSTER_TOMCAT
    ink_assert(s->vio.mutex);
#endif
    vc->write_locked = lock.m;
    lock.m           = 0;
    lock.have_lock   = false;
    return 1;
  } else {
    if (!write_vc_signal && buf.writer()->write_avail() && towrite != ntodo)
      cluster_signal_and_update(VC_EVENT_WRITE_READY, vc, s);
    return 0;
  }
}

int
ClusterHandler::valid_for_freespace_write(ClusterVConnection *vc)
{
  //
  // Determine if freespace messages are allowed on this VC
  //
  ClusterVConnState *s = &vc->read;

  ink_assert(!on_stolen_thread);

  //
  // Attempt to get the lock, if we miss, push vc into the future
  //
  DestructorLock lock(thread);

retry:
  if ((lock.m = s->vio.mutex)) {
    lock.have_lock = MUTEX_TAKE_TRY_LOCK_FOR_SPIN(lock.m, thread, s->vio._cont, READ_LOCK_SPIN_COUNT);

    if (!lock.have_lock) {
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_READ_LOCKED_STAT);

#ifdef CLUSTER_STATS
      _fw_missed_lock++;
#endif
      return -1;
    }
  }
  if (vc->was_closed()) {
    if (!vc->write_bytes_in_transit && !vc->schedule_write()) {
      close_ClusterVConnection(vc);
    }
    return 0;
  }

  if (!s->enabled && !vc->was_remote_closed()) {
#ifdef CLUSTER_STATS
    _fw_not_enabled++;
#endif
    return 0;
  }

  if (vc->pending_remote_fill) {
    if (vc->was_remote_closed())
      close_ClusterVConnection(vc);

#ifdef CLUSTER_STATS
    _fw_wait_remote_fill++;
#endif
    return 0;
  }

  if (!lock.have_lock || !s->vio.mutex || !s->vio._cont) {
    if (!lock.have_lock && s->vio.mutex && s->vio._cont) {
      goto retry;
    } else {
// No active VIO
#ifdef CLUSTER_STATS
      _fw_no_active_vio++;
#endif
      return 0;
    }
  }
  //
  // If this connection has been closed remotely, send EOS
  //
  if (vc->was_remote_closed()) {
    if (vc->write_bytes_in_transit || vc->schedule_write()) {
      // Defer close until write data is pushed
      return 0;
    }
    remote_close(vc, s);
    return 0;
  }
  //
  // If not enabled or not WRITE
  //
  if (!s->enabled || s->vio.op != VIO::READ) {
#ifdef CLUSTER_STATS
    _fw_not_enabled_or_no_read++;
#endif
    return 0;
  }

  int64_t ntodo = s->vio.ntodo();
  ink_assert(ntodo >= 0);

  if (ntodo <= 0) {
    cluster_signal_and_update(VC_EVENT_READ_COMPLETE, vc, s);
    return 0;
  }

  int64_t bytes_to_move = vc->initial_data_bytes;
  if (vc->read_block && bytes_to_move) {
    // Push initial read data into VC

    if (ntodo >= bytes_to_move) {
      Debug("cluster_vc_xfer", "finish initial data push ch %d bytes %" PRId64, vc->channel, vc->read_block->read_avail());

      s->vio.buffer.writer()->append_block(vc->read_block->clone());
      vc->read_block = 0;

    } else {
      bytes_to_move = ntodo;

      Debug("cluster_vc_xfer", "initial data push ch %d bytes %" PRId64, vc->channel, bytes_to_move);

      // Clone a portion of the data

      IOBufferBlock *b, *btail;
      b = clone_IOBufferBlockList(vc->read_block, 0, bytes_to_move, &btail);
      s->vio.buffer.writer()->append_block(b);
      vc->read_block->consume(bytes_to_move);
    }
    s->vio.ndone += bytes_to_move;
    vc->initial_data_bytes -= bytes_to_move;

    if (s->vio.ntodo() <= 0) {
      s->enabled = 0;
      cluster_signal_and_update_locked(VC_EVENT_READ_COMPLETE, vc, s);
      return 0;

    } else {
      if (vc->have_all_data) {
        if (!vc->read_block) {
          s->enabled = 0;
          cluster_signal_and_update(VC_EVENT_EOS, vc, s);
          return 0;
        }
      }
      if (cluster_signal_and_update_locked(VC_EVENT_READ_READY, vc, s) == EVENT_DONE)
        return false;

      if (s->vio.ntodo() <= 0)
        s->enabled = 0;

      if (vc->initial_data_bytes)
        return 0;
    }
  }
  // At this point, all initial read data passed in the open_read reply
  // has been moved into the user VC.
  // Now allow send of freespace to receive additional data.

  int64_t nextfree = vc->read.vio.ndone;

  nextfree = (nextfree + DEFAULT_MAX_BUFFER_SIZE - 1) / DEFAULT_MAX_BUFFER_SIZE;
  nextfree *= DEFAULT_MAX_BUFFER_SIZE;

  if (nextfree >= (vc->last_local_free / 2)) {
    nextfree = vc->last_local_free + (8 * DEFAULT_MAX_BUFFER_SIZE);
  }

  if ((vc->last_local_free == 0) || (nextfree >= vc->last_local_free)) {
    Debug(CL_PROTO, "(%d) update freespace %" PRId64, vc->channel, nextfree);
    //
    // Have good VC candidate locked for freespace write
    //
    return nextfree;

  } else {
    // No free space update required
    return 0;
  }
}

void
ClusterHandler::vcs_push(ClusterVConnection *vc, int type)
{
  if (vc->type <= VC_CLUSTER)
    vc->type = type;

  while ((vc->type > VC_CLUSTER) && !vc->in_vcs && ink_atomic_cas(pvint32(&vc->in_vcs), 0, 1)) {
    if (vc->type == VC_CLUSTER_READ)
      ink_atomiclist_push(&vc->ch->read_vcs_ready, (void *)vc);
    else
      ink_atomiclist_push(&vc->ch->write_vcs_ready, (void *)vc);
    return;
  }
}

int
ClusterHandler::remote_close(ClusterVConnection *vc, ClusterVConnState *ns)
{
  if (ns->vio.op != VIO::NONE && !vc->closed) {
    ns->enabled = 0;
    if (vc->remote_closed > 0) {
      if (ns->vio.op == VIO::READ) {
        if (ns->vio.nbytes == ns->vio.ndone) {
          return cluster_signal_and_update(VC_EVENT_READ_COMPLETE, vc, ns);
        } else {
          return cluster_signal_and_update(VC_EVENT_EOS, vc, ns);
        }
      } else {
        return cluster_signal_and_update(VC_EVENT_EOS, vc, ns);
      }
    } else {
      return cluster_signal_error_and_update(vc, ns, vc->remote_lerrno);
    }
  }
  return EVENT_CONT;
}

void
ClusterHandler::steal_thread(EThread *t)
{
  //
  // Attempt to push the control message now instead of waiting
  // for the periodic event to process it.
  //
  if (t != thread &&      // different thread to steal
      write.to_do <= 0 && // currently not trying to send data
      // nothing big outstanding
      !write.msg.count) {
    mainClusterEvent(CLUSTER_EVENT_STEAL_THREAD, (Event *)t);
  }
}

void
ClusterHandler::free_locks(bool read_flag, int i)
{
  //
  // Free VC locks.  Handle partial acquires up to i
  //
  if (i == CLUSTER_FREE_ALL_LOCKS) {
    if (read_flag) {
      i = (read.msg.state >= 2 ? read.msg.count : 0);
    } else {
      i = write.msg.count;
    }
  }
  ClusterState &s = (read_flag ? read : write);
  for (int j = 0; j < i; j++) {
    if (s.msg.descriptor[j].type == CLUSTER_SEND_DATA && s.msg.descriptor[j].channel != CLUSTER_CONTROL_CHANNEL) {
      ClusterVConnection *vc = channels[s.msg.descriptor[j].channel];
      if (VALID_CHANNEL(vc)) {
        if (read_flag) {
          if ((ProxyMutex *)vc->read_locked) {
            MUTEX_UNTAKE_LOCK(vc->read.vio.mutex, thread);
            vc->read_locked = NULL;
          }
        } else {
          if ((ProxyMutex *)vc->write_locked) {
            MUTEX_UNTAKE_LOCK(vc->write_locked, thread);
            vc->write_locked = NULL;
          }
        }
      }
    } else if (!read_flag && s.msg.descriptor[j].type == CLUSTER_SEND_FREE &&
               s.msg.descriptor[j].channel != CLUSTER_CONTROL_CHANNEL) {
      ClusterVConnection *vc = channels[s.msg.descriptor[j].channel];
      if (VALID_CHANNEL(vc)) {
        if ((ProxyMutex *)vc->read_locked) {
          MUTEX_UNTAKE_LOCK(vc->read_locked, thread);
          vc->read_locked = NULL;
        }
      }
    }
  }
}

#ifdef CLUSTER_IMMEDIATE_NETIO
void
ClusterHandler::build_poll(bool next)
{
  Pollfd *pfd;
  if (next) {
    pfd     = thread->nextPollDescriptor->alloc();
    pfd->fd = net_vc->get_socket();
    ifd     = pfd - thread->nextPollDescriptor->pfd;
  } else {
    pfd     = thread->pollDescriptor->alloc();
    pfd->fd = net_vc->get_socket();
    ifd     = pfd - thread->pollDescriptor->pfd;
  }
  pfd->events = POLLHUP;
  if (next) {
    if (read.to_do)
      pfd->events |= POLLIN;
    if (write.to_do)
      pfd->events |= POLLOUT;
  } else {
    // we have to lie since we are in the same cycle
    pfd->events = POLLIN | POLLOUT;
    // reads/writes are non-blocking anyway
    pfd->revents = POLLIN | POLLOUT;
  }
}
#endif // CLUSTER_IMMEDIATE_NETIO

extern int CacheClusterMonitorEnabled;
extern int CacheClusterMonitorIntervalSecs;

//
// The main event for machine-machine link
//
int
ClusterHandler::mainClusterEvent(int event, Event *e)
{
  // Set global time
  current_time = Thread::get_hrtime();

  if (CacheClusterMonitorEnabled) {
    if ((current_time - last_trace_dump) > HRTIME_SECONDS(CacheClusterMonitorIntervalSecs)) {
      last_trace_dump = current_time;
      dump_internal_data();
    }
  }
//
// Note: The caller always acquires the ClusterHandler mutex prior
//       to the call.  This guarantees single threaded access in
//       mainClusterEvent()
//

/////////////////////////////////////////////////////////////////////////
// If cluster interconnect is overloaded, disable remote cluster ops.
/////////////////////////////////////////////////////////////////////////
#ifndef DEBUG
  if (clm && ClusterLoadMonitor::cf_monitor_enabled > 0) {
#else
  if (0) {
#endif
    bool last_state = disable_remote_cluster_ops;
    if (clm->is_cluster_overloaded()) {
      disable_remote_cluster_ops = true;
    } else {
      disable_remote_cluster_ops = false;
    }
    if (last_state != disable_remote_cluster_ops) {
      if (disable_remote_cluster_ops) {
        Note("Network congestion to [%u.%u.%u.%u] encountered, reverting to proxy only mode", DOT_SEPARATED(ip));
      } else {
        Note("Network congestion to [%u.%u.%u.%u] cleared, reverting to cache mode", DOT_SEPARATED(ip));
        last_cluster_op_enable = current_time;
      }
    }
  }

  on_stolen_thread = (event == CLUSTER_EVENT_STEAL_THREAD);
  bool io_callback = (event == EVENT_IMMEDIATE);

  if (on_stolen_thread) {
    thread = (EThread *)e;
  } else {
    if (io_callback) {
      thread = this_ethread();
    } else {
      thread = e->ethread;
    }
  }

  int io_activity = 1;
  bool only_write_control_msgs;
  int res;

  while (io_activity) {
    io_activity             = 0;
    only_write_control_msgs = 0;

    if (downing) {
      machine_down();
      break;
    }

    //////////////////////////
    // Read Processing
    //////////////////////////
    if (!on_stolen_thread) {
      if (delayed_reads.head) {
        CLUSTER_INCREMENT_DYN_STAT(CLUSTER_DELAYED_READS_STAT);
        finish_delayed_reads();
      }
      if ((res = process_read(current_time)) < 0) {
        break;
      }
      io_activity += res;

      if (delayed_reads.head) {
        CLUSTER_INCREMENT_DYN_STAT(CLUSTER_DELAYED_READS_STAT);
        finish_delayed_reads();
      }
    }
    /////////////////////////
    // Write Processing
    /////////////////////////
    if ((res = process_write(current_time, only_write_control_msgs)) < 0) {
      break;
    }
    io_activity += res;

    /////////////////////////////////////////
    // Process deferred open_local requests
    /////////////////////////////////////////
    if (!on_stolen_thread) {
      if (do_open_local_requests())
        thread->signal_hook(thread);
    }
  }

#ifdef CLUSTER_IMMEDIATE_NETIO
  if (!dead && ((event == EVENT_POLL) || (event == EVENT_INTERVAL))) {
    if (res >= 0) {
      build_poll(true);
    }
  }
#endif
  return EVENT_CONT;
}

int ClusterHandler::process_read(ink_hrtime /* now ATS_UNUSED */)
{
#ifdef CLUSTER_STATS
  _process_read_calls++;
#endif
  if (dead) {
    // Node is down
    return 0;
  }
  ///////////////////////////////
  // Cluster read state machine
  ///////////////////////////////

  for (;;) {
    switch (read.state) {
    ///////////////////////////////////////////////
    case ClusterState::READ_START:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_start++;
#endif
        read.msg.clear();
        read.start_time = Thread::get_hrtime();
        if (build_initial_vector(CLUSTER_READ)) {
          read.state = ClusterState::READ_HEADER;
        } else {
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_HEADER:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_header++;
#endif
        read.state = ClusterState::READ_AWAIT_HEADER;
        if (!read.doIO()) {
          // i/o not initiated, retry later
          read.state = ClusterState::READ_HEADER;
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_AWAIT_HEADER:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_await_header++;
#endif
        if (!read.io_complete) {
          return 0;
        } else {
          if (read.io_complete < 0) {
            // read error, declare node down
            machine_down();
            return -1;
          }
        }
        if (read.to_do) {
          if (read.bytes_xfered) {
            CLUSTER_INCREMENT_DYN_STAT(CLUSTER_PARTIAL_READS_STAT);
            read.state = ClusterState::READ_HEADER;
            break;
          } else {
            // Zero byte read
            read.state = ClusterState::READ_HEADER;
            return 0;
          }
        } else {
#ifdef MSG_TRACE
          fprintf(t_fd, "[R] seqno=%d count=%d control_bytes=%d count_check=%d dsum=%d csum=%d\n", read.sequence_number,
                  read.msg.hdr()->count, read.msg.hdr()->control_bytes, read.msg.hdr()->count_check,
                  read.msg.hdr()->descriptor_cksum, read.msg.hdr()->control_bytes_cksum);
          fflush(t_fd);
#endif
          CLUSTER_SUM_DYN_STAT(CLUSTER_READ_BYTES_STAT, read.did);
          if (needByteSwap) {
            read.msg.hdr()->SwapBytes();
          }
          read.msg.count               = read.msg.hdr()->count;
          read.msg.control_bytes       = read.msg.hdr()->control_bytes;
          read.msg.descriptor_cksum    = read.msg.hdr()->descriptor_cksum;
          read.msg.control_bytes_cksum = read.msg.hdr()->control_bytes_cksum;
          read.msg.unused              = read.msg.hdr()->unused;

          if (MAGIC_COUNT(read) != read.msg.hdr()->count_check) {
            ink_assert(!"Read bad ClusterMsgHeader data");
            Warning("Bad ClusterMsgHeader read on [%d.%d.%d.%d], restarting", DOT_SEPARATED(ip));
            Note("Cluster read from [%u.%u.%u.%u] failed, declaring down", DOT_SEPARATED(ip));
            machine_down();
            return -1;
          }

          if (read.msg.count || read.msg.control_bytes) {
            read.msg.state++;
            read.state = ClusterState::READ_SETUP_DESCRIPTOR;
          } else {
            read.state = ClusterState::READ_COMPLETE;
          }
          break;
        }
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_SETUP_DESCRIPTOR:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_setup_descriptor++;
#endif
        if (build_initial_vector(CLUSTER_READ)) {
          read.state = ClusterState::READ_DESCRIPTOR;
        } else {
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_DESCRIPTOR:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_descriptor++;
#endif
        read.state = ClusterState::READ_AWAIT_DESCRIPTOR;
        if (!read.doIO()) {
          // i/o not initiated, retry later
          read.state = ClusterState::READ_DESCRIPTOR;
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_AWAIT_DESCRIPTOR:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_await_descriptor++;
#endif
        if (!read.io_complete) {
          return 0;
        } else {
          if (read.io_complete < 0) {
            // read error, declare node down
            machine_down();
            return -1;
          }
        }
        if (read.to_do) {
          if (read.bytes_xfered) {
            CLUSTER_INCREMENT_DYN_STAT(CLUSTER_PARTIAL_READS_STAT);
            read.state = ClusterState::READ_DESCRIPTOR;
            break;
          } else {
            // Zero byte read
            read.state = ClusterState::READ_DESCRIPTOR;
            return 0;
          }
        } else {
#ifdef CLUSTER_MESSAGE_CKSUM
          ink_release_assert(read.msg.calc_descriptor_cksum() == read.msg.descriptor_cksum);
          ink_release_assert(read.msg.calc_control_bytes_cksum() == read.msg.control_bytes_cksum);
#endif
          CLUSTER_SUM_DYN_STAT(CLUSTER_READ_BYTES_STAT, read.did);
          if (needByteSwap) {
            // Descriptors need byte swap
            swap_descriptor_bytes();
          }
          if (read.msg.count == 0) {
            read.bytes_xfered = 0;
            read.state        = ClusterState::READ_COMPLETE;
          } else {
            read.msg.state++;
            read.state = ClusterState::READ_SETUP_DATA;
          }
          break;
        }
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_SETUP_DATA:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_setup_data++;
#endif
        if (build_initial_vector(CLUSTER_READ)) {
          free_locks(CLUSTER_READ);
          if (read.to_do) {
            read.state = ClusterState::READ_DATA;
          } else {
            // Descriptor contains no VC data
            read.state = ClusterState::READ_COMPLETE;
          }
        } else {
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_DATA:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_data++;
#endif
        ink_release_assert(read.to_do);
        read.state = ClusterState::READ_AWAIT_DATA;
        if (!read.doIO()) {
          // i/o not initiated, retry later
          read.state = ClusterState::READ_DATA;
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_AWAIT_DATA:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_await_data++;
#endif
        if (!read.io_complete) {
          return 0; // awaiting i/o complete
        } else {
          if (read.io_complete > 0) {
            read.state = ClusterState::READ_POST_COMPLETE;
          } else {
            // read error, declare node down
            machine_down();
            return -1;
          }
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_POST_COMPLETE:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_post_complete++;
#endif
        if (!get_read_locks()) {
          return 0;
        }
        if (read.to_do) {
          if (read.bytes_xfered) {
            update_channels_partial_read();
            free_locks(CLUSTER_READ);
            CLUSTER_SUM_DYN_STAT(CLUSTER_READ_BYTES_STAT, read.bytes_xfered);
            CLUSTER_INCREMENT_DYN_STAT(CLUSTER_PARTIAL_READS_STAT);
            read.state = ClusterState::READ_DATA;
            return 1;
          } else {
            // Zero byte read
            free_locks(CLUSTER_READ);
            read.state = ClusterState::READ_DATA;
            return 0;
          }
        } else {
          CLUSTER_SUM_DYN_STAT(CLUSTER_READ_BYTES_STAT, read.bytes_xfered);
          read.state = ClusterState::READ_COMPLETE;
          break;
        }
      }
    ///////////////////////////////////////////////
    case ClusterState::READ_COMPLETE:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_read_complete++;
#endif
        ink_hrtime rdmsg_end_time = Thread::get_hrtime();
        CLUSTER_SUM_DYN_STAT(CLUSTER_RDMSG_ASSEMBLE_TIME_STAT, rdmsg_end_time - read.start_time);
        read.start_time = HRTIME_MSECONDS(0);
        if (dump_msgs)
          dump_read_msg();
        read.sequence_number++;
        update_channels_read();
        free_locks(CLUSTER_READ);

        read.state = ClusterState::READ_START;
        break; // setup next read
      }
    //////////////////
    default:
      //////////////////
      {
        ink_release_assert(!"ClusterHandler::process_read invalid state");
      }

    } // end of switch
  }   // end of for
}

int
ClusterHandler::process_write(ink_hrtime now, bool only_write_control_msgs)
{
#ifdef CLUSTER_STATS
  _process_write_calls++;
#endif
  /////////////////////////////////
  // Cluster write state machine
  /////////////////////////////////
  for (;;) {
    switch (write.state) {
    ///////////////////////////////////////////////
    case ClusterState::WRITE_START:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_start++;
#endif
        write.msg.clear();
        write.last_time                  = Thread::get_hrtime();
        pw_write_descriptors_built       = -1;
        pw_freespace_descriptors_built   = -1;
        pw_controldata_descriptors_built = -1;
        pw_time_expired                  = 0;
        write.state                      = ClusterState::WRITE_SETUP;
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::WRITE_SETUP:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_setup++;
#endif
        if (!on_stolen_thread && !only_write_control_msgs) {
          /////////////////////////////////////////////////////////////
          // Build a complete write descriptor containing control,
          // data and freespace message data.
          /////////////////////////////////////////////////////////////

          // Control message descriptors
          if (pw_controldata_descriptors_built) {
            pw_controldata_descriptors_built = build_controlmsg_descriptors();
          }
          // Write data descriptors
          if (pw_write_descriptors_built) {
            pw_write_descriptors_built = build_write_descriptors();
          }
          // Free space descriptors
          if (pw_freespace_descriptors_built) {
            pw_freespace_descriptors_built = build_freespace_descriptors();
          }
          add_small_controlmsg_descriptors(); // always last
        } else {
          /////////////////////////////////////////////////////////////
          // Build a write descriptor only containing control data.
          /////////////////////////////////////////////////////////////
          pw_write_descriptors_built       = 0;
          pw_freespace_descriptors_built   = 0;
          pw_controldata_descriptors_built = build_controlmsg_descriptors();
          add_small_controlmsg_descriptors(); // always last
        }

        // If nothing to write, post write completion
        if (!pw_controldata_descriptors_built && !pw_write_descriptors_built && !pw_freespace_descriptors_built) {
          write.state = ClusterState::WRITE_COMPLETE;
          break;
        } else {
          started_on_stolen_thread = on_stolen_thread;
          control_message_write    = only_write_control_msgs;
        }

// Move required data into the message header
#ifdef CLUSTER_MESSAGE_CKSUM
        write.msg.descriptor_cksum        = write.msg.calc_descriptor_cksum();
        write.msg.hdr()->descriptor_cksum = write.msg.descriptor_cksum;

        write.msg.control_bytes_cksum        = write.msg.calc_control_bytes_cksum();
        write.msg.hdr()->control_bytes_cksum = write.msg.control_bytes_cksum;
        write.msg.unused                     = 0;
#endif
        write.msg.hdr()->count         = write.msg.count;
        write.msg.hdr()->control_bytes = write.msg.control_bytes;
        write.msg.hdr()->count_check   = MAGIC_COUNT(write);

        ink_release_assert(build_initial_vector(CLUSTER_WRITE));
        free_locks(CLUSTER_WRITE);
        write.state = ClusterState::WRITE_INITIATE;
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::WRITE_INITIATE:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_initiate++;
#endif
        write.state = ClusterState::WRITE_AWAIT_COMPLETION;
        if (!write.doIO()) {
          // i/o not initiated, retry later
          write.state = ClusterState::WRITE_INITIATE;
          return 0;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::WRITE_AWAIT_COMPLETION:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_await_completion++;
#endif
        if (!write.io_complete) {
          // Still waiting for write i/o completion
          return 0;
        } else {
          if (write.io_complete < 0) {
            // write error, declare node down
            machine_down();
            write.state = ClusterState::WRITE_INITIATE;
            break;
          }
          if (write.to_do) {
            if (write.bytes_xfered) {
              CLUSTER_INCREMENT_DYN_STAT(CLUSTER_PARTIAL_WRITES_STAT);
              write.state = ClusterState::WRITE_INITIATE;
              break;
            } else {
              // Zero byte write
              write.state = ClusterState::WRITE_INITIATE;
              return 0;
            }
          }
          CLUSTER_SUM_DYN_STAT(CLUSTER_WRITE_BYTES_STAT, write.bytes_xfered);
          write.sequence_number++;
          write.state = ClusterState::WRITE_POST_COMPLETE;
        }
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::WRITE_POST_COMPLETE:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_post_complete++;
#endif
        if (!get_write_locks()) {
          CLUSTER_INCREMENT_DYN_STAT(CLUSTER_WRITE_LOCK_MISSES_STAT);
          return 0;
        }
        //
        // Move the channels into their new buckets based on how much
        // was written
        //
        update_channels_written();
        free_locks(CLUSTER_WRITE);
        write.state = ClusterState::WRITE_COMPLETE;
        break;
      }
    ///////////////////////////////////////////////
    case ClusterState::WRITE_COMPLETE:
      ///////////////////////////////////////////////
      {
#ifdef CLUSTER_STATS
        _n_write_complete++;
#endif
        write.state        = ClusterState::WRITE_START;
        ink_hrtime curtime = Thread::get_hrtime();

        if (!on_stolen_thread) {
          //
          // Complete all work in the current bucket before moving to next
          //
          pw_time_expired = (curtime - now) > CLUSTER_MAX_RUN_TIME;

          if (!control_message_write && !pw_write_descriptors_built && !pw_freespace_descriptors_built &&
              !pw_controldata_descriptors_built) {
            // skip to the next bucket
            cur_vcs = (cur_vcs + 1) % CLUSTER_BUCKETS;
          }
        } else {
          //
          // Place an upper bound on thread stealing
          //
          pw_time_expired = (curtime - now) > CLUSTER_MAX_THREAD_STEAL_TIME;
          if (pw_time_expired) {
            CLUSTER_INCREMENT_DYN_STAT(CLUSTER_THREAD_STEAL_EXPIRES_STAT);
          }
        }
        //
        // periodic activities
        //
        if (!on_stolen_thread && !cur_vcs && !dead) {
          //
          // check if this machine is supposed to be in the cluster
          //
          MachineList *mc = the_cluster_machines_config();
          if (mc && !mc->find(ip, port)) {
            Note("Cluster [%u.%u.%u.%u:%d] not in config, declaring down", DOT_SEPARATED(ip), port);
            machine_down();
          }
        }
        if (pw_time_expired) {
          return -1; // thread run time expired
        } else {
          if (pw_write_descriptors_built || pw_freespace_descriptors_built || pw_controldata_descriptors_built) {
            break; // start another write
          } else {
            return 0; // no more data to write
          }
        }
      }
    //////////////////
    default:
      //////////////////
      {
        ink_release_assert(!"ClusterHandler::process_write invalid state");
      }

    } // End of switch
  }   // End of for
}

int
ClusterHandler::do_open_local_requests()
{
  //
  // open_local requests which are unable to obtain the ClusterHandler
  // mutex are deferred and placed onto external_incoming_open_local queue.
  // It is here where we process the open_local requests within the
  // ET_CLUSTER thread.
  //
  int pending_request = 0;
  ClusterVConnection *cvc;
  ClusterVConnection *cvc_ext;
  ClusterVConnection *cvc_ext_next;
  EThread *tt = this_ethread();
  Queue<ClusterVConnection> local_incoming_open_local;

  //
  // Atomically dequeue all requests from the external queue and
  // move them to the local working queue while maintaining insertion order.
  //
  while (true) {
    cvc_ext = (ClusterVConnection *)ink_atomiclist_popall(&external_incoming_open_local);
    if (cvc_ext == 0)
      break;

    while (cvc_ext) {
      cvc_ext_next       = (ClusterVConnection *)cvc_ext->link.next;
      cvc_ext->link.next = NULL;
      local_incoming_open_local.push(cvc_ext);
      cvc_ext = cvc_ext_next;
    }

    // Process deferred open_local requests.

    while ((cvc = local_incoming_open_local.pop())) {
      MUTEX_TRY_LOCK(lock, cvc->action_.mutex, tt);
      if (lock.is_locked()) {
        if (cvc->start(tt) < 0) {
          cvc->token.clear();
          if (cvc->action_.continuation) {
            cvc->action_.continuation->handleEvent(CLUSTER_EVENT_OPEN_FAILED, 0);
            clusterVCAllocator.free(cvc);
          }
        }
        MUTEX_RELEASE(lock);

      } else {
        // unable to get mutex, insert request back onto global queue.
        Debug(CL_TRACE, "do_open_local_requests() unable to acquire mutex (cvc=%p)", cvc);
        pending_request = 1;
        ink_atomiclist_push(&external_incoming_open_local, (void *)cvc);
      }
    }
  }
  return pending_request;
}

// End of ClusterHandler.cc
