/**
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

  Copyright 2019, Oath Inc.
*/

#include <random>
#include <chrono>

#include <swoc/TextView.h>
#include <swoc/ArenaWriter.h>
#include <swoc/bwf_ip.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"
#include "txn_box/Extractor.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::BufferWriter;
using swoc::Errata;
using swoc::MemSpan;
using swoc::Rv;
using swoc::TextView;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */
class Ex_var : public Extractor
{
public:
  static constexpr TextView NAME{"var"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  using Extractor::extract; // declare hidden member function

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_var::validate(class Config &cfg, struct Extractor::Spec &spec, const class swoc::TextView &arg)
{
  auto name       = cfg.alloc_span<feature_type_for<STRING>>(1);
  spec._data.span = name.rebind<void>();
  name[0]         = cfg.localize(arg);
  return ActiveType::any_type();
}

Feature
Ex_var::extract(Context &ctx, Spec const &spec)
{
  return ctx.load_txn_var(spec._data.span.rebind<feature_type_for<STRING>>()[0]);
}

BufferWriter &
Ex_var::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_is_internal : public Extractor
{
public:
  static constexpr TextView NAME{"is-internal"}; ///< Extractor name.

  /// Check argument and indicate possible feature types.
  Rv<ActiveType>
  validate(Config &, Spec &, swoc::TextView const &) override
  {
    return ActiveType{BOOLEAN};
  }

  using Extractor::extract; // declare hidden member function

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;

  /// Required text formatting access.
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

auto
Ex_is_internal::extract(Context &ctx, Spec const &) -> Feature
{
  return ctx._txn.is_internal();
}

BufferWriter &
Ex_is_internal::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Random integer extractor.
class Ex_random : public Extractor
{
  using self_type  = Ex_random; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME{"random"}; ///< Extractor name.

  using Extractor::extract; // declare hidden member function

  /// Verify the arguments are a valid integer range.
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &spec) override;

protected:
  /// Random generator.
  /// Not thread safe, so have one for each thread.
  static thread_local std::mt19937 _engine;
};

thread_local std::mt19937 Ex_random::_engine(std::chrono::high_resolution_clock::now().time_since_epoch().count());

Feature
Ex_random::extract(Context &, Extractor::Spec const &spec)
{
  auto values = spec._data.span.rebind<feature_type_for<INTEGER>>();
  return std::uniform_int_distribution{values[0], values[1]}(_engine);
};

Rv<ActiveType>
Ex_random::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  auto values                   = cfg.alloc_span<feature_type_for<INTEGER>>(2);
  spec._data.span               = values;      // remember where the storage is.
  feature_type_for<INTEGER> min = 0, max = 99; // temporaries for parsing output.
  // Config storage for parsed output.
  values[0] = min;
  values[1] = max;
  // Parse the parameter.
  if (arg) {
    auto     max_arg{arg};
    auto     min_arg = max_arg.split_prefix_at(",-");
    TextView parsed;
    if (min_arg) {
      min = swoc::svtoi(min_arg, &parsed);
      if (parsed.size() != min_arg.size()) {
        return Errata(S_ERROR, R"(Parameter "{}" for "{}" is not an integer as required)", min_arg, NAME);
      }
    }
    if (max_arg) {
      max = swoc::svtoi(max_arg, &parsed);
      if (parsed.size() != max_arg.size()) {
        return Errata(S_ERROR, R"(Parameter "{}" for "{}" is not an integer as required)", max_arg, NAME);
      }
    }
  }

  if (min >= max) {
    return Errata(S_ERROR, R"(Parameter "{}" for "{}" has an invalid range {}-{})", min, max);
  }

  // Update the stored values now that *both* input values are validated.
  values[0] = min;
  values[1] = max;
  return {INTEGER};
}
/* ------------------------------------------------------------------------------------ */
template <typename T, const TextView *KEY> class Ex_duration : public Extractor
{
  using self_type  = Ex_duration; ///< Self reference type.
  using super_type = Extractor;   ///< Parent type.
  using ftype      = feature_type_for<DURATION>;

public:
  static constexpr TextView NAME{*KEY};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &spec) override;
  /// Extract the feature from the config.
  Feature extract(Config &cfg, Extractor::Spec const &spec) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

template <typename T, const TextView *KEY>
Feature
Ex_duration<T, KEY>::extract(Context &, Extractor::Spec const &spec)
{
  return spec._data.span.rebind<ftype>()[0];
}

template <typename T, const TextView *KEY>
Feature
Ex_duration<T, KEY>::extract(Config &, Extractor::Spec const &spec)
{
  return spec._data.span.rebind<ftype>()[0];
}

template <typename T, const TextView *KEY>
BufferWriter &
Ex_duration<T, KEY>::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

template <typename T, const TextView *KEY>
Rv<ActiveType>
Ex_duration<T, KEY>::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  auto span       = cfg.alloc_span<ftype>(1);
  spec._data.span = span; // remember where the storage is.

  if (!arg) {
    return Errata(S_ERROR, R"("{}" extractor requires an integer argument.)", NAME);
  }

  TextView parsed;
  auto     n = swoc::svtoi(arg, &parsed);
  if (parsed.size() != arg.size()) {
    return Errata(S_ERROR, R"(Parameter "{}" for "{}" is not an integer as required)", arg, NAME);
  }

  span[0] = T{n};

  ActiveType zret{DURATION};
  zret.mark_cfg_const();
  return zret;
}
/* ------------------------------------------------------------------------------------ */
class Ex_txn_conf : public Extractor
{
  using self_type  = Ex_txn_conf;        ///< Self reference type.
  using super_type = Extractor;          ///< Parent type.
  using store_type = ts::TxnConfigVar *; ///< Storage type for config var record.
public:
  static constexpr TextView NAME{"txn-conf"};
  using Extractor::extract; // declare hidden member function

  /** Validate the use of the extractor in a feature string.
   *
   * @param cfg Configuration.
   * @param spec Specifier used in the feature string for the extractor.
   * @param arg Argument for the extractor.
   * @return The value type for @a spec and @a arg.
   *
   */
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &spec) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_txn_conf::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  auto var = ts::HttpTxn::find_override(arg);
  if (nullptr == var) {
    return Errata(S_ERROR, R"("{}" is not a recognized transaction overridable configuration variable name.)", arg);
  }
  auto ptr        = cfg.alloc_span<store_type>(1);
  ptr[0]          = var;
  spec._data.span = ptr.rebind<void>(); // remember where the pointer is.
  ValueType vt    = NIL;
  switch (var->type()) {
  case TS_RECORDDATATYPE_INT:
    vt = INTEGER;
    break;
  case TS_RECORDDATATYPE_FLOAT:
    vt = FLOAT;
    break;
  case TS_RECORDDATATYPE_STRING:
    vt = STRING;
    break;
  default:
    break;
  }
  return ActiveType{vt};
}

Feature
Ex_txn_conf::extract(Context &ctx, const Extractor::Spec &spec)
{
  Feature zret{};
  auto    var            = spec._data.span.rebind<store_type>()[0];
  auto &&[value, errata] = ctx._txn.override_fetch(*var);
  if (errata.is_ok()) {
    switch (value.index()) {
    case 0:
      break;
    case 1:
      zret = std::get<1>(value);
      break;
    case 2:
      zret = std::get<2>(value);
      break;
    case 3:
      FeatureView v = std::get<3>(value);
      v._direct_p   = true;
      zret          = v;
      break;
    }
  }
  return zret;
}

BufferWriter &
Ex_txn_conf::format(BufferWriter &w, const Spec &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// The active feature.
class Ex_active_feature : public Extractor
{
  using self_type  = Ex_active_feature; ///< Self reference type.
  using super_type = Extractor;         ///< Parent type.
public:
  static constexpr TextView NAME = ACTIVE_FEATURE_KEY;
  Rv<ActiveType>
  validate(Config &cfg, Spec &, TextView const &) override
  {
    return cfg.active_type();
  }

  using Extractor::extract; // declare hidden member function
  Feature       extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_active_feature::extract(class Context &ctx, const struct Extractor::Spec &)
{
  return ctx._active;
}

BufferWriter &
Ex_active_feature::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._active);
}

/* ------------------------------------------------------------------------------------ */
/// Remnant capture group.
class Ex_unmatched_group : public Extractor
{
  using self_type  = Ex_unmatched_group; ///< Self reference type.
  using super_type = Extractor;          ///< Parent type.
public:
  static constexpr TextView NAME = UNMATCHED_FEATURE_KEY;
  Rv<ActiveType>            validate(Config &cfg, Spec &spec, TextView const &arg) override;
  using Extractor::extract; // declare hidden member function
  Feature       extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_unmatched_group::extract(class Context &ctx, const struct Extractor::Spec &)
{
  return ctx._remainder;
}

BufferWriter &
Ex_unmatched_group::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._remainder);
}

Rv<ActiveType>
Ex_unmatched_group::validate(Config &, Extractor::Spec &, TextView const &)
{
  return {STRING};
}

/* ------------------------------------------------------------------------------------ */
class Ex_env : public StringExtractor
{
  using self_type  = Ex_env;          ///< Self reference type.
  using super_type = StringExtractor; ///< Parent type.
public:
  static constexpr TextView NAME = "env";
  Rv<ActiveType>            validate(Config &cfg, Spec &spec, TextView const &arg) override;
  Feature                   extract(Config &ctx, Spec const &spec) override;
  Feature                   extract(Context &ctx, Spec const &spec) override;
  BufferWriter             &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_env::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  auto span       = cfg.alloc_span<TextView>(1);
  spec._data.span = span;
  TextView &value = span[0];
  char      c_arg[arg.size() + 1];
  memcpy(c_arg, arg.data(), arg.size());
  c_arg[arg.size()] = 0;
  if (auto txt = ::getenv(c_arg); txt != nullptr) {
    value = cfg.localize(txt);
  } else {
    value = "";
  }

  return ActiveType{STRING}.mark_cfg_const();
}

Feature
Ex_env::extract(Config &, const Spec &spec)
{
  return FeatureView::Literal(spec._data.span.rebind<TextView>()[0]);
}

Feature
Ex_env::extract(Context &, const Spec &spec)
{
  return FeatureView::Literal(spec._data.span.rebind<TextView>()[0]);
}

BufferWriter &
Ex_env::format(BufferWriter &w, Spec const &spec, Context &)
{
  return bwformat(w, spec, spec._data.span.rebind<TextView>()[0]);
}

/* ------------------------------------------------------------------------------------ */
BufferWriter &
Ex_this::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

Feature
Ex_this::extract(class Context &ctx, Extractor::Spec const &spec)
{
  return _fg ? _fg->extract(ctx, spec._ext) : NIL_FEATURE;
}

swoc::Rv<ActiveType>
Ex_this::validate(Config &cfg, Extractor::Spec &, TextView const &)
{
  return cfg.active_type();
}
/* ------------------------------------------------------------------------------------ */
// Needs to be external visible.
Ex_this ex_this;

namespace
{
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.
Ex_var var;

Ex_is_internal is_internal;

Ex_txn_conf txn_conf;

Ex_random random;
Ex_env    env;

static constexpr TextView                             NANOSECONDS = "nanoseconds";
Ex_duration<std::chrono::nanoseconds, &NANOSECONDS>   nanoseconds;
static constexpr TextView                             MILLISECONDS = "milliseconds";
Ex_duration<std::chrono::milliseconds, &MILLISECONDS> milliseconds;
static constexpr TextView                             SECONDS = "seconds";
Ex_duration<std::chrono::seconds, &SECONDS>           seconds;
static constexpr TextView                             MINUTES = "minutes";
Ex_duration<std::chrono::minutes, &MINUTES>           minutes;
static constexpr TextView                             HOURS = "hours";
Ex_duration<std::chrono::hours, &HOURS>               hours;
static constexpr TextView                             DAYS = "days";
Ex_duration<std::chrono::days, &DAYS>                 days;
static constexpr TextView                             WEEKS = "weeks";
Ex_duration<std::chrono::weeks, &WEEKS>               weeks;

Ex_active_feature  ex_with_feature;
Ex_unmatched_group unmatched_group;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Extractor::define(Ex_this::NAME, &ex_this);
  Extractor::define(Ex_active_feature::NAME, &ex_with_feature);
  Extractor::define(Ex_unmatched_group::NAME, &unmatched_group);
  Extractor::define("unmatched", &unmatched_group);

  Extractor::define(Ex_txn_conf::NAME, &txn_conf);

  Extractor::define(Ex_is_internal::NAME, &is_internal);
  Extractor::define(Ex_random::NAME, &random);
  Extractor::define(Ex_var::NAME, &var);

  Extractor::define(NANOSECONDS, &nanoseconds);
  Extractor::define(MILLISECONDS, &milliseconds);
  Extractor::define(SECONDS, &seconds);
  Extractor::define(MINUTES, &minutes);
  Extractor::define(HOURS, &hours);
  Extractor::define(DAYS, &days);
  Extractor::define(WEEKS, &weeks);

  Extractor::define(Ex_env::NAME, &env);

  return true;
}();
} // namespace
