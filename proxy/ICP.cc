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

  ICP.cc


****************************************************************************/

#include "ts/ink_platform.h"
#include "Main.h"
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "P_Net.h"
#include "MgmtUtils.h"
#include "P_RecProcess.h"
#include "ICP.h"
#include "ICPProcessor.h"
#include "ICPlog.h"
#include "logging/Log.h"
#include "logging/LogAccessICP.h"
#include "BaseManager.h"
#include "HdrUtils.h"

extern CacheLookupHttpConfig global_cache_lookup_config;
HTTPHdr gclient_request;

//****************************************************************************
//  File Overview:
//  ==============
//      ICP files
//        ICP.h           -- All ICP class definitions.
//        ICPlog.h        -- ICP log object for logging system
//        ICP.cc          -- Incoming/outgoing ICP request and ICP configuration
//                           data base management.
//        ICPConfig.cc    -- ICP interface to Traffic Server configuration
//                           management, member functions for ICPlog (object
//                           passed to logging system) along with
//                           miscellaneous support routines.
//        ICPevents.h     -- Event definitions specific to ICP.
//        ICPProcessor.h  -- ICP external interface for other subsystems.
//                           External subsystems only need to include this
//                           header to use ICP.
//        ICPProcessor.cc -- ICP external interface implementation.
//        ICPStats.cc     -- ICP statistic callback registration.
//
//
//  Class Overview:
//  ===============
//    ICPConfigData  -- Manages global ICP data from the TS configuration
//                      manager.
//    PeerConfigData -- Manages  ICP peer data from the TS configuration
//                      manager.
//    ICPConfigUpdateCont -- Used by
//                        ICPConfiguration::icp_config_change_callback()
//                        to retry callout after a delay in cases where
//                        we cannot acquire the configuration lock.
//    ICPConfiguration -- Overall manager of ICP configuration from TS
//                        configuration.  Acts as interface and uses
//                        ICPConfigData and PeerConfigData to implement
//                        actions.  Also fields/processes TS configuration
//                        callouts for "icp.config" changes.  ICP classes only
//                        see ICPConfiguration when dealing with TS
//                        configuration info.
//
//    Peer (base class) -- abstract base class
//      ParentSiblingPeer : Peer  -- ICP object describing parent/sibling
//                                   peer which is initialized from the
//                                   TS configuration data.
//      MultiCastPeer : Peer -- ICP object describing MultiCast peer.
//                              Object is initialized from the TS
//                              configuration data.
//
//    BitMap -- Generic bit map management class
//
//    ICPProcessor -- Central class which starts all periodic events
//                 and maintains ICP configuration database.  Delegates
//                 incoming data processing to ICPHandlerCont and
//                 outgoing data processing to ICPRequestCont. Implements
//                 reconfiguration actions and query requests from the
//                 external interface.
//
//    ICPRequestCont -- Implements the state machine which processes
//                 locally generated ICP queries.  Generates message
//                 queries and processes query responses.  Responses
//                 received via callout from ICPPeerReadCont.
//
//    PeriodicCont (base class) -- abstract base class
//      ICPPeriodicCont : PeriodicCont -- Periodic which looks for ICP
//                 configuration changes sent by the Traffic Server
//                 configuration manager, and initiates ICP reconfiguration
//                 in the event we have a valid configuration change via
//                 ICPProcessor::ReconfigureStateMachine().
//
//      ICPHandlerCont : PeriodicCont -- Periodic which monitors incoming
//                 ICP sockets and starts processing of the incoming ICP data.
//
//    ICPPeerReadCont -- Implements the incoming data state machine.
//                 Processes remote ICP query requests and passes query
//                 responses to ICPRequestCont via a callout.
//    ICPlog -- Logging object which encapsulates ICP query info required
//              by the new logging subsystem to produce squid access log
//              data for ICP queries.
//
//****************************************************************************
//
//  ICP is integrated into HTTP miss processing as follows.
//
//  if (HTTP Traffic Server Miss) {
//    if (proxy.config.icp.enabled) {
//      Status = QueryICP(URL, &target_ip);
//      if (Status == ICP_HIT)
//        Issue Http Request to (target_ip, proxy_port);
//    }
//    if (proxy.config.http.parent_proxy_routing_enable) {
//      Issue Http Request to (proxy.config.http.parent_proxy_hostname,
//                             proxy.config.http.parent_proxy_port)
//    }
//    else
//      Issue Http Request to Origin Server
//  }
//
//****************************************************************************

// VC++ 5.0 is rather picky
using ICPPeerReadContHandler = int (ICPPeerReadCont::*)(int, void *);
using ICPPeriodicContHandler = int (ICPPeriodicCont::*)(int, void *);
using ICPHandlerContHandler  = int (ICPHandlerCont::*)(int, void *);
using ICPRequestContHandler  = int (ICPRequestCont::*)(int, void *);

// Plugin freshness function
PluginFreshnessCalcFunc pluginFreshnessCalcFunc = (PluginFreshnessCalcFunc) nullptr;

//---------------------------------------
// Class ICPHandlerCont member functions
//      Deal with incoming ICP data
//---------------------------------------

// Static data declarations
// Allocator *ICPHandlerCont::IncomingICPDataBuf;
int64_t ICPHandlerCont::ICPDataBuf_IOBuffer_sizeindex;
static ClassAllocator<ICPPeerReadCont::PeerReadData> PeerReadDataAllocator("PeerReadDataAllocator");
static ClassAllocator<ICPPeerReadCont> ICPPeerReadContAllocator("ICPPeerReadContAllocator");

static Action *default_action = nullptr;

ICPHandlerCont::ICPHandlerCont(ICPProcessor *icpP) : PeriodicCont(icpP)
{
}

// do nothing continuation handler
int
ICPHandlerCont::TossEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  return EVENT_DONE;
}

int
ICPHandlerCont::PeriodicEvent(int event, Event * /* e ATS_UNUSED */)
{
  int n_peer, valid_peers;
  Peer *P;

  // Periodic handler which initiates incoming message processing
  // on the defined peers.

  valid_peers = _ICPpr->GetRecvPeers();

  // get peer info from the completionEvent token.
  switch (event) {
  case EVENT_POLL:
  case EVENT_INTERVAL: {
    // start read I/Os on peers which don't have outstanding I/Os
    for (n_peer = 0; n_peer < valid_peers; ++n_peer) {
      P = _ICPpr->GetNthRecvPeer(n_peer, _ICPpr->GetLastRecvPeerBias());
      if (!P || (P && !P->IsOnline())) {
        continue;
      }
      if (P->shouldStartRead()) {
        P->startingRead();
        ///////////////////////////////////////////
        // Setup state machine
        ///////////////////////////////////////////
        ICPPeerReadCont *s = ICPPeerReadContAllocator.alloc();
        int local_lookup   = _ICPpr->GetConfig()->globalConfig()->ICPLocalCacheLookup();

        s->init(_ICPpr, P, local_lookup);
        RECORD_ICP_STATE_CHANGE(s, event, ICPPeerReadCont::READ_ACTIVE);

        ///////////////////////////////////////////
        // Start processing
        ///////////////////////////////////////////
        s->handleEvent(EVENT_INTERVAL, (Event *)nullptr);
      }
    }
    break;
  }
  default: {
    ink_release_assert(!"unexpected event");
    break;
  }
  } // End of switch
  return EVENT_CONT;
}

//***************************************************************************
// Nested Class PeerReadData member functions
//      Used by ICPPeerReadCont to encapsulate the data required by
//      PeerReadStateMachine
//***************************************************************************
ICPPeerReadCont::PeerReadData::PeerReadData()
{
  init();
}

void
ICPPeerReadCont::PeerReadData::init()
{
  _start_time         = 0;
  _mycont             = nullptr;
  _peer               = nullptr;
  _next_state         = READ_ACTIVE;
  _cache_lookup_local = 0;
  _buf                = nullptr;
  _rICPmsg            = nullptr;
  _rICPmsg_len        = 0;
  _cachelookupURL.clear();
  _queryResult   = 0;
  _ICPReqCont    = nullptr;
  _bytesReceived = 0;
#ifdef DEBUG_ICP
  _nhistory = 0;
#endif
  memset((void *)&_sender, 0, sizeof(_sender));
}

ICPPeerReadCont::PeerReadData::~PeerReadData()
{
  reset(1);
}

void
ICPPeerReadCont::PeerReadData::reset(int full_reset)
{
  if (full_reset) {
    _peer = nullptr;
    _buf  = nullptr;
  }
  if (_rICPmsg) {
    _rICPmsg     = nullptr;
    _rICPmsg_len = 0;
  }

  if (_cachelookupURL.valid()) {
    _cachelookupURL.destroy();
  }
}

//***************************************************************************

//------------------------------------------------------------------------
// ICPPeerReadCont -- ICP incoming message processing state machine
//------------------------------------------------------------------------
ICPPeerReadCont::ICPPeerReadCont()
  : Continuation(nullptr),
    _object_vc(nullptr),
    _object_read(nullptr),
    _cache_req_hdr_heap_handle(nullptr),
    _cache_resp_hdr_heap_handle(nullptr),
    _ICPpr(nullptr),
    _state(nullptr),
    _start_time(0),
    _recursion_depth(0)
{
}

void
ICPPeerReadCont::init(ICPProcessor *ICPpr, Peer *p, int lookup_local)
{
  PeerReadData *s = PeerReadDataAllocator.alloc();
  s->init();
  s->_start_time         = Thread::get_hrtime();
  s->_peer               = p;
  s->_next_state         = READ_ACTIVE;
  s->_cache_lookup_local = lookup_local;
  SET_HANDLER((ICPPeerReadContHandler)&ICPPeerReadCont::ICPPeerReadEvent);
  _ICPpr                      = ICPpr;
  _state                      = s;
  _recursion_depth            = -1;
  _object_vc                  = nullptr;
  _object_read                = nullptr;
  _cache_req_hdr_heap_handle  = nullptr;
  _cache_resp_hdr_heap_handle = nullptr;
  mutex                       = new_ProxyMutex();
}

ICPPeerReadCont::~ICPPeerReadCont()
{
  reset(1); // Full reset
}

void
ICPPeerReadCont::reset(int full_reset)
{
  mutex = nullptr;
  if (this->_state) {
    this->_state->reset(full_reset);
    PeerReadDataAllocator.free(this->_state);
  }
  if (_cache_req_hdr_heap_handle) {
    ats_free(_cache_req_hdr_heap_handle);
    _cache_req_hdr_heap_handle = nullptr;
  }
  if (_cache_resp_hdr_heap_handle) {
    ats_free(_cache_resp_hdr_heap_handle);
    _cache_resp_hdr_heap_handle = nullptr;
  }
}

int
ICPPeerReadCont::ICPPeerReadEvent(int event, Event *e)
{
  switch (event) {
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE: {
    break;
  }
  case NET_EVENT_DATAGRAM_WRITE_COMPLETE:
  case NET_EVENT_DATAGRAM_READ_COMPLETE:
  case NET_EVENT_DATAGRAM_READ_ERROR:
  case NET_EVENT_DATAGRAM_WRITE_ERROR: {
    ink_assert((event != NET_EVENT_DATAGRAM_READ_COMPLETE) || (_state->_next_state == READ_DATA_DONE));
    ink_assert((event != NET_EVENT_DATAGRAM_WRITE_COMPLETE) || (_state->_next_state == WRITE_DONE));

    ink_release_assert(this == (ICPPeerReadCont *)completionUtil::getHandle(e));
    break;
  }
  case CACHE_EVENT_LOOKUP_FAILED:
  case CACHE_EVENT_LOOKUP: {
    ink_assert(_state->_next_state == AWAITING_CACHE_LOOKUP_RESPONSE);
    break;
  }
  default: {
    ink_release_assert(!"unexpected event");
  }
  } // End of switch

  // Front end to PeerReadStateMachine(), invoked by Event subsystem.
  if (PeerReadStateMachine(_state, e) == EVENT_CONT) {
    eventProcessor.schedule_in(this, RETRY_INTERVAL, ET_ICP);
    return EVENT_DONE;

  } else if (_state->_next_state == READ_PROCESSING_COMPLETE) {
    _state->_peer->cancelRead();
    this->reset(1); // Full reset
    ICPPeerReadContAllocator.free(this);
    return EVENT_DONE;

  } else {
    return EVENT_DONE;
  }
}

int
ICPPeerReadCont::StaleCheck(int event, Event * /* e ATS_UNUSED */)
{
  ip_port_text_buffer ipb;

  ink_release_assert(mutex->thread_holding == this_ethread());

  Debug("icp-stale", "Stale check res=%d for id=%d, [%s] from [%s]", event, _state->_rICPmsg->h.requestno,
        _state->_rICPmsg->un.query.URL, ats_ip_nptop(&_state->_sender, ipb, sizeof(ipb)));

  switch (event) {
  case ICP_STALE_OBJECT: {
    _state->_queryResult = CACHE_EVENT_LOOKUP_FAILED;
    break;
  }
  case ICP_FRESH_OBJECT: {
    _state->_queryResult = CACHE_EVENT_LOOKUP;
    break;
  }
  default: {
    Debug("icp-stale", "ICPPeerReadCont::StaleCheck: Invalid Event %d", event);
    _state->_queryResult = CACHE_EVENT_LOOKUP_FAILED;
    break;
  }
  }
  _object_vc->do_io(VIO::CLOSE);
  _object_vc = nullptr;
  SET_HANDLER((ICPPeerReadContHandler)&ICPPeerReadCont::ICPPeerReadEvent);
  return handleEvent(_state->_queryResult, nullptr);
}

int
ICPPeerReadCont::ICPPeerQueryEvent(int event, Event *e)
{
  ip_port_text_buffer ipb;

  Debug("icp", "Remote Query lookup res=%d for id=%d, [%s] from [%s]", event, _state->_rICPmsg->h.requestno,
        _state->_rICPmsg->un.query.URL, ats_ip_nptop(&_state->_sender, ipb, sizeof(ipb)));
  if (pluginFreshnessCalcFunc) {
    switch (event) {
    case CACHE_EVENT_OPEN_READ: {
      _object_vc = (CacheVConnection *)e;
      SET_HANDLER((ICPPeerReadContHandler)&ICPPeerReadCont::StaleCheck);
      _object_vc->get_http_info(&_object_read);
      (*pluginFreshnessCalcFunc)((void *)this);
      return EVENT_DONE;
    }
    case CACHE_EVENT_OPEN_READ_FAILED: {
      event = CACHE_EVENT_LOOKUP_FAILED;
      break;
    }
    default:
      break;
    }
  }
  // Process result
  _state->_queryResult = event;
  SET_HANDLER((ICPPeerReadContHandler)&ICPPeerReadCont::ICPPeerReadEvent);
  return handleEvent(event, e);
}

int
ICPPeerReadCont::ICPPeerQueryCont(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  ip_port_text_buffer ipb;
  Action *a;

  // Perform lookup()/open_read() on behalf of PeerReadStateMachine()

  ((char *)_state->_rICPmsg)[MAX_ICP_MSGSIZE - 1] = 0; // null terminate
  _state->_cachelookupURL.create(nullptr);
  const char *qurl = (const char *)_state->_rICPmsg->un.query.URL;
  _state->_cachelookupURL.parse(qurl, strlen(qurl));
  Debug("icp", "Remote Query for id=%d, [%s] from [%s]", _state->_rICPmsg->h.requestno, _state->_rICPmsg->un.query.URL,
        ats_ip_nptop(&_state->_sender, ipb, sizeof(ipb)));

  SET_HANDLER((ICPPeerReadContHandler)&ICPPeerReadCont::ICPPeerQueryEvent);
  if (_state->_rICPmsg->un.query.URL && *_state->_rICPmsg->un.query.URL) {
    HttpCacheKey key;
    ICPConfigData *cfg = _ICPpr->GetConfig()->globalConfig();

    Cache::generate_key(&key, &_state->_cachelookupURL, cfg->ICPCacheGeneration());
    _state->_queryResult = ~CACHE_EVENT_LOOKUP_FAILED;
    _start_time          = Thread::get_hrtime();

    if (pluginFreshnessCalcFunc && cfg->ICPStaleLookup()) {
      //////////////////////////////////////////////////////////////
      // Note: _cache_lookup_local is ignored in this case, since
      //       cache clustering is not used with stale lookup.
      //////////////////////////////////////////////////////////////
      a = cacheProcessor.open_read(this, &key, false, &gclient_request, &global_cache_lookup_config, (time_t)0);
    } else {
      a = cacheProcessor.lookup(this, &key, false, _state->_cache_lookup_local);
    }
    if (!a) {
      a = ACTION_IO_ERROR;
    }
    if (a == ACTION_RESULT_DONE) {
      return EVENT_DONE; // callback complete
    } else if (a == ACTION_IO_ERROR) {
      handleEvent(CACHE_EVENT_LOOKUP_FAILED, nullptr);
      return EVENT_DONE; // callback complete
    } else {
      return EVENT_CONT; // callback pending
    }
  } else {
    // Null URL, return failed lookup
    handleEvent(CACHE_EVENT_LOOKUP_FAILED, nullptr);
    return EVENT_DONE; // callback done
  }
}

struct AutoReference {
  AutoReference(int *cnt)
  {
    _cnt = cnt;
    (*_cnt)++;
  }
  ~AutoReference() { (*_cnt)--; }
  int *_cnt;
};

int
ICPPeerReadCont::PeerReadStateMachine(PeerReadData *s, Event *e)
{
  AutoReference l(&_recursion_depth);
  ip_port_text_buffer ipb; // scratch buffer for diagnostic messages.
  //-----------------------------------------------------------
  // State machine to process ICP data received on UDP socket
  //-----------------------------------------------------------
  MUTEX_TRY_LOCK(lock, this->mutex, this_ethread());
  if (!lock.is_locked()) {
    // we didn't get the lock, so we don't need to unlock it
    // coverity[missing_unlock]
    return EVENT_CONT; // try again later
  }

  while (true) { // loop forever

    switch (s->_next_state) {
    case READ_ACTIVE: {
      ink_release_assert(_recursion_depth == 0);
      if (!_ICPpr->Lock()) {
        return EVENT_CONT; // unable to get lock, try again later
      }

      bool valid_peer = (_ICPpr->IdToPeer(s->_peer->GetPeerID()) == s->_peer.get());

      if (valid_peer && _ICPpr->AllowICPQueries() && _ICPpr->GetConfig()->globalConfig()->ICPconfigured()) {
        // Note pending incoming ICP request or response
        _ICPpr->IncPendingQuery();
        _ICPpr->Unlock();

        s->_next_state = READ_DATA;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_DATA);
        break; // move to next_state

      } else {
        _ICPpr->Unlock();

        // ICP NOT enabled, do nothing
        s->_next_state = READ_PROCESSING_COMPLETE;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_PROCESSING_COMPLETE);
        return EVENT_DONE;
      }
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case READ_DATA: {
      ink_release_assert(_recursion_depth == 0);

      // Assumption of one outstanding read per peer...
      // Setup read from FD
      ink_assert(!s->_peer->buf);
      Ptr<IOBufferBlock> buf = s->_peer->buf = new_IOBufferBlock();
      buf->alloc(ICPHandlerCont::ICPDataBuf_IOBuffer_sizeindex);
      s->_peer->fromaddrlen = sizeof(s->_peer->fromaddr);
      buf->fill(sizeof(ICPMsg_t)); // reserve space for decoding
      char *be       = buf->buf_end() - 1;
      be[0]          = 0; // null terminate buffer
      s->_next_state = READ_DATA_DONE;
      RECORD_ICP_STATE_CHANGE(s, 0, READ_DATA_DONE);
      ink_assert(s->_peer->readAction == nullptr);
      Action *a =
        s->_peer->RecvFrom_re(this, this, buf.get(), buf->write_avail() - 1, &s->_peer->fromaddr.sa, &s->_peer->fromaddrlen);
      if (!a) {
        a = ACTION_IO_ERROR;
      }
      if (a == ACTION_RESULT_DONE) {
        // we will have been called back already and our state updated
        // appropriately.
        // move to next state
        ink_assert(s->_next_state == PROCESS_READ_DATA);
        break;
      } else if (a == ACTION_IO_ERROR) {
        // actually, this *could* be taken care of by the main handler, but
        // error processing makes more sense at this point.  Therefore,
        // the main handler ignores the errors.
        //
        // No data, terminate read loop.
        //
        ICP_INCREMENT_DYN_STAT(no_data_read_stat);
        s->_peer->buf  = nullptr; // release reference
        s->_next_state = READ_NOT_ACTIVE_EXIT;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE_EXIT);
        // move to next state
        break;
      } else {
        s->_peer->readAction = a;
        return EVENT_DONE;
      }
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case READ_DATA_DONE: {
      // Convert ICP message from network to host format
      if (s->_peer->readAction != nullptr) {
        ink_assert(s->_peer->readAction == e);
        s->_peer->readAction = nullptr;
      }
      s->_bytesReceived = completionUtil::getBytesTransferred(e);

      if (s->_bytesReceived >= 0) {
        s->_next_state = PROCESS_READ_DATA;
        RECORD_ICP_STATE_CHANGE(s, 0, PROCESS_READ_DATA);
      } else {
        ICP_INCREMENT_DYN_STAT(no_data_read_stat);
        s->_peer->buf  = nullptr; // release reference
        s->_next_state = READ_NOT_ACTIVE_EXIT;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE_EXIT);
      }
      if (_recursion_depth > 0) {
        return EVENT_DONE;
      } else {
        break;
      }
    }
      ink_release_assert(0); // Should never happen

    // fallthrough
    case PROCESS_READ_DATA:
    case ADD_PEER: {
      ink_release_assert(_recursion_depth == 0);

      Ptr<IOBufferBlock> bufblock = s->_peer->buf;
      char *buf                   = bufblock->start();

      if (s->_next_state == PROCESS_READ_DATA) {
        ICPRequestCont::NetToHostICPMsg((ICPMsg_t *)(buf + sizeof(ICPMsg_t)), (ICPMsg_t *)buf);

        // adjust buffer pointers to point to decoded message.
        bufblock->reset();
        bufblock->fill(s->_bytesReceived);

        // Validate message length for sanity
        if (s->_bytesReceived < ((ICPMsg_t *)buf)->h.msglen) {
          //
          // Short read, terminate
          //
          ICP_INCREMENT_DYN_STAT(short_read_stat);
          s->_peer->buf  = nullptr;
          s->_next_state = READ_NOT_ACTIVE;
          RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
          break; // move to next_state
        }
      }
      // Validate receiver and convert the received sockaddr
      //   to internal sockaddr format.
      IpEndpoint from;
      if (!s->_peer->ExtToIntRecvSockAddr(&s->_peer->fromaddr.sa, &from.sa)) {
        int status;
        ICPConfigData *cfg = _ICPpr->GetConfig()->globalConfig();
        ICPMsg_t *ICPmsg   = (ICPMsg_t *)buf;

        if ((cfg->ICPconfigured() == ICP_MODE_RECEIVE_ONLY) && cfg->ICPReplyToUnknownPeer() &&
            ((ICPmsg->h.version == ICP_VERSION_2) || (ICPmsg->h.version == ICP_VERSION_3)) && (ICPmsg->h.opcode == ICP_OP_QUERY)) {
          //
          // Add the unknown Peer to our database to
          // allow us to resolve the lookup request.
          //
          if (!_ICPpr->GetConfig()->Lock()) {
            s->_next_state = ADD_PEER;
            RECORD_ICP_STATE_CHANGE(s, 0, ADD_PEER);
            return EVENT_CONT;
          }
          if (!_ICPpr->GetFreePeers() || !_ICPpr->GetFreeSendPeers()) {
            RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP Peer limit exceeded");
            _ICPpr->GetConfig()->Unlock();
            goto invalid_message;
          }

          int icp_reply_port = cfg->ICPDefaultReplyPort();
          if (!icp_reply_port) {
            icp_reply_port = ntohs(ats_ip_port_cast(&s->_peer->fromaddr));
          }
          PeerConfigData *Pcfg = new PeerConfigData(PeerConfigData::CTYPE_SIBLING, IpAddr(s->_peer->fromaddr), 0, icp_reply_port);
          ParentSiblingPeer *P = new ParentSiblingPeer(PEER_SIBLING, Pcfg, _ICPpr, true);
          status               = _ICPpr->AddPeer(P);
          ink_release_assert(status);
          status = _ICPpr->AddPeerToSendList(P);
          ink_release_assert(status);

          P->GetChan()->setRemote(P->GetIP());

          // coverity[uninit_use_in_call]
          Note("ICP Peer added ip=%s", ats_ip_nptop(P->GetIP(), ipb, sizeof(ipb)));
          from = s->_peer->fromaddr;
        } else {
        invalid_message:
          //
          // Sender does not exist in ICP configuration, terminate
          //
          ICP_INCREMENT_DYN_STAT(invalid_sender_stat);
          Debug("icp", "Received msg from invalid sender [%s]", ats_ip_nptop(&s->_peer->fromaddr, ipb, sizeof(ipb)));

          s->_peer->buf  = nullptr;
          s->_next_state = READ_NOT_ACTIVE;
          RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
          break; // move to next_state
        }
      }
      // we hand off the decoded buffer from the Peer to the PeerReadData
      s->_sender      = from;
      s->_rICPmsg_len = s->_bytesReceived;
      ink_assert(!s->_buf);
      s->_buf       = s->_peer->buf;
      s->_rICPmsg   = (ICPMsg_t *)s->_buf->start();
      s->_peer->buf = nullptr;

      //
      // Handle only ICP_VERSION_2/3 messages.  Reject all others.
      //
      if ((s->_rICPmsg->h.version != ICP_VERSION_2) && (s->_rICPmsg->h.version != ICP_VERSION_3)) {
        ICP_INCREMENT_DYN_STAT(read_not_v2_icp_stat);
        Debug("icp", "Received (v=%d) !v2 && !v3 msg from sender [%s]", (uint32_t)s->_rICPmsg->h.version,
              ats_ip_nptop(&from, ipb, sizeof(ipb)));

        s->_rICPmsg    = nullptr;
        s->_buf        = nullptr;
        s->_next_state = READ_NOT_ACTIVE;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
        break; // move to next_state
      }
      //
      // If this is a query message, redirect to
      // the query specific handlers.
      //
      if (s->_rICPmsg->h.opcode == ICP_OP_QUERY) {
        ICP_INCREMENT_DYN_STAT(icp_remote_query_requests_stat);
        ink_assert(!s->_mycont);
        s->_next_state = AWAITING_CACHE_LOOKUP_RESPONSE;
        RECORD_ICP_STATE_CHANGE(s, 0, AWAITING_CACHE_LOOKUP_RESPONSE);

        if (ICPPeerQueryCont(0, (Event *)nullptr) == EVENT_DONE) {
          break; // Callback complete
        } else {
          return EVENT_DONE; // Callback pending
        }
      } else {
        // We have a response message for an ICP query.
        Debug("icp", "Response for Id=%d, from [%s]", s->_rICPmsg->h.requestno, ats_ip_nptop(&s->_sender, ipb, sizeof(ipb)));
        ICP_INCREMENT_DYN_STAT(icp_remote_responses_stat);
        s->_next_state = GET_ICP_REQUEST;
        RECORD_ICP_STATE_CHANGE(s, 0, GET_ICP_REQUEST);
        break; // move to next_state
      }
    }
      ink_release_assert(0); // Should never happen

    // fallthrough
    case AWAITING_CACHE_LOOKUP_RESPONSE: {
      int status  = 0;
      void *data  = s->_rICPmsg->un.query.URL;
      int datalen = strlen((const char *)data) + 1;

      if (s->_queryResult == CACHE_EVENT_LOOKUP) {
        // Use the received ICP data buffer for the response message
        Debug("icp", "Sending ICP_OP_HIT for id=%d, [%.*s] to [%s]", s->_rICPmsg->h.requestno, datalen, (const char *)data,
              ats_ip_nptop(&s->_sender, ipb, sizeof(ipb)));
        ICP_INCREMENT_DYN_STAT(icp_cache_lookup_success_stat);
        status = ICPRequestCont::BuildICPMsg(ICP_OP_HIT, s->_rICPmsg->h.requestno, 0 /* optflags */, 0 /* optdata */,
                                             0 /* shostid */, data, datalen, &s->_mhdr, s->_iov, s->_rICPmsg);
      } else if (s->_queryResult == CACHE_EVENT_LOOKUP_FAILED) {
        // Use the received ICP data buffer for response message
        Debug("icp", "Sending ICP_OP_MISS for id=%d, [%.*s] to [%s]", s->_rICPmsg->h.requestno, datalen, (const char *)data,
              ats_ip_nptop(&s->_sender, ipb, sizeof(ipb)));
        ICP_INCREMENT_DYN_STAT(icp_cache_lookup_fail_stat);
        status = ICPRequestCont::BuildICPMsg(ICP_OP_MISS, s->_rICPmsg->h.requestno, 0 /* optflags */, 0 /* optdata */,
                                             0 /* shostid */, data, datalen, &s->_mhdr, s->_iov, s->_rICPmsg);
      } else {
        Warning("Bad cache lookup event: %d", s->_queryResult);
        ink_release_assert(!"Invalid cache lookup event");
      }
      ink_assert(status == 0);

      // Make system log entry for ICP query
      ICPlog logentry(s);
      LogAccessICP accessor(&logentry);
      Log::access(&accessor);

      s->_next_state = SEND_REPLY;
      RECORD_ICP_STATE_CHANGE(s, 0, SEND_REPLY);

      if (_recursion_depth > 0) {
        return EVENT_DONE;
      } else {
        break;
      }
    }
      ink_release_assert(0); // Should never happen

    // fallthrough
    case SEND_REPLY: {
      ink_release_assert(_recursion_depth == 0);
      //
      // Send the query response back to the sender
      //
      s->_next_state = WRITE_DONE;
      RECORD_ICP_STATE_CHANGE(s, 0, WRITE_DONE);
      ink_assert(s->_peer->writeAction == nullptr);
      Action *a = s->_peer->SendMsg_re(this, this, &s->_mhdr, &s->_sender.sa);
      if (!a) {
        a = ACTION_IO_ERROR;
      }
      if (a == ACTION_RESULT_DONE) {
        // we have been called back already and our state updated
        // appropriately
        break;

      } else if (a == ACTION_IO_ERROR) {
        // Partial write.
        ICP_INCREMENT_DYN_STAT(query_response_partial_write_stat);
        // coverity[uninit_use_in_call]
        Debug("icp_warn", "ICP response send, sent=%d res=%d, ip=%s", ntohs(s->_rICPmsg->h.msglen), -1,
              ats_ip_ntop(&s->_sender, ipb, sizeof(ipb)));
        s->_next_state = READ_NOT_ACTIVE;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
        break;
      } else {
        s->_peer->writeAction = a;
        return EVENT_DONE;
      }
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case WRITE_DONE: {
      s->_peer->writeAction = nullptr;
      int len               = completionUtil::getBytesTransferred(e);

      if (len == (int)ntohs(s->_rICPmsg->h.msglen)) {
        ICP_INCREMENT_DYN_STAT(query_response_write_stat);
        s->_peer->LogSendMsg(s->_rICPmsg, &s->_sender.sa); // log query reply
      } else {
        // Partial write.
        ICP_INCREMENT_DYN_STAT(query_response_partial_write_stat);
        // coverity[uninit_use_in_call]
        Debug("icp_warn", "ICP response send, sent=%d res=%d, ip=%s", ntohs(s->_rICPmsg->h.msglen), len,
              ats_ip_ntop(&s->_sender, ipb, sizeof(ipb)));
      }
      // Processing complete, perform completion actions
      s->_next_state = READ_NOT_ACTIVE;
      RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
      Debug("icp", "state->READ_NOT_ACTIVE");

      if (_recursion_depth > 0) {
        return EVENT_DONE;
      } else {
        break; // move to next_state
      }
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case GET_ICP_REQUEST: {
      ink_release_assert(_recursion_depth == 0);
      ink_assert(s->_rICPmsg && s->_rICPmsg_len); // Sanity check

      // Get ICP request associated with response message
      s->_ICPReqCont = ICPRequestCont::FindICPRequest(s->_rICPmsg->h.requestno);
      if (s->_ICPReqCont) {
        s->_next_state = GET_ICP_REQUEST_MUTEX;
        RECORD_ICP_STATE_CHANGE(s, 0, GET_ICP_REQUEST_MUTEX);
        break; // move to next_state
      }
      //
      // No ICP request for response message, log as "response
      // for non-existent ICP request" and terminate processing
      //
      Debug("icp", "No ICP Request for Id=%d", s->_rICPmsg->h.requestno);
      ICP_INCREMENT_DYN_STAT(no_icp_request_for_response_stat);
      Peer *p = _ICPpr->FindPeer(s->_sender);
      p->LogRecvMsg(s->_rICPmsg, 0);
      s->_next_state = READ_NOT_ACTIVE;
      RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
      break; // move to next_state
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case GET_ICP_REQUEST_MUTEX: {
      ink_release_assert(_recursion_depth == 0);
      ink_assert(s->_ICPReqCont);
      Ptr<ProxyMutex> ICPReqContMutex(s->_ICPReqCont->mutex);
      EThread *ethread = this_ethread();
      ink_hrtime request_start_time;

      if (!MUTEX_TAKE_TRY_LOCK(ICPReqContMutex, ethread)) {
        ICP_INCREMENT_DYN_STAT(icp_response_request_nolock_stat);
        //
        // Unable to get ICP request mutex, delay and move back
        // to the GET_ICP_REQUEST state.  We need to do this
        // since the ICP request may be deallocated by the active
        // continuation.
        //
        s->_ICPReqCont = (ICPRequestCont *)nullptr;
        s->_next_state = GET_ICP_REQUEST;
        RECORD_ICP_STATE_CHANGE(s, 0, GET_ICP_REQUEST);
        return EVENT_CONT;
      }
      // Log as "response for ICP request"
      Peer *p = _ICPpr->FindPeer(s->_sender);
      p->LogRecvMsg(s->_rICPmsg, 1);

      // Process the ICP response for the given ICP request
      ICPRequestCont::ICPRequestEventArgs_t args;
      args.rICPmsg     = s->_rICPmsg;
      args.rICPmsg_len = s->_rICPmsg_len;
      args.peer        = p;
      if (!s->_ICPReqCont->GetActionPtr()->cancelled) {
        request_start_time = s->_ICPReqCont->GetRequestStartTime();
        Debug("icp", "Passing Reply for ICP Id=%d", s->_rICPmsg->h.requestno);
        s->_ICPReqCont->handleEvent((int)ICP_RESPONSE_MESSAGE, (void *)&args);
      } else {
        request_start_time = 0;
        delete s->_ICPReqCont;
        Debug("icp", "User cancelled ICP request Id=%d", s->_rICPmsg->h.requestno);
      }

      // Note: s->_ICPReqCont is deallocated at this point.
      s->_ICPReqCont = nullptr;

      MUTEX_UNTAKE_LOCK(ICPReqContMutex, ethread);
      if (request_start_time) {
        ICP_SUM_DYN_STAT(total_icp_response_time_stat, (Thread::get_hrtime() - request_start_time));
      }
      RECORD_ICP_STATE_CHANGE(s, 0, READ_NOT_ACTIVE);
      s->_next_state = READ_NOT_ACTIVE;
      break; // move to next_state
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case READ_NOT_ACTIVE:
    case READ_NOT_ACTIVE_EXIT: {
      ink_release_assert(_recursion_depth == 0);
      if (!_ICPpr->Lock()) {
        return EVENT_CONT; // unable to get lock, try again later
      }

      // Note incoming ICP request or response completion
      _ICPpr->DecPendingQuery();
      _ICPpr->Unlock();

      s->_buf = nullptr;
      if (s->_next_state == READ_NOT_ACTIVE_EXIT) {
        s->_next_state = READ_PROCESSING_COMPLETE;
        return EVENT_DONE;
      } else {
        // Last read was valid, see if any more read data before exiting
        s->reset();
        s->_start_time = Thread::get_hrtime();
        s->_next_state = READ_ACTIVE;
        RECORD_ICP_STATE_CHANGE(s, 0, READ_ACTIVE);
        break; // restart
      }
    }
      ink_release_assert(0); // Should never happen
    // fallthrough

    case READ_PROCESSING_COMPLETE:
    default:
      ink_release_assert(0); // Should never happen

    } // End of switch

  } // End of while(1)
}

//------------------------------------------------------------------------
// Class ICPRequestCont member functions
//      Implements the state machine which processes locally generated
//      ICP queries.
//------------------------------------------------------------------------
ClassAllocator<ICPRequestCont> ICPRequestCont_allocator("ICPRequestCont_allocator");

ICPRequestCont::ICPRequestCont(ICPProcessor *pr, Continuation *c, URL *u)
  : Continuation(nullptr),
    _cont(c),
    _url(u),
    _start_time(0),
    _ICPpr(pr),
    _timeout(nullptr),
    npending_actions(0),
    pendingActions(nullptr),
    _sequence_number(0),
    _expected_replies(0),
    _expected_replies_list(MAX_DEFINED_PEERS),
    _received_replies(0),
    _next_state(ICP_START)
{
  memset((void *)&_ret_sockaddr, 0, sizeof(_ret_sockaddr));
  _ret_status    = ICP_LOOKUP_FAILED;
  _act.cancelled = false;
  _act           = c;
  memset((void *)&_ICPmsg, 0, sizeof(_ICPmsg));
  memset((void *)&_sendMsgHdr, 0, sizeof(_sendMsgHdr));
  memset((void *)&_sendMsgIOV, 0, sizeof(_sendMsgIOV));

  if (c) {
    this->mutex = c->mutex;
  }
}

ICPRequestCont::~ICPRequestCont()
{
  _act        = nullptr;
  this->mutex = nullptr;

  if (_timeout) {
    _timeout->cancel(this);
    _timeout = nullptr;
  }
  RemoveICPRequest(_sequence_number);

  if (_ICPmsg.h.opcode == ICP_OP_QUERY) {
    if (_ICPmsg.un.query.URL) {
      ats_free(_ICPmsg.un.query.URL);
    }
  }
  if (pendingActions) {
    delete pendingActions;
    pendingActions = nullptr;
  }
}

void
ICPRequestCont::remove_from_pendingActions(Action *a)
{
  if (!pendingActions) {
    npending_actions--;
    return;
  }
  for (intptr_t i = 0; i < pendingActions->length(); i++) {
    if ((*pendingActions)[i] == a) {
      for (intptr_t j = i; j < pendingActions->length() - 1; j++) {
        (*pendingActions)[j] = (*pendingActions)[j + 1];
      }
      pendingActions->set_length(pendingActions->length() - 1);
      npending_actions--;
      return;
    }
  }
  npending_actions--; // completed inline
}

void
ICPRequestCont::remove_all_pendingActions()
{
  int active_pendingActions = 0;

  if (!pendingActions) {
    return;
  }
  for (intptr_t i = 0; i < pendingActions->length(); i++) {
    if ((*pendingActions)[i] && ((*pendingActions)[i] != ACTION_IO_ERROR)) {
      ((*pendingActions)[i])->cancel();
      (*pendingActions)[i] = nullptr;
      npending_actions--;
      active_pendingActions++;
    } else {
      (*pendingActions)[i] = nullptr;
    }
  }
  pendingActions->set_length(pendingActions->length() - active_pendingActions);
}

int
ICPRequestCont::ICPRequestEvent(int event, Event *e)
{
  // Note: Passed parameter 'e' is not an Event *
  //       if event == ICP_RESPONSE_MESSAGE

  ink_assert(event == NET_EVENT_DATAGRAM_WRITE_COMPLETE || event == NET_EVENT_DATAGRAM_WRITE_ERROR || event == EVENT_IMMEDIATE ||
             event == EVENT_INTERVAL || event == ICP_RESPONSE_MESSAGE);
  // handle reentrant callback
  if ((event == NET_EVENT_DATAGRAM_WRITE_COMPLETE) || (event == NET_EVENT_DATAGRAM_WRITE_ERROR)) {
    ink_assert(npending_actions > 0);
    remove_from_pendingActions((Action *)e);
    return EVENT_DONE;
  }
  // Start of user ICP query request processing.  We start here after
  // the reschedule in ICPProcessor::ICPQuery().
  switch (_next_state) {
  case ICP_START:
  case ICP_OFF_TERMINATE:
  case ICP_QUEUE_REQUEST:
  case ICP_AWAITING_RESPONSE:
  case ICP_DEQUEUE_REQUEST:
  case ICP_POST_COMPLETION:
  case ICP_REQUEST_NOT_ACTIVE: {
    if (ICPStateMachine(event, (void *)e) == EVENT_CONT) {
      //
      // Unable to acquire lock, reschedule continuation
      //
      eventProcessor.schedule_in(this, HRTIME_MSECONDS(RETRY_INTERVAL), ET_ICP);
      return EVENT_CONT;

    } else if (_next_state == ICP_DONE) {
      //
      // ICP request processing complete.
      //
      delete this;
      break;
    } else {
      break;
    }
  }
    ink_release_assert(0); // should never happen
  // fallthrough

  case ICP_DONE:
  default:
    ink_release_assert(0); // should never happen
  }                        // End of switch

  return EVENT_DONE;
}

int
ICPRequestCont::NopICPRequestEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  delete this;
  return EVENT_DONE;
}

int
ICPRequestCont::ICPStateMachine(int event, void *d)
{
  //*******************************************
  // ICP message processing state machine
  //*******************************************
  ICPConfiguration *ICPcf = _ICPpr->GetConfig();
  ip_port_text_buffer ipb;

  while (true) { // loop forever

    switch (_next_state) {
    case ICP_START: {
      // User may have cancelled request, if so abort request.
      if (_act.cancelled) {
        _next_state = ICP_DONE;
        return EVENT_DONE;
      }

      if (!_ICPpr->Lock()) {
        return EVENT_CONT; // Unable to get lock, try again later
      }

      if (_ICPpr->AllowICPQueries() && (ICPcf->globalConfig()->ICPconfigured() == ICP_MODE_SEND_RECEIVE)) {
        // Reject NULL pointer or "localhost" URLs
        if (_url->valid()) {
          int host_len;
          const char *host = _url->host_get(&host_len);
          if (ptr_len_casecmp(host, host_len, "127.0.0.1") == 0 || ptr_len_casecmp(host, host_len, "localhost") == 0) {
            _ICPpr->Unlock();

            // NULL pointer or "localhost" URL, terminate request
            _next_state = ICP_OFF_TERMINATE;
            Debug("icp", "[ICP_START] NULL/localhost URL ignored Id=%d", _sequence_number);
            break; // move to next_state
          }
        }
        // Note pending ICP request
        _ICPpr->IncPendingQuery();
        _ICPpr->Unlock();

        // Build the ICP query message
        char *urlstr   = _url->string_get(nullptr);
        int urlstr_len = strlen(urlstr) + 1;

        int status = BuildICPMsg(ICP_OP_QUERY, _sequence_number = ICPReqSeqNumber(), 0 /* optflags */, 0 /* optdata */,
                                 0 /* shostid */, (void *)urlstr, urlstr_len, &_sendMsgHdr, _sendMsgIOV, &_ICPmsg);
        // urlstr memory freed in destructor
        ink_assert(status == 0);
        Debug("icp", "[ICP_START] ICP_OP_QUERY for [%s], Id=%d", urlstr, _sequence_number);

        _next_state = ICP_QUEUE_REQUEST;
        break; // move to next_state

      } else {
        ICP_INCREMENT_DYN_STAT(icp_start_icpoff_stat);
        _ICPpr->Unlock();

        // ICP NOT enabled, terminate request
        _next_state = ICP_OFF_TERMINATE;
        break; // move to next_state
      }
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_OFF_TERMINATE: {
      if (!MUTEX_TAKE_TRY_LOCK_FOR(mutex, this_ethread(), _cont)) {
        return EVENT_CONT; // unable to get lock, delay and retry
      }
      Debug("icp", "[ICP_OFF_TERMINATE] Id=%d", _sequence_number);

      // ICP NOT enabled, post completion on request
      if (!_act.cancelled) {
        _cont->handleEvent(_ret_status, (void *)&_ret_sockaddr);
      }
      MUTEX_UNTAKE_LOCK(mutex, this_ethread());

      _next_state = ICP_DONE;
      return EVENT_DONE;
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_QUEUE_REQUEST: {
      // Place ICP request on the pending request queue
      int ret = AddICPRequest(_sequence_number, this);
      ink_assert(ret == 0);

      // Generate ICP requests to peers
      int bias         = _ICPpr->GetStartingSendPeerBias();
      int SendPeers    = _ICPpr->GetSendPeers();
      npending_actions = 0;
      while (SendPeers > 0) {
        Peer *P = _ICPpr->GetNthSendPeer(SendPeers, bias);
        if (!P->IsOnline()) {
          SendPeers--;
          continue;
        }
        //
        // Send query request to Peers
        //

        // because of reentrancy, we have to do this first, just
        // in case we get called back immediately.
        int was_expected = P->ExpectedReplies(&_expected_replies_list);
        _expected_replies += was_expected;
        npending_actions++;
        Action *a = P->SendMsg_re(this, P, &_sendMsgHdr, nullptr);
        if (!a) {
          a = ACTION_IO_ERROR;
        }
        if (a != ACTION_IO_ERROR) {
          if (a != ACTION_RESULT_DONE) {
            if (!pendingActions) {
              pendingActions = new DynArray<Action *>(&default_action);
            }
            (*pendingActions)(npending_actions) = a;
          }
          P->LogSendMsg(&_ICPmsg, nullptr); // log as send query
          Debug("icp", "[ICP_QUEUE_REQUEST] Id=%d send query to [%s]", _sequence_number,
                ats_ip_nptop(P->GetIP(), ipb, sizeof(ipb)));
        } else {
          _expected_replies_list.ClearBit(P->GetPeerID());
          _expected_replies -= was_expected;
          // Partial or failed write.
          ICP_INCREMENT_DYN_STAT(send_query_partial_write_stat);
          // coverity[uninit_use_in_call]
          Debug("icp_warn", "ICP query send, res=%d, ip=%s", ntohs(_ICPmsg.h.msglen), ats_ip_ntop(P->GetIP(), ipb, sizeof(ipb)));
        }
        SendPeers--;
      }

      Debug("icp", "[ICP_QUEUE_REQUEST] Id=%d expected replies=%d", _sequence_number, _expected_replies);
      if (!_expected_replies) {
        //
        // Nothing to wait for, terminate ICP processing
        //
        ICP_INCREMENT_DYN_STAT(icp_queries_no_expected_replies_stat);
        _next_state = ICP_DEQUEUE_REQUEST;
        break; // move to next_state
      }
      ICP_SUM_DYN_STAT(total_udp_send_queries_stat, _expected_replies);

      //
      // Setup ICP request response timeout
      //
      int tval = _ICPpr->GetConfig()->globalConfig()->ICPqueryTimeout();
      _timeout = eventProcessor.schedule_in(this, HRTIME_SECONDS(tval), ET_ICP);

      _next_state = ICP_AWAITING_RESPONSE;
      return EVENT_DONE;
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_AWAITING_RESPONSE: {
      Debug("icp", "[ICP_AWAITING_RESPONSE] Id=%d", _sequence_number);
      ink_assert(d);
      ICPRequestEventArgs_t dummyArgs;
      ICPRequestEventArgs_t *args = nullptr;

      if (event == ICP_RESPONSE_MESSAGE) {
        args = (ICPRequestEventArgs_t *)d;
      } else if (event == EVENT_INTERVAL) {
        memset((void *)&dummyArgs, 0, sizeof(dummyArgs));
        args = &dummyArgs;
      } else {
        ink_release_assert(0); // should never happen
      }

      // Process ICP response
      if (ICPResponseMessage(event, args->rICPmsg, args->peer) == EVENT_DONE) {
        // ICP Request processing is complete, do completion actions
        _next_state = ICP_DEQUEUE_REQUEST;
        break; // move to next_state

      } else {
        // Continue to wait for additional replies
        return EVENT_DONE;
      }
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_DEQUEUE_REQUEST: {
      // Remove ICP request from active queue
      int ret = RemoveICPRequest(_sequence_number);
      Debug("icp", "[ICP_DEQUEUE_REQUEST] Id=%d", _sequence_number);
      ink_assert(ret == 0);
      //_sequence_number = 0; // moved to REQUEST_NOT_ACTIVE
      _next_state = ICP_POST_COMPLETION;
      break; // move to next_state
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_POST_COMPLETION: {
      if (!MUTEX_TAKE_TRY_LOCK_FOR(mutex, this_ethread(), _cont)) {
        return EVENT_CONT; // unable to get lock, delay and retry
      }
      Debug("icp", "[ICP_POST_COMPLETION] Id=%d", _sequence_number);

      // Post completion on the ICP request.
      if (!_act.cancelled) {
        _cont->handleEvent(_ret_status, (void *)&_ret_sockaddr);
      }
      MUTEX_UNTAKE_LOCK(mutex, this_ethread());
      ICP_SUM_DYN_STAT(total_icp_request_time_stat, (Thread::get_hrtime() - _start_time));

      _next_state = ICP_WAIT_SEND_COMPLETE;
      break; // move to next_state
    }
      ink_release_assert(0); // should never happen
    // fallthrough
    case ICP_WAIT_SEND_COMPLETE: {
      // wait for all the sends to complete.
      if (npending_actions > 0) {
        Debug("icp", "[ICP_WAIT_SEND_COMPLETE] Id=%d active=%d", _sequence_number, npending_actions);
      } else {
        _next_state = ICP_REQUEST_NOT_ACTIVE;
        // move to next state
        break;
      }
    } break;
      ink_release_assert(0); // should never happen
    // fallthrough
    case ICP_REQUEST_NOT_ACTIVE: {
      Debug("icp", "[ICP_REQUEST_NOT_ACTIVE] Id=%d", _sequence_number);
      _sequence_number = 0;
      if (!_ICPpr->Lock()) {
        return EVENT_CONT; // Unable to get lock, try again later
      }

      // Note pending ICP request completion
      _ICPpr->DecPendingQuery();
      _ICPpr->Unlock();

      _next_state = ICP_DONE;
      return EVENT_DONE;
    }
      ink_release_assert(0); // should never happen
    // fallthrough

    case ICP_DONE:
    default:
      ink_release_assert(0); // should never happen

    } // End of switch

  } // End of while(1)
}

int
ICPRequestCont::ICPResponseMessage(int event, ICPMsg_t *m, Peer *peer)
{
  ip_port_text_buffer ipb, ipb2;

  if (event == EVENT_INTERVAL) {
    _timeout = nullptr;
    remove_all_pendingActions();

    // ICP request response timeout, if we received a response from
    // any parent, return it to resolve the miss.

    if (_received_replies) {
      int NumParentPeers = _ICPpr->GetParentPeers();
      if (NumParentPeers > 0) {
        int n;
        Peer *pp;
        for (n = 0; n < NumParentPeers; n++) {
          pp = _ICPpr->GetNthParentPeer(0, _ICPpr->GetStartingParentPeerBias());
          if (pp && !_expected_replies_list.IsBitSet(pp->GetPeerID()) && pp->isUp()) {
            ats_ip_copy(&_ret_sockaddr.sa, pp->GetIP());
            _ret_sockaddr.port() = htons(static_cast<ParentSiblingPeer *>(pp)->GetProxyPort());
            _ret_status          = ICP_LOOKUP_FOUND;

            Debug("icp", "ICP timeout using parent Id=%d from [%s] return [%s]", _sequence_number,
                  ats_ip_nptop(pp->GetIP(), ipb, sizeof(ipb)), ats_ip_nptop(&_ret_sockaddr, ipb2, sizeof(ipb2)));
            return EVENT_DONE;
          }
        }
      }
    }
    // Timeout received on ICP request, return ICP_LOOKUP_FAILED
    Debug("icp", "ICP Response timeout for Id=%d", _sequence_number);
    return EVENT_DONE;

  } else {
    // We have received a response to our ICP query request.
    // See if this response resolves the ICP query.
    //
    ink_assert(m->h.requestno == _sequence_number);

    switch (m->h.opcode) {
    case ICP_OP_HIT:
    case ICP_OP_HIT_OBJ: {
      // Kill timeout event
      _timeout->cancel(this);
      _timeout = nullptr;

      ICP_INCREMENT_DYN_STAT(icp_query_hits_stat);
      ++_received_replies;
      ats_ip_copy(&_ret_sockaddr, peer->GetIP());
      _ret_sockaddr.port() = htons(static_cast<ParentSiblingPeer *>(peer)->GetProxyPort());
      _ret_status          = ICP_LOOKUP_FOUND;

      Debug("icp", "ICP Response HIT for Id=%d from [%s] return [%s]", _sequence_number,
            ats_ip_nptop(peer->GetIP(), ipb, sizeof(ipb)), ats_ip_nptop(&_ret_sockaddr, ipb2, sizeof(ipb2)));
      return EVENT_DONE;
    }
    case ICP_OP_MISS:
    case ICP_OP_ERR:
    case ICP_OP_MISS_NOFETCH:
    case ICP_OP_DENIED: {
      Debug("icp", "ICP MISS response for Id=%d from [%s]", _sequence_number, ats_ip_nptop(peer->GetIP(), ipb, sizeof(ipb)));
      // "received_replies" is only for Peers who we expect a reply
      //  from (Peers which are in the expected_replies_list).
      int Id = peer->GetPeerID();
      if (_expected_replies_list.IsBitSet(Id)) {
        // Clear bit to note receipt of reply
        _expected_replies_list.ClearBit(Id);
        ++_received_replies;
      }

      if (_received_replies < _expected_replies) {
        return EVENT_CONT; // wait for more responses
      }

      // Kill timeout event
      _timeout->cancel(this);
      _timeout = nullptr;

      ICP_INCREMENT_DYN_STAT(icp_query_misses_stat);
      //
      // All responders have returned ICP_OP_MISS.
      // If parents exists, select one to resolve the request.
      //
      if (_ICPpr->GetParentPeers() > 0) {
        // In cases where multiple parents exist, we use
        // a round robin scheme.
        Peer *p = nullptr;
        // try to find an UP parent, if none, return ICP_LOOKUP_FAILED
        {
          int i;
          for (i = 0; i < _ICPpr->GetParentPeers(); i++) {
            p = _ICPpr->GetNthParentPeer(0, _ICPpr->GetStartingParentPeerBias());
            // find an UP parent
            if (p->isUp()) {
              break;
            }
          }
          // if no parent is selected, then return ICP_LOOKUP_FAILED
          if (i >= _ICPpr->GetParentPeers()) {
            Debug("icp", "None of the %d ICP parent(s) is up", _ICPpr->GetParentPeers());
            p = nullptr;
          }
        }
        if (p) {
          ats_ip_copy(&_ret_sockaddr, p->GetIP());
          _ret_sockaddr.port() = htons(static_cast<ParentSiblingPeer *>(p)->GetProxyPort());
          _ret_status          = ICP_LOOKUP_FOUND;

          Debug("icp", "ICP ALL MISS(1) for Id=%d return [%s]", _sequence_number, ats_ip_nptop(&_ret_sockaddr, ipb, sizeof(ipb)));
          return EVENT_DONE;
        }
      }
      Debug("icp", "ICP ALL MISS(2) for Id=%d return [%s]", _sequence_number, ats_ip_nptop(&_ret_sockaddr, ipb, sizeof(ipb)));
      return EVENT_DONE;
    }
    default: {
      ICP_INCREMENT_DYN_STAT(invalid_icp_query_response_stat);
      // coverity[uninit_use_in_call]
      Warning("Invalid ICP response, op=%d reqno=%d ip=%s", m->h.opcode, m->h.requestno,
              ats_ip_ntop(peer->GetIP(), ipb, sizeof(ipb)));
      return EVENT_CONT; // wait for more responses
    }

    } // End of switch
  }
}

//------------------------------------------------
// Class ICPRequestCont static member functions
//------------------------------------------------

// Static member function
void
ICPRequestCont::NetToHostICPMsg(ICPMsg_t *in, ICPMsg_t *out)
{
  out->h.opcode      = in->h.opcode;
  out->h.version     = in->h.version;
  out->h.msglen      = ntohs(in->h.msglen);
  out->h.requestno   = ntohl(in->h.requestno);
  out->h.optionflags = ntohl(in->h.optionflags);
  out->h.optiondata  = ntohl(in->h.optiondata);
  out->h.shostid     = ntohl(in->h.shostid);

  switch (in->h.opcode) {
  case ICP_OP_QUERY: {
    memcpy((char *)&out->un.query.rhostid, (char *)((char *)(&in->h.shostid) + sizeof(in->h.shostid)),
           sizeof(out->un.query.rhostid));
    out->un.query.rhostid = ntohl(out->un.query.rhostid);
    out->un.query.URL     = (char *)((char *)(&in->h.shostid) + sizeof(in->h.shostid) + sizeof(out->un.query.rhostid));
    break;
  }
  case ICP_OP_HIT: {
    out->un.hit.URL = (char *)((char *)(&in->h.shostid) + sizeof(in->h.shostid));
    break;
  }
  case ICP_OP_MISS: {
    out->un.miss.URL = (char *)((char *)(&in->h.shostid) + sizeof(in->h.shostid));
    break;
  }
  case ICP_OP_HIT_OBJ: {
    out->un.hitobj.URL = (char *)((char *)(&in->h.shostid) + sizeof(in->h.shostid));

    // strlen() is bounded since buffer in null terminated.
    out->un.hitobj.p_objsize = (char *)(out->un.hitobj.URL + strlen(out->un.hitobj.URL));
    memcpy((char *)&out->un.hitobj.objsize, out->un.hitobj.p_objsize, sizeof(out->un.hitobj.objsize));
    out->un.hitobj.objsize = ntohs(out->un.hitobj.objsize);
    out->un.hitobj.data    = (char *)(out->un.hitobj.p_objsize + sizeof(out->un.hitobj.objsize));
    break;
  }
  default:
    break;
  }
}

int
ICPRequestCont::BuildICPMsg(ICPopcode_t op, unsigned int seqno, int optflags, int optdata, int shostid, void *data, int datalen,
                            struct msghdr *mhdr, struct iovec *iov, ICPMsg_t *icpmsg)
{
  // Build ICP message for transmission in network byte order.
  if (op == ICP_OP_QUERY) {
    icpmsg->un.query.rhostid = htonl(0);
    icpmsg->un.query.URL     = (char *)data;

    mhdr->msg_iov    = iov;
    mhdr->msg_iovlen = 3;

    iov[0].iov_base = (caddr_t)icpmsg;
    iov[0].iov_len  = sizeof(ICPMsgHdr_t);

    iov[1].iov_base = (caddr_t)&icpmsg->un.query.rhostid;
    iov[1].iov_len  = sizeof(icpmsg->un.query.rhostid);

    iov[2].iov_base  = (caddr_t)data;
    iov[2].iov_len   = datalen;
    icpmsg->h.msglen = htons(iov[0].iov_len + iov[1].iov_len + iov[2].iov_len);

  } else if (op == ICP_OP_HIT) {
    icpmsg->un.hit.URL = (char *)data;

    mhdr->msg_iov    = iov;
    mhdr->msg_iovlen = 2;

    iov[0].iov_base = (caddr_t)icpmsg;
    iov[0].iov_len  = sizeof(ICPMsgHdr_t);

    iov[1].iov_base  = (caddr_t)data;
    iov[1].iov_len   = datalen;
    icpmsg->h.msglen = htons(iov[0].iov_len + iov[1].iov_len);

  } else if (op == ICP_OP_MISS) {
    icpmsg->un.miss.URL = (char *)data;

    mhdr->msg_iov    = iov;
    mhdr->msg_iovlen = 2;

    iov[0].iov_base = (caddr_t)icpmsg;
    iov[0].iov_len  = sizeof(ICPMsgHdr_t);

    iov[1].iov_base  = (caddr_t)data;
    iov[1].iov_len   = datalen;
    icpmsg->h.msglen = htons(iov[0].iov_len + iov[1].iov_len);

  } else {
    ink_release_assert(0);
    return 1; // failed
  }

  mhdr->msg_name    = (caddr_t) nullptr;
  mhdr->msg_namelen = 0;
// TODO: The following is just awkward
#if !defined(linux) && !defined(freebsd) && !defined(darwin) && !defined(solaris) && !defined(openbsd)
  mhdr->msg_accrights    = (caddr_t)0;
  mhdr->msg_accrightslen = 0;
#elif !defined(solaris)
  mhdr->msg_control    = nullptr;
  mhdr->msg_controllen = 0;
  mhdr->msg_flags      = 0;
#endif

  icpmsg->h.opcode      = op;
  icpmsg->h.version     = ICP_VERSION_2;
  icpmsg->h.requestno   = htonl(seqno);
  icpmsg->h.optionflags = htonl(optflags);
  icpmsg->h.optiondata  = htonl(optdata);
  icpmsg->h.shostid     = htonl(shostid);

  return 0; // Success
}

// Static ICPRequestCont data declarations
unsigned int ICPRequestCont::ICPRequestSeqno = 1;
Queue<ICPRequestCont> ICPRequestQueue[ICPRequestCont::ICP_REQUEST_HASH_SIZE];

// Static member function
unsigned int
ICPRequestCont::ICPReqSeqNumber()
{
  // Generate ICP request sequence numbers.  This must be unique.
  unsigned int res = 0;
  do {
    res = (unsigned int)ink_atomic_increment((int *)&ICPRequestSeqno, 1);
  } while (!res);

  return res;
}

// Static member function
inline int
ICPRequestCont::ICPRequestHash(unsigned int seqno)
{
  // ICPRequestQueue hash
  return seqno % ICP_REQUEST_HASH_SIZE;
}

// Static member function
int
ICPRequestCont::AddICPRequest(unsigned int seqno, ICPRequestCont *r)
{
  // Add ICP request to ICP outstanding queue (ICPRequestQueue).
  // return: 0 - success

  ICPRequestQueue[ICPRequestHash(seqno)].enqueue(r);
  return 0; // Success
}

// Static member function
ICPRequestCont *
ICPRequestCont::FindICPRequest(unsigned int seqno)
{
  // Find ICP request on outstanding queue with the given sequence number
  int hash = ICPRequestHash(seqno);
  ICPRequestCont *r;

  for (r = (ICPRequestCont *)ICPRequestQueue[hash].head; r; r = (ICPRequestCont *)r->link.next) {
    if (r->_sequence_number == seqno) {
      return r;
    }
  }
  return (ICPRequestCont *)nullptr; // Not found
}

// Static member function
int
ICPRequestCont::RemoveICPRequest(unsigned int seqno)
{
  // Remove ICP request from outstanding queue with the given
  //  sequence number
  // Return: 0 - success; 1 - not found

  if (!seqno) {
    return 1; // Not found
  }
  int hash = ICPRequestHash(seqno);
  ICPRequestCont *r;

  for (r = (ICPRequestCont *)ICPRequestQueue[hash].head; r; r = (ICPRequestCont *)r->link.next) {
    if (r->_sequence_number == seqno) {
      ICPRequestQueue[hash].remove(r);
      return 0;
    }
  }
  return 1; // Not found
}

//------------------------------------------------------------------------
// Class ICPProcessor member functions
//      Central class which initializes the ICP world.
//      Delegates incoming message processing to ICPHandlerCont
//      and outgoing message processing to ICPRequestCont.
//      Manages the ICP configuration database derived from TS
//      configuration info.
//------------------------------------------------------------------------

// Static data declarations for ICPProcessor
void
initialize_thread_for_icp(EThread *e)
{
  (void)e;
}

ICPProcessor icpProcessorInternal;
ICPProcessorExt icpProcessor(&icpProcessorInternal);

ICPProcessor::ICPProcessor()
  : _l(nullptr),
    _Initialized(0),
    _AllowIcpQueries(0),
    _PendingIcpQueries(0),
    _ICPConfig(nullptr),
    _ICPPeriodic(nullptr),
    _ICPHandler(nullptr),
    _mcastCB_handler(nullptr),
    _PeriodicEvent(nullptr),
    _ICPHandlerEvent(nullptr),
    _nPeerList(-1),
    _LocalPeer(nullptr),
    _curSendPeer(0),
    _nSendPeerList(-1),
    _curRecvPeer(0),
    _nRecvPeerList(-1),
    _curParentPeer(0),
    _nParentPeerList(-1),
    _ValidPollData(0),
    _last_recv_peer_bias(0)
{
  memset((void *)_PeerList, 0, sizeof(_PeerList[PEER_LIST_SIZE]));
  memset((void *)_SendPeerList, 0, sizeof(_SendPeerList[SEND_PEER_LIST_SIZE]));
  memset((void *)_RecvPeerList, 0, sizeof(_RecvPeerList[RECV_PEER_LIST_SIZE]));
  memset((void *)_ParentPeerList, 0, sizeof(_ParentPeerList[PARENT_PEER_LIST_SIZE]));
  memset((void *)_PeerIDtoPollIndex, 0, sizeof(_PeerIDtoPollIndex[PEER_ID_POLL_INDEX_SIZE]));
}

ICPProcessor::~ICPProcessor()
{
  if (_ICPPeriodic) {
    MUTEX_TAKE_LOCK(_ICPPeriodic->mutex, this_ethread());
    _PeriodicEvent->cancel();
    Mutex_unlock(_ICPPeriodic->mutex, this_ethread());
  }

  if (_ICPHandler) {
    MUTEX_TAKE_LOCK(_ICPHandler->mutex, this_ethread());
    _ICPHandlerEvent->cancel();
    Mutex_unlock(_ICPHandler->mutex, this_ethread());
  }
}

void
ICPProcessor::start()
{
  //*****************************************************
  // Perform initialization actions for ICPProcessor
  // (called at system startup)
  //*****************************************************
  if (_Initialized) { // Do only once
    return;
  }

  //
  // Setup ICPProcessor lock, required since ICPProcessor is instantiated
  //  as static object.
  //
  _l = new AtomicLock();

  //
  // Setup custom allocators
  //
  // replaced with generic IOBufferBlock allocator
  ICPHandlerCont::ICPDataBuf_IOBuffer_sizeindex = iobuffer_size_to_index(MAX_ICP_MSGSIZE, MAX_BUFFER_SIZE_INDEX);

  //
  // Setup ICP stats callbacks
  //
  InitICPStatCallbacks();

  //
  // Create ICP configuration objects
  //
  _ICPConfig = new ICPConfiguration();

  _mcastCB_handler = new ICPHandlerCont(this);
  SET_CONTINUATION_HANDLER(_mcastCB_handler, (ICPHandlerContHandler)&ICPHandlerCont::TossEvent);

  //
  // Build ICP peer list and setup listen sockets
  //
  if (_ICPConfig->globalConfig()->ICPconfigured()) {
    if (BuildPeerList() == 0) {
      if (SetupListenSockets() == 0) {
        _AllowIcpQueries = 1; // allow receipt of queries
      }
    }
  }
  DumpICPConfig();

  //
  // Start ICP configuration monitor (periodic continuation)
  //
  _ICPPeriodic = new ICPPeriodicCont(this);
  SET_CONTINUATION_HANDLER(_ICPPeriodic, (ICPPeriodicContHandler)&ICPPeriodicCont::PeriodicEvent);
  _PeriodicEvent = eventProcessor.schedule_every(_ICPPeriodic, HRTIME_MSECONDS(ICPPeriodicCont::PERIODIC_INTERVAL), ET_ICP);

  //
  // Start ICP receive handler continuation
  //
  _ICPHandler = new ICPHandlerCont(this);
  SET_CONTINUATION_HANDLER(_ICPHandler, (ICPHandlerContHandler)&ICPHandlerCont::PeriodicEvent);
  _ICPHandlerEvent = eventProcessor.schedule_every(_ICPHandler, HRTIME_MSECONDS(ICPHandlerCont::ICP_HANDLER_INTERVAL), ET_ICP);
  //
  // Stale lookup data initializations
  //
  if (!gclient_request.valid()) {
    gclient_request.create(HTTP_TYPE_REQUEST);
  }
  _Initialized = 1;
}

Action *
ICPProcessor::ICPQuery(Continuation *c, URL *url)
{
  //**************************************
  // HTTP state machine interface to ICP
  //**************************************

  // Build continuation to process ICP request
  EThread *thread    = this_ethread();
  ProxyMutex *mutex  = thread->mutex.get();
  ICPRequestCont *rc = new (ICPRequestCont_allocator.alloc()) ICPRequestCont(this, c, url);

  ICP_INCREMENT_DYN_STAT(icp_query_requests_stat);

  rc->SetRequestStartTime();
  SET_CONTINUATION_HANDLER(rc, (ICPRequestContHandler)&ICPRequestCont::ICPRequestEvent);
  eventProcessor.schedule_imm(rc, ET_ICP);

  return rc->GetActionPtr();
}

int
ICPProcessor::BuildPeerList()
{
  // Returns 0 on Success

  //
  //---------------------------------------------------------------------
  //  We always place all allocated Peer elements onto PeerList[],
  //  which is used to track allocated elements and validate (ip, port)
  //  uniqueness in the ICP configuration.
  //
  //  All MultiCastPeer(s) link the underlying ParentSiblingPeer structures
  //  using a singly linked list off the MultiCastPeer.
  //
  //  Peer elements placed onto SendPeerList[] are elements which are
  //  the target of ICP queries.
  //  In the case where MultiCasting is used, a pseudo peer element
  //  (MultiCastPeer) is placed onto the SendPeerList[] to act as a place
  //  holder for the underlying Peers.
  //
  //  RecvPeerList[] is the list of Peer(s) we perform reads on for
  //  ICP messages.  In the case of MultiCast, the pseudo MultiCast peer
  //  element (MultiCastPeer) is placed on this list.  Since we currently
  //  funnel all unicast receives through the local peer UDP socket,
  //  only the local peer and any pseudo MultiCastPeer structures reside
  //  on this list.
  //
  //  Parent (PEER_PARENT) Peer elements are also added to ParentPeerList
  //  which is used to select a parent in the case where all ICP queries
  //  have returned ICP_MISS.
  //---------------------------------------------------------------------
  //
  PeerConfigData *Pcfg;
  Peer *P;
  Peer *mcP;
  int index;
  int status;
  PeerType_t type;

  //
  // From the working copy of the ICP configuration data, build the
  // internal Peer data structures for ICP processing.
  // First, establish the Local Peer descriptor before processing
  // parents and siblings.
  //
  Pcfg = _ICPConfig->indexToPeerConfigData(0);
  ink_strlcpy(Pcfg->_hostname, "localhost", sizeof(Pcfg->_hostname));
  Pcfg->_ctype = PeerConfigData::CTYPE_LOCAL;

  // Get IP address for given interface
  IpEndpoint tmp_ip;
  if (!mgmt_getAddrForIntr(GetConfig()->globalConfig()->ICPinterface(), &tmp_ip.sa)) {
    Pcfg->_ip_addr._family = AF_UNSPEC;
    // No IP address for given interface
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP interface [%s] has no IP address", GetConfig()->globalConfig()->ICPinterface());
  } else {
    Pcfg->_my_ip_addr = Pcfg->_ip_addr = tmp_ip;
  }
  Pcfg->_proxy_port         = 0;
  Pcfg->_icp_port           = GetConfig()->globalConfig()->ICPport();
  Pcfg->_mc_member          = 0;
  Pcfg->_mc_ip_addr._family = AF_UNSPEC;
  Pcfg->_mc_ttl             = 0;

  //***************************************************
  // Descriptor for local host, add to PeerList and
  // RecvPeerList
  //***************************************************
  P      = new ParentSiblingPeer(PEER_LOCAL, Pcfg, this);
  status = AddPeer(P);
  ink_release_assert(status);
  status = AddPeerToRecvList(P);
  ink_release_assert(status);
  _LocalPeer = P;

  for (index = 1; index < MAX_DEFINED_PEERS; ++index) {
    Pcfg = _ICPConfig->indexToPeerConfigData(index);
    type = PeerConfigData::CTypeToPeerType_t(Pcfg->GetCType());
    //
    // Ignore parent and sibling entries corresponding to "localhost".
    // This is possible in a cluster configuration where parents and
    // siblings are cluster members.  Note that in a cluster
    // configuration, "icp.config" is shared by all nodes.
    //
    if (Pcfg->GetIPAddr() == _LocalPeer->GetIP()) {
      continue; // ignore
    }

    if ((type == PEER_PARENT) || (type == PEER_SIBLING)) {
      if (Pcfg->MultiCastMember()) {
        mcP = FindPeer(Pcfg->GetMultiCastIPAddr(), Pcfg->GetICPPort());
        if (!mcP) {
          //*********************************
          // Create multicast peer structure
          //*********************************
          mcP    = new MultiCastPeer(Pcfg->GetMultiCastIPAddr(), Pcfg->GetICPPort(), Pcfg->GetMultiCastTTL(), this);
          status = AddPeer(mcP);
          ink_assert(status);
          status = AddPeerToSendList(mcP);
          ink_assert(status);
          status = AddPeerToRecvList(mcP);
          ink_assert(status);
        }
        //*****************************
        // Add child to MultiCast peer
        //*****************************
        P      = new ParentSiblingPeer(type, Pcfg, this);
        status = AddPeer(P);
        ink_assert(status);
        status = ((MultiCastPeer *)mcP)->AddMultiCastChild(P);
        ink_assert(status);

      } else {
        //*****************************
        // Add parent/sibling peer
        //*****************************
        P      = new ParentSiblingPeer(type, Pcfg, this);
        status = AddPeer(P);
        ink_assert(status);
        status = AddPeerToSendList(P);
        ink_assert(status);
      }
      //****************************************
      // Also, add parent peers to parent list.
      //****************************************
      if (type == PEER_PARENT) {
        status = AddPeerToParentList(P);
        ink_assert(status);
      }
    }
  }
  return 0; // Success
}

void
ICPProcessor::FreePeerList()
{
  // Deallocate all Peer structures
  int index;
  for (index = 0; index < (_nPeerList + 1); ++index) {
    if (_PeerList[index]) {
      _PeerList[index] = nullptr;
    }
  }
  // Reset all control data
  _nPeerList           = -1;
  _LocalPeer           = (Peer *)nullptr;
  _curSendPeer         = 0;
  _nSendPeerList       = -1;
  _curRecvPeer         = 0;
  _nRecvPeerList       = -1;
  _curParentPeer       = 0;
  _nParentPeerList     = -1;
  _ValidPollData       = 0;
  _last_recv_peer_bias = 0;

  for (index = 0; index < PEER_LIST_SIZE; index++) {
    _PeerList[index] = nullptr;
  }
  for (index = 0; index < SEND_PEER_LIST_SIZE; index++) {
    _SendPeerList[index] = nullptr;
  }
  for (index = 0; index < RECV_PEER_LIST_SIZE; index++) {
    _RecvPeerList[index] = nullptr;
  }
  for (index = 0; index < PARENT_PEER_LIST_SIZE; index++) {
    _ParentPeerList[index] = nullptr;
  }
  memset((void *)_PeerIDtoPollIndex, 0, sizeof(_PeerIDtoPollIndex[PEER_ID_POLL_INDEX_SIZE]));
}

int
ICPProcessor::SetupListenSockets()
{
  int allow_null_configuration;

  if ((_ICPConfig->globalConfig()->ICPconfigured() == ICP_MODE_RECEIVE_ONLY) &&
      _ICPConfig->globalConfig()->ICPReplyToUnknownPeer()) {
    allow_null_configuration = 1;
  } else {
    allow_null_configuration = 0;
  }

  // Returns 0 on Success.

  //
  // Perform some basic sanity checks on the ICP configuration.
  //
  if (!_LocalPeer) {
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP setup, no defined local Peer");
    return 1; // Failed
  }

  if (GetSendPeers() == 0) {
    if (!allow_null_configuration) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP setup, no defined send Peer(s)");
      return 1; // Failed
    }
  }
  if (GetRecvPeers() == 0) {
    if (!allow_null_configuration) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP setup, no defined receive Peer(s)");
      return 1; // Failed
    }
  }
  //
  // Establish the required sockets for elements on the PeerList[].
  //
  Peer *P;
  int status;
  int index;
  ip_port_text_buffer ipb, ipb2;
  for (index = 0; index < (_nPeerList + 1); ++index) {
    if ((P = _PeerList[index].get())) {
      if ((P->GetType() == PEER_PARENT) || (P->GetType() == PEER_SIBLING)) {
        ParentSiblingPeer *pPS = (ParentSiblingPeer *)P;

        pPS->GetChan()->setRemote(pPS->GetIP());

      } else if (P->GetType() == PEER_MULTICAST) {
        MultiCastPeer *pMC = (MultiCastPeer *)P;
        ink_assert(_mcastCB_handler != nullptr);
        status = pMC->GetSendChan()->setup_mc_send(pMC->GetIP(), _LocalPeer->GetIP(), NON_BLOCKING, pMC->GetTTL(),
                                                   DISABLE_MC_LOOPBACK, _mcastCB_handler);
        if (status) {
          // coverity[uninit_use_in_call]
          RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP MC send setup failed, res=%d, ip=%s bind_ip=%s", status,
                           ats_ip_nptop(pMC->GetIP(), ipb, sizeof(ipb)), ats_ip_nptop(_LocalPeer->GetIP(), ipb2, sizeof(ipb2)));
          return 1; // Failed
        }

        status = pMC->GetRecvChan()->setup_mc_receive(pMC->GetIP(), _LocalPeer->GetIP(), NON_BLOCKING, pMC->GetSendChan(),
                                                      _mcastCB_handler);
        if (status) {
          // coverity[uninit_use_in_call]
          RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP MC recv setup failed, res=%d, ip=%s", status,
                           ats_ip_nptop(pMC->GetIP(), ipb, sizeof(ipb)));
          return 1; // Failed
        }
      }
    }
  }
  //
  // Setup the socket for the local host.
  // We funnel all unicast sends and receives through
  // the local peer UDP socket.
  //
  ParentSiblingPeer *pPS = (ParentSiblingPeer *)(GetLocalPeer());

  NetVCOptions options;
  options.local_ip.assign(pPS->GetIP());
  options.local_port   = pPS->GetICPPort();
  options.ip_proto     = NetVCOptions::USE_UDP;
  options.addr_binding = NetVCOptions::INTF_ADDR;
  status               = pPS->GetChan()->open(options);
  if (status) {
    // coverity[uninit_use_in_call] ?
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "ICP bind_connect failed, res=%d, ip=%s", status,
                     ats_ip_nptop(pPS->GetIP(), ipb, sizeof(ipb)));
    return 1; // Failed
  }

  return 0; // Success
}

void
ICPProcessor::ShutdownListenSockets()
{
  //
  // Close all open sockets for elements on the PeerList[]
  //
  ink_assert(!PendingQuery());
  Peer *P;

  for (int index = 0; index < (_nPeerList + 1); ++index) {
    if ((P = IdToPeer(index))) {
      if (P->GetType() == PEER_LOCAL) {
        ParentSiblingPeer *pPS = (ParentSiblingPeer *)P;
        (void)pPS->GetChan()->close();

      } else if (P->GetType() == PEER_MULTICAST) {
        MultiCastPeer *pMC = (MultiCastPeer *)P;
        (void)pMC->GetSendChan()->close();
        (void)pMC->GetRecvChan()->close();
      }
    }
  }
}

int
ICPProcessor::Reconfigure(int /* global_config_changed ATS_UNUSED */, int /* peer_config_changed ATS_UNUSED */)
{
  // Returns 0 on Success
  //
  // At this point, ICP requests processing is disabled and
  // no pending ICP requests exist.
  //
  ink_assert(_ICPConfig->HaveLock());
  ink_assert(!AllowICPQueries());
  ink_assert(!PendingQuery());
  //
  // Shutdown and deallocate all structures associated with the
  // current configuration.
  //
  ShutdownListenSockets();
  FreePeerList();
  //
  // Copy the new configuration into the working copy and
  // rebuild all associated structures.
  //
  _ICPConfig->UpdateGlobalConfig();
  _ICPConfig->UpdatePeerConfig();

  int status = -1;
  if (_ICPConfig->globalConfig()->ICPconfigured()) {
    if ((status = BuildPeerList()) == 0) {
      status = SetupListenSockets();
    }
    DumpICPConfig();
  }
  return status;
}

ICPProcessor::ReconfigState_t
ICPProcessor::ReconfigureStateMachine(ReconfigState_t s, int gconfig_changed, int pconfig_changed)
{
  //*****************************************************************
  // State machine which performs the ICP reconfiguration actions.
  // Defined states are as follows:
  //  1) (RC_RECONFIG) disable ICP, reconfigure if no request pending,
  //     else delay and retry.  Reconfigure and if success move to
  //     RC_ENABLE_ICP else RC_DONE.
  //  2) (RC_ENABLE_ICP) enable ICP, free ICP configuration lock.
  //  3) (RC_DONE) free ICP configuration lock.
  //*****************************************************************
  ink_assert(_ICPConfig->HaveLock());
  int reconfig_status;

  while (true) {
    switch (s) {
    case RC_RECONFIG: {
      if (!Lock()) {
        return RC_RECONFIG; // Unable to get lock, try again
      }

      if (PendingQuery()) {
        DisableICPQueries(); // disable ICP processing
        Unlock();
        CancelPendingReads();
        return RC_RECONFIG; // Pending requests, delay and retry

      } else {
        DisableICPQueries(); // disable ICP processing
        Unlock();
        // No pending ICP queries, perform reconfiguration
        reconfig_status = Reconfigure(gconfig_changed, pconfig_changed);

        if (reconfig_status == 0) {
          s = RC_ENABLE_ICP; // reconfig OK, enable ICP
        } else {
          s = RC_DONE; // reconfig failed, do not enable ICP
        }
        break; // move to next state
      }
    }

    case RC_ENABLE_ICP: {
      if (!Lock()) {
        return RC_ENABLE_ICP; // Unable to get lock, try again
      }

      EnableICPQueries(); // Enable ICP processing
      Unlock();

      s = RC_DONE;
      break; // move to next state
    }

    case RC_DONE: {
      // Release configuration lock
      _ICPConfig->Unlock();
      return RC_DONE; // Reconfiguration complete
    }
    default: {
      ink_release_assert(0); // Should never happen
    }

    } // End of switch

  } // End of while
  return RC_DONE;
}

void
ICPProcessor::CancelPendingReads()
{
  // Cancel pending ICP read by sending a bogus message to
  //  the local ICP port.

  ICPRequestCont *r = new (ICPRequestCont_allocator.alloc()) ICPRequestCont(this, nullptr, nullptr);
  SET_CONTINUATION_HANDLER(r, (ICPRequestContHandler)&ICPRequestCont::NopICPRequestEvent);
  r->mutex = new_ProxyMutex();

  // TODO: Check return value?
  ICPRequestCont::BuildICPMsg(ICP_OP_HIT, 0, 0 /* optflags */, 0 /* optdata */, 0 /* shostid */, (void *)nullptr, 0,
                              &r->_sendMsgHdr, r->_sendMsgIOV, &r->_ICPmsg);
  r->_sendMsgHdr.msg_iovlen = 1;
  r->_ICPmsg.h.version      = ~r->_ICPmsg.h.version; // bogus message

  Peer *lp = GetLocalPeer();
  IpEndpoint local_endpoint;
  ats_ip_copy(&local_endpoint.sa, lp->GetIP());
  r->_sendMsgHdr.msg_name    = (caddr_t)&local_endpoint;
  r->_sendMsgHdr.msg_namelen = sizeof(local_endpoint);
  udpNet.sendmsg_re(r, r, lp->GetSendFD(), &r->_sendMsgHdr);
}

Peer *
ICPProcessor::GenericFindListPeer(IpAddr const &ip, uint16_t port, int validListItems, Ptr<Peer> *List)
{
  Peer *P;
  port = htons(port);
  for (int n = 0; n < validListItems; ++n) {
    if ((P = List[n].get())) {
      if ((P->GetIP() == ip) && ((port == 0) || (ats_ip_port_cast(P->GetIP()) == port))) {
        return P;
      }
    }
  }
  return nullptr;
}

Peer *
ICPProcessor::FindPeer(IpAddr const &ip, uint16_t port)
{
  // Find (Peer *) with the given (ip,port) on the global list (PeerList)
  return GenericFindListPeer(ip, port, (_nPeerList + 1), _PeerList);
}

Peer *
ICPProcessor::FindSendListPeer(IpAddr const &ip, uint16_t port)
{
  // Find (Peer *) with the given (ip,port) on the
  //  scheduler list (SendPeerList)
  return GenericFindListPeer(ip, port, (_nSendPeerList + 1), _SendPeerList);
}

Peer *
ICPProcessor::FindRecvListPeer(IpAddr const &ip, uint16_t port)
{
  // Find (Peer *) with the given (ip,port) on the
  //  receive list (RecvPeerList)
  return GenericFindListPeer(ip, port, (_nRecvPeerList + 1), _RecvPeerList);
}

int
ICPProcessor::AddPeer(Peer *P)
{
  // Add (Peer *) to the global list (PeerList).  Make sure (ip,port) is
  //  unique.
  // Returns 1 - added; 0 - Not added

  //
  // Make sure no duplicate exists
  //
  if (FindPeer(P->GetIP())) {
    ip_port_text_buffer x;
    // coverity[uninit_use_in_call]
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "bad icp.config, multiple peer definitions for ip=%s",
                     ats_ip_nptop(P->GetIP(), x, sizeof(x)));

    return 0; // Not added
  } else {
    // Valid entry
    if (_nPeerList + 1 < PEER_LIST_SIZE) {
      _nPeerList++;
      _PeerList[_nPeerList] = P;
      P->SetPeerID(_nPeerList);
      return 1; // Added
    } else {
      return 0; // Not added
    }
  }
}

int
ICPProcessor::AddPeerToRecvList(Peer *P)
{
  // Add (Peer *) to the listen list (RecvPeerList).
  //  Make sure (ip,port) is unique.
  // Returns 1 - added; 0 - Not added

  // Assert that no duplicate exists
  ink_assert(FindRecvListPeer(IpAddr(P->GetIP()), ats_ip_port_host_order(P->GetIP())) == nullptr);

  if (_nRecvPeerList + 1 < RECV_PEER_LIST_SIZE) {
    _nRecvPeerList++;
    _RecvPeerList[_nRecvPeerList] = P;
    return 1; // Added
  } else {
    return 0; // Not added
  }
}

int
ICPProcessor::AddPeerToSendList(Peer *P)
{
  // Add (Peer *) to the scheduler list (SendPeerList).
  //  Make sure (ip,port) is unique.
  // Returns 1 - added; 0 - Not added

  // Assert that no duplicate exists
  ink_assert(FindSendListPeer(IpAddr(P->GetIP()), ats_ip_port_host_order(P->GetIP())) == nullptr);

  if (_nSendPeerList + 1 < SEND_PEER_LIST_SIZE) {
    _nSendPeerList++;
    _SendPeerList[_nSendPeerList] = P;
    return 1; // Added
  } else {
    return 0; // Not added
  }
}

int
ICPProcessor::AddPeerToParentList(Peer *P)
{
  // Add (Peer *) to the parent list (ParentPeerList).
  // Returns 1 - added; 0 - Not added

  if (_nParentPeerList + 1 < PARENT_PEER_LIST_SIZE) {
    _nParentPeerList++;
    _ParentPeerList[_nParentPeerList] = P;
    return 1; // Added
  } else {
    return 0; // Not added
  }
}

// End of ICP.cc
