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

BufferWriter &
bwformat(BufferWriter &w, ts::BWFSpec const &spec, RemapFilter::Action action)
{
  return w.write(action == RemapFilter::ALLOW ? "allow" : "deny");
}

ts::TextView
RemapFilter::add_arg(RemapArg const &arg, ts::FixedBufferWriter &errw)
{
  switch (arg.type) {
  case RemapArg::METHOD: {
    // Please remember that the order of hash idx creation is very important and it is defined
    // in HTTP.cc file. 0 in our array is the first method, CONNECT
    int m = hdrtoken_tokenize(arg.value.data(), arg.value.size(), nullptr) - HTTP_WKSIDX_CONNECT;

    if (m >= 0 && m < HTTP_WKSIDX_METHODS_CNT) {
      wk_method[m] = true;
    } else {
      Debug("url_rewrite", "[validate_filter_args] Using nonstandard method [%.*s]", static_cast<int>(arg.value.size()),
            arg.value.data());
      methods.emplace(arg.value);
    }
  }
  case RemapArg::SRC_IP: {
    bool invert_p = false;
    IpAddr min, max;
    auto range = arg.value; // may need to modify
    if (*range == '~') {
      invert_p = true;
      ++range;
    }
    if (0 == ats_ip_range_parse(range, min, max)) {
      if (invert_p) {
        // This is a bit ugly, but these values are actually hard to compute in a general and safe
        // manner.
        IpMap tmp;
        tmp.mark(IpMap::IP4_MIN_ADDR, IpMap::IP4_MAX_ADDR, this);
        tmp.unmark(min, max);
        for (auto const &r : tmp) {
          src_ip.fill(r.min(), r.max());
        }
      } else {
        src_ip.fill(min, max, this);
      }
    } else {
      errw.print("malformed IP address '{}' in filter option '{}'", arg.value, arg.tag);
      return errw.view();
    }
  }
  case RemapArg::PROXY_IP: {
    bool invert_p = false;
    IpAddr min, max;
    auto range = arg.value; // may need to modify
    if (*range == '~') {
      invert_p = true;
      ++range;
    }
    if (0 == ats_ip_range_parse(range, min, max)) {
      if (invert_p) {
        // This is a bit ugly, but these values are actually hard to compute in a general and safe
        // manner.
        IpMap tmp;
        tmp.mark(IpMap::IP4_MIN_ADDR, IpMap::IP4_MAX_ADDR, this);
        tmp.unmark(min, max);
        for (auto const &r : tmp) {
          src_ip.fill(r.min(), r.max());
        }
      } else {
        src_ip.fill(min, max, this);
      }
    } else {
      errw.print("malformed IP address '{}' in filter option '{}'", arg.value, arg.tag);
      return errw.view();
    }
  }
  case RemapArg::ACTION: {
    static constexpr std::array<std::string_view, 4> DENY_TAG  = {{"0", "off", "deny", "disable"}};
    static constexpr std::array<std::string_view, 4> ALLOW_TAG = {{"1", "on", "allow", "enable"}};
    if (DENY_TAG.end() != std::find_if(DENY_TAG.begin(), DENY_TAG.end(),
                                       [&](std::string_view tag) -> bool { return 0 == strcasecmp(tag, argv.value); })) {
      action = DENY;
    } else if (ALLOW_TAG.end() != std::find_if(DENY_TAG.begin(), DENY_TAG.end(),
                                               [&](std::string_view tag) -> bool { return 0 == strcasecmp(tag, argv.value); })) {
      action = ALLOW;
    } else {
      errw.print("Unrecognized value '{}' for filter option '{}'", arg.value, arg.tag);
      Debug("url_rewrite", "[validate_filter_args] %.*s", static_cast<int>(err.size()), errw.data());
      return errw.view();
    }
  }
  case RemapArg::INTERNAL: {
    internal_p = true;
  }
  }
  return {};
}

BufferWriter &
RemapFilter::inscribe(BufferWriter &w, ts::BWFSpec const &spec) const
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
