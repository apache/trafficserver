/** @file
 * Base extractor classes.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <unordered_map>
#include <string_view>
#include <variant>

#include <swoc/TextView.h>
#include <swoc/bwf_base.h>
#include <swoc/Errata.h>
#include <swoc/swoc_ip.h>

#include "txn_box/common.h"
#include "txn_box/Modifier.h"
#include "ts_util.h"

class Context;
class FeatureGroup;

/** Feature extraction.
 *
 * Application of format strings to transactions to extract features. This also maintains a factory
 * which maps from names to instances of subclasses. In use, the extractor will be passed a
 * run time context which is expected to suffice to extract the appropriate information.
 */
class Extractor
{
  using self_type = Extractor; ///< Self reference type.
public:
  /// Container for extractor factory.
  using Table = std::unordered_map<std::string_view, self_type *>;

  /** Feature expression specifier.
   * This is a subclass of the base format specifier, in order to add a field that points at
   * the extractor, if any, for the specifier.
   */
  struct Spec : public swoc::bwf::Spec {
    /// Extractor used in the spec, if any.
    Extractor *_exf = nullptr;
    /// Config storage for extractor, if needed.
    /// No member should be larger than a string view or span nor have any external state.
    /// Extractors are required to know what type was stored and retrieve it without additional
    /// type information.
    union union_type {
      uintmax_t u;
      swoc::MemSpan<void> span;
      swoc::TextView text;
      ReservedSpan ctx_reserved_span;

      union_type() { span = decltype(span){}; }                // default constructor.
      union_type(union_type const &that) { span = that.span; } // provide copy constructor for Spec constructors.
    } _data;
  };

  virtual ~Extractor() = default;

  /** Validate the use of the extractor in a feature string.
   *
   * @param cfg Configuration.
   * @param spec Specifier used in the feature string for the extractor.
   * @param arg Argument for the extractor.
   * @return The value type for @a spec and @a arg.
   *
   * The base implementation returns successfully as a @c STRING or @c NULL. If the extractor
   * returns some other type or needs to actually validate @a spec, it must override this method.
   * Overriding is also required if the extractor needs to do configuration time initialization.
   */
  virtual swoc::Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg);

  /** Whether the extractor uses data from the context.
   *
   * This is important for @c DIRECT features - if there is a potential reference to that value
   * in another directive, it must be "upgraded" to a @c VIEW to avoid using changed or invalid data.
   * The default implementation returns @c false.
   *
   * @return @c true if the extractor uses the context, @c false otherwise.
   */
  virtual bool has_ctx_ref() const;

  /// @}

  /** Extract the feature from the @a ctx.
   *
   * @param ctx Runtime context.
   * @param spec Specifier for the extractor.
   * @return The extracted feature.
   */
  virtual Feature extract(Context &ctx, Spec const &spec) = 0;

  /** Extract from the configuration.
   *
   * @param cfg Configuration.
   * @param spec Specifier for the extractor.
   * @return The extracted feature.
   *
   * @note Unlike @c Context based extraction, this is optional and should only be overridden for
   * extractors that do not extract runtime dependent data. In such cases the @c validate method
   * should indicate this method is available by marking the type as config extractable.
   *
   * @see validate
   * @see extract(Context & ctx, Spec const& spec)
   */
  virtual Feature extract(Config &cfg, Spec const &spec);

  /** Generate string output for the feature.
   *
   * @param w Output writer.
   * @param spec Specifier data.
   * @param ctx Transaction context.
   * @return @a w.
   *
   * This is the generic entry point for generating string output for a feature, which is required
   * for all extractors. The base implementation calls @c extract and pass that to @c bwformat.
   */
  virtual swoc::BufferWriter &format(swoc::BufferWriter &w, Spec const &spec, Context &ctx);

  /** Define @a name as the extractor @a ex.
   *
   * @param name Name of the extractor.
   * @param ex Extractor instance.
   * @return Errors, if any.
   *
   * This populates the set of names used in the configuration file to specify extractors.
   */
  static swoc::Errata define(swoc::TextView name, self_type *ex);

  /** Find the extractor for @a name.
   *
   * @param name Extractor name.
   * @return A pointer to the extractor, @c nullptr if not found.
   */
  static self_type *find(swoc::TextView const &name);

protected:
  /** Defined extractors.
   */
  static Table _ex_table; /// Obtain the named extractor table.
};

/** Cross reference extractor.
 * This requires special handling and therefore needs to be externally visible.
 */
class Ex_this : public Extractor
{
public:
  static constexpr swoc::TextView NAME{"this"}; ///< Extractor name.

  Ex_this() = default;
  explicit Ex_this(FeatureGroup &fg) : _fg(&fg) {}

  swoc::Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;

  /// Required text formatting access.
  swoc::BufferWriter &format(swoc::BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
  FeatureGroup *_fg = nullptr; ///< FeatureGroup for name lookup.
};

extern Ex_this ex_this;

/** A string.
 * The feature is extracted to transient memory. The subclass needs to provide only the @c format
 * method, this @c extract will use that to return a string.
 */
class StringExtractor : public Extractor
{
public:
  Feature extract(Context &ctx, Spec const &spec) override;
};
