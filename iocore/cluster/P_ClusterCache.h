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

  Cluster.h


****************************************************************************/

#ifndef _P_Cluster_Cache_h
#define _P_Cluster_Cache_h

//*****************************************************************************
// Initially derived from Cluster.h "1.77.2.11 1999/01/21 03:24:10"
//*****************************************************************************

/****************************************************************************/
// #define LOCAL_CLUSTER_TEST_MODE 1
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//  Set the above #define to enable local clustering.  "Local clustering"
//  is a test only mode where all cluster nodes reside on the same host.
//
//  Configuration notes:
//   - For "cluster.config" entries, always use "127.0.0.1" as the IP
//     address and select a host unique cluster port.
//
//  Restrictions:
//   1) Does not work with the manager.  You must only run with the server
//      and hand configure "cluster.config".
//   2) Currently, this has only been tested in a two node configuration.
//
/****************************************************************************/

#include "P_ClusterMachine.h"

//
// Cluster Processor
//
// - monitors the status of the cluster
// - provides communication between machines in the cluster
// - provides callbacks to other processors when the cluster configuration
//   changes
//
#define CLUSTER_MAJOR_VERSION 3
#define CLUSTER_MINOR_VERSION 2

// Lowest supported major/minor cluster version
#define MIN_CLUSTER_MAJOR_VERSION CLUSTER_MAJOR_VERSION
#define MIN_CLUSTER_MINOR_VERSION CLUSTER_MINOR_VERSION

#define DEFAULT_CLUSTER_PORT_NUMBER 0
#define DEFAULT_NUMBER_OF_CLUSTER_THREADS 1
#define DEFAULT_CLUSTER_HOST ""

#define MAX_CLUSTER_SEND_LENGTH INT_MAX

#define CLUSTER_MAX_MACHINES 256
// less than 1% disparity at 255 machines, 32707 is prime less than 2^15
#define CLUSTER_HASH_TABLE_SIZE 32707

// after timeout the configuration is "dead"
#define CLUSTER_CONFIGURATION_TIMEOUT HRTIME_DAY
// after zombie the configuration is deleted
#define CLUSTER_CONFIGURATION_ZOMBIE (HRTIME_DAY * 2)

// the number of configurations into the past we probe for data
// one allows a new machine to come into or fall out of the
// cluster without loss of data.  If the data is redistributed within
// one day, no data will be lost.
#define CONFIGURATION_HISTORY_PROBE_DEPTH 1

// move these to a central event definition file (Event.h)
#define CLUSTER_EVENT_CHANGE (CLUSTER_EVENT_EVENTS_START)
#define CLUSTER_EVENT_CONFIGURATION (CLUSTER_EVENT_EVENTS_START + 1)
#define CLUSTER_EVENT_OPEN (CLUSTER_EVENT_EVENTS_START + 2)
#define CLUSTER_EVENT_OPEN_EXISTS (CLUSTER_EVENT_EVENTS_START + 3)
#define CLUSTER_EVENT_OPEN_FAILED (CLUSTER_EVENT_EVENTS_START + 4)

// internal event code
#define CLUSTER_EVENT_STEAL_THREAD (CLUSTER_EVENT_EVENTS_START + 50)

//////////////////////////////////////////////////////////////
// Miscellaneous byte swap routines
//////////////////////////////////////////////////////////////
inline void
ats_swap16(uint16_t *d)
{
  unsigned char *p = (unsigned char *)d;
  *d               = ((p[1] << 8) | p[0]);
}

inline uint16_t
ats_swap16(uint16_t d)
{
  ats_swap16(&d);
  return d;
}

inline void
ats_swap32(uint32_t *d)
{
  unsigned char *p = (unsigned char *)d;
  *d               = ((p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]);
}

inline uint32_t
ats_swap32(uint32_t d)
{
  ats_swap32(&d);
  return d;
}

inline void
ats_swap64(uint64_t *d)
{
  unsigned char *p = (unsigned char *)d;
  *d = (((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) | ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) | ((uint64_t)p[3] << 24) |
        ((uint64_t)p[2] << 16) | ((uint64_t)p[1] << 8) | (uint64_t)p[0]);
}

inline uint64_t
ats_swap64(uint64_t d)
{
  ats_swap64(&d);
  return d;
}

//////////////////////////////////////////////////////////////

struct ClusterConfiguration {
  int n_machines;
  ClusterMachine *machines[CLUSTER_MAX_MACHINES];

  ClusterMachine *
  machine_hash(unsigned int hash_value)
  {
    return machines[hash_table[hash_value % CLUSTER_HASH_TABLE_SIZE]];
  }

  ClusterMachine *
  find(unsigned int ip, int port = 0)
  {
    for (int i = 0; i < n_machines; i++)
      if (ip == machines[i]->ip && (!port || !machines[i]->cluster_port || machines[i]->cluster_port == port))
        return machines[i];
    return NULL;
  }

  //
  // Private
  //
  ClusterConfiguration();
  unsigned char hash_table[CLUSTER_HASH_TABLE_SIZE];
  ink_hrtime changed;
  SLINK(ClusterConfiguration, link);
};

inline bool
machine_in_vector(ClusterMachine *m, ClusterMachine **mm, int len)
{
  for (int i = 0; i < len; i++)
    if (m == mm[i])
      return true;
  return false;
}

//
// Returns either a machine or NULL.
// Finds a machine starting at probe_depth going up to
//    CONFIGURATION_HISTORY_PROBE_DEPTH
// which is up, not the current machine and has not yet been probed.
// Updates: probe_depth and past_probes.
//
inkcoreapi ClusterMachine *cluster_machine_at_depth(unsigned int hash, int *probe_depth = NULL,
                                                    ClusterMachine **past_probes = NULL);

//
// Cluster
//   A cluster of machines which act as a single cache.
//
struct Cluster {
  //
  // Public Interface
  //

  //
  // Cluster Hash Function
  //

  // Takes a hash value to a machine.  The hash function has the following
  // properties:
  //   1 - it divides input domain into the output range evenly (within 1%)
  //   2 - it tends to produce the same Machine for the same hash_value's
  //       for different configurations
  //   3 - it produces the hash same function for a given configuration of
  //       machines independent of the order they were added or removed
  //       from the cluster.  (it is a pure function of the configuration)
  //   Thread-safe
  //
  ClusterMachine *
  machine_hash(unsigned int hash_value)
  {
    return current_configuration()->machine_hash(hash_value);
  }

  //
  // Cluster Configuration
  //

  // Register callback for a cluster configuration change.
  // calls cont->handleEvent(EVENT_CLUSTER_CHANGE);
  //   Thread-safe
  //
  void cluster_change_callback(Continuation *cont);

  // Return the current configuration
  //   Thread-safe
  //
  ClusterConfiguration *
  current_configuration()
  {
    return configurations.head;
  }

  // Return the previous configuration.
  // Use from within the cluster_change_callback.
  //   Thread-safe
  //
  ClusterConfiguration *
  previous_configuration()
  {
    return configurations.head->link.next;
  }

  //
  // Private
  //
  // The configurations are updated only in the thread which is
  // accepting cluster connections.
  //
  SLL<ClusterConfiguration> configurations;

  Cluster();
};

//
// ClusterVCToken
//   An token passed between nodes to represent a virtualized connection.
//   (see ClusterProcessor::alloc_remote() and attach_remote() below)
//
struct ClusterVCToken {
  //
  // Marshal this data to send the token across the cluster
  //
  uint32_t ip_created;
  uint32_t ch_id;
  uint32_t sequence_number;

  bool
  is_clear()
  {
    return !ip_created;
  }
  void
  clear()
  {
    ip_created      = 0;
    sequence_number = 0;
  }

  ClusterVCToken(unsigned int aip = 0, unsigned int id = 0, unsigned int aseq = 0)
    : ip_created(aip), ch_id(id), sequence_number(aseq)
  {
  }
  //
  // Private
  //
  void alloc();

  inline void
  SwapBytes()
  {
    ats_swap32(&ch_id);
    ats_swap32(&sequence_number);
  }
};

//
// ClusterFunctionPtr
//   A pointer to a procedure which can be invoked accross the cluster.
//   This must be registered.
//
typedef void ClusterFunction(ClusterHandler *ch, void *data, int len);
typedef ClusterFunction *ClusterFunctionPtr;

struct ClusterVConnectionBase;

struct ClusterVConnState {
  //
  // Private
  //
  volatile int enabled;
  // multiples of XXX_PERIOD, high = less often
  int priority;
  VIO vio;
  void *queue;
  int ifd;
  Event *delay_timeout;
  Link<ClusterVConnectionBase> link;

  // void enqueue(void * q, ClusterVConnection * vc);
  ClusterVConnState();
};

struct ClusterVConnectionBase : public CacheVConnection {
  //
  // Initiate an IO operation.
  // "data" is unused.
  // Only one READ and one WRITE may be active at one time.
  //   THREAD-SAFE, may be called when not handling an event from
  //                the ClusterVConnectionBase, or the ClusterVConnectionBase
  //                creation callback.
  //

  virtual VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);
  virtual VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false);
  virtual void
  do_io_shutdown(ShutdownHowTo_t howto)
  {
    (void)howto;
    ink_assert(!"shutdown of cluster connection");
  }
  virtual void do_io_close(int lerrno = -1);
  virtual VIO *do_io_pread(Continuation *, int64_t, MIOBuffer *, int64_t);

  // Set the timeouts associated with this connection.
  // active_timeout is for the total elasped time of the connection.
  // inactivity_timeout is the elapsed time *while an operation was
  //   enabled* during which the connection was unable to sink/provide data.
  // calling these functions repeatedly resets the timeout.
  //   NOT THREAD-SAFE, may only be called when handing an event from this
  //                    ClusterVConnectionBase, or the ClusterVConnectionBase
  //                    creation callback.
  //
  void set_active_timeout(ink_hrtime timeout_in);
  void set_inactivity_timeout(ink_hrtime timeout_in);
  void cancel_active_timeout();
  void cancel_inactivity_timeout();

  ClusterVConnectionBase();

#ifdef DEBUG
  // Class static data
  static int enable_debug_trace;
#endif
  Action action_;
  EThread *thread;
  volatile int closed;
  ClusterVConnState read;
  ClusterVConnState write;
  LINKM(ClusterVConnectionBase, read, link)
  LINKM(ClusterVConnectionBase, write, link)
  ink_hrtime inactivity_timeout_in;
  ink_hrtime active_timeout_in;
  Event *inactivity_timeout;
  Event *active_timeout;

  virtual void reenable(VIO *);
  virtual void reenable_re(VIO *);
};

inline void
ClusterVConnectionBase::set_active_timeout(ink_hrtime timeout)
{
  active_timeout_in = timeout;
  if (active_timeout) {
    ink_assert(!active_timeout->cancelled);
    if (active_timeout->ethread == this_ethread())
      active_timeout->schedule_in(timeout);
    else {
      active_timeout->cancel(this);
      active_timeout = thread->schedule_in(this, timeout);
    }
  } else {
    if (thread) {
      active_timeout = thread->schedule_in(this, timeout);
    }
  }
}

inline void
ClusterVConnectionBase::set_inactivity_timeout(ink_hrtime timeout)
{
  inactivity_timeout_in = timeout;
  if (inactivity_timeout) {
    ink_assert(!inactivity_timeout->cancelled);
    if (inactivity_timeout->ethread == this_ethread())
      inactivity_timeout->schedule_in(timeout);
    else {
      inactivity_timeout->cancel(this);
      inactivity_timeout = thread->schedule_in(this, timeout);
    }
  } else {
    if (thread) {
      inactivity_timeout = thread->schedule_in(this, timeout);
    }
  }
}

inline void
ClusterVConnectionBase::cancel_active_timeout()
{
  if (active_timeout) {
    active_timeout->cancel(this);
    active_timeout    = NULL;
    active_timeout_in = 0;
  }
}

inline void
ClusterVConnectionBase::cancel_inactivity_timeout()
{
  if (inactivity_timeout) {
    inactivity_timeout->cancel(this);
    inactivity_timeout    = NULL;
    inactivity_timeout_in = 0;
  }
}

// Data debt owed to VC which is deferred due to lock miss
class ByteBankDescriptor
{
public:
  ByteBankDescriptor() {}
  IOBufferBlock *
  get_block()
  {
    return block;
  }

  static ByteBankDescriptor *ByteBankDescriptor_alloc(IOBufferBlock *);
  static void ByteBankDescriptor_free(ByteBankDescriptor *);

public:
  LINK(ByteBankDescriptor, link);

private:
  Ptr<IOBufferBlock> block; // holder of bank bytes
};

enum TypeVConnection {
  VC_NULL,
  VC_CLUSTER,
  VC_CLUSTER_READ,
  VC_CLUSTER_WRITE,
  VC_CLUSTER_CLOSED,
};

//
// ClusterVConnection
//
struct ClusterVConnection : public ClusterVConnectionBase {
  //
  // Public Interface (included from ClusterVConnectionBase)
  //
  // Thread-safe  (see Net.h for details)
  //
  // virtual VIO * do_io(
  //   int                   op,
  //   Continuation        * c = NULL,
  //   int                   nbytes = INT64_MAX,
  //   MIOBuffer           * buf = 0,
  //   int                   whence = SEEK_CUR);
  //
  // NOT Thread-safe (see Net.h for details)
  //
  // void set_active_timeout(ink_hrtime timeout_in);
  // void set_inactivity_timeout(ink_hrtime timeout_in);

  //
  // Private
  //

  int startEvent(int event, Event *e);
  int mainEvent(int event, Event *e);

  // 0 on success -1 on failure
  int start(EThread *t); // New connect protocol

  ClusterVConnection(int is_new_connect_read = 0);
  ~ClusterVConnection();
  void free(); // Destructor actions (we are using ClassAllocator)

  virtual void do_io_close(int lerrno = -1);
  virtual VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);
  virtual VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false);
  virtual void reenable(VIO *vio);

  ClusterHandler *ch;
  //
  //  Read Channel: (new_connect_read == true)
  //     - open_local()    caller is reader
  //     - connect_local() caller is writer
  //
  //  Write Channel: (new_connect_read == false)
  //     - open_local()    caller is writer
  //     - connect_local() caller is reader
  //
  int new_connect_read; // Data flow direction wrt origin node
  int remote_free;
  int last_local_free;
  int channel;
  ClusterVCToken token;
  volatile int close_disabled;
  volatile int remote_closed;
  volatile int remote_close_disabled;
  volatile int remote_lerrno;
  volatile uint32_t in_vcs;
  volatile uint32_t type;
  SLINK(ClusterVConnection, ready_alink);
  int was_closed();
  void allow_close();
  void disable_close();
  int was_remote_closed();
  void allow_remote_close();
  bool schedule_write();
  void set_type(int);
  ink_hrtime start_time;
  ink_hrtime last_activity_time;
  Queue<ByteBankDescriptor> byte_bank_q; // done awaiting completion
  int n_set_data_msgs;                   // # pending set_data() msgs on VC
  int n_recv_set_data_msgs;              // # set_data() msgs received on VC
  volatile int pending_remote_fill;      // Remote fill pending on connection
  Ptr<IOBufferBlock> read_block;         // Hold current data for open read
  bool remote_ram_cache_hit;             // Entire object was from remote ram cache
  bool have_all_data;                    // All data in read_block
  int initial_data_bytes;                // bytes in open_read buffer
  Ptr<IOBufferBlock> remote_write_block; // Write side data for remote fill
  void *current_cont;                    // Track current continuation (debug)

#define CLUSTER_IOV_NOT_OPEN -2
#define CLUSTER_IOV_NONE -1
  int iov_map; // which iov?

  Ptr<ProxyMutex> read_locked;
  Ptr<ProxyMutex> write_locked;

  // Data buffer for unmarshaled objects from remote node.
  Ptr<IOBufferData> marshal_buf;

  // Pending write data
  Ptr<IOBufferBlock> write_list;
  IOBufferBlock *write_list_tail;
  int write_list_bytes;
  int write_bytes_in_transit;

  CacheHTTPInfo alternate;
  time_t time_pin;
  int disk_io_priority;
  void set_remote_fill_action(Action *);

  // Indicates whether a cache hit was from an peering cluster cache
  bool
  is_ram_cache_hit() const
  {
    return remote_ram_cache_hit;
  };
  void
  set_ram_cache_hit(bool remote_hit)
  {
    remote_ram_cache_hit = remote_hit;
  }

  // For VC(s) established via OPEN_READ, we are passed a CacheHTTPInfo
  //  in the reply.
  virtual bool get_data(int id, void *data); // backward compatibility
  virtual void get_http_info(CacheHTTPInfo **);
  virtual int64_t get_object_size();
  virtual bool is_pread_capable();

  // For VC(s) established via the HTTP version of OPEN_WRITE, additional
  //  data for the VC is passed in a second message.  This additional
  //  data has a lifetime equal to the cache VC
  virtual void set_http_info(CacheHTTPInfo *);

  virtual bool set_pin_in_cache(time_t time_pin);
  virtual time_t get_pin_in_cache();
  virtual bool set_disk_io_priority(int priority);
  virtual int get_disk_io_priority();
  virtual int get_header(void **ptr, int *len);
  virtual int set_header(void *ptr, int len);
  virtual int get_single_data(void **ptr, int *len);
};

//
// Cluster operation options
//
#define CLUSTER_OPT_STEAL 0x0001            // allow thread stealing
#define CLUSTER_OPT_IMMEDIATE 0x0002        // require immediate response
#define CLUSTER_OPT_ALLOW_IMMEDIATE 0x0004  // allow immediate response
#define CLUSTER_OPT_DELAY 0x0008            // require delayed response
#define CLUSTER_OPT_CONN_READ 0x0010        // new conn read
#define CLUSTER_OPT_CONN_WRITE 0x0020       // new conn write
#define CLUSTER_OPT_DATA_IS_OCONTROL 0x0040 // data in OutgoingControl
#define CLUSTER_FUNCTION_MALLOCED -1

struct ClusterRemoteDataHeader {
  int32_t cluster_function;
};
//
// ClusterProcessor
//
class ClusterAccept;

struct ClusterProcessor {
  //
  // Public Interface
  //

  // Invoke a function on a remote node
  //   marshal your own data, provide a continuation for timeouts and errors
  //
  // Options: CLUSTER_OPT_DELAY, CLUSTER_OPT_STEAL, CLUSTER_OPT_DATA_IS_OCONTROL
  // Returns: 1 for immediate send, 0 for delayed, -1 for error

  int invoke_remote(ClusterHandler *ch, int cluster_fn_index, void *data, int len, int options = CLUSTER_OPT_STEAL);

  int invoke_remote_data(ClusterHandler *ch, int cluster_fn_index, void *data, int data_len, IOBufferBlock *buf,
                         int logical_channel, ClusterVCToken *token, void (*bufdata_free)(void *), void *bufdata_free_arg,
                         int options = CLUSTER_OPT_STEAL);

  // Pass the data in as a malloc'ed block to be freed by callee
  int
  invoke_remote_malloced(ClusterHandler *ch, ClusterRemoteDataHeader *data, int len /* including header */)
  {
    return invoke_remote(ch, CLUSTER_FUNCTION_MALLOCED, data, len);
  }
  void free_remote_data(char *data, int len);

// Allocate the local side of a remote VConnection.
// returns a token which can be passed to the remote side
// through an existing link and passed to attach_remoteVC()
// if CLUSTER_OPT_IMMEDIATE is set, CLUSTER_DELAYED_OPEN will not be returned
//
// Options: CLUSTER_OPT_IMMEDIATE, CLUSTER_OPT_ALLOW_IMMEDIATE
// Returns: pointer for CLUSTER_OPT_IMMEDIATE
//            or CLUSTER_DELAYED_OPEN on success,
//          NULL on failure
// calls:  cont->handleEvent( CLUSTER_EVENT_OPEN, ClusterVConnection *)
//         on delayed success.
//
// NOTE: the CLUSTER_EVENT_OPEN may be called before "open/connect" returns

#define CLUSTER_DELAYED_OPEN ((ClusterVConnection *)-1)
#define CLUSTER_NODE_DOWN ((ClusterVConnection *)-2)
  ClusterVConnection *open_local(Continuation *cont, ClusterMachine *mp, ClusterVCToken &token, int options = 0);

  // Get the other side of a remote VConnection which was previously
  // allocated with open.
  //
  // Options: CLUSTER_OPT_IMMEDIATE, CLUSTER_OPT_ALLOW_IMMEDIATE
  // return a pointer or CLUSTER_DELAYED_OPEN success, NULL on failure
  //
  ClusterVConnection *connect_local(Continuation *cont, ClusterVCToken *token, int channel, int options = 0);
  inkcoreapi bool disable_remote_cluster_ops(ClusterMachine *);

  //
  // Processor interface
  //
  virtual int init();
  virtual int start();

  ClusterProcessor();
  virtual ~ClusterProcessor();

  //
  // Private
  //
  ClusterAccept *accept_handler;
  Cluster *this_cluster;
  // Connect to a new cluster machine
  void connect(char *hostname, int16_t id = -1);
  void connect(unsigned int ip, int port = 0, int16_t id = -1, bool delay = false);
  // send the list of known machines to new machine
  void send_machine_list(ClusterMachine *m);
  void compute_cluster_mode();
  // Internal invoke_remote interface
  int internal_invoke_remote(ClusterHandler *m, int cluster_fn, void *data, int len, int options, void *cmsg);
};

inkcoreapi extern ClusterProcessor clusterProcessor;

inline Cluster *
this_cluster()
{
  return clusterProcessor.this_cluster;
}

//
// Set up a thread to receive events from the ClusterProcessor
// This function should be called for all threads created to
// accept such events by the EventProcesor.
//
void initialize_thread_for_cluster(EThread *thread);

//
// ClusterFunction Registry
//
//   Declare an instance of this class here to register
//   a function.   In order to allow older versions of software
//   to co-exist with newer versions, always add to the bottom
//   of the list.
//

extern ClusterFunction *ptest_ClusterFunction;

extern ClusterFunction test_ClusterFunction;
extern ClusterFunction ping_ClusterFunction;
extern ClusterFunction ping_reply_ClusterFunction;
extern ClusterFunction machine_list_ClusterFunction;
extern ClusterFunction close_channel_ClusterFunction;
extern ClusterFunction get_hostinfo_ClusterFunction;
extern ClusterFunction put_hostinfo_ClusterFunction;
extern ClusterFunction cache_lookup_ClusterFunction;
extern ClusterFunction cache_op_ClusterFunction;
extern ClusterFunction cache_op_malloc_ClusterFunction;
extern ClusterFunction cache_op_result_ClusterFunction;
extern ClusterFunction set_channel_data_ClusterFunction;
extern ClusterFunction post_setchan_send_ClusterFunction;
extern ClusterFunction set_channel_pin_ClusterFunction;
extern ClusterFunction post_setchan_pin_ClusterFunction;
extern ClusterFunction set_channel_priority_ClusterFunction;
extern ClusterFunction post_setchan_priority_ClusterFunction;
extern ClusterFunction default_api_ClusterFunction;

struct ClusterFunctionDescriptor {
  bool fMalloced;   // the function will free the data
  bool ClusterFunc; // Process incoming message only
  //   in ET_CLUSTER thread.
  int q_priority; // lower is higher priority
  ClusterFunctionPtr pfn;
  ClusterFunctionPtr post_pfn; // msg queue/send callout
};

#define CLUSTER_CMSG_QUEUES 2
#define CMSG_MAX_PRI 0
#define CMSG_LOW_PRI (CLUSTER_CMSG_QUEUES - 1)

#ifndef DEFINE_CLUSTER_FUNCTIONS
extern
#endif
  ClusterFunctionDescriptor clusterFunction[]
#ifdef DEFINE_CLUSTER_FUNCTIONS
  =
    {
      {false, true, CMSG_LOW_PRI, test_ClusterFunction, 0},
      {false, true, CMSG_LOW_PRI, ping_ClusterFunction, 0},
      {false, true, CMSG_LOW_PRI, ping_reply_ClusterFunction, 0},
      {false, true, CMSG_LOW_PRI, machine_list_ClusterFunction, 0},
      {false, true, CMSG_LOW_PRI, close_channel_ClusterFunction, 0},
      {false, false, CMSG_LOW_PRI, get_hostinfo_ClusterFunction, 0}, // in HostDB.cc
      {false, false, CMSG_LOW_PRI, put_hostinfo_ClusterFunction, 0}, // in HostDB.cc
      {false, true, CMSG_LOW_PRI, cache_lookup_ClusterFunction, 0},  // in CacheCont.cc
      {true, true, CMSG_LOW_PRI, cache_op_malloc_ClusterFunction, 0},
      {false, true, CMSG_LOW_PRI, cache_op_ClusterFunction, 0},
      {false, false, CMSG_LOW_PRI, cache_op_result_ClusterFunction, 0},
      {false, false, CMSG_LOW_PRI, 0, 0}, // OBSOLETE
      {false, false, CMSG_LOW_PRI, 0, 0}, // OBSOLETE
      {false, false, CMSG_LOW_PRI, 0, 0}, // OBSOLETE
      {false, true, CMSG_MAX_PRI, set_channel_data_ClusterFunction, post_setchan_send_ClusterFunction},
      {false, true, CMSG_MAX_PRI, set_channel_pin_ClusterFunction, post_setchan_pin_ClusterFunction},
      {false, true, CMSG_MAX_PRI, set_channel_priority_ClusterFunction, post_setchan_priority_ClusterFunction},
      /********************************************
       * RESERVED for future cluster internal use *
       ********************************************/
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},
      {false, false, CMSG_LOW_PRI, 0, 0},

      /*********************************************
       * RESERVED for Cluster RPC API use		*
       *********************************************/
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0},
      {true, false, CMSG_LOW_PRI, default_api_ClusterFunction, 0}
      // ********** ADD NEW ENTRIES ABOVE THIS LINE ************
}
#endif

;
extern unsigned SIZE_clusterFunction; // clusterFunction[] entries

//////////////////////////////////////////////////////////////
// Map from Cluster Function code to send queue priority
//////////////////////////////////////////////////////////////
inline int
ClusterFuncToQpri(int cluster_func)
{
  if (cluster_func < 0) {
    return CMSG_LOW_PRI;
  } else {
    return clusterFunction[cluster_func].q_priority;
  }
}

//
// This table had better match the above list
//
#define TEST_CLUSTER_FUNCTION 0
#define PING_CLUSTER_FUNCTION 1
#define PING_REPLY_CLUSTER_FUNCTION 2
#define MACHINE_LIST_CLUSTER_FUNCTION 3
#define CLOSE_CHANNEL_CLUSTER_FUNCTION 4
#define GET_HOSTINFO_CLUSTER_FUNCTION 5
#define PUT_HOSTINFO_CLUSTER_FUNCTION 6
#define CACHE_LOOKUP_CLUSTER_FUNCTION 7
#define CACHE_OP_MALLOCED_CLUSTER_FUNCTION 8
#define CACHE_OP_CLUSTER_FUNCTION 9
#define CACHE_OP_RESULT_CLUSTER_FUNCTION 10
#define SET_CHANNEL_DATA_CLUSTER_FUNCTION 14
#define SET_CHANNEL_PIN_CLUSTER_FUNCTION 15
#define SET_CHANNEL_PRIORITY_CLUSTER_FUNCTION 16

/********************************************
 * RESERVED for future cluster internal use *
 ********************************************/
#define INTERNAL_RESERVED1_CLUSTER_FUNCTION 17
#define INTERNAL_RESERVED2_CLUSTER_FUNCTION 18
#define INTERNAL_RESERVED3_CLUSTER_FUNCTION 19
#define INTERNAL_RESERVED4_CLUSTER_FUNCTION 20
#define INTERNAL_RESERVED5_CLUSTER_FUNCTION 21
#define INTERNAL_RESERVED6_CLUSTER_FUNCTION 22
#define INTERNAL_RESERVED7_CLUSTER_FUNCTION 23
#define INTERNAL_RESERVED8_CLUSTER_FUNCTION 24
#define INTERNAL_RESERVED9_CLUSTER_FUNCTION 25
#define INTERNAL_RESERVED10_CLUSTER_FUNCTION 26
#define INTERNAL_RESERVED11_CLUSTER_FUNCTION 27
#define INTERNAL_RESERVED12_CLUSTER_FUNCTION 28
#define INTERNAL_RESERVED13_CLUSTER_FUNCTION 29
#define INTERNAL_RESERVED14_CLUSTER_FUNCTION 30
#define INTERNAL_RESERVED15_CLUSTER_FUNCTION 31
#define INTERNAL_RESERVED16_CLUSTER_FUNCTION 32
#define INTERNAL_RESERVED17_CLUSTER_FUNCTION 33
#define INTERNAL_RESERVED18_CLUSTER_FUNCTION 34
#define INTERNAL_RESERVED19_CLUSTER_FUNCTION 35
#define INTERNAL_RESERVED20_CLUSTER_FUNCTION 36
#define INTERNAL_RESERVED21_CLUSTER_FUNCTION 37
#define INTERNAL_RESERVED22_CLUSTER_FUNCTION 38
#define INTERNAL_RESERVED23_CLUSTER_FUNCTION 39
#define INTERNAL_RESERVED24_CLUSTER_FUNCTION 40
#define INTERNAL_RESERVED25_CLUSTER_FUNCTION 41
#define INTERNAL_RESERVED26_CLUSTER_FUNCTION 42
#define INTERNAL_RESERVED27_CLUSTER_FUNCTION 43
#define INTERNAL_RESERVED28_CLUSTER_FUNCTION 44
#define INTERNAL_RESERVED29_CLUSTER_FUNCTION 45
#define INTERNAL_RESERVED30_CLUSTER_FUNCTION 46
#define INTERNAL_RESERVED31_CLUSTER_FUNCTION 47
#define INTERNAL_RESERVED32_CLUSTER_FUNCTION 48
#define INTERNAL_RESERVED33_CLUSTER_FUNCTION 49
#define INTERNAL_RESERVED34_CLUSTER_FUNCTION 50

/****************************************************************************
 * Cluster RPC API definitions.						    *
 *									    *
 ****************************************************************************
 * Note: All of the following must be kept in sync with INKClusterRPCKey_t  *
 * 	 definition in ts/ts.h and ts/experimental.h			    *
 ****************************************************************************/

/************************************************
 * RESERVED for Wireless Group			*
 ************************************************/
#define API_F01_CLUSTER_FUNCTION 51
#define API_F02_CLUSTER_FUNCTION 52
#define API_F03_CLUSTER_FUNCTION 53
#define API_F04_CLUSTER_FUNCTION 54
#define API_F05_CLUSTER_FUNCTION 55
#define API_F06_CLUSTER_FUNCTION 56
#define API_F07_CLUSTER_FUNCTION 57
#define API_F08_CLUSTER_FUNCTION 58
#define API_F09_CLUSTER_FUNCTION 59
#define API_F10_CLUSTER_FUNCTION 60

/************************************************
 * RESERVED for future use			*
 ************************************************/
#define API_F11_CLUSTER_FUNCTION 61
#define API_F12_CLUSTER_FUNCTION 62
#define API_F13_CLUSTER_FUNCTION 63
#define API_F14_CLUSTER_FUNCTION 64
#define API_F15_CLUSTER_FUNCTION 65
#define API_F16_CLUSTER_FUNCTION 66
#define API_F17_CLUSTER_FUNCTION 67
#define API_F18_CLUSTER_FUNCTION 68
#define API_F19_CLUSTER_FUNCTION 69
#define API_F20_CLUSTER_FUNCTION 70

#define API_F21_CLUSTER_FUNCTION 71
#define API_F22_CLUSTER_FUNCTION 72
#define API_F23_CLUSTER_FUNCTION 73
#define API_F24_CLUSTER_FUNCTION 74
#define API_F25_CLUSTER_FUNCTION 75
#define API_F26_CLUSTER_FUNCTION 76
#define API_F27_CLUSTER_FUNCTION 77
#define API_F28_CLUSTER_FUNCTION 78
#define API_F29_CLUSTER_FUNCTION 79
#define API_F30_CLUSTER_FUNCTION 80

#define API_STARECT_CLUSTER_FUNCTION API_F01_CLUSTER_FUNCTION
#define API_END_CLUSTER_FUNCTION API_F30_CLUSTER_FUNCTION

#define UNDEFINED_CLUSTER_FUNCTION 0xFDEFFDEF

//////////////////////////////////////////////
// Initial cluster connect exchange message
//////////////////////////////////////////////
struct ClusterHelloMessage {
  uint16_t _NativeByteOrder;
  uint16_t _major;
  uint16_t _minor;
  uint16_t _min_major;
  uint16_t _min_minor;
  int16_t _id;
#ifdef LOCAL_CLUSTER_TEST_MODE
  int16_t _port;
  char _pad[114]; // pad out to 128 bytes
#else
  char _pad[116]; // pad out to 128 bytes
#endif

  ClusterHelloMessage() : _NativeByteOrder(1)
  {
    _major     = CLUSTER_MAJOR_VERSION;
    _minor     = CLUSTER_MINOR_VERSION;
    _min_major = MIN_CLUSTER_MAJOR_VERSION;
    _min_minor = MIN_CLUSTER_MINOR_VERSION;
    memset(_pad, '\0', sizeof(_pad));
  }
  int
  NativeByteOrder()
  {
    return (_NativeByteOrder == 1);
  }
  void
  AdjustByteOrder()
  {
    if (!NativeByteOrder()) {
      ats_swap16(&_major);
      ats_swap16(&_minor);
      ats_swap16(&_min_major);
      ats_swap16(&_min_minor);
    }
  }
};

///////////////////////////////////////////////////////////////////
// Cluster message header definition.
///////////////////////////////////////////////////////////////////
struct ClusterMessageHeader {
  uint16_t _InNativeByteOrder; // always non-zero
  uint16_t _MsgVersion;        // always non-zero

  void
  _init(uint16_t msg_version)
  {
    _InNativeByteOrder = 1;
    _MsgVersion        = msg_version;
  }
  ClusterMessageHeader() : _InNativeByteOrder(0), _MsgVersion(0) {}
  ClusterMessageHeader(uint16_t msg_version) { _init(msg_version); }
  int
  MsgInNativeByteOrder()
  {
    return (_InNativeByteOrder == 1);
  }
  int
  NeedByteSwap()
  {
    return (_InNativeByteOrder != 1);
  }
  int
  GetMsgVersion()
  {
    if (NeedByteSwap()) {
      return ats_swap16(_MsgVersion);
    } else {
      return _MsgVersion;
    }
  }
};

///////////////////////////////////////////////////////////////////

//
// cluster_ping
//
typedef void (*PingReturnFunction)(ClusterHandler *, void *data, int len);

struct PingMessage : public ClusterMessageHeader {
  PingReturnFunction fn; // Note: Pointer to a func
  char data[1];          // start of data

  enum {
    MIN_VERSION          = 1,
    MAX_VERSION          = 1,
    PING_MESSAGE_VERSION = MAX_VERSION,
  };

  PingMessage(uint16_t vers = PING_MESSAGE_VERSION) : ClusterMessageHeader(vers), fn(NULL) { data[0] = '\0'; }
  /////////////////////////////////////////////////////////////////////////////
  static int
  protoToVersion(int protoMajor)
  {
    (void)protoMajor;
    return PING_MESSAGE_VERSION;
  }
  static int
  sizeof_fixedlen_msg()
  {
    PingMessage *p = 0;
    // Maybe use offsetof here instead. /leif
    return (uintptr_t)(&p->data[0]);
  }
  void
  init(uint16_t vers = PING_MESSAGE_VERSION)
  {
    _init(vers);
  }
  inline void
  SwapBytes()
  {
  } // No action, message is always reflected back
  /////////////////////////////////////////////////////////////////////////////
};

inline void
cluster_ping(ClusterHandler *ch, PingReturnFunction fn, void *data, int len)
{
  PingMessage *msg = (PingMessage *)alloca(PingMessage::sizeof_fixedlen_msg() + len);
  msg->init();
  msg->fn = fn;
  memcpy(msg->data, data, len);
  clusterProcessor.invoke_remote(ch, PING_CLUSTER_FUNCTION, (void *)msg, (msg->sizeof_fixedlen_msg() + len));
}

// filled with 0's
extern char channel_dummy_output[DEFAULT_MAX_BUFFER_SIZE];

//
// Private (for testing)
//
ClusterConfiguration *configuration_add_machine(ClusterConfiguration *c, ClusterMachine *m);
ClusterConfiguration *configuration_remove_machine(ClusterConfiguration *c, ClusterMachine *m);
extern bool machineClusterHash;
extern bool boundClusterHash;
extern bool randClusterHash;

void build_cluster_hash_table(ClusterConfiguration *);

inline void
ClusterVC_enqueue_read(Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_read_link> &q, ClusterVConnectionBase *vc)
{
  ClusterVConnState *cs = &vc->read;
  ink_assert(!cs->queue);
  cs->queue = &q;
  q.enqueue(vc);
}

inline void
ClusterVC_enqueue_write(Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_write_link> &q, ClusterVConnectionBase *vc)
{
  ClusterVConnState *cs = &vc->write;
  ink_assert(!cs->queue);
  cs->queue = &q;
  q.enqueue(vc);
}

inline void
ClusterVC_remove_read(ClusterVConnectionBase *vc)
{
  ClusterVConnState *cs = &vc->read;
  ink_assert(cs->queue);
  ((Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_read_link> *)cs->queue)->remove(vc);
  cs->queue = NULL;
}

inline void
ClusterVC_remove_write(ClusterVConnectionBase *vc)
{
  ClusterVConnState *cs = &vc->write;
  ink_assert(cs->queue);
  ((Queue<ClusterVConnectionBase, ClusterVConnectionBase::Link_write_link> *)cs->queue)->remove(vc);
  cs->queue = NULL;
}

#endif /* _Cluster_h */
