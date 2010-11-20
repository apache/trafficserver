# include "WccpLocal.h"
# include <errno.h>
# include <openssl/md5.h>
# include <TsException.h>

namespace wccp {
// ------------------------------------------------------
// ------------------------------------------------------
ServiceGroup&
ServiceGroup::setSvcType(ServiceGroup::Type t) {
  if (STANDARD == t) {
    // For standard service, everything past ID must be zero.
    memset(&m_priority, 0,
      sizeof(this) - (
        reinterpret_cast<char*>(&m_priority) - reinterpret_cast<char*>(this)
      )
    );
  }
  m_svc_type = t; // store actual type.
  return *this;
}

bool
ServiceGroup::operator == (self const& that) const {
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
    return m_svc_id == that.m_svc_id
      && m_protocol == that.m_protocol
      && m_flags == that.m_flags
      && m_priority == that.m_priority
      && 0 == memcmp(m_ports, that.m_ports, sizeof(m_ports))
      ;
  }
}
// ------------------------------------------------------
// ------------------------------------------------------
CacheIdElt&
CacheIdElt::setBucket(int idx, bool state) {
  uint8_t& bucket = m_buckets[idx>>3];
  uint8_t mask = 1 << (idx & 7);
  if (state) bucket |= mask;
  else bucket &= !mask;
  return *this;
}

CacheIdElt&
CacheIdElt::setBuckets(bool state) {
  memset(m_buckets, state ? 0xFF : 0, sizeof(m_buckets));
  return *this;
}
// ------------------------------------------------------
inline CapabilityElt::Type
CapabilityElt::getCapType() const {
  return static_cast<Type>(ntohs(m_cap_type));
}

inline CapabilityElt&
CapabilityElt::setCapType(Type cap) {
  m_cap_type = htons(cap);
  return *this;
}

inline uint32
CapabilityElt::getCapData() const {
  return ntohl(m_cap_data);
}

inline CapabilityElt&
CapabilityElt::setCapData(uint32 data) {
  m_cap_data = htonl(data);
  return *this;
}

CapabilityElt::CapabilityElt() {
}

CapabilityElt::CapabilityElt(Type cap, uint32 data) {
  this->setCapType(cap);
  this->setCapData(data);
  m_cap_length = htons(sizeof(uint32));
}
// ------------------------------------------------------
inline uint32
MaskElt::getf_src_addr_mask() const {
  return ntohl(m_src_addr_mask);
}

inline MaskElt&
MaskElt::setf_src_addr_mask(uint32 mask) {
  m_src_addr_mask = htonl(mask);
  return *this;
}

inline uint32
MaskElt::getf_dst_addr_mask() const {
  return ntohl(m_dst_addr_mask);
}

inline MaskElt&
MaskElt::setf_dst_addr_mask(uint32 mask) {
  m_dst_addr_mask = htonl(mask);
  return *this;
}

inline uint16_t
MaskElt::getf_src_port_mask() const {
  return ntohs(m_src_port_mask);
}

inline MaskElt&
MaskElt::setf_src_port_mask(uint16_t mask) {
  m_src_port_mask = htons(mask);
  return *this;
}

inline uint16_t
MaskElt::getf_dst_port_mask() const {
  return ntohs(m_dst_port_mask);
}

inline MaskElt&
MaskElt::setf_dst_port_mask(uint16_t mask) {
  m_dst_port_mask = htons(mask);
  return *this;
}
// ------------------------------------------------------
inline uint32
ValueElt::getf_src_addr() const {
  return ntohl(m_src_addr);
}

inline ValueElt&
ValueElt::setf_src_addr(uint32 addr) {
  m_src_addr = htonl(addr);
  return *this;
}

inline uint32
ValueElt::getf_dst_addr() const {
  return ntohl(m_dst_addr);
}

inline ValueElt&
ValueElt::setf_dst_addr(uint32 addr) {
  m_dst_addr = htonl(addr);
  return *this;
}

inline uint16_t
ValueElt::getf_src_port() const {
  return ntohs(m_src_port);
}

inline ValueElt&
ValueElt::setf_src_port(uint16_t port) {
  m_src_port = htons(port);
  return *this;
}

inline uint16_t
ValueElt::getf_dst_port() const {
  return ntohs(m_dst_port);
}

inline ValueElt&
ValueElt::setf_dst_port(uint16_t port) {
  m_dst_port = htons(port);
  return *this;
}

inline uint32
ValueElt::getCacheAddr() const {
  return ntohl(m_cache_addr);
}

inline ValueElt&
ValueElt::setCacheAddr(uint32 addr) {
  m_cache_addr = htonl(addr);
  return *this;
}
// ------------------------------------------------------
MaskValueSetElt::MaskValueSetElt() {
}

MaskValueSetElt::MaskValueSetElt(uint32 count) 
  : m_count(count) {
}

inline size_t
MaskValueSetElt::calcSize() const {
  return sizeof(self) + ntohl(m_count) * sizeof(ValueElt);
}

inline MaskElt&
MaskValueSetElt::atf_mask() {
  return m_mask;
}

inline uint32
MaskValueSetElt::getf_count() const {
  return ntohl(m_count);
}

inline uint32
MaskValueSetElt::getf_src_addr_mask() const {
  return m_mask.getf_src_addr_mask();
}

inline MaskValueSetElt&
MaskValueSetElt::setf_src_addr_mask(uint32 mask) {
  m_mask.setf_src_addr_mask(mask);
  return *this;
}

inline uint32
MaskValueSetElt::getf_dst_addr_mask() const {
  return m_mask.getf_dst_addr_mask();
}

inline MaskValueSetElt&
MaskValueSetElt::setf_dst_addr_mask(uint32 mask) {
  m_mask.setf_dst_addr_mask(mask);
  return *this;
}

inline uint16_t
MaskValueSetElt::getf_src_port_mask() const {
  return m_mask.getf_src_port_mask();
}

inline MaskValueSetElt&
MaskValueSetElt::setf_src_port_mask(uint16_t mask) {
  m_mask.setf_src_port_mask(mask);
  return *this;
}

inline uint16_t
MaskValueSetElt::getf_dst_port_mask() const {
  return m_mask.getf_dst_port_mask();
}

inline MaskValueSetElt&
MaskValueSetElt::setf_dst_port_mask(uint16_t mask) {
  m_mask.setf_dst_port_mask(mask);
  return *this;
}
// ------------------------------------------------------
message_type_t
MsgHeaderComp::getType() {
  return static_cast<message_type_t>(get_field(&raw_t::m_type, m_base));
}

uint16_t
MsgHeaderComp::getVersion() {
  return get_field(&raw_t::m_version, m_base);
}

uint16_t
MsgHeaderComp::getLength() {
  return get_field(&raw_t::m_length, m_base);
}

MsgHeaderComp&
MsgHeaderComp::setType(message_type_t type) {
  set_field(&raw_t::m_type, m_base, type);
  return *this;
}

MsgHeaderComp&
MsgHeaderComp::setVersion(uint16_t version) {
  set_field(&raw_t::m_version, m_base, version);
  return *this;
}

MsgHeaderComp&
MsgHeaderComp::setLength(uint16_t length) {
  set_field(&raw_t::m_length, m_base, length);
  return *this;
}

size_t
MsgHeaderComp::calcSize() {
  return sizeof(raw_t);
}

MsgHeaderComp&
MsgHeaderComp::fill(MsgBuffer& buffer, message_type_t t) {
  size_t comp_size = this->calcSize();
  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);
  m_base = buffer.getTail();
  buffer.use(comp_size);
  this->setType(t).setVersion(VERSION).setLength(0);
  return *this;
}

int
MsgHeaderComp::parse(MsgBuffer& base) {
  int zret = PARSE_SUCCESS;
  size_t comp_size = this->calcSize();
  if (base.getSpace() < comp_size) {
    zret = PARSE_BUFFER_TOO_SMALL;
  } else {
    m_base = base.getTail();
    // Length field puts end of message past end of buffer.
    if (this->getLength() + comp_size > base.getSpace()) {
      zret = PARSE_MSG_TOO_BIG;
    } else if (INVALID_MSG_TYPE == this->toMsgType(get_field(&raw_t::m_type, m_base))
    ) {
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
SecurityComp::getOption() const {
  return static_cast<Option>(get_field(&RawNone::m_option, m_base));
}

SecurityComp&
SecurityComp::setOption(Option opt) {
  set_field(&RawNone::m_option, m_base, static_cast<uint32>(opt));
  return *this;
}

SecurityComp&
SecurityComp::setKey(char const* key) {
  m_local_key = true;
  memset(m_key, 0, KEY_SIZE);
  strncpy(m_key, key, KEY_SIZE);
  return *this;
}

void
SecurityComp::setDefaultKey(char const* key) {
  memset(m_default_key, 0, KEY_SIZE);
  strncpy(m_default_key, key, KEY_SIZE);
}

SecurityComp&
SecurityComp::fill(MsgBuffer& buffer, Option opt) {
  size_t comp_size = this->calcSize(opt);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();
 
  this->setType(COMP_TYPE)
    .setLength(comp_size - sizeof(super::raw_t))
    .setOption(opt)
    ;

  if (SECURITY_NONE != opt) {
    RawMD5::HashData& data = access_field(&RawMD5::m_data, m_base); 
    memset(data, 0, sizeof(data));
  }

  buffer.use(comp_size);
  return *this;
}

SecurityComp&
SecurityComp::secure(MsgBuffer const& msg) {
  if (SECURITY_MD5 == this->getOption()) {
    MD5_CTX ctx;
    char const* key = m_local_key ? m_key : m_default_key;
    MD5_Init(&ctx);
    MD5_Update(&ctx, key, KEY_SIZE);
    MD5_Update(&ctx, msg.getBase(), msg.getCount());
    MD5_Final(access_field(&RawMD5::m_data, m_base), &ctx);
  }
  return *this;
}

bool
SecurityComp::validate(MsgBuffer const& msg) const {
  bool zret = true;
  if (SECURITY_MD5 == this->getOption()) {
    RawMD5::HashData save;
    RawMD5::HashData& org = const_cast<RawMD5::HashData&>(access_field(&RawMD5::m_data, m_base));
    MD5_CTX ctx;
    char const* key = m_local_key ? m_key : m_default_key;
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
SecurityComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
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

ServiceComp&
ServiceComp::setPort(int idx, uint16_t port) {
  this->access()->setPort(idx, port);
  m_port_count = std::max(m_port_count, idx);
  return *this;
}

ServiceComp&
ServiceComp::addPort(uint16_t port) {
  if (m_port_count < static_cast<int>(ServiceGroup::N_PORTS))
    this->access()->setPort(m_port_count++, port);
  return *this;
}

ServiceComp&
ServiceComp::clearPorts() {
  this->access()->clearPorts();
  m_port_count = 0;
  return *this;
}

ServiceComp&
ServiceComp::fill(MsgBuffer& buffer, ServiceGroup const& svc) {
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
    &static_cast<ServiceGroup&>(*reinterpret_cast<raw_t*>(m_base)),
    &svc,
    sizeof(svc)
  );
  buffer.use(comp_size);
  return *this;
}

int
ServiceComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  size_t comp_size = this->calcSize();
  if (buffer.getSpace() < comp_size) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      ServiceGroup::Type svc  = this->getSvcType();
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
RouterIdElt&
RouterIdComp::idElt() {
  return access_field(&raw_t::m_id, m_base);
}

RouterIdElt const&
RouterIdComp::idElt() const {
  return access_field(&raw_t::m_id, m_base);
}

RouterIdComp&
RouterIdComp::setIdElt(uint32 addr, uint32 recv_id) {
  this->idElt().setAddr(addr).setRecvId(recv_id);
  return *this;
}

uint32 RouterIdComp::getAddr() const { return this->idElt().getAddr(); }

RouterIdComp&
RouterIdComp::setAddr(uint32 addr) {
  this->idElt().setAddr(addr);
  return *this;
}

uint32 RouterIdComp::getRecvId() const { return this->idElt().getRecvId();}
inline RouterIdComp&
RouterIdComp::setRecvId(uint32 id) {
  this->idElt().setRecvId(id);
  return *this;
}

uint32
RouterIdComp::getToAddr() const {
  return access_field(&raw_t::m_to_addr, m_base);
}

RouterIdComp&
RouterIdComp::setToAddr(uint32 addr) {
  access_field(&raw_t::m_to_addr, m_base) = addr;
  return *this;
}

uint32
RouterIdComp::getFromCount() const {
  return get_field(&raw_t::m_from_count, m_base);
}

uint32
RouterIdComp::getFromAddr(int idx) const {
  return access_array<uint32>(m_base + sizeof(raw_t))[idx];
}

RouterIdComp&
RouterIdComp::setFromAddr(int idx, uint32 addr) {
  access_array<uint32>(m_base + sizeof(raw_t))[idx] =  addr;
  return *this;
}

int
RouterIdComp::findFromAddr(uint32 addr) {
  int n = this->getFromCount();
  uint32* addrs = access_array<uint32>(m_base + sizeof(raw_t)) + n;
  while (n-- != 0 && *--addrs != addr)
    ;
  return n;
}

RouterIdComp&
RouterIdComp::fill(MsgBuffer& buffer, size_t n_caches) {
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

RouterIdComp&
RouterIdComp::fillSingleton(
  MsgBuffer& buffer,
  uint32 addr,
  uint32 recv_count,
  uint32 to_addr,
  uint32 from_addr
) {
  size_t comp_size = this->calcSize(1);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE)
    .setIdElt(addr, recv_count)
    .setToAddr(to_addr)
    .setFromAddr(0, from_addr)
    ;

  set_field(&raw_t::m_from_count, m_base, 1);

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);

  return *this;
}

int
RouterIdComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
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
AssignmentKeyElt&
RouterViewComp::key_elt() {
  return access_field(&raw_t::m_key, m_base);
}

AssignmentKeyElt const&
RouterViewComp::key_elt() const {
  return access_field(&raw_t::m_key, m_base);
}

uint32
RouterViewComp::getChangeNumber() const {
  return get_field(&raw_t::m_change_number, m_base);
}

RouterViewComp&
RouterViewComp::setChangeNumber(uint32 n) {
  set_field(&raw_t::m_change_number, m_base, n);
  return *this;
}
  
uint32
RouterViewComp::getCacheCount() const {
  return ntohl(*m_cache_count);
}

uint32
RouterViewComp::getRouterCount() const {
  return get_field(&raw_t::m_router_count, m_base);
}

CacheIdElt&
RouterViewComp::cacheElt(int idx) {
  return reinterpret_cast<CacheIdElt*>(m_cache_count+1)[idx];
}

uint32
RouterViewComp::getRouterAddr(int idx) const {
  return access_array<uint32>(m_base + sizeof(raw_t))[idx];
}

RouterViewComp&
RouterViewComp::setRouterAddr(int idx, uint32 addr) {
  access_array<uint32>(m_base + sizeof(raw_t))[idx] = addr;
  return *this;
}

size_t
RouterViewComp::calcSize(int n_routers, int n_caches) {
  return sizeof(raw_t)
    + n_routers * sizeof(uint32)
    + sizeof(uint32) + n_caches * sizeof(CacheIdElt)
    ;
}

uint32*
RouterViewComp::calc_cache_count_ptr() {
  return reinterpret_cast<uint32*>(
    m_base + sizeof(raw_t)
    + this->getRouterCount() * sizeof(uint32)
  );
}

RouterViewComp&
RouterViewComp::fill(
  MsgBuffer& buffer,
  int n_routers,
  int n_caches
) {
  size_t comp_size = this->calcSize(n_routers, n_caches);

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

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);

  return *this;
}

int
RouterViewComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      size_t comp_size;
      m_cache_count = this->calc_cache_count_ptr();
      // If the cache count is past the end of the buffer, or
      // the size doesn't match, it's ill formed.
      if ((static_cast<void*>(m_cache_count) >= static_cast<void*>(buffer.getBase() + buffer.getSize()))
        || (this->getLength() != (comp_size =
          this->calcSize(
            this->getRouterCount(),
            this->getCacheCount()
          ))
          - sizeof(super::raw_t)
        ))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
CacheIdComp&
CacheIdComp::fill(MsgBuffer& base, CacheIdElt const& src) {
  size_t comp_size = this->calcSize();

  if (base.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = base.getTail();
  this->setType(COMP_TYPE).setLength(comp_size - sizeof(super::raw_t));
  access_field(&raw_t::m_id, m_base) = src;
  base.use(comp_size);
  return *this;
}

int
CacheIdComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    size_t comp_size = this->calcSize();
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  return zret;
}
// ------------------------------------------------------
uint32
CacheViewComp::getChangeNumber() const {
  return get_field(&raw_t::m_change_number, m_base);
}

CacheViewComp&
CacheViewComp::setChangeNumber(uint32 n) {
  set_field(&raw_t::m_change_number, m_base, n);
  return *this;
}
  
uint32
CacheViewComp::getRouterCount() const {
  return get_field(&raw_t::m_router_count, m_base);
}

uint32
CacheViewComp::getCacheCount() const {
  return ntohl(*m_cache_count);
}

uint32
CacheViewComp::getCacheAddr(int idx) const {
  return ntohl(m_cache_count[idx+1]);
}

CacheViewComp&
CacheViewComp::setCacheAddr(int idx, uint32 addr) {
  m_cache_count[idx+1] = addr;
  return *this;
}

RouterIdElt*
CacheViewComp::atf_router_array() {
  return reinterpret_cast<RouterIdElt*>(m_base + sizeof(raw_t));
}

RouterIdElt&
CacheViewComp::routerElt(int idx) {
  return this->atf_router_array()[idx];
}

RouterIdElt*
CacheViewComp::findf_router_elt(uint32 addr) {
  for ( RouterIdElt *rtr = this->atf_router_array(),
          *limit = rtr + this->getRouterCount() ;
        rtr < limit;
        ++rtr
  ) {
    if (rtr->getAddr() == addr) return rtr;
  }
  return 0;
}

size_t
CacheViewComp::calcSize(int n_routers, int n_caches) {
  return sizeof(raw_t)
    + n_routers * sizeof(RouterIdElt)
    + sizeof(uint32) + n_caches * sizeof(uint32)
    ;
}

CacheViewComp&
CacheViewComp::fill(
  MsgBuffer& buffer,
  uint32 change_number,
  int n_routers,
  int n_caches
) {
  size_t comp_size = this->calcSize(n_routers, n_caches);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE).setChangeNumber(change_number);

  set_field(&raw_t::m_router_count, m_base, n_routers);
//  reinterpret_cast<raw_t*>(m_base)->m_router_count = htonl(n_routers);
  // Set the pointer to the count of caches.
  m_cache_count = reinterpret_cast<uint32*>(
    m_base + sizeof(raw_t) + n_routers * sizeof(RouterIdElt)
  );
  *m_cache_count = htonl(n_caches); // set the actual count.

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);
  return *this;
}

int
CacheViewComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      m_cache_count = reinterpret_cast<uint32*>(
        m_base + sizeof(raw_t)
        + this->getRouterCount() * sizeof(RouterIdElt)
      );
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
AssignmentKeyElt&
AssignInfoComp::keyElt() {
  return access_field(&raw_t::m_key, m_base);
}

AssignmentKeyElt const&
AssignInfoComp::keyElt() const {
  return access_field(&raw_t::m_key, m_base);
}

uint32
AssignInfoComp::getKeyChangeNumber() const {
  return access_field(&raw_t::m_key, m_base).getChangeNumber();
}

AssignInfoComp&
AssignInfoComp::setKeyChangeNumber(uint32 n) {
  access_field(&raw_t::m_key, m_base).setChangeNumber(n);
  return *this;
}
  
uint32
AssignInfoComp::getf_key_addr() const {
  return access_field(&raw_t::m_key, m_base).getAddr();
}

AssignInfoComp&
AssignInfoComp::setKeyAddr(uint32 addr) {
  access_field(&raw_t::m_key, m_base).setAddr(addr);
  return *this;
}

uint32
AssignInfoComp::getRouterCount() const {
  return get_field(&raw_t::m_router_count, m_base);
}

RouterAssignmentElt&
AssignInfoComp::routerElt(int idx) {
  return access_array<RouterAssignmentElt>(m_base + sizeof(raw_t))[idx];
}

uint32
AssignInfoComp::getCacheCount() const {
  return ntohl(*m_cache_count);
}

uint32
AssignInfoComp::getCacheAddr(int idx) const {
  return m_cache_count[idx+1];
}

AssignInfoComp&
AssignInfoComp::setCacheAddr(int idx, uint32 addr) {
  m_cache_count[idx+1] = addr;
  return *this;
}

size_t
AssignInfoComp::calcSize(int n_routers, int n_caches) {
  return sizeof(raw_t)
    + n_routers * sizeof(RouterAssignmentElt)
    + (1 + n_caches) * sizeof(uint32)
    + sizeof(Bucket) * N_BUCKETS
    ;
}

uint32*
AssignInfoComp::calcCacheCountPtr() {
  return reinterpret_cast<uint32*>(m_base + sizeof(raw_t) + this->getRouterCount() * sizeof(RouterAssignmentElt));
}

AssignInfoComp::Bucket*
AssignInfoComp::calcBucketPtr() {
  return reinterpret_cast<Bucket*>(reinterpret_cast<char*>(m_cache_count) + sizeof(uint32) * (1 + this->getCacheCount()));
}  

AssignInfoComp&
AssignInfoComp::fill(
  MsgBuffer& buffer,
  AssignmentKeyElt const& key,
  int n_routers,
  int n_caches,
  Bucket const* buckets
) {
  size_t comp_size = this->calcSize(n_routers, n_caches);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE);
  this->keyElt() = key;

  // Write the router count.
  set_field(&raw_t::m_router_count, m_base, n_routers);
  // Set the pointer to the count of caches and write the count.
  m_cache_count = this->calcCacheCountPtr();
  *m_cache_count = htonl(n_caches);

  // Get the bucket pointer.
  m_buckets = this->calcBucketPtr();
  memcpy(m_buckets, buckets, sizeof(Bucket) * N_BUCKETS);

  this->setLength(comp_size - sizeof(super::raw_t));
  buffer.use(comp_size);

  return *this;
}

AssignInfoComp&
AssignInfoComp::fill(MsgBuffer& buffer, self const& that) {
  size_t comp_size = that.getLength() + sizeof(super::raw_t);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();
  memcpy(m_base, that.m_base, comp_size);
  // Set the pointer to the count of caches.
  m_cache_count = this->calcCacheCountPtr();
  m_buckets = this->calcBucketPtr();
  buffer.use(comp_size);
  return *this;
}

int
AssignInfoComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      int n_routers = this->getRouterCount();
      int n_caches;
      m_cache_count = this->calcCacheCountPtr();
      n_caches = this->getCacheCount();
      m_buckets = this->calcBucketPtr();
      size_t comp_size = this->calcSize(n_routers, n_caches);
      if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  if (PARSE_SUCCESS != zret) m_base = 0;
  return zret;
}

// ------------------------------------------------------
CmdComp::cmd_t
CmdComp::getCmd() const {
  return static_cast<cmd_t>(get_field(&raw_t::m_cmd, m_base));
}

CmdComp&
CmdComp::setCmd(cmd_t cmd) {
  set_field(&raw_t::m_cmd, m_base, cmd);
  return *this;
}

uint32
CmdComp::getCmdData() const {
  return get_field(&raw_t::m_cmd_data, m_base);
}

CmdComp&
CmdComp::setCmdData(uint32 data) {
  set_field(&raw_t::m_cmd_data, m_base, data);
  return *this;
}

inline size_t
CmdComp::calcSize() {
  return sizeof(raw_t);
}

CmdComp&
CmdComp::fill(MsgBuffer& buffer, cmd_t cmd, uint32 data) {
  size_t comp_size = this->calcSize();

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE)
    .setCmd(cmd)
    .setCmdData(data)
    .setLength(sizeof(raw_t) - sizeof(super::raw_t))
    ;
  // Command length is always the same.
  set_field(&raw_t::m_length, m_base, sizeof(uint32));
//  reinterpret_cast<raw_t*>(m_base)->m_length = htons(sizeof(uint32));
  return *this;
}

int
CmdComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      if (this->getLength() + sizeof(super::raw_t) != this->calcSize())
        zret = PARSE_COMP_WRONG_SIZE;
    }
  }
  return zret;
}
// ------------------------------------------------------
CapabilityElt&
CapComp::elt(int idx) {
  return access_array<CapabilityElt>(m_base + sizeof(super::raw_t))[idx];
}

CapabilityElt const&
CapComp::elt(int idx) const {
  return access_array<CapabilityElt>(m_base + sizeof(super::raw_t))[idx];
}

void
CapComp::cache() const {
  uint32 x; // scratch for bounds checking.
  // Reset all values.
  m_packet_forward = ServiceGroup::NO_PACKET_STYLE;
  m_packet_return = ServiceGroup::NO_PACKET_STYLE;
  m_cache_assign = ServiceGroup::NO_CACHE_ASSIGN_STYLE;
  if (!m_base) return; // No data, everything is default.
  // Load from data.
  for ( uint32 i = 0, n = this->getEltCount() ; i < n ; ++i ) {
    CapabilityElt const& elt = this->elt(i);
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

CapComp&
CapComp::fill(MsgBuffer& buffer, int n) {
  size_t comp_size = this->calcSize(n);
  m_cached = false;

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();
  this->setType(COMP_TYPE).setLength(comp_size - sizeof(super::raw_t));
  m_count = n;
  buffer.use(comp_size);

  return *this;
}

int
CapComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  m_cached = false;
  if (buffer.getSpace()< sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      // No explicit count, compute it from length.
      m_count = this->getLength() / sizeof(CapabilityElt);
      buffer.use(this->getLength() + sizeof(super::raw_t));
    }
  }
  return zret;
}
// ------------------------------------------------------
MaskValueSetElt&
AssignMapComp::elt(int idx) {
  return access_array<MaskValueSetElt>(m_base + sizeof(raw_t))[idx];
}

uint32
AssignMapComp::getEltCount() const {
  return get_field(&raw_t::m_count, m_base);
}

inline size_t
AssignMapComp::calcSize(int n) {
  return sizeof(raw_t) + n * sizeof(MaskValueSetElt);
}

AssignMapComp&
AssignMapComp::fill(MsgBuffer& buffer, int n) {
  size_t comp_size = this->calcSize(n);

  if (buffer.getSpace() < comp_size)
    throw ts::Exception(BUFFER_TOO_SMALL_FOR_COMP_TEXT);

  m_base = buffer.getTail();

  this->setType(COMP_TYPE)
    .setLength(comp_size - sizeof(super::raw_t))
    ;
  set_field(&raw_t::m_count, m_base, n);
  buffer.use(comp_size);

  return *this;
}

int
AssignMapComp::parse(MsgBuffer& buffer) {
  int zret = PARSE_SUCCESS;
  if (buffer.getSpace() < sizeof(raw_t)) 
    zret = PARSE_BUFFER_TOO_SMALL;
  else {
    m_base = buffer.getTail();
    zret = this->checkHeader(buffer, COMP_TYPE);
    if (PARSE_SUCCESS == zret ) {
      size_t comp_size = this->calcSize(this->getEltCount());
      if (this->getLength() != comp_size - sizeof(super::raw_t))
        zret = PARSE_COMP_WRONG_SIZE;
      else
        buffer.use(comp_size);
    }
  }
  if (PARSE_SUCCESS != zret) m_base = 0;
  return zret;
}
// ------------------------------------------------------
detail::Assignment::Assignment()
  : m_key(0,0)
  , m_active(false)
  , m_dirty(true) {
  memset(m_buckets, UNASSIGNED_BUCKET, sizeof(m_buckets));
}

void
detail::Assignment::generate() const {
  int i;
  size_t size = AssignInfoComp::calcSize(m_router_keys.size(), m_cache_addrs.size());

  // Resize buffer if needed.
  if (m_buffer.getSize() < size) {
    free(m_buffer.getBase());
    m_buffer.set(malloc(size), size);
  }
  m_buffer.reset();

  // Load basic layout.
  m_comp.fill(m_buffer, m_key, m_router_keys.size(), m_cache_addrs.size(), m_buckets);

  i = 0;
  for ( RouterKeys::const_iterator
          spot = m_router_keys.begin(),
          limit = m_router_keys.end();
        spot != limit;
        ++spot, ++i
  ) {
    m_comp.routerElt(i) = *spot;
  }

  i = 0;
  for ( CacheAddrs::const_iterator
          spot = m_cache_addrs.begin(),
          limit = m_cache_addrs.end();
        spot != limit;
        ++spot, ++i
  ) {
    m_comp.setCacheAddr(i, *spot);
  }
  m_dirty = false;
}

void
detail::Assignment::pour(MsgBuffer& base, AssignInfoComp& comp) const {
  if (m_dirty) this->generate();
  comp.fill(base, m_comp);
}

bool
detail::Assignment::fill(cache::GroupData& group, uint32 addr) {
  // Compute the last packet received times for the routers.
  // For each cache, compute how routers mentioned it in their
  // last packet. Prepare an assignment from those caches.
  // If there are no such caches, fail the assignment.
  // Any cache that wasn't mentioned by at least one router
  // are purged.
  size_t n_routers = group.m_routers.size(); // routers in group
  size_t n_caches = group.m_caches.size(); // caches in group
  size_t v_caches = 0; // valid caches

  this->m_dirty = true;

  logf(LVL_DEBUG, "Generating assignment for group %d.", group.m_svc.getSvcId());

  // We need both routers and caches to do something useful.
  if (! (n_routers && n_caches)) return false;

  // Iteration vars.
  size_t cdx, rdx;
  cache::RouterBag::iterator rspot,
    rbegin = group.m_routers.begin(),
    rend = group.m_routers.end();
  cache::CacheBag::iterator cspot,
    cbegin = group.m_caches.begin(),
    cend = group.m_caches.end();

  size_t nr[n_caches]; // validity check counts.

  // Set assignment key
  m_key.setAddr(addr).setChangeNumber(group.m_generation);

  memset(nr, 0, sizeof(nr)); // Set counts to zero.
  m_router_keys.resize(n_routers);
  // For each router, run over the caches and bump the nr count
  // if that cache was included in the most recent packet.
  for ( rdx = 0, rspot = rbegin ; rspot != rend ; ++rspot, ++rdx ) {
    // Update router key.
    m_router_keys[rdx]
      .setChangeNumber(rspot->m_generation)
      .setAddr(rspot->m_addr)
      .setRecvId(rspot->m_recv.m_sn)
      ;
    // Check cache validity.
    for ( cdx = 0, cspot = cbegin ; cspot != cend ; ++cspot, ++cdx ) {
      if (cspot->m_src[rdx].m_time == rspot->m_recv.m_time)
        ++(nr[cdx]);
    }
  }
  // If the nr value is 0, then the cache was not included in any
  // last packet, so it should be discarded. A cache is valid if
  // nr is n_routers, indicating that every router mentioned it.
  m_cache_addrs.clear();
  m_cache_addrs.reserve(n_caches);
  for ( cdx = 0, cspot = cbegin ; cspot != cend ; ++cspot, ++cdx )
    if (nr[cdx] == n_routers) m_cache_addrs.push_back(cspot->idAddr());
  v_caches = m_cache_addrs.size();

  if (! v_caches) { // no valid caches.
    log(LVL_INFO, "Assignment requested but no valid caches were found.");
    return false;
  }

  if (1 == v_caches) memset(m_buckets, 0, sizeof(m_buckets));
  else { // Assign round robin.
    size_t x = 0;
    for ( Bucket *spot = m_buckets, *limit = m_buckets + N_BUCKETS;
          spot < limit;
          ++spot
    ) {
      spot->m_idx = x;
      spot->m_alt = 0;
      x = ( x + 1 ) % v_caches;
    }
  }
  logf(LVL_INFO, "Generated assignment for group %d with %d routers, %d valid caches.", group.m_svc.getSvcId(), n_routers, v_caches);

  return true;
}
// ------------------------------------------------------
void
BaseMsg::setBuffer(MsgBuffer const& buffer) {
  m_buffer = buffer;
}

void
BaseMsg::finalize() {
  m_header.setLength(m_buffer.getCount() - m_header.calcSize());
  m_security.secure(m_buffer);
}

bool
BaseMsg::validateSecurity() const {
  return m_security.validate(m_buffer);
}
// ------------------------------------------------------
void
HereIAmMsg::fill(
    detail::cache::GroupData const& group,
    SecurityOption sec_opt,
    int n_routers,
    int n_caches
) {
  m_header.fill(m_buffer, HERE_I_AM);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  m_cache_id.fill(m_buffer, group.m_id);
  m_cache_view.fill(m_buffer, group.m_generation, n_routers, n_caches);
}

void
HereIAmMsg::fill_caps(
    detail::cache::RouterData const& router
) {
  if (router.m_send_caps) {
    m_capabilities.fill(m_buffer, 3);
    m_capabilities.elt(0) = 
      CapabilityElt(CapabilityElt::PACKET_FORWARD_METHOD, router.m_packet_forward);
    m_capabilities.elt(1) = 
      CapabilityElt(CapabilityElt::PACKET_RETURN_METHOD, router.m_packet_return);
    m_capabilities.elt(2) = 
      CapabilityElt(CapabilityElt::CACHE_ASSIGNMENT_METHOD, router.m_cache_assign);
  }
}

int
HereIAmMsg::parse(ts::Buffer const& buffer) {
  int zret;
  this->setBuffer(buffer);
  if (!m_buffer.getBase()) return -EINVAL;
  zret = m_header.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;
  if (HERE_I_AM != m_header.getType()) return PARSE_MSG_WRONG_TYPE;

  // Time to look for components.
  zret = m_security.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_service.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_cache_id.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_cache_view.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  // These are optional so failures can be ignored.
  if (m_buffer.getSpace()) m_capabilities.parse(m_buffer);
  if (m_buffer.getSpace()) m_command.parse(m_buffer);

  return m_buffer.getSpace() ? PARSE_DATA_OVERRUN : PARSE_SUCCESS;
}
// ------------------------------------------------------
void
RedirectAssignMsg::fill(
  detail::cache::GroupData const& group,
  SecurityOption sec_opt,
  AssignmentKeyElt const& key,
  int n_routers,
  int n_caches
) {
  m_header.fill(m_buffer, REDIRECT_ASSIGN);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  group.m_assign_info.pour(m_buffer, m_assign);
}
// ------------------------------------------------------
void
ISeeYouMsg::fill(
  detail::router::GroupData const& group,
  SecurityOption sec_opt,
  detail::Assignment& assign,
  size_t to_caches,
  size_t n_routers,
  size_t n_caches,
  bool send_capabilities
) {
  m_header.fill(m_buffer, I_SEE_YOU);
  m_security.fill(m_buffer, sec_opt);
  m_service.fill(m_buffer, group.m_svc);
  m_router_id.fill(m_buffer, to_caches);
  m_router_view.fill(m_buffer, n_routers, n_caches);
  if (assign.isActive()) assign.pour(m_buffer, m_assignment);
}

int
ISeeYouMsg::parse(ts::Buffer const& buffer) {
  int zret;
  this->setBuffer(buffer);
  if (!m_buffer.getBase()) return -EINVAL;
  zret = m_header.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;
  if (I_SEE_YOU != m_header.getType()) return PARSE_MSG_WRONG_TYPE;

  // Time to look for components.
  zret = m_security.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_service.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_router_id.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  zret = m_router_view.parse(m_buffer);
  if (PARSE_SUCCESS != zret) return zret;

  // Optional components.

  // Test for alternates here
  // At most one of the asssignments but never both.
  // Can be omitted.
  m_assignment.parse(m_buffer);
  m_map.parse(m_buffer);

  // Optional components.
  m_capabilities.parse(m_buffer);
  m_command.parse(m_buffer);

  return m_buffer.getSpace() ? PARSE_DATA_OVERRUN : PARSE_SUCCESS;
}
// ------------------------------------------------------
} // namespace wccp
