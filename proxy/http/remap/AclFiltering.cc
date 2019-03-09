/** @file

  ACL descriptors for remap rules.

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
 */

#include "tscore/Diags.h"
#include "tscore/ink_inet.h"
#include "AclFiltering.h"
#include "hdrs/HTTP.h"
#include "tscore/BufferWriter.h"
#include "tscore/bwf_std_format.h"
#include "swoc/Lexicon.h"
#include "ts_swoc_bwf_aux.h"

using ts::BufferWriter;

// ===============================================================================
//                              RemapFilter
// ===============================================================================

void
RemapFilter::reset()
{
  argv.clear();

  _action         = ENABLE;
  _internal_check = DO_NOT_CHECK;

  name.clear();
  // This should force all elements to @c false.
  wk_method.assign(HTTP_WKSIDX_METHODS_CNT, false);
  methods.clear();
  src_addr.clear();
  proxy_addr.clear();
}

RemapFilter::RemapFilter() : wk_method(HTTP_WKSIDX_METHODS_CNT, false) {}

RemapFilter &
RemapFilter::add_method(ts::TextView const &method)
{
  // Please remember that the order of hash idx creation is very important and it is defined
  // in HTTP.cc file. 0 in our array is the first method, CONNECT
  int m = hdrtoken_tokenize(method) - HTTP_WKSIDX_CONNECT;

  if (0 <= m && m < HTTP_WKSIDX_METHODS_CNT) {
    wk_method[m] = true;
  } else {
    Debug("url_rewrite", "[validate_filter_args] Using nonstandard method [%.*s]", static_cast<int>(method.size()), method.data());
    methods.emplace(method);
  }
  method_active_p = true;
  return *this;
}

RemapFilter &
RemapFilter::set_method_match_inverted(bool flag)
{
  method_invert_p = flag;
  return *this;
}

BufferWriter &
RemapFilter::describe(BufferWriter &w, ts::BWFSpec const &spec) const
{
  w.print("Filter {}: flags - action={} internal={:s}", ts::bwf::FirstOf(name, "N/A"), _action, _internal_check);
  w.print("\nArgs: ");
  auto pos = w.extent();
  for (auto const &arg : argv) {
    if (w.extent() > pos) {
      w.write(", ");
    }
    w.print("{}{}", arg.key, ts::bwf::OptionalAffix(arg.value, "", "="));
  }
  if (w.extent() == pos) {
    w.write("N/A");
  }

  w.write("\nMethods: ");
  pos = w.extent();
  for (unsigned idx = 0; idx < wk_method.size(); ++idx) {
    if (wk_method[idx]) {
      if (w.extent() > pos) {
        w.write(", ");
      }
      w.write(hdrtoken_index_to_wks(idx + HTTP_WKSIDX_CONNECT));
    }
  }
  for (auto const &name : methods) {
    if (w.extent() > pos) {
      w.write(", ");
    }
    w.write(name);
  }

  if (w.extent() == pos) {
    w.write("N/A");
  }
  w.write('\n');

  if (src_addr.count() > 0) {
    w.print("Source IP map: {}", src_addr);
  }
  if (proxy_addr.count() > 0) {
    w.print("Proxy IP map: {}", proxy_addr);
  }

  return w;
}

void
RemapFilter::mark_inverted(IpMap &map, IpAddr const &min, IpAddr const &max)
{
  // This is a bit ugly, but these values are actually hard to compute in a general and safe
  // manner, particularly the increment and decrement. Instead this marks everything, then
  // unmarks ( @a min, @a max ).
  IpMap tmp;
  if (min.isIp6()) {
    tmp.mark(IpMap::IP6_MIN_ADDR, IpMap::IP6_MAX_ADDR, this);
  } else {
    tmp.mark(IpMap::IP4_MIN_ADDR, IpMap::IP4_MAX_ADDR, this);
  }
  tmp.unmark(min, max);
  for (auto const &r : tmp) {
    map.mark(r.min(), r.max(), this);
  }
}

void
RemapFilter::mark(IpMap &map, IpAddr const &min, IpAddr const &max)
{
  map.mark(min, max, this);
}

RemapFilter &
RemapFilter::set_action(Action action)
{
  _action = action;
  return *this;
}

swoc::Errata
RemapFilter::set_action(ts::TextView value)
{
  static const swoc::Lexicon<Action> LEXICON{{ENABLE, {"enable", "true", "yes", "1"}}, {DISABLE, {"disable", "false", "no", "0"}}};
  static const std::string LEXICON_NAMES{swoc::bwstring("{}", swoc::bwf::LexiconPrimaryNames(LEXICON))};

  swoc::Errata erratum;

  try {
    _action = LEXICON[value];
  } catch (std::domain_error &ex) {
    erratum.error(R"(Invalid action "{}" - must be one of {})", value, LEXICON_NAMES);
  }

  return erratum;
}

swoc::Errata
RemapFilter::set_internal_check(ts::TextView value)
{
  static const swoc::Lexicon<TriState> LEXICON{{DO_NOT_CHECK, {"ignore", "whatever", "default"}},
                                               {REQUIRE_TRUE, {"yes", "enable", "true", "1"}},
                                               {REQUIRE_FALSE, {"no", "disable", "false", "0"}}};
  static const std::string LEXICON_NAMES{swoc::bwstring("{}", swoc::bwf::LexiconPrimaryNames(LEXICON))};

  swoc::Errata erratum;

  try {
    _internal_check = LEXICON[value];
  } catch (std::domain_error &ex) {
    erratum.error(R"(Invalid internal check value "{}" - must be one of {})", value, LEXICON_NAMES);
  }

  return erratum;
}

ts::BufferWriter &
ts::bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, RemapArg::Type t)
{
  static constexpr std::array<TextView, RemapArg::N_TYPES> NAMES{
    {{"INVALID"}, {"plugin"}, {"pparam"}, {"internal"}, {"method"}, {"map_id"}, {"action"}, {"src_ip"}, {"proxy_ip"}}};
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<std::underlying_type<RemapArg::Type>::type>(t));
  } else {
    bwformat(w, spec, NAMES[t]);
  }
  return w;
}

ts::BufferWriter &
ts::bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, RemapFilter::TriState state)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<std::underlying_type<RemapFilter::TriState>::type>(state));
  } else {
    switch (state) {
    case RemapFilter::DO_NOT_CHECK:
      bwformat(w, spec, "DO_NOT_CHECK");
      break;
    case RemapFilter::REQUIRE_FALSE:
      bwformat(w, spec, "FALSE");
      break;
    case RemapFilter::REQUIRE_TRUE:
      bwformat(w, spec, "TRUE");
      break;
    }
  }
  return w;
}

ts::BufferWriter &
ts::bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, RemapFilter::Action action)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<std::underlying_type<RemapFilter::Action>::type>(action));
  } else {
    bwformat(w, spec, action == RemapFilter::ENABLE ? "enabled"_sv : "disabled"_sv);
  }
  return w;
}
