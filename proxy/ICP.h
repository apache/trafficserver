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

  ICP.h


****************************************************************************/

#ifndef _ICP_H_
#define _ICP_H_

#include "P_Net.h"
#include "P_Cache.h"
#define ET_ICP ET_CALL
#include "URL.h"
#include "ICPevents.h"
#include "ICPProcessor.h"
#include "ts/DynArray.h"

//*********************************************************************
// ICP Configurables
//*********************************************************************
#define ICP_DEBUG 1

//*********************************************************************
// ICP.h -- Internet Cache Protocol (ICP) related data structures.
//
// Message protocol definitions as defined by RFC 2186
// "Internet Cache Protocol (ICP), version 2".
//*********************************************************************
typedef struct ICPMsgHeader {
  uint8_t opcode;
  uint8_t version;
  uint16_t msglen;
  uint32_t requestno;
  uint32_t optionflags;
  uint32_t optiondata;
  uint32_t shostid;
} ICPMsgHdr_t;

//-----------------------
// opcode definitions
//-----------------------
typedef enum {
  ICP_OP_INVALID, // 00
  ICP_OP_QUERY,   // 01
  ICP_OP_HIT,     // 02
  ICP_OP_MISS,    // 03
  ICP_OP_ERR,     // 04
  //
  ICP_OP_UNUSED5, // 05 unused
  ICP_OP_UNUSED6, // 06 unused
  ICP_OP_UNUSED7, // 07 unused
  ICP_OP_UNUSED8, // 08 unused
  ICP_OP_UNUSED9, // 09 unused
  //
  ICP_OP_SECHO, // 10
  ICP_OP_DECHO, // 11
  //
  ICP_OP_UNUSED12, // 12 unused
  ICP_OP_UNUSED13, // 13 unused
  ICP_OP_UNUSED14, // 14 unused
  ICP_OP_UNUSED15, // 15 unused
  ICP_OP_UNUSED16, // 16 unused
  ICP_OP_UNUSED17, // 17 unused
  ICP_OP_UNUSED18, // 18 unused
  ICP_OP_UNUSED19, // 19 unused
  ICP_OP_UNUSED20, // 20 unused
  //
  ICP_OP_MISS_NOFETCH, // 21
  ICP_OP_DENIED,       // 22
  ICP_OP_HIT_OBJ,      // 23
  ICP_OP_END_OF_OPS    // 24 mark end of opcodes
} ICPopcode_t;

#define ICP_OP_LAST (ICP_OP_END_OF_OPS - 1)

//-----------------------
// version definitions
//-----------------------
#define ICP_VERSION_1 1
#define ICP_VERSION_2 2
#define ICP_VERSION_3 3
#define ICP_VERSION ICP_VERSION_2

//--------------------------
// optionflags definitions
//--------------------------
#define ICP_FLAG_HIT_OBJ 0x80000000ul
#define ICP_FLAG_SRC_RTT 0x40000000ul

//-----------------
// ICP Constants
//-----------------
#define MAX_ICP_MSGSIZE (16 * 1024)
#define MAX_ICP_MSG_PAYLOAD_SIZE (MAX_ICP_MSGSIZE - sizeof(ICPmsgHdr_t))
#define MAX_ICP_QUERY_PAYLOAD_SIZE (MAX_ICP_MSG_PAYLOAD_SIZE - sizeof(uint32_t))
#define MAX_DEFINED_PEERS 64
#define MSG_IOVECS 16

//------------
// ICP Data
//------------
typedef struct ICPData {
  char *URL; // null terminated
} ICPData_t;

//-------------
// ICP Query
//-------------
typedef struct ICPQuery {
  uint32_t rhostid;
  char *URL; // null terminated (outgoing)
} ICPQuery_t;

//------------
// ICP Hit
//------------
typedef struct ICPHit {
  char *URL; // null terminated
} ICPHit_t;

//------------
// ICP Miss
//------------
typedef struct ICPMiss {
  char *URL; // null terminated
} ICPMiss_t;

//------------------
// ICP Hit Object
//------------------
typedef struct ICPHitObj {
  char *URL;        // null terminated
  char *p_objsize;  // byte aligned uint16_t immediately follows URL null
  uint16_t objsize; // decoded object size
  char *data;       // object data
} ICPHitObj_t;

//------------------------
// ICP message descriptor
//------------------------
typedef struct ICPMsg {
  ICPMsgHdr_t h;
  union {
    ICPData_t data;
    ICPQuery_t query;
    ICPHit_t hit;
    ICPMiss_t miss;
    ICPHitObj_t hitobj;
  } un;
} ICPMsg_t;

//******************************************************************
// ICP implementation specific data structures.
//******************************************************************

class BitMap;
class ICPProcessor;
class ICPPeriodicCont;
class ICPHandlerCont;
class ICPPeerReadCont;
class ICPRequestCont;

typedef enum {
  PEER_NONE      = 0,
  PEER_PARENT    = 1,
  PEER_SIBLING   = 2,
  PEER_LOCAL     = 3,
  PEER_MULTICAST = 4,
} PeerType_t;

#if !defined(USE_CAS_FOR_ATOMICLOCK)
class AtomicLock
{
public:
  AtomicLock();
  ~AtomicLock();
  int Lock();
  int HaveLock();
  void Unlock();

private:
  Ptr<ProxyMutex> _mutex;
};

#else  // USE_CAS_FOR_ATOMICLOCK
class AtomicLock
{
public:
  AtomicLock();
  ~AtomicLock();
  int Lock();
  int HaveLock();
  void Unlock();

private:
  enum {
    UNLOCKED = 0,
    LOCKED   = 1,
  };
  int32_t _lock_word;
};
#endif // USE_CAS_FOR_ATOMICLOCK

//-----------------------------------------------------------------
// Class ICPConfigData -- deal with global ICP configuration data
//-----------------------------------------------------------------
class ICPConfigData
{
  friend class ICPConfiguration;

public:
  ICPConfigData()
    : _icp_enabled(0),
      _icp_port(0),
      _icp_interface(0),
      _multicast_enabled(0),
      _icp_query_timeout(0),
      _cache_lookup_local(0),
      _stale_lookup(0),
      _reply_to_unknown_peer(0),
      _default_reply_port(0),
      _cache_generation(-1)
  {
  }
  ~ICPConfigData() {} // Note: _icp_interface freed prior to delete
  inline int operator==(ICPConfigData &);
  inline int
  ICPconfigured()
  {
    return _icp_enabled;
  }
  inline int
  ICPport()
  {
    return _icp_port;
  }
  inline char *
  ICPinterface()
  {
    return _icp_interface;
  }
  inline int
  ICPmulticastConfigured()
  {
    return _multicast_enabled;
  }
  inline int
  ICPqueryTimeout()
  {
    return _icp_query_timeout;
  }
  inline int
  ICPLocalCacheLookup()
  {
    return _cache_lookup_local;
  }
  inline int
  ICPStaleLookup()
  {
    return _stale_lookup;
  }
  inline int
  ICPReplyToUnknownPeer()
  {
    return _reply_to_unknown_peer;
  }
  inline int
  ICPDefaultReplyPort()
  {
    return _default_reply_port;
  }
  inline cache_generation_t
  ICPCacheGeneration() const
  {
    return _cache_generation;
  }

private:
  //---------------------------------------------------------
  // ICP Configuration data derived from "records.config"
  //---------------------------------------------------------
  int _icp_enabled; // see ICP_MODE_XXX defines
  int _icp_port;
  char *_icp_interface;
  int _multicast_enabled;
  int _icp_query_timeout;
  int _cache_lookup_local;
  int _stale_lookup;
  int _reply_to_unknown_peer;
  int _default_reply_port;
  int64_t _cache_generation;
};

//----------------------------------------------------------------
// Class PeerConfigData -- deal with peer ICP configuration data
//----------------------------------------------------------------
class PeerConfigData
{
  friend class ICPConfiguration;
  friend class ICPProcessor;

public:
  PeerConfigData();
  PeerConfigData(int ctype, IpAddr const &ip_addr, int proxy_port, int icp_port)
    : _ctype(ctype),
      _ip_addr(ip_addr),
      _proxy_port(proxy_port),
      _icp_port(icp_port),
      _mc_member(0),
      _mc_ttl(0),
      _my_ip_addr(ip_addr)
  {
    _hostname[0] = 0;
  }
  ~PeerConfigData() {}
  bool operator==(PeerConfigData &);
  inline const char *
  GetHostname()
  {
    return _hostname;
  }
  inline int
  GetCType()
  {
    return _ctype;
  }
  inline IpAddr const &
  GetIPAddr()
  {
    return _my_ip_addr;
  }
  inline int
  GetProxyPort()
  {
    return _proxy_port;
  }
  inline int
  GetICPPort()
  {
    return _icp_port;
  }
  inline int
  MultiCastMember()
  {
    return _mc_member;
  }
  inline IpAddr const &
  GetMultiCastIPAddr()
  {
    return _mc_ip_addr;
  }
  inline int
  GetMultiCastTTL()
  {
    return _mc_ttl;
  }

  // Static member functions
  static PeerType_t CTypeToPeerType_t(int);
  static int GetHostIPByName(char *, IpAddr &);

  enum {
    HOSTNAME_SIZE = 256,
  };
  enum {
    CTYPE_NONE    = 0,
    CTYPE_PARENT  = 1,
    CTYPE_SIBLING = 2,
    CTYPE_LOCAL   = 3,
  };

private:
  //---------------------------------------------------------
  // Peer Configuration data derived from "icp.config"
  //---------------------------------------------------------
  char _hostname[HOSTNAME_SIZE];
  int _ctype;
  IpAddr _ip_addr;
  int _proxy_port;
  int _icp_port;
  //-------------------
  // MultiCast data
  //-------------------
  int _mc_member;
  IpAddr _mc_ip_addr;
  int _mc_ttl;

  //----------------------------------------------
  // Computed data not subject to "==" test
  //----------------------------------------------
  IpAddr _my_ip_addr;
};

//---------------------------------------------------------------
// Class ICPConfigUpdateCont -- Continuation which retries
//  icp_config_change_callback(). Continuation started
//  due to manager config callout or failure to acquire lock.
//---------------------------------------------------------------
class ICPConfigUpdateCont : public Continuation
{
public:
  ICPConfigUpdateCont(void *data, void *value);
  ~ICPConfigUpdateCont() {}
  int RetryICPconfigUpdate(int, Event *);

  enum {
    RETRY_INTERVAL = 10,
  };

private:
  void *_data;
  void *_value;
};

//------------------------------------------------------------------
// Class ICPConfiguration -- Overall management of ICP Config data
//------------------------------------------------------------------
class ICPConfiguration
{
public:
  ICPConfiguration();
  ~ICPConfiguration();
  int GlobalConfigChange();
  void UpdateGlobalConfig();
  int PeerConfigChange();
  void UpdatePeerConfig();

  inline ICPConfigData *
  globalConfig()
  {
    return _icp_cdata;
  }
  inline PeerConfigData *
  indexToPeerConfigData(int index)
  {
    ink_assert(index <= MAX_DEFINED_PEERS);
    return _peer_cdata[index];
  }

  // TS configuration management callout for "icp.config".
  static int mgr_icp_config_change_callback(const char *, RecDataT, RecData, void *);

  // ICP configuration callout for ET_ICP
  static void *icp_config_change_callback(void *, void *, int startup = 0);

  inline int
  Lock()
  {
    return _l.Lock();
  }
  inline void
  Unlock()
  {
    _l.Unlock();
    return;
  }
  inline int
  HaveLock()
  {
    return _l.HaveLock();
  }

  inline int
  ICPConfigCallouts()
  {
    return _icp_config_callouts;
  }

private:
  // Class data declarations
  AtomicLock _l;
  int _icp_config_callouts;

  // All ICP operation is based on "icp_data" and "peer_cdata".
  // The "icp_data_current" and "peer_cdata_current" reflect the
  // current state of the configuration.  "icp_data_current" is
  // updated via configuration callouts.  "peer_cdata_current"
  // is updated by the periodic ICP processor event (ICPPeriodicCont),
  // when configuration management signals us with a callout on "icp.config".
  // We merge current to working only after disabling ICP operation and
  // waiting for pending requests to complete.
  //
  ICPConfigData *_icp_cdata;
  ICPConfigData *_icp_cdata_current;
  PeerConfigData *_peer_cdata[MAX_DEFINED_PEERS + 1];
  PeerConfigData *_peer_cdata_current[MAX_DEFINED_PEERS + 1];
};

//------------------------------------------------------------------------
// Class Peer -- Internal structure representing ICP peers derived from
//               configuration data (abstract base class).
//------------------------------------------------------------------------

// Peer state
#define PEER_UP (1 << 0)
#define PEER_MULTICAST_COUNT_EVENT (1 << 1) // Member probe event active
#define PEER_DYNAMIC (1 << 2)               // Dynamically added, not in config

struct CacheVConnection;

class Peer : public RefCountObj
{
public:
  Peer(PeerType_t, ICPProcessor *, bool dynamic_peer = false);
  virtual ~Peer() {}
  void LogRecvMsg(ICPMsg_t *, int);

  // Pure virtual functions
  virtual sockaddr *GetIP() = 0;
  virtual Action *SendMsg_re(Continuation *, void *, struct msghdr *, struct sockaddr const *to) = 0;
  virtual Action *RecvFrom_re(Continuation *, void *, IOBufferBlock *, int, struct sockaddr *, socklen_t *) = 0;
  virtual int GetRecvFD()               = 0;
  virtual int GetSendFD()               = 0;
  virtual int ExpectedReplies(BitMap *) = 0;
  virtual int ValidSender(sockaddr *)   = 0;
  virtual void LogSendMsg(ICPMsg_t *, sockaddr const *) = 0;
  virtual int IsOnline()            = 0;
  virtual Connection *GetSendChan() = 0;
  virtual Connection *GetRecvChan() = 0;
  virtual int ExtToIntRecvSockAddr(sockaddr const *, sockaddr *) = 0;

  enum {
    OFFLINE_THRESHOLD = 20,
  };

  inline PeerType_t
  GetType()
  {
    return _type;
  }
  inline int
  GetPeerID()
  {
    return _id;
  }
  inline void
  SetPeerID(int newid)
  {
    _id = newid;
  }
  inline void
  SetNext(Peer *p)
  {
    _next = p;
  }
  inline Peer *
  GetNext()
  {
    return _next;
  }
  inline bool
  shouldStartRead()
  {
    return !notFirstRead;
  }
  inline void
  startingRead()
  {
    notFirstRead = 1;
  }
  inline void
  cancelRead()
  {
    notFirstRead = 0;
  }
  inline bool
  readActive()
  {
    return (readAction != NULL);
  }
  inline bool
  isUp()
  {
    return (_state & PEER_UP);
  }

  // these shouldn't be public
  // this is for delayed I/O
  Ptr<IOBufferBlock> buf;
  IpEndpoint fromaddr;
  socklen_t fromaddrlen;
  int notFirstRead;    // priming the reads
  Action *readAction;  // outstanding read
  Action *writeAction; // outstanding write

protected:
  PeerType_t _type;
  int _id; // handle for this peer
  Peer *_next;
  ICPProcessor *_ICPpr;

  //--------------
  // State data
  //--------------
  int _state;

  //-------------------
  // Peer Statistics
  //-------------------
  struct PeerStats {
    ink_hrtime last_send;
    ink_hrtime last_receive;
    int sent[ICP_OP_LAST + 1];
    int recv[ICP_OP_LAST + 1];
    int total_sent;
    int total_received;
    int dropped_replies; // arrived after timeout
  } _stats;
};

//------------------------------------------------
// Class ParentSiblingPeer (derived from Peer)
//------------------------------------------------
class ParentSiblingPeer : public Peer
{
public:
  ParentSiblingPeer(PeerType_t, PeerConfigData *, ICPProcessor *, bool dynamic_peer = false);
  ~ParentSiblingPeer()
  {
    if (_pconfig && (_state & PEER_DYNAMIC))
      delete _pconfig;
  }
  int GetProxyPort();
  int GetICPPort();
  virtual sockaddr *GetIP();
  virtual Action *SendMsg_re(Continuation *, void *, struct msghdr *, struct sockaddr const *to);
  virtual Action *RecvFrom_re(Continuation *, void *, IOBufferBlock *, int, struct sockaddr *, socklen_t *);
  virtual int GetRecvFD();
  virtual int GetSendFD();
  virtual int ExpectedReplies(BitMap *);
  virtual int ValidSender(struct sockaddr *);
  virtual void LogSendMsg(ICPMsg_t *, sockaddr const *);
  virtual int ExtToIntRecvSockAddr(sockaddr const *in, sockaddr *out);
  inline virtual int
  IsOnline()
  {
    return 1;
  }
  inline virtual Connection *
  GetSendChan()
  {
    return &_chan;
  }
  inline virtual Connection *
  GetRecvChan()
  {
    return &_chan;
  }
  inline PeerConfigData *
  GetConfig()
  {
    return _pconfig;
  }
  inline Connection *
  GetChan()
  {
    return &_chan;
  }

private:
  // Class data declarations
  PeerConfigData *_pconfig; // associated config data
  IpEndpoint _ip;           ///< Cache for GetIP().
  Connection _chan;
};

//------------------------------------------------
// Class MultiCastPeer (derived from Peer)
//------------------------------------------------
class MultiCastPeer : public Peer
{
public:
  MultiCastPeer(IpAddr const &, uint16_t, int, ICPProcessor *);
  ~MultiCastPeer() {}
  int GetTTL();
  int AddMultiCastChild(Peer *P);
  /** Find the multicast child peer with IP address @a ip on @a port.
      If @a port is 0 the port is not checked.
  */
  Peer *FindMultiCastChild(IpAddr const &ip, ///< IP address.
                           uint16_t port = 0 ///< Port (host order).
                           );

  virtual sockaddr *GetIP();
  virtual Action *SendMsg_re(Continuation *, void *, struct msghdr *, struct sockaddr const *to);
  virtual Action *RecvFrom_re(Continuation *, void *, IOBufferBlock *, int, struct sockaddr *, socklen_t *);
  virtual int GetRecvFD();
  virtual int GetSendFD();
  virtual int ExpectedReplies(BitMap *);
  virtual int ValidSender(struct sockaddr *);
  virtual void LogSendMsg(ICPMsg_t *, sockaddr const *);
  virtual int IsOnline();
  inline virtual Connection *
  GetRecvChan()
  {
    return &_recv_chan;
  }
  inline virtual Connection *
  GetSendChan()
  {
    return &_send_chan;
  }
  inline virtual int
  ExtToIntRecvSockAddr(sockaddr const *in, sockaddr *out)
  {
    Peer *P = FindMultiCastChild(IpAddr(in));
    if (P) {
      ats_ip_copy(out, in);
      ats_ip_port_cast(out) = ats_ip_port_cast(P->GetIP());
      return 1;
    } else {
      return 0;
    }
  }

private:
  // Class data declarations
  Connection _send_chan;
  Connection _recv_chan;
  //---------------------------
  // Multicast specific data
  //---------------------------
  IpEndpoint _mc_ip;
  int _mc_ttl;
  struct multicast_data {
    double avg_members;    // running avg of multicast responders
    int defined_members;   // as specified in icp.config
    int n_count_events;    // responder count events
    int count_event_reqno; // reqno associated with count event
    int expected_replies;  // current expected responders on multicast
  } _mc;
};

//----------------------------------------------------
// Class BitMap -- Generic bit map management class
//----------------------------------------------------
class BitMap
{
public:
  BitMap(int);
  ~BitMap();
  void SetBit(int);
  void ClearBit(int);
  int IsBitSet(int);

private:
  enum {
    STATIC_BITMAP_BYTE_SIZE = 16,
    BITS_PER_BYTE           = 8,
  };
  char _static_bitmap[STATIC_BITMAP_BYTE_SIZE];
  char *_bitmap;
  int _bitmap_size;
  int _bitmap_byte_size;
};

//----------------------------------------
// ICPProcessor -- ICP External interface
//----------------------------------------
class ICPProcessor
{
  friend class ICPHandlerCont;  // Incoming msg periodic handler
  friend class ICPPeerReadCont; // Incoming ICP request handler
  friend class ICPRequestCont;  // Outgoing ICP request handler

public:
  ICPProcessor();
  ~ICPProcessor();

  // Exported interfaces for other subsystems
  void start();
  Action *ICPQuery(Continuation *, URL *);

  // Exported interfaces to other ICP classes
  typedef enum {
    RC_RECONFIG,
    RC_ENABLE_ICP,
    RC_DONE,
  } ReconfigState_t;
  ReconfigState_t ReconfigureStateMachine(ReconfigState_t, int, int);

  Peer *FindPeer(IpAddr const &ip, uint16_t port = 0);
  Peer *
  FindPeer(IpEndpoint const &ip)
  {
    return this->FindPeer(IpAddr(&ip), ats_ip_port_host_order(&ip));
  }
  Peer *
  FindPeer(sockaddr const *ip)
  {
    return this->FindPeer(IpAddr(ip), ats_ip_port_host_order(ip));
  }

  inline Peer *
  GetLocalPeer()
  {
    return _LocalPeer;
  }
  inline Peer *
  IdToPeer(int id)
  {
    return _PeerList[id];
  }
  inline ICPConfiguration *
  GetConfig()
  {
    return _ICPConfig;
  }

  inline int
  GetFreePeers()
  {
    return PEER_LIST_SIZE - (_nPeerList + 1);
  }
  inline int
  GetFreeSendPeers()
  {
    return SEND_PEER_LIST_SIZE - (_nSendPeerList + 1);
  }
  inline int
  GetFreeRecvPeers()
  {
    return RECV_PEER_LIST_SIZE - (_nRecvPeerList + 1);
  }

private:
  inline int
  Lock()
  {
    return _l->Lock();
  }
  inline void
  Unlock()
  {
    _l->Unlock();
    return;
  }
  inline int
  HaveLock()
  {
    return _l->HaveLock();
  }
  int BuildPeerList();
  void FreePeerList();
  int SetupListenSockets();
  void ShutdownListenSockets();
  int Reconfigure(int, int);
  void InitICPStatCallbacks();

  inline void
  DisableICPQueries()
  {
    _AllowIcpQueries = 0;
  }
  inline void
  EnableICPQueries()
  {
    _AllowIcpQueries = 1;
  }
  inline int
  AllowICPQueries()
  {
    return _AllowIcpQueries;
  }
  inline int
  PendingQuery()
  {
    return _PendingIcpQueries;
  }
  inline void
  IncPendingQuery()
  {
    _PendingIcpQueries++;
  }
  inline void
  DecPendingQuery()
  {
    _PendingIcpQueries--;
  }

  Peer *GenericFindListPeer(IpAddr const &, uint16_t, int, Ptr<Peer> *);
  Peer *FindSendListPeer(IpAddr const &, uint16_t);
  Peer *FindRecvListPeer(IpAddr const &, uint16_t);
  int AddPeer(Peer *);
  int AddPeerToSendList(Peer *);
  int AddPeerToRecvList(Peer *);
  int AddPeerToParentList(Peer *);

  inline int
  GetSendPeers()
  {
    return _nSendPeerList + 1;
  }
  inline Peer *
  GetNthSendPeer(int n, int bias)
  {
    return _SendPeerList[(bias + n) % (_nSendPeerList + 1)];
  }

  inline int
  GetRecvPeers()
  {
    return _nRecvPeerList + 1;
  }
  inline Peer *
  GetNthRecvPeer(int n, int bias)
  {
    return _RecvPeerList[(bias + n) % (_nRecvPeerList + 1)];
  }

  inline int
  GetStartingSendPeerBias()
  {
    return ++_curSendPeer;
  }
  inline int
  GetStartingRecvPeerBias()
  {
    return ++_curRecvPeer;
  }

  inline int
  GetParentPeers()
  {
    return _nParentPeerList + 1;
  }
  inline Peer *
  GetNthParentPeer(int n, int bias)
  {
    return _ParentPeerList[(bias + n) % (_nParentPeerList + 1)];
  }
  inline int
  GetStartingParentPeerBias()
  {
    return ++_curParentPeer;
  }

  inline void
  SetLastRecvPeerBias(int b)
  {
    _last_recv_peer_bias = b;
  }
  inline int
  GetLastRecvPeerBias()
  {
    return _last_recv_peer_bias;
  }
  void CancelPendingReads();
  void DumpICPConfig();

private:
  // Class data declarations
  AtomicLock *_l;
  int _Initialized;
  int _AllowIcpQueries;
  int _PendingIcpQueries;
  ICPConfiguration *_ICPConfig;
  ICPPeriodicCont *_ICPPeriodic;
  ICPHandlerCont *_ICPHandler;
  ICPHandlerCont *_mcastCB_handler;
  Event *_PeriodicEvent;
  Event *_ICPHandlerEvent;

  enum {
    PEER_LIST_SIZE          = 2 * MAX_DEFINED_PEERS,
    SEND_PEER_LIST_SIZE     = 2 * MAX_DEFINED_PEERS,
    RECV_PEER_LIST_SIZE     = 2 * MAX_DEFINED_PEERS,
    PARENT_PEER_LIST_SIZE   = 2 * MAX_DEFINED_PEERS,
    PEER_ID_POLL_INDEX_SIZE = 2 * MAX_DEFINED_PEERS
  };

  // All Peer elements
  int _nPeerList; // valid PeerList[] entries - 1
  Ptr<Peer> _PeerList[PEER_LIST_SIZE];
  Ptr<Peer> _LocalPeer;

  // Peers which are targets of ICP queries
  int _curSendPeer;   // index bias for SendPeerList[]
  int _nSendPeerList; // valid SendPeerList[] entries - 1
  Ptr<Peer> _SendPeerList[SEND_PEER_LIST_SIZE];

  // List of Peers whom we issue reads from
  int _curRecvPeer;   // index bias for RecvPeerList[]
  int _nRecvPeerList; // valid RecvPeerList[] entries - 1
  Ptr<Peer> _RecvPeerList[RECV_PEER_LIST_SIZE];

  // Peers on SendPeerList which are "parent" peers
  int _curParentPeer;   // index bias for ParentPeerList[]
  int _nParentPeerList; // valid ParentPeerList[] entries - 1
  Ptr<Peer> _ParentPeerList[PARENT_PEER_LIST_SIZE];

  // Peer ID to Poll descriptor index map
  int _ValidPollData;
  int _PeerIDtoPollIndex[PEER_ID_POLL_INDEX_SIZE];
  int _last_recv_peer_bias; // bias used to build last poll data
};

//-----------------------------------------------------------------
// PeriodicCont -- Abstract base class for periodic ICP processor
//                 continuations
//-----------------------------------------------------------------
class PeriodicCont : public Continuation
{
public:
  PeriodicCont(ICPProcessor *p);
  virtual ~PeriodicCont();
  virtual int PeriodicEvent(int, Event *) = 0;

protected:
  ICPProcessor *_ICPpr;
};

//---------------------------------------------------------------
// ICPPeriodicCont -- ICPProcessor periodic event continuation.
//   Periodicly look for ICP configuration updates and if
//   updates exist schedule ICP reconfiguration.
//---------------------------------------------------------------
class ICPPeriodicCont : public PeriodicCont
{
public:
  enum {
    PERIODIC_INTERVAL = 5000,
  };
  enum {
    RETRY_INTERVAL_MSECS = 10,
  };
  ICPPeriodicCont(ICPProcessor *);
  ~ICPPeriodicCont() {}
  virtual int PeriodicEvent(int, Event *);
  int DoReconfigAction(int, Event *);

private:
  int _last_icp_config_callouts;
  int _global_config_changed;
  int _peer_config_changed;
};

//-----------------------------------------------------------------
// ICPHandlerCont -- Periodic for incoming message processing
//-----------------------------------------------------------------
class ICPHandlerCont : public PeriodicCont
{
public:
  enum {
    ICP_HANDLER_INTERVAL = 10,
  };
  ICPHandlerCont(ICPProcessor *);
  ~ICPHandlerCont() {}
  virtual int PeriodicEvent(int, Event *);
  virtual int TossEvent(int, Event *);

#ifdef DEBUG_ICP
// state history
#define MAX_ICP_HISTORY 20
  struct statehistory {
    int event;
    int newstate;
    char *file;
    int line;
  };
  statehistory _history[MAX_ICP_HISTORY];
  int _nhistory;

#define RECORD_ICP_STATE_CHANGE(peerreaddata, event_, newstate_)        \
  peerreaddata->_history[peerreaddata->_nhistory].event    = event_;    \
  peerreaddata->_history[peerreaddata->_nhistory].newstate = newstate_; \
  peerreaddata->_history[peerreaddata->_nhistory].file     = __FILE__;  \
  peerreaddata->_history[peerreaddata->_nhistory].line     = __LINE__;  \
  peerreaddata->_nhistory                                  = (peerreaddata->_nhistory + 1) % MAX_ICP_HISTORY;

#else
#define RECORD_ICP_STATE_CHANGE(x, y, z)
#endif

  static int64_t ICPDataBuf_IOBuffer_sizeindex;
};

//------------------------------------------------------------------
// ICPPeerReadCont -- ICP incoming message processing state machine
//------------------------------------------------------------------
class ICPPeerReadCont : public Continuation
{
public:
  typedef enum {
    READ_ACTIVE,
    READ_DATA,
    READ_DATA_DONE,
    PROCESS_READ_DATA,
    ADD_PEER,
    AWAITING_CACHE_LOOKUP_RESPONSE,
    SEND_REPLY,
    WRITE_DONE,
    GET_ICP_REQUEST,
    GET_ICP_REQUEST_MUTEX,
    READ_NOT_ACTIVE,
    READ_NOT_ACTIVE_EXIT,
    READ_PROCESSING_COMPLETE
  } PeerReadState_t;

  class PeerReadData
  {
  public:
    PeerReadData();
    void init();
    ~PeerReadData();
    void reset(int full_reset = 0);

    ink_hrtime _start_time;
    ICPPeerReadCont *_mycont;
    Ptr<Peer> _peer;
    PeerReadState_t _next_state;
    int _cache_lookup_local;
    Ptr<IOBufferBlock> _buf; // the buffer with the ICP message in it
    ICPMsg_t *_rICPmsg;
    int _rICPmsg_len;
    IpEndpoint _sender; // sender of rICPmsg
    URL _cachelookupURL;
    int _queryResult;
    ICPRequestCont *_ICPReqCont;
    int _bytesReceived;
    // response data
    struct msghdr _mhdr;
    struct iovec _iov[MSG_IOVECS];
  };

  ICPPeerReadCont();
  void init(ICPProcessor *, Peer *, int);
  ~ICPPeerReadCont();
  void reset(int full_reset = 0);
  int ICPPeerReadEvent(int, Event *);
  int ICPPeerQueryCont(int, Event *);
  int ICPPeerQueryEvent(int, Event *);
  int StaleCheck(int, Event *);
  int PeerReadStateMachine(PeerReadData *, Event *);

  enum {
    RETRY_INTERVAL = 10,
  };

  // Freshness specific data
  CacheVConnection *_object_vc;
  HTTPInfo *_object_read;
  HdrHeapSDKHandle *_cache_req_hdr_heap_handle;
  HdrHeapSDKHandle *_cache_resp_hdr_heap_handle;

private:
  // Class data
  ICPProcessor *_ICPpr;
  PeerReadData *_state;
  ink_hrtime _start_time;
  int _recursion_depth;
};

//----------------------------------------------------------------------
// ICPRequestCont -- ICP Request continuation  (Outgoing ICP requests)
//----------------------------------------------------------------------
class ICPRequestCont : public Continuation
{
  friend class ICPProcessor;

public:
  ICPRequestCont(ICPProcessor *i = 0, Continuation *c = 0, URL *u = 0);
  ~ICPRequestCont();
  void *operator new(size_t size, void *mem);
  void operator delete(void *mem);
  inline void
  SetRequestStartTime()
  {
    _start_time = Thread::get_hrtime();
  }
  inline ink_hrtime
  GetRequestStartTime()
  {
    return _start_time;
  }
  inline class Action *
  GetActionPtr()
  {
    return &_act;
  }

  enum {
    RETRY_INTERVAL = 10,
  };
  enum {
    ICP_REQUEST_HASH_SIZE = 1024,
  };

  //***********************************************************************
  // ICPPeerReadCont::PeerReadStateMachine() to
  // ICPRequestCont::ICPStateMachine() calling sequence definition.
  //
  //     ICPRequestEvent(ICP_RESPONSE_MESSAGE, ICPRequestEventArgs_t *)
  //
  //***********************************************************************
  typedef struct ICPRequestEventArgs {
    ICPMsg_t *rICPmsg;
    int rICPmsg_len;
    Peer *peer;
  } ICPRequestEventArgs_t;
  //***********************************************************************
  int ICPRequestEvent(int, Event *);
  int NopICPRequestEvent(int, Event *);

  // Static member functions
  static void NetToHostICPMsg(ICPMsg_t *, ICPMsg_t *);
  static int BuildICPMsg(ICPopcode_t op, unsigned int sequence_number, int optflags, int optdata, int shostid, void *data,
                         int datalen, struct msghdr *mhdr, struct iovec *iov, ICPMsg_t *icpmsg);

  static unsigned int ICPReqSeqNumber();
  static int ICPRequestHash(unsigned int);
  static int AddICPRequest(unsigned int, ICPRequestCont *);
  static ICPRequestCont *FindICPRequest(unsigned int);
  static int RemoveICPRequest(unsigned int);

private:
  typedef enum {
    ICP_START,
    ICP_OFF_TERMINATE,
    ICP_QUEUE_REQUEST,
    ICP_AWAITING_RESPONSE,
    ICP_DEQUEUE_REQUEST,
    ICP_POST_COMPLETION,
    ICP_WAIT_SEND_COMPLETE,
    ICP_REQUEST_NOT_ACTIVE,
    ICP_DONE
  } ICPstate_t;
  int ICPStateMachine(int, void *);
  int ICPResponseMessage(int, ICPMsg_t *, Peer *);
  void remove_from_pendingActions(Action *);
  void remove_all_pendingActions();

  // Static data
  static uint32_t ICPRequestSeqno;

  // Passed request data
  Continuation *_cont;
  URL *_url;

  // Return data
  IpEndpoint _ret_sockaddr;
  ICPreturn_t _ret_status;
  class Action _act;

  // Internal working data
  ink_hrtime _start_time;
  ICPProcessor *_ICPpr;
  Event *_timeout;

  // outstanding actions
  int npending_actions;
  DynArray<Action *> *pendingActions;

  ICPMsg_t _ICPmsg;
  struct msghdr _sendMsgHdr;
  struct iovec _sendMsgIOV[MSG_IOVECS];

  unsigned int _sequence_number;
  int _expected_replies;
  BitMap _expected_replies_list;
  int _received_replies;
  ICPstate_t _next_state;
};

extern ClassAllocator<ICPRequestCont> ICPRequestCont_allocator;

typedef int (*PluginFreshnessCalcFunc)(void *contp);
extern PluginFreshnessCalcFunc pluginFreshnessCalcFunc;

inline void *
ICPRequestCont::operator new(size_t /* size ATS_UNUSED */, void *mem)
{
  return mem;
}

inline void
ICPRequestCont::operator delete(void *mem)
{
  ICPRequestCont_allocator.free((ICPRequestCont *)mem);
}

extern struct RecRawStatBlock *icp_rsb;

enum {
  icp_stat_def,
  config_mgmt_callouts_stat,
  reconfig_polls_stat,
  reconfig_events_stat,
  invalid_poll_data_stat,
  no_data_read_stat,
  short_read_stat,
  invalid_sender_stat,
  read_not_v2_icp_stat,
  icp_remote_query_requests_stat,
  icp_remote_responses_stat,
  icp_cache_lookup_success_stat,
  icp_cache_lookup_fail_stat,
  query_response_write_stat,
  query_response_partial_write_stat,
  no_icp_request_for_response_stat,
  icp_response_request_nolock_stat,
  icp_start_icpoff_stat,
  send_query_partial_write_stat,
  icp_queries_no_expected_replies_stat,
  icp_query_hits_stat,
  icp_query_misses_stat,
  invalid_icp_query_response_stat,
  icp_query_requests_stat,
  total_icp_response_time_stat,
  total_udp_send_queries_stat,
  total_icp_request_time_stat,
  icp_total_reloads,
  icp_pending_reloads,
  icp_reload_start_aborts,
  icp_reload_connect_aborts,
  icp_reload_read_aborts,
  icp_reload_write_aborts,
  icp_reload_successes,
  icp_stat_count
};

#define ICP_EstablishStaticConfigInteger(_ix, _n) REC_EstablishStaticConfigInt32(_ix, _n)

#define ICP_EstablishStaticConfigStringAlloc(_ix, n) REC_EstablishStaticConfigStringAlloc(_ix, n)

#define ICP_INCREMENT_DYN_STAT(x) RecIncrRawStat(icp_rsb, mutex->thread_holding, (int)x, 1)
#define ICP_DECREMENT_DYN_STAT(x) RecIncrRawStat(icp_rsb, mutex->thread_holding, (int)x, -1)
#define ICP_SUM_DYN_STAT(x, y) RecIncrRawStat(icp_rsb, mutex->thread_holding, (int)x, (y))
#define ICP_READ_DYN_STAT(x, C, S)         \
  RecGetRawStatCount(icp_rsb, (int)x, &C); \
  RecGetRawStatSum(icp_rsb, (int)x, &S);

#define ICP_ReadConfigString REC_ReadConfigString
#define ICP_RegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc

// End of ICP.h

#endif // _ICP_H_
