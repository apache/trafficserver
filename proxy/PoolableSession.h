/** @file

  PoolableSession - class that extends ProxySession so that they can be cataloged for reuse.

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

#pragma once

#include "ProxySession.h"

class PoolableSession : public ProxySession
{
  using self_type  = PoolableSession;
  using super_type = ProxySession;

public:
  enum PooledState {
    INIT,
    SSN_IN_USE,    // actively in use
    KA_RESERVED,   // stuck to client
    KA_POOLED,     // free for reuse
    SSN_CLOSED,    // Session ready to be freed
    SSN_TO_RELEASE // Session reaady to be released
  };

  /// Hash map descriptor class for IP map.
  struct IPLinkage {
    self_type *_next = nullptr;
    self_type *_prev = nullptr;

    static self_type *&next_ptr(self_type *);
    static self_type *&prev_ptr(self_type *);
    static uint32_t hash_of(sockaddr const *key);
    static sockaddr const *key_of(self_type const *ssn);
    static bool equal(sockaddr const *lhs, sockaddr const *rhs);
    // Add a couple overloads for internal convenience.
    static bool equal(sockaddr const *lhs, PoolableSession const *rhs);
    static bool equal(PoolableSession const *lhs, sockaddr const *rhs);
  } _ip_link;

  /// Hash map descriptor class for FQDN map.
  struct FQDNLinkage {
    self_type *_next = nullptr;
    self_type *_prev = nullptr;

    static self_type *&next_ptr(self_type *);
    static self_type *&prev_ptr(self_type *);
    static uint64_t hash_of(CryptoHash const &key);
    static CryptoHash const &key_of(self_type *ssn);
    static bool equal(CryptoHash const &lhs, CryptoHash const &rhs);
  } _fqdn_link;

  CryptoHash hostname_hash;
  PooledState state = INIT;

  // Copy of the owning SM's server session sharing settings
  TSServerSessionSharingMatchMask sharing_match = TS_SERVER_SESSION_SHARING_MATCH_MASK_NONE;
  TSServerSessionSharingPoolType sharing_pool   = TS_SERVER_SESSION_SHARING_POOL_GLOBAL;

  void enable_outbound_connection_tracking(OutboundConnTrack::Group *group);
  void release_outbound_connection_tracking();

  void attach_hostname(const char *hostname);

  void set_active();
  bool is_active();
  void set_private(bool new_private = true);
  bool is_private() const;

  virtual void set_netvc(NetVConnection *newvc);

  // Used to determine whether the session is for parent proxy
  // it is session to origin server
  // We need to determine whether a closed connection was to
  // close parent proxy to update the
  // proxy.process.http.current_parent_proxy_connections
  bool to_parent_proxy = false;

  // Keep track of connection limiting and a pointer to the
  // singleton that keeps track of the connection counts.
  OutboundConnTrack::Group *conn_track_group = nullptr;

  virtual IOBufferReader *get_remote_reader() = 0;

private:
  // Sessions become if authentication headers
  //  are sent over them
  bool private_session = false;
};

inline void
PoolableSession::set_active()
{
  state = SSN_IN_USE;
}
inline bool
PoolableSession::is_active()
{
  return state == SSN_IN_USE;
}
inline void
PoolableSession::set_private(bool new_private)
{
  private_session = new_private;
}
inline bool
PoolableSession::is_private() const
{
  return private_session;
}

inline void
PoolableSession::set_netvc(NetVConnection *newvc)
{
  ProxySession::_vc = newvc;
}

//
// LINKAGE

inline PoolableSession *&
PoolableSession::IPLinkage::next_ptr(self_type *ssn)
{
  return ssn->_ip_link._next;
}

inline PoolableSession *&
PoolableSession::IPLinkage::prev_ptr(self_type *ssn)
{
  return ssn->_ip_link._prev;
}

inline uint32_t
PoolableSession::IPLinkage::hash_of(sockaddr const *key)
{
  return ats_ip_hash(key);
}

inline sockaddr const *
PoolableSession::IPLinkage::key_of(self_type const *ssn)
{
  return ssn->get_remote_addr();
}

inline bool
PoolableSession::IPLinkage::equal(sockaddr const *lhs, sockaddr const *rhs)
{
  return ats_ip_addr_port_eq(lhs, rhs);
}

inline bool
PoolableSession::IPLinkage::equal(sockaddr const *lhs, PoolableSession const *rhs)
{
  return ats_ip_addr_port_eq(lhs, key_of(rhs));
}

inline bool
PoolableSession::IPLinkage::equal(PoolableSession const *lhs, sockaddr const *rhs)
{
  return ats_ip_addr_port_eq(key_of(lhs), rhs);
}

inline PoolableSession *&
PoolableSession::FQDNLinkage::next_ptr(self_type *ssn)
{
  return ssn->_fqdn_link._next;
}

inline PoolableSession *&
PoolableSession::FQDNLinkage::prev_ptr(self_type *ssn)
{
  return ssn->_fqdn_link._prev;
}

inline uint64_t
PoolableSession::FQDNLinkage::hash_of(CryptoHash const &key)
{
  return key.fold();
}

inline CryptoHash const &
PoolableSession::FQDNLinkage::key_of(self_type *ssn)
{
  return ssn->hostname_hash;
}

inline bool
PoolableSession::FQDNLinkage::equal(CryptoHash const &lhs, CryptoHash const &rhs)
{
  return lhs == rhs;
}

inline void
PoolableSession::enable_outbound_connection_tracking(OutboundConnTrack::Group *group)
{
  ink_assert(nullptr == conn_track_group);
  conn_track_group = group;
}

inline void
PoolableSession::release_outbound_connection_tracking()
{
  // Update upstream connection tracking data if present.
  if (conn_track_group) {
    if (conn_track_group->_count >= 0) {
      (conn_track_group->_count)--;
      conn_track_group = nullptr;
    } else {
      // A bit dubious, as there's no guarantee it's still negative, but even that would be interesting to know.
      Error("[http_ss] [%" PRId64 "] number of connections should be greater than or equal to zero: %u", con_id,
            conn_track_group->_count.load());
    }
  }
}

inline void
PoolableSession::attach_hostname(const char *hostname)
{
  if (CRYPTO_HASH_ZERO == hostname_hash) {
    CryptoContext().hash_immediate(hostname_hash, (unsigned char *)hostname, strlen(hostname));
  }
}
