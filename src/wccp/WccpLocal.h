/** @file
    WCCP (v2) support for Apache Traffic Server.

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

#include "wccp/Wccp.h"
#include "WccpUtil.h"
#include "tscore/TsBuffer.h"
#include "ts/apidefs.h"
// Needed for template use of byte ordering functions.
#include <netinet/in.h>
#include <memory.h>
#include <map>

namespace wccp
{
// Forward declares
namespace detail
{
  class Assignment;
  namespace cache
  {
    struct RouterData;
  }
} // namespace detail

/// Default port used by the protocol.
static unsigned int const DEFAULT_PORT = 2048;
/// Number of buckets in WCCP hash allocation.
static unsigned int const N_BUCKETS = 256;
/// Unassigned bucket value (defined by protocol).
static uint8_t const UNASSIGNED_BUCKET = 0xFF;
/// Size of group password in octets.
static unsigned int const GROUP_PASSWORD_SIZE = 8;
/// Maximum # of caches
static uint32_t const MAX_CACHES = 32;
/// Maximum # of routers
static uint32_t const MAX_ROUTERS = 32;

/// Our version of the protocol.
static unsigned int const VERSION = 0x200;

/// @name Parse results.
/// @note Internal values are positive. System errors are reported as
/// the negative of errno.
//@{
/// Successful parse (message is well formatted)
static int const PARSE_SUCCESS = 0;
/// Component is the wrong type but looks like a valid type.
static int const PARSE_COMP_OTHER_TYPE = 1;
/// Component has a bogus type (cannot be valid).
static int const PARSE_COMP_TYPE_INVALID = 2;
/// Length in message is larger than actual message data.
static int const PARSE_MSG_TOO_BIG = 3;
/// Message header has invalid data.
static int const PARSE_MSG_INVALID = 5;
/// Component is malformed.
static int const PARSE_COMP_INVALID = 4;
/// Message is not the expected type.
static int const PARSE_MSG_WRONG_TYPE = 6;
/// Variable data for component can't fit in remaining data.
static int const PARSE_COMP_TOO_BIG = 7;
/// Fixed data for component can't fit in remaining data.
static int const PARSE_BUFFER_TOO_SMALL = 8;
/// Stored component size doesn't agree with locally computed size.
static int const PARSE_COMP_WRONG_SIZE = 9;
/// More data in message than can be accounted for.
static int const PARSE_DATA_OVERRUN = 10;
//@}

/** Buffer for serialized data.
    Takes the basic ATS buffer and adds a count field to track
    the amount of buffer in use.
*/
class MsgBuffer : protected ts::Buffer
{
public:
  typedef MsgBuffer self;   ///< Self reference type.
  typedef ts::Buffer super; ///< Parent type.

  MsgBuffer(); ///< Default construct empty buffer.
  /// Construct from ATS buffer.
  MsgBuffer(super const &that ///< Instance to copy.
  );
  /// Construct from pointer and size.
  MsgBuffer(void *ptr, ///< Pointer to buffer.
            size_t n   ///< Size of buffer.
  );
  /// Assign a buffer.
  MsgBuffer &set(void *ptr, ///< Pointer to buffer.
                 size_t n   ///< Size of buffer.
  );

  /// Get the buffer size.
  size_t getSize() const;
  /// Get the content size (use count).
  size_t getCount() const;
  /// Get address of first unused byte.
  char *getTail();
  /// Get address of first byte.
  char *getBase();
  /// Get address of first byte.
  const char *getBase() const;
  /// Get the remaining space in the buffer.
  size_t getSpace() const;
  /// Mark additional space in use.
  self &use(size_t n ///< Additional space to mark in use.
  );
  /// Mark all space as unused.
  self &reset();

  /// Reset and zero the buffer.
  self &zero();

  size_t _count; ///< Number of bytes in use.
};

/// Sect 4.4: Cache assignment method.
enum CacheAssignmentType {
  ASSIGNMENT_BY_HASH = 0,
  ASSIGNMENT_BY_MASK = 1,
};

/// Top level message types.
enum message_type_t {
  INVALID_MSG_TYPE = 0,
  HERE_I_AM        = 10,
  I_SEE_YOU        = 11,
  REDIRECT_ASSIGN  = 12,
  REMOVAL_QUERY    = 13,
};

/// Message component type.
/// See Sect 5.1 - 5.4
enum CompType {
  SECURITY_INFO       = 0,
  SERVICE_INFO        = 1,
  ROUTER_ID_INFO      = 2,
  CACHE_ID_INFO       = 3,
  RTR_VIEW_INFO       = 4,
  CACHE_VIEW_INFO     = 5,
  REDIRECT_ASSIGNMENT = 6,
  QUERY_INFO          = 7,
  CAPABILITY_INFO     = 8,
  ALT_ASSIGNMENT      = 13,
  ASSIGN_MAP          = 14,
  COMMAND_EXTENSION   = 15,
  COMP_TYPE_MIN       = SECURITY_INFO,
  COMP_TYPE_MAX       = COMMAND_EXTENSION
};

/// Router Identity.
/// Data is stored in host order. This structure is not used publicly.
struct RouterId {
  typedef RouterId self; ///< Self reference type.

  RouterId(); ///< Default constructor.
  /// Construct from address and sequence number.
  RouterId(uint32_t addr,   ///< Router address.
           uint32_t recv_id ///< Receive ID (sequence number).
  );

  uint32_t m_addr;    ///< Identifying router IP address.
  uint32_t m_recv_id; ///< Receive ID (sequence #).
};

/** Sect 5.7.1: Router Identity Element.

    This maps directly on to message content.

    @internal A @c RouterId with accessors to guarantee correct memory
    layout.
*/
class RouterIdElt : protected RouterId
{
protected:
  typedef RouterId super; ///< Parent type.
public:
  typedef RouterIdElt self; ///< Self reference type.

  /// Default constructor, members zero initialized.
  RouterIdElt();
  /// Construct from address and sequence number.
  RouterIdElt(uint32_t addr,   ///< Router address.
              uint32_t recv_id ///< Receive ID (sequence number).
  );

  /// @name Accessors
  //@{
  uint32_t getAddr() const;     ///< Get the address field.
  self &setAddr(uint32_t addr); ///< Set the address field to @a addr.
  uint32_t getRecvId() const;   ///< Get the receive ID field.
  self &setRecvId(uint32_t id); ///< Set the receive ID field to @a id.
  //@}

  /// Assign from non-serialized variant.
  self &operator=(super const &that);
};

/// Sect 5.7.3: Assignment Key Element
/// @note This maps directly on to message content.
/// @internal: At top level because it is used in more than one component.
class AssignmentKeyElt
{
public:
  typedef AssignmentKeyElt self; ///< Self reference type.

  AssignmentKeyElt(); ///< Default constructor. No member initialization.
  /// Construct from address and sequence number.
  AssignmentKeyElt(uint32_t addr,      ///< Key address.
                   uint32_t generation ///< Change number.
  );

  /// @name Accessors
  //@{
  uint32_t getAddr() const;          ///< Get the address field.
  self &setAddr(uint32_t addr);      ///< Set the address field to @a addr.
  uint32_t getChangeNumber() const;  ///< Get change number field.
  self &setChangeNumber(uint32_t n); ///< Set change number field to @a n.
  //@}
protected:
  uint32_t m_addr;          ///< Identifying router IP address.
  uint32_t m_change_number; ///< Change number (sequence #).
};

/// Sect 5.7.4: Router Assignment Element
/// @note This maps directly on to message content.
/// @internal: At top level because it is used in more than one component.
class RouterAssignElt : public RouterIdElt
{
public:
  typedef RouterAssignElt self; ///< Self reference type.
  typedef RouterIdElt super;    ///< Parent type.

  /// Default constructor, members zero initialized.
  RouterAssignElt();
  /// Construct from address and sequence number.
  RouterAssignElt(uint32_t addr,         ///< Router address.
                  uint32_t recv_id,      ///< Receive ID (sequence number).
                  uint32_t change_number ///< Change number (sequence number).
  );

  /// @name Accessors
  //@{
  uint32_t getChangeNumber() const;  ///< Get change number field.
  self &setChangeNumber(uint32_t n); ///< Set change number field to @a n.
  //@}
protected:
  uint32_t m_change_number; ///< Change number (sequence #).
};

/** List of @c RouterAssignElt
    @note Not explicitly part of the spec, but it shows up in multiple
    places.
 */
class RouterAssignListElt
{
public:
  typedef RouterAssignListElt self; ///< Self reference type.

  /// Default constructor - @b no initialization.
  RouterAssignListElt();
  /// Construct with @n elements.
  RouterAssignListElt(int n ///< Number of elements.
  );

  /// @name Accessors
  //@{
  /// Access element.
  RouterAssignElt &elt(int idx ///< Index of target element.
  );
  /// Access const element.
  RouterAssignElt const &elt(int idx ///< Index of target element.
                             ) const;
  /// Get the number of elements.
  uint32_t getCount() const;
  //@}

  /// Update ID for a router.
  self &updateRouterId(uint32_t addr,  ///< Identifying IP address of router.
                       uint32_t rcvid, ///< New receive ID value.
                       uint32_t cno    ///< New change number.
  );

  /// Get size in bytes of this structure.
  size_t getSize() const;
  /// Get the size of the variable part only.
  /// This is useful for classes that put this element in their
  /// stub structure.
  size_t getVarSize() const;
  /// Calculate size in bytes for @a n elements.
  static size_t calcSize(int n ///< Number of elements.
  );
  /// Calculate size of variable data in bytes for @a n elements.
  static size_t calcVarSize(int n ///< Number of elements.
  );

protected:
  uint32_t m_count; ///< # of elements (network order).
};

/// Sect 5.7.5: Capability Element
/// @note This maps directly on to message content.
class CapabilityElt
{
public:
  typedef CapabilityElt self; ///< Self reference type.

  /// Capability types.
  enum Type : uint16_t {
    NO_METHOD               = 0, ///< Method not set.
    PACKET_FORWARD_METHOD   = 1, ///< Packet forwarding methods.
    CACHE_ASSIGNMENT_METHOD = 2, ///< Cache assignment methods.
    PACKET_RETURN_METHOD    = 3  ///< Packet return methods.
  };

  CapabilityElt(); ///< Default constructor.
  /// Construct from address and sequence number.
  CapabilityElt(Type type,    ///< Capability type.
                uint32_t data ///< Capability data.
  );

  /// @name Accessors
  //@{
  Type getCapType() const; ///< Get the capability type.
  /// Set capability type.
  self &setCapType(Type cap ///< Capability type.
  );
  uint32_t getCapData() const; ///< Get capability data.
  /// Set capability data.
  self &setCapData(uint32_t data ///< Data value.
  );
  //@}
protected:
  enum Type m_cap_type  = NO_METHOD; ///< Capability type.
  uint16_t m_cap_length = 0;         ///< Length of capability data.
  uint32_t m_cap_data   = 0;         ///< Capability data.
};

/// Sect 5.7.7: Mask element
class MaskElt
{
public:
  typedef MaskElt self; ///< Self reference type.

  /// Default constructor - @b no initialization.
  MaskElt();
  /// Construct with specific values.
  MaskElt(uint32_t srcAddr, ///< Mask for source address.
          uint32_t dstAddr, ///< Mask for destination address.
          uint16_t srcPort, ///< Mask for source port.
          uint16_t dstPort  ///< Mask for destination port.
  );

  /// @name Accessors
  //@{
  /// Get source address mask field.
  uint32_t getSrcAddr() const;
  /// Set source address mask field to @a mask.
  self &setSrcAddr(uint32_t mask);
  /// Get destination address field.
  uint32_t getDstAddr() const;
  /// Set destination address field to @a mask.
  self &setDstAddr(uint32_t mask);
  /// Get source port mask field.
  uint16_t getSrcPort() const;
  /// Set source port mask field to @a mask.
  self &setSrcPort(uint16_t mask);
  /// Get destination port mask field.
  uint16_t getDstPort() const;
  /// Set destination port mask field to @a mask.
  self &setDstPort(uint16_t mask);
  //@}

protected:
  uint32_t m_src_addr = INADDR_ANY; ///< Source address mask.
  uint32_t m_dst_addr = INADDR_ANY; ///< Destination address mask.
  uint16_t m_src_port = 0;          ///< Source port mask.
  uint16_t m_dst_port = 0;          ///< Destination port mask.
};

/// Sect 5.7.8: Value element.
class ValueElt
{
public:
  typedef ValueElt self; ///< Self reference type.

  /// Default constructor - @b no initialization.
  ValueElt();
  /// Construct a specific value.
  ValueElt(uint32_t cacheAddr, ///< Address of cache for this value.
           uint32_t srcAddr,   ///< Value for source address.
           uint32_t dstAddr,   ///< Value for destination address.
           uint16_t srcPort,   ///< Value for source port.
           uint16_t dstPort    ///< Value for destination port.
  );

  /// @name Accessors
  //@{
  uint32_t getf_src_addr() const;     ///< Get source address field.
  self &setf_src_addr(uint32_t addr); ///< Set source address field to @a addr.
  uint32_t getDstAddr() const;        ///< Get destination address field.
  self &setf_dst_addr(uint32_t addr); ///< Set destination address field to @a addr.
  uint16_t getf_src_port() const;     ///< Get source port field.
  self &setf_src_port(uint16_t port); ///< Set source port field to @a port.
  uint16_t getDstPort() const;        ///< Get destination port field.
  self &setf_dst_port(uint16_t port); ///< Set destination port field to @a port.
  uint32_t getCacheAddr() const;      ///< Get cache address field.
  self &setCacheAddr(uint32_t addr);  ///< Set cache address field to @a addr
  //@}

protected:
  uint32_t m_src_addr;   ///< Source address.
  uint32_t m_dst_addr;   ///< Destination address.
  uint16_t m_src_port;   ///< Source port.
  uint16_t m_dst_port;   ///< Destination port.
  uint32_t m_cache_addr; ///< Cache address.
};

/** Sect 5.7.6: Mask/Value Set Element
    This is a variable sized element.
 */
class MaskValueSetElt
{
public:
  typedef MaskValueSetElt self; ///< Self reference type.

  MaskValueSetElt(); ///< Default constructor.
  /// Construct from address and sequence number.
  MaskValueSetElt(uint32_t n ///< Value count.
  );

  /// @name Accessors
  //@{
  /// Directly access contained mask element.
  MaskElt &maskElt();

  /// Get source address mask field.
  uint32_t getSrcAddrMask() const;
  /// Set source address mask field to @a mask.
  self &setSrcAddrMask(uint32_t mask);
  /// Get destination address field.
  uint32_t getDstAddrMask() const;
  /// Set destination address field to @a mask.
  self &setDstAddrMask(uint32_t mask);
  /// Get source port mask field.
  uint16_t getSrcPortMask() const;
  /// Set source port mask field to @a mask.
  self &setSrcPortMask(uint16_t mask);
  /// Get destination port mask field.
  uint16_t getDstPortMask() const;
  /// Set destination port mask field to @a mask.
  self &setDstPortMask(uint16_t mask);

  /// Append a value to this set.
  self &addValue(uint32_t cacheAddr, ///< Address of cache for this value.
                 uint32_t srcAddr,   ///< Value for source address.
                 uint32_t dstAddr,   ///< Value for destination address.
                 uint16_t srcPort,   ///< Value for source port.
                 uint16_t dstPort    ///< Value for destination port.
  );

  /// Get the value count.
  /// @note No corresponding @c set because this cannot be directly changed.
  uint32_t getCount() const;
  /// Access value element.
  ValueElt &operator[](int idx ///< Index of target element.
  );
  //@}
  /// Calculate the size of an element with @a n values.
  static size_t calcSize(uint32_t n ///< Number of values.
  );
  /// Get the size (length) of this element.
  size_t getSize() const;

protected:
  // All members are kept in network order.
  MaskElt m_mask;   ///< Base mask element.
  uint32_t m_count; ///< Number of value elements.

  /// Get base address of Value elements.
  ValueElt *values();
  /// Get base address of Value elements.
  ValueElt const *values() const;
};

/// Assignment of caches by hash.
/// Not in specification.
class HashAssignElt
{
public:
  typedef HashAssignElt self; ///< Self reference type.

  /// Hash assignment bucket.
  struct Bucket {
    unsigned int m_idx : 7; ///< Cache index.
    unsigned int m_alt : 1; ///<  Alternate hash flag.

    /// Test for unassigned value in bucket.
    bool is_unassigned() const;
  } __attribute__((aligned(1), packed));

  /// Default constructor - @b no initialization.
  HashAssignElt();
  /// Construct with @n elements.
  HashAssignElt(int n ///< Number of elements.
  );

  /// @name Accessors
  //@{
  /// Get the number of caches.
  uint32_t getCount() const;
  /// Get a cache address.
  uint32_t getAddr(int idx ///< Index of target address.
                   ) const;
  /// Set a cache address.
  self &setAddr(int idx,      ///< Index of target address.
                uint32_t addr ///< Address value to set.
  );
  /// Access a bucket.
  Bucket &operator[](size_t idx ///< Bucket index (0..N_BUCKETS-1)
  );
  /// Access a const bucket.
  Bucket const &operator[](size_t idx ///< Bucket index (0..N_BUCKETS-1)
                           ) const;
  //@}

  /** Do a round robin assignment.
      The buckets are assigned round robin, starting with 0, up to the
      index of the last cache.
      @return @c this.
  */
  self &round_robin_assign();

  /// Get size in bytes of this structure.
  size_t getSize() const;
  /// Calculate size in bytes for @a n caches.
  static size_t calcSize(int n ///< Number of caches.
  );

protected:
  uint32_t m_count; ///< # of caches (network order).

  Bucket *getBucketBase();
};

/** Assignment of caches by mask.
    @note Not in specification.

    @internal Because this is an element, it must correspond exactly
    to the serialized layout. Therefore we can't keep extra accounting
    data around to make manipulation easier. We need a helper class
    for that, which functions in a manner similar to an iterator.
 */

class MaskAssignElt
{
public:
  typedef MaskAssignElt self; ///< Self reference type.

  /** A minimalist insert iterator.
   */
  struct appender {
    typedef appender self; ///< Self reference type.
    /// Get pointer to current set.
    MaskValueSetElt *operator->();
    /// Append a new mask/value set.
    /// @return A pointer to the new set.
    MaskValueSetElt *mask(uint32_t srcAddr, ///< Mask for source address.
                          uint32_t dstAddr, ///< Mask for destination address.
                          uint16_t srcPort, ///< Mask for source port.
                          uint16_t dstPort  ///< Mask for destination port.
    );
    /// Initialize the current set to empty with specific mask values.
    /// @return A pointer to the new set.
    MaskValueSetElt *initSet(uint32_t srcAddr, ///< Mask for source address.
                             uint32_t dstAddr, ///< Mask for destination address.
                             uint16_t srcPort, ///< Mask for source port.
                             uint16_t dstPort  ///< Mask for destination port.
    );
    MaskValueSetElt *m_set; ///< Current set.
    MaskAssignElt *m_elt;   ///< Parent element.
  };

  /// @name Accessors
  //@{
  /// Get the number of mask/value sets.
  uint32_t getCount() const;
  //@}

  appender init(uint32_t srcAddr, ///< Mask for source address.
                uint32_t dstAddr, ///< Mask for destination address.
                uint16_t srcPort, ///< Mask for source port.
                uint16_t dstPort  ///< Mask for destination port.
  );

  /// Get size in bytes of this structure.
  /// @note This is not constant time. The mask/value sets must be traversed
  /// to get the total size.
  size_t getSize() const;
  /// Get the size in bytes of the variable part of this structure.
  /// @note This is not constant time. The mask/value sets must be traversed
  /// to get the total size.
  size_t getVarSize() const;

protected:
  uint32_t m_count = 0; ///< # of sets (network order).

  friend struct appender;
};

class CacheIdBox;

/** Sect 5.7.2: Web-Cache Identity Element
    According to the specification, this is a fixed structure with
    hash data. However, in practice there is an undocumented variant for
    mask assignment where it contains mask data instead of hash data.

    This class provides basic control. Two subclasses specialize for the
    two variants. Use @c isMask to detect which variant is present.

    @note Do not add virtual methods, as reasonable as that seems because
    this is a serialized object and the memory layout correspond to the
    protocol definition.

    @see CacheHashIdElt
    @see CacheMaskIdElt
*/
class CacheIdElt
{
  friend class CacheIdBox;

public:
  typedef CacheIdElt self; ///< Self reference type.

  /// Hash revision (protocol required).
  static uint16_t const HASH_REVISION = 0;

  /// @name Accessors
  //@{
  uint32_t getAddr() const;        ///< Get address field.
  self &setAddr(uint32_t addr);    ///< Set address field to @a addr.
  uint16_t getHashRev() const;     ///< Get hash revision field.
  self &setHashRev(uint16_t rev);  ///< Set hash revision field to @a rev.
  self &initHashRev();             ///< Set hash revision to default value.
  bool getUnassigned() const;      ///< Get unassigned field.
  self &setUnassigned(bool state); ///< Set unassigned field to @a state.
  bool isMask() const;             ///< @return @c true if this is a mask assignment.
                                   /** Set the maskiness of this structure.
                                       Be very careful with this, as different values change the
                                       memory layout of the object.
                                   */
  self &setMask(bool state         ///< @c true to be mask, @c false to be hash.
  );

  self &clearReserved(); ///< Set reserved bits to zero.
  //@}

protected:
  uint32_t m_addr;               ///< Identifying cache IP address.
  uint16_t m_hash_rev;           ///< Hash revision.
  unsigned int m_reserved_0 : 7; ///< Reserved bits.
                                 /** Cache not assigned.
                                     If set the cache does not have an assignment in the redirection
                                     hash table and the data in @a m_buckets is historical. This allows
                                     a cache that was removed to be added back in the same buckets.
                                 */
  unsigned int m_unassigned : 1;
  unsigned int m_reserved_1 : 1; ///< Reserved (unused).
  unsigned int m_is_mask : 1;    ///< Set -> mask, Clear -> hash.
  unsigned int m_reserved_2 : 6; ///< Reserved (unused).
                                 /** Trailing elements common to all cache ID variants.
                                     Unfortunately, although @c weight and @c status are common, they are
                                     after the variable data and so can't be put in the base class.
                                     Best we can do is declare a struct for them for later convenience.
                                 */
  struct Tail {
    uint16_t m_weight; ///< Weight of assignment.
    uint16_t m_status; ///< Cache status.
  };
};

/** Cache ID for Hash assignment.
 */
class CacheHashIdElt : public CacheIdElt
{
  friend class CacheIdBox;

public:
  typedef CacheHashIdElt self; ///< Self reference type.
  typedef CacheIdElt super;    ///< Parent type.
  /// Container for hash assignment.
  typedef uint8_t HashBuckets[N_BUCKETS >> 3];
  /// @name Accessors
  //@{
  bool getBucket(int idx) const; ///< Get bucket state at index @a idx.
  /// Set bucket at index @a idx to @a state.
  self &setBucket(int idx, bool state);
  self &setBuckets(bool state); ///< Set all buckets to @a state.
  uint16_t getWeight() const;   ///< Get weight field.
  self &setWeight(uint16_t w);  ///< Set weight field to @a w.
  uint16_t getStatus() const;   ///< Get status field.
  self &setStatus(uint16_t s);  ///< Set status field to @a s.
  //@}
  /// Get object size in bytes.
  size_t getSize() const;

protected:
  /// Bit vector of buckets assigned to this cache.
  HashBuckets m_buckets;
  Tail m_tail; /// Trailing values in element.

  /// Get the address of the tail elements.
  Tail *getTailPtr();
};

/** Cache ID for Mask assignment.
    Be a little careful with this object. Because it's an element and
    must copy the serialized data layout, almost all of the methods are
    not constant time but require walking internal data structures.

    @internal Experimentally,
    - A mask assign element count of zero does not work. It fails
    with the wrongly descriptive message "incompatible assignment data".
    - A single mask assign element with a mask set with one value seems to
    work.
 */
class CacheMaskIdElt : public CacheIdElt
{
  friend class CacheIdBox;

public:
  typedef CacheMaskIdElt self; ///< Self reference type.
  typedef CacheIdElt super;    ///< Parent type.
  /// @name Accessors
  //@{
  uint16_t getWeight() const;  ///< Get weight field.
  self &setWeight(uint16_t w); ///< Set weight field to @a w.
  uint16_t getStatus() const;  ///< Get status field.
  self &setStatus(uint16_t s); ///< Set status field to @a s.
  /// Get the number of mask/value sets.
  uint32_t getCount() const;
  //@}
  /// Get object size in bytes.
  size_t getSize() const;

protected:
  /// Mask assignment data.
  MaskAssignElt m_assign;
  /// Get a pointer to where the tail data is.
  /// Presumes the assignment is filled out.
  Tail *getTailPtr();
};

/** Holder for a @c CacheIdElt.
    This class is needed because of the restrictions on element classes and
    because a @c CacheIdElt is a variable sized element yet we need to store
    instances of it in other classes. This box both holds an instance and
    handles some of the memory allocation issues involved.
 */
class CacheIdBox
{
public:
  typedef CacheIdBox self; ///< Self reference type.

  /// Default constructor.
  CacheIdBox() = default;

  /// @name Accessors
  //@{
  /// Get the identifying cache address.
  uint32_t getAddr() const;
  /// Set the identifying cache address.
  self &setAddr(uint32_t ///< Identifying IP address.
  );
  uint16_t getHashRev() const;     ///< Get hash revision field.
  self &setHashRev(uint16_t rev);  ///< Set hash revision field to @a rev.
  self &initHashRev();             ///< Set hash revision to default value.
  bool getUnassigned() const;      ///< Get unassigned field.
  self &setUnassigned(bool state); ///< Set unassigned field to @a state.
  bool isMask() const;             ///< @return @c true if this is a mask assignment.
                                   /** Set the maskiness of this structure.
                                       Be very careful with this, as different values change the
                                       memory layout of the object.
                                   */
  self &setMask(bool state         ///< @c true to be mask, @c false to be hash.
  );

  self &clearReserved(); ///< Set reserved bits to zero.
  //@}
  /// Initialize to unassigned hash.
  /// The cache address is set to @a addr.
  self &initDefaultHash(uint32_t addr ///< Identifying cache address.
  );
  /// Initialize to unassigned mask
  /// The cache address is set to @a addr.
  self &initDefaultMask(uint32_t addr ///< Identifying cache address.
  );
  /** Fill in element from source copy.
      Internal memory is allocated and the @a src copied.
   */
  self &fill(self const &src ///< Original source element
  );
  /** Fill in element from source copy.
      This is used to write the element to memory that is allocated
      independently of the box.
      @note Caller is expected to have verified sufficient buffer space.
   */
  self &fill(void *base,     ///< Target buffer.
             self const &src ///< Original source element
  );
  /// Initialize box from an existing element in memory.
  int parse(MsgBuffer base ///< Source memory.
  );

  /// Get the size in bytes of the contained element.
  size_t getSize() const;

protected:
  /// Force buffer to be at least @a n bytes
  self &require(size_t n ///< Minimum buffer size required.
  );

  CacheIdElt *m_base       = nullptr; ///< Base address of memory for element.
  CacheIdElt::Tail *m_tail = nullptr; ///< Base address of trailing data elements.
  size_t m_size            = 0;       ///< Size of element (valid data in buffer);
  size_t m_cap             = 0;       ///< Size of allocated memory. Zero if external memory.
};

/** Base class for all components.

    Each component is a fixed sized object that represents a
    component in the WCCP message. The component instance points at
    its corresponding data in the message. Values in the message are
    accessed through accessor methods which have the form @c
    getNAME and @c setNAME for "get field" and "set field". The
    @c set methods return a reference to the component so that
    they can be chained.

    Most components will have an internal typedef @c raw_t which is a
    structure with the exact memory layout of the
    component. Components without this typedef cannot be directed
    represented by a C++ structure (which is why we need this
    indirection in the first place).
*/
class ComponentBase
{
public:
  typedef ComponentBase self; ///< Self reference type.
  /// Default constructor.
  ComponentBase() = default;
  /// Check for not present.
  bool isEmpty() const;

protected:
  /// Base of component in message data.
  /// If this is @c NULL then the component is not in the message.
  char *m_base = nullptr;
};

/// Synthetic component to represent the overall message header.
class MsgHeaderComp : public ComponentBase
{
public:
  typedef MsgHeaderComp self;  ///< Self reference type.
  typedef ComponentBase super; ///< Parent type.

  /// Sect 5.5:  Message Header
  /// Serialized layout of message header.
  struct raw_t {
    uint32_t m_type;    ///< @c message_type_t
    uint16_t m_version; ///< Implementation version of sender
    uint16_t m_length;  ///< Message body length (excluding header)
  };

  /// Default constructor.
  MsgHeaderComp() {}
  /// @name Accessors
  //@{
  message_type_t getType();           ///< Get message type field.
  uint16_t getVersion();              ///< Get message version field.
  uint16_t getLength();               ///< Get message length field.
  self &setType(message_type_t type); ///< Set message type field to @a type.
  self &setVersion(uint16_t version); ///< Set version field to @a version.
  self &setLength(uint16_t length);   ///< Set length field to @a length.
  //@}

  /// Write initial values to message data.
  /// @a base is updated to account for this component.
  self &fill(MsgBuffer &base, ///< [in,out] Buffer for component storage.
             message_type_t t ///< Message type.
  );

  /// Validate component for existing data.
  /// @a base is updated to account for this component.
  int parse(MsgBuffer &base ///< [in,out] Base address for component data.
  );

  /// Compute size of a component of this type.
  static size_t calcSize();

  /// Convert to a top level message type.
  /// @return The converted type if valid, @c INVALID_MSG_TYPE if not.
  static message_type_t toMsgType(int t);
};

/** Intermediate base class for components with the standard component header.

    @note That's all of them except the message header itself.

    @internal This saves some work getting around C++ co-variance issues
    with return values.
*/
template <typename T ///< Child class (CRT pattern)
          >
struct CompWithHeader : public ComponentBase {
  /** Serialized layout of per component header.
      All components except the message header start with this structure.
      Provided for ease of use by child classes, which should
      subclass this for their own @c raw_t.
  */
  struct raw_t {
    uint16_t m_type;   ///< Serialized @ref CompType.
    uint16_t m_length; ///< length of rest of component (not including header).
  };
  /** Size of header.
      This is needed by all subclasses because the value in the length field
      excludes this structure.
  */
  static size_t const HEADER_SIZE = sizeof(raw_t);

  /// @name Accessors
  //@{
  CompType getType() const;      ///< Get component type field.
  uint16_t getLength() const;    ///< Get component length field.
  T &setType(CompType type);     ///< Set component type field to @a type.
  T &setLength(uint16_t length); ///< Set length field to @a length.
                                 //@}

  /** Check the component header for type and length sanity.
      This requires the (subclass) client to
      - Do a size check to verify enough space for the component header.
      - Set @a m_base

      This method
      - Checks the component type against the expected type (@a ect)
      - Checks stored component length against the buffer size.

      @return A parse result.
  */
  int checkHeader(MsgBuffer const &buffer, ///< Message buffer.
                  CompType t               ///< Expected component type.
  );
};

/** Sect 5.6.1: Security Info Component
    This is used for both security options. Clients should check
    the @c m_option to see if the @a m_impl member is valid.
*/
class SecurityComp : public CompWithHeader<SecurityComp>
{
public:
  typedef SecurityComp self;          ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.
  /// Specify the type for this component.
  static CompType const COMP_TYPE = SECURITY_INFO;

  /// Import security option type.
  typedef SecurityOption Option;

  static size_t const KEY_SIZE = 8;
  typedef char Key[KEY_SIZE];

  /// Raw memory layout, no security.
  struct RawNone : public super::raw_t {
    uint32_t m_option; ///< @c Option
  };

  /// Raw memory layout, with MD5.
  struct RawMD5 : public RawNone {
    /// Size of MD5 hash (in bytes).
    static size_t const HASH_SIZE = 16;
    /// Storage for MD5 hash.
    typedef uint8_t HashData[HASH_SIZE];
    /// MD5 hash value.
    HashData m_data;
  };

  /// Default constructor.
  SecurityComp();

  /// @name Accessors
  //@{
  Option getOption() const;    ///< Get security option field.
  self &setOption(Option opt); ///< Set security option field to @a opt.
  //@}

  /// Write default values to the serialization buffer.
  self &fill(MsgBuffer &buffer, Option opt = m_default_opt);
  /// Validate an existing structure.
  int parse(MsgBuffer &buffer);

  /// Compute the memory size of the component.
  static size_t calcSize(Option opt);

  /// Set the global / default security key.
  /// This is used for the security hash unless the local key is set.
  /// @a key is copied to a global buffer and clipped to @c KEY_SIZE bytes.
  static void setDefaultKey(const char *key ///< Shared key.
  );
  static void setDefaultOption(Option opt ///< Type of security.
  );

  /// Set message local security key.
  self &setKey(const char *key ///< Shared key.
  );

  /// Compute and set the security data.
  /// @a msg must be a buffer that covers exactly the entire message.
  self &secure(MsgBuffer const &msg ///< Message data.
  );

  bool validate(MsgBuffer const &msg ///< Message data.
                ) const;

protected:
  /// Local to this message shared key / password.
  Key m_key;
  /// Use local key.
  bool m_local_key;
  /// Global (static) shared key / password.
  static Key m_default_key;
  /// Default security option.
  static Option m_default_opt;
};

/// Sect 5.6.2: Service Info Component
class ServiceComp : public CompWithHeader<ServiceComp>
{
public:
  typedef ServiceComp self;           ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Specify the type for this component.
  static CompType const COMP_TYPE = SERVICE_INFO;

  /// Serialized format for component.
  struct raw_t : public super::raw_t, public ServiceGroup {
  };

  ServiceComp(); ///< Default constructor, no member initialization.

  /// @name Accessors
  //@{
  ServiceGroup::Type getSvcType() const; ///< Get service type field.
                                         /** Set the service type.
                                             If @a svc is @c SERVICE_STANDARD then all fields except the
                                             component header and service id are set to zero as required
                                             by the protocol.
                                         */
  self &setSvcType(ServiceGroup::Type svc);

  uint8_t getSvcId() const;   ///< Get service ID field.
  self &setSvcId(uint8_t id); ///< Set service ID field to @a id.

  uint8_t getPriority() const;    ///< Get priority field.
  self &setPriority(uint8_t pri); ///< Set priority field to @a p.

  uint8_t getProtocol() const;  ///< Get protocol field.
  self &setProtocol(uint8_t p); ///< Set protocol field to @a p.

  uint32_t getFlags() const;  ///< Get flags field.
  self &setFlags(uint32_t f); ///< Set the flags flags in field to @a f.
  /// Set the flags in the flag field that are set in @a f.
  /// Other flags are unchanged.
  self &enableFlags(uint32_t f);
  /// Clear the flags in the flag field that are set in @a f.
  /// Other flags are unchanged.
  self &disableFlags(uint32_t f);

  /// Get a port value.
  uint16_t getPort(int idx ///< Index of target port.
                   ) const;
  /// Set a port value.
  self &setPort(int idx,      ///< Index of port.
                uint16_t port ///< Value for port.
  );
  /// Zero (clear) all ports.
  self &clearPorts();
  /** Add a port to the service.
      The first port which has not been set is set to @a port. It is an error
      to add more than @c N_PORTS ports.
   */
  self &addPort(uint16_t port ///< Port value.
  );
  //@}

  /// Raw access to ServiceGroup.
  operator ServiceGroup const &() const;

  /** Fill from a service group definition.
   */
  self &fill(MsgBuffer &base,        ///< Target storage.
             ServiceGroup const &svc ///< Service group definition.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the memory size of the component.
  static size_t calcSize();

protected:
  int m_port_count; ///< Number of ports in use.

  /// Cast raw internal pointer to data type.
  raw_t *access();
  /// Cast raw internal pointer to data type.
  raw_t const *access() const;
};

/// Sect 5.6.3: RouterIdentity Info Component
/// @note An instance of this struct is followed by @a m_count
/// IP addresses.
class RouterIdComp : public CompWithHeader<RouterIdComp>
{
public:
  typedef RouterIdComp self;          ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Specify the type for this component.
  static CompType const COMP_TYPE = ROUTER_ID_INFO;

  /// Stub of serialized layout.
  struct raw_t : public super::raw_t {
    RouterIdElt m_id; ///< Router ID element.
    /// Source address.
    /// For response messages, this is the address to which the
    /// original message was sent.
    uint32_t m_to_addr;
    /// # of target cache addresses.
    uint32_t m_from_count;
    // Addresses follow here.
  };

  /// @name Accessors
  //@{
  /// Directly access router ID element.
  RouterIdElt &idElt();
  /// Directly access router ID element.
  RouterIdElt const &idElt() const;
  /// Set the fields in the router ID element.
  self &setIdElt(uint32_t addr,   ///< Identifying IP address for router.
                 uint32_t recv_id ///< Receive count for router to target cache.
  );
  uint32_t getAddr() const;     ///< Get the address field in the ID element.
  self &setAddr(uint32_t addr); ///< Set the address field in the ID element.
  uint32_t getRecvId() const;   ///< Get the receive ID field in the ID element.
  self &setRecvId(uint32_t id); ///< Set the receive ID field in the ID element.

  /// Get the sent to address.
  uint32_t getToAddr() const;
  /// Set the sent to address.
  self &setToAddr(uint32_t addr ///< Address value.
  );
  /// Get router count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getFromCount() const;
  /// Get received from address.
  uint32_t getFromAddr(int idx ///< Index of address.
                       ) const;
  /// Set received from address.
  self &setFromAddr(int idx,      ///< Index of address.
                    uint32_t addr ///< Address value.
  );
  //@}
  /// Find an address in the from list.
  /// @return The index of the address, or -1 if not found.
  int findFromAddr(uint32_t addr ///< Search value.
  );

  /** Write serialization data for single cache target.
      This completely fills the component.
  */
  self &fillSingleton(MsgBuffer &base,     ///< Target storage.
                      uint32_t addr,       ///< Identifying IP address.
                      uint32_t recv_count, ///< Receive count for target cache.
                      uint32_t to_addr,    ///< Destination address in initial packet.
                      uint32_t from_addr   ///< Identifying IP address of target cache.
  );

  /** Write basic message structure.
      The router and cache data must be filled in separately.
  */
  self &fill(MsgBuffer &base, ///< Target storage.
             size_t n_caches  ///< Number of caches (fromAddr).
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the memory size of the component.
  static size_t calcSize(int n ///< Receive address count
  );
};

/** Sect 5.6.4: Web-Cache Identity Info Component
 */
class CacheIdComp : public CompWithHeader<CacheIdComp>
{
public:
  typedef CacheIdComp self;           ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = CACHE_ID_INFO;

  /// Serialized format.
  struct raw_t : public super::raw_t {
    CacheIdElt m_id; ///< Identity element stub.
  };

  /// @name Accessors
  //@{
  /// Direct access to the cache ID element.
  CacheIdBox &cacheId();
  CacheIdBox const &cacheId() const;

  // Only forward the common ones.
  uint32_t getAddr() const;        ///< Get address field.
  self &setAddr(uint32_t addr);    ///< Set address field to @a addr.
  uint16_t getHashRev() const;     ///< Get hash revision field.
  self &setHashRev(uint16_t rev);  ///< Set hash revision field to @a rev.
  bool getUnassigned() const;      ///< Get unassigned field.
  self &setUnassigned(bool state); ///< Set unassigned field to @a state.
  uint16_t getWeight() const;      ///< Get weight field.
  self &setWeight(uint16_t w);     ///< Set weight field to @a w.
  uint16_t getStatus() const;      ///< Get status field.
  self &setStatus(uint16_t s);     ///< Set status field to @a s.
                                   //@}

  /** Write serialization data.
      - Sets required header fields for the component.
      - Copies the data from @a src.
  */
  self &fill(MsgBuffer &base,      ///< Target storage.
             CacheIdBox const &src ///< Cache descriptor
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the memory size of the component.
  /// Cannot be reliably computed statically.
  size_t getSize();

protected:
  CacheIdBox m_box; ///< Wrapper for cache id element.
};

/** Sect 5.6.5: Router View Info Component
 */
class RouterViewComp : public CompWithHeader<RouterViewComp>
{
public:
  typedef RouterViewComp self;        ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = RTR_VIEW_INFO;

  /// Stub of the serialized data.
  /// There is more variable sized data that must be handled specially.
  struct raw_t : public super::raw_t {
    uint32_t m_change_number; ///< Sequence number.
    AssignmentKeyElt m_key;   ///< Assignment data.
    uint32_t m_router_count;  ///< # of router elements.
  };

  RouterViewComp();

  /// @name Accessors
  //@{
  /// Directly access assignment key.
  AssignmentKeyElt &keyElt();
  /// Directly access assignment key.
  AssignmentKeyElt const &keyElt() const;
  /// Get address in assignment key.
  uint32_t getKeyAddr() const;
  /// Set address in assignment key.
  self &setKeyAddr(uint32_t addr);
  /// Get change number in assignment key.
  uint32_t getKeyChangeNumber() const;
  /// Set change number in assignment key.
  self &setKeyChangeNumber(uint32_t n);

  uint32_t getChangeNumber() const;  ///< Get change number field.
  self &setChangeNumber(uint32_t n); ///< Set change number field to @a n

  /// Get cache count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getCacheCount() const;
  /// Access cache element.
  CacheIdBox &cacheId(int idx ///< Index of target element.
  );
  /// Access cache element.
  CacheIdBox const &cacheId(int idx ///< Index of target element.
                            ) const;
  /// Get router count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getRouterCount() const;
  /// Get router address.
  uint32_t getRouterAddr(int idx ///< Index of router.
                         ) const;
  /// Set router address.
  self &setRouterAddr(int idx,      ///< Index of router.
                      uint32_t addr ///< Address value.
  );
  //@}

  /** Write serialization data.

      A client @b must call this method before writing any fields directly.
      After invocation the client must fill in the router and cache elements.
  */
  self &fill(MsgBuffer &base, ///< Target storage.
             int n_routers,   ///< Number of routers in view.
             int n_caches     ///< Number of caches in view.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

protected:
  /// Serialized count of cache addresses.
  /// The actual addresses start immediate after this.
  uint32_t *m_cache_count;
  /// Wrappers for cache identity elements.
  /// These are variably sized in the general case.
  CacheIdBox m_cache_ids[MAX_CACHES];

  /// Compute the address of the cache count field.
  /// Assumes the router count field is set.
  uint32_t *calc_cache_count_ptr();
};

/** Sect 5.6.6: Web-Cache View Info Component
 */
class CacheViewComp : public CompWithHeader<CacheViewComp>
{
public:
  typedef CacheViewComp self;         ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = CACHE_VIEW_INFO;

  /// Stub of the serialized data.
  /// There is more variable sized data that must be handled specially.
  struct raw_t : public super::raw_t {
    uint32_t m_change_number; ///< Sequence number.
    uint32_t m_router_count;  ///< # of router ID elements.
  };

  /// @name Accessors
  //@{
  uint32_t getChangeNumber() const;  ///< Get change number field.
  self &setChangeNumber(uint32_t n); ///< Set change number field to @a n

  /// Get router count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getRouterCount() const;
  /// Access a router ID element.
  RouterIdElt &routerElt(int idx ///< Index of target element.
  );
  /** Find a router element by router IP address.
      @return A pointer to the router element or @c NULL
      if no router is identified by @a addr.
  */
  RouterIdElt *findf_router_elt(uint32_t addr ///< Router IP address.
  );
  /// Get cache count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getCacheCount() const;
  /// Get a cache address.
  uint32_t getCacheAddr(int idx ///< Index of target address.
                        ) const;
  /// Set a cache address.
  self &setCacheAddr(int idx,      ///< Index of target address.
                     uint32_t addr ///< Address value to set.
  );
  //@}

  /** Write serialization data.

      A client @b must call this method before writing any fields directly.
      After invocation the client must fill in the router and cache elements.
  */
  self &fill(MsgBuffer &buffer,                    ///< Target storage.
             detail::cache::GroupData const &group ///< Service group information.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize(int n_routers, ///< Number of routers in view.
                         int n_caches   ///< Number of caches in view.
  );

protected:
  /// Get router element array.
  /// @return A pointer to the first router element.
  RouterIdElt *atf_router_array();
  /// Serialized count of cache addresses.
  /// The actual addresses start immediate after this.
  uint32_t *m_cache_count;
};

/** Sect 5.6.7: Assignment Info Component
 */
class AssignInfoComp : public CompWithHeader<AssignInfoComp>
{
public:
  typedef AssignInfoComp self;        ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = REDIRECT_ASSIGNMENT;

  /// Stub of the serialized data.
  /// There is more variable sized data that must be handled specially.
  struct raw_t : public super::raw_t {
    AssignmentKeyElt m_key;        ///< Assignment key data.
    RouterAssignListElt m_routers; ///< Routers.
  };
  typedef HashAssignElt::Bucket Bucket; ///< Import type.

  /// @name Accessors
  //@{
  /// Directly access assignment key.
  AssignmentKeyElt &keyElt();
  /// Directly access assignment key.
  AssignmentKeyElt const &keyElt() const;
  /// Get address in assignment key.
  uint32_t getKeyAddr() const;
  /// Set address in assignment key.
  self &setKeyAddr(uint32_t addr);
  /// Get change number in assignment key.
  uint32_t getKeyChangeNumber() const;
  /// Set change number in assignment key.
  self &setKeyChangeNumber(uint32_t n);

  /// Get router count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getRouterCount() const;
  /// Access a router assignment element.
  RouterAssignElt &routerElt(int idx ///< Index of target element.
  );
  /// Get cache count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getCacheCount() const;
  /// Get a cache address.
  uint32_t getCacheAddr(int idx ///< Index of target address.
                        ) const;
  /// Set a cache address.
  self &setCacheAddr(int idx,      ///< Index of target address.
                     uint32_t addr ///< Address value to set.
  );
  /// Access a bucket.
  Bucket &bucket(int idx ///< Index of target bucket.
  );
  /// Access a bucket.
  Bucket const &bucket(int idx ///< Index of target bucket.
                       ) const;
  //@}

  /// Fill out the component from an @c Assignment.
  self &fill(MsgBuffer &buffer,               ///< Target storage.
             detail::Assignment const &assign ///< Assignment data.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize(int n_routers, ///< Number of routers in view.
                         int n_caches   ///< Number of caches in view.
  );

protected:
  /// Serialized count of cache addresses.
  /// The actual addresses start immediate after this.
  uint32_t *m_cache_count;
  /// Serialized bucket data.
  Bucket *m_buckets;
  /// Calculate the address of the cache count.
  uint32_t *calcCacheCountPtr();
  /// Calculate the address of the bucket array.
  Bucket *calcBucketPtr();
};

/** Sect 5.6.9: Capabilities Info Component
 */
class CapComp : public CompWithHeader<CapComp>
{
public:
  typedef CapComp self;               ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = CAPABILITY_INFO;

  // Not even a stub for this component, just an array of elements.

  /// Default constructor.
  CapComp();

  /// @name Accessors.
  //@{
  /// Directly access mask value element.
  CapabilityElt &elt(int idx ///< Index of target element.
  );
  CapabilityElt const &elt(int idx ///< Index of target element.
                           ) const;
  /// Get the element count.
  /// @note No corresponding @c setf_ because that cannot be changed once set.
  /// @see fill
  uint32_t getEltCount() const;
  //@}

  /** Write serialization data.

      The capability elements must be filled @b after invoking this method.
      And, of course, do not fill more than @a n of them.
  */
  self &fill(MsgBuffer &buffer, ///< Target storage.
             int n              ///< Number of capabilities.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize(int n ///< Number of capabilities.
  );

  /// Find value for Cache Assignment.
  ServiceGroup::CacheAssignmentStyle getCacheAssignmentStyle() const;
  /// Find value for packet forwarding.
  ServiceGroup::PacketStyle getPacketForwardStyle() const;
  /// Find value for packet return.
  ServiceGroup::PacketStyle getPacketReturnStyle() const;
  /// Invalidate cached values.
  /// Needed after modifying elements via the @c elt method.
  self &invalidate();

protected:
  /// Fill the cached values.
  void cache() const;

  int m_count = 0; ///< # of elements.
                   /** Whether the style values are valid.
                       We load all the values on the first request because we have to walk
                       all the capabilities anyway, and cache them.
                   */
  mutable bool m_cached = false;
  /// Style used to forward packets to cache.
  mutable ServiceGroup::PacketStyle m_packet_forward = ServiceGroup::PacketStyle::NO_PACKET_STYLE;
  /// Style used to return packets to the router.
  mutable ServiceGroup::PacketStyle m_packet_return = ServiceGroup::PacketStyle::NO_PACKET_STYLE;
  /// Style used to make cache assignments.
  mutable ServiceGroup::CacheAssignmentStyle m_cache_assign = ServiceGroup::CacheAssignmentStyle::NO_CACHE_ASSIGN_STYLE;
};

/** Sect 5.6.10: Alternate Assignment Component
    This is an abstract base class. It is specialized for each alternate.
 */
class AltAssignComp : public CompWithHeader<AltAssignComp>
{
public:
  typedef AltAssignComp self;         ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = ALT_ASSIGNMENT;
  /// Alternate is hash.
  static uint16_t const ALT_HASH_ASSIGNMENT = 0;
  /// Alternate is mask.
  static uint16_t const ALT_MASK_ASSIGNMENT = 1;

  /// Component secondary header.
  /// @internal Split out because we need to compute its size.
  struct local_header_t {
    uint16_t m_assign_type;   ///< Assignment body type.
    uint16_t m_assign_length; ///< Assignment body length.
  };
  /// Stub of the serialized data.
  /// There is more variable sized data that must be handled specially.
  struct raw_t : public super::raw_t, public local_header_t {
    // These are the same in all current subclasses.
    AssignmentKeyElt m_key;        ///< Assignment key data.
    RouterAssignListElt m_routers; ///< Routers.
  };

  /// Force virtual destructor.
  virtual ~AltAssignComp() {}
  /// @name Accessors
  //@{
  /// Get the assignment type.
  uint16_t getAssignType() const;
  /// Set the assignment type.
  self &setAssignType(uint16_t t ///< Assignment type.
  );
  /// Get the assignment length.
  uint16_t getAssignLength() const;
  /// Set the assignment length.
  self &setAssignLength(uint16_t length ///< Length in bytes.
  );
  /// Get router count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getRouterCount() const;
  /// Directly access assignment key.
  AssignmentKeyElt &keyElt();
  /// Directly access assignment key.
  AssignmentKeyElt const &keyElt() const;
  //@}

  /// Fill out the component from an @c Assignment.
  virtual self &fill(MsgBuffer &buffer,               ///< Target storage.
                     detail::Assignment const &assign ///< Assignment data.
                     ) = 0;

  /// Validate an existing structure.
  /// @return Parse result.
  virtual int parse(MsgBuffer &buffer) = 0;

protected:
  /// Calculate the first byte past the end of the variable data.
  void *calcVarPtr();
};

/** Sect 5.6.10: Alternate Assignment Component
    This is the hash based version.
 */
class AltHashAssignComp : public AltAssignComp
{
public:
  typedef AltHashAssignComp self; ///< Self reference type.
  typedef AltAssignComp super;    ///< Parent type.

  /// @name Accessors
  //@{
  /// Get cache count field.
  /// @note No @c setf method because this cannot be changed independently.
  /// @see fill
  uint32_t getCacheCount() const;
  //@}

  /// Force virtual destructor.
  virtual ~AltHashAssignComp() {}
  /// Fill out the component from an @c Assignment.
  virtual self &fill(MsgBuffer &buffer,               ///< Target storage.
                     detail::Assignment const &assign ///< Assignment data.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  virtual int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize(int n_routers, ///< Number of routers in view.
                         int n_caches   ///< Number of caches in view.
  );

protected:
  /// Serialized count of cache addresses.
  /// The actual addresses start immediate after this.
  uint32_t *m_cache_count;
  /// Calculate the address of the cache count.
  uint32_t *calcCacheCountPtr();
};

/** Sect 5.6.10: Alternate Assignment Component
    This is the mask based version.
 */
class AltMaskAssignComp : public AltAssignComp
{
public:
  typedef AltMaskAssignComp self; ///< Self reference type.
  typedef AltAssignComp super;    ///< Parent type.

  /// Force virtual destructor.
  virtual ~AltMaskAssignComp() {}
  /// Fill out the component from an @c Assignment.
  virtual self &fill(MsgBuffer &buffer,               ///< Target storage.
                     detail::Assignment const &assign ///< Assignment data.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  virtual int parse(MsgBuffer &buffer);

protected:
  MaskAssignElt *m_mask_elt; ///< Address of the mask assign element.
};

/** Sect 5.6.12: Command Info Component
 */
class CmdComp : public CompWithHeader<CmdComp>
{
public:
  typedef CmdComp self;               ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = COMMAND_EXTENSION;

  /// Command types.
  enum cmd_t {
    SHUTDOWN          = 1, ///< Cache is shutting down.
    SHUTDOWN_RESPONSE = 2  ///< SHUTDOWN ack.
  };

  /// Serialized data layout.
  /// @internal Technically the command data is variable, but all currently
  /// defined commands have the same 32 bit data element.
  struct raw_t : public super::raw_t {
    uint16_t m_cmd;        ///< Command type / code.
    uint16_t m_cmd_length; ///< Length of command data.
    uint32_t m_cmd_data;   ///< Command data.
  };

  /// @name Accessors.
  //@{
  /// Directly access mask value element.
  cmd_t getCmd() const;            ///< Get command type.
  self &setCmd(cmd_t cmd);         ///< Set command type.
  uint32_t getCmdData() const;     ///< Get command data.
  self &setCmdData(uint32_t data); ///< Set command @a data.
  //@}

  /// Write basic serialization data.
  /// Elements must be filled in separately and after invoking this method.
  self &fill(MsgBuffer &buffer, ///< Component storage.
             cmd_t cmd,         ///< Command type.
             uint32_t data      ///< Command data.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize();
};

/// Sect 5.6.11: Assignment Map Component
class AssignMapComp : public CompWithHeader<AssignMapComp>
{
public:
  typedef AssignMapComp self;         ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = ASSIGN_MAP;

  /// Serialized layout structure.
  /// Not complete, only a stub.
  struct raw_t : public super::raw_t {
    MaskAssignElt m_assign;
  };

  /// Default constructor.
  AssignMapComp();

  /// @name Accessors.
  //@{
  /// Get the element count.
  uint32_t getCount() const;
  //@}

  /// Fill from assignment data.
  self &fill(MsgBuffer &buffer,               ///< Component storage.
             detail::Assignment const &assign ///< Assignment data.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);
};

/// Sect 5.6.8: Router Query Info Component.
class QueryComp : public CompWithHeader<QueryComp>
{
public:
  typedef QueryComp self;             ///< Self reference type.
  typedef CompWithHeader<self> super; ///< Parent type.

  /// Component type ID for this component.
  static CompType const COMP_TYPE = QUERY_INFO;

  /// Internal layout.
  struct raw_t : public super::raw_t {
    uint32_t m_router_addr; ///< Identifying router address.
    uint32_t m_recv_id;     ///< Receive ID router expects in reply.
    uint32_t m_to_addr;     ///< Destination address of query.
    uint32_t m_cache_addr;  ///< Identifying address of cache.
  };

  /// @name Accessors.
  //@{
  /// Directly access mask value element.
  uint32_t getRouterAddr() const;     ///< Get identifying router address.
  self &setRouterAddr(uint32_t addr); ///< Set identifying router address.
  uint32_t getToAddr() const;         ///< Get target address.
  self &setToAddr(uint32_t addr);     ///< Set target address.
  uint32_t getCacheAddr() const;      ///< Get identifying cache address.
  self &setCacheAddr(uint32_t addr);  ///< Set identifying cache address.
  uint32_t getRecvId() const;         ///< Get receive ID.
  self &setRecvId(uint32_t data);     ///< Set receive ID.
  //@}

  /// Write serialization data.
  /// This fills in all fields.
  self &fill(MsgBuffer &buffer,   ///< Component storage.
             uint32_t routerAddr, ///< Router identifying address.
             uint32_t toAddr,     ///< Destination address.
             uint32_t cacheAddr,  ///< Cache identifying address.
             uint32_t recvId      ///< Receive ID.
  );

  /// Validate an existing structure.
  /// @return Parse result.
  int parse(MsgBuffer &buffer);

  /// Compute the total size of the component.
  static size_t calcSize();
};

/// Cache assignment hash function.
inline uint8_t
assignment_hash(uint32_t key ///< Key to hash.
)
{
  key ^= key >> 16;
  key ^= key >> 8;
  return key & 0xFF;
}

/// IP header information for a receive message.
struct IpHeader {
  uint32_t m_src; ///< Source address.
  uint32_t m_dst; ///< Destination address.
};

// Various static values.
const char *const BUFFER_TOO_SMALL_FOR_COMP_TEXT = "Unable to write component -- buffer too small";

// ------------------------------------------------------
namespace detail
{
  /** Local storage for cache assignment data.
      The maintenance of this data is sufficiently complex that it is better
      to have a standard class to hold it, rather than updating a serialized
      form.
  */
  class Assignment
  {
  public:
    typedef Assignment self;      ///< Self reference type.
    typedef AssignmentKeyElt Key; ///< Import assignment key type.
    /// Import assignment bucket definition.
    /// @internal Just one byte, no serialization issues.
    typedef AssignInfoComp::Bucket Bucket;

    /// Default constructor. Initialization to empty state.
    Assignment();

    /** Check for active assignment.

        An assignment is active if it is is current. This means either it
        was successfully generated on the cache side, or a valid assignment
        was received on the router side and has not expired.

        @return The current active state.
    */
    bool isActive() const;
    /// Control active flag.
    self &setActive(bool state ///< New active state.
    );

    /** Fill the assignment from cache service group data.
        Caches that are obsolete are purged from the data.
        @return @c true if a valid assignment was generated,
        @c false otherwise.
    */
    bool fill(cache::GroupData &group, ///< Service group data.
              uint32_t addr            ///< Identifying IP address of designated cache.
    );
    /// Update the receive ID for a router.
    self &updateRouterId(uint32_t addr,  ///< Identifying IP address of router.
                         uint32_t rcvid, ///< New receive ID.
                         uint32_t cno    ///< New change number.
    );

    /// Get the assignment key.
    AssignmentKeyElt const &getKey() const;
    /// Get the router assignment list.
    RouterAssignListElt const &getRouterList() const;
    /// Get the hash assignment.
    HashAssignElt const &getHash() const;
    /// Get the mask assignment.
    MaskAssignElt const &getMask() const;

  protected:
    Key m_key;     ///< Assignment key.
    bool m_active; ///< Active state.

    // These store the serialized assignment chunks which are assembled
    // in to the components as needed. Each points in to the serialization
    // buffer, or is @c NULL if that assignment data isn't valid.
    /// Router assignments.
    RouterAssignListElt *m_router_list;
    /// Hash assignment.
    HashAssignElt *m_hash_assign;
    /// Mask assignment.
    MaskAssignElt *m_mask_assign;

    /// Buffer for serialization.
    MsgBuffer m_buffer;
  };

  namespace endpoint
  {
    /// Common service group data.
    struct GroupData {
      typedef GroupData self; ///< Self reference type.

      ServiceGroup m_svc;           ///< The service definition.
      uint32_t m_generation    = 0; ///< Generation value (change number).
      time_t m_generation_time = 0; ///< Time of last view change.

      bool m_use_security_opt             = false;         ///< Use group local security.
      SecurityComp::Option m_security_opt = SECURITY_NONE; ///< Type of security.
      bool m_use_security_key             = false;         ///< Use group local key.
      SecurityComp::Key m_security_key;                    ///< MD5 key.

      /** Group assignment data.
          This is used as a place to generate an assignment or
          store one received from an extern source.
      */
      detail::Assignment m_assign_info;

      /// Default constructor.
      GroupData() = default;
      /// Use @a key instead of global default.
      self &setKey(const char *key ///< Shared key.
      );
      /// Use security @a style instead of global default.
      self &setSecurity(SecurityOption style ///< Security style to use.
      );
    };
  } // namespace endpoint
} // namespace detail
// ------------------------------------------------------
/** Base class for all messages.
 */
class BaseMsg
{
public:
  /// Default constructor.
  BaseMsg();
  /// Destructor.
  virtual ~BaseMsg() {}
  /// Set the message @a buffer.
  void setBuffer(MsgBuffer const &buffer ///< Storage for message.
  );
  /// Get the current buffer.
  MsgBuffer const &buffer() const;
  /// Invoke once all components have been filled.
  /// Sets the length and updates security data if needed.
  virtual void finalize();
  /// Get available buffer space.
  size_t getSpace() const;
  /// Get the message size.
  size_t getCount() const;

  /// Validate security option.
  /// @note This presumes a subclass has already successfully parsed.
  bool validateSecurity() const;

  // Common starting components for all messages.
  MsgHeaderComp m_header;  ///< Message header.
  SecurityComp m_security; ///< Security component.
  ServiceComp m_service;   ///< Service provided.

protected:
  MsgBuffer m_buffer; ///< Raw storage for message data.
};

/// Sect 5.1: Layout and control for @c WCCP2_HERE_I_AM
class HereIAmMsg : public BaseMsg
{
public:
  typedef HereIAmMsg self; ///< Self reference type.

  /** Fill in the basic message structure.
      This expects @c setBuffer to have already been called
      with an appropriate buffer.
      The actual router and cache data must be filled in
      after this call, which will allocate the appropriate spaces
      in the message layou.
  */
  void fill(detail::cache::GroupData const &group, ///< Service group for message.
            CacheIdBox const &cache_id,            ///< ID to use for this cache.
            SecurityOption sec_opt                 ///< Security option to use.
  );
  /** Fill in optional capabilities.
      The capabilities component is added only if the @a router
      is set to send them.
  */
  void fill_caps(detail::cache::RouterData const &router ///< Target router.
  );
  /// Parse message data, presumed to be of this type.
  int parse(ts::Buffer const &buffer ///< Raw message data.
  );

  CacheIdComp m_cache_id;     ///< Web cache identity info.
  CacheViewComp m_cache_view; ///< Web cache view.
  CapComp m_capabilities;     ///< Capabilities data.
  CmdComp m_command;          ///< Command extension.
};

/// Sect 5.2: 'I See You' Message
class ISeeYouMsg : public BaseMsg
{
public:
  typedef ISeeYouMsg self; ///< Self reference type.

  /// Fill out message structure.
  /// Router ID and view data must be filled in separately.
  void fill(detail::router::GroupData const &group, ///< Service groupc context.
            SecurityOption sec_opt,                 ///< Security option.
            detail::Assignment &assign,             ///< Cache assignment data.
            size_t to_caches,                       ///< # of target caches for message.
            size_t n_routers,                       ///< Routers in view.
            size_t n_caches,                        ///< Caches in view.
            bool send_capabilities = false          ///< Send capabilities.
  );

  /// Parse message data, presumed to be of this type.
  int parse(ts::Buffer const &buffer ///< Raw message data.
  );

  RouterIdComp m_router_id;     ///< Router ID.
  RouterViewComp m_router_view; ///< Router view data.
  // The rest of these are optional. The spec says we should get
  // an assignment or map, but in practice that doesn't happen with
  // actual Cisco routers in the hash case. Perhaps it happens with
  // a map.
  AssignInfoComp m_assignment; ///< Assignment data.
  AssignMapComp m_map;         ///< Assignment map.
  CapComp m_capabilities;      ///< Capabilities data.
  CmdComp m_command;           ///< Command extension.
};

/// Sect 5.1: Layout and control for @c WCCP2_REDIRECT_ASSIGN
class RedirectAssignMsg : public BaseMsg
{
public:
  typedef RedirectAssignMsg self; ///< Self reference type.

  /** Fill in the basic message structure.
      This expects @c setBuffer to have already been called
      with an appropriate buffer.
      The actual router and cache data must be filled in
      after this call, which will allocate the appropriate spaces
      in the message layou.
  */
  void fill(detail::cache::GroupData const &group, ///< Service group for message.
            SecurityOption sec_opt                 ///< Security option to use.
  );

  /// Parse message data, presumed to be of this type.
  int parse(ts::Buffer const &buffer ///< Raw message data.
  );

  // Only one of these should be present in an instance.
  AssignInfoComp m_hash_assign;        ///< Primary (hash) assignment.
  AltHashAssignComp m_alt_hash_assign; ///< Alternate (hash) assignment.
  AltMaskAssignComp m_alt_mask_assign; ///< Alternate (mask) assignment.
};

/// Sect 5.4: @c WCCP_REMOVAL_QUERY
class RemovalQueryMsg : public BaseMsg
{
public:
  typedef RemovalQueryMsg self; ///< Self reference type.

  /** Fill in the basic message structure.
      This expects @c setBuffer to have already been called
      with an appropriate buffer.
      The actual router and cache data must be filled in
      after this call, which will allocate the appropriate spaces
      in the message layou.
  */
  void fill(detail::cache::GroupData const &group, ///< Service group for message.
            SecurityOption sec_opt,                ///< Security option to use.
            AssignmentKeyElt const &key,           ///< Assignment key.
            int n_routers,                         ///< Number of routers expected.
            int n_caches                           ///< Number of caches expected.
  );

  /// Parse message data, presumed to be of this type.
  int parse(ts::Buffer const &buffer ///< Raw message data.
  );

  QueryComp m_query; ///< Router Removal Query component.
};

// ------------------------------------------------------
/// Last packet information.
struct PacketStamp {
  typedef PacketStamp self; ///< Self reference type.

  PacketStamp(); ///< Default constructor (zero elements).
  /// Set the @a time and @a generation.
  self &set(time_t time, uint32_t generation);

  time_t m_time; ///< Time when packet was sent/received.
  uint32_t m_sn; ///< Sequence # of packet.
};

/** Implementation class for EndPoint.
    All of the WCCP structures are defined in this class.

    A note on the component classes - these are designed to reside in
    a side buffer which then points in to the actual message
    buffer. This is done because the WCCP designers were not too
    bright. Rather than packing the fixed sized elements in front and
    using offsets to point at variables sized data, it's intermixed,
    so it's not possible to declare C++ structures that map on to the
    actual message data in all cases. And because mixed styles are
    worse than a consistent mediocre style, we go with the latter and
    put all the message structures on the side. This also means having
    to use accessor methods.
 */
class Impl : public ts::IntrusivePtrCounter
{
  friend class EndPoint;

public:
  typedef Impl self; ///< Self reference type.

  /// Import detail struct.
  typedef detail::endpoint::GroupData GroupData;

  /// Default constructor.
  Impl() = default;
  /** Set the local address used for this endpoint.
      If not set, an arbitrary local address will be
      @note This can only be called once, and must be called before
      @c open.
  */
  /// Force virtual destructor.
  virtual ~Impl();

  /// Open a socket for communications.
  /// @return 0 on success, -ERRNO on failure.
  virtual int open(uint32_t addr ///< Local IP address.
  );

  /// Use MD5 security.
  void useMD5Security(ts::ConstBuffer const &key ///< Shared key.
  );

  /// Perform all scheduled housekeeping functions.
  /// @return 0 for success, -errno on error.
  virtual int housekeeping() = 0;

  /// Receive and process a message.
  /// @return 0 for success, -ERRNO on system error.
  virtual ts::Rv<int> handleMessage();

  /// Check if endpoint is configured.
  /// @return @c true if ready to operate, @c false otherwise.
  virtual bool isConfigured() const = 0;

  /* These handlers simply generate an error message and return.  The
     base @c handleMessage will read the data from the socket and
     validate the message header. It then calls the appropriate one of
     these specialized message handlers.  Subclasses should override
     to process relevant messages.
  */
  /// Process HERE_I_AM message.
  virtual ts::Errata handleHereIAm(IpHeader const &header, ///< IP packet data.
                                   ts::Buffer const &data  ///< Buffer with message data.
  );
  /// Process I_SEE_YOU message.
  virtual ts::Errata handleISeeYou(IpHeader const &header, ///< IP packet data.
                                   ts::Buffer const &data  ///< Buffer with message data.
  );
  /// Process REDIRECT_ASSIGN message.
  virtual ts::Errata handleRedirectAssign(IpHeader const &header, ///< IP packet data.
                                          ts::Buffer const &data  ///< Buffer with message data.
  );
  /// Process REMOVAL_QUERY message.
  virtual ts::Errata handleRemovalQuery(IpHeader const &header, ///< IP packet data.
                                        ts::Buffer const &data  ///< Buffer with message data.
  );

protected:
  /** Local address for this end point.
      This is set only when the socket is open.
   */
  uint32_t m_addr = INADDR_ANY;
  int m_fd        = ts::NO_FD; ///< Our socket.

  bool m_use_security_opt             = false;         ///< Use group local security.
  SecurityComp::Option m_security_opt = SECURITY_NONE; ///< Type of security.
  bool m_use_security_key             = false;         ///< Use group local key.
  SecurityComp::Key m_security_key;                    ///< MD5 key.

  /// Close the socket.
  void close();
  /// Set security options in a message.
  /// @return Security option to use during message fill.
  SecurityOption setSecurity(BaseMsg &msg,          ///< Message.
                             GroupData const &group ///< Group data used to control fill.
                             ) const;
  /// Validate a security component.
  bool validateSecurity(BaseMsg &msg,          ///< Message data (including security component).
                        GroupData const &group ///< Group data for message.
  );
};
// ------------------------------------------------------
namespace detail
{
  namespace cache
  {
    /// Caches's view of caches.
    struct CacheData {
      /// Get the identifying IP address for this cache.
      uint32_t idAddr() const;
      /// Cache identity data.
      CacheIdBox m_id;
      /// Last time this cache was mentioned by the routers.
      /// Indexed in parallel to the routers.
      std::vector<PacketStamp> m_src;
    };

    /// Cache's view of routers.
    struct RouterData {
      /// Default constructor, no member initialization.
      RouterData();
      /// Construct with address.
      RouterData(uint32_t addr ///< Router identifying address.
      );

      /// Time until next packet.
      /// @return Seconds until a packet should be sent.
      time_t waitTime(time_t now ///< Current time.
                      ) const;
      /// Time till next ping.
      /// @return Seconds until a HERE_I_AM should be sent.
      time_t pingTime(time_t now ///< Current time.
                      ) const;

      uint32_t m_addr;       ///< Router identifying IP address.
      uint32_t m_generation; ///< Router's view change number.
                             /** Most recent packet received from router.
                              *  The sequence number @a m_sn is the receive ID of the router.
                              */
      PacketStamp m_recv;
      /** Most recent packet sent to router.
       *  The sequence number @a m_sn is the view generation of this cache.
       */
      PacketStamp m_xmit;
      /// Cache ID of this cache as reflected by this router.
      CacheIdBox m_local_cache_id;
      int m_rapid;      ///< Rapid replies to send.
      bool m_assign;    ///< Send a REDIRECT_ASSIGN.
      bool m_send_caps; ///< Send capabilities.
      /// Packet forwarding method selected.
      ServiceGroup::PacketStyle m_packet_forward = ServiceConstants::NO_PACKET_STYLE;
      /// Packet return method selected.
      ServiceGroup::PacketStyle m_packet_return = ServiceConstants::NO_PACKET_STYLE;
      /// Cache assignment method selected.
      ServiceGroup::CacheAssignmentStyle m_cache_assign = ServiceConstants::NO_CACHE_ASSIGN_STYLE;
    };

    /// Data for a seeded router.
    struct SeedRouter {
      SeedRouter(); ///< Default constructor, no member initialization.
      /// Construct with address @a addr.
      /// Other members are zero initialized.
      SeedRouter(uint32_t addr);
      uint32_t m_addr;  ///< Address of router.
      uint32_t m_count; ///< # of packets sent w/o response.
      time_t m_xmit;    ///< Time of last packet sent.
    };

    /// Storage type for known caches.
    typedef std::vector<CacheData> CacheBag;
    /// Storage type for known routers.
    typedef std::vector<RouterData> RouterBag;

    /** Cache's view of a service group.
        This stores the internal accounting information, it is not the
        serialized form.
    */
    struct GroupData : public endpoint::GroupData {
      typedef GroupData self;            ///< Self reference type.
      typedef endpoint::GroupData super; ///< Parent type.

      /// Cache identity of this cache.
      CacheIdBox m_id;

      /// Packet forwarding methods supported.
      ServiceGroup::PacketStyle m_packet_forward = ServiceConstants::NO_PACKET_STYLE;
      /// Packet return methods supported.
      ServiceGroup::PacketStyle m_packet_return = ServiceConstants::NO_PACKET_STYLE;
      /// Cache assignment methods supported.
      ServiceGroup::CacheAssignmentStyle m_cache_assign = ServiceConstants::NO_CACHE_ASSIGN_STYLE;

      /// Known caches.
      CacheBag m_caches;
      /// Known routers.
      RouterBag m_routers;
      char *m_proc_name;

      /// Set if there an assignment should be computed and sent.
      /// This is before checking for being a designated cache
      /// (that check is part of the assignment generation).
      bool m_assignment_pending;

      /// Seed routers.
      std::vector<SeedRouter> m_seed_routers;

      GroupData(); ///< Default constructor.

      void setProcName(const ts::ConstBuffer &name);
      const char *getProcName();

      /// Find a router by IP @a addr.
      /// @return A pointer to the router, or @c NULL if not found.
      RouterBag::iterator findRouter(uint32_t addr ///< IP address of cache.
      );

      /// Set an initial router for a service group.
      self &seedRouter(uint32_t addr ///< IP address for router.
      );
      /// Remove a seed router.
      /// @return The last time a packet was sent to the router.
      time_t removeSeedRouter(uint32_t addr ///< Identifying router address.
      );

      /// Find a cache by IP @a addr.
      /// @return An iterator to the cache, or @c NULL if not found.
      CacheBag::iterator findCache(uint32_t addr ///< IP address of cache.
      );
      /// Adjust packet stamp vectors to track routers.
      void resizeCacheSources();

      /// Time until next event.
      /// @return The number of seconds until the next event of interest.
      time_t waitTime(time_t now ///< Current time.
                      ) const;

      /** Cull routers.
          Routers that have not replied recently are moved from the
          active router list to the seed router list.

          @return @c true if any routers were culled, @c false otherwise.
      */
      bool cullRouters(time_t now ///< Current time.
      );

      /** Check to see if the process associated with service is up
       */
      bool processUp();

      /// Update state to reflect a view change.
      self &viewChanged(time_t now);

      /// Use @a key instead of global default.
      self &setKey(const char *key ///< Shared key.
      );
      /// Use security @a style instead of global default.
      self &setSecurity(SecurityOption style ///< Security style to use.
      );
    };
    inline const char *
    GroupData::getProcName()
    {
      return m_proc_name;
    }
    inline void
    GroupData::setProcName(const ts::ConstBuffer &name)
    {
      m_proc_name = ats_strndup(name.data(), name.size());
    }
  } // namespace cache
} // namespace detail

/** Implementation class for Cache Endpoint.
 */
class CacheImpl : public Impl
{
public:
  typedef CacheImpl self; ///< Self reference type.
  typedef Impl super;     ///< Parent type.

  // Import details
  typedef detail::cache::SeedRouter SeedRouter;
  typedef detail::cache::CacheData CacheData;
  typedef detail::cache::RouterData RouterData;
  typedef detail::cache::GroupData GroupData;
  typedef detail::cache::CacheBag CacheBag;
  typedef detail::cache::RouterBag RouterBag;

  /** Define a service group.
      If no service is defined for the ID in @a svc, it is created.
      If @a result is not @c NULL then it is set to
      - @c ServiceGroup::DEFINED if the service was created.
      - @c ServiceGroup::EXISTS if the service matches the existing service.
      - @c ServiceGroup::CONFLICT if the service doesn't match the existing service.

      @return The data for the service group.
  */
  virtual GroupData &defineServiceGroup(ServiceGroup const &svc,         ///< [in] Service to define.
                                        ServiceGroup::Result *result = 0 ///< [out] Result for service creation.
  );

  /** Set an initial router for a service group.
      This is needed to bootstrap the protocol.
      If the router is already seeded, this call is silently ignored.
  */
  self &seedRouter(uint8_t id,   ///< Service group ID.
                   uint32_t addr ///< IP address for router.
  );

  /// Define services from a configuration file.
  ts::Errata loadServicesFromFile(const char *path ///< Path to file.
  );

  /// Override.
  int open(uint32_t addr);

  /// Time until next scheduled event.
  time_t waitTime() const;

  /// Check for configuration.
  bool isConfigured() const;

  /// Perform all scheduled housekeeping functions.
  /// @return 0 for success, -errno on error.
  virtual int housekeeping();

  /** Check cache assignment reported by a router against internal assign.
      @return @c true if they are the same, @c false otherwise.
  */
  virtual ts::Errata checkRouterAssignment(GroupData const &group,    ///< Group with assignment.
                                           RouterViewComp const &comp ///< Assignment reported by router.
                                           ) const;

protected:
  /// Generate contents in HERE_I_AM @a msg for seed router.
  void generateHereIAm(HereIAmMsg &msg, ///< Message with allocated buffer.
                       GroupData &group ///< Group with data for message.
  );
  /// Generate contents in HERE_I_AM @a msg for active @a router.
  void generateHereIAm(HereIAmMsg &msg,   ///< Message with allocated buffer.
                       GroupData &group,  ///< Group with data for message.
                       RouterData &router ///< Target router.
  );
  /// Generate contents in REDIRECT_ASSIGN @a msg for a service @a group.
  void generateRedirectAssign(RedirectAssignMsg &msg, ///< Message with allocated buffer.
                              GroupData &group        ///< Group with data for message.
  );
  /// Process HERE_I_AM message.
  virtual ts::Errata handleISeeYou(IpHeader const &header, ///< IP packet data.
                                   ts::Buffer const &data  ///< Buffer with message data.
  );
  /// Process REMOVAL_QUERY message.
  virtual ts::Errata handleRemovalQuery(IpHeader const &header, ///< IP packet data.
                                        ts::Buffer const &data  ///< Message data.
  );

  /// Map Service Group ID to Service Group Data.
  typedef std::map<uint8_t, GroupData> GroupMap;
  /// Active service groups.
  GroupMap m_groups;
};
// ------------------------------------------------------
namespace detail
{
  namespace router
  {
    /** Router's view of a cache.
        @a m_count tracks the number of packets received from this particular
        cache. The RFC is unclear but it looks like this should be tracked
        independently for each target address (which can be different than
        caches if multicasting). A response is pending if @a m_count is
        different than @ m_xmit.m_gen which is the received count last time
        this router sent this cache a response.
    */
    struct CacheData {
      /// Get the identifying IP address for this cache.
      uint32_t idAddr() const;

      /// Received count for this cache.
      uint32_t m_recv_count;
      /// Change number of last received message.
      uint32_t m_generation;
      /// Need to send a response to this cache.
      bool m_pending;
      /// Address used by cache to send to this router.
      uint32_t m_to_addr;
      /// Stamp for last pack transmitted to this cache.
      PacketStamp m_xmit;
      //// Stamp for last packet received from this cache.
      PacketStamp m_recv;

      CacheIdBox m_id;        ///< Transmitted cache descriptor.
      uint32_t m_target_addr; ///< Target address of last packet.
    };

    /// Router's view of other routers.
    struct RouterData {
      typedef RouterData self; ///< Self reference type.

      /// Identifying IP address of router.
      uint32_t m_addr;
      /** Stamp for last mention of this router from a cache.
          Indexed in parallel with the Caches.
          The sequence number @a m_sn is the cache's change #.
      */
      std::vector<PacketStamp> m_src;
      /// Resize the packet stamp vector.
      self &resize(size_t);
    };

    /// Storage type for known caches.
    typedef std::vector<CacheData> CacheBag;
    /// Storage type for known routers.
    typedef std::vector<RouterData> RouterBag;

    /** A router's view of a service group.

        This stores the internal accounting information, it is not the
        serialized form.
    */
    struct GroupData : public detail::endpoint::GroupData {
      typedef GroupData self;                    ///< Self reference type.
      typedef detail::endpoint::GroupData super; ///< Parent type.

      GroupData(); ///< Default constructor.

      /// Known caches.
      CacheBag m_caches;
      /// Known (other) routers.
      RouterBag m_routers;

      /// Find a cache by IP @a addr.
      /// @return An iterator to the cache, or @c NULL if not found.
      CacheBag::iterator findCache(uint32_t addr ///< IP address of cache.
      );
      /// Adjust packet stamp vectors to track caches.
      void resizeRouterSources();
    };
  } // namespace router
} // namespace detail

/** Implementation class for Router Endpoint.
 */
class RouterImpl : public Impl
{
public:
  typedef RouterImpl self; ///< Self reference type.
  typedef Impl super;      ///< Parent type.
  // Import details
  typedef detail::router::CacheData CacheData;
  typedef detail::router::RouterData RouterData;
  typedef detail::router::GroupData GroupData;
  typedef detail::router::CacheBag CacheBag;
  typedef detail::router::RouterBag RouterBag;

  /// Process HERE_I_AM message.
  virtual ts::Errata handleHereIAm(IpHeader const &header, ///< IP packet data.
                                   ts::Buffer const &data  ///< Buffer with message data.
  );
  /// Perform all scheduled housekeeping functions.
  int housekeeping();
  /// Send pending I_SEE_YOU messages.
  int xmitISeeYou();
  /// Check for configuration.
  bool isConfigured() const;

protected:
  /** Find or create a service group record.
      If no service is defined for the ID, it is created.
      If @a result is not @c NULL then it is set to
      - @c ServiceGroup::DEFINED if the service was created.
      - @c ServiceGroup::EXISTS if the service matches the existing service.
      - @c ServiceGroup::CONFLICT if the service doesn't match the existing service.

      @return The data for the service group.
  */
  GroupData &defineServiceGroup(ServiceGroup const &svc, ServiceGroup::Result *result);
  /// Fill out message data.
  void generateISeeYou(ISeeYouMsg &msg,  ///< Message structure to fill.
                       GroupData &group, ///< Group data for message.
                       CacheData &cache  ///< Target cache.
  );

  /// Map Service Group ID to Service Group Data.
  typedef std::map<uint8_t, GroupData> GroupMap;
  /// Active service groups.
  GroupMap m_groups;
};
// ------------------------------------------------------
inline RouterId::RouterId() : m_addr(0), m_recv_id(0) {}
inline RouterId::RouterId(uint32_t addr, uint32_t recv_id) : m_addr(addr), m_recv_id(recv_id) {}

inline RouterIdElt::RouterIdElt(uint32_t addr, uint32_t recv_id) : super(addr, htonl(recv_id)) {}
inline uint32_t
RouterIdElt::getAddr() const
{
  return m_addr;
}
inline RouterIdElt &
RouterIdElt::setAddr(uint32_t addr)
{
  m_addr = addr;
  return *this;
}
inline uint32_t
RouterIdElt::getRecvId() const
{
  return ntohl(m_recv_id);
}
inline RouterIdElt &
RouterIdElt::setRecvId(uint32_t recv_id)
{
  m_recv_id = htonl(recv_id);
  return *this;
}
inline RouterIdElt &
RouterIdElt::operator=(super const &that)
{
  return this->setAddr(that.m_addr).setRecvId(that.m_recv_id);
}

inline MaskElt::MaskElt() {}

inline MaskElt::MaskElt(uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
  : m_src_addr(srcAddr), m_dst_addr(dstAddr), m_src_port(srcPort), m_dst_port(dstPort)
{
}

inline uint32_t
MaskElt::getSrcAddr() const
{
  return ntohl(m_src_addr);
}
inline MaskElt &
MaskElt::setSrcAddr(uint32_t mask)
{
  m_src_addr = htonl(mask);
  return *this;
}
inline uint32_t
MaskElt::getDstAddr() const
{
  return ntohl(m_dst_addr);
}
inline MaskElt &
MaskElt::setDstAddr(uint32_t mask)
{
  m_dst_addr = htonl(mask);
  return *this;
}
inline uint16_t
MaskElt::getSrcPort() const
{
  return ntohs(m_src_port);
}
inline MaskElt &
MaskElt::setSrcPort(uint16_t mask)
{
  m_src_port = htons(mask);
  return *this;
}
inline uint16_t
MaskElt::getDstPort() const
{
  return ntohs(m_dst_port);
}
inline MaskElt &
MaskElt::setDstPort(uint16_t mask)
{
  m_dst_port = htons(mask);
  return *this;
}

inline ValueElt::ValueElt() {}

inline ValueElt::ValueElt(uint32_t cacheAddr, uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
  : m_src_addr(srcAddr), m_dst_addr(dstAddr), m_src_port(srcPort), m_dst_port(dstPort), m_cache_addr(cacheAddr)
{
}

inline MaskValueSetElt::MaskValueSetElt() {}
inline MaskValueSetElt::MaskValueSetElt(uint32_t count) : m_count(count) {}
inline MaskElt &
MaskValueSetElt::maskElt()
{
  return m_mask;
}
inline uint32_t
MaskValueSetElt::getCount() const
{
  return ntohl(m_count);
}

inline uint32_t
MaskValueSetElt::getSrcAddrMask() const
{
  return m_mask.getSrcAddr();
}
inline MaskValueSetElt &
MaskValueSetElt::setSrcAddrMask(uint32_t mask)
{
  m_mask.setSrcAddr(mask);
  return *this;
}
inline uint32_t
MaskValueSetElt::getDstAddrMask() const
{
  return m_mask.getDstAddr();
}
inline MaskValueSetElt &
MaskValueSetElt::setDstAddrMask(uint32_t mask)
{
  m_mask.setDstAddr(mask);
  return *this;
}
inline uint16_t
MaskValueSetElt::getSrcPortMask() const
{
  return m_mask.getSrcPort();
}
inline MaskValueSetElt &
MaskValueSetElt::setSrcPortMask(uint16_t mask)
{
  m_mask.setSrcPort(mask);
  return *this;
}
inline uint16_t
MaskValueSetElt::getDstPortMask() const
{
  return m_mask.getDstPort();
}
inline MaskValueSetElt &
MaskValueSetElt::setDstPortMask(uint16_t mask)
{
  m_mask.setDstPort(mask);
  return *this;
}
inline ValueElt *
MaskValueSetElt::values()
{
  return reinterpret_cast<ValueElt *>(this + 1);
}
inline ValueElt const *
MaskValueSetElt::values() const
{
  return const_cast<self *>(this)->values();
}
inline size_t
MaskValueSetElt::calcSize(uint32_t n)
{
  return sizeof(self) + n * sizeof(ValueElt);
}
inline size_t
MaskValueSetElt::getSize() const
{
  return self::calcSize(ntohl(m_count));
}

inline size_t
MaskAssignElt::getSize() const
{
  return sizeof(self) + this->getVarSize();
}

inline uint32_t
CacheIdElt::getAddr() const
{
  return m_addr;
}
inline CacheIdElt &
CacheIdElt::setAddr(uint32_t addr)
{
  m_addr = addr;
  return *this;
}
inline uint16_t
CacheIdElt::getHashRev() const
{
  return ntohs(m_hash_rev);
}
inline CacheIdElt &
CacheIdElt::setHashRev(uint16_t addr)
{
  m_hash_rev = htons(addr);
  return *this;
}
inline CacheIdElt &
CacheIdElt::initHashRev()
{
  this->setHashRev(HASH_REVISION);
  return *this;
}
inline bool
CacheIdElt::getUnassigned() const
{
  return 1 == m_unassigned;
}
inline CacheIdElt &
CacheIdElt::setUnassigned(bool state)
{
  m_unassigned = state ? 1 : 0;
  return *this;
}
inline CacheIdElt &
CacheIdElt::clearReserved()
{
  m_reserved_0 = 0;
  m_reserved_1 = 0;
  m_reserved_2 = 0;
  return *this;
}
inline bool
CacheIdElt::isMask() const
{
  return 1 == m_is_mask;
}
inline CacheIdElt &
CacheIdElt::setMask(bool state)
{
  m_is_mask = state ? 1 : 0;
  return *this;
}

#if 0
inline uint16_t CacheIdElt::getWeight() const { return ntohs(m_weight); }
inline CacheIdElt&
CacheIdElt::setWeight(uint16_t w) {
  m_weight = htons(w);
  return *this;
}
inline uint16_t CacheIdElt::getStatus() const { return ntohs(m_status); }
inline CacheIdElt&
CacheIdElt::setStatus(uint16_t s) {
  m_status = htons(s);
  return *this;
}
#endif

inline CacheIdElt::Tail *
CacheHashIdElt::getTailPtr()
{
  return &m_tail;
}

#if 0
inline bool
CacheHashIdElt::getBucket(int idx) const {
  return 0 != (m_buckets[idx>>3] & (1<<(idx & 7)));
}
#endif

inline uint32_t
CacheMaskIdElt::getCount() const
{
  return m_assign.getCount();
}

inline size_t
CacheMaskIdElt::getSize() const
{
  return sizeof(self) + sizeof(Tail) + m_assign.getVarSize();
}

inline CacheIdElt::Tail *
CacheMaskIdElt::getTailPtr()
{
  return reinterpret_cast<Tail *>(reinterpret_cast<char *>(this) + sizeof(self) + m_assign.getVarSize());
}

inline uint32_t
CacheIdBox::getAddr() const
{
  return m_base->getAddr();
}

inline CacheIdBox &
CacheIdBox::setAddr(uint32_t addr)
{
  m_base->setAddr(addr);
  return *this;
}

inline CacheIdBox &
CacheIdBox::setUnassigned(bool state)
{
  m_base->setUnassigned(state);
  return *this;
}

inline AssignmentKeyElt::AssignmentKeyElt(uint32_t addr, uint32_t n) : m_addr(addr), m_change_number(htonl(n)) {}
inline uint32_t
AssignmentKeyElt::getAddr() const
{
  return m_addr;
}
inline AssignmentKeyElt &
AssignmentKeyElt::setAddr(uint32_t addr)
{
  m_addr = addr;
  return *this;
}
inline uint32_t
AssignmentKeyElt::getChangeNumber() const
{
  return ntohl(m_change_number);
}
inline AssignmentKeyElt &
AssignmentKeyElt::setChangeNumber(uint32_t n)
{
  m_change_number = htonl(n);
  return *this;
}

inline RouterAssignElt::RouterAssignElt(uint32_t addr, uint32_t recv_id, uint32_t change_number)
  : super(addr, recv_id), m_change_number(htonl(change_number))
{
}
inline uint32_t
RouterAssignElt::getChangeNumber() const
{
  return ntohl(m_change_number);
}
inline RouterAssignElt &
RouterAssignElt::setChangeNumber(uint32_t n)
{
  m_change_number = htonl(n);
  return *this;
}

inline SecurityComp::SecurityComp() : m_local_key(false) {}
inline void
SecurityComp::setDefaultOption(Option opt)
{
  m_default_opt = opt;
}
inline size_t
SecurityComp::calcSize(Option opt)
{
  return SECURITY_NONE == opt ? sizeof(RawNone) : sizeof(RawMD5);
}

inline ServiceComp::ServiceComp() : m_port_count(0) {}
inline ServiceComp::raw_t *
ServiceComp::access()
{
  return reinterpret_cast<raw_t *>(m_base);
}
inline ServiceComp::raw_t const *
ServiceComp::access() const
{
  return reinterpret_cast<raw_t const *>(m_base);
}
inline ServiceGroup::Type
ServiceComp::getSvcType() const
{
  return this->access()->getSvcType();
}
inline ServiceComp &
ServiceComp::setSvcType(ServiceGroup::Type t)
{
  this->access()->setSvcType(t);
  return *this;
}
inline uint8_t
ServiceComp::getSvcId() const
{
  return this->access()->getSvcId();
}
inline ServiceComp &
ServiceComp::setSvcId(uint8_t id)
{
  this->access()->setSvcId(id);
  return *this;
}
inline uint8_t
ServiceComp::getPriority() const
{
  return this->access()->getPriority();
}
inline ServiceComp &
ServiceComp::setPriority(uint8_t pri)
{
  this->access()->setPriority(pri);
  return *this;
}
inline uint8_t
ServiceComp::getProtocol() const
{
  return this->access()->getProtocol();
}
inline ServiceComp &
ServiceComp::setProtocol(uint8_t proto)
{
  this->access()->setProtocol(proto);
  return *this;
}
inline uint32_t
ServiceComp::getFlags() const
{
  return this->access()->getFlags();
}
inline ServiceComp &
ServiceComp::setFlags(uint32_t flags)
{
  this->access()->setFlags(flags);
  return *this;
}
inline ServiceComp &
ServiceComp::enableFlags(uint32_t flags)
{
  this->access()->enableFlags(flags);
  return *this;
}
inline ServiceComp &
ServiceComp::disableFlags(uint32_t flags)
{
  this->access()->disableFlags(flags);
  return *this;
}
inline uint16_t
ServiceComp::getPort(int idx) const
{
  return this->access()->getPort(idx);
}
inline size_t
ServiceComp::calcSize()
{
  return sizeof(raw_t);
}
inline ServiceComp::operator ServiceGroup const &() const
{
  return *static_cast<ServiceGroup const *>(this->access());
}

inline size_t
RouterIdComp::calcSize(int n)
{
  return sizeof(raw_t) + n * sizeof(uint32_t);
}

inline uint32_t
RouterViewComp::getKeyAddr() const
{
  return this->keyElt().getAddr();
}
inline RouterViewComp &
RouterViewComp::setKeyAddr(uint32_t addr)
{
  this->keyElt().setAddr(addr);
  return *this;
}
inline uint32_t
RouterViewComp::getKeyChangeNumber() const
{
  return this->keyElt().getChangeNumber();
}
inline RouterViewComp &
RouterViewComp::setKeyChangeNumber(uint32_t change_number)
{
  this->keyElt().setChangeNumber(change_number);
  return *this;
}
inline CacheIdBox const &
RouterViewComp::cacheId(int idx) const
{
  return const_cast<self *>(this)->cacheId(idx);
}

inline CacheIdBox &
CacheIdComp::cacheId()
{
  return m_box;
}
inline CacheIdBox const &
CacheIdComp::cacheId() const
{
  return m_box;
}
inline uint32_t
CacheIdComp::getAddr() const
{
  return this->cacheId().getAddr();
}
inline CacheIdComp &
CacheIdComp::setAddr(uint32_t addr)
{
  this->cacheId().setAddr(addr);
  return *this;
}
inline uint16_t
CacheIdComp::getHashRev() const
{
  return this->cacheId().getHashRev();
}
inline CacheIdComp &
CacheIdComp::setHashRev(uint16_t rev)
{
  this->cacheId().setHashRev(rev);
  return *this;
}
inline bool
CacheIdComp::getUnassigned() const
{
  return this->cacheId().getUnassigned();
}
inline CacheIdComp &
CacheIdComp::setUnassigned(bool state)
{
  this->cacheId().setUnassigned(state);
  return *this;
}
#if 0
inline uint16_t
CacheIdComp::getWeight() const {
  return this->idElt().getWeight();
}
inline CacheIdComp&
CacheIdComp::setWeight(uint16_t w) {
  this->idElt().setWeight(w);
  return *this;
}
inline uint16_t
CacheIdComp::getStatus() const {
  return this->idElt().getStatus();
}
inline CacheIdComp&
CacheIdComp::setStatus(uint16_t s) {
  this->idElt().setStatus(s);
  return *this;
}
inline bool
CacheIdComp::getBucket(int idx) const {
  return this->idElt().getBucket(idx);
}
inline CacheIdComp&
CacheIdComp::setBucket(int idx, bool state) {
  this->idElt().setBucket(idx, state);
  return *this;
}
inline CacheIdComp&
CacheIdComp::setBuckets(bool state) {
  this->idElt().setBuckets(state);
  return *this;
}
inline size_t CacheIdComp::calcSize() { return sizeof(raw_t); }
#endif

inline bool
detail::Assignment::isActive() const
{
  return m_active;
}
inline detail::Assignment &
detail::Assignment::setActive(bool state)
{
  m_active = state;
  return *this;
}
inline detail::Assignment &
detail::Assignment::updateRouterId(uint32_t addr, uint32_t rcvid, uint32_t cno)
{
  if (m_router_list)
    m_router_list->updateRouterId(addr, rcvid, cno);
  return *this;
}
inline AssignmentKeyElt::AssignmentKeyElt() {}
inline AssignmentKeyElt const &
detail::Assignment::getKey() const
{
  return m_key;
}
inline RouterAssignListElt const &
detail::Assignment::getRouterList() const
{
  assert(m_router_list);
  return *m_router_list;
}
inline HashAssignElt const &
detail::Assignment::getHash() const
{
  assert(m_hash_assign);
  return *m_hash_assign;
}
inline MaskAssignElt const &
detail::Assignment::getMask() const
{
  assert(m_mask_assign);
  return *m_mask_assign;
}

inline MsgBuffer::MsgBuffer() : super(), _count(0) {}
inline MsgBuffer::MsgBuffer(super const &that) : super(that), _count(0) {}
inline MsgBuffer::MsgBuffer(void *p, size_t n) : super(static_cast<char *>(p), n), _count(0) {}

inline size_t
MsgBuffer::getSize() const
{
  return _size;
}
inline size_t
MsgBuffer::getCount() const
{
  return _count;
}
inline char *
MsgBuffer::getBase()
{
  return _ptr;
}
inline const char *
MsgBuffer::getBase() const
{
  return _ptr;
}
inline char *
MsgBuffer::getTail()
{
  return _ptr + _count;
}
inline size_t
MsgBuffer::getSpace() const
{
  return _size - _count;
}
inline MsgBuffer &
MsgBuffer::reset()
{
  _count = 0;
  return *this;
}

inline MsgBuffer &
MsgBuffer::set(void *ptr, size_t n)
{
  _ptr   = static_cast<char *>(ptr);
  _size  = n;
  _count = 0;
  return *this;
}

inline MsgBuffer &
MsgBuffer::use(size_t n)
{
  _count += std::min(n, this->getSpace());
  return *this;
}

inline MsgBuffer &
MsgBuffer::zero()
{
  memset(_ptr, 0, _size);
  _count = 0;
  return *this;
}

inline PacketStamp::PacketStamp() : m_time(0), m_sn(0) {}

inline PacketStamp &
PacketStamp::set(time_t time, uint32_t sn)
{
  m_time = time;
  m_sn   = sn;
  return *this;
}

inline ServiceGroup::ServiceGroup() : m_svc_type(STANDARD), m_svc_id(0), m_priority(0), m_protocol(0), m_flags(0) {}

inline RouterIdElt::RouterIdElt() {}
inline RouterAssignElt::RouterAssignElt() : m_change_number(0) {}

inline RouterAssignListElt::RouterAssignListElt() {}
inline RouterAssignListElt::RouterAssignListElt(int n) : m_count(htonl(n)) {}
inline RouterAssignElt &
RouterAssignListElt::elt(int n)
{
  return access_array<RouterAssignElt>(this + 1)[n];
}
inline RouterAssignElt const &
RouterAssignListElt::elt(int n) const
{
  return const_cast<self *>(this)->elt(n);
}
inline size_t
RouterAssignListElt::calcVarSize(int n)
{
  return n * sizeof(RouterAssignElt);
}
inline size_t
RouterAssignListElt::calcSize(int n)
{
  return sizeof(self) + self::calcVarSize(n);
}
inline size_t
RouterAssignListElt::getSize() const
{
  return this->calcSize(this->getCount());
}
inline size_t
RouterAssignListElt::getVarSize() const
{
  return this->getSize() - sizeof(self);
}
// This is untainted because an overall size check is done when the packet is read. If any of the
// counts are bogus, that size check will fail.
// coverity[ -tainted_data_return]
inline uint32_t
RouterAssignListElt::getCount() const
{
  return ntohl(m_count);
}

inline HashAssignElt::HashAssignElt() {}
inline HashAssignElt::HashAssignElt(int n) : m_count(htonl(n)) {}
// This is untainted because an overall size check is done when the packet is read. If any of the
// counts are bogus, that size check will fail.
// coverity[ -tainted_data_return]
inline uint32_t
HashAssignElt::getCount() const
{
  return ntohl(m_count);
}
inline size_t
HashAssignElt::calcSize(int n)
{
  return sizeof(self) + n * sizeof(uint32_t) + sizeof(Bucket) * N_BUCKETS;
}
inline size_t
HashAssignElt::getSize() const
{
  return self::calcSize(this->getCount());
}
inline uint32_t
HashAssignElt::getAddr(int idx) const
{
  // coverity[ptr_arith]
  return (&m_count)[idx + 1];
}
inline HashAssignElt &
HashAssignElt::setAddr(int idx, uint32_t addr)
{
  // coverity[ptr_arith]
  (&m_count)[idx + 1] = addr;
  return *this;
}
inline HashAssignElt::Bucket *
HashAssignElt::getBucketBase()
{
  // coverity[ptr_arith]
  return reinterpret_cast<Bucket *>((&m_count + 1 + this->getCount()));
}
inline HashAssignElt::Bucket &HashAssignElt::operator[](size_t idx)
{
  return this->getBucketBase()[idx];
}
inline HashAssignElt::Bucket const &HashAssignElt::operator[](size_t idx) const
{
  return (*(const_cast<self *>(this)))[idx];
}

// This is untainted because an overall size check is done when the packet is read. If any of the
// counts are bogus, that size check will fail.
// coverity[ -tainted_data_return]
inline uint32_t
MaskAssignElt::getCount() const
{
  return ntohl(m_count);
}
inline MaskValueSetElt *MaskAssignElt::appender::operator->()
{
  return m_set;
}
inline MaskValueSetElt *
MaskAssignElt::appender::initSet(uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
{
  (*(new (m_set) MaskValueSetElt(0)))
    .setSrcAddrMask(srcAddr)
    .setDstAddrMask(dstAddr)
    .setSrcPortMask(srcPort)
    .setDstPortMask(dstPort);
  return m_set;
}
inline MaskValueSetElt *
MaskAssignElt::appender::mask(uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
{
  m_set          = reinterpret_cast<MaskValueSetElt *>(reinterpret_cast<char *>(m_set) + m_set->getSize());
  m_elt->m_count = htonl(1 + m_elt->getCount()); // bump set count.
  this->initSet(srcAddr, dstAddr, srcPort, dstPort);
  return m_set;
}
inline MaskAssignElt::appender
MaskAssignElt::init(uint32_t srcAddr, uint32_t dstAddr, uint16_t srcPort, uint16_t dstPort)
{
  appender zret;
  m_count    = htonl(1);
  zret.m_set = reinterpret_cast<MaskValueSetElt *>(this + 1);
  zret.m_elt = this;
  zret.initSet(srcAddr, dstAddr, srcPort, dstPort);
  return zret;
}

inline bool
ComponentBase::isEmpty() const
{
  return 0 == m_base;
}

inline message_type_t
MsgHeaderComp::toMsgType(int t)
{
  return HERE_I_AM != t && I_SEE_YOU != t && REDIRECT_ASSIGN != t && REMOVAL_QUERY != t ? INVALID_MSG_TYPE :
                                                                                          static_cast<message_type_t>(t);
}

template <typename T>
CompType
CompWithHeader<T>::getType() const
{
  return static_cast<CompType>(ntohs(reinterpret_cast<raw_t const *>(m_base)->m_type));
}

template <typename T>
T &
CompWithHeader<T>::setType(CompType t)
{
  reinterpret_cast<raw_t *>(m_base)->m_type = htons(t);
  return static_cast<T &>(*this);
}

template <typename T>
uint16_t
CompWithHeader<T>::getLength() const
{
  return ntohs(reinterpret_cast<raw_t const *>(m_base)->m_length);
}

template <typename T>
T &
CompWithHeader<T>::setLength(uint16_t length)
{
  reinterpret_cast<raw_t *>(m_base)->m_length = htons(length);
  return static_cast<T &>(*this);
}

template <typename T>
int
CompWithHeader<T>::checkHeader(MsgBuffer const &buffer, CompType ect)
{
  CompType act = this->getType();
  if (act != ect)
    return (act < COMP_TYPE_MIN || COMP_TYPE_MAX < act) ? PARSE_COMP_TYPE_INVALID : PARSE_COMP_OTHER_TYPE;
  if (this->getLength() + sizeof(raw_t) > buffer.getSpace())
    return PARSE_COMP_TOO_BIG;
  return PARSE_SUCCESS;
}

inline AssignInfoComp::Bucket &
AssignInfoComp::bucket(int idx)
{
  return m_buckets[idx];
}
inline AssignInfoComp::Bucket const &
AssignInfoComp::bucket(int idx) const
{
  return m_buckets[idx];
}
inline RouterViewComp::RouterViewComp() : m_cache_count(0)
{
  memset(m_cache_ids, 0, sizeof(m_cache_ids));
}

inline CapComp::CapComp() {}
inline CapComp &
CapComp::invalidate()
{
  m_cached = false;
  return *this;
}
inline uint32_t
CapComp::getEltCount() const
{
  return this->m_count;
}
inline size_t
CapComp::calcSize(int n)
{
  return sizeof(super::raw_t) + n * sizeof(CapabilityElt);
}
inline ServiceGroup::PacketStyle
CapComp::getPacketForwardStyle() const
{
  if (!m_cached)
    this->cache();
  return m_packet_forward;
}
inline ServiceGroup::PacketStyle
CapComp::getPacketReturnStyle() const
{
  if (!m_cached)
    this->cache();
  return m_packet_return;
}
inline ServiceGroup::CacheAssignmentStyle
CapComp::getCacheAssignmentStyle() const
{
  if (!m_cached)
    this->cache();
  return m_cache_assign;
}

inline AssignMapComp::AssignMapComp() {}

/*  Implementation note: Due to a bug in gcc, we have to be
    careful with these fields. If we use the field access templates
    directly, we get bad results because the pointer to member matching
    is done incorrectly (it uses a super type, not @c raw_t). We work
    around this by putting the pointer to member in a static variable.
*/
inline uint16_t
AltAssignComp::getAssignType() const
{
  static uint16_t raw_t::*mptr = &raw_t::m_assign_type;
  return get_field(mptr, m_base);
}
inline AltAssignComp &
AltAssignComp::setAssignType(uint16_t t)
{
  static uint16_t raw_t::*mptr = &raw_t::m_assign_type;
  set_field(mptr, m_base, t);
  return *this;
}
inline uint16_t
AltAssignComp::getAssignLength() const
{
  static uint16_t raw_t::*mptr = &raw_t::m_assign_length;
  return get_field(mptr, m_base);
}
inline AltAssignComp &
AltAssignComp::setAssignLength(uint16_t length)
{
  static uint16_t raw_t::*mptr = &raw_t::m_assign_length;
  set_field(mptr, m_base, length);
  return *this;
}

inline uint32_t
QueryComp::getRouterAddr() const
{
  return access_field(&raw_t::m_router_addr, m_base);
}
inline QueryComp &
QueryComp::setRouterAddr(uint32_t addr)
{
  access_field(&raw_t::m_router_addr, m_base) = addr;
  return *this;
}
inline uint32_t
QueryComp::getToAddr() const
{
  return access_field(&raw_t::m_to_addr, m_base);
}
inline QueryComp &
QueryComp::setToAddr(uint32_t addr)
{
  access_field(&raw_t::m_to_addr, m_base) = addr;
  return *this;
}
inline uint32_t
QueryComp::getCacheAddr() const
{
  return access_field(&raw_t::m_cache_addr, m_base);
}
inline QueryComp &
QueryComp::setCacheAddr(uint32_t addr)
{
  access_field(&raw_t::m_cache_addr, m_base) = addr;
  return *this;
}
inline uint32_t
QueryComp::getRecvId() const
{
  return get_field(&raw_t::m_recv_id, m_base);
}
inline QueryComp &
QueryComp::setRecvId(uint32_t data)
{
  set_field(&raw_t::m_recv_id, m_base, data);
  return *this;
}
inline size_t
QueryComp::calcSize()
{
  return sizeof(raw_t);
}

inline detail::cache::SeedRouter::SeedRouter() {}

inline detail::cache::SeedRouter::SeedRouter(uint32_t addr) : m_addr(addr), m_count(0), m_xmit(0) {}

inline BaseMsg::BaseMsg() : m_buffer(0, 0) {}
inline MsgBuffer const &
BaseMsg::buffer() const
{
  return m_buffer;
}
inline size_t
BaseMsg::getSpace() const
{
  return m_buffer.getSpace();
}
inline size_t
BaseMsg::getCount() const
{
  return m_buffer.getCount();
}

inline RouterImpl::RouterData &
RouterImpl::RouterData::resize(size_t n)
{
  m_src.resize(n);
  return *this;
}
// ------------------------------------------------------

} // namespace wccp
