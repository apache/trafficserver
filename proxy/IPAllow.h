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
 *  IPAllow.h - Interface to IP Access Control systtem
 *
 *
 ****************************************************************************/

#pragma once

#include <string>
#include <string_view>
#include <set>
#include <vector>
#include <atomic>

#include "hdrs/HTTP.h"
#include "ProxyConfig.h"
#include "tscore/IpMap.h"
#include "tscpp/util/TextView.h"

// forward declare in name only so it can be a friend.
struct IpAllowUpdate;

/** Singleton class for access controls.
 */
class IpAllow : public ConfigInfo
{
  friend struct IpAllowUpdate;

  // These point in to the bulk loaded configuration file, which therefore needs to be kept around
  // until the configuration is destructed. The number is expected to be small enough a vector is the
  // best container.
  using MethodNames = std::vector<std::string_view>;

  static constexpr uint32_t ALL_METHOD_MASK = ~0; // Mask for all methods.

  /** An access control record.
      It has the methods permitted and the source line. This is a POD used by @a ACL.
  */
  struct Record {
    /// Default constructor.
    /// Present only to make Vec<> happy, do not use.
    Record() = default;
    explicit Record(uint32_t method_mask);
    Record(uint32_t method_mask, int line, MethodNames &&nonstandard_methods, bool deny_nonstandard_methods);

    uint32_t _method_mask{0};
    int _src_line{0};
    MethodNames _nonstandard_methods;
    bool _deny_nonstandard_methods{false};
  };

public:
  using self_type     = IpAllow; ///< Self reference type.
  using scoped_config = ConfigProcessor::scoped_config<self_type, self_type>;

  // indicator for whether we should be checking the acl record for src ip or dest ip
  enum match_key_t { SRC_ADDR, DST_ADDR };
  /// Token strings for configuration
  static constexpr ts::TextView OPT_MATCH_SRC{"src_ip"};
  static constexpr ts::TextView OPT_MATCH_DST{"dest_ip"};
  static constexpr ts::TextView OPT_ACTION_TAG{"action"};
  static constexpr ts::TextView OPT_ACTION_ALLOW{"ip_allow"};
  static constexpr ts::TextView OPT_ACTION_DENY{"ip_deny"};
  static constexpr ts::TextView OPT_METHOD{"method"};
  static constexpr ts::TextView OPT_METHOD_ALL{"all"};

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
    ACL(const self_type &) = delete; // no copies.
    ACL(self_type &&that) noexcept;  // move allowed.
    ~ACL();

    self_type &operator=(const self_type &) = delete;
    self_type &operator                     =(self_type &&that) noexcept;

    void clear(); ///< Drop data and config reference.

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

    const Record *_r{nullptr}; ///< The actual ACL record.
    IpAllow *_config{nullptr}; ///< The backing configuration.
  };

  IpAllow(const char *config_var);

  void Print();

  static ACL match(sockaddr const *ip, match_key_t key);
  static ACL match(IpEndpoint const *ip, match_key_t key);

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

private:
  static size_t configid;               ///< Configuration ID for update management.
  static const Record ALLOW_ALL_RECORD; ///< Static record that allows all access.
  static bool accept_check_p;           ///< @c true if deny all can be enforced during accept.

  static constexpr const char *MODULE_NAME = "IPAllow";

  void PrintMap(IpMap *map);
  int BuildTable();

  ats_scoped_str config_file_path; ///< Path to configuration file.
  /// The file contents so records can point in to this instead of separately allocating.
  ats_scoped_str file_buff;
  //  const char *module_name{nullptr};
  //  const char *action{nullptr};
  IpMap _src_map;
  IpMap _dst_map;
  std::vector<Record> _src_acls;
  std::vector<Record> _dst_acls;
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
IpAllow::match(IpEndpoint const *ip, match_key_t key) -> ACL
{
  return self_type::match(&ip->sa, key);
}

inline auto
IpAllow::makeAllowAllACL() -> ACL
{
  return {&ALLOW_ALL_RECORD, nullptr};
}
