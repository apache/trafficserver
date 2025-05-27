/** @file

  Access control by IP address and HTTP method.

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

/*****************************************************************************
 *
 *  IPAllow.h - Interface to IP Access Control system
 *
 *
 ****************************************************************************/

#pragma once

#include <string>
#include <string_view>

#include "proxy/hdrs/HTTP.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "swoc/TextView.h"
#include "swoc/swoc_file.h"
#include "swoc/swoc_ip.h"
#include "swoc/Errata.h"

// forward declare in name only so it can be a friend.
struct IpAllowUpdate;
namespace YAML
{
class Node;
}

/** Singleton class for access controls.
 */
class IpAllow : public ConfigInfo
{
  using MethodNames = swoc::MemSpan<swoc::TextView>;

  static constexpr uint32_t ALL_METHOD_MASK = ~0; // Mask for all methods.

  /** An access control record.
      It has the methods permitted and the source line. This is a POD used by @a ACL.
  */
  struct Record {
    /// Default constructor.
    /// Present only to make Vec<> happy, do not use.
    Record()              = default;
    Record(Record &&that) = default;
    /** Construct from mask.
     *
     * @param method_mask Bit mask of allowed methods.
     */
    explicit Record(uint32_t method_mask);

    /** Construct from values.
     *
     * @param method_mask Well known method mask.
     * @param line Source line in configuration file.
     * @param nonstandard_methods Allowed methods that are not well known.
     * @param deny_nonstandard_methods Denied methods that are not well known.
     */
    Record(uint32_t method_mask, int line, MethodNames &&nonstandard_methods, bool deny_nonstandard_methods);

    uint32_t    _method_mask{0};                  ///< Well known method mask.
    int         _src_line{0};                     ///< Configuration file source line.
    MethodNames _nonstandard_methods;             ///< Allowed methods that are not well known.
    bool        _deny_nonstandard_methods{false}; ///< Denied methods that are not well known.
  };

public:
  using self_type     = IpAllow; ///< Self reference type.
  using scoped_config = ConfigProcessor::scoped_config<self_type, self_type>;
  using IpMap         = swoc::IPSpace<Record const *>;
  using IpCategories  = std::unordered_map<std::string, swoc::IPSpace<bool>>;

  // indicator for whether we should be checking the acl record for src ip or dest ip
  enum class match_key_t { SRC_ADDR, DST_ADDR };

  /// Token strings for configuration
  static constexpr swoc::TextView OPT_MATCH_SRC{"src_ip"};
  static constexpr swoc::TextView OPT_MATCH_DST{"dest_ip"};
  static constexpr swoc::TextView OPT_ACTION_TAG{"action"};
  static constexpr swoc::TextView OPT_ACTION_ALLOW{"ip_allow"};
  static constexpr swoc::TextView OPT_ACTION_DENY{"ip_deny"};
  static constexpr swoc::TextView OPT_METHOD{"method"};
  static constexpr swoc::TextView OPT_METHOD_ALL{"all"};

  /*
   * A YAML configuration file looks something like this:
   *
   * ip_categories:
   *   - name: ACME_INTERNAL
   *     ip_addrs:
   *       - 10.0.0.0/8
   *       - 172.16.0.0/20
   *       - 192.168.1.0/24
   *
   * ip_allow:
   *   - apply: in
   *     ip_categories: ACME_INTERNAL
   *     action: allow
   *     methods:
   *     - GET
   *     - HEAD
   *     - POST
   *   - apply: in
   *     ip_addrs: 127.0.0.1
   *     action: allow
   *     methods: ALL
   *
   */
  static const inline std::string YAML_TAG_ROOT{"ip_allow"};

  static const inline std::string YAML_TAG_CATEGORY_ROOT{"ip_categories"};
  static const inline std::string YAML_TAG_CATEGORY_NAME{"name"};
  static const inline std::string YAML_TAG_CATEGORY_IP_ADDRS{"ip_addrs"};

  static const inline std::string YAML_TAG_IP_ADDRS{"ip_addrs"};
  static const inline std::string YAML_TAG_IP_CATEGORIES{"ip_categories"};
  static const inline std::string YAML_TAG_APPLY{"apply"};
  static const inline std::string YAML_VALUE_APPLY_IN{"in"};
  static const inline std::string YAML_VALUE_APPLY_OUT{"out"};
  static const inline std::string YAML_TAG_ACTION{"action"};
  static const inline std::string YAML_VALUE_ACTION_ALLOW{"set_allow"};
  static const inline std::string YAML_VALUE_ACTION_ALLOW_OLD_NAME{"allow"};
  static const inline std::string YAML_VALUE_ACTION_DENY{"set_deny"};
  static const inline std::string YAML_VALUE_ACTION_DENY_OLD_NAME{"deny"};
  static const inline std::string YAML_TAG_METHODS{"methods"};
  static const inline std::string YAML_VALUE_METHODS_ALL{"all"};

  static constexpr const char *MODULE_NAME = "IPAllow";

  /** An access control record and support data.
   * The primary point of this is to hold the backing configuration in memory while the ACL
   * is in use.
   */
  class ACL
  {
    friend class IpAllow;
    using self_type = ACL; ///< Self reference type.
  public:
    ACL()                  = default;
    ACL(const self_type &) = delete;         // no copies.
    explicit ACL(self_type &&that) noexcept; // move allowed.
    ~ACL();

    self_type &operator=(const self_type &) = delete;
    self_type &operator=(self_type &&that) noexcept;

    void clear(); ///< Drop data and config reference.

    /** Convert well known string index to mask.
     *
     * @param wksidx Well known string index.
     * @return A mask for that method.
     */
    static uint32_t MethodIdxToMask(int wksidx);

    /// Check if the ACL is valid (i.e. not uninitialized or missing).
    bool isValid() const;
    /// Check if the ACL denies all access.
    bool isDenyAll() const;
    /// Check if the ACL allows all access.
    bool isAllowAll() const;

    bool isMethodAllowed(int method_wksidx) const;

    bool isNonstandardMethodAllowed(std::string_view method) const;

    /// Return the configuration source line for this ACL.
    int source_line() const;

  private:
    // @a config must already be ref counted.
    ACL(const Record *r, IpAllow *config) noexcept;

    const Record *_r{nullptr};      ///< The actual ACL record.
    IpAllow      *_config{nullptr}; ///< The backing configuration.
  };

  explicit IpAllow(const char *ip_allow_config_var, const char *categories_config_var);

  void Print() const;

  static ACL match(swoc::IPAddr const &addr, match_key_t key);
  static ACL match(swoc::IPEndpoint const *addr, match_key_t key);
  static ACL match(sockaddr const *sa, match_key_t key);

  static void startup();
  static void reconfigure();
  /// @return The global instance.
  static IpAllow *acquire();
  /// Release the configuration.
  /// @a config is released and can then be garbage collected.
  static void release(IpAllow *config);
  /// Release this configuration.
  /// @a this is released and can then be garbage collected.
  void release();

  /// A static ACL that permits all methods.
  static ACL makeAllowAllACL();
  /// A static ACL that denies everything.
  static const ACL DENY_ALL_ACL;

  /* @return The previous accept check state
   * This is a global variable that is independent of
   * the ip_allow configuration
   */
  static bool enableAcceptCheck(bool state);

  /* @return The current accept check state
   * This is a global variable that is independent of
   * the ip_allow configuration
   */
  static bool isAcceptCheckEnabled();

  const swoc::file::path &get_config_file() const;

  /**
   * Check if an IP category contains a specific IP address.
   *
   * @param category The IP category to check.
   * @param addr The IP address to check against the category.
   * @return True if the category contains the address, false otherwise.
   */
  static bool ip_category_contains_addr(std::string const &category, swoc::IPAddr const &addr);

  /** Indicate whether ip_allow.yaml has no rules associated with it.
   *
   * If there are no rules, then all traffic will be blocked. This is used
   * during ATS configuration to verify that the user has provided a usable
   * ip_allow.yaml file.
   *
   * @return True if there are no rules in ip_allow.yaml, false otherwise.
   */
  static bool has_no_rules();

private:
  static size_t       configid;         ///< Configuration ID for update management.
  static const Record ALLOW_ALL_RECORD; ///< Static record that allows all access.
  static bool         accept_check_p;   ///< @c true if deny all can be enforced during accept.

  void DebugMap(IpMap const &map) const;

  swoc::Errata BuildTable();
  swoc::Errata YAMLBuildTable(const std::string &content);
  swoc::Errata YAMLLoadEntry(const YAML::Node &);
  swoc::Errata YAMLLoadIPAddrRange(const YAML::Node &, IpMap *map, Record const *mark);
  swoc::Errata YAMLLoadIPCategory(const YAML::Node &, IpMap *map, Record const *mark);
  swoc::Errata YAMLLoadMethod(const YAML::Node &node, Record &rec);

  swoc::Errata BuildCategories();
  swoc::Errata YAMLBuildCategories(const std::string &content);
  swoc::Errata YAMLLoadCategoryRoot(const YAML::Node &);
  swoc::Errata YAMLLoadCategoryDefinition(const YAML::Node &);
  swoc::Errata YAMLLoadCategoryIpRange(const YAML::Node &, swoc::IPSpace<bool> &space);

  /// Copy @a src to the local arena and return a view of the copy.
  swoc::TextView localize(swoc::TextView src);

  swoc::file::path ip_allow_config_file;      ///< Path to ip_allow configuration file.
  swoc::file::path ip_categories_config_file; ///< Path to ip_allow configuration file.
  IpMap            _src_map;
  IpMap            _dst_map;
  IpCategories     ip_category_map; ///< Map of IP categories to IP spaces.
  /// Storage for records.
  swoc::MemArena _arena;

  /// Whether to allow "allow" and "deny" as action tags.
  bool _is_legacy_action_policy{true};

  friend swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, IpAllow::IpMap const &map);
};

// ------ Record methods --------

inline IpAllow::Record::Record(uint32_t method_mask) : _method_mask(method_mask) {}

inline IpAllow::Record::Record(uint32_t method_mask, int ln, MethodNames &&nonstandard_methods, bool deny_nonstandard_methods)
  : _method_mask(method_mask),
    _src_line(ln),
    _nonstandard_methods(nonstandard_methods),
    _deny_nonstandard_methods(deny_nonstandard_methods)
{
}

// ------ ACL methods --------

inline IpAllow::ACL::ACL(const IpAllow::Record *r, IpAllow *config) noexcept : _r(r), _config(config) {}

inline IpAllow::ACL::ACL(self_type &&that) noexcept : _r(that._r), _config(that._config)
{
  that._r      = nullptr;
  that._config = nullptr;
}

inline IpAllow::ACL::~ACL()
{
  if (_config != nullptr) {
    _config->release();
  }
}

inline auto
IpAllow::ACL::operator=(self_type &&that) noexcept -> self_type &
{
  // move and clear so @a that doesn't drop the config reference.
  this->_r      = that._r;
  that._r       = nullptr;
  this->_config = that._config;
  that._config  = nullptr;

  return *this;
}

inline uint32_t
IpAllow::ACL::MethodIdxToMask(int wksidx)
{
  return 1U << (wksidx - HTTP_WKSIDX_CONNECT);
}

inline bool
IpAllow::ACL::isValid() const
{
  return _r != nullptr;
}

inline bool
IpAllow::ACL::isDenyAll() const
{
  return _r == nullptr || (_r->_method_mask == 0 && _r->_nonstandard_methods.empty());
}

inline bool
IpAllow::ACL::isAllowAll() const
{
  return _r && _r->_method_mask == ALL_METHOD_MASK;
}

inline bool
IpAllow::ACL::isMethodAllowed(int method_wksidx) const
{
  return _r && 0 != (_r->_method_mask & MethodIdxToMask(method_wksidx));
}

inline bool
IpAllow::ACL::isNonstandardMethodAllowed(std::string_view method) const
{
  if (_r == nullptr) {
    return false;
  } else if (_r->_method_mask == ALL_METHOD_MASK) {
    return true;
  }
  bool method_in_set =
    std::find_if(_r->_nonstandard_methods.begin(), _r->_nonstandard_methods.end(),
                 [method](std::string_view const &s) { return 0 == strcasecmp(s, method); }) != _r->_nonstandard_methods.end();
  return _r->_deny_nonstandard_methods ? !method_in_set : method_in_set;
}

inline void
IpAllow::ACL::clear()
{
  if (_config) {
    _config->release();
    _config = nullptr;
  }
  _r = nullptr;
}

inline int
IpAllow::ACL::source_line() const
{
  return _r ? _r->_src_line : 0;
}

// ------ IpAllow methods --------

inline bool
IpAllow::enableAcceptCheck(bool state)
{
  bool temp      = accept_check_p;
  accept_check_p = state;
  return temp;
}

inline bool
IpAllow::isAcceptCheckEnabled()
{
  return accept_check_p;
}

inline auto
IpAllow::match(swoc::IPEndpoint const *addr, match_key_t key) -> ACL
{
  return self_type::match(swoc::IPAddr(&addr->sa), key);
}

inline auto
IpAllow::match(sockaddr const *sa, match_key_t key) -> ACL
{
  return self_type::match(swoc::IPAddr(sa), key);
}

inline auto
IpAllow::makeAllowAllACL() -> ACL
{
  return {&ALLOW_ALL_RECORD, nullptr};
}

inline const swoc::file::path &
IpAllow::get_config_file() const
{
  return ip_allow_config_file;
}

inline bool
IpAllow::has_no_rules()
{
  auto const *self = IpAllow::acquire();
  return self->_src_map.count() == 0 && self->_dst_map.count() == 0;
}
