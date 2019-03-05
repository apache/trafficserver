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
#include <functional>
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
    /// Parse a specifier.
    /// @a this is reset to default and then updated in accordance with @a fmt.
    bool parse(TextView fmt);

    char _fill = ' ';      ///< Fill character.
    char _sign = SIGN_NEG; ///< Numeric sign style.
    /// Flag for how to align the output inside a limited width field.
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
   * performs the equivalent of the @c parse method. This allows the formatting to treat
   * pre-compiled or immediately parsed format strings the same. It also enables formatted print
   * support any parser that can deliver literals and @c Spec instances.
   */
  class Format
  {
  public:
    /// Construct from a format string @a fmt.
    Format(TextView fmt);

    /// Extraction support for TextView.
    struct TextViewExtractor {
      TextView _fmt; ///< Format string.

      /// @return @c true if more format string, @c false if none.
      explicit operator bool() const;

      /** Extract next formatting elements.
       *
       * @param literal_v [out] The next literal.
       * @param spec [out] The next format specifier.
       * @return @c true if @a spec was filled out, @c false if no specifier was found.
       */
      bool operator()(std::string_view &literal_v, Spec &spec);

      /** Parse elements of a format string.

          @param fmt The format string [in|out]
          @param literal A literal if found
          @param specifier A specifier if found (less enclosing braces)
          @return @c true if a specifier was found, @c false if not.

          Pull off the next literal and/or specifier from @a fmt. The return value distinguishes
          the case of no specifier found (@c false) or an empty specifier (@c true).

       */
      static bool parse(TextView &fmt, std::string_view &literal, std::string_view &specifier);
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

  /** Signature for a functor bound to a name.
   *
   * @param w The output.
   * @param spec The format specifier.
   *
   * The functor is expected to write to @a w based on @a spec.
   */
  using ExternalGeneratorSignature = BufferWriter &(BufferWriter &w, Spec const &spec);

  /** Base class for implementing a name binding functor.
   *
   * This expected to be inherited by other classes that provide the name binding service.
   * It does a few small but handy things.
   *
   * - Force a virtual destructor.
   * - Force the implementation of the binding method by declaring it as a pure virtual.
   * - Provide a standard "missing name" method.
   */
  class NameBinding
  {
  public:
    virtual ~NameBinding(); ///< Force virtual destructor.

    /** Generate output text for @a name on the output @a w using the format specifier @a spec.
     * This must match the @c BoundNameSignature type.
     *
     * @param w Output stream.
     * @param spec Parsed format specifier.
     *
     * @note The tag name can be found in @c spec._name.
     *
     * @return @a w
     */
    virtual BufferWriter &operator()(BufferWriter &w, Spec const &spec) const = 0;

  protected:
    /** Standardized missing name method.
     *
     * @param w The destination buffer.
     * @param spec Format specifier, used to determine the invalid name.
     * @return @a w
     */
    static BufferWriter &err_invalid_name(BufferWriter &w, Spec const &spec);
  };

  /** An explicitly empty set of bound names.
   *
   * To simplify the overall implementation, a name binding is always required to format output.
   * This class is used in situations where there is no available binding or such names would not be
   * useful. This class with @c throw on any attempt to use a name.
   */
  class NilBinding : public NameBinding
  {
  public:
    /// Do name based formatted output.
    /// This always throws an exception.
    BufferWriter &operator()(BufferWriter &, Spec const &) const override;
  };

  /** Associate generators with names.
   *
   *  @tparam F The function signature for generators in this container.
   *
   * This is a base class used by different types of name containers. It is not expected to be used
   * directly. The subclass should provide a function type @a F that is suitable for its particular
   * generators.
   */
  template <typename F> class NameMap
  {
  private:
    using self_type = NameMap; ///< self reference type.
  public:
    /// Signature for generators.
    using Generator = std::function<F>;

    /// Construct an empty container.
    NameMap();
    /// Construct and assign the names and generators in @a list
    NameMap(std::initializer_list<std::tuple<std::string_view, Generator const &>> list);

    /** Assign the @a generator to the @a name.
     *
     * @param name Name associated with the @a generator.
     * @param generator The generator function.
     */
    self_type &assign(std::string_view const &name, Generator const &generator);

  protected:
    /// Copy @a name in to local storage and return a view of it.
    std::string_view localize(std::string_view const &name);

    using Map = std::unordered_map<std::string_view, Generator>;
    Map _map;              ///< Mapping of name -> generator
    MemArena _arena{1024}; ///< Local name storage.
  };

  /** A class to hold external / context-free name bindings.
   *
   * These names access external data and therefore have no context. An externally accessible
   * singleton instance of this is used as the default if no explicit name set is provided. This
   * enables the executable to establish a set of global names to be used.
   */
  class ExternalNames : public NameMap<ExternalGeneratorSignature>, public NameBinding
  {
    using self_type  = ExternalNames;                       ///< Self reference type.
    using super_type = NameMap<ExternalGeneratorSignature>; ///< Super class.
    using Map        = super_type::Map;                     ///< Inherit from superclass.

  public:
    using super_type::super_type; // import constructors.

    /// The bound accessor is this class.
    NameBinding const &bind() const;

    /// Bound name access.
    BufferWriter &operator()(BufferWriter &w, const Spec &spec) const override;

    /// @copydoc NameMap::assign(std::string_view const &name, Generator const &generator)
  };

  /** Associate names with context dependent generators.
   *
   * @tparam T The context type. This is used directly. If the context needs to be @c const
   * then this parameter should make that explicit, e.g. @c ContextNames<const Context>. This
   * parameter is accessible via the @c context_type alias.
   *
   * This provides a name binding that also has a local context, provided at the formatting call
   * site. The functors have access to this context and are presumed to use it to generate output.
   * This binding can also contain external generators which do not get access to the context to
   * make it convenient to add external generators as well as context generators.
   *
   * A context functor should have the signature
   * @code
   *   BufferWriter & generator(BufferWriter & w, const Spec & spec, T & context);
   * @endcode
   *
   * @a context will be the context for the binding passed to the formatter.
   *
   * This is used by the formatting logic by calling the @c bind method with a context object.
   *
   * This class doubles as a @c NameBinding, such that it passes itself to the formatting logic.
   * In actual use that is more convenient for external code to overload name dispatch, which can
   * then be done by subclassing this class and overriding the function operator. Otherwise most
   * of the class would need to be duplicated in order to override a nested or associated binding
   * class.
   *
   */
  template <typename T> class ContextNames : public NameMap<BufferWriter &(BufferWriter &, const Spec &, T &)>, public NameBinding
  {
  private:
    using self_type  = ContextNames; ///< self reference type.
    using super_type = NameMap<BufferWriter &(BufferWriter &, const Spec &, T &)>;
    using Map        = typename super_type::Map;

  public:
    using context_type = T; ///< Export for external convenience.
    /// Functional type for a generator.
    using Generator = typename super_type::Generator;
    /// Signature for an external (context-free) generator.
    using ExternalGenerator = std::function<ExternalGeneratorSignature>;

    using super_type::super_type; // inherit @c super_type constructors.

    /** Assign the external generator @a bg to @a name.
     *
     * This is used for generators in the namespace that do not use the context.
     *
     * @param name Name associated with the generator.
     * @param bg An external generator that requires no context.
     * @return @c *this
     */
    self_type &assign(std::string_view const &name, const ExternalGenerator &bg);

    /** Assign the @a generator to the @a name.
     *
     * @param name Name associated with the @a generator.
     * @param generator The generator function.
     *
     * @internal This is a covarying override of the super type, added to covary and
     * provide documentation.
     */
    self_type &assign(std::string_view const &name, Generator const &generator);

    /** Bind the name map to a specific @a context.
     *
     * @param context The instance of @a T to use in the generators.
     * @return A reference to an internal instance of a subclass of the protocol class @c BoundNames.
     *
     * This is used when passing the context name map to the formatter.
     */
    const NameBinding &bind(context_type &context);

  protected:
    /** Override of virtual method to provide an implementation.
     *
     * @param w Output.
     * @param spec Format specifier for output.
     * @return @a w
     *
     * This is called from the formatting logic to generate output for a named specifier. Subclasses
     * that need to handle name dispatch differently need only override this method.
     */
    BufferWriter &operator()(BufferWriter &w, const Spec &spec) const override;

    context_type *_ctx = nullptr; ///< Context for generators.
  };

  /** Default global names.
   * This nameset is used if no other is provided. Therefore bindings added to this nameset will be
   * available in the default formatting use.
   */
  extern ExternalNames Global_Names;

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

  inline BufferWriter &
  NameBinding::err_invalid_name(BufferWriter &w, const Spec &spec)
  {
    return w.print("{{~{}~}}", spec._name);
  }

  inline BufferWriter &
  NilBinding::operator()(BufferWriter &, bwf::Spec const &) const
  {
    throw std::runtime_error("Use of nil bound names in BW formatting");
  }

  template <typename T>
  inline const NameBinding &
  ContextNames<T>::bind(context_type &ctx)
  {
    _ctx = &ctx;
    return *this;
  }

  template <typename T>
  BufferWriter &
  ContextNames<T>::operator()(BufferWriter &w, const Spec &spec) const
  {
    if (!spec._name.empty()) {
      if (auto spot = super_type::_map.find(spec._name); spot != super_type::_map.end()) {
        spot->second(w, spec, *_ctx);
      } else {
        this->err_invalid_name(w, spec);
      }
    }
    return w;
  }

  template <typename F> NameMap<F>::NameMap() {}

  template <typename F> NameMap<F>::NameMap(std::initializer_list<std::tuple<std::string_view, const Generator &>> list)
  {
    for (auto &&[name, generator] : list) {
      this->assign(name, generator);
    }
  }

  template <typename F>
  std::string_view
  NameMap<F>::localize(std::string_view const &name)
  {
    auto span = _arena.alloc(name.size());
    memcpy(span, name);
    return span.view();
  }

  template <typename F>
  auto
  NameMap<F>::assign(std::string_view const &name, Generator const &generator) -> self_type &
  {
    _map[this->localize(name)] = generator;
    return *this;
  }

  inline BufferWriter &
  ExternalNames::operator()(BufferWriter &w, const Spec &spec) const
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

  inline NameBinding const &
  ExternalNames::bind() const
  {
    return *this;
  }

  template <typename T>
  auto
  ContextNames<T>::assign(std::string_view const &name, ExternalGenerator const &bg) -> self_type &
  {
    // wrap @a bg in a shim that discards the context so it can be stored in the map.
    super_type::assign(name, [bg](BufferWriter &w, Spec const &spec, context_type &) -> BufferWriter & { return bg(w, spec); });
    return *this;
  }

  template <typename T>
  auto
  ContextNames<T>::assign(std::string_view const &name, Generator const &g) -> self_type &
  {
    super_type::assign(name, g);
    return *this;
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
  /// as needed without moving data in the output buffer.
  void Adjust_Alignment(BufferWriter &aux, Spec const &spec);

  /** Format @a n as an integral value.
   *
   * @param w Output buffer.
   * @param spec Format specifier.
   * @param n Input value to format.
   * @param negative_p Input value should be treated as a negative value.
   * @return @a w
   *
   * A leading sign character will be output based on @a spec and @a negative_p.
   */
  BufferWriter &Format_Integer(BufferWriter &w, Spec const &spec, uintmax_t n, bool negative_p);

  /** Format @a f as a floating point value.
   *
   * @param w Output buffer.
   * @param spec Format specifier.
   * @param f Input value to format.
   * @param negative_p Input value shoudl be treated as a negative value.
   * @return @a w
   *
   * A leading sign character will be output based on @a spec and @a negative_p.
   */
  BufferWriter &Format_Float(BufferWriter &w, Spec const &spec, double f, bool negative_p);

  /** Format output as a hexadecimal dump.
   *
   * @param w Output buffer.
   * @param view Input view.
   * @param digits Digit array for hexadecimal digits.
   *
   * This dumps the memory in the @a view as a hexadecimal string.
   */
  void Format_As_Hex(BufferWriter &w, std::string_view view, const char *digits);

  /* Capture support, which allows format extractors to capture arguments and consume them.
   * This was built in order to support C style formatting, which needs to capture arguments
   * to set the minimum width and/or the precision of other arguments.
   *
   * The key component is the ability to dynamically access an element of a tuple using
   * @c std::any.
   *
   * Note: Much of this was originally in the meta support but it caused problems in use if
   * the tuple header wasn't also included. I was unable to determine why, so this code doesn't
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

template <typename Binding, typename Extractor, typename... Args>
BufferWriter &
BufferWriter::print_nfv(Binding const &names, Extractor &&ex, std::tuple<Args...> const &args)
{
  using namespace std::literals;
  static constexpr int N = sizeof...(Args); // Check argument indices against this.
  static const auto fa   = bwf::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Args...>{});
  int arg_idx            = 0; // the next argument index to be processed.

  // Parser is required to return @c false if there's no more data, @c true if something was parsed.
  while (ex) {
    std::string_view lit_v;
    bwf::Spec spec;
    bool spec_p = ex(lit_v, spec);

    // If there's a literal, just ship it.
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
            bwf::arg_capture(ex, lw, spec, bwf::Tuple_Nth(args, static_cast<size_t>(spec._idx)), swoc::meta::CaseArg);
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
  return this->print_nfv(bwf::Global_Names.bind(), bwf::Format::bind(fmt), std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::print(bwf::Format const &fmt, Args &&... args)
{
  return this->print_nfv(bwf::Global_Names.bind(), fmt.bind(), std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::print_v(TextView const &fmt, std::tuple<Args...> const &args)
{
  return this->print_nfv(bwf::Global_Names.bind(), bwf::Format::bind(fmt), args);
}

template <typename... Args>
BufferWriter &
BufferWriter::print_v(const bwf::Format &fmt, const std::tuple<Args...> &args)
{
  return this->print_nfv(bwf::Global_Names.bind(), fmt.bind(), args);
}

template <typename Binding, typename Extractor>
BufferWriter &
BufferWriter::print_nfv(Binding const &names, Extractor &&f)
{
  return print_nfv(names, f, std::make_tuple());
}

template <typename Binding>
BufferWriter &
BufferWriter::print_n(Binding const &names, TextView const &fmt)
{
  return print_nfv(names, bwf::Format::bind(fmt), std::make_tuple());
}

// ---- Formatting for specific types.

// Must be first because it is used by other formatters, and is not inline.
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, std::string_view sv);

// Pointers that are not specialized.
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const void *ptr)
{
  bwf::Spec ptr_spec{spec};
  ptr_spec._radix_lead_p = true;

  if (ptr == nullptr) {
    if (spec._type == 's' || spec._type == 'S') {
      ptr_spec._type = bwf::Spec::DEFAULT_TYPE;
      ptr_spec._ext  = ""_sv; // clear any extension.
      return bwformat(w, spec, spec._type == 's' ? "null"_sv : "NULL"_sv);
    } else if (spec._type == bwf::Spec::DEFAULT_TYPE) {
      return w; // print nothing if there is no format character override.
    }
  }

  if (ptr_spec._type == bwf::Spec::DEFAULT_TYPE || ptr_spec._type == 'p') {
    ptr_spec._type = 'x'; // if default or 'p;, switch to lower hex.
  } else if (ptr_spec._type == 'P') {
    ptr_spec._type = 'X'; // P means upper hex, overriding other specializations.
  }
  return bwf::Format_Integer(w, ptr_spec, reinterpret_cast<intptr_t>(ptr), false);
}

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

template <size_t N>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const char (&a)[N])
{
  return bwformat(w, spec, std::string_view(a, N - 1));
}

// Capture this explicitly so it doesn't go to any other pointer type.
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::nullptr_t)
{
  return bwformat(w, spec, static_cast<void *>(nullptr));
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, const char *v)
{
  if (spec._type == 'x' || spec._type == 'X' || spec._type == 'p' || spec._type == 'P') {
    bwformat(w, spec, static_cast<const void *>(v));
  } else if (v != nullptr) {
    bwformat(w, spec, std::string_view(v));
  } else {
    bwformat(w, spec, nullptr);
  }
  return w;
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::string const &s)
{
  return bwformat(w, spec, std::string_view{s});
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, TextView tv)
{
  return bwformat(w, spec, static_cast<std::string_view>(tv));
}

template <typename X, typename V>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, TransformView<X, V> &&view)
{
  while (view)
    w.write(char(*(view++)));
  return w;
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
/** Generate formatted output to a @c std::string @a s using format @a fmt with arguments @a args.
 *
 * @tparam Args Format argument types.
 * @param s Output string.
 * @param fmt Format string.
 * @param args A tuple of the format arguments.
 * @return @a s
 *
 * The output is generated to @a s as is. If @a s does not have sufficient space for the output
 * it is resized to be sufficient and the output formatted again. The result is that @a s will
 * containing exactly the formatted output.
 *
 * @note This function is intended for use by other formatting front ends, such as in classes that
 * need to generate formatted output. For direct use there is an overload that takes an argument
 * list.
 */
template <typename... Args>
std::string &
bwprint_v(std::string &s, TextView fmt, std::tuple<Args...> const &args)
{
  auto len = s.size(); // remember initial size
  size_t n = FixedBufferWriter(s.data(), s.size()).print_v(fmt, args).extent();
  s.resize(n);   // always need to resize - if shorter, must clip pre-existing text.
  if (n > len) { // dropped data, try again.
    FixedBufferWriter(s.data(), s.size()).print_v(fmt, args);
  }
  return s;
}

/** Generate formatted output to a @c std::string @a s using format @a fmt with arguments @a args.
 *
 * @tparam Args Format argument types.
 * @param s Output string.
 * @param fmt Format string.
 * @param args Arguments for format string.
 * @return @a s
 *
 * The output is generated to @a s as is. If @a s does not have sufficient space for the output
 * it is resized to be sufficient and the output formatted again. The result is that @a s will
 * contain exactly the formatted output.
 *
 * @note This is intended for direct use. For indirect use (as a backend for another class) see the
 * overload that takes an argument tuple.
 */
template <typename... Args>
std::string &
bwprint(std::string &s, TextView fmt, Args &&... args)
{
  return bwprint_v(s, fmt, std::forward_as_tuple(args...));
}

/// @cond COVARY
template <typename... Args>
auto
FixedBufferWriter::print(TextView fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::print_v(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
auto
FixedBufferWriter::print_v(TextView fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::print_v(fmt, args));
}

template <typename... Args>
auto
FixedBufferWriter::print(bwf::Format const &fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::print_v(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
auto
FixedBufferWriter::print_v(bwf::Format const &fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::print_v(fmt, args));
}
/// @endcond

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
  tag_label(BufferWriter &w, const bwf::Spec &, meta::CaseTag<1>) -> decltype(T::label, meta::TypeFunc<void>())
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

// Basic format wrappers - these are here because they're used internally.
namespace bwf
{
  /** Hex dump wrapper.
   *
   * This wrapper indicates the contained view should be dumped as raw memory in hexadecimal format.
   * This is intended primarily for internal use by other formatting logic.
   *
   * @see As_Hex
   */
  struct HexDump {
    std::string_view _view; ///< A view of the memory to dump.

    /** Dump @a n bytes starting at @a mem as hex.
     *
     * @param mem First byte of memory to dump.
     * @param n Number of bytes.
     */
    HexDump(void const *mem, size_t n) : _view(static_cast<char const *>(mem), n) {}
  };

  /** Treat @a t as raw memory and dump the memory as hexadecimal.
   *
   * @tparam T Type of argument.
   * @param t Object to dump.
   * @return @a A wrapper to do a hex dump.
   *
   * This is the standard way to do a hexadecimal memory dump of an object.
   *
   * @internal This function exists so that other types can overload it for special processing,
   * which would not be possible with just @c HexDump.
   */
  template <typename T>
  HexDump
  As_Hex(T const &t)
  {
    return HexDump(&t, sizeof(T));
  }

} // namespace bwf

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::HexDump const &hex);

} // namespace swoc
