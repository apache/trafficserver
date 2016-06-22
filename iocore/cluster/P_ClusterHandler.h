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

  ClusterHandler.h
****************************************************************************/

#ifndef _P_ClusterHandler_h
#define _P_ClusterHandler_h

class ClusterLoadMonitor;

struct ClusterCalloutContinuation;
typedef int (ClusterCalloutContinuation::*ClstCoutContHandler)(int, void *);

struct ClusterCalloutContinuation : public Continuation {
  struct ClusterHandler *_ch;

  int CalloutHandler(int event, Event *e);
  ClusterCalloutContinuation(struct ClusterHandler *ch);
  ~ClusterCalloutContinuation();
};

struct ClusterControl : public Continuation {
  int len; // TODO: Should this be 64-bit ?
  char size_index;
  int64_t *real_data;
  char *data;
  void (*free_proc)(void *);
  void *free_proc_arg;
  Ptr<IOBufferBlock> iob_block;

  IOBufferBlock *
  get_block()
  {
    return iob_block;
  }
  bool
  fast_data()
  {
    return (len <= MAX_FAST_CONTROL_MESSAGE);
  }
  bool
  valid_alloc_data()
  {
    return iob_block && real_data && data;
  }

  enum {
    // DATA_HDR = size_index (1 byte) + magicno (1 byte) + sizeof(this)

    DATA_HDR = (sizeof(int64_t) * 2) // must be multiple of sizeof(int64_t)
  };

  ClusterControl();
  void real_alloc_data(int, bool);
  void free_data();
  virtual void freeall() = 0;
};

struct OutgoingControl : public ClusterControl {
  ClusterHandler *ch;
  ink_hrtime submit_time;

  static OutgoingControl *alloc();

  OutgoingControl();
  void
  alloc_data(bool align_int32_on_non_int64_boundary = true)
  {
    real_alloc_data(1, align_int32_on_non_int64_boundary); /* read access */
  }

  void
  set_data(char *adata, int alen)
  {
    data          = adata;
    len           = alen;
    free_proc     = 0;
    free_proc_arg = 0;
    real_data     = 0;

    // Create IOBufferBlock wrapper around passed data.

    iob_block = new_IOBufferBlock();
    iob_block->set_internal(adata, alen, BUFFER_SIZE_FOR_XMALLOC(alen));
    iob_block->_buf_end = iob_block->end();
  }

  void
  set_data(IOBufferBlock *buf, void (*free_data_proc)(void *), void *free_data_arg)
  {
    data          = buf->data->data();
    len           = bytes_IOBufferBlockList(buf, 1); // read avail bytes
    free_proc     = free_data_proc;
    free_proc_arg = free_data_arg;
    real_data     = 0;
    iob_block     = buf;
  }
  int startEvent(int event, Event *e);
  virtual void freeall();
};

//
// incoming control messsage are received by this machine
//
struct IncomingControl : public ClusterControl {
  ink_hrtime recognized_time;

  static IncomingControl *alloc();

  IncomingControl();
  void
  alloc_data(bool align_int32_on_non_int64_boundary = true)
  {
    real_alloc_data(0, align_int32_on_non_int64_boundary); /* write access */
  }
  virtual void freeall();
};

//
// Interface structure for internal_invoke_remote()
//
struct invoke_remote_data_args {
  int32_t magicno;
  OutgoingControl *msg_oc;
  OutgoingControl *data_oc;
  int dest_channel;
  ClusterVCToken token;

  enum {
    MagicNo = 0x04141998,
  };
  invoke_remote_data_args() : magicno(MagicNo), msg_oc(NULL), data_oc(NULL), dest_channel(0) {}
};

//
// Descriptor of a chunk of a message (see Memo.ClusterIODesign)
// This has been tested for aligment on the Sparc using TestDescriptor.cc
//

// type
#define CLUSTER_SEND_FREE 0
#define CLUSTER_SEND_DATA 1
#define CLUSTER_SEQUENCE_NUMBER(_x) (((unsigned int)_x) & 0xFFFF)

struct Descriptor { // Note: Over the Wire structure
  uint32_t type : 1;
  uint32_t channel : 15;
  uint16_t sequence_number; // lower 16 bits of the ClusterVCToken.seq
  uint32_t length;

  inline void
  SwapBytes()
  {
    ats_swap16((uint16_t *)this); // Hack
    ats_swap16((uint16_t *)&sequence_number);
    ats_swap32((uint32_t *)&length);
  }
};

struct ClusterMsgHeader { // Note: Over the Wire structure
  uint16_t count;
  uint16_t descriptor_cksum;
  uint16_t control_bytes_cksum;
  uint16_t unused;
  uint32_t control_bytes;
  uint32_t count_check;

  void
  clear()
  {
    count               = 0;
    descriptor_cksum    = 0;
    control_bytes_cksum = 0;
    unused              = 0;
    control_bytes       = 0;
    count_check         = 0;
  }
  ClusterMsgHeader() : count(0), descriptor_cksum(0), control_bytes_cksum(0), unused(0), control_bytes(0), count_check(0) {}
  inline void
  SwapBytes()
  {
    ats_swap16((uint16_t *)&count);
    ats_swap16((uint16_t *)&descriptor_cksum);
    ats_swap16((uint16_t *)&control_bytes_cksum);
    ats_swap16((uint16_t *)&unused);
    ats_swap32((uint32_t *)&control_bytes);
    ats_swap32((uint32_t *)&count_check);
  }
};

struct ClusterMsg {
  Descriptor *descriptor;
  Ptr<IOBufferBlock> iob_descriptor_block;
  int count;
  int control_bytes;
  int descriptor_cksum;
  int control_bytes_cksum;
  int unused;
  int state; // Only used by read to denote
             //   read phase (count, descriptor, data)
  Queue<OutgoingControl> outgoing_control;
  Queue<OutgoingControl> outgoing_small_control;
  Queue<OutgoingControl> outgoing_callout; // compound msg callbacks

  // read processing usage.
  int control_data_offset;
  int did_small_control_set_data;
  int did_large_control_set_data;
  int did_small_control_msgs;
  int did_large_control_msgs;
  int did_freespace_msgs;

  ClusterMsgHeader *
  hdr()
  {
    return (ClusterMsgHeader *)(((char *)descriptor) - sizeof(ClusterMsgHeader));
  }

  IOBufferBlock *
  get_block()
  {
    return iob_descriptor_block;
  }

  IOBufferBlock *
  get_block_header()
  {
    int start_offset;

    start_offset = (char *)hdr() - iob_descriptor_block->buf();
    iob_descriptor_block->reset();
    iob_descriptor_block->next = 0;
    iob_descriptor_block->fill(start_offset);
    iob_descriptor_block->consume(start_offset);
    return iob_descriptor_block;
  }

  IOBufferBlock *
  get_block_descriptor()
  {
    int start_offset;

    start_offset = ((char *)hdr() + sizeof(ClusterMsgHeader)) - iob_descriptor_block->buf();
    iob_descriptor_block->reset();
    iob_descriptor_block->next = 0;
    iob_descriptor_block->fill(start_offset);
    iob_descriptor_block->consume(start_offset);
    return iob_descriptor_block;
  }

  void
  clear()
  {
    hdr()->clear();
    count               = 0;
    control_bytes       = 0;
    descriptor_cksum    = 0;
    control_bytes_cksum = 0;
    unused              = 0;
    state               = 0;
    outgoing_control.clear();
    outgoing_small_control.clear();
    control_data_offset        = 0;
    did_small_control_set_data = 0;
    did_large_control_set_data = 0;
    did_small_control_msgs     = 0;
    did_large_control_msgs     = 0;
    did_freespace_msgs         = 0;
  }
  uint16_t
  calc_control_bytes_cksum()
  {
    uint16_t cksum = 0;
    char *p        = (char *)&descriptor[count];
    char *endp     = p + control_bytes;
    while (p < endp) {
      cksum += *p;
      ++p;
    }
    return cksum;
  }
  uint16_t
  calc_descriptor_cksum()
  {
    uint16_t cksum = 0;
    char *p        = (char *)&descriptor[0];
    char *endp     = (char *)&descriptor[count];
    while (p < endp) {
      cksum += *p;
      ++p;
    }
    return cksum;
  }
  ClusterMsg()
    : descriptor(NULL),
      iob_descriptor_block(NULL),
      count(0),
      control_bytes(0),
      descriptor_cksum(0),
      control_bytes_cksum(0),
      unused(0),
      state(0),
      control_data_offset(0),
      did_small_control_set_data(0),
      did_large_control_set_data(0),
      did_small_control_msgs(0),
      did_large_control_msgs(0),
      did_freespace_msgs(0)
  {
  }
};

//
// State for a particular (read/write) direction of a cluster link
//
struct ClusterHandler;
struct ClusterState : public Continuation {
  ClusterHandler *ch;
  bool read_channel;
  bool do_iodone_event; // schedule_imm() on i/o complete
  int n_descriptors;
  ClusterMsg msg;
  unsigned int sequence_number;
  int to_do;             // # of bytes to transact
  int did;               // # of bytes transacted
  int n_iov;             // defined iov(s) in this operation
  int io_complete;       // current i/o complete
  int io_complete_event; // current i/o complete event
  VIO *v;                // VIO associated with current op
  int bytes_xfered;      // bytes xfered at last callback
  int last_ndone;        // last do_io ndone
  int total_bytes_xfered;
  IOVec *iov; // io vector for readv, writev
  Ptr<IOBufferData> iob_iov;

  // Write byte bank structures
  char *byte_bank;    // bytes buffered for transit
  int n_byte_bank;    // number of bytes buffered for transit
  int byte_bank_size; // allocated size of byte bank

  int missed;
  bool missed_msg;
  ink_hrtime last_time;
  ink_hrtime start_time;

  Ptr<IOBufferBlock> block[MAX_TCOUNT];
  class MIOBuffer *mbuf;
  int state; // See enum defs below

  enum {
    READ_START = 1,
    READ_HEADER,
    READ_AWAIT_HEADER,
    READ_SETUP_DESCRIPTOR,
    READ_DESCRIPTOR,
    READ_AWAIT_DESCRIPTOR,
    READ_SETUP_DATA,
    READ_DATA,
    READ_AWAIT_DATA,
    READ_POST_COMPLETE,
    READ_COMPLETE
  } read_state_t;

  enum {
    WRITE_START = 1,
    WRITE_SETUP,
    WRITE_INITIATE,
    WRITE_AWAIT_COMPLETION,
    WRITE_POST_COMPLETE,
    WRITE_COMPLETE,
  } write_state_t;

  ClusterState(ClusterHandler *, bool);
  ~ClusterState();
  IOBufferData *get_data();
  void build_do_io_vector();
  int doIO();
  int doIO_read_event(int, void *);
  int doIO_write_event(int, void *);
  void IOComplete();
};

//
// ClusterHandlerBase superclass for processors with
// bi-directional VConnections.
//
struct ClusterHandlerBase : public Continuation {
  //
  // Private
  //
  Queue<ClusterVConnectionBase, ClusterVConnection::Link_read_link> *read_vcs;
  Queue<ClusterVConnectionBase, ClusterVConnection::Link_write_link> *write_vcs;
  int cur_vcs;
  int min_priority;
  Event *trigger_event;

  ClusterHandlerBase() : Continuation(NULL), read_vcs(NULL), write_vcs(NULL), cur_vcs(0), min_priority(1) {}
};

struct ClusterHandler : public ClusterHandlerBase {
#ifdef MSG_TRACE
  FILE *t_fd;
#endif
  NetVConnection *net_vc;
  EThread *thread;
  unsigned int ip;
  int port;
  char *hostname;
  ClusterMachine *machine;
  int ifd;
  int id;
  bool dead;
  bool downing;

  int32_t active; // handler currently running
  bool on_stolen_thread;

  struct ChannelData {
    int channel_number;
    LINK(ChannelData, link);
  };

  int n_channels;
  ClusterVConnection **channels;
  struct ChannelData **channel_data;
  Queue<ChannelData> free_local_channels;

  bool connector;
  int cluster_connect_state; // see clcon_state_t enum
  ClusterHelloMessage clusteringVersion;
  ClusterHelloMessage nodeClusteringVersion;
  bool needByteSwap;
  int configLookupFails;

#define CONFIG_LOOKUP_RETRIES 10

  enum {
    CLCON_INITIAL = 1,
    CLCON_SEND_MSG,
    CLCON_SEND_MSG_COMPLETE,
    CLCON_READ_MSG,
    CLCON_READ_MSG_COMPLETE,
    CLCON_VALIDATE_MSG,
    CLCON_CONN_BIND_CLEAR,
    CLCON_CONN_BIND,
    CLCON_CONN_BIND_OK,
    CLCON_ABORT_CONNECT,
    CLCON_DELETE_CONNECT
  } clcon_state_t;

  InkAtomicList outgoing_control_al[CLUSTER_CMSG_QUEUES];
  InkAtomicList external_incoming_control;
  InkAtomicList external_incoming_open_local;
  ClusterCalloutContinuation *callout_cont[MAX_COMPLETION_CALLBACK_EVENTS];
  Event *callout_events[MAX_COMPLETION_CALLBACK_EVENTS];
  Event *cluster_periodic_event;
  Queue<OutgoingControl> outgoing_control[CLUSTER_CMSG_QUEUES];
  Queue<IncomingControl> incoming_control;
  InkAtomicList read_vcs_ready;
  InkAtomicList write_vcs_ready;
  ClusterState read;
  ClusterState write;

  ink_hrtime current_time;
  ink_hrtime last;
  ink_hrtime last_report;
  int n_since_last_report;
  ink_hrtime last_cluster_op_enable;
  ink_hrtime last_trace_dump;

  DLL<ClusterVConnectionBase> delayed_reads;
  ClusterLoadMonitor *clm;
  bool disable_remote_cluster_ops;

  // process_write() state data
  int pw_write_descriptors_built;
  int pw_freespace_descriptors_built;
  int pw_controldata_descriptors_built;
  int pw_time_expired;
  bool started_on_stolen_thread;
  bool control_message_write;

#ifdef CLUSTER_STATS
  Ptr<IOBufferBlock> message_blk;

  int64_t _vc_writes;
  int64_t _vc_write_bytes;
  int64_t _control_write_bytes;
  int _dw_missed_lock;
  int _dw_not_enabled;
  int _dw_wait_remote_fill;
  int _dw_no_active_vio;
  int _dw_not_enabled_or_no_write;
  int _dw_set_data_pending;
  int _dw_no_free_space;
  int _fw_missed_lock;
  int _fw_not_enabled;
  int _fw_wait_remote_fill;
  int _fw_no_active_vio;
  int _fw_not_enabled_or_no_read;
  int _process_read_calls;
  int _n_read_start;
  int _n_read_header;
  int _n_read_await_header;
  int _n_read_setup_descriptor;
  int _n_read_descriptor;
  int _n_read_await_descriptor;
  int _n_read_setup_data;
  int _n_read_data;
  int _n_read_await_data;
  int _n_read_post_complete;
  int _n_read_complete;
  int _process_write_calls;
  int _n_write_start;
  int _n_write_setup;
  int _n_write_initiate;
  int _n_write_await_completion;
  int _n_write_post_complete;
  int _n_write_complete;

  void
  clear_cluster_stats()
  {
    _vc_writes                  = 0;
    _vc_write_bytes             = 0;
    _control_write_bytes        = 0;
    _dw_missed_lock             = 0;
    _dw_not_enabled             = 0;
    _dw_wait_remote_fill        = 0;
    _dw_no_active_vio           = 0;
    _dw_not_enabled_or_no_write = 0;
    _dw_set_data_pending        = 0;
    _dw_no_free_space           = 0;
    _fw_missed_lock             = 0;
    _fw_not_enabled             = 0;
    _fw_wait_remote_fill        = 0;
    _fw_no_active_vio           = 0;
    _fw_not_enabled_or_no_read  = 0;
    _process_read_calls         = 0;
    _n_read_start               = 0;
    _n_read_header              = 0;
    _n_read_await_header        = 0;
    _n_read_setup_descriptor    = 0;
    _n_read_descriptor          = 0;
    _n_read_await_descriptor    = 0;
    _n_read_setup_data          = 0;
    _n_read_data                = 0;
    _n_read_await_data          = 0;
    _n_read_post_complete       = 0;
    _n_read_complete            = 0;
    _process_write_calls        = 0;
    _n_write_start              = 0;
    _n_write_setup              = 0;
    _n_write_initiate           = 0;
    _n_write_await_completion   = 0;
    _n_write_post_complete      = 0;
    _n_write_complete           = 0;
  }
#endif // CLUSTER_STATS

  ClusterHandler();
  ~ClusterHandler();
  bool check_channel(int c);
  int alloc_channel(ClusterVConnection *vc, int requested_channel = 0);
  void free_channel(ClusterVConnection *vc);
  //
  //  local_channel()
  //  - Initiator node-node TCP socket  &&  Odd channel  => Local Channel
  //  - !Initiator node-node TCP socket &&  Even channel => Local Channel
  inline bool
  local_channel(int i)
  {
    return !connector == !(i & 1);
  }

  void close_ClusterVConnection(ClusterVConnection *);
  int cluster_signal_and_update(int event, ClusterVConnection *vc, ClusterVConnState *s);
  int cluster_signal_and_update_locked(int event, ClusterVConnection *vc, ClusterVConnState *s);
  int cluster_signal_error_and_update(ClusterVConnection *vc, ClusterVConnState *s, int lerrno);
  void close_free_lock(ClusterVConnection *, ClusterVConnState *);

#define CLUSTER_READ true
#define CLUSTER_WRITE false

  bool build_data_vector(char *, int, bool);
  bool build_initial_vector(bool);

  void add_to_byte_bank(ClusterVConnection *);
  void update_channels_read();
  int process_incoming_callouts(ProxyMutex *);
  void update_channels_partial_read();
  void process_set_data_msgs();
  void process_small_control_msgs();
  void process_large_control_msgs();
  void process_freespace_msgs();
  bool complete_channel_read(int, ClusterVConnection *vc);
  void finish_delayed_reads();
  // returns: false if the channel was closed

  void update_channels_written();

  int build_write_descriptors();
  int build_freespace_descriptors();
  int build_controlmsg_descriptors();
  int add_small_controlmsg_descriptors();
  int valid_for_data_write(ClusterVConnection *vc);
  int valid_for_freespace_write(ClusterVConnection *vc);

  int machine_down();
  int remote_close(ClusterVConnection *vc, ClusterVConnState *ns);
  void steal_thread(EThread *t);

#define CLUSTER_FREE_ALL_LOCKS -1
  void free_locks(bool read_flag, int i = CLUSTER_FREE_ALL_LOCKS);
  bool get_read_locks();
  bool get_write_locks();
  int zombify(Event *e = NULL); // optional event to use

  int connectClusterEvent(int event, Event *e);
  int startClusterEvent(int event, Event *e);
  int mainClusterEvent(int event, Event *e);
  int beginClusterEvent(int event, Event *e);
  int zombieClusterEvent(int event, Event *e);
  int protoZombieEvent(int event, Event *e);

  void vcs_push(ClusterVConnection *vc, int type);
  bool vc_ok_read(ClusterVConnection *);
  bool vc_ok_write(ClusterVConnection *);
  int do_open_local_requests();
  void swap_descriptor_bytes();
  int process_read(ink_hrtime);
  int process_write(ink_hrtime, bool);

  void dump_write_msg(int);
  void dump_read_msg();
  int compute_active_channels();
  void dump_internal_data();

#ifdef CLUSTER_IMMEDIATE_NETIO
  void build_poll(bool);
#endif
};

// Valid (ClusterVConnection *) in ClusterHandler.channels[]
#define VALID_CHANNEL(vc) (vc && !(((uintptr_t)vc) & 1))

// outgoing control continuations
extern ClassAllocator<OutgoingControl> outControlAllocator;

// incoming control descriptors
extern ClassAllocator<IncomingControl> inControlAllocator;

#endif /* _ClusterHandler_h */
