/** @file
    WCCP Message parsing and generation.

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

#include "WccpLocal.h"
#include <errno.h>
#include <openssl/md5.h>
#include "api/ts/TsException.h"
#include "ts/ink_memory.h"
#include "ts/ink_string.h"

namespace wccp
{
// ------------------------------------------------------
// ------------------------------------------------------
ServiceGroup &
ServiceGroup::setSvcType(ServiceGroup::Type t)
{
  if (STANDARD == t) {
    // For standard service, everything past ID must be zero.
    memset(&m_priority, 0, sizeof(*this) - (reinterpret_cast<char *>(&m_priority) - reinterpret_cast<char *>(this)));
  }
  m_svc_type = t; // store actual type.
  return *this;
}

bool
ServiceGroup::operator==(self const &that) const
{
  if (m_svc_type == STANDARD) {
    // If type are different, fail, else if both are STANDARD
    // then we need only match on the ID.
    return that.m_svc_type == STANDARD && m_svc_id == that.m_svc_id;
  } else if (that.m_svc_type != DYNAMIC) {
    return false;
  } else {
    // Both services are DYNAMIC, check the properties.
    // Port check is technically too strict -- should ignore
    // ports beyond the terminating null port. Oh well.
    return m_svc_id == that.m_svc_id && m_protocol == that.m_protocol && m_flags == that.m_flags && m_priority == that.m_priority &&
           0 == memcmp(m_ports, that.m_ports, sizeof(m_ports));
  }
}
// ------------------------------------------------------
// ------------------------------------------------------
CacheHashIdElt &
CacheHashIdElt::setBucket(int idx, bool state)
{
  uint8_t &bucket = m_buckets[idx >> 3];
  uint8_t mask    = 1 << (idx & 7);
  if (state)
    bucket |= mask;
  else
    bucket &= !mask;
  return *this;
}

CacheHashIdElt &
CacheHashIdElt::setBuckets(bool state)
{
  memset(m_buckets, state ? 0xFF : 0, sizeof(m_buckets));
  return *this;
}

size_t
CacheIdBox::getSize() const
{
  return m_size;
}

CacheIdBox &
CacheIdBox::require(size_t n)
{
  if (m_cap < n) {
    if (m_base && m_cap)
      ats_free(m_base);
    m_base = static_cast<CacheIdElt *>(ats_malloc(n));
    m_cap  = n;
  }
  memset(m_base, 0, m_cap);
  m_size = 0;
  return *this;
}

CacheIdBox &
CacheIdBox::initDefaultHash(uint32_t addr)
{
  this->require(sizeof(CacheHashIdElt));
  m_size = sizeof(CacheHashIdElt);
  m_base->initHashRev().setUnassigned(true).setMask(false).setAddr(addr);
  m_tail           = static_cast<CacheHashIdElt *>(m_base)->getTailPtr();
  m_tail->m_weight = htons(0);
  m_tail->m_status = htons(0);
  return *this;
}

CacheIdBox &
CacheIdBox::initDefaultMask(uint32_t addr)
{
  // Base element plus set with 1 value plus tail.
  this->require(sizeof(CacheMaskIdElt) + MaskValueSetElt::calcSize(1) + sizeof(CacheIdElt::Tail));
  CacheMaskIdElt *mid = static_cast<CacheMaskIdElt *>(m_base);
  mid->initHashRev().setUnassigned(true).setMask(true).setAddr(addr);
  mid->m_assign.init(0, 0, 0, 0)->addValue(addr, 0, 0, 0, 0);
  m_size           = mid->getSize();
  m_tail           = mid->getTailPtr();
  m_tail->m_weight = htons(0);
  m_tail->m_status = htons(0);
  return *this;
}

CacheIdBox &
CacheIdBox::fill(self const &src)
{
  size_t n = src.getSize();
  this->require(src.getSize());
  memcpy(m_base, src.m_base, n);
  m_size = src.m_size;
  // If tail is set in src, use the same offset here.
  if (src.m_tail)
    m_tail = reinterpret_cast<CacheIdElt::Tail *>(reinterpret_cast<char *>(m_base) +
                                                  (reinterpret_cast<char *>(src.m_tail) - reinterpret_cast<char *>(src.m_base)));
  else
    m_tail = 0;
  return *this;
}

CacheIdBox &
CacheIdBox::fill(void *base, self const &src)
{
  m_size = src.getSize();
  m_cap  = 0;
  m_base = static_cast<CacheIdElt *>(base);
  memcpy(m_base, src.m_base, m_size);
  return *this;
}

int
CacheIdBox::parse(MsgBuffer base)
{
  int zret        = PARSE_SUCCESS;
  CacheIdElt *ptr = reinterpret_cast<CacheIdElt *>(base.getTail());
  size_t n        = base.getSpace();
  m_cap           = 0;
  if (ptr->isMask()) {
    CacheMaskIdElt *mptr = static_cast<CacheMaskIdElt *>(ptr);
    size_t size          = sizeof(CacheMaskIdElt);
    // Sanity check - verify enough room for empty elements.
    if (n < size || n < size + MaskValueSetElt::calcSize(0) * mptr->getCount()) {
      zret = PARSE_BUFFER_TOO_SMALL;
    } else {
      m_size = mptr->getSize();
      if (n < m_size) {
        zret = PARSE_BUFFER_TOO_SMALL;
        logf(LVL_DEBUG, "I_SEE_YOU Cache Mask ID too small: %lu < %lu", n, m_size);
      } else {
        m_tail = mptr->getTailPtr();
      }
    }
  } else {
    if (n < sizeof(CacheHashIdElt)) {
      zret = PARSE_BUFFER_TOO_SMALL;
      logf(LVL_DEBUG, "I_SEE_YOU Cache Hash ID too small: %lu < %lu", n, sizeof(CacheHashIdElt));
    } else {
      m_size = sizeof(CacheHashIdElt);
      m_tail = static_cast<CacheHashIdElt *>(m_base)->getTailPtr();
    }
  }
  if (PARSE_SUCCESS == zret)
    m_base = ptr;
  return zret;
}
// ------------------------------------------------------
inline CapabilityElt::Type
CapabilityElt::getCapType() const
{
  return static_cast<Type>(ntohs(m_cap_type));
}

inline CapabilityElt &
CapabilityElt::setCapType(Type cap)
{
  m_cap_type = static_cast<Type>(htons(cap));
  return *this;
}

inline uint32_t
CapabilityElt::getCapData() const
{
  return ntohl(m_cap_data);
}

inline CapabilityElt &
CapabilityElt::setCapData(uint32_t data)
{
  m_cap_data = htonl(data);
  return *this;
}

CapabilityElt::CapabilityElt() {}

CapabilityElt::CapabilityElt(Type cap, uint32_t data)
{
  this->setCapType(cap);
  this->setCapData(data);
  m_cap_length = htons(sizeof(uint32_t));
}
// ------------------------------------------------------
inline uint32_t
ValueElt::getf_src_addr() const
{
  return ntohl(m_src_addr);
}

inline ValueElt &
ValueElt::setf_src_addr(uint32_t addr)
{
  m_src_addr = htonl(addr);
  return *this;
}

inline uint32_t
ValueElt::getDstAddr() const
{
  return ntohl(m_dst_addr);
}

inline ValueElt &
ValueElt::setf_dst_addr(uint32_t addr)
{
  m_dst_addr = htonl(addr);
  return *this;
}

inline uint16_t
ValueElt::getf_src_port() const
{
  return ntohs(m_src_port);
}

inline ValueElt &
ValueElt::setf_src_port(uint16_t port)
{
  m_src_port = htons(port);
  return *this;
}

inline uint16_t
ValueElt::getDstPort() const
{
  return ntohs(m_dst_port);
}

inline ValueElt &
ValueElt::setf_dst_port(uint16_t port)
{
  m_dst_port = htons(port);
  return *this;
}

inline uint32_t
ValueElt::getCacheAddr() const
{
  return ntohl(m_cache_addr);
}

inline ValueElt &
ValueElt::setCacheAddr(uint32_t addr)
{
  m_cache_addr = htonl(addr);
  return *this;
}
// ------------------------------------------------------
MaskValueSetElt &
MaskValueSetElt::addValue(uint32_t cacheAddr, uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
{
  uint32_t idx = ntohl(m_count);
  new (this->values() + idx) ValueElt(cacheAddr, srcAddr, dstAddr, srcPort, dstPort);
  m_count = htonl(idx + 1);
  return *this;
}

size_t
MaskAssignElt::getVarSize() const
{
  size_t zret = 0;
  int n       = this->getCount();

  MaskValueSetElt const *set = reinterpret_cast<MaskValueSetElt const *>(this + 1);
  while (n--) {
    size_t k = set->getSize();
    zret += k;
    set = reinterpret_cast<MaskValueSetElt const *>(reinterpret_cast<char const *>(set) + k);
  }
  return zret;
}

HashAssignElt &
HashAssignElt::round_robin_assign()
{
  uint32_t v_caches = this->getCount();
  Bucket *buckets   = this->getBucketBase();
  if (1 == v_caches)
    memset(buckets, 0, sizeof(Bucket) * N_BUCKETS);
  else { // Assign round robin.
    size_t x = 0;
    for (Bucket *spot = buckets, *limit = spot + N_BUCKETS; spot < limit; ++spot) {
      spot->m_idx = x;
      spot->m_alt = 0;
      x           = (x + 1) % v_caches;
    }
  }
  return *this;
}

RouterAssignListElt &
RouterAssignListElt::updateRouterId(uint32_t addr, uint32_t rcvid, uint32_t cno)
{
  uint32_t n           = this->getCount();
  RouterAssignElt *elt = access_array<RouterAssignElt>(this + 1);
  for (uint32_t i = 0; i < n; ++i, ++elt) {
    if (addr == elt->getAddr()) {
      elt->setChangeNumber(cno).setRecvId(rcvid);
      break;
    }
  }
  return *this;
}
// ------------------------------------------------------
message_type_t
MsgHeaderComp::getType()
{
  return static_cast<message_type_t>(get_field(&raw_t::m_type, m_base));
}

uint16_t
MsgHeaderComp::getVersion()
{
  return get_field(&raw_t::m_version, m_base);
}

uint16_t
MsgHeaderComp::getLength()
{
  return get_field(&raw_t::m_length, m_base);
}

MsgHeaderComp &
MsgHeaderComp::setType(message_type_t type)
{
  set_field(&raw_t::m_type, m_base, type);
  return *this;
}

MsgHeaderComp &
MsgHeaderComp::setVersion(uint16_t version)
{
  set_field(&raw_t::m_version, m_base, version);
  return *this;
}

MsgHeaderComp &
MsgHeaderComp::setLength(uint16_t length)
{
  set_field(&raw_t::m_length, m_base, length);
  return *this;
}

size_t
MsgHeaderComp::calcSize()
{
  return sizeof(raw_t);
}

MsgHeaderComp &
MsgHeaderComp::fill(MsgBuffer &buffer, message_type_t t)
{
  size_t comp_size = this->calcSize();
  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);
  m_base = buffer.getTail();
  buffer.use(comp_size);
  this->setType(t).setVersion(VERSION).setLength(0);
  return *this;
}

int
MsgHeaderComp::parse(MsgBuffer &base)
{
  int zret         = PARSE_SUCCESS;
  size_t comp_size = this->calcSize();
  if (base.getSpace() < comp_size) {
    zret = PARSE_BUFFER_TOO_SMALL;
  } else {
    m_base = base.getTail();
    // Length field puts end of message past end of buffer.
    if (this->getLength() + comp_size > base.getSpace()) {
      zret = PARSE_MSG_TOO_BIG;
    } else if (INVALID_MSG_TYPE == this->toMsgType(get_field(&raw_t::m_type, m_base))) {
      zret = PARSE_COMP_TYPE_INVALID;
    } else {
      base.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
SecurityComp::Key SecurityComp::m_default_key;
SecurityComp::Option SecurityComp::m_default_opt = SECURITY_NONE;

SecurityComp::Option
SecurityComp::getOption() const
{
  return static_cast<Option>(get_field(&RawNone::m_option, m_base));
}

SecurityComp &
SecurityComp::setOption(Option opt)
{
  set_field(&RawNone::m_option, m_base, static_cast<uint32_t>(opt));
  return *this;
}

SecurityComp &
SecurityComp::setKey(char const *key)
{
  m_local_key = true;
  ink_strlcpy(m_key, key, KEY_SIZE);
  return *this;
}

void
SecurityComp::setDefaultKey(char const *key)
{
  ink_strlcpy(m_default_key, key, KEY_SIZE);
}

SecurityComp &
SecurityComp::fill(MsgBuffer &buffer, Option opt)
{
  size_t comp_size = this->calcSize(opt);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setLength(comp_size - sizeof(super::raw_t)).setOption(opt);

  if (SECURITY_NONE != opt) {
    RawMD5::HashData &data = access_field(&RawMD5::m_data, m_base);
    memset(data, 0, sizeof(data));
  }

  buffer.use(comp_size);
  return *this;
}

SecurityComp &
SecurityComp::secure(MsgBuffer const &msg)
{
  if (SECURITY_MD5 == this->getOption()) {
    MD5_CTX ctx;
    char const *key = m_local_key ? m_key : m_default_key;
    MD5_Init(&ctx);
    MD5_Update(&ctx, key, KEY_SIZE);
    MD5_Update(&ctx, msg.getBase(), msg.getCount());
    MD5_Final(access_field(&RawMD5::m_data, m_base), &ctx);
  }
  return *this;
}

bool
SecurityComp::validate(MsgBuffer const &msg) const
{
  bool zret = true;
  if (SECURITY_MD5 == this->getOption()) {
    RawMD5::HashData save;
    RawMD5::HashData &org = const_cast<RawMD5::HashData &>(access_field(&RawMD5::m_data, m_base));
    MD5_CTX ctx;
    char const *key = m_local_key ? m_key : m_default_key;
    // save the original hash aside.
    memcpy(save, org, sizeof(save));
    // zero out the component hash area to compute the hash.
    memset(org, 0, sizeof(org));
    // Compute hash in to hash area.
    MD5_Init(&ctx);
    MD5_Update(&ctx, key, KEY_SIZE);
    MD5_Update(&ctx, msg.getBase(), msg.getCount());
    MD5_Final(org, &ctx);
    // check hash.
    zret = 0 == memcmp(org, save, sizeof(save));
    // restore original data.
    memcpy(org, save, sizeof(save));
  }
  return zret;
}

int
SecurityComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      Option opt = this->getOption();
      if (SECURITY_NONE != opt && SECURITY_MD5 != opt)
        zret = PARSE_COMP_INVALID;
      else {
        size_t comp_size = this->calcSize(opt);
        if (this->getLength() != comp_size - sizeof(super::raw_t))
          zret = PARSE_COMP_WRONG_SIZE;
        else
          buffer.use(comp_size);
      }
    }
  }
  return zret;
}
// ------------------------------------------------------

ServiceComp &
ServiceComp::setPort(int idx, uint16_t port)
{
  this->access()->setPort(idx, port);
  m_port_count = std::max(m_port_count, idx);
  return *this;
}

ServiceComp &
ServiceComp::addPort(uint16_t port)
{
  if (m_port_count < static_cast<int>(ServiceGroup::N_PORTS))
    this->access()->setPort(m_port_count++, port);
  return *this;
}

ServiceComp &
ServiceComp::clearPorts()
{
  this->access()->clearPorts();
  m_port_count = 0;
  return *this;
}

ServiceComp &
ServiceComp::fill(MsgBuffer &buffer, ServiceGroup const &svc)
{
  size_t comp_size = this->calcSize();

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setLength(comp_size - sizeof(super::raw_t));

  // Cast buffer to our serialized type, then cast to ServiceGroup
  // to get offset of that part of the serialization storage.
  memcpy(
    // This generates a gcc warning, but the next line doesn't. Yay.
    //    static_cast<ServiceGroup*>(reinterpret_cast<raw_t*>(m_base)),
    &static_cast<ServiceGroup &>(*reinterpret_cast<raw_t *>(m_base)), &svc, sizeof(svc));
  buffer.use(comp_size);
  return *this;
}

int
ServiceComp::parse(MsgBuffer &buffer)
{
  int zret         = PARSE_SUCCESS;
  size_t comp_size = this->calcSize();
  if (buffer.getSpace() < comp_size)
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      ServiceGroup::Type svc = this->getSvcType();
      if (ServiceGroup::DYNAMIC != svc && ServiceGroup::STANDARD != svc)
        zret = PARSE_COMP_INVALID;
      else if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
RouterIdElt &
RouterIdComp::idElt()
{
  return access_field(&raw_t::m_id, m_base);
}

RouterIdElt const &
RouterIdComp::idElt() const
{
  return access_field(&raw_t::m_id, m_base);
}

RouterIdComp &
RouterIdComp::setIdElt(uint32_t addr, uint32_t recv_id)
{
  this->idElt().setAddr(addr).setRecvId(recv_id);
  return *this;
}

uint32_t
RouterIdComp::getAddr() const
{
  return this->idElt().getAddr();
}

RouterIdComp &
RouterIdComp::setAddr(uint32_t addr)
{
  this->idElt().setAddr(addr);
  return *this;
}

uint32_t
RouterIdComp::getRecvId() const
{
  return this->idElt().getRecvId();
}
inline RouterIdComp &
RouterIdComp::setRecvId(uint32_t id)
{
  this->idElt().setRecvId(id);
  return *this;
}

uint32_t
RouterIdComp::getToAddr() const
{
  return access_field(&raw_t::m_to_addr, m_base);
}

RouterIdComp &
RouterIdComp::setToAddr(uint32_t addr)
{
  access_field(&raw_t::m_to_addr, m_base) = addr;
  return *this;
}

uint32_t
RouterIdComp::getFromCount() const
{
  return get_field(&raw_t::m_from_count, m_base);
}

uint32_t
RouterIdComp::getFromAddr(int idx) const
{
  return access_array<uint32_t>(m_base + sizeof(raw_t))[idx];
}

RouterIdComp &
RouterIdComp::setFromAddr(int idx, uint32_t addr)
{
  access_array<uint32_t>(m_base + sizeof(raw_t))[idx] = addr;
  return *this;
}

int
RouterIdComp::findFromAddr(uint32_t addr)
{
  int n           = this->getFromCount();
  uint32_t *addrs = access_array<uint32_t>(m_base + sizeof(raw_t)) + n;
  while (n-- != 0 && *--addrs != addr)
    ;
  return n;
}

RouterIdComp &
RouterIdComp::fill(MsgBuffer &buffer, size_t n_caches)
{
  size_t comp_size = this->calcSize(n_caches);
  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE);
  set_field(&raw_t::m_from_count, m_base, n_caches);
  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);

  return *this;
}

RouterIdComp &
RouterIdComp::fillSingleton(MsgBuffer &buffer, uint32_t addr, uint32_t recv_count, uint32_t to_addr, uint32_t from_addr)
{
  size_t comp_size = this->calcSize(1);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setIdElt(addr, recv_count).setToAddr(to_addr).setFromAddr(0, from_addr);

  set_field(&raw_t::m_from_count, m_base, 1);

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);

  return *this;
}

int
RouterIdComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      size_t comp_size = this->calcSize(this->getFromCount());
      if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
AssignmentKeyElt &
RouterViewComp::keyElt()
{
  return access_field(&raw_t::m_key, m_base);
}

AssignmentKeyElt const &
RouterViewComp::keyElt() const
{
  return access_field(&raw_t::m_key, m_base);
}

uint32_t
RouterViewComp::getChangeNumber() const
{
  return get_field(&raw_t::m_change_number, m_base);
}

RouterViewComp &
RouterViewComp::setChangeNumber(uint32_t n)
{
  set_field(&raw_t::m_change_number, m_base, n);
  return *this;
}

// This is untainted because an overall size check is done when the packet is read. If any of the
// counts are bogus, that size check will fail.
// coverity[ -tainted_data_return]
uint32_t
RouterViewComp::getCacheCount() const
{
  return ntohl(*m_cache_count);
}

// This is untainted because an overall size check is done when the packet is read. If any of the
// counts are bogus, that size check will fail.
// coverity[ -tainted_data_return]
uint32_t
RouterViewComp::getRouterCount() const
{
  return get_field(&raw_t::m_router_count, m_base);
}

CacheIdBox &
RouterViewComp::cacheId(int idx)
{
  return m_cache_ids[idx];
}

uint32_t
RouterViewComp::getRouterAddr(int idx) const
{
  return access_array<uint32_t>(m_base + sizeof(raw_t))[idx];
}

RouterViewComp &
RouterViewComp::setRouterAddr(int idx, uint32_t addr)
{
  access_array<uint32_t>(m_base + sizeof(raw_t))[idx] = addr;
  return *this;
}

uint32_t *
RouterViewComp::calc_cache_count_ptr()
{
  return reinterpret_cast<uint32_t *>(m_base + sizeof(raw_t) + this->getRouterCount() * sizeof(uint32_t));
}

RouterViewComp &
RouterViewComp::fill(MsgBuffer &buffer, int n_routers, int n_caches)
{
  // TBD: This isn't right since the upgrade to mask support
  // because the size isn't a static function of the router and
  // cache count anymore.
  size_t comp_size = sizeof(raw_t);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE);

  // No setf for this field, hand pack it.
  set_field(&raw_t::m_router_count, m_base, n_routers);
  // Set the pointer to the count of caches.
  m_cache_count = this->calc_cache_count_ptr();
  // No setf, go direct.
  *m_cache_count = htonl(n_caches);

  this->setLength(comp_size - HEADER_SIZE);
  buffer.use(comp_size);

  return *this;
}

int
RouterViewComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      uint32_t ncaches; // # of caches.
      if (this->getRouterCount() > MAX_ROUTERS)
        zret = PARSE_MSG_INVALID;
      // check if cache count is past end of buffer
      else if (static_cast<void *>(m_cache_count = this->calc_cache_count_ptr()) >=
               static_cast<void *>(buffer.getBase() + buffer.getSize()))
        zret = PARSE_COMP_WRONG_SIZE, log(LVL_DEBUG, "I_SEE_YOU: cache counter past end of buffer");
      else if ((ncaches = this->getCacheCount()) > MAX_CACHES)
        zret = PARSE_MSG_INVALID;
      else {
        size_t comp_size = reinterpret_cast<char *>(m_cache_count + 1) - m_base;
        // Walk the cache ID elements.
        MsgBuffer spot(buffer);
        CacheIdBox *box = m_cache_ids;
        uint32_t idx    = 0;
        spot.use(comp_size);
        while (idx < ncaches && PARSE_SUCCESS == (zret = box->parse(spot))) {
          size_t k = box->getSize();
          spot.use(k);
          comp_size += k;
          ++box;
          ++idx;
        }
        if (PARSE_SUCCESS == zret)
          buffer.use(comp_size);
      }
    }
  }
  return zret;
}
// ------------------------------------------------------
CacheIdComp &
CacheIdComp::fill(MsgBuffer &base, CacheIdBox const &src)
{
  size_t comp_size = src.getSize() + HEADER_SIZE;

  if (base.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = base.getTail();
  this->setType(COMP_TYPE).setLength(comp_size - HEADER_SIZE);
  m_box.fill(&(access_field(&raw_t::m_id, m_base)), src);
  base.use(comp_size);
  return *this;
}

int
CacheIdComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      MsgBuffer tmp(buffer);
      tmp.use(reinterpret_cast<char *>(&(access_field(&raw_t::m_id, m_base))) - m_base);
      zret = m_box.parse(tmp);
      if (PARSE_SUCCESS == zret) {
        size_t comp_size = HEADER_SIZE + m_box.getSize();
        if (this->getLength() != comp_size - HEADER_SIZE)
          zret = PARSE_COMP_WRONG_SIZE;
        else
          buffer.use(comp_size);
      }
    }
  }
  return zret;
}
// ------------------------------------------------------
uint32_t
CacheViewComp::getChangeNumber() const
{
  return get_field(&raw_t::m_change_number, m_base);
}

CacheViewComp &
CacheViewComp::setChangeNumber(uint32_t n)
{
  set_field(&raw_t::m_change_number, m_base, n);
  return *this;
}

uint32_t
CacheViewComp::getRouterCount() const
{
  return get_field(&raw_t::m_router_count, m_base);
}

uint32_t
CacheViewComp::getCacheCount() const
{
  return ntohl(*m_cache_count);
}

uint32_t
CacheViewComp::getCacheAddr(int idx) const
{
  return ntohl(m_cache_count[idx + 1]);
}

CacheViewComp &
CacheViewComp::setCacheAddr(int idx, uint32_t addr)
{
  m_cache_count[idx + 1] = addr;
  return *this;
}

RouterIdElt *
CacheViewComp::atf_router_array()
{
  return reinterpret_cast<RouterIdElt *>(m_base + sizeof(raw_t));
}

RouterIdElt &
CacheViewComp::routerElt(int idx)
{
  return this->atf_router_array()[idx];
}

RouterIdElt *
CacheViewComp::findf_router_elt(uint32_t addr)
{
  for (RouterIdElt *rtr = this->atf_router_array(), *limit = rtr + this->getRouterCount(); rtr < limit; ++rtr) {
    if (rtr->getAddr() == addr)
      return rtr;
  }
  return 0;
}

size_t
CacheViewComp::calcSize(int n_routers, int n_caches)
{
  return sizeof(raw_t) + n_routers * sizeof(RouterIdElt) + sizeof(uint32_t) + n_caches * sizeof(uint32_t);
}

CacheViewComp &
CacheViewComp::fill(MsgBuffer &buffer, detail::cache::GroupData const &group)
{
  int i;
  size_t n_routers = group.m_routers.size();
  size_t n_caches  = group.m_caches.size();
  size_t comp_size = this->calcSize(n_routers, n_caches);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setChangeNumber(group.m_generation);

  set_field(&raw_t::m_router_count, m_base, n_routers);
  // Set the pointer to the count of caches.
  m_cache_count  = reinterpret_cast<uint32_t *>(m_base + sizeof(raw_t) + n_routers * sizeof(RouterIdElt));
  *m_cache_count = htonl(n_caches); // set the actual count.

  // Fill routers.
  i = 0;
  for (detail::cache::RouterBag::const_iterator spot = group.m_routers.begin(), limit = group.m_routers.end(); spot != limit;
       ++spot, ++i) {
    this->routerElt(i).setAddr(spot->m_addr).setRecvId(spot->m_recv.m_sn);
  }

  // fill caches.
  i = 0;
  for (detail::cache::CacheBag::const_iterator spot = group.m_caches.begin(), limit = group.m_caches.end(); spot != limit;
       ++spot, ++i) {
    this->setCacheAddr(i, spot->idAddr());
  }

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);
  return *this;
}

int
CacheViewComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      m_cache_count    = reinterpret_cast<uint32_t *>(m_base + sizeof(raw_t) + this->getRouterCount() * sizeof(RouterIdElt));
      size_t comp_size = this->calcSize(this->getRouterCount(), this->getCacheCount());
      if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
AssignmentKeyElt &
AssignInfoComp::keyElt()
{
  return access_field(&raw_t::m_key, m_base);
}

AssignmentKeyElt const &
AssignInfoComp::keyElt() const
{
  return access_field(&raw_t::m_key, m_base);
}

uint32_t
AssignInfoComp::getKeyChangeNumber() const
{
  return access_field(&raw_t::m_key, m_base).getChangeNumber();
}

AssignInfoComp &
AssignInfoComp::setKeyChangeNumber(uint32_t n)
{
  access_field(&raw_t::m_key, m_base).setChangeNumber(n);
  return *this;
}

uint32_t
AssignInfoComp::getKeyAddr() const
{
  return access_field(&raw_t::m_key, m_base).getAddr();
}

AssignInfoComp &
AssignInfoComp::setKeyAddr(uint32_t addr)
{
  access_field(&raw_t::m_key, m_base).setAddr(addr);
  return *this;
}

uint32_t
AssignInfoComp::getRouterCount() const
{
  return access_field(&raw_t::m_routers, m_base).getCount();
}

RouterAssignElt &
AssignInfoComp::routerElt(int idx)
{
  return access_field(&raw_t::m_routers, m_base).elt(idx);
}

uint32_t
AssignInfoComp::getCacheCount() const
{
  return ntohl(*m_cache_count);
}

uint32_t
AssignInfoComp::getCacheAddr(int idx) const
{
  return m_cache_count[idx + 1];
}

AssignInfoComp &
AssignInfoComp::setCacheAddr(int idx, uint32_t addr)
{
  m_cache_count[idx + 1] = addr;
  return *this;
}

size_t
AssignInfoComp::calcSize(int n_routers, int n_caches)
{
  return sizeof(raw_t) + RouterAssignListElt::calcVarSize(n_routers) + HashAssignElt::calcSize(n_caches);
}

uint32_t *
AssignInfoComp::calcCacheCountPtr()
{
  return reinterpret_cast<uint32_t *>(m_base + sizeof(raw_t) + access_field(&raw_t::m_routers, m_base).getVarSize());
}

AssignInfoComp::Bucket *
AssignInfoComp::calcBucketPtr()
{
  return reinterpret_cast<Bucket *>(reinterpret_cast<char *>(m_cache_count) + sizeof(uint32_t) * (1 + this->getCacheCount()));
}

AssignInfoComp &
AssignInfoComp::fill(MsgBuffer &buffer, detail::Assignment const &assign)
{
  RouterAssignListElt const &ralist = assign.getRouterList();
  HashAssignElt const &ha           = assign.getHash();
  size_t n_routers                  = ralist.getCount();
  size_t n_caches                   = ha.getCount();
  size_t comp_size                  = this->calcSize(n_routers, n_caches);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE);
  this->keyElt() = assign.getKey();
  memcpy(&(access_field(&raw_t::m_routers, m_base)), &ralist, ralist.getSize());
  // Set the pointer to the count of caches and write the count.
  m_cache_count = this->calcCacheCountPtr();
  memcpy(m_cache_count, &ha, ha.getSize());

  this->setLength(comp_size - HEADER_SIZE);
  buffer.use(comp_size);

  return *this;
}

int
AssignInfoComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < HEADER_SIZE)
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      int n_routers = this->getRouterCount();
      int n_caches;
      m_cache_count    = this->calcCacheCountPtr();
      n_caches         = this->getCacheCount();
      m_buckets        = this->calcBucketPtr();
      size_t comp_size = this->calcSize(n_routers, n_caches);
      if (this->getLength() != comp_size - HEADER_SIZE)
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  if (PARSE_SUCCESS != zret)
    m_base = 0;
  return zret;
}

AssignmentKeyElt &
AltAssignComp::keyElt()
{
  return access_field(&raw_t::m_key, m_base);
}

AssignmentKeyElt const &
AltAssignComp::keyElt() const
{
  return access_field(&raw_t::m_key, m_base);
}

void *
AltAssignComp::calcVarPtr()
{
  return reinterpret_cast<void *>(m_base + sizeof(raw_t) + access_field(&raw_t::m_routers, m_base).getVarSize());
}

uint32_t
AltAssignComp::getRouterCount() const
{
  return access_field(&raw_t::m_routers, m_base).getCount();
}

uint32_t
AltHashAssignComp::getCacheCount() const
{
  return ntohl(*m_cache_count);
}

size_t
AltHashAssignComp::calcSize(int n_routers, int n_caches)
{
  return sizeof(raw_t) + RouterAssignListElt::calcVarSize(n_routers) + HashAssignElt::calcSize(n_caches);
}

AltHashAssignComp &
AltHashAssignComp::fill(MsgBuffer &buffer, detail::Assignment const &assign)
{
  RouterAssignListElt const &ralist = assign.getRouterList();
  HashAssignElt const &ha           = assign.getHash();
  size_t n_routers                  = ralist.getCount();
  size_t n_caches                   = ha.getCount();
  size_t comp_size                  = this->calcSize(n_routers, n_caches);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE)
    .setLength(comp_size - HEADER_SIZE)
    .setAssignType(ALT_HASH_ASSIGNMENT)
    .setAssignLength(comp_size - HEADER_SIZE - sizeof(local_header_t));
  this->keyElt() = assign.getKey();
  memcpy(&(access_field(&raw_t::m_routers, m_base)), &ralist, ralist.getSize());
  // Set the pointer to the count of caches and write the count.
  m_cache_count = static_cast<uint32_t *>(this->calcVarPtr());
  memcpy(m_cache_count, &ha, ha.getSize());

  buffer.use(comp_size);

  return *this;
}

int
AltHashAssignComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      int n_routers = this->getRouterCount();
      int n_caches;
      m_cache_count    = static_cast<uint32_t *>(this->calcVarPtr());
      n_caches         = this->getCacheCount();
      size_t comp_size = this->calcSize(n_routers, n_caches);
      if (this->getLength() != comp_size - HEADER_SIZE)
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  if (PARSE_SUCCESS != zret)
    m_base = 0;
  return zret;
}

AltMaskAssignComp &
AltMaskAssignComp::fill(MsgBuffer &buffer, detail::Assignment const &assign)
{
  RouterAssignListElt const &ralist = assign.getRouterList();
  MaskAssignElt const &ma           = assign.getMask();
  size_t comp_size                  = sizeof(raw_t) + ralist.getVarSize() + ma.getSize();

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE)
    .setLength(comp_size - HEADER_SIZE)
    .setAssignType(ALT_MASK_ASSIGNMENT)
    .setAssignLength(comp_size - HEADER_SIZE - sizeof(local_header_t));
  this->keyElt() = assign.getKey();

  memcpy(&(access_field(&raw_t::m_routers, m_base)), &ralist, ralist.getSize());
  m_mask_elt = static_cast<MaskAssignElt *>(this->calcVarPtr());
  memcpy(m_mask_elt, &ma, ma.getSize());

  buffer.use(comp_size);

  return *this;
}

int
AltMaskAssignComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      RouterAssignListElt *ralist = &(access_field(&raw_t::m_routers, m_base));
      m_mask_elt                  = static_cast<MaskAssignElt *>(this->calcVarPtr());
      size_t comp_size            = sizeof(raw_t) + ralist->getVarSize() + m_mask_elt->getSize();
      if (this->getLength() != comp_size - HEADER_SIZE)
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  if (PARSE_SUCCESS != zret)
    m_base = 0;
  return zret;
}

// ------------------------------------------------------
CmdComp::cmd_t
CmdComp::getCmd() const
{
  return static_cast<cmd_t>(get_field(&raw_t::m_cmd, m_base));
}

CmdComp &
CmdComp::setCmd(cmd_t cmd)
{
  set_field(&raw_t::m_cmd, m_base, cmd);
  return *this;
}

uint32_t
CmdComp::getCmdData() const
{
  return get_field(&raw_t::m_cmd_data, m_base);
}

CmdComp &
CmdComp::setCmdData(uint32_t data)
{
  set_field(&raw_t::m_cmd_data, m_base, data);
  return *this;
}

inline size_t
CmdComp::calcSize()
{
  return sizeof(raw_t);
}

CmdComp &
CmdComp::fill(MsgBuffer &buffer, cmd_t cmd, uint32_t data)
{
  size_t comp_size = this->calcSize();

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setCmd(cmd).setCmdData(data).setLength(sizeof(raw_t) - sizeof(super::raw_t));
  // Command length is always the same.
  set_field(&raw_t::m_length, m_base, sizeof(uint32_t));
  //  reinterpret_cast<raw_t*>(m_base)->m_length = htons(sizeof(uint32_t));
  return *this;
}

int
CmdComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      if (this->getLength() + sizeof(super::raw_t) != this->calcSize())
        zret = PARSE_COMP_WRONG_SIZE;
    }
  }
  return zret;
}
// ------------------------------------------------------
CapabilityElt &
CapComp::elt(int idx)
{
  return access_array<CapabilityElt>(m_base + sizeof(super::raw_t))[idx];
}

CapabilityElt const &
CapComp::elt(int idx) const
{
  return access_array<CapabilityElt>(m_base + sizeof(super::raw_t))[idx];
}

void
CapComp::cache() const
{
  uint32_t x; // scratch for bounds checking.
  // Reset all values.
  m_packet_forward = ServiceGroup::NO_PACKET_STYLE;
  m_packet_return  = ServiceGroup::NO_PACKET_STYLE;
  m_cache_assign   = ServiceGroup::NO_CACHE_ASSIGN_STYLE;
  if (!m_base)
    return; // No data, everything is default.
  // Load from data.
  for (uint32_t i = 0, n = this->getEltCount(); i < n; ++i) {
    CapabilityElt const &elt = this->elt(i);
    switch (elt.getCapType()) {
    case CapabilityElt::PACKET_FORWARD_METHOD:
      x = elt.getCapData();
      if (0 < x && x < 4)
        m_packet_forward = static_cast<ServiceGroup::PacketStyle>(x);
      break;
    case CapabilityElt::PACKET_RETURN_METHOD:
      x = elt.getCapData();
      if (0 < x && x < 4)
        m_packet_return = static_cast<ServiceGroup::PacketStyle>(x);
      break;
    case CapabilityElt::CACHE_ASSIGNMENT_METHOD:
      x = elt.getCapData();
      if (0 < x && x < 4)
        m_cache_assign = static_cast<ServiceGroup::CacheAssignmentStyle>(x);
      break;
    default:
      logf(LVL_INFO, "Invalid capability type %d in packet.", elt.getCapType());
      break;
    }
  }
  m_cached = true;
}

CapComp &
CapComp::fill(MsgBuffer &buffer, int n)
{
  size_t comp_size = this->calcSize(n);
  m_cached         = false;

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();
  this->setType(COMP_TYPE).setLength(comp_size - sizeof(super::raw_t));
  m_count = n;
  buffer.use(comp_size);

  return *this;
}

int
CapComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  m_cached = false;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      // No explicit count, compute it from length.
      m_count = this->getLength() / sizeof(CapabilityElt);
      buffer.use(this->getLength() + sizeof(super::raw_t));
    }
  }
  return zret;
}
// ------------------------------------------------------
int
QueryComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t))
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret)
      buffer.use(this->calcSize());
  }
  return zret;
}
// ------------------------------------------------------
uint32_t
AssignMapComp::getCount() const
{
  return access_field(&raw_t::m_assign, m_base).getCount();
}

AssignMapComp &
AssignMapComp::fill(MsgBuffer &buffer, detail::Assignment const &assign)
{
  size_t comp_size        = sizeof(raw_t);
  MaskAssignElt const &ma = assign.getMask();
  size_t ma_size          = ma.getSize(); // Not constant time.

  // Can't be precise, but we need at least one mask/value set with
  // at least one value. If we don't have that it's a clear fail.
  if (buffer.getSpace() < comp_size + sizeof(MaskValueSetElt::calcSize(1)))
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();
  memcpy(&(access_field(&raw_t::m_assign, m_base)), &ma, ma_size);
  comp_size += ma_size - sizeof(ma);

  this->setType(COMP_TYPE).setLength(comp_size - HEADER_SIZE);
  buffer.use(comp_size);

  return *this;
}

int
AssignMapComp::parse(MsgBuffer &buffer)
{
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < HEADER_SIZE)
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret   = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret) {
      // TBD - Actually check the mask/value set data !!
      buffer.use(this->getLength() + HEADER_SIZE);
    }
  }
  if (PARSE_SUCCESS != zret)
    m_base = 0;
  return zret;
}
// ------------------------------------------------------
detail::Assignment::Assignment() : m_key(0, 0), m_active(false), m_router_list(0), m_hash_assign(0), m_mask_assign(0) {}

bool
detail::Assignment::fill(cache::GroupData &group, uint32_t addr)
{
  // Compute the last packet received times for the routers.  For each
  // cache, compute how many routers mentioned it in their last
  // packet. Prepare an assignment from those caches.  If there are no
  // such caches, fail the assignment.  Any cache that wasn't
  // mentioned by at least one router are purged.

  size_t n_routers = group.m_routers.size(); // routers in group
  size_t n_caches  = group.m_caches.size();  // caches in group

  // We need both routers and caches to do something useful.
  if (!(n_routers && n_caches))
    return false;

  // Iteration vars.
  size_t cdx, rdx;
  cache::RouterBag::iterator rspot, rbegin = group.m_routers.begin(), rend = group.m_routers.end();
  cache::CacheBag::iterator cspot, cbegin = group.m_caches.begin(), cend = group.m_caches.end();

  size_t nr[n_caches];       // validity check counts.
  memset(nr, 0, sizeof(nr)); // Set counts to zero.

  // Guess at size of serialization buffer. For the router list and
  // the hash assignment, we can compute reasonable upper bounds and so
  // we don't have to do space checks when those get filled out.
  // The mask assignment is more difficult. We just guess generously and
  // try to recover if we go over.
  size_t size = RouterAssignListElt::calcSize(n_routers) + HashAssignElt::calcSize(n_caches) + 4096;

  if (m_buffer.getSize() < size) {
    ats_free(m_buffer.getBase());
    m_buffer.set(ats_malloc(size), size);
  }
  m_buffer.reset();

  // Set assignment key
  m_key.setAddr(addr).setChangeNumber(group.m_generation);

  m_router_list = reinterpret_cast<RouterAssignListElt *>(m_buffer.getBase());
  new (m_router_list) RouterAssignListElt(n_routers);

  // For each router, update the assignment and run over the caches
  // and bump the nr count if that cache was included in the most
  // recent packet. Note that the router data gets updated from
  // ISeeYou message processing as well, this is more of an initial
  // setup.
  for (rdx = 0, rspot = rbegin; rspot != rend; ++rspot, ++rdx) {
    // Update router assignment.
    m_router_list->elt(rdx).setChangeNumber(rspot->m_generation).setAddr(rspot->m_addr).setRecvId(rspot->m_recv.m_sn);
    // Check cache validity.
    for (cdx = 0, cspot = cbegin; cspot != cend; ++cspot, ++cdx) {
      if (cspot->m_src[rdx].m_time == rspot->m_recv.m_time)
        ++(nr[cdx]);
    }
  }

  size_t k = m_router_list->getSize();
  m_buffer.use(k);
  m_hash_assign = reinterpret_cast<HashAssignElt *>(m_buffer.getTail());

  // If the nr value is 0, then the cache was not included in any
  // last packet, so it should be discarded. A cache is valid if
  // nr is n_routers, indicating that every router mentioned it.
  int v_caches = 0; // valid caches
  for (cdx = 0, cspot = cbegin; cspot != cend; ++cspot, ++cdx)
    if (nr[cdx] == n_routers) {
      m_hash_assign->setAddr(cdx, cspot->idAddr());
      ++v_caches;
    }

  if (!v_caches) { // no valid caches.
    log(LVL_INFO, "Attempted to generate cache assignment but no valid caches were found.");
    return false;
  }
  // Just sets the cache count.
  new (m_hash_assign) HashAssignElt(v_caches);
  m_hash_assign->round_robin_assign();
  m_buffer.use(m_hash_assign->getSize());

  m_mask_assign = reinterpret_cast<MaskAssignElt *>(m_buffer.getTail());
  new (m_mask_assign) MaskAssignElt;

  // For now, hardwire everything to first cache.
  // We have plenty of space, but will need to check if we do something
  // more complex here.
  m_mask_assign->init(0, 0, 0, 0)->addValue(m_hash_assign->getAddr(0), 0, 0, 0, 0);

  logf(LVL_INFO, "Generated assignment for group %d with %d routers, %d valid caches.", group.m_svc.getSvcId(), n_routers,
       v_caches);

  return true;
}
// ------------------------------------------------------
void
BaseMsg::setBuffer(MsgBuffer const &buffer)
{
  m_buffer = buffer;
}

void
BaseMsg::finalize()
{
  m_header.setLength(m_buffer.getCount() - m_header.calcSize());
  m_security.secure(m_buffer);
}

bool
BaseMsg::validateSecurity() const
{
  return m_security.validate(m_buffer);
}
// ------------------------------------------------------
void
HereIAmMsg::fill(detail::cache::GroupData const &group, CacheIdBox const &cache_id, SecurityOption sec_opt)
{
  m_header.fill(m_buffer, HERE_I_AM);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  m_cache_id.fill(m_buffer, cache_id);
  m_cache_view.fill(m_buffer, group);
}

void
HereIAmMsg::fill_caps(detail::cache::RouterData const &router)
{
  if (router.m_send_caps) {
    m_capabilities.fill(m_buffer, 3);
    m_capabilities.elt(0) = CapabilityElt(CapabilityElt::PACKET_FORWARD_METHOD, router.m_packet_forward);
    m_capabilities.elt(1) = CapabilityElt(CapabilityElt::CACHE_ASSIGNMENT_METHOD, router.m_cache_assign);
    m_capabilities.elt(2) = CapabilityElt(CapabilityElt::PACKET_RETURN_METHOD, router.m_packet_return);
  }
}

int
HereIAmMsg::parse(ts::Buffer const &buffer)
{
  int zret;
  this->setBuffer(buffer);
  if (!m_buffer.getBase())
    return -EINVAL;
  zret = m_header.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;
  if (HERE_I_AM != m_header.getType())
    return PARSE_MSG_WRONG_TYPE;

  // Time to look for components.
  zret = m_security.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_service.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_cache_id.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_cache_view.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  // These are optional so failures can be ignored.
  if (m_buffer.getSpace())
    m_capabilities.parse(m_buffer);
  if (m_buffer.getSpace())
    m_command.parse(m_buffer);

  return m_buffer.getSpace() ? PARSE_DATA_OVERRUN : PARSE_SUCCESS;
}
// ------------------------------------------------------
void
RedirectAssignMsg::fill(detail::cache::GroupData const &group, SecurityOption sec_opt)
{
  m_header.fill(m_buffer, REDIRECT_ASSIGN);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  switch (group.m_cache_assign) {
  case ServiceGroup::HASH_ONLY:
    m_hash_assign.fill(m_buffer, group.m_assign_info);
    break;
  case ServiceGroup::MASK_ONLY:
    m_alt_mask_assign.fill(m_buffer, group.m_assign_info);
    break;
  default:
    logf(LVL_WARN, "Bad assignment type [%d] for REDIRECT_ASSIGN", group.m_cache_assign);
    break;
  }
}
// ------------------------------------------------------
void
ISeeYouMsg::fill(detail::router::GroupData const &group, SecurityOption sec_opt, detail::Assignment & /* assign ATS_UNUSED */,
                 size_t to_caches, size_t n_routers, size_t n_caches, bool /* send_capabilities ATS_UNUSED */
)
{
  m_header.fill(m_buffer, I_SEE_YOU);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  m_router_id.fill(m_buffer, to_caches);
  m_router_view.fill(m_buffer, n_routers, n_caches);
}

int
ISeeYouMsg::parse(ts::Buffer const &buffer)
{
  int zret;
  this->setBuffer(buffer);
  if (!m_buffer.getBase())
    return -EINVAL;
  zret = m_header.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;
  if (I_SEE_YOU != m_header.getType())
    return PARSE_MSG_WRONG_TYPE;

  // Time to look for components.
  zret = m_security.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_service.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_router_id.parse(m_buffer);
  if (PARSE_SUCCESS != zret) {
    logf(LVL_DEBUG, "I_SEE_YOU: Invalid %d router id", zret);
    return zret;
  }

  zret = m_router_view.parse(m_buffer);
  if (PARSE_SUCCESS != zret) {
    logf(LVL_DEBUG, "I_SEE_YOU: Invalid %d router view", zret);
    return zret;
  }

  // Optional components.

  // Test for alternates here
  // At most one of the asssignments but never both.
  // Can be omitted.
  m_assignment.parse(m_buffer);
  m_map.parse(m_buffer);

  // Optional components.
  m_capabilities.parse(m_buffer);
  m_command.parse(m_buffer);

  if (m_buffer.getSpace()) {
    zret = PARSE_DATA_OVERRUN;
    logf(LVL_DEBUG, "I_SEE_YOU: Data overrun %lu", m_buffer.getSpace());
  }

  return zret;
}
// ------------------------------------------------------
int
RemovalQueryMsg::parse(ts::Buffer const &buffer)
{
  int zret;
  this->setBuffer(buffer);
  if (!m_buffer.getBase())
    return -EINVAL;
  zret = m_header.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;
  if (REMOVAL_QUERY != m_header.getType())
    return PARSE_MSG_WRONG_TYPE;

  // Get the components.
  zret = m_security.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_service.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  zret = m_query.parse(m_buffer);
  if (PARSE_SUCCESS != zret)
    return zret;

  return m_buffer.getSpace() ? PARSE_DATA_OVERRUN : PARSE_SUCCESS;
}
// ------------------------------------------------------
} // namespace wccp
