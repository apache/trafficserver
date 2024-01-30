/** @file
 * Regular expression support.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <bitset>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/Expr.h"

/** Regular expression support.
 *
 * This is split out from @c Comparison because regular expressions will be used in additional
 * situations. It is non-copyable because it is basically a wrapper on a non-shared PCRE code
 * block and it seems silly to have a handle to what is effectively a handle. Aggregrating classes
 * can deal with it the same way as a @c std::unique_ptr.
 */
class Rxp
{
  using self_type = Rxp; ///< Self reference type.
  /// Cleanup for compiled regular expressions.
  struct PCRE_Deleter {
    void
    operator()(pcre2_code *ptr)
    {
      pcre2_code_free(ptr);
    }
  };
  /// Handle for compiled regular expression.
  using RxpHandle = std::unique_ptr<pcre2_code, PCRE_Deleter>;

public:
  Rxp()                  = default;
  Rxp(self_type const &) = delete;
  Rxp(self_type &&that) : _rxp(std::move(that._rxp)) {}
  self_type &operator=(self_type const &) = delete;

  /** Apply the regular expression.
   *
   * @param text Subject for application.
   * @param match Match data.
   * @return The match result - negative for failure, 0 for not match, or largest capture group matched.
   *
   * @a match must be provided externally and must be of sufficient length.
   *
   * @see capture_count
   */
  int operator()(swoc::TextView text, pcre2_match_data *match) const;

  /// @return The number of capture groups in the expression.
  size_t capture_count() const;

  /// Regular expression options.
  union Options {
    unsigned int all; ///< All of the flags.
    struct {
      unsigned int nc : 1; ///< Case insensitive
    } f;
  };

  /** Create a regular expression instance from @a str.
   *
   * @param str Regular expressions.
   * @param options Compile time options.
   * @return An instance if successful, errors if not.
   */
  static swoc::Rv<self_type> parse(swoc::TextView const &str, Options const &options);

protected:
  RxpHandle _rxp; /// Compiled regular expression.

  /// Internal constructor used by @a parse.
  Rxp(pcre2_code *rxp) : _rxp(rxp) {}
};

/** Container for a regular expression operation.
 *
 * This holds a regular expression and the machinery needed to apply it at run time.
 *
 * @internal I don't like this here, but where else to put it? Is anything going to use
 * the @c Rxp class without also using this?
 */
class RxpOp
{
  using self_type                = RxpOp;
  template <typename R> using Rv = swoc::Rv<R>; // import

public:
  struct DynamicRxp {
    Expr _expr;        ///< Feature expression source for regular expression.
    Rxp::Options _opt; ///< Options for regular expression.
  };

  RxpOp() = default;
  explicit RxpOp(Rxp &&rxp);
  RxpOp(Expr &&expr, Rxp::Options opt);

  /// Get the number of capture groups.
  size_t capture_count();

  int operator()(Context &ctx, swoc::TextView src);

  static Rv<self_type> load(Config &cfg, Expr &&expr, Rxp::Options opt);

protected:
  std::variant<std::monostate, Rxp, DynamicRxp> _raw;
  // Indices for variant types.
  static constexpr size_t NO_VALUE = 0;
  static constexpr size_t STATIC   = 1;
  static constexpr size_t DYNAMIC  = 2;

  /// Process the regular expression based on the expression type.
  /// This is used during configuration load.
  struct Cfg_Visitor {
    /** Constructor.
     *
     * @param cfg Configuration being loaded.
     * @param opt Options from directive arguments.
     */
    Cfg_Visitor(Config &cfg, Rxp::Options opt) : _cfg(cfg), _rxp_opt(opt) {}

    Rv<RxpOp> operator()(std::monostate);
    Rv<RxpOp> operator()(Feature &f);
    Rv<RxpOp> operator()(Expr::List &l);
    Rv<RxpOp> operator()(Expr::Direct &d);
    Rv<RxpOp> operator()(Expr::Composite &comp);

    Config &_cfg;          ///< Configuration being loaded.
    Rxp::Options _rxp_opt; ///< Any options from directive arguments.
  };

  /** Runtime support.
   *
   * This enables dynamic regular expressions at a reasonable run time cost. If the configuration
   * is a literal it is compiled during configuration load and stored as an @c Rxp instance.
   * Otherwise the @c Expr is stored and evaluated on invocation.
   */
  struct Apply_Visitor {
    /// Invoke on invalid / uninitialized rxp, always fails.
    bool operator()(std::monostate) const;

    /** Invoke the @a rxp against the active feature.
     *
     * @param rxp Compiled regular expression.
     * @return @c true on success, @c false otherwise.
     */
    bool operator()(Rxp const &rxp) const;
    /** Compile the @a expr into a regular expression.
     *
     * @param dr Feature expression and options.
     * @return @c true on successful match, @c false otherwise.
     *
     * @internal This compiles the feature from @a expr and then invokes the @c Rxp overload to do
     * the match.
     */
    bool operator()(DynamicRxp const &dr) const;

    Context &_ctx;       ///< Configuration context.
    swoc::TextView _src; ///< regex text.
  };
};
