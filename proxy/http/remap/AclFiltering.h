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

#include "swoc/Errata.h"

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
    SRC_ADDR,     ///< Check user agent IP address.
    PROXY_ADDR,   ///< Check local inbound IP address (proxy address).
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
                                                         {"src-ip", SRC_ADDR, true},
                                                         {"proxy-ip", PROXY_ADDR, true},
                                                         {"in-ip", PROXY_ADDR, true},
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
   * @param k Key.
   * @param v Value.
   */
  RemapArg(Type t, ts::TextView k, ts::TextView v) : type(t), key(k), value(v) {}

  Type type = INVALID; ///< Type of argument.
  ts::TextView key;    ///< Name of the argument.
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

  /// Filter action.
  enum Action : uint8_t {
    ENABLE, ///< The rule is enabled.
    DISABLE ///< The rule is disabled.
  };

  /// Boolean checking - don't care, false, true values.
  enum TriState {
    DO_NOT_CHECK,  ///< Do not check.
    REQUIRE_FALSE, ///< Required to be @c false.
    REQUIRE_TRUE   ///< Required to be @c true.
  };

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

  /** Add @a method to the set of matched methods.
   *
   * @param method Name of method.
   * @return @a this
   */
  self_type &add_method(ts::TextView const &method);

  /** Set whether to invert the method matching.
   *
   * @param flag New value for inverting.
   * @return @a this
   *
   * If the method match is inverted, then this filters matches any method @b not in the match set.
   */
  self_type &set_method_match_inverted(bool flag);

  self_type &mark_src_addr(IpAddr &min, IpAddr &max);
  self_type &mark_src_addr_inverted(IpAddr &min, IpAddr &max);
  self_type &mark_proxy_addr(IpAddr &min, IpAddr &max);
  self_type &mark_proxy_addr_inverted(IpAddr &min, IpAddr &max);

  self_type &set_action(Action action);

  swoc::Errata set_action(ts::TextView value);

  bool
  is_enabled() const
  {
    return _action == ENABLE;
  }

  self_type &set_internal_check(TriState state);
  swoc::Errata set_internal_check(ts::TextView value);

  bool check_for_internal(bool internal_p);

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

  /// Check if the transaction is internal.
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
  IpMap src_addr;
  /// Map of local inbound addresses of interest.
  IpMap proxy_addr;

protected:
  void mark(IpMap &map, IpAddr const &min, IpAddr const &max);

  /** Mark a @a map with the addresses not in [ @a min, @a max ].
   *
   * @param map Map to fill.
   * @param min Min address of excluded range.
   * @param max Max address of exclude range.
   * @param mark Value to use in the fill.
   */
  void mark_inverted(IpMap &map, IpAddr const &min, IpAddr const &max);

  /// Action if this filter is matched.
  Action _action = ENABLE;
  /// Check if internal request.
  TriState _internal_check = DO_NOT_CHECK;
};

using RemapFilterList = RemapFilter::List;

inline RemapFilter &
RemapFilter::set_name(ts::TextView const &text)
{
  name = text;
  return *this;
}

inline RemapFilter &
RemapFilter::mark_src_addr(IpAddr &min, IpAddr &max)
{
  this->mark(src_addr, min, max);
  return *this;
}
inline RemapFilter &
RemapFilter::mark_src_addr_inverted(IpAddr &min, IpAddr &max)
{
  this->mark_inverted(src_addr, min, max);
  return *this;
}
inline RemapFilter &
RemapFilter::mark_proxy_addr(IpAddr &min, IpAddr &max)
{
  this->mark(proxy_addr, min, max);
  return *this;
}
inline RemapFilter &
RemapFilter::mark_proxy_addr_inverted(IpAddr &min, IpAddr &max)
{
  this->mark_inverted(proxy_addr, min, max);
  return *this;
}

inline RemapFilter::self_type &
RemapFilter::set_internal_check(RemapFilter::TriState state)
{
  _internal_check = state;
  return *this;
}

inline bool
RemapFilter::check_for_internal(bool internal_p)
{
  return !((_internal_check == REQUIRE_TRUE && !internal_p) || (_internal_check == REQUIRE_FALSE && internal_p));
}

namespace ts
{
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, RemapArg::Type type);

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, RemapFilter::TriState state);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, RemapFilter::Action action);

inline BufferWriter &
bwformat(ts::BufferWriter &w, BWFSpec const &spec, RemapFilter const &rule)
{
  return rule.describe(w, spec);
}

} // namespace ts
