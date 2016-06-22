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

  ICPConfig.cc


****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_file.h"
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "P_Net.h"
#include "P_RecProcess.h"
#include "Main.h"
#include "ICP.h"
#include "ICPProcessor.h"
#include "ICPlog.h"
#include "BaseManager.h"
#include "ts/I_Layout.h"

//--------------------------------------------------------------------------
//  Each ICP peer is described in "icp.config" with the
//  following info:
//    hostname (string)         -- hostname, used only if (host_ip_str == 0)
//    host_ip_str (string)      -- decimal dot notation; if null get IP
//                                   addresss via lookup on hostname
//    ctype (int)               -- 1=Parent, 2=Sibling, 3=local
//    proxy_port (int)          -- TCP Port #
//    icp_port (int)            -- UDP Port #
//    multicast_member          -- 0=No 1=Yes
//    multicast_ip_str (string) -- decimal dot notation
//    multicast_ttl (int)       -- (1 - 2; default 1)
//
//#############################################################################
//
//  ICP global configuration options are described in "records.config" using
//  the following entries.
//
//    proxy.config.icp.icp_configuration STRING (default "icp.config")
//    proxy.config.icp.enabled INT (0=No 1=Yes)
//    proxy.config.icp.icp_port INT 3130
//    proxy.config.icp.icp_interface STRING hme0
//    proxy.config.icp.multicast_enabled INT (0=No 1=Yes)
//    proxy.config.icp.query_timeout INT (seconds default is 2 secs)
//    proxy.config.icp.lookup_local INT (default is cluster lookup)
//
//  Example (1 parent and 1 sibling):
//  ============================================
//    proxy.config.icp.enabled INT 1
//    proxy.config.icp.multicast_enabled INT 0
//    proxy.config.icp.query_timeout INT 2
//
//    === "icp.config" entries ===
//
//    hostname:         "host1"
//    host_ip_str:      "209.1.33.10"
//    ctype:            1
//    proxy_port:       8080
//    icp_port:         3130
//    multicast_member  0
//    multicast_ip_str: "0.0.0.0"
//    multicast_ttl:    0
//
//    hostname:         "host2"
//    host_ip_str:      "209.1.33.11"
//    ctype:            2
//    proxy_port:       8080
//    icp_port:         3130
//    multicast_member  0
//    multicast_ip_str: "0.0.0.0"
//    multicast_ttl:    0
//
//  Example (1 parent and 1 sibling using MultiCast):
//  ============================================================
//    proxy.config.icp.enabled INT 1
//    proxy.config.icp.multicast_enabled INT 1
//    proxy.config.icp.query_timeout INT 2
//
//    === "icp.config" entries ===
//
//    hostname:         "host1"
//    host_ip_str:      "209.1.33.10"
//    ctype:            1
//    proxy_port:       8080
//    icp_port:         3130
//    multicast_member  1
//    multicast_ip_str: "239.128.16.128"
//    multicast_ttl:    1
//
//    hostname:         "host2"
//    host_ip_str:      "209.1.33.11"
//    ctype:            2
//    proxy_port:       8080
//    icp_port:         3130
//    multicast_member  1
//    multicast_ip_str: "239.128.16.128"
//    multicast_ttl:    1
//

//------------------------------------
// Class AtomicLock member functions
//------------------------------------
#if !defined(USE_CAS_FOR_ATOMICLOCK)
AtomicLock::AtomicLock()
{
  _mutex = new_ProxyMutex();
}

AtomicLock::~AtomicLock()
{
}

int
AtomicLock::Lock()
{
  EThread *et = this_ethread();
  ink_assert(et != NULL);
  return MUTEX_TAKE_TRY_LOCK(_mutex, et);
}

int
AtomicLock::HaveLock()
{
  EThread *et = this_ethread();
  ink_assert(et != NULL);
  return (_mutex->thread_holding == et);
}

void
AtomicLock::Unlock()
{
  EThread *et = this_ethread();
  ink_assert(et != NULL);
  MUTEX_UNTAKE_LOCK(_mutex, et);
}

#else  // USE_CAS_FOR_ATOMICLOCK
AtomicLock::AtomicLock() : lock_word(0)
{
}

AtomicLock::~AtomicLock()
{
}

int
AtomicLock::Lock()
{
  bool status = ink_atomic_cas(&_lock_word, AtomicLock::UNLOCKED, AtomicLock::LOCKED);
  return status;
}

int
AtomicLock::HaveLock()
{
  return (_lock_word == LOCKED);
}

void
AtomicLock::Unlock()
{
  ink_assert(_lock_word == AtomicLock::LOCKED);
  _lock_word = AtomicLock::UNLOCKED;
}
#endif // USE_CAS_FOR_ATOMICLOCK

//---------------------------------------------------------------------
// Class BitMap -- Member functions.
//                 Generic bitmap management class
//      Note: Bit positions are zero based (0 .. (bitmap_maxsize-1) )
//---------------------------------------------------------------------
BitMap::BitMap(int bitmap_maxsize)
{
  if (bitmap_maxsize <= (int)(STATIC_BITMAP_BYTE_SIZE * BITS_PER_BYTE)) {
    _bitmap           = _static_bitmap;
    _bitmap_size      = bitmap_maxsize;
    _bitmap_byte_size = STATIC_BITMAP_BYTE_SIZE;
  } else {
    _bitmap_byte_size = (bitmap_maxsize + (BITS_PER_BYTE - 1)) / BITS_PER_BYTE;
    _bitmap           = new char[_bitmap_byte_size];
    _bitmap_size      = bitmap_maxsize;
  }
  memset((void *)_bitmap, 0, _bitmap_byte_size);
}

BitMap::~BitMap()
{
  if (_bitmap_size > (int)(STATIC_BITMAP_BYTE_SIZE * BITS_PER_BYTE)) {
    delete[] _bitmap;
  }
}

void
BitMap::SetBit(int bit)
{
  if (bit >= _bitmap_size)
    return;

  char *pbyte = &_bitmap[bit / BITS_PER_BYTE];
  *pbyte |= (1 << (bit % BITS_PER_BYTE));
}

void
BitMap::ClearBit(int bit)
{
  if (bit >= _bitmap_size)
    return;

  char *pbyte = &_bitmap[bit / BITS_PER_BYTE];
  *pbyte &= ~(1 << (bit % BITS_PER_BYTE));
}

int
BitMap::IsBitSet(int bit)
{
  if (bit >= _bitmap_size)
    return 0;

  char *pbyte = &_bitmap[bit / BITS_PER_BYTE];
  if (*pbyte & (1 << (bit % BITS_PER_BYTE)))
    return 1;
  else
    return 0;
}

//-----------------------------------------------------------------------
// Class ICPConfigData member functions
//      Manage global ICP configuration data from the TS configuration.
//      Support class for ICPConfiguration.
//-----------------------------------------------------------------------
int
ICPConfigData::operator==(ICPConfigData &ICPData)
{
  if (ICPData._icp_enabled != _icp_enabled)
    return 0;
  if (ICPData._icp_port != _icp_port)
    return 0;
  if (ICPData._icp_interface != _icp_interface)
    return 0;
  if (ICPData._multicast_enabled != _multicast_enabled)
    return 0;
  if (ICPData._icp_query_timeout != _icp_query_timeout)
    return 0;
  if (ICPData._cache_lookup_local != _cache_lookup_local)
    return 0;
  if (ICPData._stale_lookup != _stale_lookup)
    return 0;
  if (ICPData._reply_to_unknown_peer != _reply_to_unknown_peer)
    return 0;
  if (ICPData._default_reply_port != _default_reply_port)
    return 0;
  return 1;
}

//------------------------------------------------------------------------
// Class PeerConfigData member functions
//      Manage ICP Peer configuration data from the TS configuration.
//      Support class for ICPConfiguration.
//------------------------------------------------------------------------
PeerConfigData::PeerConfigData() : _ctype(CTYPE_NONE), _proxy_port(0), _icp_port(0), _mc_member(0), _mc_ttl(0)
{
  memset(_hostname, 0, HOSTNAME_SIZE);
}

PeerType_t
PeerConfigData::CTypeToPeerType_t(int ctype)
{
  switch (ctype) {
  case (int)CTYPE_PARENT:
    return PEER_PARENT;

  case (int)CTYPE_SIBLING:
    return PEER_SIBLING;

  case (int)CTYPE_LOCAL:
    return PEER_LOCAL;

  default:
    return PEER_NONE;
  }
}

int
PeerConfigData::GetHostIPByName(char *hostname, IpAddr &rip)
{
  // Short circuit NULL hostname case
  if (0 == hostname || 0 == *hostname)
    return 1; // Unable to map to IP address

  addrinfo hints;
  addrinfo *ai;
  sockaddr const *best = 0;

  ink_zero(hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags  = AI_ADDRCONFIG;
  if (0 == getaddrinfo(hostname, 0, &hints, &ai)) {
    for (addrinfo *spot = ai; spot; spot = spot->ai_next) {
      // If current address is valid, and either we don't have one yet
      // or this address is less than our current, set it as current.
      if (ats_is_ip(spot->ai_addr) && (!best || -1 == ats_ip_addr_cmp(spot->ai_addr, best))) {
        best = spot->ai_addr;
      }
    }
    if (best)
      rip.assign(best);
    freeaddrinfo(ai);
  }
  return best ? 0 : 1;
}

bool
PeerConfigData::operator==(PeerConfigData &PeerData)
{
  if (strncmp(PeerData._hostname, _hostname, PeerConfigData::HOSTNAME_SIZE) != 0)
    return false;
  if (PeerData._ctype != _ctype)
    return false;
  if (PeerData._ip_addr != _ip_addr)
    return false;
  if (PeerData._proxy_port != _proxy_port)
    return false;
  if (PeerData._icp_port != _icp_port)
    return false;
  if (PeerData._mc_member != _mc_member)
    return false;
  if (PeerData._mc_ip_addr != _mc_ip_addr)
    return false;
  if (PeerData._mc_ttl != _mc_ttl)
    return false;
  return true;
}

//-----------------------------------------------------------------------
// Class ICPConfigUpdateCont member functions
//      Retry callout to ICPConfiguration::icp_config_change_callback()
//-----------------------------------------------------------------------
ICPConfigUpdateCont::ICPConfigUpdateCont(void *d, void *v) : Continuation(new_ProxyMutex()), _data(d), _value(v)
{
}

int
ICPConfigUpdateCont::RetryICPconfigUpdate(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  ICPConfiguration::icp_config_change_callback(_data, _value);
  delete this;
  return EVENT_DONE;
}

//--------------------------------------------------------------------------
// Class ICPConfiguration member functions
//      Overall manager of ICP configuration data from TS configuration.
//--------------------------------------------------------------------------
typedef int (ICPConfigUpdateCont::*ICPCfgContHandler)(int, void *);
ICPConfiguration::ICPConfiguration() : _icp_config_callouts(0)
{
  //*********************************************************
  // Allocate working and current ICPConfigData structures
  //*********************************************************
  _icp_cdata         = new ICPConfigData();
  _icp_cdata_current = new ICPConfigData();

  //********************************************************************
  // Read ICP config and setup update callbacks for "icp_cdata_current"
  //********************************************************************
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_icp_enabled, "proxy.config.icp.enabled");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_icp_port, "proxy.config.icp.icp_port");
  ICP_EstablishStaticConfigStringAlloc(_icp_cdata_current->_icp_interface, "proxy.config.icp.icp_interface");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_multicast_enabled, "proxy.config.icp.multicast_enabled");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_icp_query_timeout, "proxy.config.icp.query_timeout");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_cache_lookup_local, "proxy.config.icp.lookup_local");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_stale_lookup, "proxy.config.icp.stale_icp_enabled");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_reply_to_unknown_peer, "proxy.config.icp.reply_to_unknown_peer");
  ICP_EstablishStaticConfigInteger(_icp_cdata_current->_default_reply_port, "proxy.config.icp.default_reply_port");
  REC_EstablishStaticConfigInteger(_icp_cdata_current->_cache_generation, "proxy.config.http.cache.generation");

  UpdateGlobalConfig(); // sync working copy with current

  //**********************************************************
  // Allocate working and current PeerConfigData structures
  //**********************************************************
  for (int n = 0; n <= MAX_DEFINED_PEERS; ++n) {
    _peer_cdata[n]         = new PeerConfigData;
    _peer_cdata_current[n] = new PeerConfigData;
  }

  //*********************************************************
  // Initialize Peer data by simulating an update callout.
  //*********************************************************
  char icp_config_filename[PATH_NAME_MAX] = "";
  ICP_ReadConfigString(icp_config_filename, "proxy.config.icp.icp_configuration", sizeof(icp_config_filename) - 1);
  (void)icp_config_change_callback((void *)this, (void *)icp_config_filename, 1);
  UpdatePeerConfig(); // sync working copy with current

  //***************************************
  // Setup update callout on "icp.config"
  //***************************************
  ICP_RegisterConfigUpdateFunc("proxy.config.icp.icp_configuration", mgr_icp_config_change_callback, (void *)this);
}

ICPConfiguration::~ICPConfiguration()
{
// TBD: Need to disable update callbacks before deallocating data.
//      How do we do this?  For now, this never happens.
#ifdef OMIT
  if (_icp_cdata) {
    // TBD: Make sure _icp_cdata->_icp_interface has been freed
    delete ((void *)_icp_cdata);
  }
  if (_icp_cdata_current)
    delete ((void *)_icp_cdata_current);
  if (_peer_cdata)
    delete ((void *)_peer_cdata);
  if (_peer_cdata_current)
    delete ((void *)_peer_cdata_current);
#endif // OMIT
}

int
ICPConfiguration::GlobalConfigChange()
{
  return (!(*_icp_cdata == *_icp_cdata_current));
}

void
ICPConfiguration::UpdateGlobalConfig()
{
  *_icp_cdata = *_icp_cdata_current;
}

int
ICPConfiguration::PeerConfigChange()
{
  // Note: Entry zero reserved for "localhost"
  for (int i = 1; i <= MAX_DEFINED_PEERS; ++i) {
    if (!(*_peer_cdata[i] == *_peer_cdata_current[i]))
      return 1;
  }
  return 0;
}

void
ICPConfiguration::UpdatePeerConfig()
{
  // Note: Entry zero reserved for "localhost"
  for (int i = 1; i <= MAX_DEFINED_PEERS; ++i) {
    //
    // Broken on DEC and Solaris x86
    // *_peer_cdata[i] = *_peer_cdata_current[i];
    //
    memcpy(_peer_cdata[i], _peer_cdata_current[i], sizeof(*_peer_cdata[i]));
    // Setup IP address
    if ((_peer_cdata[i]->_ip_addr.isValid()) && _peer_cdata[i]->_hostname[0]) {
      // IP address not specified, lookup using hostname.
      (void)PeerConfigData::GetHostIPByName(_peer_cdata[i]->_hostname, _peer_cdata[i]->_my_ip_addr);
    } else {
      // IP address specified by user, lookup on hostname not required.
      _peer_cdata[i]->_my_ip_addr = _peer_cdata[i]->_ip_addr;
    }
  }
}

int
ICPConfiguration::mgr_icp_config_change_callback(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                                 RecData data, void *cookie)
{
  //*****************************************************************
  // Callout invoked by Configuration management when changes occur
  // to icp.config
  //*****************************************************************

  // Map this manager configuration callout onto ET_ICP

  ICPConfigUpdateCont *rh = new ICPConfigUpdateCont(cookie, data.rec_string);
  SET_CONTINUATION_HANDLER(rh, (ICPCfgContHandler)&ICPConfigUpdateCont::RetryICPconfigUpdate);
  eventProcessor.schedule_imm(rh, ET_ICP);
  return EVENT_DONE;
}

namespace
{
inline char *
next_field(char *text, char fs)
{
  text = strchr(text, fs);
  // Compress contiguous whitespace by leaving zret pointing at the last space.
  if (text && *text == fs)
    while (text[1] == fs)
      ++text;
  return text;
}
}

void *
ICPConfiguration::icp_config_change_callback(void *data, void *value, int startup)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  //
  // Cast passed parameters to correct types
  //
  char *filename              = (char *)value;
  ICPConfiguration *ICPconfig = (ICPConfiguration *)data;

  //
  // Determine if data is locked, if so defer update action
  //
  if (!startup && !ICPconfig->Lock()) {
    // Build retry continuation
    ICPConfigUpdateCont *rh = new ICPConfigUpdateCont(data, value);
    SET_CONTINUATION_HANDLER(rh, (ICPCfgContHandler)&ICPConfigUpdateCont::RetryICPconfigUpdate);
    eventProcessor.schedule_in(rh, HRTIME_MSECONDS(ICPConfigUpdateCont::RETRY_INTERVAL), ET_ICP);
    return EVENT_DONE;
  }
  ICP_INCREMENT_DYN_STAT(config_mgmt_callouts_stat);
  ICPconfig->_icp_config_callouts++;

  //
  // Allocate working buffer for PeerConfigData[]
  //
  PeerConfigData *P = new PeerConfigData[MAX_DEFINED_PEERS + 1];

  //
  // Build pathname to "icp.config" and open file
  //
  ink_release_assert(filename != NULL);

  ats_scoped_str config_path(Layout::get()->relative_to(Layout::get()->sysconfdir, filename));
  int fd = open(config_path, O_RDONLY);
  if (fd < 0) {
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, open failed");
    delete[] P;
    return EVENT_DONE;
  }
  //***********************************************************************
  // Parse records in "icp.config"
  //  Each line is formatted as follows with ":" separator for each field
  //    - hostname (string)           -- Identifier for entry
  //    - host_ip_str (string)        -- decimal dot notation
  //    - ctype (int)                 -- 1=Parent, 2=Sibling, 3=Local
  //    - proxy_port (int)            -- TCP Port #
  //    - icp_port (int)              -- UDP Port #
  //    - multicast_member            -- 0=No 1=Yes
  //    - multicast_ip_str (string)   -- decimal dot notation
  //    - multicast_ttl (int)         -- (1 - 2; default 1)
  //***********************************************************************
  const int colons_per_entry = 8; // expected ':' separators per entry

  int error = 0;
  int ln    = 0;
  int n_colons;
  char line[512];
  char *cur;
  char *next;
  char *p;
  char fs = ':'; // field separator.
  int len;       // length of current input line (original).

  int n = 1; // Note: Entry zero reserved for "localhost" data

  //////////////////////////////////////////////////////////////////////
  // Read and parse "icp.config" entries.
  //
  // Note: ink_file_fd_readline() null terminates returned buffer
  //////////////////////////////////////////////////////////////////////
  while ((len = ink_file_fd_readline(fd, sizeof(line) - 1, line)) > 0) {
    ln++;
    cur = line;
    while (isspace(*cur))
      ++cur, --len; // skip leading space.
    if (!*cur || *cur == '#')
      continue;

    if (n >= MAX_DEFINED_PEERS) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, maximum peer entries exceeded");
      error = 1;
      break;
    }
    //***********************************
    // Verify general syntax of entry
    //***********************************
    /* Ugly. The original field separator was colon, but we can't have that
       if we want to support IPv6. So - since each line is required to have a
       separator at the end of the line, we look there and require it to be
       consistent. It still must be an acceptable character.
    */
    char *last = cur + len - 1; // last character.
    if ('\n' == *last)
      --last; // back over trailing LF.
    if (NULL == strchr(" ;:|,", *last)) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, invalid separator [value %d]", *last);
      error = 1;
      break;
    }
    fs = *last;

    n_colons = 0;
    p        = cur;
    while (0 != (p = next_field(p, fs))) {
      ++p;
      ++n_colons;
    }
    if (n_colons != colons_per_entry) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, invalid syntax, line %d: expected %d fields, found %d", ln,
                       colons_per_entry, n_colons);
      error = 1;
      break;
    }
    //*******************
    // Extract hostname
    //*******************
    next    = next_field(cur, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      ink_strlcpy(P[n]._hostname, cur, PeerConfigData::HOSTNAME_SIZE);
    } else {
      P[n]._hostname[0] = 0;
    }
    //*********************
    // Extract host_ip_str
    //*********************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      if (0 != P[n]._ip_addr.load(cur)) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad host ip_addr, line %d", ln);
        error = 1;
        break;
      }
    } else {
      P[n]._ip_addr.invalidate();
    }

    if (!P[n]._hostname[0] && !P[n]._ip_addr.isValid()) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad hostname, line %d", ln);
      error = 1;
      break;
    }
    //******************
    // Extract ctype
    //******************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      P[n]._ctype = atoi(cur);
      if ((P[n]._ctype != PeerConfigData::CTYPE_PARENT) && (P[n]._ctype != PeerConfigData::CTYPE_SIBLING) &&
          (P[n]._ctype != PeerConfigData::CTYPE_LOCAL)) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad ctype, line %d", ln);
        error = 1;
        break;
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, 2bad ctype, line %d", ln);
      error = 1;
      break;
    }
    //*********************
    // Extract proxy_port
    //*********************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      if ((P[n]._proxy_port = atoi(cur)) <= 0) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad proxy_port, line %d", ln);
        error = 1;
        break;
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, 2bad proxy_port, line %d", ln);
      error = 1;
      break;
    }
    //*********************
    // Extract icp_port
    //*********************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      if ((P[n]._icp_port = atoi(cur)) <= 0) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad icp_port, line %d", ln);
        error = 1;
        break;
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, 2bad icp_port, line %d", ln);
      error = 1;
      break;
    }
    //****************************
    // Extract multicast_member
    //****************************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      if ((P[n]._mc_member = atoi(cur)) < 0) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad mc_member, line %d", ln);
        error = 1;
        break;
      }
      if ((P[n]._mc_member != 0) && (P[n]._mc_member != 1)) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad mc_member (2), line %d", ln);
        error = 1;
        break;
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, 2bad mc_member, line %d", ln);
      error = 1;
      break;
    }
    //****************************
    // Extract multicast_ip_str
    //****************************
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      P[n]._mc_ip_addr.load(cur);
      // Validate only if "multicast_member" is set.
      if (P[n]._mc_member != 0 && !P[n]._mc_ip_addr.isMulticast()) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad multicast ip_addr, line %d", ln);
        error = 1;
        break;
      }
    } else {
      P[n]._mc_ip_addr.invalidate();
    }
    //************************
    // Extract multicast_ttl
    //************************
    // Note: last entry is always terminated with a ":"
    cur     = next;
    next    = next_field(next, fs);
    *next++ = 0;
    if (cur != (next - 1)) {
      P[n]._mc_ttl = atoi(cur);
      if ((P[n]._mc_ttl = atoi(cur)) <= 0) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, bad mc_ttl, line %d", ln);
        error = 1;
        break;
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "read icp.config, 2bad mc_ttl, line %d", ln);
      error = 1;
      break;
    }
    n++; // bump PeerConfigData[] index
  }
  close(fd);

  if (!error) {
    for (int i                           = 0; i <= MAX_DEFINED_PEERS; i++)
      *ICPconfig->_peer_cdata_current[i] = P[i];
  }
  delete[] P; // free working buffer
  if (!startup)
    ICPconfig->Unlock();
  return EVENT_DONE;
}

//-------------------------------------------------------
// Class Peer member functions (abstract base class)
//-------------------------------------------------------
Peer::Peer(PeerType_t t, ICPProcessor *icpPr, bool dynamic_peer)
  : buf(NULL), notFirstRead(0), readAction(NULL), writeAction(NULL), _type(t), _next(0), _ICPpr(icpPr), _state(PEER_UP)
{
  notFirstRead = 0;
  if (dynamic_peer) {
    _state |= PEER_DYNAMIC;
  }
  memset((void *)&this->_stats, 0, sizeof(this->_stats));
  ink_zero(fromaddr);
  fromaddrlen = sizeof(fromaddr);
  _id         = 0;
}

void
Peer::LogRecvMsg(ICPMsg_t *m, int valid)
{
  // Note: ICPMsg_t (m) is in native byte order

  // Note numerous stats on a per peer basis
  _stats.last_receive = Thread::get_hrtime();
  if ((m->h.opcode >= ICP_OP_QUERY) && (m->h.opcode <= ICP_OP_LAST)) {
    _stats.recv[m->h.opcode]++;
  } else {
    _stats.recv[ICP_OP_INVALID]++;
  }
  _stats.total_received++;

  if (!valid) {
    // Message arrived, but ICP request no longer on pending list
    _stats.dropped_replies++;
  }
  if ((_state & PEER_UP) == 0) {
    ip_port_text_buffer ipb;
    // Currently marked down so we still send but do not expect reply.
    // Now mark up so we will wait for reply.
    _state |= PEER_UP;
    _stats.total_received = _stats.total_sent; // restart timeout count

    Debug("icp", "Peer [%s] now back online", ats_ip_nptop(this->GetIP(), ipb, sizeof(ipb)));
  }
}

//---------------------------------------------------------------
// Class ParentSiblingPeer (derived from Peer) member functions
//      ICP object describing Parent or Sibling Peers.
//---------------------------------------------------------------
ParentSiblingPeer::ParentSiblingPeer(PeerType_t t, PeerConfigData *p, ICPProcessor *icpPr, bool dynamic_peer)
  : Peer(t, icpPr, dynamic_peer), _pconfig(p)
{
  ats_ip_set(&_ip.sa, _pconfig->GetIPAddr(), htons(_pconfig->GetICPPort()));
}

int
ParentSiblingPeer::GetProxyPort()
{
  return _pconfig->GetProxyPort();
}

int
ParentSiblingPeer::GetICPPort()
{
  return _pconfig->GetICPPort();
}

sockaddr *
ParentSiblingPeer::GetIP()
{
  // The real data is in _pconfig, but I don't think ever changes so
  // it should be OK to have set this in the constructor.
  return &_ip.sa;
}

Action *
ParentSiblingPeer::SendMsg_re(Continuation *cont, void *token, struct msghdr *msg, sockaddr const *to)
{
  // Note: All sends are funneled through the local peer UDP socket.

  Peer *lp = _ICPpr->GetLocalPeer();

  if (to) {
    // Send to specified host
    Peer *p = _ICPpr->FindPeer(IpAddr(to), ntohs(ats_ip_port_cast(to)));
    ink_assert(p);

    msg->msg_name    = &p->GetSendChan()->addr;
    msg->msg_namelen = ats_ip_size(&p->GetSendChan()->addr);
    Action *a        = udpNet.sendmsg_re(cont, token, lp->GetSendFD(), msg);
    return a;
  } else {
    // Send to default host
    msg->msg_name    = &_chan.addr;
    msg->msg_namelen = ats_ip_size(&_chan.addr.sa);
    Action *a        = udpNet.sendmsg_re(cont, token, lp->GetSendFD(), msg);
    return a;
  }
}

Action *
ParentSiblingPeer::RecvFrom_re(Continuation *cont, void *token, IOBufferBlock *bufblock, int size, struct sockaddr *from,
                               socklen_t *fromlen)
{
  // Note: All receives are funneled through the local peer UDP socket.

  Peer *lp  = _ICPpr->GetLocalPeer();
  Action *a = udpNet.recvfrom_re(cont, token, lp->GetRecvFD(), from, fromlen, bufblock, size, true, 0);
  return a;
}

int
ParentSiblingPeer::GetRecvFD()
{
  return _chan.fd;
}

int
ParentSiblingPeer::GetSendFD()
{
  return _chan.fd;
}

int
ParentSiblingPeer::ExpectedReplies(BitMap *expected_replies_list)
{
  if (((_state & PEER_UP) == 0) || ((_stats.total_sent - _stats.total_received) > Peer::OFFLINE_THRESHOLD)) {
    if (_state & PEER_UP) {
      ip_port_text_buffer ipb;
      _state &= ~PEER_UP;
      Debug("icp", "Peer [%s] marked offline", ats_ip_nptop(this->GetIP(), ipb, sizeof(ipb)));
    }
    //
    // We will continue to send messages, but will not wait for a reply
    // until we receive a response.
    //
    return 0;
  } else {
    expected_replies_list->SetBit(this->GetPeerID());
    return 1;
  }
}

int
ParentSiblingPeer::ValidSender(sockaddr *fr)
{
  if (_type == PEER_LOCAL) {
    //
    // We are currently funneling all unicast receives
    // through the local peer UDP socket.  As long as
    // the sender is known within the ICP configuration,
    // consider it valid.
    //
    Peer *p = _ICPpr->FindPeer(fr);
    if (p) {
      return 1; // Valid sender
    } else {
      return 0; // Invalid sender
    }

  } else {
    // Make sure the sockaddr_in corresponds to this peer
    // Need to update once we have support for comparing address
    // and port in a socakddr.
    if (ats_ip_addr_eq(this->GetIP(), fr) && (ats_ip_port_cast(this->GetIP()) == ats_ip_port_cast(fr))) {
      return 1; // Sender is this peer
    } else {
      return 0; // Sender is not this peer
    }
  }
}

void
ParentSiblingPeer::LogSendMsg(ICPMsg_t *m, sockaddr const * /* sa ATS_UNUSED */)
{
  // Note: ICPMsg_t (m) is in network byte order

  // Note numerous stats on a per peer basis
  _stats.last_send = Thread::get_hrtime();
  _stats.sent[m->h.opcode]++;
  _stats.total_sent++;
}

int
ParentSiblingPeer::ExtToIntRecvSockAddr(sockaddr const *in, sockaddr *out)
{
  Peer *p = _ICPpr->FindPeer(IpAddr(in));
  if (p && (p->GetType() != PEER_LOCAL)) {
    // Map from received (ip, port) to defined (ip, port).
    ats_ip_copy(out, p->GetIP());
    return 1;
  } else {
    return 0;
  }
}

//-----------------------------------------------------------
// Class MultiCastPeer (derived from Peer) member functions
//      ICP object describing MultiCast Peers.
//-----------------------------------------------------------
MultiCastPeer::MultiCastPeer(IpAddr const &addr, uint16_t mc_port, int ttl, ICPProcessor *icpPr)
  : Peer(PEER_MULTICAST, icpPr), _mc_ttl(ttl)
{
  ats_ip_set(&_mc_ip.sa, addr, htons(mc_port));
  memset(&this->_mc, 0, sizeof(this->_mc));
}

int
MultiCastPeer::GetTTL()
{
  return _mc_ttl;
}

sockaddr *
MultiCastPeer::GetIP()
{
  return &_mc_ip.sa;
}

Action *
MultiCastPeer::SendMsg_re(Continuation *cont, void *token, struct msghdr *msg, sockaddr const *to)
{
  Action *a;

  if (to) {
    // Send to MultiCast group member (UniCast)
    Peer *p = FindMultiCastChild(IpAddr(to), ats_ip_port_host_order(to));
    ink_assert(p);
    a = ((ParentSiblingPeer *)p)->SendMsg_re(cont, token, msg, 0);
  } else {
    // Send to MultiCast group
    msg->msg_name    = (caddr_t)&_send_chan.addr;
    msg->msg_namelen = sizeof(_send_chan.addr);
    a                = udpNet.sendmsg_re(cont, token, _send_chan.fd, msg);
  }
  return a;
}

Action *
MultiCastPeer::RecvFrom_re(Continuation *cont, void *token, IOBufferBlock * /* bufblock ATS_UNUSED */, int len,
                           struct sockaddr *from, socklen_t *fromlen)
{
  Action *a = udpNet.recvfrom_re(cont, token, _recv_chan.fd, from, fromlen, buf, len, true, 0);
  return a;
}

int
MultiCastPeer::GetRecvFD()
{
  return _recv_chan.fd;
}

int
MultiCastPeer::GetSendFD()
{
  return _send_chan.fd;
}

int
MultiCastPeer::ExpectedReplies(BitMap *expected_replies_list)
{
  // TBD: Expected replies should be calculated as a running average
  //      from replies returned from a periodic inquiry message.

  int replies          = 0;
  ParentSiblingPeer *p = (ParentSiblingPeer *)this->_next;
  while (p) {
    replies += p->ExpectedReplies(expected_replies_list);
    p = (ParentSiblingPeer *)p->GetNext();
  }
  return replies;
}

int
MultiCastPeer::ValidSender(sockaddr *sa)
{
  // TBD: Use hash function
  // Make sure sockaddr_in corresponds to a defined peer in the
  //  MultiCast group.
  Peer *P = _next;
  while (P) {
    if (ats_ip_addr_eq(P->GetIP(), sa) && (ats_ip_port_cast(P->GetIP()) == ats_ip_port_cast(sa))) {
      return 1;
    } else {
      P = P->GetNext();
    }
  }
  return 0;
}

void
MultiCastPeer::LogSendMsg(ICPMsg_t *m, sockaddr const *sa)
{
  // Note: ICPMsg_t (m) is in network byte order
  if (sa) {
    // UniCast send on MultiCast interface, only update stats for
    //  target Peer.
    //
    Peer *p;
    p = FindMultiCastChild(IpAddr(sa), ats_ip_port_host_order(sa));
    if (p)
      ((ParentSiblingPeer *)p)->LogSendMsg(m, sa);

  } else {
    // Note numerous stats on MultiCast peer and each member peer
    _stats.last_send = Thread::get_hrtime();
    _stats.sent[m->h.opcode]++;
    _stats.total_sent++;

    Peer *p = _next;
    while (p) {
      ((ParentSiblingPeer *)p)->LogSendMsg(m, sa);
      p = p->GetNext();
    }
  }
}

int
MultiCastPeer::IsOnline()
{
  return (_ICPpr->GetConfig()->globalConfig()->ICPmulticastConfigured());
}

int
MultiCastPeer::AddMultiCastChild(Peer *P)
{
  // Add (Peer *) to the given MultiCast structure.
  // Make sure child (ip,port) is unique.
  sockaddr const *ip = P->GetIP();
  if (FindMultiCastChild(IpAddr(ip), ats_ip_port_host_order(ip))) {
    ip_text_buffer x;
    Warning("bad icp.config, multiple multicast child definitions for ip=%s", ats_ip_ntop(ip, x, sizeof(x)));
    return 0; // Not added, already exists
  } else {
    P->SetNext(this->_next);
    this->_next = P;
    ++_mc.defined_members;
    return 1; // Added
  }
}

Peer *
MultiCastPeer::FindMultiCastChild(IpAddr const &addr, uint16_t port)
{
  // Locate child (Peer *) with the given (ip,port). This is split out
  // rather than using a sockaddr so we can indicate the port is to not
  // be checked (@a port == 0).
  Peer *curP = this->_next;
  while (curP) {
    sockaddr const *peer_ip = curP->GetIP();
    if (addr == peer_ip && (!port || port == ats_ip_port_host_order(peer_ip))) {
      return curP;
    } else {
      curP = curP->GetNext();
    }
  }
  return NULL;
}

//-------------------------------------------------------------------------
// Class PeriodicCont member functions (abstract base class)
//      Look for TS ICP configuration changes by periodically looking.
//-------------------------------------------------------------------------
typedef int (ICPPeriodicCont::*ICPPeriodicContHandler)(int, void *);
PeriodicCont::PeriodicCont(ICPProcessor *icpP) : Continuation(0), _ICPpr(icpP)
{
  mutex = new_ProxyMutex();
}

PeriodicCont::~PeriodicCont()
{
  mutex = 0;
}

//-----------------------------------------
// Class ICPPeriodicCont member functions
//-----------------------------------------
ICPPeriodicCont::ICPPeriodicCont(ICPProcessor *icpP)
  : PeriodicCont(icpP), _last_icp_config_callouts(0), _global_config_changed(0), _peer_config_changed(0)
{
}

int
ICPPeriodicCont::PeriodicEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  int do_reconfig     = 0;
  ICPConfiguration *C = _ICPpr->GetConfig();

  if (C->GlobalConfigChange())
    do_reconfig = 1;

  int configcallouts = C->ICPConfigCallouts();
  if (_last_icp_config_callouts != configcallouts) {
    // We have a "icp.config" change callout which we
    //  have not processed.
    _last_icp_config_callouts = configcallouts;
    do_reconfig               = 1;
  }

  if (do_reconfig) {
    //
    // We have a configuration change, create worker continuation.
    //
    ICPPeriodicCont *rc = new ICPPeriodicCont(_ICPpr);
    SET_CONTINUATION_HANDLER(rc, (ICPPeriodicContHandler)&ICPPeriodicCont::DoReconfigAction);
    eventProcessor.schedule_imm(rc);
  }
  return EVENT_CONT;
}

int
ICPPeriodicCont::DoReconfigAction(int event, Event *e)
{
  //************************************************************
  // Initiate reconfiguration action if any global or peer
  // configuration changes have occured.
  //************************************************************
  ICPConfiguration *C = _ICPpr->GetConfig();

  for (;;) {
    switch (event) {
    case EVENT_IMMEDIATE:
    case EVENT_INTERVAL: {
      ink_assert(!_global_config_changed && !_peer_config_changed);
      if (C->Lock()) {
        ICP_INCREMENT_DYN_STAT(reconfig_polls_stat);
        if (C->GlobalConfigChange()) {
          _global_config_changed = 1;
        }
        //
        // TS Configuration management makes callouts whenever changes
        // are made to "icp.config", which describes the ICP peer
        // configuration.
        //
        if (C->PeerConfigChange()) {
          _peer_config_changed = 1;
        }
        if (_global_config_changed || _peer_config_changed) {
          //
          // Start the reconfiguration sequence.
          //
          ICP_INCREMENT_DYN_STAT(reconfig_events_stat);
          ICPProcessor::ReconfigState_t NextState;

          NextState = _ICPpr->ReconfigureStateMachine(ICPProcessor::RC_RECONFIG, _global_config_changed, _peer_config_changed);
          if (NextState == ICPProcessor::RC_DONE) {
            // Completed all reconfiguration actions.
            // ReconfigureStateMachine() has invoked C->Unlock()
            delete this;
            return EVENT_DONE;
          } else {
            // Delay and restart update.
            _global_config_changed = 0;
            _peer_config_changed   = 0;
            C->Unlock();
            e->schedule_in(HRTIME_MSECONDS(RETRY_INTERVAL_MSECS));
            return EVENT_CONT;
          }

        } else {
          // No configuration changes detected.
          C->Unlock();
        }

      } else {
        // Missed lock, retry later
        e->schedule_in(HRTIME_MSECONDS(RETRY_INTERVAL_MSECS));
        return EVENT_CONT;
      }
      delete this;
      return EVENT_DONE;
    }
    default: {
      ink_release_assert(!"ICPPeriodicCont::DoReconfigAction() bad event");
    }
    } // End of switch
  }   // End of for

  return EVENT_DONE;
}

//----------------------------------------------------------------
// Class ICPlog member functions
//  Basic accessor object used by the new logging subsystem
//  for squid access log data for ICP queries.
//----------------------------------------------------------------
ink_hrtime
ICPlog::GetElapsedTime()
{
  return (Thread::get_hrtime() - _s->_start_time);
}

sockaddr const *
ICPlog::GetClientIP()
{
  return &_s->_sender.sa;
}

in_port_t
ICPlog::GetClientPort()
{
  return _s->_sender.port();
}

SquidLogCode
ICPlog::GetAction()
{
  if (_s->_queryResult == CACHE_EVENT_LOOKUP)
    return SQUID_LOG_UDP_HIT;
  else
    return SQUID_LOG_UDP_MISS;
}

const char *
ICPlog::GetCode()
{
  static const char *const ICPCodeStr = "000";
  return ICPCodeStr;
}

int
ICPlog::GetSize()
{
  return ntohs(_s->_rICPmsg->h.msglen);
}

const char *
ICPlog::GetMethod()
{
  return HTTP_METHOD_ICP_QUERY;
}

const char *
ICPlog::GetURI()
{
  return (const char *)_s->_rICPmsg->un.query.URL;
}

const char *
ICPlog::GetIdent()
{
  static const char *const ICPidentStr = "";
  return ICPidentStr;
}

SquidHierarchyCode
ICPlog::GetHierarchy()
{
  return SQUID_HIER_NONE;
}

const char *
ICPlog::GetFromHost()
{
  static const char *const FromHostStr = "";
  return FromHostStr;
}

const char *
ICPlog::GetContentType()
{
  static const char *const ICPcontentTypeStr = "";
  return ICPcontentTypeStr;
}

//*****************************************************************************
// ICP Debug support.
//*****************************************************************************
//
static const char *ICPstatNames[] = {"icp_stat_def",
                                     "config_mgmt_callouts_stat",
                                     "reconfig_polls_stat",
                                     "reconfig_events_stat",
                                     "invalid_poll_data_stat",
                                     "no_data_read_stat",
                                     "short_read_stat",
                                     "invalid_sender_stat",
                                     "read_not_v2_icp_stat",
                                     "icp_remote_query_requests_stat",
                                     "icp_remote_responses_stat",
                                     "icp_cache_lookup_success_stat",
                                     "icp_cache_lookup_fail_stat",
                                     "query_response_write_stat",
                                     "query_response_partial_write_stat",
                                     "no_icp_request_for_response_stat",
                                     "icp_response_request_nolock_stat",
                                     "icp_start_icpoff_stat",
                                     "send_query_partial_write_stat",
                                     "icp_queries_no_expected_replies_stat",
                                     "icp_query_hits_stat",
                                     "icp_query_misses_stat",
                                     "invalid_icp_query_response_stat",
                                     "icp_query_requests_stat",
                                     "total_icp_response_time_stat",
                                     "total_udp_send_queries_stat",
                                     "total_icp_request_time_stat",
                                     "icp_total_reloads",
                                     "icp_pending_reloads",
                                     "icp_reload_start_aborts",
                                     "icp_reload_connect_aborts",
                                     "icp_reload_read_aborts",
                                     "icp_reload_write_aborts",
                                     "icp_reload_successes",
                                     "icp_stat_count",
                                     ""};

void
dumpICPstatEntry(int i, const char *name)
{
  int l = strlen(name);
  int64_t sval, cval;

  RecRawStat *p = RecGetGlobalRawStatPtr(icp_rsb, i);
  sval          = p->sum;
  cval          = p->count;

  printf("%-32s %12" PRId64 " %16" PRId64 " %17.4f\n", &name[l > 31 ? l - 31 : 0], cval, sval,
         cval ? (((double)sval) / ((double)cval)) : 0.0);
}

void
dumpICPstats()
{
  printf("\n");
  int i;
  for (i = 0; i < icp_stat_count; ++i) {
    dumpICPstatEntry(i, ICPstatNames[i]);
  }
}

void
ICPProcessor::DumpICPConfig()
{
  Peer *P;
  PeerType_t type;
  int id;
  ip_port_text_buffer ipb;

  Debug("icp", "On=%d, MultiCast=%d, Timeout=%d LocalCacheLookup=%d", GetConfig()->globalConfig()->ICPconfigured(),
        GetConfig()->globalConfig()->ICPmulticastConfigured(), GetConfig()->globalConfig()->ICPqueryTimeout(),
        GetConfig()->globalConfig()->ICPLocalCacheLookup());
  Debug("icp", "StaleLookup=%d, ReplyToUnknowPeer=%d, DefaultReplyPort=%d", GetConfig()->globalConfig()->ICPStaleLookup(),
        GetConfig()->globalConfig()->ICPReplyToUnknownPeer(), GetConfig()->globalConfig()->ICPDefaultReplyPort());

  for (int i = 0; i < (_nPeerList + 1); i++) {
    P    = _PeerList[i];
    id   = P->GetPeerID();
    type = P->GetType();
    const char *str_type;

    switch (type) {
    case PEER_PARENT: {
      str_type = "P";
      break;
    }
    case PEER_SIBLING: {
      str_type = "S";
      break;
    }
    case PEER_LOCAL: {
      str_type = "L";
      break;
    }
    case PEER_MULTICAST: {
      str_type = "M";
      break;
    }
    default: {
      str_type = "N";
      break;
    }
    } // End of switch

    if (*str_type == 'M') {
      Debug("icp", "[%d]: Type=%s IP=%s", id, str_type, ats_ip_nptop(P->GetIP(), ipb, sizeof(ipb)));
    } else {
      ParentSiblingPeer *Pps = static_cast<ParentSiblingPeer *>(P);
      Debug("icp", "[%d]: Type=%s IP=%s PPort=%d Host=%s", id, str_type, ats_ip_nptop(P->GetIP(), ipb, sizeof(ipb)),
            Pps->GetConfig()->GetProxyPort(), Pps->GetConfig()->GetHostname());

      Debug("icp", "[%d]: MC ON=%d MC_IP=%s MC_TTL=%d", id, Pps->GetConfig()->MultiCastMember(),
            Pps->GetConfig()->GetMultiCastIPAddr().toString(ipb, sizeof(ipb)), Pps->GetConfig()->GetMultiCastTTL());
    }
  }
}

//*****************************************************************************

// End of ICPConfig.cc
