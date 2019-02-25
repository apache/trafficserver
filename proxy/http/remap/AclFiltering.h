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
    PLUGIN,       ///< Specify a plugin.
    PLUGIN_PARAM, ///< Specific a plugin parameter.
    INTERNAL,     ///< Filter for internal requests.
    METHOD,       ///< Filter by method.
    MAP_ID,       ///< Assign a rule ID.
    ACTION,       ///< ACL action.
    SRC_IP,       ///< Check user agent IP address.
    PROXY_IP,     ///< Check local inbound IP address (proxy address).
  };

  /// Number of values in @c Type
  static constexpr unsigned N_TYPES = 9;

  /** A description of an argument.
   * This enables mapping from string names to argument types.
   */
  struct Descriptor {
    ts::TextView name;     ///< Argument name.
    Type type;             ///< Type of argument of this @a name.
    bool value_required_p; ///< Argument must have a value.
  };

  /// List of supported arguments.
  /// @internal Note the value '9' here is not the same as @c N_TYPES because @c INVALID isn't
  /// listed here, and some are duplicates - it happens to work out to the same but it's not
  /// required.
  static constexpr std::array<Descriptor, 9> ArgTags = {{{"internal", INTERNAL, false},
                                                         {"plugin", PLUGIN, true},
                                                         {"pparam", PLUGIN_PARAM, true},
                                                         {"method", METHOD, true},
                                                         {"src-ip", SRC_IP, true},
                                                         {"proxy-ip", PROXY_IP, true},
                                                         {"in-ip", PROXY_IP, true},
                                                         {"action", ACTION, true},
                                                         {"mapid", MAP_ID, true}}};

  RemapArg() = default;

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
  ts::TextView add_arg(RemapArg const &arg, ts::FixedBufferWriter &errw);

  /** Generate formatted output describing this instance.
   *
   * @param w Output buffer.
   * @param spec Format specification.
   * @return @a w
   *
   * This is intended to be used for BW formatting.
   */
  ts::BufferWriter &describe(ts::BufferWriter &w, ts::BWFSpec const &spec) const;

  self_type *_next = nullptr; ///< Forward intrusive link.
  self_type *_prev = nullptr; ///< Backward instrusive link.
  using Linkage    = ts::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;
  using List       = ts::IntrusiveDList<Linkage>;

  /// Filter action.
  enum Action : uint8_t { ALLOW, DENY } action = ALLOW;
  /// Check if the transaction is internal.
  bool internal_p      = false;
  bool method_active_p = false; ///< Methods should be checked for matching.
  bool method_invert_p = false; ///< Match on not a listed method.

  std::string name; // optional filter name

  /** Array of arguments for the filter.
   * Each element is a tuple of (key, value).
   * These must be C-string compatible because the plugin API requires that.
   */
  std::vector<RemapArg> argv;

  // methods
  /// Flag vector for well known methods.
  /// @internal This must be a vector because the number of well known methods is computed at run time.
  std::vector<bool> wk_method;
  /// General method set.
  /// This contains methods that are not well known, i.e. are not built in to HTTP processing.
  using MethodSet = std::set<std::string_view>;
  /// Set for methods that are not well known (built in to TS).
  MethodSet methods;

  /// Map of remote inbound addresses of interest.
  IpMap src_ip;
  /// Map of local inbound addresses of interest.
  IpMap proxy_ip;

protected:
  /** Fill a @a map with the addresses not in [ @a min, @a max ].
   *
   * @param map Map to fill.
   * @param min Min address of excluded range.
   * @param max Max address of exclude range.
   * @param mark Value to use in the fill.
   */
  void fill_inverted(IpMap &map, IpAddr const &min, IpAddr const &max, void *mark);

  ts::TextView add_ip_addr_arg(IpMap &map, ts::TextView range, ts::TextView const &tag, ts::FixedBufferWriter &errw);
};

using RemapFilterList = RemapFilter::List;

inline RemapFilter &
RemapFilter::set_name(ts::TextView const &text)
{
  name = text;
  return *this;
}

namespace ts
{
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, RemapArg::Type type);

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, RemapFilter::Action action);

inline BufferWriter &
bwformat(ts::BufferWriter &w, BWFSpec const &spec, RemapFilter const &rule)
{
  return rule.describe(w, spec);
}

} // namespace ts
