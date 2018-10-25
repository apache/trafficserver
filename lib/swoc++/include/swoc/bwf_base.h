/** @file

    Basic formatting support for @c BufferWriter.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#pragma once

#include <cstdlib>
#include <utility>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <string>
#include <iosfwd>
#include <string_view>
#include <tuple>
#include <any>

#include "swoc/TextView.h"
#include "swoc/MemSpan.h"
#include "swoc/MemArena.h"
#include "swoc/BufferWriter.h"
#include "swoc/swoc_meta.h"

namespace swoc
{
namespace bwf
{
  /** Parsed version of a format specifier.
   *
   * Literals are represented as an instance of this class, with the type set to
   * @c LITERAL_TYPE and the literal text in the @a _ext field.
   */
  struct Spec {
    using self_type = Spec; ///< Self reference type.

    static constexpr char DEFAULT_TYPE = 'g'; ///< Default format type.
    static constexpr char INVALID_TYPE = 0;   ///< Type for missing or invalid specifier.
    static constexpr char LITERAL_TYPE = '"'; ///< Internal type to mark a literal.
    static constexpr char CAPTURE_TYPE = 1;   ///< Internal type to mark a capture.

    static constexpr char SIGN_ALWAYS = '+'; ///< Always print a sign character.
    static constexpr char SIGN_NEVER  = ' '; ///< Never print a sign character.
    static constexpr char SIGN_NEG    = '-'; ///< Print a sign character only for negative values (default).

    /// Constructor a default instance.
    constexpr Spec() {}

    /// Construct by parsing @a fmt.
    Spec(const TextView &fmt);
    /// Parse a specifier
    bool parse(TextView fmt);

    char _fill = ' ';      ///< Fill character.
    char _sign = SIGN_NEG; ///< Numeric sign style.
    enum class Align : char {
      NONE,                            ///< No alignment.
      LEFT,                            ///< Left alignment '<'.
      RIGHT,                           ///< Right alignment '>'.
      CENTER,                          ///< Center alignment '^'.
      SIGN                             ///< Align plus/minus sign before numeric fill. '='
    } _align           = Align::NONE;  ///< Output field alignment.
    char _type         = DEFAULT_TYPE; ///< Type / radix indicator.
    bool _radix_lead_p = false;        ///< Print leading radix indication.
    // @a _min is unsigned because there's no point in an invalid default, 0 works fine.
    unsigned int _min = 0;                                        ///< Minimum width.
    int _prec         = -1;                                       ///< Precision
    unsigned int _max = std::numeric_limits<unsigned int>::max(); ///< Maximum width
    int _idx          = -1;                                       ///< Positional "name" of the specification.
    std::string_view _name;                                       ///< Name of the specification.
    std::string_view _ext;                                        ///< Extension if provided.

    /// Global default instance for use in situations where a format specifier isn't available.
    static const self_type DEFAULT;

    /// Validate @a c is a specifier type indicator.
    static bool is_type(char c);

    /// Check if the type flag is numeric.
    static bool is_numeric_type(char c);

    /// Check if the type is an upper case variant.
    static bool is_upper_case_type(char c);

    /// Check if the type @a in @a this is numeric.
    bool has_numeric_type() const;

    /// Check if the type in @a this is an upper case variant.
    bool has_upper_case_type() const;

    /// Check if the type is a raw pointer.
    bool has_pointer_type() const;

    /// Check if the type is valid.
    bool has_valid_type() const;

  protected:
    /// Validate character is alignment character and return the appropriate enum value.
    Align align_of(char c);

    /// Validate is sign indicator.
    bool is_sign(char c);

    /// Handrolled initialization the character syntactic property data.
    static const struct Property {
      Property(); ///< Default constructor, creates initialized flag set.
      /// Flag storage, indexed by character value.
      uint8_t _data[0x100];
      /// Flag mask values.
      static constexpr uint8_t ALIGN_MASK        = 0x0F; ///< Alignment type.
      static constexpr uint8_t TYPE_CHAR         = 0x10; ///< A valid type character.
      static constexpr uint8_t UPPER_TYPE_CHAR   = 0x20; ///< Upper case flag.
      static constexpr uint8_t NUMERIC_TYPE_CHAR = 0x40; ///< Numeric output.
      static constexpr uint8_t SIGN_CHAR         = 0x80; ///< Is sign character.
    } _prop;
  };

  /** Format string support.
   *
   * This contains the parsing logic for format strings and also serves as the type for pre-compiled
   * format string.
   *
   * When used by the print formatting logic, there is an abstraction layer, "extraction", which
   * performs the equivalent of the @c parse method. This allows the formatting to treat pre-compiled
   * or immediately parsed format strings the same. It also enables providing any parser that can
   * deliver literals and @c Spec instances.
   */
  class Format
  {
  public:
    /// Construct from a format string @a fmt.
    Format(TextView fmt);

    /// Extraction support for TextView.
    struct TextViewExtractor {
      TextView _fmt;
      explicit operator bool() const;
      bool operator()(std::string_view &literal_v, Spec &spec);

      /** Parse elements of a format string.

          @param fmt The format string [in|out]
          @param literal A literal if found
          @param spec A specifier if found (less enclosing braces)
          @return @c true if a specifier was found, @c false if not.

          Pull off the next literal and/or specifier from @a fmt. The return value distinguishes
          the case of no specifier found (@c false) or an empty specifier (@c true).

       */
      static bool parse(TextView &fmt, std::string_view &literal, std::string_view &spec);
    };
    /// Wrap the format string in an extractor.
    static TextViewExtractor bind(TextView fmt);

    /// Extraction support for pre-parsed format strings.
    struct FormatExtractor {
      const std::vector<Spec> &_fmt; ///< Parsed format string.
      int _idx = 0;                  ///< Element index.
      explicit operator bool() const;
      bool operator()(std::string_view &literal_v, Spec &spec);
    };
    /// Wrap the format instance in an extractor.
    FormatExtractor bind() const;

  protected:
    /// Default constructor for use by subclasses with alternate formatting.
    Format() = default;

    std::vector<Spec> _items; ///< Items from format string.
  };

  // Name binding - support for having format specifier names.

  /// Generic name generator signature.
  using BoundNameSignature = BufferWriter &(BufferWriter &, Spec const &);

  /** Protocol class for handling bound names.
   *
   * This is an API facade for names that are fully bound and do not need any data / context
   * beyond that of the @c BufferWriter. It is expected other name collections will subclass
   * this to pass to the formatting logic.
   */
  class BoundNames
  {
  public:
    virtual ~BoundNames();
    /** Generate output text for @a name on the output @a w using the format specifier @a spec.
     * This must match the @c BoundNameSignature type.
     *
     * @param w Output stream.
     * @param spec Parsed format specifier.
     *
     * @note The tag name can be found in @c spec._name.
     *
     * @return
     */
    virtual BufferWriter &operator()(BufferWriter &w, Spec const &spec) const = 0;

    /** Capture an argument.
     *
     * @param w Output.
     * @param spec Capturing specifier.
     * @param arg The captured argument.
     *
     * @note This is really for C / printf support where some specifiers are dependent on values
     * passed in other arguments.
     */
    virtual void capture(BufferWriter &w, Spec const &spec, std::any const &arg) const;

  protected:
    /// Write missing name output.
    BufferWriter &err_invalid_name(BufferWriter &w, Spec const &) const;
  };

  /// Empty bound names - used for where no name binding is available or desired.
  /// Throws if any name is used.
  class NilBoundNames : public BoundNames
  {
  public:
    BufferWriter &operator()(BufferWriter &, Spec const &) const override;
  };

  /** Binding names to generators.
   *  @tparam F The function signature for generators in this container.
   *
   * This is a base class used by different types of name containers. It is not expected to be used
   * directly.
   */
  template <typename F> class NameBinding
  {
  private:
    using self_type = NameBinding; ///< self reference type.
  public:
    using Generator = std::function<F>;

    /// Construct an empty name set.
    NameBinding();
    /// Construct and assign the names and generators in @a list
    NameBinding(std::initializer_list<std::tuple<std::string_view, const Generator &>> list);

    /** Assign the @a generator to the @a name.
     *
     * @param name Name associated with the @a generator.
     * @param generator The generator function.
     */
    self_type &assign(std::string_view name, const Generator &generator);

  protected:
    /// Copy @a name in to local storage and return a view of it.
    std::string_view localize(std::string_view name);

    using Map = std::unordered_map<std::string_view, Generator>;
    Map _map;              ///< Mapping of name -> generator
    MemArena _arena{1024}; ///< Local name storage.
  };

  /** A class to hold global name bindings.
   *
   * These names access global data and therefore have no context. An instance of this is used
   * as the default if no explicit name set is provided.
   */
  class GlobalNames : public NameBinding<BoundNameSignature>
  {
    using self_type  = GlobalNames;
    using super_type = NameBinding<BoundNameSignature>;
    using Map        = super_type::Map;

  public:
    using super_type::super_type;
    /// Provide an accessor for formatting.
    BoundNames &bind();

  protected:
    class Binding : public BoundNames
    {
    public:
      Binding(const super_type::Map &map);
      BufferWriter &operator()(BufferWriter &w, const Spec &spec) const override;

    protected:
      const Map &_map;
    } _binding{super_type::_map};
  };

  /** Binding for context based names.
   *
   * @tparam T The context type. This is used directly. If the context needs to be @c const
   * then this parameter should make that explicit, e.g. @c Names<const Context>. This
   * paramater is accessible via the @c context_type alias.
   *
   * This enables named format specifications, such as "{tag}", generated from a context of type @a
   * T. Each supported tag requires a @a Generator which is a functor of type
   *
   * @code
   *   BufferWriter & generator(BufferWriter & w, const Spec & spec, T & context);
   * @endcode
   */
  template <typename T> class ContextNames : public NameBinding<BufferWriter &(BufferWriter &, const Spec &, T &)>
  {
  private:
    using self_type  = ContextNames; ///< self reference type.
    using super_type = NameBinding<BufferWriter &(BufferWriter &, const Spec &, T &)>;

  public:
    using context_type = T; ///< Export for external convenience.
    /// Functional type for a generator.
    using Generator      = typename super_type::Generator;
    using BoundGenerator = std::function<BoundNameSignature>;

    using super_type::super_type; // inherit @c super_type constructors.

    /** Assign the bound generator @a bg to @a name.
     *
     * This is used for generators in the namespace that do not require the context.
     *
     * @param name Name associated with the generator.
     * @param bg A bound generator that requires no context.
     * @return @c *this
     */
    self_type &assign(std::string_view name, const BoundGenerator &bg);

    /// Inherit unbound generator assignment from the @c super_type.
    using super_type::assign;

    /** Bind the names to a specific @a context.
     *
     * @param context The instance of @a T to use in the generators.
     * @return A reference to an internal instance of a subclass of the protocol class @c BoundNames.
     */
    const BoundNames &bind(context_type &context);

  protected:
    using Map = typename super_type::Map;
    /// Subclass of @a BoundNames used to bind this set of names to a context.
    class Binding : public BoundNames
    {
      using self_type  = Binding;
      using super_type = BoundNames;

    public:
      /// Invoke the generator for @a name.
      BufferWriter &operator()(BufferWriter &w, const Spec &spec) const override;

    protected:
      Binding(Map const &map); ///< Must have a map reference.
      self_type &assign(context_type *);

      Map const &_map;              ///< The mapping for name look ups.
      context_type *_ctx = nullptr; ///< Context for generators.

      friend ContextNames;
    } _binding{super_type::_map};
  };

  /** Default global names.
   * This nameset is used if no other is provided. Therefore bindings added to this nameset will be
   * available in the default formatting use.
   */
  extern GlobalNames Global_Names;

  // --------------- Implementation --------------------
  /// --- Spec ---

  inline Spec::Align
  Spec::align_of(char c)
  {
    return static_cast<Align>(_prop._data[static_cast<unsigned>(c)] & Property::ALIGN_MASK);
  }

  inline bool
  Spec::is_sign(char c)
  {
    return _prop._data[static_cast<unsigned>(c)] & Property::SIGN_CHAR;
  }

  inline bool
  Spec::is_type(char c)
  {
    return _prop._data[static_cast<unsigned>(c)] & Property::TYPE_CHAR;
  }

  inline bool
  Spec::is_upper_case_type(char c)
  {
    return _prop._data[static_cast<unsigned>(c)] & Property::UPPER_TYPE_CHAR;
  }

  inline bool
  Spec::is_numeric_type(char c)
  {
    return _prop._data[static_cast<unsigned>(c)] & Property::NUMERIC_TYPE_CHAR;
  }

  inline bool
  Spec::has_numeric_type() const
  {
    return _prop._data[static_cast<unsigned>(_type)] & Property::NUMERIC_TYPE_CHAR;
  }

  inline bool
  Spec::has_upper_case_type() const
  {
    return _prop._data[static_cast<unsigned>(_type)] & Property::UPPER_TYPE_CHAR;
  }

  inline bool
  Spec::has_pointer_type() const
  {
    return _type == 'p' || _type == 'P';
  }

  inline bool
  Spec::has_valid_type() const
  {
    return _type != INVALID_TYPE;
  }

  inline auto
  Format::bind(swoc::TextView fmt) -> TextViewExtractor
  {
    return {fmt};
  }

  inline auto
  Format::bind() const -> FormatExtractor
  {
    return {_items};
  }

  inline Format::TextViewExtractor::operator bool() const { return !_fmt.empty(); }
  inline Format::FormatExtractor::operator bool() const { return _idx < static_cast<int>(_fmt.size()); }

  /// --- Names / Generators ---

  // Base implementation does nothing as this is rarely used.
  inline void
  BoundNames::capture(BufferWriter &, swoc::bwf::Spec const &, std::any const &) const
  {
  }

  inline BufferWriter &
  BoundNames::err_invalid_name(BufferWriter &w, const Spec &spec) const
  {
    return w.print("{{~{}~}}", spec._name);
  }

  inline BufferWriter &
  NilBoundNames::operator()(BufferWriter &, bwf::Spec const &) const
  {
    throw std::runtime_error("Use of nil bound names in BW formating");
  }

  template <typename T> inline ContextNames<T>::Binding::Binding(Map const &map) : _map(map) {}

  template <typename T>
  inline const BoundNames &
  ContextNames<T>::bind(context_type &ctx)
  {
    return _binding.assign(&ctx);
  }

  template <typename T>
  inline auto
  ContextNames<T>::Binding::assign(context_type *ctx) -> self_type &
  {
    _ctx = ctx;
    return *this;
  }

  template <typename T>
  BufferWriter &
  ContextNames<T>::Binding::operator()(BufferWriter &w, const Spec &spec) const
  {
    if (!spec._name.empty()) {
      if (auto spot = _map.find(spec._name); spot != _map.end()) {
        spot->second(w, spec, *_ctx);
      } else {
        this->err_invalid_name(w, spec);
      }
    }
    return w;
  }

  template <typename F> NameBinding<F>::NameBinding() {}

  template <typename F> NameBinding<F>::NameBinding(std::initializer_list<std::tuple<std::string_view, const Generator &>> list)
  {
    for (auto &&[name, generator] : list) {
      this->assign(name, generator);
    }
  }

  template <typename F>
  std::string_view
  NameBinding<F>::localize(std::string_view name)
  {
    auto span = _arena.alloc(name.size());
    memcpy(span.data(), name.data(), name.size());
    return span.view();
  }

  template <typename F>
  auto
  NameBinding<F>::assign(std::string_view name, const Generator &generator) -> self_type &
  {
    name       = this->localize(name);
    _map[name] = generator;
    return *this;
  }

  inline GlobalNames::Binding::Binding(const super_type::Map &map) : _map(map) {}

  inline BufferWriter &
  GlobalNames::Binding::operator()(BufferWriter &w, const Spec &spec) const
  {
    if (!spec._name.empty()) {
      if (auto spot = _map.find(spec._name); spot != _map.end()) {
        spot->second(w, spec);
      } else {
        this->err_invalid_name(w, spec);
      }
    }
    return w;
  }

  inline BoundNames &
  GlobalNames::bind()
  {
    return _binding;
  }

  /// --- Formatting ---

  /// Internal signature for template generated formatting.
  /// @a args is a forwarded tuple of arguments to be processed.
  template <typename TUPLE> using ArgFormatterSignature = BufferWriter &(*)(BufferWriter &w, Spec const &, TUPLE const &args);

  /// Internal error / reporting message generators
  void Err_Bad_Arg_Index(BufferWriter &w, int i, size_t n);

  // MSVC will expand the parameter pack inside a lambda but not gcc, so this indirection is required.

  /// This selects the @a I th argument in the @a TUPLE arg pack and calls the formatter on it. This
  /// (or the equivalent lambda) is needed because the array of formatters must have a homogenous
  /// signature, not vary per argument. Effectively this indirection erases the type of the specific
  /// argument being formatted. Instances of this have the signature @c ArgFormatterSignature.
  template <typename TUPLE, size_t I>
  BufferWriter &
  Arg_Formatter(BufferWriter &w, Spec const &spec, TUPLE const &args)
  {
    return bwformat(w, spec, std::get<I>(args));
  }

  /// This exists only to expand the index sequence into an array of formatters for the tuple type
  /// @a TUPLE.  Due to langauge limitations it cannot be done directly. The formatters can be
  /// accessed via standard array access in contrast to templated tuple access. The actual array is
  /// static and therefore at run time the only operation is loading the address of the array.
  template <typename TUPLE, size_t... N>
  ArgFormatterSignature<TUPLE> *
  Get_Arg_Formatter_Array(std::index_sequence<N...>)
  {
    static ArgFormatterSignature<TUPLE> fa[sizeof...(N)] = {&bwf::Arg_Formatter<TUPLE, N>...};
    return fa;
  }

  /// Perform alignment adjustments / fill on @a w of the content in @a lw.
  /// This is the normal mechanism, in cases where the length can be known or limited before
  /// conversion, it can be more efficient to work in a temporary local buffer and copy out
  /// as neeed without moving data in the output buffer.
  void Adjust_Alignment(BufferWriter &aux, Spec const &spec);

  /// Generic integral conversion.
  BufferWriter &Format_Integer(BufferWriter &w, Spec const &spec, uintmax_t n, bool negative_p);

  /// Generic floating point conversion.
  BufferWriter &Format_Float(BufferWriter &w, Spec const &spec, double n, bool negative_p);

  /* Capture support, which allows format extractors to capture arguments and consume them.
   * This was built in order to support C style formatting, which needs to capture arguments
   * to set the minimum width and/or the precision of other arguments.
   *
   * The key component is the ability to dynamically access an element of a tuple using
   * @c std::any.
   *
   * Note: Much of this was originally in the meta support but it caused problems in use if
   * the tuple header wasn't also included. I was unable to determine why, as this code doesn't
   * depend on tuple explicitly.
   */
  /// The signature for accessing an element of a tuple.
  template <typename T> using TupleAccessorSignature = std::any (*)(T const &t);
  /// Template access method.
  template <size_t IDX, typename T>
  std::any
  TupleAccessor(T const &t)
  {
    return std::any(&std::get<IDX>(t));
  }
  /// Create and return an array of specialized accessors, indexed by tuple index.
  template <typename T, size_t... N>
  std::array<TupleAccessorSignature<T>, sizeof...(N)> &
  Tuple_Accessor_Array(std::index_sequence<N...>)
  {
    static std::array<TupleAccessorSignature<T>, sizeof...(N)> accessors = {&TupleAccessor<N>...};
    return accessors;
  }
  /// Get the Nth element of the tuple as @c std::any.
  template <typename T>
  std::any
  Tuple_Nth(T const &t, size_t idx)
  {
    return Tuple_Accessor_Array<T>(std::make_index_sequence<std::tuple_size<T>::value>())[idx](t);
  }
  /// If capture is used, the format extractor must provide a @c capture method. This isn't required
  /// so make it compile time optional, but throw if the extractor sets up for capture and didn't
  /// provide one.
  template <typename F>
  auto
  arg_capture(F &&f, BufferWriter &, Spec const &, std::any &&, swoc::meta::CaseTag<0>) -> void
  {
    throw std::runtime_error("Capture specification used in format extractor that does not support capture");
  }
  template <typename F>
  auto
  arg_capture(F &&f, BufferWriter &w, Spec const &spec, std::any &&value, swoc::meta::CaseTag<1>)
    -> decltype(f.capture(w, spec, value))
  {
    return f.capture(w, spec, value);
  }

} // namespace bwf

/* [Need to clip this out and put it in Sphinx
 *
 * The format parser :arg:`F` performs parsing of the format specifier, which is presumed to be
 * bound to this instance of :arg:`F`. The parser is called as a functor and must have a function
 * method with the signature
 *
 * bool (string_view & literal_v, bwf::Spec & spec)
 *
 * The parser must parse out to the next specifier in the format, or the end of the format if there
 * are no more specifiers. If the format is exhausted this should return @c false. A return of @c true
 * indicates there is either a literal, a specifier, or both.
 *
 * When a literal is found, it should be returned in :arg:`literal_v`. If a specifier is found and
 * parsed, it should be put in :arg:`spec`. Both of these *must* be cleared if the corresponding
 * data is not found in the incremental parse of the format.
 */

// This is the real printing logic, all other variants pack up their arguments and send them here.
template <typename F, typename... Args>
BufferWriter &
BufferWriter::print_nv(bwf::BoundNames const &names, F &&f, std::tuple<Args...> const &args)
{
  using namespace std::literals;
  static constexpr int N = sizeof...(Args); // used as loop limit
  static const auto fa   = bwf::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Args...>{});
  int arg_idx            = 0; // the next argument index to be processed.

  // Parser is required to return @c false if there's no more data, @c true if something was parsed.
  while (f) {
    std::string_view lit_v;
    bwf::Spec spec;
    bool spec_p = f(lit_v, spec);
    if (lit_v.size()) {
      this->write(lit_v);
    }

    if (spec_p) {
      size_t width = this->remaining();
      if (spec._max < width) {
        width = spec._max;
      }
      FixedBufferWriter lw{this->aux_data(), width};

      if (spec._name.size() == 0) {
        spec._idx = arg_idx++;
      }
      if (0 <= spec._idx) {
        if (spec._idx < N) {
          if (spec._type == bwf::Spec::CAPTURE_TYPE) {
            bwf::arg_capture(f, lw, spec, bwf::Tuple_Nth(args, static_cast<size_t>(spec._idx)), swoc::meta::CaseArg);
          } else {
            fa[spec._idx](lw, spec, args);
          }
        } else {
          bwf::Err_Bad_Arg_Index(lw, spec._idx, N);
        }
      } else if (spec._name.size()) {
        names(lw, spec);
      }
      if (lw.extent()) {
        bwf::Adjust_Alignment(lw, spec);
        this->commit(lw.extent());
      }
    }
  }
  return *this;
}

template <typename... Args>
BufferWriter &
BufferWriter::print(const TextView &fmt, Args &&... args)
{
  return this->print_nv(bwf::Global_Names.bind(), bwf::Format::bind(fmt), std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::print(bwf::Format const &fmt, Args &&... args)
{
  return this->print_nv(bwf::Global_Names.bind(), fmt.bind(), std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::printv(TextView const &fmt, std::tuple<Args...> const &args)
{
  return this->print_nv(bwf::Global_Names.bind(), bwf::Format::bind(fmt), args);
}

template <typename... Args>
BufferWriter &
BufferWriter::printv(const bwf::Format &fmt, const std::tuple<Args...> &args)
{
  return this->print_nv(bwf::Global_Names.bind(), fmt.bind(), args);
}

template <typename F>
BufferWriter &
BufferWriter::print_nv(const bwf::BoundNames &names, F &&f)
{
  return print_nv(names, f, std::make_tuple());
}

// ---- Formatting for specific types.

// Pointers that are not specialized.
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const void *ptr)
{
  bwf::Spec ptr_spec{spec};
  ptr_spec._radix_lead_p = true;
  if (ptr_spec._type == bwf::Spec::DEFAULT_TYPE || ptr_spec._type == 'p') {
    ptr_spec._type = 'x'; // if default or 'p;, switch to lower hex.
  } else if (ptr_spec._type == 'P') {
    ptr_spec._type = 'X'; // P means upper hex, overriding other specializations.
  }
  return bwf::Format_Integer(w, ptr_spec, reinterpret_cast<intptr_t>(ptr), false);
}

// MemSpan
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, MemSpan<void> const &span);

template <typename T>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, MemSpan<T> const &span)
{
  bwf::Spec s{spec};
  // If the precision isn't already specified, make it the size of the objects in the span.
  // This will break the output into blocks of that size.
  if (spec._prec <= 0) {
    s._prec = sizeof(T);
  }
  return bwformat(w, s, span.template rebind<void>());
};
// -- Common formatters --

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, std::string_view sv);

template <size_t N>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const char (&a)[N])
{
  return bwformat(w, spec, std::string_view(a, N - 1));
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const char *v)
{
  if (spec._type == 'x' || spec._type == 'X') {
    bwformat(w, spec, static_cast<const void *>(v));
  } else {
    bwformat(w, spec, std::string_view(v));
  }
  return w;
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, TextView tv)
{
  return bwformat(w, spec, static_cast<std::string_view>(tv));
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::string const &s)
{
  return bwformat(w, spec, std::string_view{s});
}

template <typename F>
auto
bwformat(BufferWriter &w, bwf::Spec const &spec, F &&f) ->
  typename std::enable_if<std::is_floating_point<typename std::remove_reference<F>::type>::value, BufferWriter &>::type
{
  return f < 0 ? bwf::Format_Float(w, spec, -f, true) : bwf::Format_Float(w, spec, f, false);
}

/* Integer types.

   Due to some oddities for MacOS building, need a bit more template magic here. The underlying
   integer rendering is in @c Format_Integer which takes @c intmax_t or @c uintmax_t. For @c
   bwformat templates are defined, one for signed and one for unsigned. These forward their argument
   to the internal renderer. To avoid additional ambiguity the template argument is checked with @c
   std::enable_if to invalidate the overload if the argument type isn't a signed / unsigned
   integer. One exception to this is @c char which is handled by a previous overload in order to
   treat the value as a character and not an integer. The overall benefit is this works for any set
   of integer types, rather tuning and hoping to get just the right set of overloads.
 */

template <typename I>
auto
bwformat(BufferWriter &w, bwf::Spec const &spec, I &&i) ->
  typename std::enable_if<std::is_unsigned<typename std::remove_reference<I>::type>::value &&
                            std::is_integral<typename std::remove_reference<I>::type>::value,
                          BufferWriter &>::type
{
  return bwf::Format_Integer(w, spec, i, false);
}

template <typename I>
auto
bwformat(BufferWriter &w, bwf::Spec const &spec, I &&i) ->
  typename std::enable_if<std::is_signed<typename std::remove_reference<I>::type>::value &&
                            std::is_integral<typename std::remove_reference<I>::type>::value,
                          BufferWriter &>::type
{
  bool neg_p  = false;
  uintmax_t n = static_cast<uintmax_t>(i);
  if (i < 0) {
    n     = static_cast<uintmax_t>(-i);
    neg_p = true;
  }
  return bwf::Format_Integer(w, spec, n, neg_p);
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, char c)
{
  return w.write(c);
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bool f)
{
  using namespace std::literals;
  if ('s' == spec._type) {
    w.write(f ? "true"sv : "false"sv);
  } else if ('S' == spec._type) {
    w.write(f ? "TRUE"sv : "FALSE"sv);
  } else {
    bwf::Format_Integer(w, spec, static_cast<uintmax_t>(f), false);
  }
  return w;
}

// std::string support
/** Print to a @c std::string

    Print to the string @a s. If there is overflow then resize the string sufficiently to hold the output
    and print again. The effect is the string is resized only as needed to hold the output.
 */
template <typename... Args>
std::string &
bwprintv(std::string &s, TextView fmt, std::tuple<Args...> const &args)
{
  auto len = s.size(); // remember initial size
  size_t n = FixedBufferWriter(const_cast<char *>(s.data()), s.size()).printv(fmt, std::move(args)).extent();
  s.resize(n);   // always need to resize - if shorter, must clip pre-existing text.
  if (n > len) { // dropped data, try again.
    FixedBufferWriter(const_cast<char *>(s.data()), s.size()).printv(fmt, std::move(args));
  }
  return s;
}

template <typename... Args>
std::string &
bwprint(std::string &s, TextView fmt, Args &&... args)
{
  return bwprintv(s, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
inline auto
FixedBufferWriter::print(TextView fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
inline auto
FixedBufferWriter::printv(TextView fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, args));
}

template <typename... Args>
inline auto
FixedBufferWriter::print(bwf::Format const &fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
inline auto
FixedBufferWriter::printv(bwf::Format const &fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, args));
}

// Special case support for @c Scalar, because @c Scalar is a base utility for some other utilities
// there can be some unpleasant cirularities if @c Scalar includes BufferWriter formatting. If the
// support is here then it's fine because anything using BWF for @c Scalar must include this header.
template <intmax_t N, typename C, typename T> class Scalar;
namespace detail
{
  template <typename T>
  auto
  tag_label(BufferWriter &w, const bwf::Spec &, meta::CaseTag<0>) -> void
  {
  }

  template <typename T>
  auto
  tag_label(BufferWriter &w, const bwf::Spec &, meta::CaseTag<1>) -> decltype(T::label, meta::CaseVoidFunc())
  {
    w.print("{}", T::label);
  }
} // namespace detail

template <intmax_t N, typename C, typename T>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, Scalar<N, C, T> const &x)
{
  bwformat(w, spec, x.value());
  if (!spec.has_numeric_type()) {
    detail::tag_label<T>(w, spec, meta::CaseArg);
  }
  return w;
}

// Generically a stream operator is a formatter with the default specification.
template <typename V>
BufferWriter &
operator<<(BufferWriter &w, V &&v)
{
  return bwformat(w, bwf::Spec::DEFAULT, std::forward<V>(v));
}

} // namespace swoc
