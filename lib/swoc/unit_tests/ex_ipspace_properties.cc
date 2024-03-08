// SPDX-License-Identifier: Apache-2.0
// Copyright 2014 Network Geographics

/** @file

    Example use of IPSpace for property mapping.
*/

#include "catch.hpp"

#include <memory>
#include <limits>
#include <iostream>

#include "swoc/TextView.h"
#include "swoc/swoc_ip.h"
#include "swoc/bwf_ip.h"
#include "swoc/bwf_std.h"

using namespace std::literals;
using namespace swoc::literals;
using swoc::TextView;
using swoc::IPEndpoint;

using swoc::IP4Addr;
using swoc::IP4Range;

using swoc::IP6Addr;
using swoc::IP6Range;

using swoc::IPAddr;
using swoc::IPRange;
using swoc::IPSpace;

using swoc::MemSpan;
using swoc::MemArena;

using W = swoc::LocalBufferWriter<256>;
namespace {
bool Verbose_p =
#if VERBOSE_EXAMPLE_OUTPUT
  true
#else
  false
#endif
  ;
} // namespace

TEST_CASE("IPSpace bitset blending", "[libswoc][ipspace][bitset][blending]") {
  // Color each address with a set of bits.
  using PAYLOAD = std::bitset<32>;
  // Declare the IPSpace.
  using Space = swoc::IPSpace<PAYLOAD>;
  // Example data type.
  using Data = std::tuple<TextView, PAYLOAD>;

  // Dump the ranges to stdout.
  auto dump = [](Space &space) -> void {
    if (Verbose_p) {
      std::cout << W().print("{} ranges\n", space.count());
      for (auto &&[r, payload] : space) {
        std::cout << W().print("{:25} : {}\n", r, payload);
      }
    }
  };

  // Convert a list of bit indices into a bitset.
  auto make_bits = [](std::initializer_list<unsigned> indices) -> PAYLOAD {
    PAYLOAD bits;
    for (auto idx : indices) {
      bits[idx] = true;
    }
    return bits;
  };

  // Bitset blend functor which computes a union of the bitsets.
  auto blender = [](PAYLOAD &lhs, PAYLOAD const &rhs) -> bool {
    lhs |= rhs;
    return true;
  };

  // Example marking functor.
  auto marker = [&](Space &space, swoc::MemSpan<Data> ranges) -> void {
    // For each test range, compute the bitset from the list of bit indices.
    for (auto &&[text, bits] : ranges) {
      space.blend(IPRange{text}, bits, blender);
    }
  };

  // The IPSpace instance.
  Space space;

  // test ranges 1
  std::array<Data, 7> ranges_1 = {
    {{"100.0.0.0-100.0.0.255", make_bits({0})},
     {"100.0.1.0-100.0.1.255", make_bits({1})},
     {"100.0.2.0-100.0.2.255", make_bits({2})},
     {"100.0.3.0-100.0.3.255", make_bits({3})},
     {"100.0.4.0-100.0.4.255", make_bits({4})},
     {"100.0.5.0-100.0.5.255", make_bits({5})},
     {"100.0.6.0-100.0.6.255", make_bits({6})}}
  };

  marker(space, MemSpan<Data>{ranges_1.data(), ranges_1.size()});
  dump(space);

  // test ranges 2
  std::array<Data, 3> ranges_2 = {
    {{"100.0.0.0-100.0.0.255", make_bits({31})},
     {"100.0.1.0-100.0.1.255", make_bits({30})},
     {"100.0.2.128-100.0.3.127", make_bits({29})}}
  };

  marker(space, MemSpan<Data>{ranges_2.data(), ranges_2.size()});
  dump(space);

  // test ranges 3
  std::array<Data, 1> ranges_3 = {{{"100.0.2.0-100.0.4.255", make_bits({2, 3, 29})}}};

  marker(space, MemSpan<Data>{ranges_3.data(), ranges_3.size()});
  dump(space);

  // reset blend functor
  auto resetter = [](PAYLOAD &lhs, PAYLOAD const &rhs) -> bool {
    auto mask  = rhs;
    lhs       &= mask.flip();
    return lhs != 0;
  };

  // erase bits
  space.blend(IPRange{"0.0.0.0-255.255.255.255"}, make_bits({2, 3, 29}), resetter);
  dump(space);

  // ragged boundaries
  space.blend(IPRange{"100.0.2.19-100.0.5.117"}, make_bits({16, 18, 20}), blender);
  dump(space);

  // bit list blend functor which computes a union of the bitsets.
  auto bit_blender = [](PAYLOAD &lhs, std::initializer_list<unsigned> const &rhs) -> bool {
    for (auto idx : rhs)
      lhs[idx] = true;
    return true;
  };

  std::initializer_list<unsigned> bit_list = {10, 11};
  space.blend(IPRange{"0.0.0.1-255.255.255.254"}, bit_list, bit_blender);
  dump(space);
}

// ---

/** A "table" is conceptually a table with the rows labeled by IP address and a set of
 * property columns that represent data for each IP address.
 */
class Table {
  using self_type = Table; ///< Self reference type.
public:
  static constexpr char SEP = ','; /// Value separator for input file.

  /** A property is the description of data for an address.
   * The table consists of an ordered list of properties, each corresponding to a column.
   */
  class Property {
    using self_type = Property; ///< Self reference type.
  public:
    /// A handle to an instance.
    using Handle = std::unique_ptr<self_type>;

    /** Construct an instance.
     *
     * @param name Property name.
     */
    Property(TextView const &name) : _name(name){};

    /// Force virtual destructor.
    virtual ~Property() = default;

    /** The size of the property in bytes.
     *
     * @return The amount of data needed for a single instance of the property value.
     */
    virtual size_t size() const = 0;

    /** The index in the table of the property.
     *
     * @return The column index.
     */
    unsigned
    idx() const {
      return _idx;
    }

    /** Token persistence.
     *
     * @return @c true if the token needs to be preserved, @c false if not.
     *
     * If the token for the value is consumed, this should be left as is. However, if the token
     * itself needs to be persistent for the lifetime of the table, this must be overridden to
     * return @c true.
     */
    virtual bool
    needs_localized_token() const {
      return false;
    }

    /// @return The row data offset in bytes for this property.
    size_t
    offset() const {
      return _offset;
    }

    /** Parse the @a token.
     *
     * @param token Value from the input file for this property.
     * @param span Row data storage for this property.
     * @return @c true if @a token was correctly parse, @c false if not.
     *
     * The table parses the input file and handles the fields in a line. Each value is passed to
     * the corresponding property for parsing via this method. The method should update the data
     * pointed at by @a span.
     */
    virtual bool parse(TextView token, MemSpan<std::byte> span) = 0;

  protected:
    friend class Table;

    TextView _name;                                        ///< Name of the property.
    unsigned _idx  = std::numeric_limits<unsigned>::max(); ///< Column index.
    size_t _offset = std::numeric_limits<size_t>::max();   ///< Offset into a row.

    /** Set the column index.
     *
     * @param idx Index for this property.
     * @return @a this.
     *
     * This is called from @c Table to indicate the column index.
     */
    self_type &
    assign_idx(unsigned idx) {
      _idx = idx;
      return *this;
    }

    /** Set the row data @a offset.
     *
     * @param offset Offset in bytes.
     * @return @a this
     *
     * This is called from @c Table to store the row data offset.
     */
    self_type &
    assign_offset(size_t offset) {
      _offset = offset;
      return *this;
    }
  };

  /// Construct an empty Table.
  Table() = default;

  /** Add a property column to the table.
   *
   * @tparam P Property class.
   * @param col Column descriptor.
   * @return @a A pointer to the property.
   *
   * The @c Property instance must be owned by the @c Table because changes are made to it specific
   * to this instance of @c Table.
   */
  template <typename P> P *add_column(std::unique_ptr<P> &&col);

  /// A row in the table.
  class Row {
    using self_type = Row; ///< Self reference type.
  public:
    /// Default cconstruct an row with uninitialized data.
    Row(MemSpan<std::byte> span) : _data(span) {}
    /** Extract property specific data from @a this.
     *
     * @param prop Property that defines the data.
     * @return The range of bytes in the row for @a prop.
     */
    MemSpan<std::byte> span_for(Property const &prop) const;

  protected:
    MemSpan<std::byte> _data; ///< Raw row data.
  };

  /** Parse input.
   *
   * @param src The source to parse.
   * @return @a true if parsing was successful, @c false if not.
   *
   * In general, @a src will be the contents of a file.
   *
   * @see swoc::file::load
   */
  bool parse(TextView src);

  /** Look up @a addr in the table.
   *
   * @param addr Address to find.
   * @return A @c Row for the address, or @c nullptr if not found.
   */
  Row *find(IPAddr const &addr);

  /// @return The number of ranges in the container.
  size_t
  size() const {
    return _space.count();
  }

  /** Property for column @a idx.
   *
   * @param idx Index.
   * @return The property.
   */
  Property *
  column(unsigned idx) {
    return _columns[idx].get();
  }

protected:
  size_t _size = 0; ///< Size of row data.
  /// Defined properties for columns.
  std::vector<Property::Handle> _columns;

  /// IPSpace type.
  using space = IPSpace<Row>;
  space _space; ///< IPSpace instance.

  MemArena _arena; ///< Arena for storing rows.

  /** Extract the next token from the line.
   *
   * @param line Current line [in,out]
   * @return Extracted token.
   */
  TextView token(TextView &line);

  /** Localize view.
   *
   * @param src View to localize.
   * @return The localized view.
   *
   * This copies @a src to the internal @c MemArena and returns a view of the copied data.
   */
  TextView localize(TextView const &src);
};

template <typename P>
P *
Table::add_column(std::unique_ptr<P> &&col) {
  auto prop = col.get();
  auto idx  = _columns.size();
  col->assign_offset(_size);
  col->assign_idx(idx);
  _size += static_cast<Property *>(prop)->size();
  _columns.emplace_back(std::move(col));
  return prop;
}

TextView
Table::localize(TextView const &src) {
  auto span = _arena.alloc(src.size()).rebind<char>();
  memcpy(span, src);
  return span;
}

TextView
Table::token(TextView &line) {
  TextView::size_type idx = 0;
  // Characters of interest.
  static char constexpr separators[2] = {'"', SEP};
  static TextView sep_list{separators, 2};
  bool in_quote_p = false;
  while (idx < line.size()) {
    // Next character of interest.
    idx = line.find_first_of(sep_list, idx);
    if (TextView::npos == idx) { // nothing interesting left, consume all of @a line.
      break;
    } else if ('"' == line[idx]) { // quote, skip it and flip the quote state.
      in_quote_p = !in_quote_p;
      ++idx;
    } else if (SEP == line[idx]) { // separator.
      if (in_quote_p) {            // quoted separator, skip and continue.
        ++idx;
      } else { // found token, finish up.
        break;
      }
    }
  }

  // clip the token from @a src and trim whitespace, quotes
  auto zret = line.take_prefix(idx).trim_if(&isspace).trim('"');
  return zret;
}

bool
Table::parse(TextView src) {
  unsigned line_no = 0;
  while (src) {
    auto line = src.take_prefix_at('\n').ltrim_if(&isspace);
    ++line_no;
    // skip blank and comment lines.
    if (line.empty() || '#' == *line) {
      continue;
    }

    auto range_token = line.take_prefix_at(',');
    IPRange range{range_token};
    if (range.empty()) {
      std::cout << W().print("{} is not a valid range specification.", range_token);
      continue; // This is an error, real code should report it.
    }

    auto span = _arena.alloc(_size).rebind<std::byte>(); // need this broken out.
    Row row{span};                                       // store the original span to preserve it.
    for (auto const &col : _columns) {
      auto token = this->token(line);
      if (col->needs_localized_token()) {
        token = this->localize(token);
      }
      if (!col->parse(token, span.subspan(0, col->size()))) {
        std::cout << W().print("Value \"{}\" at index {} on line {} is invalid.", token, col->idx(), line_no);
      }
      // drop reference to storage used by this column.
      span.remove_prefix(col->size());
    }
    _space.mark(range, std::move(row));
  }
  return true;
}

auto
Table::find(IPAddr const &addr) -> Row * {
  auto spot = _space.find(addr);
  return spot == _space.end() ? nullptr : &(spot->payload());
}

bool
operator==(Table::Row const &, Table::Row const &) {
  return false;
}

MemSpan<std::byte>
Table::Row::span_for(Table::Property const &prop) const {
  return _data.subspan(prop.offset(), prop.size());
}

// ---

/** A set of keys, each of which represents an independent property.
 * The set of keys must be specified at construction, keys not in the list are invalid.
 */
class FlagGroupProperty : public Table::Property {
  using self_type  = FlagGroupProperty; ///< Self reference type.
  using super_type = Table::Property;   ///< Parent type.
public:
  /** Construct with a @a name and a list of @a tags.
   *
   * @param name of the property
   * @param tags List of valid tags that represent attributes.
   *
   * Input tokens must consist of lists of tokens, each of which is one of the @a tags.
   * This is stored so that the exact set of tags present can be retrieved.
   */
  FlagGroupProperty(TextView const &name, std::initializer_list<TextView> tags);

  /** Check for a tag being present.
   *
   * @param idx Tag index, as specified in the constructor tag list.
   * @param row Row data from the @c Table.
   * @return @c true if the tag was present, @c false if not.
   */
  bool is_set(Table::Row const &row, unsigned idx) const;

protected:
  size_t size() const override; ///< Storeage required in a row.

  /** Parse a token.
   *
   * @param token Token to parse (list of tags).
   * @param span Storage for parsed results.
   * @return @c true on a successful parse, @c false if not.
   */
  bool parse(TextView token, MemSpan<std::byte> span) override;
  /// List of tags.
  std::vector<TextView> _tags;
};

/** Enumeration property.
 * The tokens for this property are assumed to be from a limited set of tags. Each token, the
 * value for that row, must be one of those tags. The tags do not need to be specified, but will be
 * accumulated as needed. The property supports a maximum of 255 distinct tags.
 */
class EnumProperty : public Table::Property {
  using self_type  = EnumProperty;    ///< Self reference type.
  using super_type = Table::Property; ///< Parent type.
  using store_type = __uint8_t;       ///< Row storage type.
public:
  using super_type::super_type; ///< Inherit super type constructors.

  /// @return The enumeration tag for this @a row.
  TextView operator()(Table::Row const &row) const;

protected:
  std::vector<TextView> _tags; ///< Tags in the enumeration.

  /// @a return Size of required storage.
  size_t
  size() const override {
    return sizeof(store_type);
  }

  /** Parse a token.
   *
   * @param token Token to parse (an enumeration tag).
   * @param span Storage for parsed results.
   * @return @c true on a successful parse, @c false if not.
   */
  bool parse(TextView token, MemSpan<std::byte> span) override;
};

class StringProperty : public Table::Property {
  using self_type  = StringProperty;
  using super_type = Table::Property;

public:
  static constexpr size_t SIZE = sizeof(TextView);
  using super_type::super_type;

protected:
  size_t
  size() const override {
    return SIZE;
  }
  bool parse(TextView token, MemSpan<std::byte> span) override;
  bool
  needs_localized_token() const override {
    return true;
  }
};

// ---
bool
StringProperty::parse(TextView token, MemSpan<std::byte> span) {
  memcpy(span.data(), &token, sizeof(token));
  return true;
}

FlagGroupProperty::FlagGroupProperty(TextView const &name, std::initializer_list<TextView> tags) : super_type(name) {
  _tags.reserve(tags.size());
  for (auto const &tag : tags) {
    _tags.emplace_back(tag);
  }
}

bool
FlagGroupProperty::parse(TextView token, MemSpan<std::byte> span) {
  if ("-"_tv == token) {
    return true;
  } // marker for no flags.
  memset(span, 0);
  while (token) {
    auto tag   = token.take_prefix_at(';');
    unsigned j = 0;
    for (auto const &key : _tags) {
      if (0 == strcasecmp(key, tag)) {
        span[j / 8] |= (std::byte{1} << (j % 8));
        break;
      }
      ++j;
    }
    if (j > _tags.size()) {
      std::cout << W().print("Tag \"{}\" is not recognized.", tag);
      return false;
    }
  }
  return true;
}

bool
FlagGroupProperty::is_set(Table::Row const &row, unsigned idx) const {
  auto sp = row.span_for(*this);
  return std::byte{0} != ((sp[idx / 8] >> (idx % 8)) & std::byte{1});
}

size_t
FlagGroupProperty::size() const {
  return swoc::Scalar<8>(swoc::round_up(_tags.size())).count();
}

bool
EnumProperty::parse(TextView token, MemSpan<std::byte> span) {
  // Already got one?
  auto spot = std::find_if(_tags.begin(), _tags.end(), [&](TextView const &tag) { return 0 == strcasecmp(token, tag); });
  if (spot == _tags.end()) { // nope, add it to the list.
    _tags.push_back(token);
    spot = std::prev(_tags.end());
  }
  span.rebind<uint8_t>()[0] = spot - _tags.begin();
  return true;
}

TextView
EnumProperty::operator()(Table::Row const &row) const {
  auto idx = row.span_for(*this).rebind<store_type>()[0];
  return _tags[idx];
}

// ---

TEST_CASE("IPSpace properties", "[libswoc][ip][ex][properties]") {
  Table table;
  auto flag_names                   = {"prod"_tv, "dmz"_tv, "internal"_tv};
  auto owner                        = table.add_column(std::make_unique<EnumProperty>("owner"));
  auto colo                         = table.add_column(std::make_unique<EnumProperty>("colo"));
  auto flags                        = table.add_column(std::make_unique<FlagGroupProperty>("flags"_tv, flag_names));
  [[maybe_unused]] auto description = table.add_column(std::make_unique<StringProperty>("Description"));

  TextView src = R"(10.1.1.0/24,asf,cmi,prod;internal,"ASF core net"
192.168.28.0/25,asf,ind,prod,"Indy Net"
192.168.28.128/25,asf,abq,dmz;internal,"Albuquerque zone"
)";

  REQUIRE(true == table.parse(src));
  REQUIRE(3 == table.size());
  auto row = table.find(IPAddr{"10.1.1.56"});
  REQUIRE(nullptr != row);
  CHECK(true == flags->is_set(*row, 0));
  CHECK(false == flags->is_set(*row, 1));
  CHECK(true == flags->is_set(*row, 2));
  CHECK("asf"_tv == (*owner)(*row));

  row = table.find(IPAddr{"192.168.28.131"});
  REQUIRE(row != nullptr);
  CHECK("abq"_tv == (*colo)(*row));
};
