// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   IP address and network related classes.
 */

#pragma once
#include <limits.h>
#include <netinet/in.h>
#include <string_view>
#include <variant>

#include "swoc/swoc_version.h"
#include "swoc/TextView.h"
#include "swoc/DiscreteRange.h"
#include "swoc/RBTree.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

class IP4Addr;

class IP6Addr;

class IPAddr;

class IPMask;

class IP4Range;

class IP6Range;

class IPRange;

class IP4Net;

class IP6Net;

class IPNet;

using ::std::string_view;
extern void *const pseudo_nullptr;

/** A union to hold @c sockaddr compliant IP address structures.

    This class contains a number of static methods to perform operations on external @c sockaddr
    instances. These are all duplicates of methods that operate on the internal @c sockaddr and
    are provided primarily for backwards compatibility during the shift to using this class.

    We use the term "endpoint" because these contain more than just the raw address, all of the data
    for an IP endpoint is present.
 */
union IPEndpoint {
  using self_type = IPEndpoint; ///< Self reference type.

  struct sockaddr sa;      ///< Generic address.
  struct sockaddr_in sa4;  ///< IPv4
  struct sockaddr_in6 sa6; ///< IPv6

  /// Default construct invalid instance.
  IPEndpoint();

  IPEndpoint(self_type const &that);

  /// Construct from the @a text representation of an address.
  IPEndpoint(string_view const &text);

  // Construct from @a IPAddr
  explicit IPEndpoint(IPAddr const &addr);

  // Construct from @c sockaddr
  IPEndpoint(sockaddr const *sa);

  /** Break a string in to IP address relevant tokens.
   *
   * @param src Source text. [in]
   * @param host The host / address. [out]
   * @param port The port. [out]
   * @param rest Any text past the end of the IP address. [out]
   * @return @c true if an IP address was found, @c false otherwise.
   *
   * Any of the out parameters can be @c nullptr in which case they are not updated.
   * This parses and discards the IPv6 brackets.
   */
  static bool tokenize(string_view src, string_view *host = nullptr, string_view *port = nullptr, string_view *rest = nullptr);

  /** Parse a string for an IP address.

      The address resulting from the parse is copied to this object if the conversion is successful,
      otherwise this object is invalidated.

      @return @c true on success, @c false otherwise.
  */
  bool parse(string_view const &str);

  /// Invalidate a @c sockaddr.
  static void invalidate(sockaddr *addr);

  /// Invalidate this endpoint.
  self_type &invalidate();

  /// Copy constructor.
  self_type &operator=(self_type const &that);

  /** Copy (assign) the contents of @a src to @a dst.
   *
   * The caller must ensure @a dst is large enough to hold the contents of @a src, the size of which
   * can vary depending on the type of address in @a dst.
   *
   * @param dst Destination.
   * @param src Source.
   * @return @c true if @a dst is a valid IP address, @c false otherwise.
   */
  static bool assign(sockaddr *dst, sockaddr const *src);

  /** Assign from a socket address.
      The entire address (all parts) are copied if the @a ip is valid.
  */
  self_type &assign(sockaddr const *addr);

  /// Assign from an @a addr and @a port.
  self_type &assign(IPAddr const &addr, in_port_t port = 0);

  /// Copy to @a sa.
  const self_type &fill(sockaddr *addr) const;

  /// Test for valid IP address.
  bool is_valid() const;

  /// Test for IPv4.
  bool is_ip4() const;

  /// Test for IPv6.
  bool is_ip6() const;

  /** Effectively size of the address.
   *
   * @return The size of the structure appropriate for the address family of the stored address.
   */
  socklen_t size() const;

  /// @return The IP address family.
  sa_family_t family() const;

  /// Set to be the ANY address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self_type &set_to_any(int family);

  /// @return @c true if this is the ANY address, @c false if not.
  bool is_any() const;

  /// Set to be loopback address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self_type &set_to_loopback(int family);

  /// @return @c true if this is a loopback address, @c false if not.
  bool is_loopback() const;

  /// Port in network order.
  in_port_t &network_order_port();

  /// Port in network order.
  in_port_t network_order_port() const;

  /// Port in host horder.
  in_port_t host_order_port() const;

  /// Port in network order from @a sockaddr.
  static in_port_t &port(sockaddr *sa);

  /// Port in network order from @a sockaddr.
  static in_port_t port(sockaddr const *sa);

  /// Port in host order directly from a @c sockaddr
  static in_port_t host_order_port(sockaddr const *sa);

  /// Automatic conversion to @c sockaddr.
  operator sockaddr *() { return &sa; }

  /// Automatic conversion to @c sockaddr.
  operator sockaddr const *() const { return &sa; }

  /// The string name of the address family.
  static string_view family_name(sa_family_t family);
};

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

  constexpr IP4Addr() = default; ///< Default constructor - minimum address.

  /// Construct using IPv4 @a addr (in host order).
  /// @note Host order seems odd, but all of the standard network macro values such as @c INADDR_LOOPBACK
  /// are in host order.
  explicit constexpr IP4Addr(in_addr_t addr);

  /// Construct from @c sockaddr_in.
  explicit IP4Addr(sockaddr_in const *sa);

  /// Construct from text representation.
  /// If the @a text is invalid the result is an invalid instance.
  IP4Addr(string_view const &text);

  /// Construct from generic address @a addr.
  explicit IP4Addr(IPAddr const &addr);

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
   * @return The byte at @a idx in the address.
   */
  uint8_t
  operator[](unsigned idx) const {
    return reinterpret_cast<bytes const &>(_addr)[idx];
  }

  /// Apply @a mask to address, leaving the network portion.
  self_type &operator&=(IPMask const &mask);

  /// Apply @a mask to address, creating the broadcast address.
  self_type &operator|=(IPMask const &mask);

  /// Write this adddress and @a port to the sockaddr @a sa.
  sockaddr_in *fill(sockaddr_in *sa, in_port_t port = 0) const;

  /// @return The address in network order.
  in_addr_t network_order() const;

  /// @return The address in host order.
  in_addr_t host_order() const;

  /** Parse @a text as IPv4 address.
      The address resulting from the parse is copied to this object if the conversion is successful,
      otherwise this object is invalidated.

      @return @c true on success, @c false otherwise.
  */
  bool load(string_view const &text);

  /// Standard ternary compare.
  int
  cmp(self_type const &that) const {
    return _addr < that._addr ? -1 : _addr > that._addr ? 1 : 0;
  }

  /// Get the IP address family.
  /// @return @c AF_INET
  /// @note Useful primarily for template classes.
  constexpr sa_family_t family();

  /// Test for ANY address.
  bool
  is_any() const {
    return _addr == INADDR_ANY;
  }

  /// Test for multicast
  bool
  is_multicast() const {
    return IN_MULTICAST(_addr);
  }

  /// Test for loopback
  bool
  is_loopback() const {
    return (*this)[3] == IN_LOOPBACKNET;
  }

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
    This should be presumed to be in network order.
 */
class IP6Addr {
  using self_type = IP6Addr; ///< Self reference type.
  friend class IP6Range;

  friend class IPMask;

public:
  static constexpr size_t WIDTH         = 128;                                                ///< Number of bits in the address.
  static constexpr size_t SIZE          = WIDTH / std::numeric_limits<unsigned char>::digits; ///< Size of address in bytes.
  static constexpr sa_family_t AF_value = AF_INET6;                                           ///< Address family type.

  using quad_type                 = uint16_t;                 ///< Size of one segment of an IPv6 address.
  static constexpr size_t N_QUADS = SIZE / sizeof(quad_type); ///< # of quads in an IPv6 address.

  /// Direct access type for the address.
  /// Equivalent to the data type for data member @c s6_addr in @c in6_addr.
  using raw_type = std::array<unsigned char, SIZE>;

  /// Direct access type for the address by quads (16 bits).
  /// This corresponds to the elements of the text format of the address.
  using quad_store_type = std::array<quad_type, N_QUADS>;

  /// Number of bits per quad.
  static constexpr size_t QUAD_WIDTH = std::numeric_limits<unsigned char>::digits * sizeof(quad_type);

  /// A bit mask of all 1 bits the size of a quad.
  static constexpr quad_type QUAD_MASK = ~quad_type{0};

  /// Type used as a "word", the natural working unit of the address.
  using word_type = uint64_t;

  static constexpr size_t WORD_SIZE = sizeof(word_type);

  /// Number of bits per word.
  static constexpr size_t WORD_WIDTH = std::numeric_limits<unsigned char>::digits * WORD_SIZE;

  /// Number of words used for basic address storage.
  static constexpr size_t N_STORE = SIZE / sizeof(word_type);

  /// Type used to store the address.
  using word_store_type = std::array<word_type, N_STORE>;

  /// Minimum value of an address.
  static const self_type MIN;
  /// Maximum value of an address.
  static const self_type MAX;

  IP6Addr()                      = default; ///< Default constructor - 0 address.
  IP6Addr(self_type const &that) = default;

  /// Construct using IPv6 @a addr.
  explicit IP6Addr(in6_addr const &addr);

  /// Construct from @c sockaddr_in.
  explicit IP6Addr(sockaddr_in6 const *addr) { *this = addr; }

  /// Construct from text representation.
  /// If the @a text is invalid the result is an invalid instance.
  IP6Addr(string_view const &text);

  /// Construct from generic @a addr.
  explicit IP6Addr(IPAddr const &addr);

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
  self_type &operator=(in6_addr const &ip);

  /// Set to the address in @a addr.
  self_type &operator=(sockaddr_in6 const *addr);

  /// Write to @c sockaddr using network order and @a port.
  sockaddr *copy_to(sockaddr *sa, in_port_t port = 0) const;

  /// Copy address to @a addr in network order.
  in6_addr &copy_to(in6_addr &addr) const;

  /// Return the address in network order.
  in6_addr network_order() const;

  /** Parse a string for an IP address.

      The address resuling from the parse is copied to this object if the conversion is successful,
      otherwise this object is invalidated.

      @return @c true on success, @c false otherwise.
  */
  bool load(string_view const &str);

  /// Generic compare.
  int cmp(self_type const &that) const;

  /// Get the address family.
  /// @return The address family.
  /// @note Useful primarily for templates.
  constexpr sa_family_t family();

  /// Test for ANY address.
  bool
  is_any() const {
    return _addr._store[0] == 0 && _addr._store[1] == 0;
  }

  /// Test for loopback
  bool
  is_loopback() const {
    return _addr._store[0] == 0 && _addr._store[1] == 1;
  }

  /// Test for multicast
  bool
  is_multicast() const {
    return _addr._raw[7] == 0xFF;
  }

  self_type &
  clear() {
    _addr._store[0] = _addr._store[1] = 0;
    return *this;
  }

  self_type &operator&=(IPMask const &mask);

  self_type &operator|=(IPMask const &mask);

  static void reorder(in6_addr &dst, raw_type const &src);

  static void reorder(raw_type &dst, in6_addr const &src);

protected:
  friend bool operator==(self_type const &, self_type const &);

  friend bool operator!=(self_type const &, self_type const &);

  friend bool operator<(self_type const &, self_type const &);

  friend bool operator<=(self_type const &, self_type const &);

  /// Type for digging around inside the address, with the various forms of access.
  /// These are in sort of host order - @a _store elements are host order, but the
  /// MSW and LSW are swapped (big-endian). This makes various bits of the implementation
  /// easier. Conversion to and from network order is via the @c reorder method.
  union {
    word_store_type _store = {0}; ///< 0 is MSW, 1 is LSW.
    quad_store_type _quad;        ///< By quad.
    raw_type _raw;                ///< By byte.
  } _addr;

  static constexpr unsigned LSW = 1; ///< Least significant word index.
  static constexpr unsigned MSW = 0; ///< Most significant word index.

  /// Index of quads in @a _addr._quad.
  /// This converts from the position in the text format to the quads in the binary format.
  static constexpr std::array<unsigned, N_QUADS> QUAD_IDX = {3, 2, 1, 0, 7, 6, 5, 4};

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

/** Storage for an IP address.
 */
class IPAddr {
  friend class IPRange;

  using self_type = IPAddr; ///< Self reference type.
public:
  IPAddr()                      = default; ///< Default constructor - invalid result.
  IPAddr(self_type const &that) = default; ///< Copy constructor.

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

  /// Set to the address in @a addr.
  self_type &assign(in_addr_t addr);

  /// Set to address in @a addr.
  self_type &assign(in6_addr const &addr);

  /// Assign from end point.
  self_type &operator=(IPEndpoint const &ip);

  /// Assign from IPv4 raw address.
  self_type &operator=(in_addr_t ip);

  /// Assign from IPv6 raw address.
  self_type &operator=(in6_addr const &ip);

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
  bool isCompatibleWith(self_type const &that);

  /// Get the address family.
  /// @return The address family.
  sa_family_t family() const;

  /// Test for IPv4.
  bool is_ip4() const;

  /// Test for IPv6.
  bool is_ip6() const;

  IP4Addr const &ip4() const;

  IP6Addr const &ip6() const;

  explicit operator IP4Addr const &() const { return _addr._ip4; }

  explicit
  operator IP4Addr &() {
    return _addr._ip4;
  }

  explicit operator IP6Addr const &() const { return _addr._ip6; }

  explicit
  operator IP6Addr &() {
    return _addr._ip6;
  }

  /// Test for validity.
  bool is_valid() const;

  /// Make invalid.
  self_type &invalidate();

  /// Test for multicast
  bool is_multicast() const;

  /// Test for loopback
  bool is_loopback() const;

  ///< Pre-constructed invalid instance.
  static self_type const INVALID;

protected:
  friend IP4Addr;
  friend IP6Addr;

  /// Address data.
  union raw_addr_type {
    IP4Addr _ip4;                                    ///< IPv4 address (host)
    IP6Addr _ip6;                                    ///< IPv6 address (host)
    uint8_t _octet[IP6Addr::SIZE];                   ///< IPv4 octets.
    uint64_t _u64[IP6Addr::SIZE / sizeof(uint64_t)]; ///< As 64 bit chunks.

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

  IPMask() = default;

  explicit IPMask(raw_type count);

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
   * @return The width of the largest network starting at @a addr.
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

  /// Force @a this to an invalid state.
  self_type &
  clear() {
    _cidr = INVALID;
    return *this;
  }

  /// The width of the mask.
  raw_type width() const;

  self_type &
  operator<<=(raw_type n) {
    _cidr -= n;
    return *this;
  }

  self_type &
  operator>>=(raw_type n) {
    _cidr += n;
    return *this;
  }

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

/** An inclusive range of IPv4 addresses.
 */
class IP4Range : public DiscreteRange<IP4Addr> {
  using self_type   = IP4Range;
  using super_type  = DiscreteRange<IP4Addr>;
  using metric_type = IP4Addr;

public:
  /// Default constructor, invalid range.
  IP4Range() = default;

  /// Construct from an network expressed as @a addr and @a mask.
  IP4Range(IP4Addr const &addr, IPMask const &mask);

  /// Construct from super type.
  /// @internal Why do I have to do this, even though the super type constructors are inherited?
  IP4Range(super_type const &r) : super_type(r) {}

  /** Construct range from @a text.
   *
   * @param text Range text.
   * @see IP4Range::load
   *
   * This results in a zero address if @a text is not a valid string. If this should be checked,
   * use @c load.
   */
  IP4Range(string_view const &text);

  using super_type::super_type; ///< Import super class constructors.

  /** Set @a this range.
   *
   * @param addr Minimum address.
   * @param mask CIDR mask to compute maximum adddress from @a addr.
   * @return @a this
   */
  self_type &assign(IP4Addr const &addr, IPMask const &mask);

  using super_type::assign; ///< Import assign methods.

  /** Assign to this range from text.
   *
   * @param text Range text.
   *
   * The text must be in one of three formats.
   * - A dashed range, "addr1-addr2"
   * - A singleton, "addr". This is treated as if it were "addr-addr", a range of size 1.
   * - CIDR notation, "addr/cidr" where "cidr" is a number from 0 to the number of bits in the address.
   */
  bool load(string_view text);

  /** Compute the mask for @a this as a network.
   *
   * @return If @a this is a network, the mask for that network. Otherwise an invalid mask.
   *
   * @see IPMask::is_valid
   */
  IPMask network_mask() const;

  class NetSource;

  /** Generate a list of networks covering @a this range.
   *
   * @return A network generator.
   *
   * The returned object can be used as an iterator, or as a container to iterating over
   * the unique minimal set of networks that cover @a this range.
   *
   * @code
   * void (IP4Range const& range) {
   *   for ( auto const& net : range ) {
   *     net.addr(); // network address.
   *     net.mask(); // network mask;
   *   }
   * }
   * @endcode
   */
  NetSource networks() const;
};

/** Network generator class.
 * This generates networks from a range and acts as both a forward iterator and a container.
 */
class IP4Range::NetSource {
  using self_type = NetSource; ///< Self reference type.
public:
  using range_type = IP4Range; ///< Import base range type.

  /// Construct from @a range.
  explicit NetSource(range_type const &range);

  /// Copy constructor.
  NetSource(self_type const &that) = default;

  /// This class acts as a container and an iterator.
  using iterator = self_type;
  /// All iteration is constant so no distinction between iterators.
  using const_iterator = iterator;

  iterator begin() const; ///< First network.
  iterator end() const;   ///< Past last network.

  /// Return @c true if there are valid networks, @c false if not.
  bool empty() const;

  /// @return The current network.
  IP4Net operator*() const;

  /// Access @a this as if it were an @c IP4Net.
  self_type *operator->();

  /// Iterator support.
  /// @return The current network address.
  IP4Addr const &addr() const;

  /// Iterator support.
  /// @return The current network mask.
  IPMask mask() const;

  /// Move to next network.
  self_type &operator++();

  /// Move to next network.
  self_type operator++(int);

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality.
  bool operator!=(self_type const &that) const;

protected:
  IP4Range _range; ///< Remaining range.
  /// Mask for current network.
  IP4Addr _mask{~static_cast<in_addr_t>(0)};
  IPMask::raw_type _cidr = IP4Addr::WIDTH; ///< Current CIDR value.

  void search_wider();

  void search_narrower();

  bool is_valid(IP4Addr mask);
};

class IP6Range : public DiscreteRange<IP6Addr> {
  using self_type  = IP6Range;
  using super_type = DiscreteRange<IP6Addr>;

public:
  /// Construct from super type.
  /// @internal Why do I have to do this, even though the super type constructors are inherited?
  IP6Range(super_type const &r) : super_type(r) {}

  /** Construct range from @a text.
   *
   * @param text Range text.
   * @see IP4Range::load
   *
   * This results in a zero address if @a text is not a valid string. If this should be checked,
   * use @c load.
   */
  IP6Range(string_view const &text);

  using super_type::super_type; ///< Import super class constructors.

  /** Set @a this range.
   *
   * @param addr Minimum address.
   * @param mask CIDR mask to compute maximum adddress from @a addr.
   * @return @a this
   */
  self_type &assign(IP6Addr const &addr, IPMask const &mask);

  using super_type::assign; ///< Import assign methods.

  /** Assign to this range from text.
   *
   * @param text Range text.
   *
   * The text must be in one of three formats.
   * - A dashed range, "addr1-addr2"
   * - A singleton, "addr". This is treated as if it were "addr-addr", a range of size 1.
   * - CIDR notation, "addr/cidr" where "cidr" is a number from 0 to the number of bits in the address.
   */
  bool load(string_view text);

  /** Compute the mask for @a this as a network.
   *
   * @return If @a this is a network, the mask for that network. Otherwise an invalid mask.
   *
   * @see IPMask::is_valid
   */
  IPMask network_mask() const;

  class NetSource;

  /** Generate a list of networks covering @a this range.
   *
   * @return A network generator.
   *
   * The returned object can be used as an iterator, or as a container to iterating over
   * the unique minimal set of networks that cover @a this range.
   *
   * @code
   * void (IP6Range const& range) {
   *   for ( auto const& net : range ) {
   *     net.addr(); // network address.
   *     net.mask(); // network mask;
   *   }
   * }
   * @endcode
   */
  NetSource networks() const;
};

/** Network generator class.
 * This generates networks from a range and acts as both a forward iterator and a container.
 */
class IP6Range::NetSource {
  using self_type = NetSource; ///< Self reference type.
public:
  using range_type = IP6Range; ///< Import base range type.

  /// Construct from @a range.
  explicit NetSource(range_type const &range);

  /// Copy constructor.
  NetSource(self_type const &that) = default;

  /// This class acts as a container and an iterator.
  using iterator = self_type;
  /// All iteration is constant so no distinction between iterators.
  using const_iterator = iterator;

  iterator begin() const; ///< First network.
  iterator end() const;   ///< Past last network.

  /// Return @c true if there are valid networks, @c false if not.
  bool empty() const;

  /// @return The current network.
  IP6Net operator*() const;

  /// Access @a this as if it were an @c IP6Net.
  self_type *operator->();

  /// Iterator support.
  /// @return The current network address.
  IP6Addr const &
  addr() const {
    return _range.min();
  }

  /// Iterator support.
  /// @return The current network mask.
  IPMask
  mask() const {
    return _mask;
  }

  /// Move to next network.
  self_type &operator++();

  /// Move to next network.
  self_type operator++(int);

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality.
  bool operator!=(self_type const &that) const;

protected:
  IP6Range _range;              ///< Remaining range.
  IPMask _mask{IP6Addr::WIDTH}; ///< Current CIDR value.

  void search_wider();

  void search_narrower();

  bool is_valid(IPMask const &mask);
};

class IPRange {
  using self_type = IPRange;

public:
  /// Default constructor - construct invalid range.
  IPRange() = default;

  IPRange(IPAddr const &min, IPAddr const &max);

  /// Construct from an IPv4 @a range.
  IPRange(IP4Range const &range);

  /// Construct from an IPv6 @a range.
  IPRange(IP6Range const &range);

  /** Construct from a string format.
   *
   * @param text Text form of range.
   *
   * The string can be a single address, two addresses separated by a dash '-' or a CIDR network.
   */
  IPRange(string_view const &text);

  /// Equality
  bool
  operator==(self_type const &that) const {
    if (_family != that._family) {
      return false;
    }
    if (this->is_ip4()) {
      return _range._ip4 == that._range._ip4;
    }
    if (this->is_ip6()) {
      return _range._ip6 == that._range._ip6;
    }
    return true;
  }

  /// @return @c true if this is an IPv4 range, @c false if not.
  bool
  is_ip4() const {
    return AF_INET == _family;
  }

  /// @return @c true if this is an IPv6 range, @c false if not.
  bool
  is_ip6() const {
    return AF_INET6 == _family;
  }

  /** Check if @a this range is the IP address @a family.
   *
   * @param family IP address family.
   * @return @c true if this is @a family, @c false if not.
   */
  bool is(sa_family_t family) const;

  /** Load the range from @a text.
   *
   * @param text Range specifier in text format.
   * @return @c true if @a text was successfully parsed, @c false if not.
   *
   * A successful parse means @a this was loaded with the specified range. If not the range is
   * marked as invalid.
   */
  bool load(std::string_view const &text);

  /// @return The minimum address in the range.
  IPAddr min() const;

  /// @return The maximum address in the range.
  IPAddr max() const;

  bool empty() const;

  IP4Range const &
  ip4() const {
    return _range._ip4;
  }

  IP6Range const &
  ip6() const {
    return _range._ip6;
  }

  /** Compute the mask for @a this as a network.
   *
   * @return If @a this is a network, the mask for that network. Otherwise an invalid mask.
   *
   * @see IPMask::is_valid
   */
  IPMask network_mask() const;

  class NetSource;

  /** Generate a list of networks covering @a this range.
   *
   * @return A network generator.
   *
   * The returned object can be used as an iterator, or as a container to iterating over
   * the unique minimal set of networks that cover @a this range.
   *
   * @code
   * void (IPRange const& range) {
   *   for ( auto const& net : range ) {
   *     net.addr(); // network address.
   *     net.mask(); // network mask;
   *   }
   * }
   * @endcode
   */
  NetSource networks() const;

protected:
  /** Range container.
   *
   * @internal
   *
   * This was a @c std::variant at one point, but the complexity got in the way because
   * - These objects have no state, need no destruction.
   * - Construction was problematic because @c variant requires construction, then access,
   *   whereas this needs access to construct (e.g. via the @c load method).
   */
  union {
    std::monostate _nil; ///< Make constructor easier to implement.
    IP4Range _ip4;       ///< IPv4 range.
    IP6Range _ip6;       ///< IPv6 range.
  } _range{std::monostate{}};
  /// Family of @a _range.
  sa_family_t _family{AF_UNSPEC};
};

/** Network generator class.
 * This generates networks from a range and acts as both a forward iterator and a container.
 */
class IPRange::NetSource {
  using self_type = NetSource; ///< Self reference type.
public:
  using range_type = IPRange; ///< Import base range type.

  /// Construct from @a range.
  explicit NetSource(range_type const &range);

  /// Copy constructor.
  NetSource(self_type const &that) = default;

  /// This class acts as a container and an iterator.
  using iterator = self_type;
  /// All iteration is constant so no distinction between iterators.
  using const_iterator = iterator;

  iterator begin() const; ///< First network.
  iterator end() const;   ///< Past last network.

  /// @return The current network.
  IPNet operator*() const;

  /// Access @a this as if it were an @c IP6Net.
  self_type *operator->();

  /// Iterator support.
  /// @return The current network address.
  IPAddr addr() const;

  /// Iterator support.
  /// @return The current network mask.
  IPMask mask() const;

  /// Move to next network.
  self_type &operator++();

  /// Move to next network.
  self_type operator++(int);

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality.
  bool operator!=(self_type const &that) const;

protected:
  union {
    std::monostate _nil;
    IP4Range::NetSource _ip4;
    IP6Range::NetSource _ip6;
  };
  sa_family_t _family = AF_UNSPEC;
};

/// An IPv4 network.
class IP4Net {
  using self_type = IP4Net; ///< Self reference type.
public:
  IP4Net()                      = default; ///< Construct invalid network.
  IP4Net(self_type const &that) = default; ///< Copy constructor.

  /** Construct from @a addr and @a mask.
   *
   * @param addr An address in the network.
   * @param mask The mask for the network.
   *
   * The network is based on the mask, and the resulting network address is chosen such that the
   * network will contain @a addr. For a given @a addr and @a mask there is only one network
   * that satisifies these criteria.
   */
  IP4Net(IP4Addr addr, IPMask mask);

  IP4Net(swoc::TextView text) { this->load(text); }

  /** Parse network as @a text.
   *
   * @param text String describing the network in CIDR format.
   * @return @c true if a valid string, @c false if not.
   */
  bool load(swoc::TextView text);

  /// @return @c true if the network is valid, @c false if not.
  bool is_valid() const;

  /// @return THh smallest address in the network.
  IP4Addr lower_bound() const;

  /// @return The largest address in the network.
  IP4Addr upper_bound() const;

  /// @return The mask for the network.
  IPMask const &mask() const;

  /// @return A range that exactly covers the network.
  IP4Range as_range() const;

  /** Assign an @a addr and @a mask to @a this.
   *
   * @param addr Network addres.
   * @param mask Network mask.
   * @return @a this.
   */
  self_type &assign(IP4Addr const &addr, IPMask const &mask);

  /// Reset network to invalid state.
  self_type &
  clear() {
    _mask.clear();
    return *this;
  }

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality
  bool operator!=(self_type const &that) const;

protected:
  IP4Addr _addr; ///< Network address (also lower_bound).
  IPMask _mask;  ///< Network mask.
};

class IP6Net {
  using self_type = IP6Net; ///< Self reference type.
public:
  IP6Net()                      = default; ///< Construct invalid network.
  IP6Net(self_type const &that) = default; ///< Copy constructor.

  /** Construct from @a addr and @a mask.
   *
   * @param addr An address in the network.
   * @param mask The mask for the network.
   *
   * The network is based on the mask, and the resulting network address is chosen such that the
   * network will contain @a addr. For a given @a addr and @a mask there is only one network
   * that satisifies these criteria.
   */
  IP6Net(IP6Addr addr, IPMask mask);

  /** Parse network as @a text.
   *
   * @param text String describing the network in CIDR format.
   * @return @c true if a valid string, @c false if not.
   */
  bool load(swoc::TextView text);

  /// @return @c true if the network is valid, @c false if not.
  bool is_valid() const;

  /// @return THh smallest address in the network.
  IP6Addr lower_bound() const;

  /// @return The largest address in the network.
  IP6Addr upper_bound() const;

  /// @return The mask for the network.
  IPMask const &mask() const;

  /// @return A range that exactly covers the network.
  IP6Range as_range() const;

  /** Assign an @a addr and @a mask to @a this.
   *
   * @param addr Network addres.
   * @param mask Network mask.
   * @return @a this.
   */
  self_type &assign(IP6Addr const &addr, IPMask const &mask);

  /// Reset network to invalid state.
  self_type &
  clear() {
    _mask.clear();
    return *this;
  }

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality
  bool operator!=(self_type const &that) const;

protected:
  IP6Addr _addr; ///< Network address (also lower_bound).
  IPMask _mask;  ///< Network mask.
};

/** Representation of an IP address network.
 *
 */
class IPNet {
  using self_type = IPNet; ///< Self reference type.
public:
  IPNet()                      = default; ///< Construct invalid network.
  IPNet(self_type const &that) = default; ///< Copy constructor.

  /** Construct from @a addr and @a mask.
   *
   * @param addr An address in the network.
   * @param mask The mask for the network.
   *
   * The network is based on the mask, and the resulting network address is chosen such that the
   * network will contain @a addr. For a given @a addr and @a mask there is only one network
   * that satisifies these criteria.
   */
  IPNet(IPAddr const &addr, IPMask const &mask);

  IPNet(TextView text);

  /** Parse network as @a text.
   *
   * @param text String describing the network in CIDR format.
   * @return @c true if a valid string, @c false if not.
   */
  bool load(swoc::TextView text);

  /// @return @c true if the network is valid, @c false if not.
  bool is_valid() const;

  /// @return THh smallest address in the network.
  IPAddr lower_bound() const;

  /// @return The largest address in the network.
  IPAddr upper_bound() const;

  IPMask::raw_type width() const;

  /// @return The mask for the network.
  IPMask const &mask() const;

  /// @return A range that exactly covers the network.
  IPRange as_range() const;

  bool
  is_ip4() const {
    return _addr.is_ip4();
  }

  bool
  is_ip6() const {
    return _addr.is_ip6();
  }

  sa_family_t
  family() const {
    return _addr.family();
  }

  IP4Net
  ip4() const {
    return IP4Net{_addr.ip4(), _mask};
  }

  IP6Net
  ip6() const {
    return IP6Net{_addr.ip6(), _mask};
  }

  /** Assign an @a addr and @a mask to @a this.
   *
   * @param addr Network addres.
   * @param mask Network mask.
   * @return @a this.
   */
  self_type &assign(IPAddr const &addr, IPMask const &mask);

  /// Reset network to invalid state.
  self_type &
  clear() {
    _mask.clear();
    return *this;
  }

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality
  bool operator!=(self_type const &that) const;

protected:
  IPAddr _addr; ///< Address and family.
  IPMask _mask; ///< Network mask.
};

// --------------------------------------------------------------------------
/** Coloring of IP address space.
 *
 * @tparam PAYLOAD The color class.
 *
 * This is a class to do fast coloring and lookup of the IP address space. It is range oriented and
 * performs well for ranges, much less well for singletons. Conceptually every IP address is a key
 * in the space and can have a color / payload of type @c PAYLOAD.
 *
 * @c PAYLOAD must have the properties
 *
 * - Cheap to copy.
 * - Comparable via the equality and inequality operators.
 */
template <typename PAYLOAD> class IPSpace {
  using self_type = IPSpace;
  using IP4Space  = DiscreteSpace<IP4Addr, PAYLOAD>;
  using IP6Space  = DiscreteSpace<IP6Addr, PAYLOAD>;

public:
  using payload_t  = PAYLOAD; ///< Export payload type.
  using value_type = std::tuple<IPRange const, PAYLOAD &>;

  /// Construct an empty space.
  IPSpace() = default;

  /** Mark the range @a r with @a payload.
   *
   * @param range Range to mark.
   * @param payload Payload to assign.
   * @return @a this
   *
   * All addresses in @a r are set to have the @a payload.
   */
  self_type &mark(IPRange const &range, PAYLOAD const &payload);

  /** Fill the @a range with @a payload.
   *
   * @param range Destination range.
   * @param payload Payload for range.
   * @return this
   *
   * Addresses in @a range are set to have @a payload if the address does not already have a payload.
   */
  self_type &fill(IPRange const &range, PAYLOAD const &payload);

  /** Erase addresses in @a range.
   *
   * @param range Address range.
   * @return @a this
   */
  self_type &erase(IPRange const &range);

  /** Blend @a color in to the @a range.
   *
   * @tparam F Blending functor type (deduced).
   * @tparam U Data to blend in to payloads.
   * @param range Target range.
   * @param color Data to blend in to existing payloads in @a range.
   * @param blender Blending functor.
   * @return @a this
   *
   * @a blender is required to have the signature <tt>void(PAYLOAD& lhs , U CONST&rhs)</tt>. It must
   * act as a compound assignment operator, blending @a rhs into @a lhs. That is, if the result of
   * blending @a rhs in to @a lhs is defined as "lhs @ rhs" for the binary operator "@", then @a
   * blender computes "lhs @= rhs".
   *
   * Every address in @a range is assigned a payload. If the address does not already have a color,
   * it is assigned the default constructed @c PAYLOAD blended with @a color. If the address has a
   * @c PAYLOAD @a p, @a p is updated by invoking <tt>blender(p, color)</tt>, with the expectation
   * that @a p will be updated in place.
   */
  template <typename F, typename U = PAYLOAD> self_type &blend(IPRange const &range, U const &color, F &&blender);

  template <typename F, typename U = PAYLOAD>
  self_type &
  blend(IP4Range const &range, U const &color, F &&blender) {
    _ip4.blend(range, color, blender);
    return *this;
  }

  template <typename F, typename U = PAYLOAD>
  self_type &
  blend(IP6Range const &range, U const &color, F &&blender) {
    _ip6.blend(range, color, blender);
    return *this;
  }

  /// @return The number of distinct ranges.
  size_t
  count() const {
    return _ip4.count() + _ip6.count();
  }

  size_t
  count_ip4() const {
    return _ip4.count();
  }
  size_t
  count_ip6() const {
    return _ip6.count();
  }

  size_t count(sa_family_t f) const;

  /// Remove all ranges.
  void clear();

  /** Constant iterator.
   * THe value type is a tuple of the IP address range and the @a PAYLOAD. Both are constant.
   *
   * @internal The non-const iterator is a subclass of this, in order to share implementation. This
   * also makes it easy to convert from iterator to const iterator, which is desirable.
   *
   * @internal The return type is quite tricky because the value type of the nested containers is
   * not the same as the value type for this container. It's not even a composite - @c IPRange is
   * not an alias for either of the family specific range types. Therefore the iterator itself must
   * contain a synthesized instance of the value type, which creates scoping and update problems.
   * The approach here is to update the synthetic value when the iterator is modified and returning
   * it by value for the dereference operator because a return by reference means  code like
   * @code
   *   auto && [ r , p ] = *(space.find(addr));
   * @endcode
   * can fail due to the iterator going out of scope after the statement is finished making @a r
   * and @a p dangling references. If the return is by value the compiler takes care of it.
   */
  class const_iterator {
    using self_type = const_iterator; ///< Self reference type.
    friend class IPSpace;

  public:
    using value_type = std::tuple<IPRange const, PAYLOAD const &>; /// Import for API compliance.
    // STL algorithm compliance.
    using iterator_category = std::bidirectional_iterator_tag;
    using pointer           = value_type *;
    using reference         = value_type &;
    using difference_type   = int;

    /// Default constructor.
    const_iterator() = default;

    /// Copy constructor.
    const_iterator(self_type const &that);

    /// Assignment.
    self_type &operator=(self_type const &that);

    /// Pre-increment.
    /// Move to the next element in the list.
    /// @return The iterator.
    self_type &operator++();

    /// Pre-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator.
    self_type &operator--();

    /// Post-increment.
    /// Move to the next element in the list.
    /// @return The iterator value before the increment.
    self_type operator++(int);

    /// Post-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator value before the decrement.
    self_type operator--(int);

    /// Dereference.
    /// @return A reference to the referent.
    value_type operator*() const;

    /// Dereference.
    /// @return A pointer to the referent.
    value_type const *operator->() const;

    /// Equality
    bool operator==(self_type const &that) const;

    /// Inequality
    bool operator!=(self_type const &that) const;

  protected:
    // These are stored non-const to make implementing @c iterator easier. This class provides the
    // required @c const protection. This is basic a tuple of iterators - for forward iteration if
    // the primary (ipv4) iterator is at the end, then use the secondary (ipv6) iterator. The reverse
    // is done for reverse iteration. This depends on the extra support @c IntrusiveDList iterators
    // provide.
    typename IP4Space::iterator _iter_4; ///< IPv4 sub-space iterator.
    typename IP6Space::iterator _iter_6; ///< IPv6 sub-space iterator.
    /// Current value.
    value_type _value{IPRange{}, *static_cast<PAYLOAD *>(pseudo_nullptr)};

    /// Dummy payload.
    /// @internal Used to initialize @c value_type for invalid iterators.
    //    static constexpr PAYLOAD * const null_payload = nullptr;

    /** Internal constructor.
     *
     * @param iter4 Starting place for IPv4 subspace.
     * @param iter6 Starting place for IPv6 subspace.
     *
     * In practice, both iterators should be either the beginning or ending iterator for the subspace.
     */
    const_iterator(typename IP4Space::iterator const &iter4, typename IP6Space::iterator const &iter6);
  };

  /** Iterator.
   * The value type is a tuple of the IP address range and the @a PAYLOAD. The range is constant
   * and the @a PAYLOAD is a reference. This can be used to update the @a PAYLOAD for this range.
   *
   * @note Range merges are not trigged by modifications of the @a PAYLOAD via an iterator.
   */
  class iterator : public const_iterator {
    using self_type  = iterator;
    using super_type = const_iterator;

    friend class IPSpace;

  public:
  public:
    /// Value type of iteration.
    using value_type = std::tuple<IPRange const, PAYLOAD &>;
    using pointer    = value_type *;
    using reference  = value_type &;

    /// Default constructor.
    iterator() = default;

    /// Copy constructor.
    iterator(self_type const &that);

    /// Assignment.
    self_type &operator=(self_type const &that);

    /// Pre-increment.
    /// Move to the next element in the list.
    /// @return The iterator.
    self_type &operator++();

    /// Pre-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator.
    self_type &operator--();

    /// Post-increment.
    /// Move to the next element in the list.
    /// @return The iterator value before the increment.
    self_type
    operator++(int) {
      self_type zret{*this};
      ++*this;
      return zret;
    }

    /// Post-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator value before the decrement.
    self_type
    operator--(int) {
      self_type zret{*this};
      --*this;
      return zret;
    }

    /// Dereference.
    /// @return A reference to the referent.
    value_type operator*() const;

    /// Dereference.
    /// @return A pointer to the referent.
    value_type const *operator->() const;

  protected:
    using super_type::super_type; /// Inherit supertype constructors.
  };

  /** Find the payload for an @a addr.
   *
   * @param addr Address to find.
   * @return Iterator for the range containing @a addr.
   */
  iterator
  find(IPAddr const &addr) {
    if (addr.is_ip4()) {
      return this->find(addr.ip4());
    } else if (addr.is_ip6()) {
      return this->find(addr.ip6());
    }
    return this->end();
  }

  /** Find the payload for an @a addr.
   *
   * @param addr Address to find.
   * @return The payload if any, @c nullptr if the address is not in the space.
   */
  iterator
  find(IP4Addr const &addr) {
    auto spot = _ip4.find(addr);
    return spot == _ip4.end() ? this->end() : iterator{_ip4.find(addr), _ip6.begin()};
  }

  /** Find the payload for an @a addr.
   *
   * @param addr Address to find.
   * @return The payload if any, @c nullptr if the address is not in the space.
   */
  iterator
  find(IP6Addr const &addr) {
    return {_ip4.end(), _ip6.find(addr)};
  }

  /// @return A constant iterator to the first element.
  const_iterator begin() const;

  /// @return A constent iterator past the last element.
  const_iterator end() const;

  iterator begin();

  iterator end();

  /// Iterator to the first IPv4 address.
  const_iterator begin_ip4() const;
  /// Iterator past the last IPv4 address.
  const_iterator end_ip4() const;

  const_iterator begin_ip6() const;
  const_iterator end_ip6() const;

  const_iterator
  begin(sa_family_t family) const {
    if (AF_INET == family) {
      return this->begin_ip4();
    } else if (AF_INET6 == family) {
      return this->begin_ip6();
    }
    return this->end();
  }

  const_iterator
  end(sa_family_t family) const {
    if (AF_INET == family) {
      return this->end_ip4();
    } else if (AF_INET6 == family) {
      return this->end_ip6();
    }
    return this->end();
  }

protected:
  IP4Space _ip4; ///< Sub-space containing IPv4 ranges.
  IP6Space _ip6; ///< sub-space containing IPv6 ranges.
};

template <typename PAYLOAD>
IPSpace<PAYLOAD>::const_iterator::const_iterator(typename IP4Space::iterator const &iter4, typename IP6Space::iterator const &iter6)
  : _iter_4(iter4), _iter_6(iter6) {
  if (_iter_4.has_next()) {
    new (&_value) value_type{_iter_4->range(), _iter_4->payload()};
  } else if (_iter_6.has_next()) {
    new (&_value) value_type{_iter_6->range(), _iter_6->payload()};
  }
}

template <typename PAYLOAD> IPSpace<PAYLOAD>::const_iterator::const_iterator(self_type const &that) {
  *this = that;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator=(self_type const &that) -> self_type & {
  _iter_4 = that._iter_4;
  _iter_6 = that._iter_6;
  new (&_value) value_type{that._value};
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator++() -> self_type & {
  bool incr_p = false;
  if (_iter_4.has_next()) {
    ++_iter_4;
    incr_p = true;
    if (_iter_4.has_next()) {
      new (&_value) value_type{_iter_4->range(), _iter_4->payload()};
      return *this;
    }
  }

  if (_iter_6.has_next()) {
    if (incr_p || (++_iter_6).has_next()) {
      new (&_value) value_type{_iter_6->range(), _iter_6->payload()};
      return *this;
    }
  }
  new (&_value) value_type{IPRange{}, *static_cast<PAYLOAD *>(pseudo_nullptr)};
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator++(int) -> self_type {
  self_type zret(*this);
  ++*this;
  return zret;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator--() -> self_type & {
  if (_iter_6.has_prev()) {
    --_iter_6;
    new (&_value) value_type{_iter_6->range(), _iter_6->payload()};
    return *this;
  }
  if (_iter_4.has_prev()) {
    --_iter_4;
    new (&_value) value_type{_iter_4->range(), _iter_4->payload()};
    return *this;
  }
  new (&_value) value_type{IPRange{}, *static_cast<PAYLOAD *>(pseudo_nullptr)};
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator--(int) -> self_type {
  self_type zret(*this);
  --*this;
  return zret;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator*() const -> value_type {
  return _value;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::const_iterator::operator->() const -> value_type const * {
  return &_value;
}

/* Bit of subtlety with equality - although it seems that if @a _iter_4 is valid, it doesn't matter
 * where @a _iter6 is (because it is really the iterator location that's being checked), it's
 * neccesary to do the @a _iter_4 validity on both iterators to avoid the case of a false positive
 * where different internal iterators are valid. However, in practice the other (non-active)
 * iterator won't have an arbitrary value, it will be either @c begin or @c end in step with the
 * active iterator therefore it's effective and cheaper to just check both values.
 */

template <typename PAYLOAD>
bool
IPSpace<PAYLOAD>::const_iterator::operator==(self_type const &that) const {
  return _iter_4 == that._iter_4 && _iter_6 == that._iter_6;
}

template <typename PAYLOAD>
bool
IPSpace<PAYLOAD>::const_iterator::operator!=(self_type const &that) const {
  return _iter_4 != that._iter_4 || _iter_6 != that._iter_6;
}

template <typename PAYLOAD> IPSpace<PAYLOAD>::iterator::iterator(self_type const &that) {
  *this = that;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::iterator::operator=(self_type const &that) -> self_type & {
  this->super_type::operator=(that);
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::iterator::operator->() const -> value_type const * {
  return static_cast<value_type *>(&super_type::_value);
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::iterator::operator*() const -> value_type {
  return reinterpret_cast<value_type const &>(super_type::_value);
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::iterator::operator++() -> self_type & {
  this->super_type::operator++();
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::iterator::operator--() -> self_type & {
  this->super_type::operator--();
  return *this;
}

// --------------------------------------------------------------------------
// -- IP4Addr --
inline constexpr sa_family_t
IP4Addr::family() {
  return AF_value;
}

// -- IP6Addr --
inline constexpr sa_family_t
IP6Addr::family() {
  return AF_value;
}

// -- IPAddr --
// @c constexpr constructor is required to initialize _something_, it can't be completely uninitializing.
inline constexpr IPAddr::raw_addr_type::raw_addr_type() : _ip4(INADDR_ANY) {}

inline IPAddr::IPAddr(in_addr_t addr) : _addr(addr), _family(IP4Addr::AF_value) {}

inline IPAddr::IPAddr(in6_addr const &addr) : _addr(addr), _family(IP6Addr::AF_value) {}

inline IPAddr::IPAddr(sockaddr const *addr) {
  this->assign(addr);
}

inline IPAddr::IPAddr(IPEndpoint const &addr) {
  this->assign(&addr.sa);
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
IPAddr::operator=(IPEndpoint const &addr) {
  return this->assign(&addr.sa);
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
IPAddr::isCompatibleWith(self_type const &that) {
  return this->is_valid() && _family == that._family;
}

inline bool
IPAddr::is_loopback() const {
  return (AF_INET == _family && 0x7F == _addr._octet[0]) || (AF_INET6 == _family && _addr._ip6.is_loopback());
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
bool operator==(IPAddr const &lhs, sockaddr const *rhs);

inline bool
operator==(sockaddr const *lhs, IPAddr const &rhs) {
  return rhs == lhs;
}

inline bool
operator!=(IPAddr const &lhs, sockaddr const *rhs) {
  return !(lhs == rhs);
}

inline bool
operator!=(sockaddr const *lhs, IPAddr const &rhs) {
  return !(rhs == lhs);
}

inline bool
operator==(IPAddr const &lhs, IPEndpoint const &rhs) {
  return lhs == &rhs.sa;
}

inline bool
operator==(IPEndpoint const &lhs, IPAddr const &rhs) {
  return &lhs.sa == rhs;
}

inline bool
operator!=(IPAddr const &lhs, IPEndpoint const &rhs) {
  return !(lhs == &rhs.sa);
}

inline bool
operator!=(IPEndpoint const &lhs, IPAddr const &rhs) {
  return !(rhs == &lhs.sa);
}

inline IP4Addr const &
IPAddr::ip4() const {
  return _addr._ip4;
}

inline IP6Addr const &
IPAddr::ip6() const {
  return _addr._ip6;
}

/// ------------------------------------------------------------------------------------

inline IPEndpoint::IPEndpoint() {
  sa.sa_family = AF_UNSPEC;
}

inline IPEndpoint::IPEndpoint(IPAddr const &addr) {
  this->assign(addr);
}

inline IPEndpoint::IPEndpoint(sockaddr const *sa) {
  this->assign(sa);
}

inline IPEndpoint::IPEndpoint(IPEndpoint::self_type const &that) : self_type(&that.sa) {}

inline IPEndpoint &
IPEndpoint::invalidate() {
  sa.sa_family = AF_UNSPEC;
  return *this;
}

inline void
IPEndpoint::invalidate(sockaddr *addr) {
  addr->sa_family = AF_UNSPEC;
}

inline bool
IPEndpoint::is_valid() const {
  return sa.sa_family == AF_INET || sa.sa_family == AF_INET6;
}

inline IPEndpoint &
IPEndpoint::operator=(self_type const &that) {
  self_type::assign(&sa, &that.sa);
  return *this;
}

inline IPEndpoint &
IPEndpoint::assign(sockaddr const *src) {
  self_type::assign(&sa, src);
  return *this;
}

inline IPEndpoint const &
IPEndpoint::fill(sockaddr *addr) const {
  self_type::assign(addr, &sa);
  return *this;
}

inline bool
IPEndpoint::is_ip4() const {
  return AF_INET == sa.sa_family;
}

inline bool
IPEndpoint::is_ip6() const {
  return AF_INET6 == sa.sa_family;
}

inline sa_family_t
IPEndpoint::family() const {
  return sa.sa_family;
}

inline in_port_t &
IPEndpoint::network_order_port() {
  return self_type::port(&sa);
}

inline in_port_t
IPEndpoint::network_order_port() const {
  return self_type::port(&sa);
}

inline in_port_t
IPEndpoint::host_order_port() const {
  return ntohs(this->network_order_port());
}

inline in_port_t &
IPEndpoint::port(sockaddr *sa) {
  switch (sa->sa_family) {
  case AF_INET:
    return reinterpret_cast<sockaddr_in *>(sa)->sin_port;
  case AF_INET6:
    return reinterpret_cast<sockaddr_in6 *>(sa)->sin6_port;
  }
  // Force a failure upstream by returning a null reference.
  throw std::domain_error("sockaddr is not a valid IP address");
}

inline in_port_t
IPEndpoint::port(sockaddr const *addr) {
  return self_type::port(const_cast<sockaddr *>(addr));
}

inline in_port_t
IPEndpoint::host_order_port(sockaddr const *sa) {
  return ntohs(self_type::port(sa));
}

// --- IPAddr variants ---

inline constexpr IP4Addr::IP4Addr(in_addr_t addr) : _addr(addr) {}

inline IP4Addr::IP4Addr(std::string_view const &text) {
  if (!this->load(text)) {
    _addr = INADDR_ANY;
  }
}

inline IP4Addr::IP4Addr(IPAddr const &addr) : _addr(addr._family == AF_INET ? addr._addr._ip4._addr : INADDR_ANY) {}

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

inline bool
operator==(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr == rhs._addr;
}

inline bool
operator!=(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr != rhs._addr;
}

inline bool
operator<(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr < rhs._addr;
}

inline bool
operator<=(IP4Addr const &lhs, IP4Addr const &rhs) {
  return lhs._addr <= rhs._addr;
}

inline bool
operator>(IP4Addr const &lhs, IP4Addr const &rhs) {
  return rhs < lhs;
}

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

constexpr in_addr_t
IP4Addr::reorder(in_addr_t src) {
  return ((src & 0xFF) << 24) | (((src >> 8) & 0xFF) << 16) | (((src >> 16) & 0xFF) << 8) | ((src >> 24) & 0xFF);
}

// ---

inline IP6Addr::IP6Addr(in6_addr const &addr) {
  *this = addr;
}

inline IP6Addr::IP6Addr(std::string_view const &text) {
  if (!this->load(text)) {
    this->clear();
  }
}

inline IP6Addr::IP6Addr(IPAddr const &addr) : _addr{addr._addr._ip6._addr} {}

inline in6_addr &
IP6Addr::copy_to(in6_addr &addr) const {
  self_type::reorder(addr, _addr._raw);
  return addr;
}

inline in6_addr
IP6Addr::network_order() const {
  in6_addr zret;
  return this->copy_to(zret);
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
  if (++(_addr._store[1]) == 0) {
    ++(_addr._store[0]);
  }
  return *this;
}

inline IP6Addr &
IP6Addr::operator--() {
  if (--(_addr._store[1]) == ~static_cast<uint64_t>(0)) {
    --(_addr._store[0]);
  }
  return *this;
}

inline void
IP6Addr::reorder(unsigned char dst[WORD_SIZE], unsigned char const src[WORD_SIZE]) {
  for (size_t idx = 0; idx < WORD_SIZE; ++idx) {
    dst[idx] = src[WORD_SIZE - (idx + 1)];
  }
}

inline bool
operator==(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[0] == rhs._addr._store[0] && lhs._addr._store[1] == rhs._addr._store[1];
}

inline bool
operator!=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[0] != rhs._addr._store[0] || lhs._addr._store[1] != rhs._addr._store[1];
}

inline bool
operator<(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[0] < rhs._addr._store[0] ||
         (lhs._addr._store[0] == rhs._addr._store[0] && lhs._addr._store[1] < rhs._addr._store[1]);
}

inline bool
operator>(IP6Addr const &lhs, IP6Addr const &rhs) {
  return rhs < lhs;
}

inline bool
operator<=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return lhs._addr._store[0] < rhs._addr._store[0] ||
         (lhs._addr._store[0] == rhs._addr._store[0] && lhs._addr._store[1] <= rhs._addr._store[1]);
}

inline bool
operator>=(IP6Addr const &lhs, IP6Addr const &rhs) {
  return rhs <= lhs;
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

// --- //

inline bool
IPAddr::operator==(self_type const &that) const {
  switch (_family) {
  case AF_INET:
    return that._family == AF_INET && _addr._ip4 == that._addr._ip4;
  case AF_INET6:
    return that._family == AF_INET6 && _addr._ip6 == that._addr._ip6;
  default:
    return false;
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

// Disambiguating comparisons.

inline bool
operator==(IPAddr const &lhs, IP4Addr const &rhs) {
  return lhs.is_ip4() && static_cast<IP4Addr const &>(lhs) == rhs;
}

inline bool
operator!=(IPAddr const &lhs, IP4Addr const &rhs) {
  return !lhs.is_ip4() || static_cast<IP4Addr const &>(lhs) != rhs;
}

inline bool
operator==(IP4Addr const &lhs, IPAddr const &rhs) {
  return rhs.is_ip4() && lhs == static_cast<IP4Addr const &>(rhs);
}

inline bool
operator!=(IP4Addr const &lhs, IPAddr const &rhs) {
  return !rhs.is_ip4() || lhs != static_cast<IP4Addr const &>(rhs);
}

inline bool
operator==(IPAddr const &lhs, IP6Addr const &rhs) {
  return lhs.is_ip6() && static_cast<IP6Addr const &>(lhs) == rhs;
}

inline bool
operator!=(IPAddr const &lhs, IP6Addr const &rhs) {
  return !lhs.is_ip6() || static_cast<IP6Addr const &>(lhs) != rhs;
}

inline bool
operator==(IP6Addr const &lhs, IPAddr const &rhs) {
  return rhs.is_ip6() && lhs == rhs.ip6();
}

inline bool
operator!=(IP6Addr const &lhs, IPAddr const &rhs) {
  return !rhs.is_ip6() || lhs != rhs.ip6();
}

// +++ IPRange +++

inline IP4Range::IP4Range(string_view const &text) {
  this->load(text);
}

inline auto
IP4Range::networks() const -> NetSource {
  return {NetSource{*this}};
}

inline IP6Range::IP6Range(string_view const &text) {
  this->load(text);
}

inline auto
IP6Range::networks() const -> NetSource {
  return {NetSource{*this}};
}

inline IPRange::IPRange(IP4Range const &range) : _family(AF_INET) {
  _range._ip4 = range;
}

inline IPRange::IPRange(IP6Range const &range) : _family(AF_INET6) {
  _range._ip6 = range;
}

inline IPRange::IPRange(string_view const &text) {
  this->load(text);
}

inline auto
IPRange::networks() const -> NetSource {
  return {NetSource{*this}};
}

inline bool
IPRange::is(sa_family_t family) const {
  return family == _family;
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

inline IPAddr
operator&(IPAddr const &addr, IPMask const &mask) {
  return IPAddr{addr} &= mask;
}

inline IPAddr
operator|(IPAddr const &addr, IPMask const &mask) {
  return IPAddr{addr} |= mask;
}

// +++ IPNet +++

inline IP4Net::IP4Net(swoc::IP4Addr addr, swoc::IPMask mask) : _addr(addr & mask), _mask(mask) {}

inline IPMask const &
IP4Net::mask() const {
  return _mask;
}

inline bool
IP4Net::is_valid() const {
  return _mask.is_valid();
}

inline IP4Addr
IP4Net::lower_bound() const {
  return _addr;
}

inline IP4Addr
IP4Net::upper_bound() const {
  return _addr | _mask;
}

inline IP4Range
IP4Net::as_range() const {
  return {this->lower_bound(), this->upper_bound()};
}

inline bool
IP4Net::operator==(self_type const &that) const {
  return _mask == that._mask && _addr == that._addr;
}

inline bool
IP4Net::operator!=(self_type const &that) const {
  return _mask != that._mask || _addr != that._addr;
}

inline IP4Net::self_type &
IP4Net::assign(IP4Addr const &addr, IPMask const &mask) {
  _addr = addr & mask;
  _mask = mask;
  return *this;
}

inline IP6Net::IP6Net(swoc::IP6Addr addr, swoc::IPMask mask) : _addr(addr & mask), _mask(mask) {}

inline IPMask const &
IP6Net::mask() const {
  return _mask;
}

inline bool
IP6Net::is_valid() const {
  return _mask.is_valid();
}

inline IP6Addr
IP6Net::lower_bound() const {
  return _addr;
}

inline IP6Addr
IP6Net::upper_bound() const {
  return _addr | _mask;
}

inline IP6Range
IP6Net::as_range() const {
  return {this->lower_bound(), this->upper_bound()};
}

inline bool
IP6Net::operator==(self_type const &that) const {
  return _mask == that._mask && _addr == that._addr;
}

inline bool
IP6Net::operator!=(self_type const &that) const {
  return _mask != that._mask || _addr != that._addr;
}

inline IP6Net::self_type &
IP6Net::assign(IP6Addr const &addr, IPMask const &mask) {
  _addr = addr & mask;
  _mask = mask;
  return *this;
}

inline IPNet::IPNet(IPAddr const &addr, IPMask const &mask) : _addr(addr & mask), _mask(mask) {}

inline IPNet::IPNet(TextView text) {
  this->load(text);
}

inline bool
IPNet::is_valid() const {
  return _mask.is_valid();
}

inline IPAddr
IPNet::lower_bound() const {
  return _addr;
}

inline IPAddr
IPNet::upper_bound() const {
  return _addr | _mask;
}

inline IPMask::raw_type
IPNet::width() const {
  return _mask.width();
}

inline IPMask const &
IPNet::mask() const {
  return _mask;
}

inline IPRange
IPNet::as_range() const {
  return {this->lower_bound(), this->upper_bound()};
}

inline IPNet::self_type &
IPNet::assign(IPAddr const &addr, IPMask const &mask) {
  _addr = addr & mask;
  _mask = mask;
  return *this;
}

inline bool
IPNet::operator==(IPNet::self_type const &that) const {
  return _mask == that._mask && _addr == that._addr;
}

inline bool
IPNet::operator!=(IPNet::self_type const &that) const {
  return _mask != that._mask || _addr != that._addr;
}

inline bool
operator==(IPNet const &lhs, IP4Net const &rhs) {
  return lhs.is_ip4() && lhs.ip4() == rhs;
}

inline bool
operator==(IP4Net const &lhs, IPNet const &rhs) {
  return rhs.is_ip4() && rhs.ip4() == lhs;
}

inline bool
operator==(IPNet const &lhs, IP6Net const &rhs) {
  return lhs.is_ip6() && lhs.ip6() == rhs;
}

inline bool
operator==(IP6Net const &lhs, IPNet const &rhs) {
  return rhs.is_ip6() && rhs.ip6() == lhs;
}

// +++ Range -> Network classes +++

inline bool
IP4Range::NetSource::is_valid(swoc::IP4Addr mask) {
  return ((mask._addr & _range._min._addr) == _range._min._addr) && ((_range._min._addr | ~mask._addr) <= _range._max._addr);
}

inline IP4Net
IP4Range::NetSource::operator*() const {
  return IP4Net{_range.min(), IPMask{_cidr}};
}

inline IP4Range::NetSource::iterator
IP4Range::NetSource::begin() const {
  return *this;
}

inline IP4Range::NetSource::iterator
IP4Range::NetSource::end() const {
  return self_type{range_type{}};
}

inline bool
IP4Range::NetSource::empty() const {
  return _range.empty();
}

inline IPMask
IP4Range::NetSource::mask() const {
  return IPMask{_cidr};
}

inline auto
IP4Range::NetSource::operator->() -> self_type * {
  return this;
}

inline IP4Addr const &
IP4Range::NetSource::addr() const {
  return _range.min();
}

inline bool
IP4Range::NetSource::operator==(IP4Range::NetSource::self_type const &that) const {
  return ((_cidr == that._cidr) && (_range == that._range)) || (_range.empty() && that._range.empty());
}

inline bool
IP4Range::NetSource::operator!=(IP4Range::NetSource::self_type const &that) const {
  return !(*this == that);
}

inline auto
IP6Range::NetSource::begin() const -> iterator {
  return *this;
}

inline auto
IP6Range::NetSource::end() const -> iterator {
  return self_type{range_type{}};
}

inline bool
IP6Range::NetSource::empty() const {
  return _range.empty();
}

inline IP6Net
IP6Range::NetSource::operator*() const {
  return IP6Net{_range.min(), _mask};
}

inline auto
IP6Range::NetSource::operator->() -> self_type * {
  return this;
}

inline bool
IP6Range::NetSource::is_valid(IPMask const &mask) {
  return ((_range.min() & mask) == _range.min()) && ((_range.min() | mask) <= _range.max());
}

inline bool
IP6Range::NetSource::operator==(IP6Range::NetSource::self_type const &that) const {
  return ((_mask == that._mask) && (_range == that._range)) || (_range.empty() && that._range.empty());
}

inline bool
IP6Range::NetSource::operator!=(IP6Range::NetSource::self_type const &that) const {
  return !(*this == that);
}

inline IPRange::NetSource::NetSource(IPRange::NetSource::range_type const &range) {
  if (range.is_ip4()) {
    new (&_ip4) decltype(_ip4)(range.ip4());
    _family = AF_INET;
  } else if (range.is_ip6()) {
    new (&_ip6) decltype(_ip6)(range.ip6());
    _family = AF_INET6;
  }
}

inline auto
IPRange::NetSource::begin() const -> iterator {
  return *this;
}

inline auto
IPRange::NetSource::end() const -> iterator {
  return AF_INET == _family ? self_type{IP4Range{}} : AF_INET6 == _family ? self_type{IP6Range{}} : self_type{IPRange{}};
}

inline IPAddr
IPRange::NetSource::addr() const {
  if (AF_INET == _family) {
    return _ip4.addr();
  } else if (AF_INET6 == _family) {
    return _ip6.addr();
  }
  return {};
}

inline IPMask
IPRange::NetSource::mask() const {
  if (AF_INET == _family) {
    return _ip4.mask();
  } else if (AF_INET6 == _family) {
    return _ip6.mask();
  }
  return {};
}

inline IPNet
IPRange::NetSource::operator*() const {
  return {this->addr(), this->mask()};
}

inline auto
IPRange::NetSource::operator++() -> self_type & {
  if (AF_INET == _family) {
    ++_ip4;
  } else if (AF_INET6 == _family) {
    ++_ip6;
  }
  return *this;
}

inline auto
IPRange::NetSource::operator->() -> self_type * {
  return this;
}

inline bool
IPRange::NetSource::operator==(self_type const &that) const {
  if (_family != that._family) {
    return false;
  }
  if (AF_INET == _family) {
    return _ip4 == that._ip4;
  } else if (AF_INET6 == _family) {
    return _ip6 == that._ip6;
  } else if (AF_UNSPEC == _family) {
    return true;
  }
  return false;
}

inline bool
IPRange::NetSource::operator!=(self_type const &that) const {
  return !(*this == that);
}

// --- IPSpace

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::mark(IPRange const &range, PAYLOAD const &payload) -> self_type & {
  if (range.is(AF_INET)) {
    _ip4.mark(range.ip4(), payload);
  } else if (range.is(AF_INET6)) {
    _ip6.mark(range.ip6(), payload);
  }
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::fill(IPRange const &range, PAYLOAD const &payload) -> self_type & {
  if (range.is(AF_INET6)) {
    _ip6.fill(range.ip6(), payload);
  } else if (range.is(AF_INET)) {
    _ip4.fill(range.ip4(), payload);
  }
  return *this;
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::erase(IPRange const &range) -> self_type & {
  if (range.is(AF_INET)) {
    _ip4.erase(range.ip4());
  } else if (range.is(AF_INET6)) {
    _ip6.erase(range.ip6());
  }
  return *this;
}

template <typename PAYLOAD>
template <typename F, typename U>
auto
IPSpace<PAYLOAD>::blend(IPRange const &range, U const &color, F &&blender) -> self_type & {
  if (range.is(AF_INET)) {
    _ip4.blend(range.ip4(), color, blender);
  } else if (range.is(AF_INET6)) {
    _ip6.blend(range.ip6(), color, blender);
  }
  return *this;
}

template <typename PAYLOAD>
void
IPSpace<PAYLOAD>::clear() {
  _ip4.clear();
  _ip6.clear();
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::begin() const -> const_iterator {
  auto nc_this = const_cast<self_type *>(this);
  return const_iterator(nc_this->_ip4.begin(), nc_this->_ip6.begin());
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::end() const -> const_iterator {
  auto nc_this = const_cast<self_type *>(this);
  return const_iterator(nc_this->_ip4.end(), nc_this->_ip6.end());
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::begin_ip4() const -> const_iterator {
  return this->begin();
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::end_ip4() const -> const_iterator {
  auto nc_this = const_cast<self_type *>(this);
  return iterator(nc_this->_ip4.end(), nc_this->_ip6.begin());
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::begin_ip6() const -> const_iterator {
  auto nc_this = const_cast<self_type *>(this);
  return iterator(nc_this->_ip4.end(), nc_this->_ip6.begin());
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::end_ip6() const -> const_iterator {
  return this->end();
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::begin() -> iterator {
  return iterator{_ip4.begin(), _ip6.begin()};
}

template <typename PAYLOAD>
auto
IPSpace<PAYLOAD>::end() -> iterator {
  return iterator{_ip4.end(), _ip6.end()};
}

template <typename PAYLOAD>
size_t
IPSpace<PAYLOAD>::count(sa_family_t f) const {
  return IP4Addr::AF_value == f ? _ip4.count() : IP6Addr::AF_value == f ? _ip6.count() : 0;
}

}} // namespace swoc::SWOC_VERSION_NS

/// @cond NOT_DOCUMENTED
namespace std {

// -- Tuple support for IP4Net --
template <> class tuple_size<swoc::IP4Net> : public std::integral_constant<size_t, 2> {};

template <size_t IDX> class tuple_element<IDX, swoc::IP4Net> { static_assert("swoc::IP4Net tuple index out of range"); };

template <> class tuple_element<0, swoc::IP4Net> {
public:
  using type = swoc::IP4Addr;
};

template <> class tuple_element<1, swoc::IP4Net> {
public:
  using type = swoc::IPMask;
};

// -- Tuple support for IP6Net --
template <> class tuple_size<swoc::IP6Net> : public std::integral_constant<size_t, 2> {};

template <size_t IDX> class tuple_element<IDX, swoc::IP6Net> { static_assert("swoc::IP6Net tuple index out of range"); };

template <> class tuple_element<0, swoc::IP6Net> {
public:
  using type = swoc::IP6Addr;
};

template <> class tuple_element<1, swoc::IP6Net> {
public:
  using type = swoc::IPMask;
};

// -- Tuple support for IPNet --
template <> class tuple_size<swoc::IPNet> : public std::integral_constant<size_t, 2> {};

template <size_t IDX> class tuple_element<IDX, swoc::IPNet> { static_assert("swoc::IPNet tuple index out of range"); };

template <> class tuple_element<0, swoc::IPNet> {
public:
  using type = swoc::IPAddr;
};

template <> class tuple_element<1, swoc::IPNet> {
public:
  using type = swoc::IPMask;
};

} // namespace std
/// @endcond

namespace swoc { inline namespace SWOC_VERSION_NS {

template <size_t IDX>
typename std::tuple_element<IDX, IP4Net>::type
get(swoc::IP4Net const &net) {
  if constexpr (IDX == 0) {
    return net.lower_bound();
  } else if constexpr (IDX == 1) {
    return net.mask();
  }
}

template <size_t IDX>
typename std::tuple_element<IDX, IP6Net>::type
get(swoc::IP6Net const &net) {
  if constexpr (IDX == 0) {
    return net.lower_bound();
  } else if constexpr (IDX == 1) {
    return net.mask();
  }
}

template <size_t IDX>
typename std::tuple_element<IDX, IPNet>::type
get(swoc::IPNet const &net) {
  if constexpr (IDX == 0) {
    return net.lower_bound();
  } else if constexpr (IDX == 1) {
    return net.mask();
  }
}

}} // namespace swoc::SWOC_VERSION_NS
