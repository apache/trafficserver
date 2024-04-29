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

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>

#include "txn_box/common.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"
#include "txn_box/Directive.h"
#include "txn_box/Comparison.h"

#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"

using swoc::BufferWriter;
using swoc::Errata;
using swoc::Rv;
using swoc::TextView;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
Feature
Generic::extract() const
{
  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
class Do_ua_req_url_host : public Directive
{
  using super_type = Directive;
  using self_type  = Do_ua_req_url_host;

public:
  static const std::string KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_url_host(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Feature expression.
};

const std::string Do_ua_req_url_host::KEY{"ua-req-url-host"};
const HookMask    Do_ua_req_url_host::HOOKS = MaskFor(Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP);

Do_ua_req_url_host::Do_ua_req_url_host(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_url_host::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      auto value = ctx.extract(_expr);
      if (auto host = std::get_if<IndexFor(STRING)>(&value); nullptr != host) {
        url.host_set(*host);
      }
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_url_host::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                         YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type{std::move(expr)});
}

// ---

class Do_proxy_req_url_host : public Directive
{
  using super_type = Directive;
  using self_type  = Do_proxy_req_url_host;

public:
  static const std::string KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_proxy_req_url_host(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature expression.
};

const std::string Do_proxy_req_url_host::KEY{"proxy-req-url-host"};
const HookMask    Do_proxy_req_url_host::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_url_host::Do_proxy_req_url_host(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_url_host::invoke(Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      auto value = ctx.extract(_expr);
      if (auto host = std::get_if<IndexFor(STRING)>(&value); nullptr != host) {
        url.host_set(*host);
      }
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_url_host::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                            swoc::TextView const &, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type{std::move(expr)});
}

/* ------------------------------------------------------------------------------------ */

class Do_ua_req_url_port : public Directive
{
  using super_type = Directive;
  using self_type  = Do_ua_req_url_port;

public:
  static const std::string KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_url_port(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Feature expression.
};

const std::string Do_ua_req_url_port::KEY{"ua-req-url-port"};
const HookMask    Do_ua_req_url_port::HOOKS{MaskFor({Hook::CREQ, Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_url_port::Do_ua_req_url_port(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_url_port::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      auto port = ctx.extract(_expr).as_integer(-1);
      if (0 < port && port < std::numeric_limits<in_port_t>::max()) {
        url.port_set(port);
      }
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_url_port::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                         YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(INTEGER)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), INTEGER);
  }
  return Handle(new self_type{std::move(expr)});
}

// ---

class Do_proxy_req_url_port : public Directive
{
  using super_type = Directive;
  using self_type  = Do_proxy_req_url_port;

public:
  static const std::string KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_proxy_req_url_port(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Feature expression.
};

const std::string Do_proxy_req_url_port::KEY{"proxy-req-url-port"};
const HookMask    Do_proxy_req_url_port::HOOKS{MaskFor(Hook::PREQ)};

Do_proxy_req_url_port::Do_proxy_req_url_port(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_url_port::invoke(Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      auto port = ctx.extract(_expr).as_integer(-1);
      if (0 < port && port < std::numeric_limits<in_port_t>::max()) {
        url.port_set(port);
      }
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_url_port::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                            swoc::TextView const &, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(INTEGER)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), INTEGER);
  }
  return Handle(new self_type{std::move(expr)});
}

/* ------------------------------------------------------------------------------------ */
namespace
{
// @return @c true if @a loc is syntactically a valid location and break out the pieces.
bool
Loc_String_Parse(TextView const &loc, TextView &host_token, int &port)
{
  TextView port_token, rest;
  if (swoc::IPEndpoint::tokenize(loc, &host_token, &port_token, &rest) && rest.size() == 0) {
    if (port_token.empty()) {
      port = 0;
      return true;
    }

    if (auto n = svtou(port_token, &rest);
        rest.size() == port_token.size() && 0 < n && n <= std::numeric_limits<in_port_t>::max()) {
      port = n;
      return true;
    }
  }
  return false;
}

/// Set the location in a URL, accepting either a string or a tuple of <host, port>.
void
URL_Loc_Set(Context &ctx, Expr &expr, ts::URL &url)
{
  auto     value = ctx.extract(expr);
  TextView host_token;
  int      port = -1; // if still -1 after parsing, the parsing failed.
  if (auto loc = std::get_if<IndexFor(STRING)>(&value); nullptr != loc) {
    // split the string to get the pieces.
    Loc_String_Parse(*loc, host_token, port);
  } else if (auto t = std::get_if<IndexFor(TUPLE)>(&value); nullptr != t) {
    // Must be host name, then port.
    if (t->size() > 0) {
      if (auto &f0 = (*t)[0]; f0.index() == IndexFor(STRING)) {
        host_token = std::get<IndexFor(STRING)>(f0);
        if (t->size() > 1) {
          auto &f1 = (*t)[1];
          if (is_empty(f1)) {
            port = 0; // clear port.
          } else {
            port = f1.as_integer(-1); // try as integer, fail if not convertible.
          }
        }
      }
    }
  }
  if (0 <= port && port < std::numeric_limits<in_port_t>::max()) {
    url.host_set(host_token);
    url.port_set(port); // if @a port is 0 then it will be removed from the URL.
  }
}

/// Set the location in the Host field and URL (if needed).
void
Req_Loc_Set(Context &ctx, Expr &expr, ts::HttpRequest &req)
{
  auto     value = ctx.extract(expr);
  TextView host_token;
  int      port = -1; // if still -1 after parsing, the parsing failed.
  if (auto loc = std::get_if<IndexFor(STRING)>(&value); nullptr != loc && Loc_String_Parse(*loc, host_token, port)) {
    req.field_obtain(ts::HTTP_FIELD_HOST).assign(*loc);
  } else if (auto t = std::get_if<IndexFor(TUPLE)>(&value); nullptr != t) {
    if (t->size() > 0) { // host name, then port.
      if (auto &f0 = (*t)[0]; f0.index() == IndexFor(STRING)) {
        host_token = std::get<IndexFor(STRING)>(f0);
        if (t->size() > 1) {
          auto &f1 = (*t)[1];
          if (is_empty(f1)) {
            port = 0; // clear port.
          } else {
            port = f1.as_integer(-1); // try as integer, fail if not convertible.
          }
        } else {
          port = 0; // no port element, clear port.
        }
        auto                    buffer = ctx.transient_buffer(host_token.size() + 1 + std::numeric_limits<in_port_t>::digits10);
        swoc::FixedBufferWriter w{buffer};
        w.write(host_token);
        if (port > 0) {
          w.write(':');
          bwformat(w, swoc::bwf::Spec::DEFAULT, port);
        }
        req.field_obtain(ts::HTTP_FIELD_HOST).assign(w.view());
        ctx.transient_discard();
      }
    }
  }

  // If the field was set, set the URL to match if it has a host.
  if (port >= 0) {
    if (auto url{req.url()}; url.is_valid() && !url.host().empty()) {
      url.host_set(host_token);
      url.port_set(port);
    }
  }
}

} // namespace

class Do_ua_req_url_loc : public Directive
{
  using super_type = Directive;
  using self_type  = Do_ua_req_url_loc;

public:
  static inline const std::string KEY = "ua-req-url-loc";
  static const HookMask           HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_url_loc(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Feature expression.
};

const HookMask Do_ua_req_url_loc::HOOKS = MaskFor(Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP);

Do_ua_req_url_loc::Do_ua_req_url_loc(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_url_loc::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      URL_Loc_Set(ctx, _expr, url);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_url_loc::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                        YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy({STRING, TUPLE})) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {} or a {} of 2 elements.)", KEY, drtv_node.Mark(), STRING,
                  TUPLE);
  }
  return Handle(new self_type{std::move(expr)});
}

// ---

class Do_proxy_req_url_loc : public Directive
{
  using super_type = Directive;
  using self_type  = Do_proxy_req_url_loc;

public:
  static inline const std::string KEY = "proxy-req-url-loc";
  static const HookMask           HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_proxy_req_url_loc(Expr &&expr);

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Feature expression.
};

const HookMask Do_proxy_req_url_loc::HOOKS = MaskFor(Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP);

Do_proxy_req_url_loc::Do_proxy_req_url_loc(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_url_loc::invoke(Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      URL_Loc_Set(ctx, _expr, url);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_url_loc::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                           YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy({STRING, TUPLE})) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {} or a {} of 2 elements.)", KEY, drtv_node.Mark(), STRING,
                  TUPLE);
  }
  return Handle(new self_type{std::move(expr)});
}

/* ------------------------------------------------------------------------------------ */
/** Set the host for the request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_ua_req_host : public Directive
{
  using super_type = Directive;      ///< Parent type.
  using self_type  = Do_ua_req_host; ///< Self reference type.
public:
  static inline const std::string KEY{"ua-req-host"}; ///< Directive name.
  static const HookMask           HOOKS;              ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_host(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_ua_req_host::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_host::Do_ua_req_host(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_host::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    auto value = ctx.extract(_expr);
    if (auto host = std::get_if<IndexFor(STRING)>(&value); nullptr != host) {
      hdr.host_set(*host);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_host::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                     YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the port for the user agent request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_ua_req_port : public Directive
{
  using super_type = Directive;      ///< Parent type.
  using self_type  = Do_ua_req_port; ///< Self reference type.
public:
  static inline const std::string KEY{"ua-req-port"}; ///< Directive name.
  static const HookMask           HOOKS;              ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_port(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_ua_req_port::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_port::Do_ua_req_port(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_port::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    auto value = ctx.extract(_expr);
    if (auto port = value.as_integer(-1); port >= 0) {
      hdr.port_set(port);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_port::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                     YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(INTEGER)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), INTEGER);
  }
  return Handle(new self_type(std::move(expr)));
}

// ---

/** Set the port for the proxy request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_proxy_req_port : public Directive
{
  using super_type = Directive;         ///< Parent type.
  using self_type  = Do_proxy_req_port; ///< Self reference type.
public:
  static inline const std::string KEY{"proxy-req-port"}; ///< Directive name.
  static const HookMask           HOOKS;                 ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_proxy_req_port(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_proxy_req_port::HOOKS{MaskFor(Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP)};

Do_proxy_req_port::Do_proxy_req_port(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_port::invoke(Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    auto value = ctx.extract(_expr);
    if (auto port = value.as_integer(-1); port >= 0) {
      hdr.port_set(port);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_port::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                        YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(INTEGER)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), INTEGER);
  }
  return Handle(new self_type(std::move(expr)));
}

/* ------------------------------------------------------------------------------------ */
/** Set the host for the request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_proxy_req_host : public Directive
{
  using super_type = Directive;         ///< Parent type.
  using self_type  = Do_proxy_req_host; ///< Self reference type.
public:
  static inline const std::string KEY{"proxy-req-host"};      ///< Directive name.
  static inline const HookMask    HOOKS{MaskFor(Hook::PREQ)}; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_proxy_req_host(Expr &&fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Host feature.
};

Do_proxy_req_host::Do_proxy_req_host(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata
Do_proxy_req_host::invoke(Context &ctx)
{
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    hdr.host_set(host);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_host::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                        YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the location for the user agent request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_ua_req_loc : public Directive
{
  using super_type = Directive;     ///< Parent type.
  using self_type  = Do_ua_req_loc; ///< Self reference type.
public:
  static inline const std::string KEY{"ua-req-loc"}; ///< Directive name.
  static const HookMask           HOOKS;             ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_ua_req_loc(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_ua_req_loc::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_loc::Do_ua_req_loc(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_loc::invoke(Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    Req_Loc_Set(ctx, _expr, hdr);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_loc::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                    YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy({STRING, TUPLE})) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {} or a {}.)", KEY, drtv_node.Mark(), STRING, TUPLE);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the location for the proxy request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_proxy_req_loc : public Directive
{
  using super_type = Directive;        ///< Parent type.
  using self_type  = Do_proxy_req_loc; ///< Self reference type.
public:
  static inline const std::string KEY{"proxy-req-loc"}; ///< Directive name.
  static const HookMask           HOOKS;                ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression.
   */
  explicit Do_proxy_req_loc(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_proxy_req_loc::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_loc::Do_proxy_req_loc(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_loc::invoke(Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    Req_Loc_Set(ctx, _expr, hdr);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_loc::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                       YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy({STRING, TUPLE})) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {} or a {}.)", KEY, drtv_node.Mark(), STRING, TUPLE);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the scheme for the inbound request.
 */
class Do_ua_req_scheme : public Directive
{
  using super_type = Directive;        ///< Parent type.
  using self_type  = Do_ua_req_scheme; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_ua_req_scheme(Expr &&fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Scheme expression.
};

const std::string Do_ua_req_scheme::KEY{"ua-req-scheme"};
const HookMask    Do_ua_req_scheme::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_scheme::Do_ua_req_scheme(Expr &&fmt) : _expr(std::move(fmt)) {}

Errata
Do_ua_req_scheme::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_expr))};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    hdr.url().scheme_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_scheme::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                       YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the URL for the inbound request.
 */
class Do_ua_req_url : public Directive
{
  using super_type = Directive;     ///< Parent type.
  using self_type  = Do_ua_req_url; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_ua_req_url(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< URL expression.
};

const std::string Do_ua_req_url::KEY{"ua-req-url"};
const HookMask    Do_ua_req_url::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_url::Do_ua_req_url(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_url::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_expr))};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    hdr.url_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_url::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                    YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the scheme for the outbound request.
 */
class Do_proxy_req_scheme : public Directive
{
  using super_type = Directive;           ///< Parent type.
  using self_type  = Do_proxy_req_scheme; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_proxy_req_scheme(Expr &&fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_proxy_req_scheme::KEY{"proxy-req-scheme"};
const HookMask    Do_proxy_req_scheme::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_scheme::Do_proxy_req_scheme(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata
Do_proxy_req_scheme::invoke(Context &ctx)
{
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    hdr.url().scheme_set(host);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_scheme::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                          YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the URL for the outbound request.
 */
class Do_proxy_req_url : public Directive
{
  using super_type = Directive;        ///< Parent type.
  using self_type  = Do_proxy_req_url; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression for the URL.
   */
  explicit Do_proxy_req_url(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< URL expression.
};

const std::string Do_proxy_req_url::KEY{"proxy-req-url"};
const HookMask    Do_proxy_req_url::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_url::Do_proxy_req_url(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_proxy_req_url::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_expr))};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    hdr.url_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_url::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                       YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a {}.)", KEY, drtv_node.Mark(), STRING);
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
class Do_did_remap : public Directive
{
  using self_type = Do_did_remap;

public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature expression..
   *
   * @param expr Feature expression for the URL.
   */
  explicit Do_did_remap(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Boolean to set whether remap was done.
};

const std::string Do_did_remap::KEY{"did-remap"};
const HookMask    Do_did_remap::HOOKS{MaskFor(Hook::REMAP)};

Do_did_remap::Do_did_remap(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_did_remap::invoke(Context &ctx)
{
  auto f            = ctx.extract(_expr);
  ctx._remap_status = f.as_bool() ? TSREMAP_DID_REMAP : TSREMAP_NO_REMAP;
  return {};
}

Rv<Directive::Handle>
Do_did_remap::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                   YAML::Node key_value)
{
  // Default, with no value, is @c true.
  if (key_value.IsNull()) {
    return Handle{new self_type(Expr(true))};
  }
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing value of "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(BOOLEAN)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be convertible to a {}.)", KEY, drtv_node.Mark(), BOOLEAN);
  }
  return Handle{new self_type{std::move(expr)}};
}

/* ------------------------------------------------------------------------------------ */
/** Do the remap.
 */
class Do_apply_remap_rule : public Directive
{
  using super_type = Directive;           ///< Parent type.
  using self_type  = Do_apply_remap_rule; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);
};

const std::string Do_apply_remap_rule::KEY{"apply-remap-rule"};
const HookMask    Do_apply_remap_rule::HOOKS{MaskFor(Hook::REMAP)};

Errata
Do_apply_remap_rule::invoke(Context &ctx)
{
  ctx._remap_status = TSREMAP_DID_REMAP;
  // This is complex because the internal logic is as well. A bit fragile, but this is
  // really only useful as a backwards compatibility fix for pre ATS 9 and should eventually
  // be removed.
  // Copy over the host and port.
  ts::URL replacement_url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl};
  ts::URL target_url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl};
  ts::URL request_url{ctx._remap_info->requestBufp, ctx._remap_info->requestUrl};

  in_port_t port = replacement_url.port();
  // decanonicalize the port - may need to dig in and see if it was explicitly set.
  if ((port == 80 && replacement_url.scheme() == ts::URL_SCHEME_HTTP) ||
      (port == 443 && replacement_url.scheme() == ts::URL_SCHEME_HTTPS)) {
    port = 0;
  }
  request_url.port_set(port);
  request_url.host_set(replacement_url.host());
  if (ts::HttpRequest{ctx._remap_info->requestBufp, ctx._remap_info->requestHdrp}.method() != "CONNECT"_tv) {
    request_url.scheme_set(replacement_url.scheme());
    // Update the path as needed.
    auto replacement_path{replacement_url.path()};
    auto target_path{target_url.path()};
    auto request_path{request_url.path()};

    // Need to do better - see if Context can provide an ArenaWriter?
    swoc::LocalBufferWriter<(1 << 16) - 1> url_w;
    url_w.write(replacement_path);
    if (request_path.size() > target_path.size()) {
      // Always slash separate the replacement from the remnant of the incoming request path.
      if (url_w.size() && url_w.view()[url_w.size() - 1] != '/') {
        url_w.write('/');
      }
      // Already have the separating slash, trim it from the target path.
      url_w.write(request_path.substr(target_path.size()).ltrim('/'));
    }
    request_url.path_set(TextView{url_w.view()}.ltrim('/'));
  };

  return {};
}

swoc::Rv<Directive::Handle>
Do_apply_remap_rule::load(Config &, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &, YAML::Node)
{
  return Handle(new self_type);
}
/* ------------------------------------------------------------------------------------ */
/** Set the path for the request.
 */
class Do_ua_req_path : public Directive
{
  using super_type = Directive;      ///< Parent type.
  using self_type  = Do_ua_req_path; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a expr.
   *
   * @param expr Feature for host.
   */
  explicit Do_ua_req_path(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const std::string Do_ua_req_path::KEY{"ua-req-path"};
const HookMask    Do_ua_req_path::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_path::Do_ua_req_path(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_path::invoke(Context &ctx)
{
  auto value{ctx.extract(_expr)};
  if (auto text = std::get_if<IndexFor(STRING)>(&value); text) {
    if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
      hdr.url().path_set(*text);
    }
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_path::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                     YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the fragment for the request.
 */
class Do_ua_req_fragment : public Directive
{
  using super_type = Directive;          ///< Parent type.
  using self_type  = Do_ua_req_fragment; ///< Self reference type.
public:
  static inline const std::string KEY{"ua-req-fragment"}; ///< Directive name.
  static const HookMask           HOOKS;                  ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param expr Feature for host.
   */
  explicit Do_ua_req_fragment(Expr &&expr);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const HookMask Do_ua_req_fragment::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_fragment::Do_ua_req_fragment(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_fragment::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_expr))};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    hdr.url().fragment_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_fragment::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                         YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (expr.is_null()) {
    expr = Feature{FeatureView::Literal(""_tv)};
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the path for the request.
 */
class Do_proxy_req_path : public Directive
{
  using super_type = Directive;         ///< Parent type.
  using self_type  = Do_proxy_req_path; ///< Self reference type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_proxy_req_path(Expr &&fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_proxy_req_path::KEY{"proxy-req-path"};
const HookMask    Do_proxy_req_path::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_path::Do_proxy_req_path(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata
Do_proxy_req_path::invoke(Context &ctx)
{
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    hdr.url().path_set(host);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_path::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                        YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the fragment for the request.
 */
class Do_proxy_req_fragment : public Directive
{
  using super_type = Directive;             ///< Parent type.
  using self_type  = Do_proxy_req_fragment; ///< Self reference type.
public:
  static inline const std::string KEY{"proxy-req-fragment"}; ///< Directive name.
  static const HookMask           HOOKS;                     ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  explicit Do_proxy_req_fragment(Expr &&fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const HookMask Do_proxy_req_fragment::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_fragment::Do_proxy_req_fragment(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata
Do_proxy_req_fragment::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    hdr.url().fragment_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_fragment::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                            swoc::TextView const &, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
class FieldDirective : public Directive
{
  using self_type  = FieldDirective; ///< Self reference type.
  using super_type = Directive;      ///< Parent type.
protected:
  TextView _name; ///< Field name.
  Expr     _expr; ///< Value for field.

  /** Base constructor.
   *
   * @param name Name of the field.
   * @param expr Value to assign to the field.
   */
  FieldDirective(TextView const &name, Expr &&expr);

  /** Load from configuration.
   *
   * @param cfg Configuration.
   * @param maker Subclass maker.
   * @param key Name of the key identifying the directive.
   * @param name Field name (directive argumnet).
   * @param key_value  Value of the node for @a key.
   * @return An instance of the directive for @a key, or errors.
   */
  static Rv<Handle> load(Config &cfg, std::function<Handle(TextView const &name, Expr &&fmt)> const &maker, TextView const &key,
                         TextView const &name, YAML::Node key_value);

  /** Invoke the directive on an HTTP header.
   *
   * @param ctx Runtime context.
   * @param hdr HTTP header.
   * @return Errors, if any.
   */
  Errata invoke_on_hdr(Context &ctx, ts::HttpHeader &&hdr);

  /** Get the directive name (key).
   *
   * @return The directive key.
   *
   * Used by subclasses to provide the key for diagnostics.
   */
  virtual swoc::TextView key() const = 0;

  template <typename T>
  void
  apply(Context &, ts::HttpHeader &&, T const &)
  {
  }

  /// Visitor to perform the assignment.
  struct Apply {
    Context        &_ctx;   ///< Runtime context.
    ts::HttpHeader &_hdr;   ///< HTTP Header to modify.
    ts::HttpField   _field; ///< Working field.
    TextView const &_name;  ///< Field name.

    Apply(Context &ctx, ts::HttpHeader &hdr, TextView const &name) : _ctx(ctx), _hdr(hdr), _field(hdr.field(name)), _name(name) {}

    /// Clear all duplicate fields past @a _field.
    void
    clear_dups()
    {
      if (_field.is_valid()) {
        for (auto nf = _field.next_dup(); nf.is_valid();) {
          nf.destroy();
        }
      }
    }

    /// Assigned @a text to a single field, creating as needed.
    /// @return @c true if a field value was changed, @c false if the current value is @a text.
    void
    assign(TextView const &text)
    {
      if (_field.is_valid()) {
        if (_field.value() != text) {
          _field.assign(text);
        }
      } else {
        _hdr.field_create(_name).assign(text);
      }
    }

    /// Nil / NULL means destroy the field.
    void
    operator()(feature_type_for<NIL>)
    {
      if (_field.is_valid()) {
        this->clear_dups();
        _field.destroy();
      }
    }

    /// Assign the string, clear out any dups.
    void
    operator()(feature_type_for<STRING> &text)
    {
      this->assign(text);
      this->clear_dups();
    }

    /// Assign the tuple elements to duplicate fields.
    void
    operator()(feature_type_for<TUPLE> &t)
    {
      for (auto const &tf : t) {
        auto text{std::get<STRING>(tf.join(_ctx, ", "_tv))};
        // skip to next equal field, destroying mismatched fields.
        // once @a _field becomes invalid, it remains in that state.
        while (_field.is_valid() && _field.value() != text) {
          auto tmp = _field.next_dup();
          _field.destroy();
          _field = std::move(tmp);
        }
        this->assign(text);
        _field = _field.next_dup(); // does nothing if @a _field is invalid.
      }
      if (_field.is_valid()) {
        this->clear_dups(); // Any remaining fields need to be cleaned up.
        _field.destroy();
      }
    }

    // Other types, convert to string
    template <typename T>
    auto
    operator()(T &&t) -> EnableForFeatureTypes<T, void>
    {
      this->assign(_ctx.template render_transient([&t](BufferWriter &w) { bwformat(w, bwf::Spec::DEFAULT, t); }));
      this->clear_dups();
    }
  };
};

FieldDirective::FieldDirective(TextView const &name, Expr &&expr) : _name(name), _expr(std::move(expr)) {}

Errata
FieldDirective::invoke_on_hdr(Context &ctx, ts::HttpHeader &&hdr)
{
  if (hdr.is_valid()) {
    auto value{ctx.extract(this->_expr)};
    std::visit(Apply(ctx, hdr, _name), value);
    return {};
  }
  return Errata(S_ERROR, R"(Failed to assign field value due to invalid HTTP header.)");
}

auto
FieldDirective::load(Config &cfg, std::function<Handle(TextView const &, Expr &&)> const &maker, TextView const &key,
                     TextView const &arg, YAML::Node key_value) -> Rv<Handle>
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing value for "{}".)", key);
    return std::move(errata);
  }

  auto expr_type = expr.result_type();
  if (!expr_type.has_value()) {
    return Errata(S_ERROR, R"(Directive "{}" must have a value.)", key);
  }
  return maker(cfg.localize(arg), std::move(expr));
}

// -- Implementations --

/* ------------------------------------------------------------------------------------ */
/// Set transaction level debuggging for this transaction.
class Do_txn_debug : public Directive
{
  using self_type  = Do_txn_debug; ///< Self reference type.
  using super_type = Directive;    ///< Parent type.
public:
  static inline const std::string KEY{"txn-debug"}; ///< Directive name.

  /// Valid hooks for directive.
  static inline const HookMask HOOKS{
    MaskFor({Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Whether transaction debug is enabled or disabled.

  Do_txn_debug(Expr &&msg);
};

Do_txn_debug::Do_txn_debug(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_txn_debug::invoke(Context &ctx)
{
  auto f = ctx.extract(_expr);
  ctx._txn.enable_debug(f.as_bool());
  return {};
}

Rv<Directive::Handle>
Do_txn_debug::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                   YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return std::move(errata);
  }
  return {Handle{new self_type{std::move(expr)}}};
}

// --
class Do_ua_req_field : public FieldDirective
{
  using self_type  = Do_ua_req_field;
  using super_type = FieldDirective;

public:
  static const std::string KEY;   ///< Directive key.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  using super_type::invoke;
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView
  key() const override
  {
    return KEY;
  }
};

const std::string Do_ua_req_field::KEY{"ua-req-field"};
const HookMask    Do_ua_req_field::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Errata
Do_ua_req_field::invoke(Context &ctx)
{
  return this->invoke_on_hdr(ctx, ctx.ua_req_hdr());
}

Rv<Directive::Handle>
Do_ua_req_field::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                      YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}

// --
class Do_proxy_req_field : public FieldDirective
{
  using self_type  = Do_proxy_req_field;
  using super_type = FieldDirective;

public:
  static const std::string KEY;   ///< Directive key.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView
  key() const override
  {
    return KEY;
  }
};

const std::string Do_proxy_req_field::KEY{"proxy-req-field"};
const HookMask    Do_proxy_req_field::HOOKS{MaskFor({Hook::PREQ})};

Errata
Do_proxy_req_field::invoke(Context &ctx)
{
  return this->invoke_on_hdr(ctx, ctx.proxy_req_hdr());
}

Rv<Directive::Handle>
Do_proxy_req_field::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                         YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}

// --
class Do_proxy_rsp_field : public FieldDirective
{
  using self_type  = Do_proxy_rsp_field;
  using super_type = FieldDirective;

public:
  static const std::string KEY;   ///< Directive key.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView
  key() const override
  {
    return KEY;
  }
};

const std::string Do_proxy_rsp_field::KEY{"proxy-rsp-field"};
const HookMask    Do_proxy_rsp_field::HOOKS{MaskFor(Hook::PRSP)};

Errata
Do_proxy_rsp_field::invoke(Context &ctx)
{
  return this->invoke_on_hdr(ctx, ctx.proxy_rsp_hdr());
}

Rv<Directive::Handle>
Do_proxy_rsp_field::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                         YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}

// --
class Do_upstream_rsp_field : public FieldDirective
{
  using self_type  = Do_upstream_rsp_field;
  using super_type = FieldDirective;

public:
  static const std::string KEY;   ///< Directive key.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView
  key() const override
  {
    return KEY;
  }
};

const std::string Do_upstream_rsp_field::KEY{"upstream-rsp-field"};
const HookMask    Do_upstream_rsp_field::HOOKS{MaskFor(Hook::URSP)};

Errata
Do_upstream_rsp_field::invoke(Context &ctx)
{
  return this->invoke_on_hdr(ctx, ctx.upstream_rsp_hdr());
}

Rv<Directive::Handle>
Do_upstream_rsp_field::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                            YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}
/* ------------------------------------------------------------------------------------ */
/// Set upstream response status code.
class Do_upstream_rsp_status : public Directive
{
  using self_type  = Do_upstream_rsp_status; ///< Self reference type.
  using super_type = Directive;              ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Return status.

  Do_upstream_rsp_status() = default;
};

const std::string Do_upstream_rsp_status::KEY{"upstream-rsp-status"};
const HookMask    Do_upstream_rsp_status::HOOKS{MaskFor({Hook::URSP})};

Errata
Do_upstream_rsp_status::invoke(Context &ctx)
{
  int     status = TS_HTTP_STATUS_NONE;
  Feature value  = ctx.extract(_expr);
  auto    vtype  = value.value_type();
  if (INTEGER == vtype) {
    status = std::get<IndexFor(INTEGER)>(value);
  } else if (TUPLE == vtype) {
    auto t = std::get<IndexFor(TUPLE)>(value);
    if (0 < t.count() && t.count() <= 2) {
      if (t[0].value_type() != INTEGER) {
        return Errata(S_ERROR, R"(Tuple for "{}" must be an integer and a string.)", KEY);
      }
      status = std::get<IndexFor(INTEGER)>(t[0]);
      if (t.count() == 2) {
        if (t[1].value_type() != STRING) {
          return Errata(S_ERROR, R"(Tuple for "{}" must be an integer and a string.)", KEY);
        }
        ctx._txn.ursp_hdr().reason_set(std::get<IndexFor(STRING)>(t[1]));
      }
    } else {
      return Errata(S_ERROR, R"(Tuple for "{}" has {} elements, instead of there required 1 or 2.)", KEY, t.size());
    }
  }
  if (100 <= status && status <= 599) {
    ctx._txn.ursp_hdr().status_set(static_cast<TSHttpStatus>(status));
  } else {
    return Errata(S_ERROR, R"(Status value {} out of range 100..599 for "{}".)", status, KEY);
  }
  return {};
}

Rv<Directive::Handle>
Do_upstream_rsp_status::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                             swoc::TextView const &, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  auto   self = new self_type;
  Handle handle(self);

  auto expr_type = expr.result_type();
  if (!expr_type.can_satisfy({INTEGER, TUPLE})) {
    return Errata(S_ERROR, R"(Value for "{}" at {} is not an integer or tuple as required.)", KEY, drtv_node.Mark());
  }
  self->_expr = std::move(expr);
  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Set upstream response reason phrase.
class Do_upstream_reason : public Directive
{
  using self_type  = Do_upstream_reason; ///< Self reference type.
  using super_type = Directive;          ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Expr         _fmt;                          ///< Reason phrase.

  Do_upstream_reason() = default;
};

const std::string Do_upstream_reason::KEY{"upstream-reason"};
const HookMask    Do_upstream_reason::HOOKS{MaskFor({Hook::URSP})};

Errata
Do_upstream_reason::invoke(Context &ctx)
{
  auto value = ctx.extract(_fmt);
  if (STRING != value.value_type()) {
    return Errata(S_ERROR, R"(Value for "{}" is not a string.)", KEY);
  }
  ctx._txn.ursp_hdr().reason_set(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle>
Do_upstream_reason::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                         YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(The value for "{}" must be a string.)", KEY, drtv_node.Mark());
  }
  auto   self = new self_type;
  Handle handle(self);

  self->_fmt = std::move(expr);

  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Set proxy response status code.
class Do_proxy_rsp_status : public Directive
{
  using self_type  = Do_proxy_rsp_status; ///< Self reference type.
  using super_type = Directive;           ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Return status.

  Do_proxy_rsp_status() = default;
};

const std::string Do_proxy_rsp_status::KEY{"proxy-rsp-status"};
const HookMask    Do_proxy_rsp_status::HOOKS{MaskFor({Hook::PRSP})};

Errata
Do_proxy_rsp_status::invoke(Context &ctx)
{
  int     status = TS_HTTP_STATUS_NONE;
  Feature value  = ctx.extract(_expr);
  auto    vtype  = value.value_type();
  if (INTEGER == vtype) {
    status = std::get<IndexFor(INTEGER)>(value);
  } else if (TUPLE == vtype) {
    auto t = std::get<IndexFor(TUPLE)>(value);
    if (0 < t.count() && t.count() <= 2) {
      if (t[0].value_type() != INTEGER) {
        return Errata(S_ERROR, R"(Tuple for "{}" must be an integer and a string.)", KEY);
      }
      status = std::get<IndexFor(INTEGER)>(t[0]);
      if (t.count() == 2) {
        if (t[1].value_type() != STRING) {
          return Errata(S_ERROR, R"(Tuple for "{}" must be an integer and a string.)", KEY);
        }
        ctx._txn.prsp_hdr().reason_set(std::get<IndexFor(STRING)>(t[1]));
      }
    } else {
      return Errata(S_ERROR, R"(Tuple for "{}" has {} elements, instead of there required 1 or 2.)", KEY, t.size());
    }
  }
  if (100 <= status && status <= 599) {
    ctx._txn.prsp_hdr().status_set(static_cast<TSHttpStatus>(status));
  } else {
    return Errata(S_ERROR, R"(Status value {} out of range 100..599 for "{}".)", status, KEY);
  }
  return {};
}

Rv<Directive::Handle>
Do_proxy_rsp_status::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                          YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  auto   self = new self_type;
  Handle handle(self);

  auto expr_type = expr.result_type();
  if (!expr_type.can_satisfy(MaskFor({INTEGER, TUPLE}))) {
    return Errata(S_ERROR, R"(Value for "{}" at {} is not an integer or tuple as required.)", KEY, drtv_node.Mark());
  }
  self->_expr = std::move(expr);
  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Set proxy response reason phrase.
class Do_proxy_rsp_reason : public Directive
{
  using self_type  = Do_proxy_rsp_reason; ///< Self reference type.
  using super_type = Directive;           ///< Parent type.
public:
  static inline const std::string KEY{"proxy-rsp-reason"};      ///< Directive name.
  static inline const HookMask    HOOKS{MaskFor({Hook::PRSP})}; ///< Valid hooks for directive.

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Expr         _expr;                         ///< Reason phrase.

  Do_proxy_rsp_reason() = default;
};

Errata
Do_proxy_rsp_reason::invoke(Context &ctx)
{
  auto value = ctx.extract(_expr);
  if (STRING != value.value_type()) {
    return Errata(S_ERROR, R"(Value for "{}" is not a string.)", KEY);
  }
  ctx._txn.prsp_hdr().reason_set(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle>
Do_proxy_rsp_reason::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                          YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(The value for "{}" must be a string.)", KEY, drtv_node.Mark());
  }
  auto   self = new self_type;
  Handle handle(self);

  self->_expr = std::move(expr);

  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Set proxy response (error) body.
class Do_proxy_rsp_body : public Directive
{
  using self_type  = Do_proxy_rsp_body; ///< Self reference type.
  using super_type = Directive;         ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Body content.

  Do_proxy_rsp_body() = default;
};

const std::string Do_proxy_rsp_body::KEY{"proxy-rsp-body"};
const HookMask    Do_proxy_rsp_body::HOOKS{MaskFor({Hook::PRSP})};

Errata
Do_proxy_rsp_body::invoke(Context &ctx)
{
  TextView body, mime{"text/html"};
  auto     value = ctx.extract(_expr);
  if (STRING == value.value_type()) {
    body = std::get<IndexFor(STRING)>(value);
  } else if (auto tp = std::get_if<IndexFor(TUPLE)>(&value); tp) {
    if (tp->count() == 2) {
      if (auto ptr = std::get_if<IndexFor(STRING)>(&(*tp)[0]); ptr) {
        body = *ptr;
      }
      if (auto ptr = std::get_if<IndexFor(STRING)>(&(*tp)[1]); ptr) {
        mime = *ptr;
      }
    } else {
      return Errata(S_ERROR, R"(Value for "{}" is not a list of length 2.)", KEY);
    }
  } else {
    return Errata(S_ERROR, R"(Value for "{}" is not a string nor a list.)", KEY);
  }
  ctx._txn.error_body_set(body, mime);
  return {};
}

Rv<Directive::Handle>
Do_proxy_rsp_body::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                        YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy({STRING, ActiveType::TupleOf(STRING)})) {
    return Errata(S_ERROR, R"(The value for "{}" must be a string or a list of two strings.)", KEY, drtv_node.Mark());
  }
  auto   self = new self_type;
  Handle handle(self);

  self->_expr = std::move(expr);

  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Replace the upstream response body with a feature.
class Do_upstream_rsp_body : public Directive
{
  using self_type  = Do_upstream_rsp_body; ///< Self reference type.
  using super_type = Directive;            ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Body content.

  Do_upstream_rsp_body(Expr &&expr) : _expr(std::move(expr)) {}
};

const std::string Do_upstream_rsp_body::KEY{"upstream-rsp-body"};
const HookMask    Do_upstream_rsp_body::HOOKS{MaskFor({Hook::URSP})};

Errata
Do_upstream_rsp_body::invoke(Context &ctx)
{
  /// State data for the transform continuation.
  /// @internal Due to ugliness in the plugin API where the final event for the @c Continuation
  /// can arrive after the transaction is destroyed, the @c IOBuffer needs to get cleaned up at
  /// transaction termination, not the final transform event. Therefore the destructor here does
  /// the cleanup, so that it can be marked for cleanup in the @c Context.
  struct State {
    TextView   _view;                ///< Source view for body.
    TSIOBuffer _tsio_buff = nullptr; ///< Buffer used to write body.
    /// Clean up the @c IOBuffer.
    ~State()
    {
      if (_tsio_buff) {
        TSIOBufferDestroy(_tsio_buff);
      }
    }
  };

  auto static transform = [](TSCont contp, TSEvent ev_code, void *) -> int {
    if (TSVConnClosedGet(contp)) {
      // IOBuffer is cleaned up at transaction close, not here.
      TSContDestroy(contp);
      return 0;
    }

    auto in_vio = TSVConnWriteVIOGet(contp);
    switch (ev_code) {
    case TS_EVENT_ERROR:
      TSContCall(TSVIOContGet(in_vio), TS_EVENT_ERROR, in_vio);
      break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    default:
      // Consume all input data.
      auto in_todo   = TSVIONTodoGet(in_vio);
      auto in_reader = TSVIOReaderGet(in_vio);
      if (in_reader && in_todo) {
        auto avail = TSIOBufferReaderAvail(in_reader);
        in_todo    = std::min(in_todo, avail);
        if (in_todo > 0) {
          TSIOBufferReaderConsume(in_reader, in_todo);
          TSVIONDoneSet(in_vio, TSVIONDoneGet(in_vio) + in_todo);
          TSContCall(TSVIOContGet(in_vio),
                     (TSVIONTodoGet(in_vio) <= 0) ? TS_EVENT_VCONN_WRITE_COMPLETE : TS_EVENT_VCONN_WRITE_READY, in_vio);
        }
        // If the buffer isn't already there, create it and write out the view.
        if (auto state = static_cast<State *>(TSContDataGet(contp)); state && !state->_tsio_buff) {
          auto out_vconn    = TSTransformOutputVConnGet(contp);
          state->_tsio_buff = TSIOBufferCreate();
          TSIOBufferWrite(state->_tsio_buff, state->_view.data(), state->_view.size());
          auto out_vio = TSVConnWrite(out_vconn, contp, TSIOBufferReaderAlloc(state->_tsio_buff), state->_view.size());
          TSVIOReenable(out_vio);
        }
      }
      break;
    }

    return 0;
  };

  auto      value        = ctx.extract(_expr);
  auto      vtype        = value.value_type();
  TextView *content      = nullptr;
  TextView  content_type = "text/html";
  if (STRING == vtype) {
    content = &std::get<IndexFor(STRING)>(value);
  } else if (TUPLE == vtype) {
    auto t = std::get<IndexFor(TUPLE)>(value);
    if (t.size() > 0) {
      if (STRING == t[0].value_type()) {
        content = &std::get<IndexFor(STRING)>(t[0]);
        if (t.size() > 1 && STRING == t[1].value_type()) {
          content_type = std::get<IndexFor(STRING)>(t[1]);
        }
      }
    }
  }

  if (content) {
    // The view contents are in the transaction data, but the view in the feature is not.
    // Put a copy in the transform @a state.
    auto state = ctx.make<State>();
    ctx.mark_for_cleanup(state);
    auto cont    = TSTransformCreate(transform, ctx._txn);
    state->_view = *content;
    TSContDataSet(cont, state);
    TSHttpTxnHookAdd(ctx._txn, TS_HTTP_RESPONSE_TRANSFORM_HOOK, cont);
    ctx._txn.ursp_hdr().field_obtain("Content-Type"_tv).assign(content_type);
  }

  return {};
}

Rv<Directive::Handle>
Do_upstream_rsp_body::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                           YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(The value for "{}" must be a string.)", KEY, drtv_node.Mark());
  }

  return Handle(new self_type(std::move(expr)));
}
// ---
/// Immediate proxy reply.
class Do_proxy_reply : public Directive
{
  using self_type  = Do_proxy_reply; ///< Self reference type.
  using super_type = Directive;      ///< Parent type.

  /// Per context information.
  /// This is what is stored in the span @c CfgInfo::_ctx_span
  struct CtxInfo {
    TextView _reason; ///< Status reason string.
  };

public:
  inline static const std::string KEY        = "proxy-reply"; ///< Directive name.
  inline static const std::string STATUS_KEY = "status";      ///< Key for status value.
  inline static const std::string REASON_KEY = "reason";      ///< Key for reason value.
  inline static const std::string BODY_KEY   = "body";        ///< Key for body.

  static inline const HookMask HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP})}; ///< Valid hooks for directive.

  /// Need to do fixups on a later hook.
  static constexpr Hook FIXUP_HOOK = Hook::PRSP;

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

  static Errata cfg_init(Config &cfg, CfgStaticData const *);

protected:
  using index_type = FeatureGroup::index_type;

  FeatureGroup _fg; ///< Support cross references among the keys.

  int        _status = 0; ///< Return status is literal, 0 => extract at runtime.
  index_type _status_idx; ///< Return status.
  index_type _reason_idx; ///< Status reason text.
  index_type _body_idx;   ///< Body content of respons.
  /// Bounce from fixup hook directive back to @a this.
  Directive::Handle _fixup{new LambdaDirective([this](Context &ctx) -> Errata { return this->fixup(ctx); })};

  Errata load_status();

  /// Do post invocation fixup.
  Errata fixup(Context &ctx);
};

Errata
Do_proxy_reply::cfg_init(Config &cfg, CfgStaticData const *)
{
  cfg.reserve_slot(FIXUP_HOOK); // needed to fix up "Location" field in proxy response.
  return {};
}

Errata
Do_proxy_reply::invoke(Context &ctx)
{
  auto ctx_info = ctx.obtain_named_object<CtxInfo>(KEY);

  // Is a fix up hook required to set the reason correctly?
  bool need_hook_p = false;

  // Finalize the reason and stash it in context storage.
  if (_reason_idx != FeatureGroup::INVALID_IDX) {
    Feature reason = _fg.extract(ctx, _reason_idx);
    if (reason.index() == IndexFor(STRING)) {
      ctx.commit(reason);
      need_hook_p       = ctx_info->_reason.empty(); // hook needed if this is first to set reason.
      ctx_info->_reason = std::get<IndexFor(STRING)>(reason);
    }
  }

  // Set the status to prevent the upstream request.
  if (_status) {
    ctx._txn.status_set(static_cast<TSHttpStatus>(_status));
  } else {
    auto &&[status, errata]{_fg.extract(ctx, _status_idx).as_integer(-1)};
    if (100 <= status && status <= 599) {
      ctx._txn.status_set(static_cast<TSHttpStatus>(status));
    }
  }

  // Set the body.
  if (_body_idx != FeatureGroup::INVALID_IDX) {
    auto body{_fg.extract(ctx, _body_idx)};
    ctx._txn.error_body_set(std::get<IndexFor(STRING)>(body), "text/html");
  }

  // Arrange for fixup to get invoked.
  if (need_hook_p) {
    ctx.on_hook_do(FIXUP_HOOK, _fixup.get());
  }
  return {};
}

Errata
Do_proxy_reply::fixup(Context &ctx)
{
  if (auto ctx_info = ctx.named_object<CtxInfo>(KEY); ctx_info) {
    if (!ctx_info->_reason.empty()) {
      auto hdr{ctx.proxy_rsp_hdr()};
      hdr.reason_set(ctx_info->_reason);
    }
  }
  return {};
}

Errata
Do_proxy_reply::load_status()
{
  _status_idx = _fg.index_of(STATUS_KEY);

  FeatureGroup::ExprInfo &info = _fg[_status_idx];

  if (info._expr.is_literal()) {
    auto &&[status, errata]{std::get<Feature>(info._expr._raw).as_integer(-1)};
    if (!errata.is_ok()) {
      errata.note("While load key '{}' for directive '{}'", STATUS_KEY, KEY);
      return std::move(errata);
    }
    if (!(100 <= status && status <= 599)) {
      return Errata(S_ERROR, R"(Value for '{}' key in {} directive is not a positive integer 100..599 as required.)", STATUS_KEY,
                    KEY);
    }
    _status = status;
  } else if (!info._expr.result_type().can_satisfy(MaskFor(STRING, INTEGER))) {
    return Errata(S_ERROR, R"({} is not an integer nor string as required.)", STATUS_KEY);
  }
  return {};
}

Rv<Directive::Handle>
Do_proxy_reply::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                     YAML::Node key_value)
{
  Handle handle{new self_type};
  Errata errata;
  auto   self = static_cast<self_type *>(handle.get());
  if (key_value.IsScalar()) {
    errata = self->_fg.load_as_scalar(cfg, key_value, STATUS_KEY);
  } else if (key_value.IsSequence()) {
    errata = self->_fg.load_as_tuple(cfg, key_value,
                                     {
                                       {STATUS_KEY, FeatureGroup::REQUIRED},
                                       {REASON_KEY}
    });
  } else if (key_value.IsMap()) {
    errata = self->_fg.load(cfg, key_value,
                            {
                              {STATUS_KEY, FeatureGroup::REQUIRED},
                              {REASON_KEY},
                              {BODY_KEY}
    });
  } else {
    return Errata(S_ERROR, R"(Value for "{}" key at {} is must be a scalar, a list, or a map and is not.)", KEY, key_value.Mark());
  }
  if (!errata.is_ok()) {
    errata.note(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  self->_reason_idx = self->_fg.index_of(REASON_KEY);
  self->_body_idx   = self->_fg.index_of(BODY_KEY);
  errata.note(self->load_status());

  if (!errata.is_ok()) {
    errata.note(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  return {std::move(handle), {}};
}
// ---
/* ------------------------------------------------------------------------------------ */
class Do_remap_redirect : public Directive
{
  using self_type  = Do_remap_redirect; ///< Self reference type.
  using super_type = Directive;         ///< Parent type.
public:
  static inline const HookMask    HOOKS{MaskFor(Hook::REMAP)}; ///< Valid hooks for directive.
  static inline const std::string KEY = "remap-redirect";      ///< Directive name.

  Errata            invoke(Context &ctx) override; ///< Runtime activation.
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);
};

Rv<Directive::Handle>
Do_remap_redirect::load(Config &, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &, YAML::Node)
{
  return Handle{new self_type};
}

Errata
Do_remap_redirect::invoke(Context &ctx)
{
  if (ctx._remap_info) {
    ctx._remap_info->redirect = 1;
    ctx._remap_status         = TSREMAP_DID_REMAP;
  }
  return {};
}
/* ------------------------------------------------------------------------------------ */
/// Redirect.
/// Although this could technically be done "by hand", it's common enough to justify
/// a specific directive.
class Do_redirect : public Directive
{
  using self_type  = Do_redirect; ///< Self reference type.
  using super_type = Directive;   ///< Parent type.

  /// Per context information, used for fix up on proxy response hook.
  /// -- doc Do_redirect::CtxInfo
  struct CtxInfo {
    TextView _location; ///< Redirect target location.
    TextView _reason;   ///< Status text.
  };

public:
  inline static const std::string KEY          = "redirect"; ///< Directive name.
  inline static const std::string STATUS_KEY   = "status";   ///< Key for status value.
  inline static const std::string REASON_KEY   = "reason";   ///< Key for reason value.
  inline static const std::string LOCATION_KEY = "location"; ///< Key for location value.
  inline static const std::string BODY_KEY     = "body";     ///< Key for body.

  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Need to do fixups on a later hook.
  static constexpr Hook FIXUP_HOOK = Hook::PRSP;
  /// Status code to use if not specified.
  static const int DEFAULT_STATUS = TS_HTTP_STATUS_MOVED_PERMANENTLY;

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

  /// Configuration level initialization.
  static Errata cfg_init(Config &cfg, CfgStaticData const *rtti);

protected:
  using index_type = FeatureGroup::index_type;

  FeatureGroup _fg; ///< Support cross references among the keys.

  int        _status = 0;   ///< Return status is literal, 0 => extract at runtime.
  index_type _status_idx;   ///< Return status.
  index_type _reason_idx;   ///< Status reason text.
  index_type _location_idx; ///< Location field value.
  index_type _body_idx;     ///< Body content of respons.
  /// Bounce from fixup hook directive back to @a this.
  Directive::Handle _set_location{new LambdaDirective([this](Context &ctx) -> Errata { return this->fixup(ctx); })};

  Errata load_status();

  /// Do post invocation fixup.
  Errata fixup(Context &ctx);
};

const HookMask Do_redirect::HOOKS{MaskFor(Hook::PRE_REMAP, Hook::REMAP)};

Errata
Do_redirect::cfg_init(Config &cfg, CfgStaticData const *)
{
  cfg.reserve_slot(FIXUP_HOOK); // needed to fix up "Location" field in proxy response.
  return {};
}
// -- doc Do_redirect::cfg_init

Errata
Do_redirect::invoke(Context &ctx)
{
  auto ctx_info = ctx.obtain_named_object<CtxInfo>(KEY);

  // If the Location view is empty, it hasn't been set and therefore the clean up hook
  // hasn't been set either, so need to do that.
  bool need_hook_p = ctx_info->_location.empty();

  // Finalize the location and stash it in context storage.
  Feature location = _fg.extract(ctx, _location_idx);
  if (location.index() == IndexFor(STRING)) {
    ctx.commit(location);
    ctx_info->_location = std::get<IndexFor(STRING)>(location);
  } else {
    return Errata(S_ERROR, "{} directive - '{}' was not a string as required.", KEY, LOCATION_KEY);
  }

  // Set the status to prevent the upstream request.
  if (_status) {
    ctx._txn.status_set(static_cast<TSHttpStatus>(_status));
  } else {
    Feature value = _fg.extract(ctx, _status_idx);
    auto &&[status, errata]{value.as_integer(DEFAULT_STATUS)};
    if (!(100 <= status && status <= 599)) {
      status = DEFAULT_STATUS;
    }
    ctx._txn.status_set(static_cast<TSHttpStatus>(status));
  }
  // Set the reason.
  if (_reason_idx != FeatureGroup::INVALID_IDX) {
    auto reason{_fg.extract(ctx, _reason_idx)};
    if (reason.index() == IndexFor(STRING)) {
      ctx.commit(reason);
      ctx_info->_reason = std::get<IndexFor(STRING)>(reason);
    }
  }
  // Set the body.
  if (_body_idx != FeatureGroup::INVALID_IDX) {
    auto body{_fg.extract(ctx, _body_idx)};
    ctx._txn.error_body_set(std::get<IndexFor(STRING)>(body), "text/html");
  }
  // Arrange for fixup to get invoked.
  if (need_hook_p) {
    ctx.on_hook_do(FIXUP_HOOK, _set_location.get());
  }
  return {};
}

Errata
Do_redirect::fixup(Context &ctx)
{
  if (auto ctx_info = ctx.named_object<CtxInfo>(KEY); ctx_info) {
    auto hdr{ctx.proxy_rsp_hdr()};

    auto field{hdr.field_obtain(ts::HTTP_FIELD_LOCATION)};
    field.assign(ctx_info->_location);

    if (!ctx_info->_reason.empty()) {
      hdr.reason_set(ctx_info->_reason);
    }
  }
  return {};
}

Errata
Do_redirect::load_status()
{
  _status_idx = _fg.index_of(STATUS_KEY);

  if (_status_idx == FeatureGroup::INVALID_IDX) { // not present, use default value.
    _status = DEFAULT_STATUS;
    return {};
  }

  FeatureGroup::ExprInfo &info = _fg[_status_idx];

  if (info._expr.is_literal()) {
    auto &&[status, errata]{std::get<Feature>(info._expr._raw).as_integer(DEFAULT_STATUS)};
    _status = status;
    if (!errata.is_ok()) {
      errata.note("While load key '{}' for directive '{}'", STATUS_KEY, KEY);
      return std::move(errata);
    }
    if (!(100 <= status && status <= 599)) {
      return Errata(S_ERROR, R"(Value for '{}' key in {} directive is not a positive integer 100..599 as required.)", STATUS_KEY,
                    KEY);
    }
  } else {
    auto rtype = info._expr.result_type();
    if (rtype != STRING && rtype != INTEGER) {
      return Errata(S_ERROR, R"({} is not an integer nor string as required.)", STATUS_KEY);
    }
  }
  return {};
}

Rv<Directive::Handle>
Do_redirect::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                  YAML::Node key_value)
{
  Handle handle{new self_type};
  Errata errata;
  auto   self = static_cast<self_type *>(handle.get());
  if (key_value.IsScalar()) {
    errata = self->_fg.load_as_scalar(cfg, key_value, LOCATION_KEY);
  } else if (key_value.IsSequence()) {
    errata = self->_fg.load_as_tuple(cfg, key_value,
                                     {
                                       {STATUS_KEY,   FeatureGroup::REQUIRED},
                                       {LOCATION_KEY, FeatureGroup::REQUIRED}
    });
  } else if (key_value.IsMap()) {
    errata = self->_fg.load(cfg, key_value,
                            {
                              {LOCATION_KEY, FeatureGroup::REQUIRED},
                              {STATUS_KEY},
                              {REASON_KEY},
                              {BODY_KEY}
    });
  } else {
    return Errata(S_ERROR, R"(Value for "{}" key at {} is must be a scalar, a list, or a map and is not.)", KEY, key_value.Mark());
  }
  if (!errata.is_ok()) {
    errata.note(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  self->_reason_idx   = self->_fg.index_of(REASON_KEY);
  self->_body_idx     = self->_fg.index_of(BODY_KEY);
  self->_location_idx = self->_fg.index_of(LOCATION_KEY);
  errata.note(self->load_status());

  if (!errata.is_ok()) {
    errata.note(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  return {std::move(handle), {}};
}
/* ------------------------------------------------------------------------------------ */
/// Send a debug message.
class Do_debug : public Directive
{
  using self_type  = Do_debug;
  using super_type = Directive;

public:
  static const std::string KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata            invoke(Context &ctx) override;
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _tag;
  Expr _msg;

  Do_debug(Expr &&tag, Expr &&msg);
};

const std::string Do_debug::KEY{"debug"};
const HookMask    Do_debug::HOOKS{MaskFor({Hook::POST_LOAD, Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP,
                                           Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

Do_debug::Do_debug(Expr &&tag, Expr &&msg) : _tag(std::move(tag)), _msg(std::move(msg)) {}

Errata
Do_debug::invoke(Context &ctx)
{
  [[maybe_unused]] TextView tag = ctx.extract_view(_tag, {Context::EX_COMMIT, Context::EX_C_STR});
  TextView                  msg = ctx.extract_view(_msg);
  TS_DBG("%.*s", static_cast<int>(msg.size()), msg.data());
  return {};
}

Rv<Directive::Handle>
Do_debug::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
               YAML::Node key_value)
{
  if (key_value.IsScalar()) {
    auto &&[msg_fmt, msg_errata] = cfg.parse_expr(key_value);
    if (!msg_errata.is_ok()) {
      msg_errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
      return {{}, std::move(msg_errata)};
    }
    return {Handle{new self_type{Expr{Config::PLUGIN_TAG}, std::move(msg_fmt)}}, {}};
  } else if (key_value.IsSequence()) {
    if (key_value.size() > 2) {
      return Errata(S_ERROR, R"(Value for "{}" key at {} is not a list of two strings as required.)", KEY, key_value.Mark());
    } else if (key_value.size() < 1) {
      return Errata(S_ERROR, R"(The list value for "{}" key at {} does not have at least one string as required.)", KEY,
                    key_value.Mark());
    }
    auto &&[tag_expr, tag_errata] = cfg.parse_expr(key_value[0]);
    if (!tag_errata.is_ok()) {
      tag_errata.note(R"(While parsing tag at {} for "{}" directive at {}.)", key_value[0].Mark(), KEY, drtv_node.Mark());
      return std::move(tag_errata);
    }
    auto &&[msg_expr, msg_errata] = cfg.parse_expr(key_value[1]);
    if (!tag_errata.is_ok()) {
      tag_errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value[1].Mark(), KEY, drtv_node.Mark());
      return std::move(tag_errata);
    }
    return Handle(new self_type(std::move(tag_expr), std::move(msg_expr)));
  }
  return Errata(S_ERROR, R"(Value for "{}" key at {} is not a string or a list of strings as required.)", KEY, key_value.Mark());
}

/* ------------------------------------------------------------------------------------ */
/// Log an Error message.
class Do_error : public Directive
{
  using self_type  = Do_error;
  using super_type = Directive;

public:
  static inline const std::string KEY{"error"};
  static const HookMask           HOOKS; ///< Valid hooks for directive.

  Errata            invoke(Context &ctx) override;
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _msg;

  Do_error(Expr &&msg);
};

const HookMask Do_error::HOOKS{MaskFor({Hook::POST_LOAD, Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP,
                                        Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

Do_error::Do_error(Expr &&msg) : _msg(std::move(msg)) {}

Errata
Do_error::invoke(Context &ctx)
{
  TextView msg = ctx.extract_view(_msg);
  ts::Log_Error(msg);
  return {};
}

Rv<Directive::Handle>
Do_error::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
               YAML::Node key_value)
{
  auto &&[msg_fmt, msg_errata] = cfg.parse_expr(key_value);
  if (!msg_errata.is_ok()) {
    msg_errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(msg_errata)};
  }
  return {Handle{new self_type{std::move(msg_fmt)}}};
}

/// Log an notify message.
class Do_note : public Directive
{
  using self_type  = Do_note;   ///< Self reference type.
  using super_type = Directive; ///< Super type.
public:
  static inline const std::string KEY{"note"}; ///< Name of directive.
  /// Valid hooks for this directive.
  static inline const HookMask HOOKS{MaskFor(Hook::POST_LOAD, Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP,
                                             Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP)};

  /// Runtime invocation.
  Errata invoke(Context &ctx) override;

  /// Load from configuration.
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _msg; ///< Message to log.

  /** Constructor.
   *
   * @param msg Parsed feature expression for message.
   */
  Do_note(Expr &&msg);
};
// -- doc end Do_note

Do_note::Do_note(Expr &&msg) : _msg(std::move(msg)) {}

// -- doc note::invoke
Errata
Do_note::invoke(Context &ctx)
{
  TextView msg = ctx.extract_view(_msg);
  ts::Log_Note(msg);
  return {};
}
// -- doc note::invoke

// -- doc note::load
Rv<Directive::Handle>
Do_note::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
              YAML::Node key_value)
{
  auto &&[msg_fmt, msg_errata] = cfg.parse_expr(key_value);
  if (!msg_errata.is_ok()) {
    msg_errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return std::move(msg_errata);
  }
  return Handle{new self_type{std::move(msg_fmt)}};
}
// -- doc note::load

/// Log an warning message.
class Do_warning : public Directive
{
  using self_type  = Do_warning;
  using super_type = Directive;

public:
  static inline const std::string KEY{"warning"};
  static const HookMask           HOOKS; ///< Valid hooks for directive.

  Errata            invoke(Context &ctx) override;
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _msg;

  Do_warning(Expr &&msg);
};

const HookMask Do_warning::HOOKS{MaskFor({Hook::POST_LOAD, Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP,
                                          Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

Do_warning::Do_warning(Expr &&msg) : _msg(std::move(msg)) {}

Errata
Do_warning::invoke(Context &ctx)
{
  TextView msg = ctx.extract_view(_msg);
  ts::Log_Warning(msg);
  return {};
}

Rv<Directive::Handle>
Do_warning::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                 YAML::Node key_value)
{
  auto &&[msg_fmt, msg_errata] = cfg.parse_expr(key_value);
  if (!msg_errata.is_ok()) {
    msg_errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(msg_errata)};
  }
  return {Handle{new self_type{std::move(msg_fmt)}}};
}

/* ------------------------------------------------------------------------------------ */
/// Set the cache key.
class Do_cache_key : public Directive
{
  using self_type  = Do_cache_key; ///< Self reference type.
  using super_type = Directive;    ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Cache key.

  Do_cache_key(Expr &&fmt) : _fmt(std::move(fmt)) {}
};

const std::string Do_cache_key::KEY{"cache-key"};
const HookMask    Do_cache_key::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Errata
Do_cache_key::invoke(Context &ctx)
{
  auto value = ctx.extract(_fmt);
  ctx._txn.cache_key_assign(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle>
Do_cache_key::load(Config    &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &,
                   YAML::Node key_value)
{
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  return Handle(new self_type(std::move(fmt)));
}
/* ------------------------------------------------------------------------------------ */
/// Set a transaction configuration variable override.
class Do_txn_conf : public Directive
{
  using self_type  = Do_txn_conf; ///< Self reference type.
  using super_type = Directive;   ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr              _expr; ///< Value for override.
  ts::TxnConfigVar *_var = nullptr;

  Do_txn_conf(Expr &&fmt, ts::TxnConfigVar *var) : _expr(std::move(fmt)), _var(var) {}
};

const std::string Do_txn_conf::KEY{"txn-conf"};
const HookMask    Do_txn_conf::HOOKS{
  MaskFor({Hook::TXN_START, Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ})};

Errata
Do_txn_conf::invoke(Context &ctx)
{
  auto value = ctx.extract(_expr);
  if (value.index() == IndexFor(INTEGER)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(INTEGER)>(value));
  } else if (value.index() == IndexFor(BOOLEAN)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(BOOLEAN)>(value) ? 1L : 0L);
  } else if (value.index() == IndexFor(STRING)) {
    // Unfortunately although the interface doesn't appear to require C strings, in practice some of
    // the string overridables do (such as client cert file path).
    auto str = ctx.localize_as_c_str(std::get<IndexFor(STRING)>(value));
    ctx._txn.override_assign(*_var, str);
  } else if (value.index() == IndexFor(FLOAT)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(FLOAT)>(value));
  }
  return {};
}

Rv<Directive::Handle>
Do_txn_conf::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                  YAML::Node key_value)
{
  auto txn_var = ts::HttpTxn::find_override(arg);
  if (!txn_var) {
    return Errata(S_ERROR, R"("{}" is not recognized as an overridable transaction configuration variable.)", arg);
  }
  if (txn_var->type() != TS_RECORDDATATYPE_INT && txn_var->type() != TS_RECORDDATATYPE_STRING &&
      txn_var->type() != TS_RECORDDATATYPE_FLOAT) {
    return Errata(S_ERROR, R"("{}" is of type "{}" which is not currently supported.)", arg,
                  ts::TSRecordDataTypeNames[txn_var->type()]);
  }
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  return Handle(new self_type(std::move(fmt), txn_var));
}

/* ------------------------------------------------------------------------------------ */
/// Set the address for the upstream.
class Do_upstream_addr : public Directive
{
  using self_type  = Do_upstream_addr; ///< Self reference type.
  using super_type = Directive;        ///< Parent type.
public:
  static inline const std::string KEY{"upstream-addr"}; ///< Directive name.
  static const HookMask           HOOKS;                ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr              _expr; ///< Address.
  ts::TxnConfigVar *_var = nullptr;

  Do_upstream_addr(Expr &&expr) : _expr(std::move(expr)) {}
};

const HookMask Do_upstream_addr::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ})};

Errata
Do_upstream_addr::invoke(Context &ctx)
{
  auto value = ctx.extract(_expr);
  if (value.index() == IndexFor(IP_ADDR)) {
    ctx._txn.set_upstream_addr(std::get<IndexFor(IP_ADDR)>(value));
  }
  return {};
}

Rv<Directive::Handle>
Do_upstream_addr::load(Config    &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &,
                       YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  if (!expr.result_type().can_satisfy(IP_ADDR)) {
    return Errata(S_ERROR, R"(Value for "{}" must be an IP address.)");
  }

  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/// Set a transaction local variable.
class Do_var : public Directive
{
  using self_type  = Do_var;    ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY;   ///< Directive name.
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  TextView _name;  ///< Variable name.
  Expr     _value; ///< Value for variable.

  Do_var(TextView const &arg, Expr &&value) : _name(arg), _value(std::move(value)) {}
};

const std::string Do_var::KEY{"var"};
const HookMask    Do_var::HOOKS{
  MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ, Hook::URSP, Hook::PRSP})};

Errata
Do_var::invoke(Context &ctx)
{
  ctx.store_txn_var(_name, ctx.extract(_value));
  return {};
}

Rv<Directive::Handle>
Do_var::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
             YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  return Handle(new self_type(cfg.localize(arg), std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/// Internal transaction error control
class Do_txn_error : public Directive
{
  using self_type  = Do_txn_error;
  using super_type = Directive;

public:
  static inline const std::string KEY{"txn-error"};
  /// Valid hooks for directive.
  static inline const HookMask HOOKS{
    MaskFor({Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

  Errata            invoke(Context &ctx) override;
  static Rv<Handle> load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr;

  Do_txn_error(Expr &&msg);
};

Do_txn_error::Do_txn_error(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_txn_error::invoke(Context &ctx)
{
  ctx._global_status = ctx.extract(_expr).as_bool() ? TS_EVENT_HTTP_ERROR : TS_EVENT_HTTP_CONTINUE;
  return {};
}

Rv<Directive::Handle>
Do_txn_error::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                   YAML::Node key_value)
{
  auto &&[expr, errata] = cfg.parse_expr(key_value);
  if (!errata.is_ok()) {
    errata.note(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {std::move(errata)};
  }
  return {Handle{new self_type{std::move(expr)}}};
}

/* ------------------------------------------------------------------------------------ */
/** @c with directive.
 *
 * This is a core directive that has lots of special properties.
 */
class Do_with : public Directive
{
  using super_type = Directive;
  using self_type  = Do_with;

public:
  static const std::string KEY;
  static const std::string SELECT_KEY;
  static const std::string FOR_EACH_KEY;
  static const std::string CONTINUE_KEY;
  static const HookMask    HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr              _expr; ///< Feature expression
  Directive::Handle _do;   ///< Explicit actions.

  union {
    uint32_t all = 0;
    struct {
      unsigned for_each_p : 1; ///< Direct action is per tuple element.
      unsigned continue_p : 1; ///< Continue with directives after this - want default to be 0.
    } f;
  } _opt;

  /// A single case in the select.
  struct Case {
    Comparison::Handle _cmp; ///< Comparison to perform.
    Directive::Handle  _do;  ///< Directives to execute.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.

  Do_with() = default;

  Errata load_case(Config &cfg, YAML::Node node);
};

const std::string Do_with::KEY{"with"};
const std::string Do_with::SELECT_KEY{"select"};
const std::string Do_with::FOR_EACH_KEY{"for-each"};
const std::string Do_with::CONTINUE_KEY{"continue"};

const HookMask Do_with::HOOKS{MaskFor({Hook::POST_LOAD, Hook::TXN_START, Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP,
                                       Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP})};

Errata
Do_with::invoke(Context &ctx)
{
  Feature feature{ctx.extract(_expr)};
  ctx.commit(feature);
  Feature save{ctx._active};
  ctx._active = feature;

  if (_do) {
    if (_opt.f.for_each_p) {
      ctx._active_ext = feature;
      while (!is_nil(feature)) {
        ctx._active = car(feature);
        ctx.mark_terminal(false);
        _do->invoke(ctx);
        cdr(feature);
      }
      clear(feature);
      ctx._active_ext = NIL_FEATURE;
      // Iteration can potentially modify the extracted feature value, so if there are comparisons
      // reset the feature.
      if (!_cases.empty()) {
        ctx._active = feature = ctx.extract(_expr);
      }
    } else {
      ctx.mark_terminal(false);
      _do->invoke(ctx);
    }
  }

  ctx.mark_terminal(false); // default is continue on.
  for (auto const &c : _cases) {
    if (!c._cmp || (*c._cmp)(ctx, feature)) {
      if (c._do) {
        c._do->invoke(ctx);
      }
      ctx.mark_terminal(!_opt.f.continue_p); // successful compare, mark terminal.
      break;
    }
  }
  // Need to restore to previous state if nothing matched.
  clear(ctx._active);
  ctx._active = save;
  return {};
}

swoc::Rv<Directive::Handle>
Do_with::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
              YAML::Node key_value)
{
  // Need to parse this first, so the feature type can be determined.
  auto &&[expr, errata] = cfg.parse_expr(key_value);

  if (!errata.is_ok()) {
    return std::move(errata);
  }

  auto  *self = new self_type;
  Handle handle(self); // for return, and cleanup in case of error.
  self->_expr  = std::move(expr);
  auto f_scope = cfg.feature_scope(self->_expr.result_type());

  YAML::Node select_node{drtv_node[SELECT_KEY]};
  if (select_node) {
    if (select_node.IsMap()) {
      errata = self->load_case(cfg, select_node);
      if (!errata.is_ok()) {
        return std::move(errata);
      }
    } else if (select_node.IsSequence()) {
      for (YAML::Node child : select_node) {
        errata = (self->load_case(cfg, child));
        if (!errata.is_ok()) {
          errata.note(R"(While loading "{}" directive at {} in "{}" at {}.)", KEY, drtv_node.Mark(), SELECT_KEY,
                      select_node.Mark());
          return std::move(errata);
        }
      }
    } else {
      return Errata(S_ERROR, R"(The value for "{}" at {} in "{}" directive at {} is not a list or object.")", SELECT_KEY,
                    select_node.Mark(), KEY, drtv_node.Mark());
    }
  }

  YAML::Node continue_node{drtv_node[CONTINUE_KEY]};
  if (continue_node) {
    self->_opt.f.continue_p = true;
  }

  YAML::Node do_node{drtv_node[DO_KEY]};
  YAML::Node for_each_node{drtv_node[FOR_EACH_KEY]};
  if (do_node && for_each_node) {
    return Errata(S_ERROR, R"("{}" directive cannot have both "{}" and "{}" as keys - {}.)", DO_KEY, FOR_EACH_KEY,
                  drtv_node.Mark());
  } else if (do_node) {
    auto &&[do_handle, errata]{cfg.parse_directive(do_node)};
    if (errata.is_ok()) {
      self->_do = std::move(do_handle);
    } else {
      errata.note(R"(While parsing "{}" key at {} in selection case at {}.)", DO_KEY, do_node.Mark(), drtv_node.Mark());
      return std::move(errata);
    }
  } else if (for_each_node) {
    auto &&[fe_handle, errata]{cfg.parse_directive(for_each_node)};
    if (errata.is_ok()) {
      self->_do               = std::move(fe_handle);
      self->_opt.f.for_each_p = true;
    } else {
      errata.note(R"(While parsing "{}" key at {} in selection case at {}.)", FOR_EACH_KEY, for_each_node.Mark(), drtv_node.Mark());
      return std::move(errata);
    }
  }
  return handle;
}

Errata
Do_with::load_case(Config &cfg, YAML::Node node)
{
  if (node.IsMap()) {
    Case       c;
    YAML::Node do_node{node[DO_KEY]};
    // It's allowed to have no comparison, which is either an empty map or only a DO key.
    // In that case the comparison always matches.
    if (node.size() > 1 || (node.size() == 1 && !do_node)) {
      auto f_scope = cfg.feature_scope(_expr.result_type());
      auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, node)};
      if (cmp_errata.is_ok()) {
        c._cmp = std::move(cmp_handle);
      } else {
        return std::move(cmp_errata);
      }
    }

    if (do_node) {
      auto c_scope = cfg.capture_scope((c._cmp ? c._cmp->rxp_group_count() : 0), node.Mark().line);
      auto &&[handle, errata]{cfg.parse_directive(do_node)};
      if (errata.is_ok()) {
        c._do = std::move(handle);
      } else {
        errata.note(R"(While parsing "{}" key at {} in selection case at {}.)", DO_KEY, do_node.Mark(), node.Mark());
        return std::move(errata);
      }
    } else {
      c._do.reset(new NilDirective);
    }
    // Everything is fine, update the case load and return.
    _cases.emplace_back(std::move(c));
    return {};
  }
  return Errata(S_ERROR, R"(The value at {} for "{}" is not an object as required.")", node.Mark(), SELECT_KEY);
}

/* ------------------------------------------------------------------------------------ */
const std::string When::KEY{"when"};
const HookMask    When::HOOKS{
  MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

When::When(Hook hook_idx, Directive::Handle &&directive) : _hook(hook_idx), _directive(std::move(directive)) {}

// Put the internal directive in the directive array for the specified hook.
Errata
When::invoke(Context &ctx)
{
  return ctx.on_hook_do(_hook, _directive.get());
}

swoc::Rv<Directive::Handle>
When::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
           YAML::Node key_value)
{
  Errata zret;
  if (Hook hook{HookName[key_value.Scalar()]}; hook != Hook::INVALID) {
    if (YAML::Node do_node{drtv_node[DO_KEY]}; do_node) {
      auto save = cfg._hook;
      cfg._hook = hook;
      auto &&[do_handle, do_errata]{cfg.parse_directive(do_node)};
      cfg._hook = save;
      if (do_errata.is_ok()) {
        cfg.reserve_slot(hook);
        return {Handle{new self_type{hook, std::move(do_handle)}}, {}};
      } else {
        zret.note(do_errata);
        zret.note(R"(Failed to load directive in "{}" at {} in "{}" directive at {}.)", DO_KEY, do_node.Mark(), KEY,
                  key_value.Mark());
      }
    } else {
      zret.note(R"(The required "{}" key was not found in the "{}" directive at {}.")", DO_KEY, KEY, drtv_node.Mark());
    }
  } else {
    zret.note(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_value.Scalar(), When::KEY, key_value.Mark());
  }
  return {{}, std::move(zret)};
}

/* ------------------------------------------------------------------------------------ */

namespace
{
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<When>();
  Config::define<Do_with>();

  Config::define<Do_ua_req_field>();
  Config::define<Do_ua_req_url>();
  Config::define<Do_ua_req_url>("ua-url-host"_tv); // alias
  Config::define<Do_ua_req_url_host>();
  Config::define<Do_ua_req_url_port>();
  Config::define<Do_ua_req_url_loc>();
  Config::define<Do_ua_req_scheme>();
  Config::define<Do_ua_req_host>();
  Config::define<Do_ua_req_port>();
  Config::define<Do_ua_req_loc>();
  Config::define<Do_ua_req_path>();
  Config::define<Do_ua_req_fragment>();

  Config::define<Do_proxy_req_field>();
  Config::define<Do_proxy_req_url>();
  Config::define<Do_proxy_req_url_host>();
  Config::define<Do_proxy_req_url_port>();
  Config::define<Do_proxy_req_url_loc>();
  Config::define<Do_proxy_req_host>();
  Config::define<Do_proxy_req_port>();
  Config::define<Do_proxy_req_loc>();
  Config::define<Do_proxy_req_scheme>();
  Config::define<Do_proxy_req_path>();
  Config::define<Do_proxy_req_fragment>();

  Config::define<Do_upstream_rsp_field>();
  Config::define<Do_upstream_rsp_status>();
  Config::define<Do_upstream_reason>();

  Config::define<Do_upstream_addr>();

  Config::define<Do_proxy_rsp_field>();
  Config::define<Do_proxy_rsp_status>();
  Config::define<Do_proxy_rsp_reason>();
  Config::define<Do_proxy_rsp_body>();

  Config::define<Do_upstream_rsp_body>();

  Config::define<Do_cache_key>();
  Config::define<Do_txn_conf>();
  Config::define<Do_redirect>();
  Config::define<Do_remap_redirect>();
  Config::define<Do_proxy_reply>();
  Config::define<Do_debug>();
  Config::define<Do_note>();
  Config::define<Do_warning>();
  Config::define<Do_error>();
  Config::define<Do_txn_error>();
  Config::define<Do_txn_debug>();
  Config::define<Do_var>();

  Config::define<Do_apply_remap_rule>();
  Config::define<Do_did_remap>();

  return true;
}();
} // namespace
