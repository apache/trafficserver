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

/*****************************************************************************
 *
 *  ControlMatcher.h - Interface to general purpose matcher
 *
 *
 *
 *
 *  Description:
 *
 *     The control matcher module provides the ability to lookup arbitrary
 *  information specific to a URL and IP address.  The outside
 *  world only sees the ControlMatcher class which parses the relevant
 *  configuration file and builds the lookup table
 *
 *     Four types of matched are supported: hostname, domain name, ip address
 *  and URL regex.  For these four types, three lookup tables are used.  Regex and
 *  ip lookups have there own tables and host and domain lookups share a single
 *  table
 *
 *  Module Purpose & Specifications
 *  -------------------------------
 *   -  to provide a generic mechanism for matching configuration data
 *       against hostname, domain name, ip address and URL regex
 *   -  the generic mechanism should require minimum effort to apply it
 *       to new features that require per request matching
 *   -  for the mechanism to be efficient such that lookups against
 *       the tables are not a performance problem when they are both done
 *       for every request through the proxy and set of matching
 *       is very large
 *
 *  Lookup Table Descriptions
 *  -------------------------
 *
 *   regex table - implemented as a linear list of regular expressions to
 *       match against
 *
 *   host/domain table - The host domain table is logically implemented as
 *       tree, broken up at each partition in a hostname.  Three mechanism
 *       are used to move from one level to the next: a hash table, a fixed
 *       sized array and a constant time index (class charIndex).  The constant
 *       time index is only used to from the root domain to the first
 *       level partition (ie: .com). The fixed array is used for subsequent
 *       partitions until the fan out exceeds the arrays fixed size at which
 *       time, the fixed array is converted to a hash table
 *
 *   ip table - supports ip ranges.  A single ip address is treated as
 *       a range with the same beginning and end address.  The table is
 *       is divided up into a fixed number of  levels, indexed 8 bit
 *       boundaries, starting at the high bit of the address.  Subsequent
 *       levels are allocated only when needed.
 *
 ****************************************************************************/

//
// IMPORTANT: Instantiating these templates
//
//    The Implementation for these templates appears in
//     ControlMatcher.cc   To get the templates instantiated
//     correctly on all compilers new uses MUST explicitly
//     instantiate the new instance at the bottom of
//     ControlMatcher.cc
//

#pragma once

#include <unordered_map>

#include "tscore/Result.h"
#include "tscore/MatcherUtils.h"

#include "tscore/ink_apidefs.h"
#include "tscore/ink_defs.h"
#include "proxy/hdrs/HTTP.h"
#include "tsutil/Regex.h"
#include "proxy/hdrs/URL.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

#include <swoc/swoc_ip.h>

#ifdef HAVE_CTYPE_H
#include <cctype>
#endif

class HostLookup;
struct HttpApiInfo;
struct matcher_line;
struct matcher_tags;

struct RequestData {
public:
  // First three are the lookup keys to the tables
  //  get_ip() can be either client_ip or server_ip
  //  depending on how the module user wants to key
  //  the table
  virtual ~RequestData() {}
  virtual char           *get_string() = 0;
  virtual const char     *get_host()   = 0;
  virtual sockaddr const *get_ip()     = 0;

  virtual sockaddr const *get_client_ip() = 0;
};

class HttpRequestData : public RequestData
{
public:
  char           *get_string() override;
  const char     *get_host() override;
  sockaddr const *get_ip() override;
  sockaddr const *get_client_ip() override;

  HttpRequestData()

  {
    ink_zero(src_ip);
    ink_zero(dest_ip);
  }

  HTTPHdr     *hdr          = nullptr;
  char const  *hostname_str = nullptr;
  HttpApiInfo *api_info     = nullptr;
  time_t       xact_start   = 0;
  IpEndpoint   src_ip;
  IpEndpoint   dest_ip;
  uint16_t     incoming_port                   = 0;
  char        *tag                             = nullptr;
  bool         internal_txn                    = false;
  URL        **cache_info_lookup_url           = nullptr;
  URL        **cache_info_parent_selection_url = nullptr;
};

// Mixin class for shared info across all templates. This just wraps the
// shared members such that we don't have to duplicate all these initializers
// etc. If someone wants to rewrite all this code to use setters and getters,
// by all means, please do so. The plumbing is in place :).
template <class Data> class BaseMatcher
{
public:
  BaseMatcher(const char *name, const char *filename) : matcher_name(name), file_name(filename) {}

  ~BaseMatcher() { delete[] data_array; }

protected:
  int         num_el       = -1;        // number of elements in the table
  const char *matcher_name = "unknown"; // Used for Debug/Warning/Error messages
  const char *file_name    = nullptr;   // Used for Debug/Warning/Error messages
  Data       *data_array   = nullptr;   // Array with the Data elements
  int         array_len    = -1;        // length of the arrays (all three are the same length)
};

template <class Data, class MatchResult> class UrlMatcher : protected BaseMatcher<Data>
{
  using super = BaseMatcher<Data>;

public:
  UrlMatcher(const char *name, const char *filename);
  ~UrlMatcher();

  void   AllocateSpace(int num_entries);
  Result NewEntry(matcher_line *line_info);

  void Match(RequestData *rdata, MatchResult *result) const;
  void Print() const;

  using super::array_len;
  using super::data_array;
  using super::file_name;
  using super::matcher_name;
  using super::num_el;

private:
  std::unordered_map<std::string, int> url_ht;
  char                               **url_str   = nullptr; // array of url strings
  int                                 *url_value = nullptr; // array of posion of url strings
};

template <class Data, class MatchResult> class RegexMatcher : protected BaseMatcher<Data>
{
  using super = BaseMatcher<Data>;

public:
  RegexMatcher(const char *name, const char *filename);

  void   AllocateSpace(int num_entries);
  Result NewEntry(matcher_line *line_info);

  void Match(RequestData *rdata, MatchResult *result) const;
  void Print() const;

  using super::array_len;
  using super::data_array;
  using super::file_name;
  using super::matcher_name;
  using super::num_el;

protected:
  std::vector<Regex>       regex_array;   // array of regexs
  std::vector<std::string> regex_strings; // array of regex strings
};

template <class Data, class MatchResult> class HostRegexMatcher : public RegexMatcher<Data, MatchResult>
{
  using super = BaseMatcher<Data>;

public:
  HostRegexMatcher(const char *name, const char *filename);
  void Match(RequestData *rdata, MatchResult *result) const;

  using super::array_len;
  using super::data_array;
  using super::file_name;
  using super::matcher_name;
  using super::num_el;
};

template <class Data, class MatchResult> class HostMatcher : protected BaseMatcher<Data>
{
  using super = BaseMatcher<Data>;

public:
  HostMatcher(const char *name, const char *filename);
  ~HostMatcher();

  void   AllocateSpace(int num_entries);
  Result NewEntry(matcher_line *line_info);

  void Match(RequestData *rdata, MatchResult *result) const;
  void Print() const;

  using super::array_len;
  using super::data_array;
  using super::file_name;
  using super::matcher_name;
  using super::num_el;

  HostLookup *
  getHLookup()
  {
    return host_lookup.get();
  }

private:
  static void                 PrintFunc(void *opaque_data);
  std::unique_ptr<HostLookup> host_lookup = nullptr; // Data structure to do the lookups
};

template <class Data, class MatchResult> class IpMatcher : protected BaseMatcher<Data>
{
  using super = BaseMatcher<Data>;

public:
  IpMatcher(const char *name, const char *filename);

  void   AllocateSpace(int num_entries);
  Result NewEntry(matcher_line *line_info);

  void Match(sockaddr const *ip_addr, RequestData *rdata, MatchResult *result) const;
  void Print() const;

  using super::array_len;
  using super::data_array;
  using super::file_name;
  using super::matcher_name;
  using super::num_el;

private:
  static void PrintFunc(void *opaque_data);
  using AddrMap = swoc::IPSpace<Data *>;
  AddrMap ip_addrs; // Data structure to do lookups
};

#define ALLOW_HOST_TABLE       1 << 0
#define ALLOW_IP_TABLE         1 << 1
#define ALLOW_REGEX_TABLE      1 << 2
#define ALLOW_HOST_REGEX_TABLE 1 << 3
#define ALLOW_URL_TABLE        1 << 4
#define DONT_BUILD_TABLE       1 << 5 // for testing

template <class Data, class MatchResult> class ControlMatcher
{
public:
  // Parameter name must not be deallocated before this object is
  ControlMatcher(const char *file_var, const char *name, const matcher_tags *tags,
                 int flags_in = (ALLOW_HOST_TABLE | ALLOW_IP_TABLE | ALLOW_REGEX_TABLE | ALLOW_HOST_REGEX_TABLE | ALLOW_URL_TABLE));
  ~ControlMatcher();

  int BuildTable();
  int BuildTableFromString(char *str);

  void Match(RequestData *rdata, MatchResult *result) const;
  void Print() const;

  int
  getEntryCount() const
  {
    return m_numEntries;
  }

  HostMatcher<Data, MatchResult> *
  getHostMatcher()
  {
    return hostMatch.get();
  }

  RegexMatcher<Data, MatchResult> *
  getReMatcher()
  {
    return reMatch.get();
  }

  IpMatcher<Data, MatchResult> *
  getIPMatcher()
  {
    return ipMatch.get();
  }

  // private
  std::unique_ptr<RegexMatcher<Data, MatchResult>>     reMatch;
  std::unique_ptr<UrlMatcher<Data, MatchResult>>       urlMatch;
  std::unique_ptr<HostMatcher<Data, MatchResult>>      hostMatch;
  std::unique_ptr<IpMatcher<Data, MatchResult>>        ipMatch;
  std::unique_ptr<HostRegexMatcher<Data, MatchResult>> hrMatch;

  const matcher_tags *config_tags = nullptr;
  char                config_file_path[PATH_NAME_MAX];
  int                 flags        = 0;
  int                 m_numEntries = 0;
  const char         *matcher_name = "unknown"; // Used for Debug/Warning/Error messages
};
