/** @file WCCP End Point class implementation.

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

#include <errno.h>

#include "swoc/bwf_base.h"
#include "swoc/bwf_ex.h"
#include "swoc/IPAddr.h"
#include "swoc/bwf_ip.h"

#include "WccpLocal.h"
#include "WccpUtil.h"
#include "WccpMeta.h"
#include "tscore/ink_string.h"
#include "tscore/ink_defs.h"

using swoc::Errata;
// ------------------------------------------------------
namespace wccp
{
#if defined IP_RECVDSTADDR
#define DSTADDR_SOCKOPT  IP_RECVDSTADDR
#define DSTADDR_DATASIZE (CMSG_SPACE(sizeof(struct in_addr)))
#define dstaddr(x)       (CMSG_DATA(x))
#elif defined IP_PKTINFO
#define DSTADDR_SOCKOPT  IP_PKTINFO
#define DSTADDR_DATASIZE (CMSG_SPACE(sizeof(struct in_pktinfo)))
#define dstaddr(x)       (&(((struct in_pktinfo *)(CMSG_DATA(x)))->ipi_addr))
#else
#error "can't determine socket option"
#endif

// ------------------------------------------------------
Impl::GroupData &
Impl::GroupData::setKey(const char *key)
{
  if ((m_use_security_key = (key != nullptr))) {
    ink_strlcpy(m_security_key, key, SecurityComp::KEY_SIZE);
  }
  return *this;
}

Impl::GroupData &
Impl::GroupData::setSecurity(SecurityOption style)
{
  m_use_security_opt = true;
  m_security_opt     = style;
  return *this;
}

Impl::~Impl()
{
  this->close();
}

Errata
Impl::open(uint addr)
{
  struct sockaddr saddr;
  sockaddr_in &in_addr = reinterpret_cast<sockaddr_in &>(saddr);
  ats_scoped_fd fd;

  if (ts::NO_FD != m_fd) {
    return {ERRATA_NOTE, "Attempted to open already open WCCP Endpoint"};
  }

  if (ts::NO_FD == (fd = socket(PF_INET, SOCK_DGRAM, 0))) {
    return {ERRATA_FATAL, "Failed to create socket - {}", swoc::bwf::Errno()};
  }

  if (INADDR_ANY != addr)
    m_addr = addr; // overridden.
  memset(&saddr, 0, sizeof(saddr));
  in_addr.sin_family      = AF_INET;
  in_addr.sin_port        = htons(DEFAULT_PORT);
  in_addr.sin_addr.s_addr = m_addr;
  int zret                = bind(fd, &saddr, sizeof(saddr));
  if (-1 == zret) {
    swoc::bwf::Errno e; // save this over closing.
    this->close();
    return {ERRATA_FATAL, "Failed to bind socket to port - {}", e};
  }
  bw_log(DL_Note, "Socket bound to {}:{}", swoc::IP4Addr(m_addr), DEFAULT_PORT);

  // Now get the address. Usually the same but possibly different,
  // certainly if addr was INADDR_ANY.
  if (INADDR_ANY == m_addr && INADDR_ANY == (m_addr = Get_Local_Address(fd))) {
    swoc::bwf::Errno e; // save this over closing.
    this->close();
    return {ERRATA_FATAL, "Failed to get local address for socket - {}", e};
  }

  // Enable retrieval of destination address on packets.
  int ip_pktinfo_flag = 1;
  if (-1 == setsockopt(fd, IPPROTO_IP, DSTADDR_SOCKOPT, &ip_pktinfo_flag, sizeof(ip_pktinfo_flag))) {
    swoc::bwf::Errno e; // save this over closing.
    this->close();
    return {ERRATA_FATAL, "Failed to enable destination address retrieval - {}", e};
  }

#if defined IP_MTU_DISCOVER
  /// Disable PMTU on Linux because of a bug in IOS routers.
  /// WCCP packets are rejected as duplicates if the IP fragment
  /// identifier is 0, which is the value used when PMTU is enabled.
  int pmtu = IP_PMTUDISC_DONT;
  if (-1 == setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &pmtu, sizeof(pmtu))) {
    swoc::bwf::Errno e; // save this over closing.
    this->close();
    return {ERRATA_FATAL, "Failed to disable PMTU on WCCP socket - {}", e};
  }
#endif

  m_fd = fd.release();
  return {};
}

void
Impl::close()
{
  if (ts::NO_FD != m_fd) {
    ::close(m_fd);
    m_fd = ts::NO_FD;
  }
}

void
Impl::useMD5Security(std::string_view const key)
{
  m_use_security_opt = true;
  m_security_opt     = SECURITY_MD5;
  m_use_security_key = true;
  memset(m_security_key, 0, SecurityComp::KEY_SIZE);
  // Great. Have to cast or we get a link error.
  memcpy(m_security_key, key.data(), std::min(key.size(), static_cast<size_t>(SecurityComp::KEY_SIZE)));
}

SecurityOption
Impl::setSecurity(BaseMsg &msg, GroupData const &group) const
{
  SecurityOption zret = SECURITY_NONE;
  if (group.m_use_security_opt)
    zret = group.m_security_opt;
  else if (m_use_security_opt)
    zret = m_security_opt;
  if (group.m_use_security_key)
    msg.m_security.setKey(group.m_security_key);
  else if (m_use_security_key)
    msg.m_security.setKey(m_security_key);
  return zret;
}

bool
Impl::validateSecurity(BaseMsg &msg, GroupData const &group)
{
  SecurityOption opt = msg.m_security.getOption();
  if (group.m_use_security_opt) {
    if (opt != group.m_security_opt)
      return false;
  } else if (m_use_security_opt) {
    if (opt != m_security_opt)
      return false;
  }
  if (opt == SECURITY_MD5) {
    if (group.m_use_security_key)
      msg.m_security.setKey(group.m_security_key);
    else if (m_use_security_key)
      msg.m_security.setKey(m_security_key);
    return msg.validateSecurity();
  }
  return true;
}

swoc::Errata
Impl::handleMessage()
{
  ssize_t n;                // recv byte count.
  struct sockaddr src_addr; // sender's address.
  msghdr recv_hdr;
  iovec recv_buffer;
  IpHeader ip_header;
  static ssize_t const BUFFER_SIZE = 65536;
  char buffer[BUFFER_SIZE];
  static size_t const ANC_BUFFER_SIZE = DSTADDR_DATASIZE;
  char anc_buffer[ANC_BUFFER_SIZE];

  if (ts::NO_FD == m_fd) {
    return {ec_for(ENOTCONN), ERRATA_NOTE, "No open socket."};
  }

  recv_buffer.iov_base = buffer;
  recv_buffer.iov_len  = BUFFER_SIZE;

  recv_hdr.msg_name       = &src_addr;
  recv_hdr.msg_namelen    = sizeof(src_addr);
  recv_hdr.msg_iov        = &recv_buffer;
  recv_hdr.msg_iovlen     = 1;
  recv_hdr.msg_control    = anc_buffer;
  recv_hdr.msg_controllen = ANC_BUFFER_SIZE;
  recv_hdr.msg_flags      = 0; // output only, make Coverity shut up.

  // coverity[uninit_use_in_call]
  n = recvmsg(m_fd, &recv_hdr, MSG_TRUNC);
  if (n > BUFFER_SIZE)
    return {ec_for(EMSGSIZE), ERRATA_ERROR, "Internal buffer is too small"};
  else if (n < 0)
    return {ec_for(), ERRATA_ERROR, "Network error"};

  // Extract the original destination address.
  ip_header.m_src = access_field(&sockaddr_in::sin_addr, &src_addr).s_addr;
  for (cmsghdr *anc = CMSG_FIRSTHDR(&recv_hdr); anc; anc = CMSG_NXTHDR(&recv_hdr, anc)) {
    if (anc->cmsg_level == IPPROTO_IP && anc->cmsg_type == DSTADDR_SOCKOPT) {
      ip_header.m_dst = ((struct in_addr *)dstaddr(anc))->s_addr;
      break;
    }
  }

  // Check to see if there is a valid header.
  MsgHeaderComp header;
  MsgBuffer msg_buffer(buffer_type(buffer, n));
  if (PARSE_SUCCESS == header.parse(msg_buffer)) {
    message_type_t msg_type = header.getType();
    buffer_type chunk(buffer, n);

    switch (msg_type) {
    case HERE_I_AM:
      this->handleHereIAm(ip_header, chunk);
      break;
    case I_SEE_YOU:
      this->handleISeeYou(ip_header, chunk);
      break;
    case REDIRECT_ASSIGN:
      this->handleRedirectAssign(ip_header, chunk);
      break;
    case REMOVAL_QUERY:
      this->handleRemovalQuery(ip_header, chunk);
      break;
    default:
      fprintf(stderr, "Unknown message type %d ignored.\n", msg_type);
      break;
    };
  } else {
    fprintf(stderr, "Malformed message ignored.\n");
  }
  return {};
}

Errata
Impl::handleHereIAm(IpHeader const &, buffer_type const &)
{
  return {ERRATA_NOTE, "Unanticipated WCCP2_HERE_I_AM message ignored"};
}
Errata
Impl::handleISeeYou(IpHeader const &, buffer_type const & /* data ATS_UNUSED */)
{
  return {ERRATA_NOTE, "Unanticipated WCCP2_I_SEE_YOU message ignored."};
}
Errata
Impl::handleRedirectAssign(IpHeader const &, buffer_type const & /* data ATS_UNUSED */)
{
  return {ERRATA_NOTE, "Unanticipated WCCP2_REDIRECT_ASSIGN message ignored."};
}
Errata
Impl::handleRemovalQuery(IpHeader const &, buffer_type const & /* data ATS_UNUSED */)
{
  return {ERRATA_NOTE, "Unanticipated WCCP2_REMOVAL_QUERY message ignored."};
}
// ------------------------------------------------------
CacheImpl::GroupData::GroupData() : m_proc_name(nullptr), m_assignment_pending(false) {}

CacheImpl::GroupData &
CacheImpl::GroupData::seedRouter(uint32_t addr)
{
  // Be nice and don't add it if it's already there.
  if (m_seed_routers.end() == find_by_member(m_seed_routers, &SeedRouter::m_addr, addr))
    m_seed_routers.push_back(SeedRouter(addr));
  return *this;
}

time_t
CacheImpl::GroupData::removeSeedRouter(uint32_t addr)
{
  time_t zret                             = 0;
  std::vector<SeedRouter>::iterator begin = m_seed_routers.begin();
  std::vector<SeedRouter>::iterator end   = m_seed_routers.end();
  std::vector<SeedRouter>::iterator spot  = std::find_if(begin, end, ts::predicate(&SeedRouter::m_addr, addr));

  if (end != spot) {
    zret = spot->m_xmit;
    m_seed_routers.erase(spot);
  }

  return zret;
}

CacheImpl::GroupData &
CacheImpl::GroupData::setKey(const char *key)
{
  return static_cast<self_type &>(this->super_type::setKey(key));
}

CacheImpl::GroupData &
CacheImpl::GroupData::setSecurity(SecurityOption style)
{
  return static_cast<self_type &>(this->super_type::setSecurity(style));
}

CacheImpl::CacheBag::iterator
CacheImpl::GroupData::findCache(uint32_t addr)
{
  return std::find_if(m_caches.begin(), m_caches.end(), ts::predicate(&CacheData::idAddr, addr));
}

CacheImpl::RouterBag::iterator
CacheImpl::GroupData::findRouter(uint32_t addr)
{
  return std::find_if(m_routers.begin(), m_routers.end(), ts::predicate(&RouterData::m_addr, addr));
}

void
CacheImpl::GroupData::resizeCacheSources()
{
  int count = m_routers.size();
  for (CacheBag::iterator spot = m_caches.begin(), limit = m_caches.end(); spot != limit; ++spot) {
    spot->m_src.resize(count);
  }
}

inline CacheImpl::RouterData::RouterData() : m_addr(0), m_generation(0), m_rapid(0), m_assign(false), m_send_caps(false) {}

inline CacheImpl::RouterData::RouterData(uint32_t addr)
  : m_addr(addr), m_generation(0), m_rapid(0), m_assign(false), m_send_caps(false)
{
}

time_t
CacheImpl::RouterData::pingTime(time_t now) const
{
  time_t tx = m_xmit.m_time + (m_rapid ? TIME_UNIT / 10 : TIME_UNIT);
  return tx < now ? 0 : tx - now;
}

time_t
CacheImpl::RouterData::waitTime(time_t now) const
{
  return m_assign ? 0 : this->pingTime(now);
}

uint32_t
detail::cache::CacheData::idAddr() const
{
  return m_id.getAddr();
}

CacheImpl::GroupData &
CacheImpl::defineServiceGroup(ServiceGroup const &svc, ServiceGroup::Result *result)
{
  uint8_t svc_id          = svc.getSvcId();
  GroupMap::iterator spot = m_groups.find(svc_id);
  GroupData *group; // service with target ID.
  ServiceGroup::Result zret;
  if (spot == m_groups.end()) { // not defined
    group        = &(m_groups[svc_id]);
    group->m_svc = svc;
    group->m_id.initDefaultHash(m_addr);
    zret = ServiceGroup::DEFINED;
  } else {
    group = &spot->second;
    zret  = group->m_svc == svc ? ServiceGroup::EXISTS : ServiceGroup::CONFLICT;
  }
  if (result)
    *result = zret;
  return *group;
}

time_t
CacheImpl::GroupData::waitTime(time_t now) const
{
  time_t zret = std::numeric_limits<time_t>::max();
  // Active routers.
  for (RouterBag::const_iterator router = m_routers.begin(), router_limit = m_routers.end(); router != router_limit && zret;
       ++router) {
    zret = std::min(zret, router->waitTime(now));
  }
  // Seed routers.
  for (std::vector<SeedRouter>::const_iterator router = m_seed_routers.begin(), router_limit = m_seed_routers.end();
       router != router_limit && zret; ++router) {
    time_t tx = router->m_xmit + TIME_UNIT;
    if (tx < now)
      zret = 0;
    else
      zret = std::min(tx - now, zret);
  }
  // Assignment
  if (m_assignment_pending) {
    time_t tx = m_generation_time + (3 * TIME_UNIT / 2);
    if (tx < now)
      zret = 0;
    else
      zret = std::min(tx - now, zret);
  }

  return zret;
}

bool
CacheImpl::GroupData::processUp()
{
  bool zret                 = false;
  const char *proc_pid_path = this->getProcName();
  if (proc_pid_path == NULL || proc_pid_path[0] == '\0') {
    zret = true; // No process to track, always chatter
  } else {
    // Look for the pid file
    ats_scoped_fd fd{open(proc_pid_path, O_RDONLY)};
    if (fd >= 0) {
      char buffer[256];
      ssize_t read_count = read(fd, buffer, sizeof(buffer) - 1);
      if (read_count > 0) {
        buffer[read_count] = '\0';
        int pid            = atoi(buffer);
        if (pid > 0) {
          // If the process is still running, it has an entry in the proc file system, (Linux only)
          snprintf(buffer, sizeof(buffer), "/proc/%d/status", pid);
          ats_scoped_fd fd2{open(buffer, O_RDONLY)};
          if (fd2 >= 0) {
            zret = true;
          }
        }
      }
    }
  }
  return zret;
}

bool
CacheImpl::GroupData::cullRouters(time_t now)
{
  bool zret  = false;
  size_t idx = 0, n = m_routers.size();
  while (idx < n) {
    RouterData &router = m_routers[idx];
    if (router.m_recv.m_time + TIME_UNIT * 3 < now) {
      uint32_t addr = router.m_addr;
      // Clip the router by copying down and resizing.
      // Must do all caches as well.
      --n; // Decrement router counter first.
      if (idx < n)
        router = m_routers[n];
      m_routers.resize(n);
      for (CacheBag::iterator cache = m_caches.begin(), cache_limit = m_caches.end(); cache != cache_limit; ++cache) {
        if (idx < n)
          cache->m_src[idx] = cache->m_src[n];
        cache->m_src.resize(n);
      }
      // Put it back in the seeds.
      this->seedRouter(addr);
      zret = true; // Router was culled, report it to caller.
      bw_log(DL_Note, "Router {:x} timed out and was removed from the active list.", swoc::IP4Addr(addr));
    } else {
      ++idx; // move to next router.
    }
  }
  if (zret)
    this->viewChanged(now);
  return zret;
}

CacheImpl::GroupData &
CacheImpl::GroupData::viewChanged(time_t now)
{
  m_generation      += 1;
  m_generation_time  = now;
  m_assign_info.setActive(false); // invalidate current assignment.
  m_assignment_pending = m_routers.size() && m_caches.size();
  // Cancel any pending assignment transmissions.
  ts::for_each(m_routers, ts::assign_member(&RouterData::m_assign, false));
  bw_log(DL_Debug, "Service group {} view change ({})", m_svc.getSvcId(), m_generation);

  return *this;
}

Cache::Service &
Cache::Service::setKey(const char *key)
{
  m_group->setKey(key);
  return *this;
}

Cache::Service &
Cache::Service::setSecurity(SecurityOption opt)
{
  m_group->setSecurity(opt);
  return *this;
}

CacheImpl &
CacheImpl::seedRouter(uint8_t id, uint32_t addr)
{
  GroupMap::iterator spot = m_groups.find(id);
  if (spot != m_groups.end())
    spot->second.seedRouter(addr);
  return *this;
}

bool
CacheImpl::isConfigured() const
{
  return INADDR_ANY != m_addr && m_groups.size() > 0;
}

Errata
CacheImpl::open(uint32_t addr)
{
  auto zret = this->super_type::open(addr);
  // If the socket was successfully opened, go through the
  // services and update the local service descriptor.
  if (zret.is_ok()) {
    for (GroupMap::iterator spot = m_groups.begin(), limit = m_groups.end(); spot != limit; ++spot) {
      spot->second.m_id.setAddr(m_addr);
    }
  }
  return zret;
}

time_t
CacheImpl::waitTime() const
{
  time_t now = time(0);
  return ts::minima(m_groups, &GroupData::waitTime, now);
}

void
CacheImpl::generateHereIAm(HereIAmMsg &msg, GroupData &group)
{
  msg.fill(group, group.m_id, this->setSecurity(msg, group));
  msg.finalize();
}

void
CacheImpl::generateHereIAm(HereIAmMsg &msg, GroupData &group, RouterData &router)
{
  SecurityOption sec_opt = this->setSecurity(msg, group);

  msg.fill(group, group.m_id, sec_opt);
  if (router.m_local_cache_id.getSize())
    msg.m_cache_id.setUnassigned(false);

  msg.fill_caps(router);
  msg.finalize();
}

void
CacheImpl::generateRedirectAssign(RedirectAssignMsg &msg, GroupData &group)
{
  msg.fill(group, this->setSecurity(msg, group));
  msg.finalize();
}

Errata
CacheImpl::checkRouterAssignment(GroupData const &group, RouterViewComp const &comp) const
{
  detail::Assignment const &ainfo = group.m_assign_info;
  // If group doesn't have an active assignment, always match w/o checking.
  Errata zret; // default is success.

  // if active assignment and data we can check, then check.
  if (ainfo.isActive() && !comp.isEmpty()) {
    // Validate the assignment key.
    if (ainfo.getKey().getAddr() != comp.getKeyAddr() || ainfo.getKey().getChangeNumber() != comp.getKeyChangeNumber()) {
      zret.note(ERRATA_NOTE, "Router assignment key did not match.");
    } else if (ServiceGroup::HASH_ONLY == group.m_cache_assign) {
      // Still not sure how much checking we really want or should
      // do here. For now, we'll just leave the checks validating
      // the assignment key.
    } else if (ServiceGroup::MASK_ONLY == group.m_cache_assign) {
      // The data passed back is useless. In practice the interesting
      // data in the mask case is in the Assignment Map Component
      // which the router seems to send when using mask assignment.
    }
  }
  return zret;
}

int
CacheImpl::housekeeping()
{
  int zret = 0;
  sockaddr_in dst_addr;
  sockaddr *addr_ptr              = reinterpret_cast<sockaddr *>(&dst_addr);
  time_t now                      = time(0);
  static size_t const BUFFER_SIZE = 4096;
  MsgBuffer msg_buffer;
  char msg_data[BUFFER_SIZE];
  msg_buffer.assign({msg_data, BUFFER_SIZE});

  // Set up everything except the IP address.
  memset(&dst_addr, 0, sizeof(dst_addr));
  dst_addr.sin_family = AF_INET;
  dst_addr.sin_port   = htons(DEFAULT_PORT);

  // Walk the service groups and do their housekeeping.
  for (GroupMap::iterator svc_spot = m_groups.begin(), svc_limit = m_groups.end(); svc_spot != svc_limit; ++svc_spot) {
    GroupData &group = svc_spot->second;

    // Check to see if it's time for an assignment.
    if (group.m_assignment_pending && group.m_generation_time + ASSIGN_WAIT <= now) {
      // Is a valid assignment possible?
      if (group.m_assign_info.fill(group, m_addr)) {
        group.m_assign_info.setActive(true);
        ts::for_each(group.m_routers, ts::assign_member(&RouterData::m_assign, true));
      }

      // Always clear because no point in sending an assign we can't generate.
      group.m_assignment_pending = false;
    }

    group.cullRouters(now); // TBD UPDATE VIEW!

    // Check to see if the related service is up
    if (group.processUp()) {
      // Check the active routers for scheduled packets.
      for (RouterBag::iterator rspot = group.m_routers.begin(), rend = group.m_routers.end(); rspot != rend; ++rspot) {
        dst_addr.sin_addr.s_addr = rspot->m_addr;
        if (0 == rspot->pingTime(now)) {
          HereIAmMsg here_i_am;
          here_i_am.setBuffer(msg_buffer);
          this->generateHereIAm(here_i_am, group, *rspot);
          zret = sendto(m_fd, msg_data, here_i_am.getCount(), 0, addr_ptr, sizeof(dst_addr));
          if (0 <= zret) {
            rspot->m_xmit.set(now, group.m_generation);
            rspot->m_send_caps = false;
            bw_log(DL_Debug, "Sent HERE_I_AM for service group {} to router {} {}[#{},{}].", group.m_svc.getSvcId(),
                   swoc::IP4Addr(rspot->m_addr), swoc::bwf::If(rspot->m_rapid, "[rapid] "), group.m_generation, now);
            if (rspot->m_rapid)
              --(rspot->m_rapid);
          } else {
            bw_log(DL_Warning, "Failed to send to router {:x} - {}", swoc::IP4Addr(rspot->m_addr), swoc::bwf::Errno());
          }
        } else if (rspot->m_assign) {
          RedirectAssignMsg redirect_assign;
          redirect_assign.setBuffer(msg_buffer);
          this->generateRedirectAssign(redirect_assign, group);
          zret = sendto(m_fd, msg_data, redirect_assign.getCount(), 0, addr_ptr, sizeof(dst_addr));
          if (0 <= zret)
            rspot->m_assign = false;
        }
      }
    }

    // Seed routers.
    for (std::vector<SeedRouter>::iterator sspot = group.m_seed_routers.begin(), slimit = group.m_seed_routers.end();
         sspot != slimit; ++sspot) {
      // Check to see if the related service is up
      if (group.processUp()) {
        HereIAmMsg here_i_am;
        here_i_am.setBuffer(msg_buffer);
        // Is the router due for a ping?
        if (sspot->m_xmit + TIME_UNIT > now)
          continue; // no

        this->generateHereIAm(here_i_am, group);

        dst_addr.sin_addr.s_addr = sspot->m_addr;
        zret                     = sendto(m_fd, msg_data, here_i_am.getCount(), 0, addr_ptr, sizeof(dst_addr));
        if (0 <= zret) {
          bw_log(DL_Debug, "Sent HERE_I_AM for SG {} to seed router {:x} [gen=#{},t={},n={}].", group.m_svc.getSvcId(),
                 swoc::IP4Addr(sspot->m_addr), group.m_generation, now, here_i_am.getCount());
          sspot->m_xmit   = now;
          sspot->m_count += 1;
        } else
          bw_log(DL_Debug, "Error [{}] sending HERE_I_AM for SG %d to seed router {} [#{},{}].", swoc::bwf::Errno(zret),
                 group.m_svc.getSvcId(), swoc::IP4Addr(sspot->m_addr), group.m_generation, now);
      }
    }
  }
  return zret;
}

Errata
CacheImpl::handleISeeYou(IpHeader const & /* ip_hdr ATS_UNUSED */, buffer_type const &chunk)
{
  Errata zret;
  ISeeYouMsg msg;
  // Set if our view of the group changes enough to bump the
  // generation number.
  bool view_changed = false;
  time_t now        = time(0); // don't call this over and over.
  int parse         = msg.parse(chunk);

  if (PARSE_SUCCESS != parse)
    return {ERRATA_NOTE, "Ignored malformed [%d] WCCP2_I_SEE_YOU message.", parse};

  ServiceGroup svc(msg.m_service);
  GroupMap::iterator spot = m_groups.find(svc.getSvcId());
  if (spot == m_groups.end())
    return {ERRATA_NOTE, "WCCP2_I_SEE_YOU ignored - service group %d not found.", svc.getSvcId()};

  GroupData &group = spot->second;

  if (!this->validateSecurity(msg, group))
    return {ERRATA_NOTE, "Ignored WCCP2_I_SEE_YOU with invalid security."};

  if (svc != group.m_svc)
    return {ERRATA_NOTE, "WCCP2_I_SEE_YOU ignored - service group definition {} does not match.", svc.getSvcId()};

  if (-1 == msg.m_router_id.findFromAddr(m_addr))
    return {ERRATA_NOTE, "WCCP2_I_SEE_YOU ignored -- cache not in from list."};

  bw_log(DL_Debug, "Received WCCP2_I_SEE_YOU for group {}.", group.m_svc.getSvcId());

  // Preferred address for router.
  in_addr_t router_addr = msg.m_router_id.idElt().getAddr();
  // Where we sent our packet.
  in_addr_t to_addr = msg.m_router_id.getToAddr();
  in_addr_t recv_id = msg.m_router_id.idElt().getRecvId();
  RouterBag::iterator ar_spot; // active router
  int router_idx;              // index in active routers.

  CapComp &caps = msg.m_capabilities;
  // Handle the router that sent us this.
  ar_spot = find_by_member(group.m_routers, &RouterData::m_addr, router_addr);
  if (ar_spot == group.m_routers.end()) {
    // This is a new router that's replied to one of our pings.
    // Need to do various setup and reply things to get the connection
    // established.

    // Remove this from the seed routers and copy the last packet
    // sent time.
    RouterData r(router_addr); // accumulate state before we commit it.
    r.m_xmit.m_time = group.removeSeedRouter(to_addr);

    // Validate capabilities.
    ServiceGroup::PacketStyle ps;
    ServiceGroup::CacheAssignmentStyle as;
    const char *caps_tag = caps.isEmpty() ? "default" : "router";

    // No caps -> use GRE forwarding.
    ps = caps.isEmpty() ? ServiceGroup::GRE : caps.getPacketForwardStyle();
    if (ServiceGroup::GRE & ps & group.m_packet_forward)
      r.m_packet_forward = ServiceGroup::GRE;
    else if (ServiceGroup::L2 & ps & group.m_packet_forward)
      r.m_packet_forward = ServiceGroup::L2;
    else
      zret.note(ERRATA_WARN, "Packet forwarding (config={}, {}={}) did not match.", group.m_packet_forward, caps_tag, ps);

    // No caps -> use GRE return.
    ps = caps.isEmpty() ? ServiceGroup::GRE : caps.getPacketReturnStyle();
    if (ServiceGroup::GRE & ps & group.m_packet_return)
      r.m_packet_return = ServiceGroup::GRE;
    else if (ServiceGroup::L2 & ps & group.m_packet_return)
      r.m_packet_return = ServiceGroup::L2;
    else
      zret.note(ERRATA_WARN, "Packet return (local={}, {}={}) did not match.", group.m_packet_return, caps_tag, ps);

    // No caps -> use HASH assignment.
    as = caps.isEmpty() ? ServiceGroup::HASH_ONLY : caps.getCacheAssignmentStyle();
    if (ServiceGroup::HASH_ONLY & as & group.m_cache_assign)
      r.m_cache_assign = ServiceGroup::HASH_ONLY;
    else if (ServiceGroup::MASK_ONLY & as & group.m_cache_assign) {
      r.m_cache_assign = ServiceGroup::MASK_ONLY;
      group.m_id.initDefaultMask(m_addr); // switch to MASK style ID.
    } else
      zret.note(ERRATA_WARN, "Cache assignment (local={}, {}={}) did not match.", group.m_cache_assign, caps_tag, as);

    if (!zret) {
      // cancel out, can't use this packet because we reject the router.
      return std::move(zret.note(ERRATA_WARN, "Router {} rejected because of capabilities mismatch.", swoc::IP4Addr(router_addr)));
    }

    group.m_routers.push_back(r);
    ar_spot      = group.m_routers.end() - 1;
    view_changed = true;
    bw_log(DL_Note, "Added source router {} to view {}", swoc::IP4Addr(router_addr), group.m_svc.getSvcId());
  } else {
    // Existing router. Update the receive ID in the assignment object.
    group.m_assign_info.updateRouterId(router_addr, recv_id, msg.m_router_view.getChangeNumber());
    // Check the assignment to see if we need to send it again.
    Errata status = this->checkRouterAssignment(group, msg.m_router_view);
    if (!status.empty()) {
      ar_spot->m_assign = true; // schedule an assignment message.
      bw_log(DL_Note, "Router assignment reported from {:x} did not match local assignment. Resending assignment.\n{}",
             swoc::IP4Addr(router_addr), status);
    }
  }
  time_t then = ar_spot->m_recv.m_time; // used for comparisons later.
  ar_spot->m_recv.set(now, recv_id);
  ar_spot->m_generation = msg.m_router_view.getChangeNumber();
  router_idx            = ar_spot - group.m_routers.begin();
  // Reply with our own capability options iff the router sent one to us.
  // This is a violation of the spec but it's what we have to do in practice
  // for mask assignment.
  ar_spot->m_send_caps = !caps.isEmpty();

  // For all the other listed routers, seed them if they're not
  // already active.
  uint32_t nr = msg.m_router_view.getRouterCount();
  for (uint32_t idx = 0; idx < nr; ++idx) {
    uint32_t addr = msg.m_router_view.getRouterAddr(idx);
    if (group.m_routers.end() == find_by_member(group.m_routers, &RouterData::m_addr, addr))
      group.seedRouter(addr);
  }

  // Update/Install the caches.
  // TBD: Must bump view if a router fails to report a cache it reported
  // in its last packet.
  group.resizeCacheSources();
  uint32_t nc = msg.m_router_view.getCacheCount();
  for (uint32_t idx = 0; idx < nc; ++idx) {
    CacheIdBox &cache          = msg.m_router_view.cacheId(idx);
    CacheBag::iterator ac_spot = group.findCache(cache.getAddr());
    if (group.m_caches.end() == ac_spot) {
      group.m_caches.push_back(CacheData());
      ac_spot = group.m_caches.end() - 1;
      ac_spot->m_src.resize(group.m_routers.size());
      bw_log(DL_Note, "Added cache {} to view {}", swoc::IP4Addr(cache.getAddr()), group.m_svc.getSvcId());
      view_changed = true;
    } else {
      // Check if the cache wasn't reported last time but was reported
      // this time. In that case we need to bump the view to trigger
      // assignment generation.
      if (ac_spot->m_src[router_idx].m_time != then)
        view_changed = true;
    }
    ac_spot->m_id.fill(cache);
    // If cache is this cache, update data in router record.
    if (cache.getAddr() == m_addr)
      ar_spot->m_local_cache_id.fill(cache);
    ac_spot->m_src[router_idx].set(now, recv_id);
  }

  if (view_changed)
    group.viewChanged(now);

  return zret;
}

Errata
CacheImpl::handleRemovalQuery(IpHeader const & /* ip_hdr ATS_UNUSED */, buffer_type const &chunk)
{
  Errata zret;
  RemovalQueryMsg msg;
  time_t now = time(0);
  int parse  = msg.parse(chunk);

  if (PARSE_SUCCESS != parse)
    return {ERRATA_NOTE, "Ignored malformed WCCP2_REMOVAL_QUERY message."};

  ServiceGroup svc(msg.m_service);
  GroupMap::iterator spot = m_groups.find(svc.getSvcId());
  if (spot == m_groups.end())
    return {ERRATA_NOTE, "WCCP2_REMOVAL_QUERY ignored - service group {} not found.", svc.getSvcId()};

  GroupData &group = spot->second;

  if (!this->validateSecurity(msg, group))
    return {ERRATA_NOTE, "Ignored WCCP2_REMOVAL_QUERY with invalid security."};

  if (svc != group.m_svc)
    return {ERRATA_NOTE, "WCCP2_REMOVAL_QUERY ignored - service group definition {} does not match.", svc.getSvcId()};

  uint32_t target_addr = msg.m_query.getCacheAddr(); // intended cache
  if (m_addr == target_addr) {
    uint32_t raddr             = msg.m_query.getRouterAddr();
    RouterBag::iterator router = group.findRouter(raddr);
    if (group.m_routers.end() != router) {
      router->m_rapid = true; // do rapid responses.
      router->m_recv.set(now, msg.m_query.getRecvId());
      bw_log(DL_Note, "WCCP2_REMOVAL_QUERY from router {:x}", swoc::IP4Addr(raddr));
    } else {
      bw_log(DL_Note, "WCCP2_REMOVAL_QUERY from unknown router {:x}", swoc::IP4Addr(raddr));
    }
  } else {
    // Not an error in the multi-cast case, so just log under debug.
    bw_log(DL_Debug, "WCCP2_REMOVAL_QUERY ignored -- target cache address {} did not match local address {}",
           swoc::IP4Addr(target_addr), swoc::IP4Addr(m_addr));
  }

  bw_log(DL_Debug, "Received WCCP2_REMOVAL_QUERY for group {}.", group.m_svc.getSvcId());

  return zret;
}
// ------------------------------------------------------
inline uint32_t
detail::router::CacheData::idAddr() const
{
  return m_id.getAddr();
}

RouterImpl::GroupData::GroupData() {}

RouterImpl::CacheBag::iterator
RouterImpl::GroupData::findCache(uint32_t addr)
{
  return std::find_if(m_caches.begin(), m_caches.end(), ts::predicate(&CacheData::idAddr, addr));
}

RouterImpl::GroupData &
RouterImpl::defineServiceGroup(ServiceGroup const &svc, ServiceGroup::Result *result)
{
  uint8_t svc_id          = svc.getSvcId();
  GroupMap::iterator spot = m_groups.find(svc_id);
  GroupData *group; // service with target ID.
  ServiceGroup::Result zret;
  if (spot == m_groups.end()) { // not defined
    group        = &(m_groups[svc_id]);
    group->m_svc = svc;
    zret         = ServiceGroup::DEFINED;
  } else {
    group = &spot->second;
    zret  = group->m_svc == svc ? ServiceGroup::EXISTS : ServiceGroup::CONFLICT;
  }
  if (result)
    *result = zret;
  return *group;
}

void
RouterImpl::GroupData::resizeRouterSources()
{
  ts::for_each(m_routers, &RouterData::resize, m_caches.size());
}

Errata
RouterImpl::handleHereIAm(IpHeader const &ip_hdr, buffer_type const &chunk)
{
  Errata zret;
  HereIAmMsg msg;
  static GroupData nil_group; // scratch until I clean up the security.
  // Set if our view of the group changes enough to bump the
  // generation number.
  bool view_changed = false;
  int i;                // scratch index var.
  time_t now = time(0); // don't call this over and over.
  int parse  = msg.parse(chunk);

  if (PARSE_SUCCESS != parse)
    return {ERRATA_NOTE, "Ignored malformed WCCP2_HERE_I_AM message."};

  if (!this->validateSecurity(msg, nil_group))
    return {ERRATA_NOTE, "Ignored WCCP2_HERE_I_AM with invalid security."};

  ServiceGroup svc(msg.m_service);
  ServiceGroup::Result r;
  GroupData &group = this->defineServiceGroup(svc, &r);
  if (ServiceGroup::CONFLICT == r)
    return {ERRATA_NOTE, "WCCP2_HERE_I_AM ignored - service group {} definition does not match.", svc.getSvcId()};
  else if (ServiceGroup::DEFINED == r)
    return {ERRATA_NOTE, "Service group {} defined by WCCP2_HERE_I_AM.", svc.getSvcId()};

  // Check if this cache is already known.
  uint32_t cache_addr = msg.m_cache_id.getAddr();
  int cache_idx;
  uint32_t cache_gen;
  CacheBag::iterator cache = group.findCache(cache_addr);
  if (cache == group.m_caches.end()) { // not known
    group.m_caches.push_back(CacheData());
    // Vector modified, need clean end value.
    cache               = group.m_caches.end() - 1;
    cache->m_recv_count = 0;
    group.resizeRouterSources();
    view_changed = true;
  } else {
    // Did the cache mention us specifically?
    // If so, make sure the sequence # is correct.
    RouterIdElt *me = msg.m_cache_view.findf_router_elt(m_addr);
    if (me && me->getRecvId() != cache->m_recv_count)
      return {ERRATA_NOTE, "Discarded out of date (recv={}, local={}) WCCP2_HERE_I_AM.", me->getRecvId(), cache->m_recv_count};
  }

  cache_gen = msg.m_cache_view.getChangeNumber();

  cache_idx = cache - group.m_caches.begin();
  cache->m_id.fill(msg.m_cache_id.cacheId());
  cache->m_recv.set(now, cache_gen);
  cache->m_pending = true;
  cache->m_to_addr = ip_hdr.m_dst;

  // Add any new routers
  i = msg.m_cache_view.getRouterCount();
  while (i-- > 0) {
    uint32_t addr            = msg.m_cache_view.routerElt(i).getAddr();
    RouterBag::iterator spot = find_by_member(group.m_routers, &RouterData::m_addr, addr);
    if (spot == group.m_routers.end()) {
      group.m_routers.push_back(RouterData());
      // Can't count on previous end value, modified container.
      spot         = group.m_routers.end() - 1;
      spot->m_addr = addr;
      spot->m_src.resize(group.m_caches.size());
      view_changed = true;
    }
    spot->m_src[cache_idx].set(now, cache_gen);
  }

  if (view_changed)
    ++(group.m_generation);
  return zret;
}

void
RouterImpl::generateISeeYou(ISeeYouMsg &msg, GroupData &group, CacheData &cache)
{
  int i;
  size_t n_routers = group.m_routers.size();
  size_t n_caches  = group.m_caches.size();

  // Not handling multi-cast so target caches is hardwired to 1.
  msg.fill(group, this->setSecurity(msg, group), group.m_assign_info, 1, n_routers, n_caches);

  // Fill in ID data not done by fill.
  msg.m_router_id.setIdElt(m_addr, cache.m_recv_count + 1).setToAddr(cache.m_to_addr).setFromAddr(0, cache.idAddr());
  ;

  // Fill view routers.
  i = 0;
  for (RouterBag::iterator router = group.m_routers.begin(), router_limit = group.m_routers.end(); router != router_limit;
       ++router, ++i) {
    msg.m_router_view.setRouterAddr(i, router->m_addr);
  }

  // Fill view caches.
  i = 0;
  for (CacheBag::iterator spot = group.m_caches.begin(), limit = group.m_caches.end(); spot != limit; ++spot, ++i) {
    // TBD: This needs to track memory because cache ID elements
    // turn out to be variable sized.
    //    msg.m_router_view.cacheId(i) = spot->m_id;
  }

  msg.finalize();
}

int
RouterImpl::xmitISeeYou()
{
  int zret = 0;
  ISeeYouMsg msg;
  MsgBuffer buffer;
  sockaddr_in dst_addr;
  time_t now                      = time(0);
  static size_t const BUFFER_SIZE = 4096;
  char *data                      = static_cast<char *>(alloca(BUFFER_SIZE));

  memset(&dst_addr, 0, sizeof(dst_addr));
  dst_addr.sin_family = AF_INET;
  dst_addr.sin_port   = htons(DEFAULT_PORT);
  buffer.assign({data, BUFFER_SIZE});

  // Send out messages for each service group.
  for (GroupMap::iterator svc_spot = m_groups.begin(), svc_limit = m_groups.end(); svc_spot != svc_limit; ++svc_spot) {
    GroupData &group = svc_spot->second;

    // Check each active cache in the group.
    for (CacheBag::iterator cache = group.m_caches.begin(), cache_limit = group.m_caches.end(); cache != cache_limit; ++cache) {
      if (!cache->m_pending)
        continue;

      msg.setBuffer(buffer);
      this->generateISeeYou(msg, group, *cache);
      dst_addr.sin_addr.s_addr = cache->m_id.getAddr();
      zret                     = sendto(m_fd, data, msg.getCount(), 0, reinterpret_cast<sockaddr *>(&dst_addr), sizeof(dst_addr));
      if (0 <= zret) {
        cache->m_xmit.set(now, group.m_generation);
        cache->m_pending    = false;
        cache->m_recv_count = msg.m_router_id.getRecvId();
        bw_log(DL_Debug, "I_SEE_YOU -> {}", swoc::IP4Addr(cache->m_id.getAddr()));
      } else {
        bw_log(DL_Warning, "Router transmit failed - {}", swoc::bwf::Errno());
        return zret;
      }
    }
  }
  return zret;
}

int
RouterImpl::housekeeping()
{
  return this->xmitISeeYou();
}

bool
RouterImpl::isConfigured() const
{
  return false;
}
// ------------------------------------------------------
EndPoint::EndPoint() {}

EndPoint::~EndPoint() {}

EndPoint::EndPoint(self_type const &that) : m_ptr(that.m_ptr) {}

inline EndPoint::ImplType *
EndPoint::instance()
{
  return m_ptr ? m_ptr.get() : this->make();
}

EndPoint &
EndPoint::setAddr(uint32_t addr)
{
  this->instance()->m_addr = addr;
  bw_log(DL_Debug, "Endpoint address set to {}", swoc::IP4Addr(addr));
  return *this;
}

bool
EndPoint::isConfigured() const
{
  return m_ptr && m_ptr->isConfigured();
}

Errata
EndPoint::open(uint32_t addr)
{
  return this->instance()->open(addr);
}

void
EndPoint::useMD5Security(std::string_view const key)
{
  this->instance()->useMD5Security(key);
}

int
EndPoint::getSocket() const
{
  return m_ptr ? m_ptr->m_fd : ts::NO_FD;
}

int
EndPoint::housekeeping()
{
  // Don't force an instance because if there isn't one,
  // there's no socket either.
  return m_ptr && ts::NO_FD != m_ptr->m_fd ? m_ptr->housekeeping() : -ENOTCONN;
}

swoc::Errata
EndPoint::handleMessage()
{
  return m_ptr ? m_ptr->handleMessage() :
                 swoc::Errata(ec_for(ENOTCONN), ERRATA_NOTE, "EndPoint::handleMessage called on unconnected instance");
}
// ------------------------------------------------------
Cache::Cache() {}

Cache::~Cache() {}

EndPoint::ImplType *
Cache::make()
{
  m_ptr.reset(new ImplType);
  return m_ptr.get();
}

inline Cache::ImplType *
Cache::instance()
{
  return static_cast<ImplType *>(this->super_type::instance());
}

inline Cache::ImplType *
Cache::impl()
{
  return static_cast<ImplType *>(m_ptr.get());
}

inline Cache::ImplType const *
Cache::impl() const
{
  return static_cast<ImplType *>(m_ptr.get());
}

Cache::Service
Cache::defineServiceGroup(ServiceGroup const &svc, ServiceGroup::Result *result)
{
  return Service(*this, this->instance()->defineServiceGroup(svc, result));
}

time_t
Cache::waitTime() const
{
  return m_ptr ? this->impl()->waitTime() : std::numeric_limits<time_t>::max();
}

Cache &
Cache::addSeedRouter(uint8_t id, uint32_t addr)
{
  this->instance()->seedRouter(id, addr);
  return *this;
}

swoc::Errata
Cache::loadServicesFromFile(const char *path)
{
  return this->instance()->loadServicesFromFile(path);
}
// ------------------------------------------------------
Router::Router() {}

Router::~Router() {}

EndPoint::ImplType *
Router::make()
{
  m_ptr.reset(new ImplType);
  return m_ptr.get();
}

inline Router::ImplType *
Router::instance()
{
  return static_cast<ImplType *>(this->super_type::instance());
}

inline Router::ImplType *
Router::impl()
{
  return static_cast<ImplType *>(m_ptr.get());
}
// ------------------------------------------------------
} // namespace wccp
