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

using ts::BufferWriter;

// ===============================================================================
//                              RemapFilter
// ===============================================================================

void
RemapFilter::reset()
{
  argv.clear();

  action     = ALLOW;
  internal_p = false;

  name.clear();
  // This should force all elements to @c false.
  wk_method.assign(HTTP_WKSIDX_METHODS_CNT, false);
  methods.clear();
  src_ip.clear();
  proxy_ip.clear();
}

RemapFilter::RemapFilter() : wk_method(HTTP_WKSIDX_METHODS_CNT, false) {}

ts::TextView
RemapFilter::add_arg(RemapArg const &arg, ts::FixedBufferWriter &errw)
{
  switch (arg.type) {
  case RemapArg::METHOD: {
    auto text{arg.value};
    if ('~' == *text) {
      ++text;
      method_invert_p = true;
    }
    while (text) {
      auto method = text.take_prefix_at(";,"_sv);
      if (method.empty()) {
        continue;
      }
      // Please remember that the order of hash idx creation is very important and it is defined
      // in HTTP.cc file. 0 in our array is the first method, CONNECT
      int m = hdrtoken_tokenize(method) - HTTP_WKSIDX_CONNECT;

      if (0 <= m && m < HTTP_WKSIDX_METHODS_CNT) {
        wk_method[m] = true;
      } else {
        Debug("url_rewrite", "[validate_filter_args] Using nonstandard method [%.*s]", static_cast<int>(arg.value.size()),
              arg.value.data());
        methods.emplace(arg.value);
      }
      method_active_p = true;
    }
  } break;
  case RemapArg::SRC_IP: {
    auto err_msg = this->add_ip_addr_arg(src_ip, arg.value, arg.tag, errw);
    if (!err_msg.empty()) {
      return err_msg;
    }
  } break;
  case RemapArg::PROXY_IP: {
    auto err_msg = this->add_ip_addr_arg(proxy_ip, arg.value, arg.tag, errw);
    if (!err_msg.empty()) {
      return err_msg;
    }
  } break;
  case RemapArg::ACTION: {
    static constexpr std::array<std::string_view, 4> DENY_TAG  = {{"0", "off", "deny", "disable"}};
    static constexpr std::array<std::string_view, 4> ALLOW_TAG = {{"1", "on", "allow", "enable"}};
    if (DENY_TAG.end() != std::find_if(DENY_TAG.begin(), DENY_TAG.end(),
                                       [&](std::string_view tag) -> bool { return 0 == strcasecmp(tag, arg.value); })) {
      action = DENY;
    } else if (ALLOW_TAG.end() != std::find_if(DENY_TAG.begin(), DENY_TAG.end(),
                                               [&](std::string_view tag) -> bool { return 0 == strcasecmp(tag, arg.value); })) {
      action = ALLOW;
    } else {
      errw.print("Unrecognized value '{}' for filter option '{}'", arg.value, arg.tag);
      Debug("url_rewrite", "[validate_filter_args] %.*s", static_cast<int>(errw.size()), errw.data());
      return errw.view();
    }
  } break;
  case RemapArg::INTERNAL: {
    internal_p = true;
  } break;
  default:
    ink_assert(false);
    break;
  }
  return {};
}

BufferWriter &
RemapFilter::describe(BufferWriter &w, ts::BWFSpec const &spec) const
{
  w.print("Filter {}: flags - action={} internal={:s}", ts::bwf::FirstOf(name, "N/A"), action, internal_p);
  w.print("\nArgs: ");
  auto pos = w.extent();
  for (auto const &arg : argv) {
    if (w.extent() > pos) {
      w.write(", ");
    }
    w.print("{}{}", arg.tag, ts::bwf::OptionalAffix(arg.value, "", "="));
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

  if (src_ip.count() > 0) {
    w.print("Source IP map: {}", src_ip);
  }
  if (proxy_ip.count() > 0) {
    w.print("Proxy IP map: {}", proxy_ip);
  }

  return w;
}

void
RemapFilter::fill_inverted(IpMap &map, IpAddr const &min, IpAddr const &max, void *mark)
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
    map.fill(r.min(), r.max());
  }
}

ts::TextView
RemapFilter::add_ip_addr_arg(IpMap &map, ts::TextView range, ts::TextView const &tag, ts::FixedBufferWriter &errw)
{
  bool invert_p = false;
  IpAddr min, max;
  if (*range == '~') {
    invert_p = true;
    ++range;
  }
  if (0 == ats_ip_range_parse(range, min, max)) {
    if (invert_p) {
      this->fill_inverted(map, min, max, this);
    } else {
      map.fill(min, max, this);
    }
  } else {
    errw.print("malformed IP address '{}' in argument '{}'", range, tag);
    return errw.view();
  }
  return {};
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
ts::bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, RemapFilter::Action action)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<std::underlying_type<RemapFilter::Action>::type>(action));
  } else {
    bwformat(w, spec, action == RemapFilter::ALLOW ? "Allow"_sv : "Deny"_sv);
  }
  return w;
}
