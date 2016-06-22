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
 *       paritions until the fan out exceeds the arrays fixed size at which
 *       time, the fixed array is converted to a hash table
 *
 *   ip table - supports ip ranges.  A single ip address is treated as
 *       a range with the same beginning and end address.  The table is
 *       is devided up into a fixed number of  levels, indexed 8 bit
 *       boundaries, starting at the the high bit of the address.  Subsequent
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

#ifndef _CONTROL_MATCHER_H_
#define _CONTROL_MATCHER_H_

#include "ts/DynArray.h"
#include "ts/ink_hash_table.h"
#include "ts/IpMap.h"
#include "ts/MatcherUtils.h"

#include "ts/ink_apidefs.h"
#include "ts/ink_defs.h"
#include "HTTP.h"
#include "ts/Regex.h"

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#define SignalError(_buf, _already)                         \
  {                                                         \
    if (_already == false)                                  \
      pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, _buf); \
    _already = true;                                        \
    Warning("%s", _buf);                                    \
  }

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
  virtual char *get_string()       = 0;
  virtual const char *get_host()   = 0;
  virtual sockaddr const *get_ip() = 0;

  virtual sockaddr const *get_client_ip() = 0;
};

class HttpRequestData : public RequestData
{
public:
  inkcoreapi char *get_string();
  inkcoreapi const char *get_host();
  inkcoreapi sockaddr const *get_ip();
  inkcoreapi sockaddr const *get_client_ip();

  HttpRequestData() : hdr(NULL), hostname_str(NULL), api_info(NULL), xact_start(0), incoming_port(0), tag(NULL), internal_txn(false)
  {
    ink_zero(src_ip);
    ink_zero(dest_ip);
  }

  HTTPHdr *hdr;
  char *hostname_str;
  HttpApiInfo *api_info;
  time_t xact_start;
  IpEndpoint src_ip;
  IpEndpoint dest_ip;
  uint16_t incoming_port;
  char *tag;
  bool internal_txn;
};

template <class Data, class Result> class UrlMatcher
{
public:
  UrlMatcher(const char *name, const char *filename);
  ~UrlMatcher();
  void Match(RequestData *rdata, Result *result);
  void AllocateSpace(int num_entries);
  config_parse_error NewEntry(matcher_line *line_info);
  void Print();

  int
  getNumElements()
  {
    return num_el;
  }
  Data *
  getDataArray()
  {
    return data_array;
  }

protected:
  InkHashTable *url_ht;
  char **url_str;           // array of url strings
  int *url_value;           // array of posion of url strings
  Data *data_array;         // data array.  Corresponds to re_array
  int array_len;            // length of the arrays (all three are the same length)
  int num_el;               // number of elements in the table
  const char *matcher_name; // Used for Debug/Warning/Error messages
  const char *file_name;    // Used for Debug/Warning/Error messages
};

template <class Data, class Result> class RegexMatcher
{
public:
  RegexMatcher(const char *name, const char *filename);
  ~RegexMatcher();
  void Match(RequestData *rdata, Result *result);
  void AllocateSpace(int num_entries);
  config_parse_error NewEntry(matcher_line *line_info);
  void Print();

  int
  getNumElements()
  {
    return num_el;
  }
  Data *
  getDataArray()
  {
    return data_array;
  }

protected:
  pcre **re_array;          // array of compiled regexs
  char **re_str;            // array of uncompiled regex strings
  Data *data_array;         // data array.  Corresponds to re_array
  int array_len;            // length of the arrays (all three are the same length)
  int num_el;               // number of elements in the table
  const char *matcher_name; // Used for Debug/Warning/Error messages
  const char *file_name;    // Used for Debug/Warning/Error messages
};

template <class Data, class Result> class HostRegexMatcher : public RegexMatcher<Data, Result>
{
public:
  HostRegexMatcher(const char *name, const char *filename);
  void Match(RequestData *rdata, Result *result);
};

template <class Data, class Result> class HostMatcher
{
public:
  HostMatcher(const char *name, const char *filename);
  ~HostMatcher();
  void Match(RequestData *rdata, Result *result);
  void AllocateSpace(int num_entries);
  config_parse_error NewEntry(matcher_line *line_info);
  void Print();

  int
  getNumElements()
  {
    return num_el;
  }
  Data *
  getDataArray()
  {
    return data_array;
  }
  HostLookup *
  getHLookup()
  {
    return host_lookup;
  }

private:
  static void PrintFunc(void *opaque_data);
  HostLookup *host_lookup;  // Data structure to do the lookups
  Data *data_array;         // array of all data items
  int array_len;            // the length of the arrays
  int num_el;               // the numbe of itmems in the tree
  const char *matcher_name; // Used for Debug/Warning/Error messages
  const char *file_name;    // Used for Debug/Warning/Error messages
};

template <class Data, class Result> class IpMatcher
{
public:
  IpMatcher(const char *name, const char *filename);
  ~IpMatcher();
  void Match(sockaddr const *ip_addr, RequestData *rdata, Result *result);
  void AllocateSpace(int num_entries);
  config_parse_error NewEntry(matcher_line *line_info);
  void Print();

  int
  getNumElements()
  {
    return num_el;
  }
  Data *
  getDataArray()
  {
    return data_array;
  }

  static void PrintFunc(void *opaque_data);
  IpMap ip_map;             // Data structure to do lookups
  Data *data_array;         // array of the data lements with in the table
  int array_len;            // size of the arrays
  int num_el;               // number of elements in the table
  const char *matcher_name; // Used for Debug/Warning/Error messages
  const char *file_name;    // Used for Debug/Warning/Error messages
};

#define ALLOW_HOST_TABLE 1 << 0
#define ALLOW_IP_TABLE 1 << 1
#define ALLOW_REGEX_TABLE 1 << 2
#define ALLOW_HOST_REGEX_TABLE 1 << 3
#define ALLOW_URL_TABLE 1 << 4
#define DONT_BUILD_TABLE 1 << 5 // for testing

template <class Data, class Result> class ControlMatcher
{
public:
  // Parameter name must not be deallocated before this
  //  object is
  ControlMatcher(const char *file_var, const char *name, const matcher_tags *tags,
                 int flags_in = (ALLOW_HOST_TABLE | ALLOW_IP_TABLE | ALLOW_REGEX_TABLE | ALLOW_HOST_REGEX_TABLE | ALLOW_URL_TABLE));
  ~ControlMatcher();
  int BuildTable();
  int BuildTableFromString(char *str);
  void Match(RequestData *rdata, Result *result);
  void Print();

  int
  getEntryCount()
  {
    return m_numEntries;
  }
  HostMatcher<Data, Result> *
  getHostMatcher()
  {
    return hostMatch;
  }
  RegexMatcher<Data, Result> *
  getReMatcher()
  {
    return reMatch;
  }
  UrlMatcher<Data, Result> *
  getUrlMatcher()
  {
    return urlMatch;
  }
  IpMatcher<Data, Result> *
  getIPMatcher()
  {
    return ipMatch;
  }
  HostRegexMatcher<Data, Result> *
  getHrMatcher()
  {
    return hrMatch;
  }

  // private:
  RegexMatcher<Data, Result> *reMatch;
  UrlMatcher<Data, Result> *urlMatch;
  HostMatcher<Data, Result> *hostMatch;
  IpMatcher<Data, Result> *ipMatch;
  HostRegexMatcher<Data, Result> *hrMatch;
  const matcher_tags *config_tags;
  char config_file_path[PATH_NAME_MAX];
  int flags;
  int m_numEntries;
  const char *matcher_name; // Used for Debug/Warning/Error messages
};

#endif /* _CONTROL_MATCHER_H_ */
