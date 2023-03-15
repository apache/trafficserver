// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   IP address and network related classes.
 */

#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include "swoc/swoc_version.h"
#include "swoc/swoc_meta.h"
#include "swoc/MemSpan.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

using std::string_view;

union IPEndpoint;
class IPAddr;
class IPMask;

/** Storage for an IPv4 address.
    Stored in host order.
 */
class IP4Addr {
  using self_type = IP4Addr; ///< Self reference type.
  friend class IP4Range;

public:
  static constexpr size_t SIZE  = sizeof(in_addr_t);                                 ///< Size of IPv4 address in bytes.
  static constexpr size_t WIDTH = std::numeric_limits<unsigned char>::digits * SIZE; ///< # of bits in an address.

  static const self_type MIN;                                                        ///< Minimum value.
  static const self_type MAX;                                                        ///< Maximum value.
  static constexpr sa_family_t AF_value = AF_INET;                                   ///< Address family type.

  constexpr IP4Addr() = default; ///< Default constructor - ANY address.

  /// Copy constructor.
  IP4Addr(self_type const& that) = default;

  /// Construct using IPv4 @a addr (in host order).
  /// @note Host order seems odd, but all of the standard network macro values such as @c INADDR_LOOPBACK
  /// are in host order.
  explicit constexpr IP4Addr(in_addr_t addr);

  /// Construct from @c sockaddr_in.
  explicit IP4Addr(sockaddr_in const *s);

  /// Construct from text representation.
  /// If the @a text is invalid the result is @c INADDR_ANY
  IP4Addr(string_view const &text);

  /// Self assignment.
  self_type & operator=(self_type const& that) = default;

  /// Assign from IPv4 raw address.
  self_type &operator=(in_addr_t ip);

  /// Set to the address in @a addr.
  self_type &operator=(sockaddr_in const *sa);

  /// Increment address.
  self_type &operator++();

  /// Decrement address.
  self_type &operator--();

  /** Byte access.
   *
   * @param idx Byte index.
   * @return The byte at @a idx in the address (network order).
   *
   * For convenience, this returns in "text order" of the octets.
   */
  uint8_t operator[](unsigned idx) const;

  /// Apply @a mask to address, leaving the network portion.
  self_type &operator&=(IPMask const &mask);

  /// Apply @a mask to address, creating the broadcast address.
  self_type &operator|=(IPMask const &mask);

  /** Update socket address with this address.
   *
   * @param sa Socket address.
   * @return @sa
   *
   * @a sa is assumed to be large enough to hold an IPv4 address.
   */
  sockaddr *copy_to(sockaddr *sa) const;

  /** Update socket address with this address.
   *
   * @param sin IPv4 socket address.
   * @return @sin
   */
  sockaddr_in *copy_to(sockaddr_in *sin) const;

  /// @return The address in network order.
  in_addr_t network_order() const;

  /// @return The address in host order.
  in_addr_t host_order() const;

  /** Parse IPv4 address.
   *
   * @param text Text to parse.
   *
   * @return @c true if @a text is a valid IPv4 address, @c false otherwise.
   *
   * Whitespace is trimmed from @text before parsing. If the parse fails @a this is set to @c INADDR_ANY.
  */
  bool load(string_view const &text);

  /// Standard ternary compare.
  int cmp(self_type const &that) const;

  /// Get the IP address family.
  /// @return @c AF_INET
  /// @note Useful primarily for template classes.
  constexpr sa_family_t family() const;

  /// @return @c true if this is the "any" address, @c false if not.
  bool is_any() const;

  /// @return @c true if this is a multicast address, @c false if not.
  bool is_multicast() const;

  /// @return @c true if this is a loopback address, @c false if not.
  bool is_loopback() const;

  /// @return @c true if the address is in the link local network.
  bool is_link_local() const;

  /// @return @c true if the address is private.
  bool is_private() const;

  /** Left shift.
   *
   * @param n Number of bits to shift left.
   * @return @a this.
   */
  self_type &operator<<=(unsigned n);

  /** Right shift.
   *
   * @param n Number of bits to shift right.
   * @return @a this.
   */
  self_type &operator>>=(unsigned n);

  /** Bitwise AND.
   *
   * @param that Source address.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise AND of the corresponding bits in @a this and @a that.
   */
  self_type &operator&=(self_type const &that);

  /** Bitwise OR.
   *
   * @param that Source address.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise OR of the corresponding bits in @a this and @a that.
   */
  self_type &operator|=(self_type const &that);

  /** Convert between network and host order.
   *
   * @param src Input address.
   * @return @a src with the byte reversed.
   *
   * This performs the same computation as @c ntohl and @c htonl but is @c constexpr to be usable
   * in situations those two functions are not.
   */
  constexpr static in_addr_t reorder(in_addr_t src);

protected:
  /// Access by bytes.
  using bytes = std::array<uint8_t, 4>;

  friend bool operator==(self_type const &, self_type const &);
  friend bool operator!=(self_type const &, self_type const &);
  friend bool operator<(self_type const &, self_type const &);
  friend bool operator<=(self_type const &, self_type const &);

  in_addr_t _addr = INADDR_ANY; ///< Address in host order.
};

/** Storage for an IPv6 address.
    Internal storage is not necessarily network ordered.
    @see network_order
    @see copy_to
 */
class IP6Addr {
  using self_type = IP6Addr; ///< Self reference type.

  friend class IP6Range;
  friend class IPMask;

public:
  static constexpr size_t WIDTH         = 128;                                          ///< Number of bits in the address.
  static constexpr size_t SIZE          = WIDTH / std::numeric_limits<uint8_t>::digits; ///< Size of address in bytes.
  static constexpr sa_family_t AF_value = AF_INET6;                                     ///< Address family type.

  using quad_type                 = uint16_t;                 ///< Size of one segment of an IPv6 address.
  static constexpr size_t N_QUADS = SIZE / sizeof(quad_type); ///< # of quads in an IPv6 address.
  /// Number of bits per quad.
  static constexpr size_t QUAD_WIDTH = std::numeric_limits<uint8_t>::digits * sizeof(quad_type);

  /// Direct access type for the address.
  /// Equivalent to the data type for data member @c s6_addr in @c in6_addr.
  using raw_type = std::array<uint8_t, SIZE>;

  /// Minimum value of an address.
  static const self_type MIN;
  /// Maximum value of an address.
  static const self_type MAX;

  IP6Addr()            = default; ///< Default constructor - ANY address.
  IP6Addr(self_type const &that) = default;

  /// Construct using IPv6 @a addr.
  explicit IP6Addr(in6_addr const &addr);

  /// Construct from @c sockaddr_in.
  explicit IP6Addr(sockaddr_in6 const *addr) { *this = addr; }

  /// Construct from text representation.
  /// If the @a text is invalid the result is any address.
  /// @see load
  IP6Addr(string_view const &text);

  /** Construct mapped IPv4 address.
   *
   * @param addr IPv4 address
   */
  explicit IP6Addr(IP4Addr addr);

  /// Self assignment.
  self_type & operator=(self_type const& that) = default;

  /** Left shift.
   *
   * @param n Number of bits to shift left.
   * @return @a this.
   */
  self_type &operator<<=(unsigned n);

  /** Right shift.
   *
   * @param n Number of bits to shift right.
   * @return @a this.
   */
  self_type &operator>>=(unsigned n);

  /** Bitwise AND.
   *
   * @param that Source address.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise AND of the corresponding bits in @a this and @a that.
   */
  self_type &operator&=(self_type const &that);

  /** Bitwise OR.
   *
   * @param that Source address.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise OR of the corresponding bits in @a this and @a that.
   */
  self_type &operator|=(self_type const &that);

  /// Increment address.
  self_type &operator++();

  /// Decrement address.
  self_type &operator--();

  /// Assign from IPv6 raw address.
  self_type &operator=(in6_addr const &addr);

  /// Set to the address in @a addr.
  self_type &operator=(sockaddr_in6 const *addr);

  /** Access a byte in the address.
   *
   * @param idx Byte index.
   * @return The "text order" byte.
   */
  constexpr uint8_t operator [] (int idx) const;

  /** Update socket address with this address.
   *
   * @param sin IPv6 socket address.
   * @return @sin
   */
  sockaddr_in6 *copy_to(sockaddr_in6 *sin6) const;

  /** Update socket address with this address.
   *
   * @param sa Socket address.
   * @return @sin
   *
   * @a sa is assumed to be large enough to hold an IPv6 address.
   */
  sockaddr *copy_to(sockaddr *sa) const;

  /// Return the address in host order.
  in6_addr host_order() const;

  /** Copy the address in host order.
   *
   * @param dst Destination for host order address.
   * @return @a dst
   */
  in6_addr& host_order(in6_addr & dst) const;

  /// Return the address in network order.
  in6_addr network_order() const;

  /** Copy the address in network order.
   *
   * @param dst Destination for network order address.
   * @return @a dst
   */
  in6_addr &network_order(in6_addr &dst) const;

  /** Parse a string for an IP address.

      The address resuling from the parse is copied to this object if the conversion is successful,
      otherwise this object is invalidated.

      @return @c true on success, @c false otherwise.
  */
  bool load(string_view const &str);

  /// Generic three value compare.
  int cmp(self_type const &that) const;

  /// @return The address family.
  constexpr sa_family_t family() const;

  /// @return @c true if this is the "any" address, @c false if not.
  bool is_any() const;

  /// @return @c true if this is a loopback address, @c false if not.
  bool is_loopback() const;

  /// @return @c true if this is a multicast address, @c false if not.
  bool is_multicast() const;

  /// @return @c true if this is a link local address, @c false if not.
  bool is_link_local() const;

  /// @return @c true if the address is private.
  bool is_private() const;

  ///  @return @c true if this is an IPv4 addressed mapped to IPv6, @c false if not.
  bool is_mapped_ip4() const;

  /** Reset to default constructed state.
   *
   * @return @a this
   */
  self_type & clear();

  /** Bitwise AND.
   *
   * @param that Source mask.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise AND of the corresponding bits in @a this and @a that.
   */
  self_type &operator&=(IPMask const &that);

  /** Bitwise OR.
   *
   * @param that Source mask.
   * @return @a this.
   *
   * The bits in @a this are set to the bitwise OR of the corresponding bits in @a this and @a that.
   */
  self_type &operator|=(IPMask const &that);

  /** Convert between network and host ordering.
   *
   * @param dst Destination for re-ordered address.
   * @param src Original address.
   */
  static void reorder(in6_addr &dst, raw_type const &src);

  /** Convert between network and host ordering.
   *
   * @param dst Destination for re-ordered address.
   * @param src Original address.
   */
  static void reorder(raw_type &dst, in6_addr const &src);

  template < typename T > auto as_span() -> std::enable_if_t<swoc::meta::is_any_of_v<T, std::byte, uint8_t, uint16_t, uint32_t, uint64_t>, swoc::MemSpan<T>> {
    return swoc::MemSpan(_addr._store).template rebind<T>();
  }

  template < typename T > auto as_span() const -> std::enable_if_t<swoc::meta::is_any_of_v<typename std::remove_const_t<T>, std::byte, uint8_t, uint16_t, uint32_t, uint64_t>, swoc::MemSpan<T const>> {
    return swoc::MemSpan<uint64_t const>(_addr._store).template rebind<T const>();
  }

protected:
  friend bool operator==(self_type const &, self_type const &);

  friend bool operator!=(self_type const &, self_type const &);

  friend bool operator<(self_type const &, self_type const &);

  friend bool operator<=(self_type const &, self_type const &);

  /// Direct access type for the address by quads (16 bits).
  /// This corresponds to the elements of the text format of the address.
  using quad_store_type = std::array<quad_type, N_QUADS>;

  /// A bit mask of all 1 bits the size of a quad.
  static constexpr quad_type QUAD_MASK = ~quad_type{0};

  /// Type used as a "word", the natural working unit of the address.
  using word_type = uint64_t;

  static constexpr size_t WORD_SIZE = sizeof(word_type);

  /// Number of bits per word.
  static constexpr size_t WORD_WIDTH = std::numeric_limits<uint8_t>::digits * WORD_SIZE;

  /// Number of words used for basic address storage.
  static constexpr size_t N_STORE = SIZE / WORD_SIZE;

  /// Type used to store the address.
  using word_store_type = std::array<word_type, N_STORE>;

  /// Type for digging around inside the address, with the various forms of access.
  /// These are in sort of host order - @a _store elements are host order, but the
  /// MSW and LSW are swapped (big-endian). This makes various bits of the implementation
  /// easier. Conversion to and from network order is via the @c reorder method.
  union Addr {
    word_store_type _store = {0}; ///< 0 is MSW, 1 is LSW.
    quad_store_type _quad;        ///< By quad.
    raw_type _raw;                ///< By byte.
    in6_addr _in6;                ///< By networking type (but in host order!)
  } _addr;
  static_assert(sizeof(in6_addr) == sizeof(raw_type));

  static constexpr unsigned LSW = 1; ///< Least significant word index.
  static constexpr unsigned MSW = 0; ///< Most significant word index.

  /// Index of quads in @a _addr._quad.
  /// This converts from the position in the text format to the quads in the binary format.
  static constexpr std::array<unsigned, N_QUADS> QUAD_IDX = {3, 2, 1, 0, 7, 6, 5, 4};

  /// Index of bytes in @a _addr._raw
  /// This converts MSB (0) to LSB (15) indices to the bytes in the binary format.
  static constexpr std::array<unsigned, SIZE> RAW_IDX = { 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8 };

  /// Convert between network and host order.
  /// The conversion is symmetric.
  /// @param dst Output where reordered value is placed.
  /// @param src Input value to resorder.
  static void reorder(unsigned char dst[WORD_SIZE], unsigned char const src[WORD_SIZE]);

  /** Construct from two 64 bit values.
   *
   * @param msw The most significant 64 bits, host order.
   * @param lsw The least significant 64 bits, host order.
   */
  IP6Addr(word_store_type::value_type msw, word_store_type::value_type lsw) : _addr{{msw, lsw}} {}

  friend IP6Addr operator&(IP6Addr const &addr, IPMask const &mask);

  friend IP6Addr operator|(IP6Addr const &addr, IPMask const &mask);
};

/** An IPv4 or IPv6 address.
 *
 * The family type is stored. For comparisons, invalid < IPv4 < IPv6. All invalid instances are equal.
 */
class IPAddr {
  friend class IPRange;

  using self_type = IPAddr; ///< Self reference type.
public:
  IPAddr()                      = default; ///< Default constructor - invalid result.
  IPAddr(self_type const &that) = default; ///< Copy constructor.
  self_type & operator = (self_type const& that) = default; ///< Copy assignment.

  /// Construct using IPv4 @a addr.
  explicit IPAddr(in_addr_t addr);

  /// Construct using an IPv4 @a addr
  IPAddr(IP4Addr const &addr) : _addr{addr}, _family(IP4Addr::AF_value) {}

  /// Construct using IPv6 @a addr.
  explicit IPAddr(in6_addr const &addr);

  /// construct using an IPv6 @a addr
  IPAddr(IP6Addr const &addr) : _addr{addr}, _family(IP6Addr::AF_value) {}

  /// Construct from @c sockaddr.
  explicit IPAddr(sockaddr const *addr);

  /// Construct from @c IPEndpoint.
  explicit IPAddr(IPEndpoint const &addr);

  /// Construct from text representation.
  /// If the @a text is invalid the result is an invalid instance.
  explicit IPAddr(string_view const &text);

  /// Set to the address in @a addr.
  self_type &assign(sockaddr const *addr);

  /// Set to the address in @a addr.
  self_type &assign(sockaddr_in const *addr);

  /// Set to the address in @a addr.
  self_type &assign(sockaddr_in6 const *addr);

  /// Set to IPv4 @a addr.
  self_type &assign(in_addr_t addr);

  /// Set to IPv6 @a addr
  self_type &assign(in6_addr const &addr);

  /// Assign from end point.
  self_type &operator=(IPEndpoint const &ip);

  /// Assign from IPv4 raw address.
  self_type &operator=(in_addr_t ip);

  /// Assign from IPv6 raw address.
  self_type &operator=(in6_addr const &addr);

  bool operator==(self_type const &that) const;

  bool operator!=(self_type const &that) const;

  bool operator<(self_type const &that) const;

  bool operator>(self_type const &that) const;

  bool operator<=(self_type const &that) const;

  bool operator>=(self_type const &that) const;

  /// Assign from @c sockaddr
  self_type &operator=(sockaddr const *addr);

  self_type &operator&=(IPMask const &mask);

  self_type &operator|=(IPMask const &mask);

  /** Copy the address to a socket address.
   *
   * @param sa Destination.
   * @return @a sa
   */
  sockaddr * copy_to(sockaddr * sa);

  /** Parse a string and load the result in @a this.
   *
   * @param text Text to parse.
   * @return  @c true on success, @c false otherwise.
   */
  bool load(string_view const &text);

  /// Generic compare.
  int cmp(self_type const &that) const;

  /// Test for same address family.
  /// @c return @c true if @a that is the same address family as @a this.
  bool is_same_family(self_type const &that);

  /// Get the address family.
  /// @return The address family.
  sa_family_t family() const;

  /// Test for IPv4.
  bool is_ip4() const;

  /// Test for IPv6.
  bool is_ip6() const;

  /// @return As IPv4 address - results are undefined if it is not actually IPv4.
  IP4Addr const &ip4() const;

  /// @return As IPv4 address - results are undefined if it is not actually IPv4.
  explicit operator IP4Addr() const;

  /// @return As IPv6 address - results are undefined if it is not actually IPv6.
  IP6Addr const &ip6() const;

  /// @return As IPv6 address - results are undefined if it is not actually IPv6.
  explicit operator IP6Addr() const;

  /// Test for validity.
  bool is_valid() const;

  /// Make invalid.
  self_type &invalidate();

  /// Test for loopback
  bool is_loopback() const;

  /// Test for multicast
  bool is_multicast() const;

  /// @return @c true if this is a link local address, @c false if not.
  bool is_link_local() const;

  /// @return @c true if this is a private address, @c false if not.
  bool is_private() const;

  ///< Pre-constructed invalid instance.
  static self_type const INVALID;

protected:
  friend IP4Addr;
  friend IP6Addr;

  /// Address data.
  union raw_addr_type {
    IP4Addr _ip4;                                    ///< IPv4 address (host)
    IP6Addr _ip6;                                    ///< IPv6 address (host)

    constexpr raw_addr_type();

    raw_addr_type(in_addr_t addr) : _ip4(addr) {}

    raw_addr_type(in6_addr const &addr) : _ip6(addr) {}

    raw_addr_type(IP4Addr const &addr) : _ip4(addr) {}

    raw_addr_type(IP6Addr const &addr) : _ip6(addr) {}
  } _addr;

  sa_family_t _family{AF_UNSPEC}; ///< Protocol family.
};

/** An IP address mask.
 *
 * This is essentially a width for a bit mask.
 */
class IPMask {
  using self_type = IPMask; ///< Self reference type.

  friend class IP4Addr;
  friend class IP6Addr;

public:
  using raw_type = uint8_t; ///< Storage for mask width.

  IPMask() = default; ///< Default construct to invalid mask.

  /** Construct a mask of @a width.
   *
   * @param width Number of bits in the mask.
   *
   * @note Because this is a network mask, it is always left justified.
   */
  explicit IPMask(raw_type width);

  /// @return @c true if the mask is valid, @c false if not.
  bool is_valid() const;

  /** Parse mask from @a text.
   *
   * @param text A number in string format.
   * @return @a true if a valid CIDR value, @c false if not.
   */
  bool load(string_view const &text);

  /** Copmute a mask for the network at @a addr.
   * @param addr Lower bound of network.
   * @return A mask with the width of the largest network starting at @a addr.
   */
  static self_type mask_for(IPAddr const &addr);

  /** Copmute a mask for the network at @a addr.
   * @param addr Lower bound of network.
   * @return A mask with the width of the largest network starting at @a addr.
   */
  static self_type mask_for(IP4Addr const &addr);

  /** Copmute a mask for the network at @a addr.
   * @param addr Lower bound of network.
   * @return A mask with the width of the largest network starting at @a addr.
   */
  static self_type mask_for(IP6Addr const &addr);

  /// Change to default constructed state (invalid).
  self_type & clear();

  /// The width of the mask.
  raw_type width() const;

  /** Extend the mask (cover more addresses).
   *
   * @param n Number of bits to extend.
   * @return @a this
   *
   * Effectively shifts the mask left, bringing in 0 bits on the right.
   */
  self_type & operator<<=(raw_type n);

  /** Narrow the mask (cover fewer addresses).
   *
   * @param n Number of bits to narrow.
   * @return @a this
   *
   * Effectively shift the mask right, bringing in 1 bits on the left.
   */
  self_type & operator>>=(raw_type n);

  /** The mask as an IPv4 address.
   *
   * @return An IPv4 address that is the mask.
   *
   * If the mask is wider than an IPv4 address, the maximum mask is returned.
   */
  IP4Addr as_ip4() const;

  /** The mask as an IPv6 address.
   *
   * @return An IPv6 address that is the mask.
   *
   * If the mask is wider than an IPv6 address, the maximum mask is returned.
   */
  IP6Addr as_ip6() const;

protected:
  /// Marker value for an invalid mask.
  static constexpr auto INVALID = std::numeric_limits<raw_type>::max();

  raw_type _cidr = INVALID; ///< Mask width in bits.

  /// Compute a partial IPv6 mask, sized for the basic storage type.
  static raw_type mask_for_quad(IP6Addr::quad_type q);
};

// --- Implementation

inline constexpr IP4Addr::IP4Addr(in_addr_t addr) : _addr(addr) {}

inline IP4Addr::IP4Addr(string_view const &text) {
  if (!this->load(text)) {
    _addr = INADDR_ANY;
  }
}

inline constexpr sa_family_t
IP4Addr::family() const {
  return AF_value;
}

inline IP4Addr &
IP4Addr::operator<<=(unsigned n) {
  _addr <<= n;
  return *this;
}

inline IP4Addr &
IP4Addr::operator>>=(unsigned n) {
  _addr >>= n;
  return *this;
}

inline IP4Addr &
IP4Addr::operator&=(self_type const &that) {
  _addr &= that._addr;
  return *this;
}

inline IP4Addr &
IP4Addr::operator|=(self_type const &that) {
  _addr |= that._addr;
  return *this;
}

inline IP4Addr &
IP4Addr::operator++() {
  ++_addr;
  return *this;
}

inline IP4Addr &
IP4Addr::operator--() {
  --_addr;
  return *this;
}

inline in_addr_t
IP4Addr::network_order() const {
  return htonl(_addr);
}

inline in_addr_t
IP4Addr::host_order() const {
  return _addr;
}

inline auto
IP4Addr::operator=(in_addr_t ip) -> self_type & {
  _addr = ntohl(ip);
  return *this;
}

inline sockaddr *
IP4Addr::copy_to(sockaddr *sa) const {
  this->copy_to(reinterpret_cast<sockaddr_in*>(sa));
  return sa;
}

/// Equality.
inline bool
operator==(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr == rhs._addr;
}

/// @return @c true if @a lhs is equal to @a rhs.
inline bool
operator!=(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr != rhs._addr;
}

/// @return @c true if @a lhs is less than @a rhs (host order).
inline bool
operator<(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr < rhs._addr;
}

/// @return @c true if @a lhs is less than or equal to@a rhs (host order).
inline bool
operator<=(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr <= rhs._addr;
}

/// @return @c true if @a lhs is greater than @a rhs (host order).
inline bool
operator>(IP4Addr const &lhs, IP4Addr const &rhs) {
  return rhs < lhs;
}

/// @return @c true if @a lhs is greater than or equal to @a rhs (host order).
inline bool
operator>=(IP4Addr const &lhs, IP4Addr const &rhs) {
  return rhs <= lhs;
}

inline IP4Addr &
IP4Addr::operator&=(IPMask const &mask) {
  _addr &= mask.as_ip4()._addr;
  return *this;
}

inline IP4Addr &
IP4Addr::operator|=(IPMask const &mask) {
  _addr |= ~(mask.as_ip4()._addr);
  return *this;
}

inline bool
IP4Addr::is_any() const {
  return _addr == INADDR_ANY;
}

inline bool
IP4Addr::is_loopback() const {
  return (*this)[0] == IN_LOOPBACKNET;
}

inline bool
IP4Addr::is_multicast() const {
  return IN_MULTICAST(_addr);
}

inline bool
IP4Addr::is_link_local() const {
  return (_addr & 0xFFFF0000) == 0xA9FE0000; // 169.254.0.0/16
}

inline bool IP4Addr::is_private() const {
  return (((_addr & 0xFF000000) == 0x0A000000) ||        // 10.0.0.0/8
          ((_addr & 0xFFC00000) == 0x64400000) ||        // 100.64.0.0/10
          ((_addr & 0xFFF00000) == 0xAC100000) || // 172.16.0.0/12
          ((_addr & 0xFFFF0000) == 0xC0A80000)           // 192.168.0.0/16
  );
}

inline uint8_t
IP4Addr::operator[](unsigned int idx) const {
  return reinterpret_cast<bytes const &>(_addr)[3 - idx];
}

inline int
IP4Addr::cmp(IP4Addr::self_type const &that) const {
  return _addr < that._addr ? -1 : _addr > that._addr ? 1 : 0;
}

constexpr in_addr_t
IP4Addr::reorder(in_addr_t src) {
  return ((src & 0xFF) << 24) | (((src >> 8) & 0xFF) << 16) | (((src >> 16) & 0xFF) << 8) | ((src >> 24) & 0xFF);
}

// +++ IP6Addr +++

inline constexpr sa_family_t
IP6Addr::family() const {
  return AF_value;
}

inline IP6Addr::IP6Addr(in6_addr const &addr) {
  *this = addr;
}

inline IP6Addr::IP6Addr(string_view const &text) {
  if (!this->load(text)) {
    this->clear();
  }
}

inline IP6Addr::IP6Addr(IP4Addr addr) {
  _addr._store[MSW] = 0;
  _addr._quad[QUAD_IDX[4]] = 0;
  _addr._quad[QUAD_IDX[5]] = 0xffff;
  _addr._quad[QUAD_IDX[6]] = addr.host_order() >> QUAD_WIDTH;
  _addr._quad[QUAD_IDX[7]] = addr.host_order();
}

inline bool
IP6Addr::is_loopback() const {
  return _addr._store[MSW] == 0 && _addr._store[LSW] == 1;
}

inline bool
IP6Addr::is_multicast() const {
  return _addr._raw[RAW_IDX[0]] == 0xFF;
}

inline bool
IP6Addr::is_any() const {
  return _addr._store[MSW] == 0 && _addr._store[LSW] == 0;
}

inline bool
IP6Addr::is_mapped_ip4() const {
  return 0 == _addr._store[MSW] && (_addr._quad[QUAD_IDX[4]] == 0 && _addr._quad[QUAD_IDX[5]] == 0xFFFF);
}

inline bool
IP6Addr::is_link_local() const {
  return _addr._raw[RAW_IDX[0]] == 0xFE && (_addr._raw[RAW_IDX[1]] & 0xC0) == 0x80; // fe80::/10
}

inline bool IP6Addr::is_private() const {
  return (_addr._raw[RAW_IDX[0]]& 0xFE) == 0xFC; // fc00::/7
}

inline in6_addr
IP6Addr::host_order() const {
  Addr zret { {_addr._store[LSW], _addr._store[MSW] }};
  return zret._in6;
}

inline in6_addr &
IP6Addr::host_order(in6_addr & dst) const {
  Addr * addr = reinterpret_cast<Addr*>(&dst);
  addr->_store[0] = _addr._store[LSW];
  addr->_store[1] = _addr._store[MSW];
  return dst;
}

inline in6_addr
IP6Addr::network_order() const {
  in6_addr zret;
  return this->network_order(zret);
}

inline in6_addr &
IP6Addr::network_order(in6_addr & dst) const {
  self_type::reorder(dst, _addr._raw);
  return dst;
}

inline auto
IP6Addr::clear() -> self_type & {
  _addr._store[MSW] = _addr._store[LSW] = 0;
  return *this;
}

inline auto
IP6Addr::operator=(in6_addr const &addr) -> self_type & {
  self_type::reorder(_addr._raw, addr);
  return *this;
}

inline auto
IP6Addr::operator=(sockaddr_in6 const *addr) -> self_type & {
  if (addr) {
    *this = addr->sin6_addr;
  } else {
    this->clear();
  }
  return *this;
}

inline IP6Addr &
IP6Addr::operator++() {
  if (++(_addr._store[LSW]) == 0) {
    ++(_addr._store[MSW]);
  }
  return *this;
}

inline IP6Addr &
IP6Addr::operator--() {
  if (--(_addr._store[LSW]) == ~static_cast<uint64_t>(0)) {
    --(_addr._store[MSW]);
  }
  return *this;
}

inline void
IP6Addr::reorder(unsigned char dst[WORD_SIZE], unsigned char const src[WORD_SIZE]) {
  for (size_t idx = 0; idx < WORD_SIZE; ++idx) {
    dst[idx] = src[WORD_SIZE - (idx + 1)];
  }
}

/// @return @c true if @a lhs is equal to @a rhs.
inline bool
operator==(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[IP6Addr::MSW] == rhs._addr._store[IP6Addr::MSW] && lhs._addr._store[IP6Addr::LSW] == rhs._addr._store[IP6Addr::LSW];
}

/// @return @c true if @a lhs is not equal to @a rhs.
inline bool
operator!=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[IP6Addr::MSW] != rhs._addr._store[IP6Addr::MSW] || lhs._addr._store[IP6Addr::LSW] != rhs._addr._store[IP6Addr::LSW];
}

/// @return @c true if @a lhs is less than @a rhs.
inline bool
operator<(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[IP6Addr::MSW] < rhs._addr._store[IP6Addr::MSW] ||
  (lhs._addr._store[IP6Addr::MSW] == rhs._addr._store[IP6Addr::MSW] && lhs._addr._store[IP6Addr::LSW] < rhs._addr._store[IP6Addr::LSW]);
}

/// @return @c true if @a lhs is greater than @a rhs.
inline bool
operator>(IP6Addr const &lhs, IP6Addr const &rhs) {
  return rhs < lhs;
}

/// @return @c true if @a lhs is less than or equal to @a rhs.
inline bool
operator<=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[IP6Addr::MSW] < rhs._addr._store[IP6Addr::MSW] ||
  (lhs._addr._store[IP6Addr::MSW] == rhs._addr._store[IP6Addr::MSW] && lhs._addr._store[IP6Addr::LSW] <= rhs._addr._store[IP6Addr::LSW]);
}

/// @return @c true if @a lhs is greater than or equal to @a rhs.
inline bool
operator>=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return rhs <= lhs;
}

inline sockaddr *
IP6Addr::copy_to(sockaddr *sa) const {
  this->copy_to(reinterpret_cast<sockaddr_in6*>(sa));
  return sa;
}

inline IP6Addr &
IP6Addr::operator&=(IPMask const &mask) {
  if (mask._cidr < WORD_WIDTH) {
    _addr._store[MSW] &= (~word_type{0} << (WORD_WIDTH - mask._cidr));
    _addr._store[LSW] = 0;
  } else if (mask._cidr < WIDTH) {
    _addr._store[LSW] &= (~word_type{0} << (2 * WORD_WIDTH - mask._cidr));
  }
  return *this;
}

inline IP6Addr &
IP6Addr::operator|=(IPMask const &mask) {
  if (mask._cidr < WORD_WIDTH) {
    _addr._store[MSW] |= (~word_type{0} >> mask._cidr);
    _addr._store[LSW] = ~word_type{0};
  } else if (mask._cidr < WIDTH) {
    _addr._store[LSW] |= (~word_type{0} >> (mask._cidr - WORD_WIDTH));
  }
  return *this;
}

// +++ IPMask +++

inline IPMask::IPMask(raw_type width) : _cidr(width) {}

inline bool
IPMask::is_valid() const {
  return _cidr < INVALID;
}

inline auto
IPMask::width() const -> raw_type {
  return _cidr;
}

inline bool
operator==(IPMask const &lhs, IPMask const &rhs) {
  return lhs.width() == rhs.width();
}

inline bool
operator!=(IPMask const &lhs, IPMask const &rhs) {
  return lhs.width() != rhs.width();
}

inline bool
operator<(IPMask const &lhs, IPMask const &rhs) {
  return lhs.width() < rhs.width();
}

inline IP4Addr
IPMask::as_ip4() const {
  static constexpr auto MASK = ~in_addr_t{0};
  in_addr_t addr             = MASK;
  if (_cidr < IP4Addr::WIDTH) {
    addr <<= IP4Addr::WIDTH - _cidr;
  }
  return IP4Addr{addr};
}

inline auto
IPMask::clear() ->  self_type & {
  _cidr = INVALID;
  return *this;
}

inline auto
IPMask::operator<<=(raw_type n) -> self_type & {
  _cidr -= n;
  return *this;
}

inline auto
IPMask::operator>>=(IPMask::raw_type n) -> self_type & {
  _cidr += n;
  return *this;
}

// +++ mixed mask operators +++

inline IP4Addr
operator&(IP4Addr const &addr, IPMask const &mask) {
  return IP4Addr{addr} &= mask;
}

inline IP4Addr
operator|(IP4Addr const &addr, IPMask const &mask) {
  return IP4Addr{addr} |= mask;
}

inline IP6Addr
operator&(IP6Addr const &addr, IPMask const &mask) {
  return IP6Addr{addr} &= mask;
}

inline IP6Addr
operator|(IP6Addr const &addr, IPMask const &mask) {
  return IP6Addr{addr} |= mask;
}

constexpr uint8_t
IP6Addr::operator[](int idx) const {
  return _addr._raw[RAW_IDX[idx]];
}

inline IPAddr
operator&(IPAddr const &addr, IPMask const &mask) {
  return IPAddr{addr} &= mask;
}

inline IPAddr
operator|(IPAddr const &addr, IPMask const &mask) {
  return IPAddr{addr} |= mask;
}

// @c constexpr constructor is required to initialize _something_, it can't be completely uninitializing.
inline constexpr IPAddr::raw_addr_type::raw_addr_type() : _ip4(INADDR_ANY) {}

inline IPAddr::IPAddr(in_addr_t addr) : _addr(addr), _family(IP4Addr::AF_value) {}

inline IPAddr::IPAddr(in6_addr const &addr) : _addr(addr), _family(IP6Addr::AF_value) {}

inline IPAddr::IPAddr(sockaddr const *addr) {
  this->assign(addr);
}

inline IPAddr::IPAddr(string_view const &text) {
  this->load(text);
}

inline IPAddr &
IPAddr::operator=(in_addr_t addr) {
  _family    = AF_INET;
  _addr._ip4 = addr;
  return *this;
}

inline IPAddr &
IPAddr::operator=(in6_addr const &addr) {
  _family    = AF_INET6;
  _addr._ip6 = addr;
  return *this;
}

inline IPAddr &
IPAddr::operator=(sockaddr const *addr) {
  return this->assign(addr);
}

inline sa_family_t
IPAddr::family() const {
  return _family;
}

inline bool
IPAddr::is_ip4() const {
  return AF_INET == _family;
}

inline bool
IPAddr::is_ip6() const {
  return AF_INET6 == _family;
}

inline bool
IPAddr::is_same_family(self_type const &that) {
  return this->is_valid() && _family == that._family;
}

inline bool
IPAddr::is_loopback() const {
  return (AF_INET == _family && _addr._ip4.is_loopback()) || (AF_INET6 == _family && _addr._ip6.is_loopback());
}

inline bool
IPAddr::is_link_local() const {
  return this->is_ip4() ? this->ip4().is_link_local()
         : this->is_ip6() ? this->ip6().is_link_local()
                          : false;
}

inline bool
IPAddr::is_private() const {
  return this->is_ip4() ? this->ip4().is_private()
         : this->is_ip6() ? this->ip6().is_private()
                          : false;
}

inline IPAddr &
IPAddr::assign(in_addr_t addr) {
  _family    = AF_INET;
  _addr._ip4 = addr;
  return *this;
}

inline IPAddr &
IPAddr::assign(in6_addr const &addr) {
  _family    = AF_INET6;
  _addr._ip6 = addr;
  return *this;
}

inline IPAddr &
IPAddr::assign(sockaddr_in const *addr) {
  if (addr) {
    _family    = AF_INET;
    _addr._ip4 = addr;
  } else {
    _family = AF_UNSPEC;
  }
  return *this;
}

inline IPAddr &
IPAddr::assign(sockaddr_in6 const *addr) {
  if (addr) {
    _family    = AF_INET6;
    _addr._ip6 = addr->sin6_addr;
  } else {
    _family = AF_UNSPEC;
  }
  return *this;
}

inline bool
IPAddr::is_valid() const {
  return _family == AF_INET || _family == AF_INET6;
}

inline IPAddr &
IPAddr::invalidate() {
  _family = AF_UNSPEC;
  return *this;
}

// Associated operators.

/// Equality.
bool operator==(IPAddr const &lhs, sockaddr const *rhs);

/// Equality.
inline bool
operator==(sockaddr const *lhs, IPAddr const &rhs) {
  return rhs == lhs;
}

/// Inequality.
inline bool
operator!=(IPAddr const &lhs, sockaddr const *rhs) {
  return !(lhs == rhs);
}

/// Inequality.
inline bool
operator!=(sockaddr const *lhs, IPAddr const &rhs) {
  return !(rhs == lhs);
}

inline IP4Addr const & IPAddr::ip4() const { return _addr._ip4; }
inline IPAddr::operator IP4Addr() const { return _addr._ip4; }

inline IP6Addr const & IPAddr::ip6() const { return _addr._ip6; }
inline IPAddr::operator IP6Addr() const { return _addr._ip6; }

inline bool
IPAddr::operator==(self_type const &that) const {
  switch (_family) {
  case AF_INET:
    return that._family == AF_INET && _addr._ip4 == that._addr._ip4;
    case AF_INET6:
      return that._family == AF_INET6 && _addr._ip6 == that._addr._ip6;
      default:
        return ! that.is_valid();
  }
}

inline bool
IPAddr::operator!=(self_type const &that) const {
  return !(*this == that);
}

inline bool
IPAddr::operator>(self_type const &that) const {
  return that < *this;
}

inline bool
IPAddr::operator<=(self_type const &that) const {
  return !(that < *this);
}

inline bool
IPAddr::operator>=(self_type const &that) const {
  return !(*this < that);
}

// Disambiguating between comparisons and implicit conversions.

inline bool
operator==(IPAddr const &lhs, IP4Addr const &rhs) {
  return lhs.is_ip4() && lhs.ip4() == rhs;
}

inline bool
operator!=(IPAddr const &lhs, IP4Addr const &rhs) {
  return !lhs.is_ip4() || lhs.ip4() != rhs;
}

inline bool
operator==(IP4Addr const &lhs, IPAddr const &rhs) {
  return rhs.is_ip4() && lhs == rhs.ip4();
}

inline bool
operator!=(IP4Addr const &lhs, IPAddr const &rhs) {
  return !rhs.is_ip4() || lhs != rhs.ip4();
}

inline bool
operator==(IPAddr const &lhs, IP6Addr const &rhs) {
  return lhs.is_ip6() && lhs.ip6() == rhs;
}

inline bool
operator!=(IPAddr const &lhs, IP6Addr const &rhs) {
  return !lhs.is_ip6() || lhs.ip6() != rhs;
}

inline bool
operator==(IP6Addr const &lhs, IPAddr const &rhs) {
  return rhs.is_ip6() && lhs == rhs.ip6();
}

inline bool
operator!=(IP6Addr const &lhs, IPAddr const &rhs) {
  return !rhs.is_ip6() || lhs != rhs.ip6();
}
}} // namespace swoc::SWOC_VERSION_NS

namespace std {

/// Standard hash support for @a IP4Addr.
template <> struct hash<swoc::IP4Addr> {
  size_t operator()(swoc::IP4Addr const &addr) const {
    return addr.network_order();
  }
};

/// Standard hash support for @a IP6Addr.
template <> struct hash<swoc::IP6Addr> {
  size_t operator()(swoc::IP6Addr const &addr) const {
    // XOR the 64 chunks then XOR that down to 32 bits.
    auto words = addr.as_span<uint64_t>();
    return words[0] ^ words[1];
  }
};

/// Standard hash support for @a IPAddr.
template <> struct hash<swoc::IPAddr> {
  size_t operator()(swoc::IPAddr const &addr) const {
    return addr.is_ip4() ? hash<swoc::IP4Addr>()(addr.ip4()) : addr.is_ip6() ? hash<swoc::IP6Addr>()(addr.ip6()) : 0;
  }
};

} // namespace std
