/** @file
 * Regular expression support.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include "txn_box/common.h"
#include "txn_box/Rxp.h"
#include "txn_box/Config.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Rv<Rxp>
Rxp::parse(TextView const &str, Options const &options)
{
  int errc         = 0;
  size_t err_off   = 0;
  uint32_t rxp_opt = 0;
  if (options.f.nc) {
    rxp_opt = PCRE2_CASELESS;
  }
  auto result = pcre2_compile(reinterpret_cast<unsigned const char *>(str.data()), str.size(), rxp_opt, &errc, &err_off, nullptr);
  if (nullptr == result) {
    PCRE2_UCHAR err_buff[128];
    auto err_size = pcre2_get_error_message(errc, err_buff, sizeof(err_buff));
    return Errata(S_ERROR, R"(Failed to parse regular expression - error "{}" [{}] at offset {} in "{}".)",
                  TextView(reinterpret_cast<char const *>(err_buff), err_size), errc, err_off, str);
  }
  return {result};
};

int
Rxp::operator()(swoc::TextView text, pcre2_match_data *match) const
{
  return pcre2_match(_rxp.get(), reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), 0, 0, match, nullptr);
}

size_t
Rxp::capture_count() const
{
  uint32_t count = 0;
  auto result    = pcre2_pattern_info(_rxp.get(), PCRE2_INFO_CAPTURECOUNT, &count);
  return result == 0 ? count + 1 : 0; // output doesn't reflect capture group 0, apparently.
}
/* ------------------------------------------------------------------------------------ */
RxpOp::RxpOp(Rxp &&rxp) : _raw(std::move(rxp)) {}
RxpOp::RxpOp(Expr &&expr, Rxp::Options opt) : _raw(DynamicRxp{std::move(expr), opt}) {}

Rv<RxpOp>
RxpOp::Cfg_Visitor::operator()(Feature &f)
{
  if (IndexFor(STRING) != f.index()) {
    return Errata(S_ERROR, R"(Regular expression literal was not a string as required.)");
  }

  auto &&[rxp, rxp_errata]{Rxp::parse(std::get<IndexFor(STRING)>(f), _rxp_opt)};
  if (!rxp_errata.is_ok()) {
    rxp_errata.note(R"(While parsing regular expression.)");
    return std::move(rxp_errata);
  }
  _cfg.require_rxp_group_count(rxp.capture_count());
  return RxpOp(std::move(rxp));
}

Rv<RxpOp>
RxpOp::Cfg_Visitor::operator()(std::monostate)
{
  return Errata(S_ERROR, R"(Literal must be a string)");
}

Rv<RxpOp>
RxpOp::Cfg_Visitor::operator()(Expr::Direct &d)
{
  return RxpOp(Expr(std::move(d)), _rxp_opt);
}

Rv<RxpOp>
RxpOp::Cfg_Visitor::operator()(Expr::Composite &comp)
{
  return RxpOp(Expr(std::move(comp)), _rxp_opt);
}

Rv<RxpOp>
RxpOp::Cfg_Visitor::operator()(Expr::List &)
{
  return Errata(S_ERROR, R"(Literal must be a string)");
}

bool
RxpOp::Apply_Visitor::operator()(std::monostate) const
{
  return false;
}

bool
RxpOp::Apply_Visitor::operator()(const Rxp &rxp) const
{
  auto result = rxp(_src, _ctx.rxp_working_match_data());
  if (result > 0) {
    _ctx.rxp_commit_match(_src);
    _ctx._remainder.clear();
    return true;
  }
  return false;
}

bool
RxpOp::Apply_Visitor::operator()(DynamicRxp const &dr) const
{
  auto f = _ctx.extract(dr._expr);
  if (auto text = std::get_if<IndexFor(STRING)>(&f); text != nullptr) {
    auto &&[rxp, rxp_errata]{Rxp::parse(*text, dr._opt)};
    if (rxp_errata.is_ok()) {
      _ctx.rxp_match_require(rxp.capture_count());
      return (*this)(rxp); // forward to Rxp overload.
    }
  }
  return false;
}

Rv<RxpOp>
RxpOp::load(Config &cfg, Expr &&expr, Rxp::Options opt)
{
  return {std::visit(Cfg_Visitor(cfg, opt), expr._raw)};
}

int
RxpOp::operator()(Context &ctx, swoc::TextView src)
{
  return std::visit(Apply_Visitor{ctx, src}, _raw);
}

size_t
RxpOp::capture_count()
{
  if (_raw.index() == STATIC) {
    return std::get<STATIC>(_raw).capture_count();
  }
  return 0; // No rxp or indeterminate.
}
