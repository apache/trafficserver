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

  Copyright 2020, Oath Inc.
*/

#include <random>
#include <chrono>

#include <openssl/ssl.h>

#include <swoc/TextView.h>
#include <swoc/ArenaWriter.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"
#include "txn_box/Extractor.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using swoc::BufferWriter;
using swoc::Errata;
using swoc::Rv;
using swoc::MemSpan;
// namespace bwf = swoc::bwf;
// using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/** Extract the number of transactions for the inbound session.
 */
class Ex_inbound_txn_count : public Extractor
{
public:
  /// Extractor name.
  static constexpr TextView NAME{"inbound-txn-count"};
  using Extractor::extract; // declare hidden member function
  /** Validate argument and indicate extracted type.
   *
   * @return The active type and any errors.
   */
  Rv<ActiveType> validate(Config &, Extractor::Spec &, TextView const &) override;

  /** Extract the transaction count.
   *
   * @param ctx Current transaction context.
   * @param spec Format specifier.
   * @return The extracted feature.
   *
   * This is called when the extractor is a @c Direct feature and therefore typed.
   */
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_inbound_txn_count::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{INTEGER}; // never a problem, just return the type.
}
Feature
Ex_inbound_txn_count::extract(Context &ctx, Spec const &)
{
  return feature_type_for<INTEGER>(ctx.inbound_ssn().txn_count());
}
/* ------------------------------------------------------------------------------------ */
/// Extract the SNI name from the inbound session.
class Ex_inbound_sni : public Extractor
{
public:
  static constexpr TextView NAME{"inbound-sni"};
  using Extractor::extract; // declare hidden member function
  /// Extract the SNI  name from the inbound session.
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_inbound_sni::extract(Context &ctx, Spec const &)
{
  return ctx.inbound_ssn().sni();
}
/* ------------------------------------------------------------------------------------ */
/// Extract the client session remote address.
class Ex_inbound_addr_remote : public Extractor
{
public:
  static constexpr TextView NAME{"inbound-addr-remote"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_inbound_addr_remote::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{NIL, IP_ADDR};
}

Feature
Ex_inbound_addr_remote::extract(Context &ctx, Spec const &)
{
  if (auto addr = ctx.inbound_ssn().addr_remote(); addr) {
    return swoc::IPAddr{addr};
  }

  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
/// Extract the client session local address.
class Ex_inbound_addr_local : public Extractor
{
public:
  static constexpr TextView NAME{"inbound-addr-local"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_inbound_addr_local::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{NIL, IP_ADDR};
}

Feature
Ex_inbound_addr_local::extract(Context &ctx, Spec const &)
{
  if (auto addr = ctx.inbound_ssn().addr_local(); addr) {
    return swoc::IPAddr{addr};
  }

  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
class Ex_has_inbound_protocol_prefix : public Extractor
{
public:
  static constexpr TextView NAME{"has-inbound-protocol-prefix"};
  using Extractor::extract; // declare hidden member function
  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_has_inbound_protocol_prefix::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  spec._data.text = cfg.localize(arg, Config::LOCAL_CSTR);
  return {BOOLEAN};
}

auto
Ex_has_inbound_protocol_prefix::extract(Context &ctx, Spec const &spec) -> Feature
{
  return !ctx._txn.inbound_ssn().protocol_contains(spec._data.text).empty();
}

/* ------------------------------------------------------------------------------------ */
class Ex_has_outbound_protocol_prefix : public Extractor
{
public:
  static constexpr TextView NAME{"has-outbound-protocol-prefix"};
  using Extractor::extract; // declare hidden member function
  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_has_outbound_protocol_prefix::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  spec._data.text = cfg.localize(arg, Config::LOCAL_CSTR);
  return {BOOLEAN};
}

auto
Ex_has_outbound_protocol_prefix::extract(Context &ctx, Spec const &spec) -> Feature
{
  return !ctx._txn.outbound_protocol_contains(spec._data.text).empty();
}
/* ------------------------------------------------------------------------------------ */
class Ex_inbound_protocol_stack : public Extractor
{
public:
  static constexpr TextView NAME{"inbound-protocol-stack"};
  using Extractor::extract; // declare hidden member function
  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;

  /// Required text formatting access.
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_inbound_protocol_stack::validate(Config &, Extractor::Spec &, TextView const &)
{
  return {ActiveType::TupleOf(STRING)};
}

auto
Ex_inbound_protocol_stack::extract(Context &ctx, Spec const &) -> Feature
{
  std::array<char const *, 10> tags;
  auto n = ctx._txn.inbound_ssn().protocol_stack(MemSpan{tags.data(), tags.size()});
  if (n > 0) {
    auto span = ctx.alloc_span<Feature>(n);
    for (decltype(n) idx = 0; idx < n; ++idx) {
      // Plugin API guarantees returned tags are process lifetime so can be marked literal.
      span[idx] = FeatureView::Literal(TextView{tags[idx], TextView::npos});
    }
    return span;
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_inbound_protocol_stack::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_outbound_protocol_stack : public Extractor
{
public:
  static constexpr TextView NAME{"outbound-protocol-stack"};
  using Extractor::extract; // declare hidden member function
  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_outbound_protocol_stack::validate(Config &, Extractor::Spec &, TextView const &)
{
  return {ActiveType::TupleOf(STRING)};
}

auto
Ex_outbound_protocol_stack::extract(Context &ctx, Spec const &) -> Feature
{
  std::array<char const *, 10> tags;
  auto n = ctx._txn.outbound_protocol_stack(MemSpan{tags.data(), tags.size()});
  if (n > 0) {
    auto span = ctx.alloc_span<Feature>(n);
    for (decltype(n) idx = 0; idx < n; ++idx) {
      // Plugin API guarantees returned tags are process lifetime so can be marked literal.
      span[idx] = FeatureView::Literal(TextView{tags[idx], TextView::npos});
    }
    return span;
  }
  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
/// Client Session protocol information.
class Ex_inbound_protocol : public StringExtractor
{
  using self_type  = Ex_inbound_protocol; ///< Self reference type.
  using super_type = StringExtractor;     ///< Parent type.
public:
  static constexpr TextView NAME{"inbound-protocol"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_inbound_protocol::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  spec._data.text = cfg.localize(arg, Config::LOCAL_CSTR);
  return {STRING};
}

BufferWriter &
Ex_inbound_protocol::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto tag = ctx._txn.inbound_ssn().protocol_contains(spec._data.text);
  return bwformat(w, spec, tag);
}

class Ex_outbound_protocol : public StringExtractor
{
  using self_type  = Ex_outbound_protocol; ///< Self reference type.
  using super_type = StringExtractor;      ///< Parent type.
public:
  static constexpr TextView NAME{"outbound-protocol"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_outbound_protocol::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  spec._data.text = cfg.localize(arg, Config::LOCAL_CSTR);
  return {STRING};
}

BufferWriter &
Ex_outbound_protocol::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto tag = ctx._txn.outbound_protocol_contains(spec._data.text);
  return bwformat(w, spec, tag);
}
/* ------------------------------------------------------------------------------------ */
class Ex_inbound_cert_verify_result : public Extractor
{
  using self_type  = Ex_inbound_cert_verify_result;
  using super_type = Extractor;

public:
  static constexpr TextView NAME{"inbound-cert-verify-result"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_inbound_cert_verify_result::validate(Config &, Extractor::Spec &, TextView const &)
{
  return {INTEGER};
}

Feature
Ex_inbound_cert_verify_result::extract(Context &ctx, const Spec &)
{
  return ctx._txn.ssl_inbound_context().verify_result();
}

/// Value for an object in the issuer section of the server certificate of an inbound session.
/// Extractor argument is the name of the field.
class Ex_inbound_cert_local_issuer_value : public StringExtractor
{
  using self_type  = Ex_inbound_cert_local_issuer_value;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"inbound-cert-local-issuer-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_inbound_cert_local_issuer_value::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate issuer name in "{}" extractor.)", arg, NAME);
  }
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_inbound_cert_local_issuer_value::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_inbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.local_issuer_field(nid));
}

class Ex_outbound_cert_local_issuer_value : public StringExtractor
{
  using self_type  = Ex_outbound_cert_local_issuer_value;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"outbound-cert-local-issuer-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_outbound_cert_local_issuer_value::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate issuer name in "{}" extractor.)", arg, NAME);
  }
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_outbound_cert_local_issuer_value::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_outbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.local_issuer_field(nid));
}

/// Value for an object in the subject section of the server certificate of an inbound session.
/// Extractor argument is the name of the field.
class Ex_inbound_cert_local_subject_field : public StringExtractor
{
  using self_type  = Ex_inbound_cert_local_subject_field;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"inbound-cert-local-subject-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_inbound_cert_local_subject_field::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate subject name in "{}" extractor.)", arg, NAME);
  }
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_inbound_cert_local_subject_field::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_inbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.local_subject_field(nid));
}

class Ex_outbound_cert_local_subject_field : public StringExtractor
{
  using self_type  = Ex_outbound_cert_local_subject_field;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"outbound-cert-local-subject-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_outbound_cert_local_subject_field::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate subject name in "{}" extractor.)", arg, NAME);
  }
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_outbound_cert_local_subject_field::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_outbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.local_subject_field(nid));
}
/// Value for an object in the issuer section of the client certificate of an inbound session.
/// Extractor argument is the name of the field.
class Ex_inbound_cert_remote_issuer_value : public StringExtractor
{
  using self_type  = Ex_inbound_cert_remote_issuer_value;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"inbound-cert-remote-issuer-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_inbound_cert_remote_issuer_value::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate issuer name in "{}" extractor.)", arg, NAME);
  }
  // Sigh - abuse the memspan and use the size as the integer value.
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_inbound_cert_remote_issuer_value::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_inbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.remote_issuer_field(nid));
}

class Ex_outbound_cert_remote_issuer_value : public StringExtractor
{
  using self_type  = Ex_outbound_cert_remote_issuer_value;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"outbound-cert-remote-issuer-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_outbound_cert_remote_issuer_value::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate issuer name in "{}" extractor.)", arg, NAME);
  }
  // Sigh - abuse the memspan and use the size as the integer value.
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_outbound_cert_remote_issuer_value::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_outbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.remote_issuer_field(nid));
}

/// Value for an object in the subject section of the client certificate of an inbound session.
/// Extractor argument is the name of the field.
class Ex_inbound_cert_remote_subject_field : public StringExtractor
{
  using self_type  = Ex_inbound_cert_remote_subject_field;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"inbound-cert-remote-subject-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_inbound_cert_remote_subject_field::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate subject name in "{}" extractor.)", arg, NAME);
  }
  // Sigh - abuse the memspan and use the size as the integer value.
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_inbound_cert_remote_subject_field::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_inbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.remote_subject_field(nid));
}

class Ex_outbound_cert_remote_subject_field : public StringExtractor
{
  using self_type  = Ex_outbound_cert_remote_subject_field;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"outbound-cert-remote-subject-field"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
};

Rv<ActiveType>
Ex_outbound_cert_remote_subject_field::validate(Config &, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument for the value name.)", NAME);
  }
  intptr_t nid = ts::ssl_nid(arg);
  if (NID_undef == nid) {
    return Errata(S_ERROR, R"("{}" is not a valid certificate subject name in "{}" extractor.)", arg, NAME);
  }
  // Sigh - abuse the memspan and use the size as the integer value.
  spec._data.u = nid;
  return {STRING};
}

BufferWriter &
Ex_outbound_cert_remote_subject_field::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  auto ssl_ctx = ctx._txn.ssl_outbound_context();
  auto nid     = spec._data.u;
  return bwformat(w, spec, ssl_ctx.remote_subject_field(nid));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ts_uuid : public StringExtractor
{
  using self_type  = Ex_ts_uuid;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"ts-uuid"};
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_ts_uuid::validate(Config &, Spec &, const TextView &)
{
  return {STRING};
}

BufferWriter &
Ex_ts_uuid::format(BufferWriter &w, const Spec &spec, Context &)
{
  return bwformat(w, spec, TSUuidStringGet(TSProcessUuidGet()));
}
/* ------------------------------------------------------------------------------------ */
namespace
{
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.

Ex_inbound_txn_count inbound_txn_count;
Ex_inbound_sni inbound_sni;
Ex_inbound_protocol inbound_protocol;
Ex_inbound_addr_remote inbound_addr_remote;
Ex_inbound_addr_local inbound_addr_local;
Ex_has_inbound_protocol_prefix has_inbound_protocol_prefix;
Ex_inbound_protocol_stack inbound_protocol_stack;
Ex_inbound_cert_verify_result inbound_cert_verify_result;
Ex_inbound_cert_local_issuer_value inbound_cert_local_issuer_value;
Ex_inbound_cert_local_subject_field inbound_cert_local_subject_field;
Ex_inbound_cert_remote_issuer_value inbound_cert_remote_issuer_value;
Ex_inbound_cert_remote_subject_field inbound_cert_remote_subject_field;

Ex_has_outbound_protocol_prefix has_outbound_protocol_prefix;
Ex_outbound_protocol_stack outbound_protocol_stack;
Ex_outbound_cert_local_issuer_value outbound_cert_local_issuer_value;
Ex_outbound_cert_local_subject_field outbound_cert_local_subject_field;
Ex_outbound_cert_remote_issuer_value outbound_cert_remote_issuer_value;
Ex_outbound_cert_remote_subject_field outbound_cert_remote_subject_field;

Ex_ts_uuid ts_uuid;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Extractor::define(Ex_inbound_txn_count::NAME, &inbound_txn_count);
  Extractor::define(Ex_inbound_sni::NAME, &inbound_sni);
  Extractor::define(Ex_inbound_protocol::NAME, &inbound_protocol);
  Extractor::define(Ex_has_inbound_protocol_prefix::NAME, &has_inbound_protocol_prefix);
  Extractor::define(Ex_inbound_protocol_stack::NAME, &inbound_protocol_stack);
  Extractor::define(Ex_inbound_addr_remote::NAME, &inbound_addr_remote);
  Extractor::define(Ex_inbound_addr_local::NAME, &inbound_addr_local);
  Extractor::define(Ex_inbound_cert_verify_result::NAME, &inbound_cert_verify_result);
  Extractor::define(Ex_inbound_cert_local_subject_field::NAME, &inbound_cert_local_subject_field);
  Extractor::define(Ex_inbound_cert_local_issuer_value::NAME, &inbound_cert_local_issuer_value);
  Extractor::define(Ex_inbound_cert_remote_subject_field::NAME, &inbound_cert_remote_subject_field);
  Extractor::define(Ex_inbound_cert_remote_issuer_value::NAME, &inbound_cert_remote_issuer_value);

  Extractor::define(Ex_has_outbound_protocol_prefix::NAME, &has_outbound_protocol_prefix);
  Extractor::define(Ex_outbound_protocol_stack::NAME, &outbound_protocol_stack);
  Extractor::define(Ex_outbound_cert_local_subject_field::NAME, &outbound_cert_local_subject_field);
  Extractor::define(Ex_outbound_cert_local_issuer_value::NAME, &outbound_cert_local_issuer_value);
  Extractor::define(Ex_outbound_cert_remote_subject_field::NAME, &outbound_cert_remote_subject_field);
  Extractor::define(Ex_outbound_cert_remote_issuer_value::NAME, &outbound_cert_remote_issuer_value);

  Extractor::define(Ex_ts_uuid::NAME, &ts_uuid);

  return true;
}();
} // namespace
