/** @file

  A brief file description

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

#pragma once

#include <string>
#include <string_view>
#include <set>
#include <vector>
#include <bitset>

#include "tscpp/util/TextView.h"
#include "tscpp/util/IntrusiveDList.h"
#include "tscore/ink_inet.h"
#include "tscore/IpMap.h"
#include "tscore/BufferWriter.h"

extern int HTTP_WKSIDX_METHODS_CNT;

/** An argument to a remap rule.
 * This stores strings by reference - for persistent structures strings must be copied elsewhere.
 */
struct RemapArg {
  /// Type of argument (feature based).
  enum Type : int8_t {
    INVALID,      ///< Invalid / uninitialized.
    INTERNAL,     ///< Filter for internal requests.
    PLUGIN,       ///< Specify a plugin.
    PLUGIN_PARAM, ///< Specific a plugin parameter.
    METHOD,       ///< Filter by method.
    MAP_ID,       ///< Assign a rule ID.
    ACTION,       ///< ACL action.
    SRC_IP,       ///< Check user agent IP address.
    PROXY_IP,     ///< Check local inbound IP address (proxy address).
  };

  /** A description of an argument.
   * This enables mapping from string names to argument types.
   */
  struct Descriptor {
    ts::TextView name;     ///< Argument name.
    Type type;             ///< Type of argument of this @a name.
    bool value_required_p; ///< Argument must have a value.
  };
  /// List of supported arguments.
  static constexpr std::array<Descriptor, 9> Args = {{{"internal", INTERNAL, false},
                                                      {"plugin", PLUGIN, true},
                                                      {"pparam", PLUGIN_PARAM, true},
                                                      {"method", METHOD, true},
                                                      {"src-ip", SRC_IP, true},
                                                      {"proxy-ip", PROXY_IP, true},
                                                      {"in-ip", PROXY_IP, true},
                                                      {"action", ACTION, true},
                                                      {"mapid", MAP_ID, true}}};

  /** Construct from argument @a key and @a value.
   *
   * @param key Key.
   * @param value Value [depends on @a key].
   *
   * If the @a key is invalid or a value is required and not provided, the instance is
   * constructed with a @a type of @c INVALID.
   */
  RemapArg(ts::TextView key, ts::TextView value);

  /** Explicit construct with all values.
   *
   * @param t Type.
   * @param k Tag.
   * @param v Value.
   */
  RemapArg(Type t, ts::TextView k, ts::TextView v) : type(t), tag(k), value(v) {}

  Type type = INVALID; ///< Type of argument.
  ts::TextView tag;    ///< Name of the argument.
  ts::TextView value;  ///< Value. [depends on @a type]
};

/** A filter for a remap rule.
 *
 **/
class RemapFilter
{
private:
  void reset();

public:
  using self_type = RemapFilter; ///< Self reference type.

  /// Default constructor.
  RemapFilter();

  /** Set the name for the filter.
   *
   * @param text Name.
   * @return @a this
   *
   * Instances are not required to have names.
   */
  self_type &set_name(ts::TextView const &text);

  /** Add an argument to the filter.
   *
   * @param arg The argument instance.
   * @param errw Error buffer for reporting.
   * @return An error string on failure, an empty view on success.
   *
   * The @c RemapArg instances have views, not strings, and so must point to strings with a lifetime
   * longer than that of this instance. This is the responsibility of the caller (presumably the
   * instance owner).
   */
  ts::textView add_arg(RemapArg const &arg, ts::FixedBufferWriter &errw);

  ts::BufferWriter &inscribe(ts::BufferWriter &w, ts::BWFSpec const &spec) const;

  self_type *_next = nullptr; ///< Forward intrusive link.
  self_type *_prev = nullptr; ///< Backward instrusive link.
  using Linkage    = ts::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;

  enum Action : uint8_t { ALLOW, DENY } action = ALLOW;
  bool internal_p                              = false;

  std::string name; // optional filter name

  /** Array of arguments for the filter.
   * Each element is a tuple of (key, value).
   * These must be C-string compatible because the plugin API requires that.
   */
  std::vector<RemapArg> argv;

  // methods
  /// Flag vector for well known methods.
  std::vector<bool> wk_method;
  /// Set of method names.
  using MethodSet = std::set<std::string>;
  /// Set for methods that are not well known (built in to TS).
  MethodSet methods;

  /// Map of remote inbound addresses of interest.
  IpMap src_ip;
  /// Map of local inbound addresses of interest.
  IpMap proxy_ip;
};

using RemapFilterList = ts::IntrusiveDList<RemapFilter::Linkage>;

inline size_t
RemapFilter::add_arg(RemapArg const &arg)
{
  argv.emplace_back(arg);
  return argv.size();
}

inline RemapFilter &
RemapFilter::set_name(ts::TextView const &text)
{
  name = text;
  return *this;
}

namespace ts
{
inline BufferWriter &
bwformat(ts::BufferWriter &w, BWFSpec const &spec, RemapFilter const &rule)
{
  return rule.inscribe(w, spec);
}

} // namespace ts
