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
 *  ParentSelection.h - Interface to Parent Selection System
 *
 *
 ****************************************************************************/

#pragma once

#include "ProxyConfig.h"
#include "ControlBase.h"
#include "ControlMatcher.h"
#include "records/P_RecProcess.h"
#include "tscore/ConsistentHash.h"
#include "tscore/Tokenizer.h"
#include "tscore/ink_apidefs.h"
#include "HostStatus.h"

#include <algorithm>
#include <vector>

#define MAX_PARENTS 64

struct RequestData;
struct matcher_line;
struct ParentResult;
struct OverridableHttpConfigParams;
class ParentRecord;
class ParentSelectionStrategy;

enum ParentResultType {
  PARENT_UNDEFINED,
  PARENT_DIRECT,
  PARENT_SPECIFIED,
  PARENT_AGENT,
  PARENT_FAIL,
};

static const char *ParentResultStr[] = {"PARENT_UNDEFINED", "PARENT_DIRECT", "PARENT_SPECIFIED", "PARENT_AGENT", "PARENT_FAIL"};

enum ParentRR_t {
  P_NO_ROUND_ROBIN = 0,
  P_STRICT_ROUND_ROBIN,
  P_HASH_ROUND_ROBIN,
  P_CONSISTENT_HASH,
  P_LATCHED_ROUND_ROBIN,
  P_UNDEFINED
};

enum ParentRetry_t {
  PARENT_RETRY_NONE               = 0,
  PARENT_RETRY_SIMPLE             = 1,
  PARENT_RETRY_UNAVAILABLE_SERVER = 2,
  // both simple and unavailable server retry
  PARENT_RETRY_BOTH = 3
};

struct UnavailableServerResponseCodes {
  UnavailableServerResponseCodes(char *val);
  ~UnavailableServerResponseCodes(){};

  bool
  contains(int code)
  {
    return binary_search(codes.begin(), codes.end(), code);
  }

private:
  std::vector<int> codes;
};

struct SimpleRetryResponseCodes {
  SimpleRetryResponseCodes(char *val);
  ~SimpleRetryResponseCodes(){};

  bool
  contains(int code)
  {
    return binary_search(codes.begin(), codes.end(), code);
  }

private:
  std::vector<int> codes;
};
// struct pRecord
//
//    A record for an individual parent
//
struct pRecord : ATSConsistentHashNode {
  char hostname[MAXDNAME + 1];
  int port;
  std::atomic<time_t> failedAt = 0;
  std::atomic<int> failCount   = 0;
  int32_t upAt;
  const char *scheme; // for which parent matches (if any)
  int idx;
  float weight;
  char hash_string[MAXDNAME + 1];
  std::atomic<int> retriers = 0;
};

typedef ControlMatcher<ParentRecord, ParentResult> P_table;

// class ParentRecord : public ControlBase
//
//   A record for a configuration line in the parent.config
//    file
//
class ParentRecord : public ControlBase
{
public:
  ~ParentRecord();

  Result Init(matcher_line *line_info);
  bool DefaultInit(char *val);
  void UpdateMatch(ParentResult *result, RequestData *rdata);

  void Print() const;

  pRecord *parents           = nullptr;
  pRecord *secondary_parents = nullptr;
  int num_parents            = 0;
  int num_secondary_parents  = 0;

  bool
  bypass_ok() const
  {
    return go_direct;
  }

  const char *scheme = nullptr;
  // private:
  void PreProcessParents(const char *val, const int line_num, char *buf, size_t len);
  const char *ProcessParents(char *val, bool isPrimary);
  bool ignore_query                                                  = false;
  uint32_t rr_next                                                   = 0;
  bool go_direct                                                     = true;
  bool parent_is_proxy                                               = true;
  ParentSelectionStrategy *selection_strategy                        = nullptr;
  UnavailableServerResponseCodes *unavailable_server_retry_responses = nullptr;
  SimpleRetryResponseCodes *simple_server_retry_responses            = nullptr;
  ParentRetry_t parent_retry                                         = PARENT_RETRY_NONE;
  int max_simple_retries                                             = 1;
  int max_unavailable_server_retries                                 = 1;
  int secondary_mode                                                 = 1;
  bool ignore_self_detect                                            = false;
};

// If the parent was set by the external customer api,
//   our HttpRequestData structure told us what parent to
//   use and we are only called to preserve clean interface
//   between HttpTransact & the parent selection code.  The following
ParentRecord *const extApiRecord = (ParentRecord *)0xeeeeffff;

// used here to to set the number of ATSConsistentHashIter's
// used in NextHopSelectionStrategy to limit the host group
// size as well, group size is one to one with the number of rings
constexpr const uint32_t MAX_GROUP_RINGS = 5;

struct ParentResult {
  ParentResult() { reset(); }
  // For outside consumption
  ParentResultType result;
  const char *hostname;
  int port;
  bool retry;
  bool chash_init[MAX_GROUP_RINGS] = {false};
  HostStatus_t first_choice_status = HostStatus_t::HOST_STATUS_INIT;

  void
  reset()
  {
    ink_zero(*this);
    line_number   = -1;
    result        = PARENT_UNDEFINED;
    mapWrapped[0] = false;
    mapWrapped[1] = false;
  }

  bool
  is_api_result() const
  {
    return rec == extApiRecord;
  }

  // Do we have some result?
  bool
  is_some() const
  {
    if (rec == nullptr) {
      // If we don't have a result, we either haven't done a parent
      // lookup yet (PARENT_UNDEFINED), or the lookup didn't match
      // anything (PARENT_DIRECT).
      ink_assert(result == PARENT_UNDEFINED || result == PARENT_DIRECT);
      return false;
    }

    return true;
  }

  bool
  parent_is_proxy() const
  {
    // Parents set by the TSHttpTxnParentProxySet API are always considered proxies rather than origins.
    return is_api_result() ? true : rec->parent_is_proxy;
  }

  unsigned
  retry_type() const
  {
    return is_api_result() ? PARENT_RETRY_NONE : rec->parent_retry;
  }

  unsigned
  max_retries(ParentRetry_t method) const
  {
    // There's no API for specifying the retries, so you get 0.
    if (is_api_result()) {
      return 0;
    }

    switch (method) {
    case PARENT_RETRY_NONE:
      return 0;
    case PARENT_RETRY_SIMPLE:
      return rec->max_simple_retries;
    case PARENT_RETRY_UNAVAILABLE_SERVER:
      return rec->max_unavailable_server_retries;
    case PARENT_RETRY_BOTH:
      return std::max(rec->max_unavailable_server_retries, rec->max_simple_retries);
    }

    return 0;
  }

  bool
  response_is_retryable(HTTPStatus response_code) const
  {
    Debug("parent_select", "In response_is_retryable, code: %d", response_code);
    if (retry_type() == PARENT_RETRY_BOTH) {
      Debug("parent_select", "Saw retry both");
      return (rec->unavailable_server_retry_responses->contains(response_code) ||
              rec->simple_server_retry_responses->contains(response_code));
    } else if (retry_type() == PARENT_RETRY_UNAVAILABLE_SERVER) {
      Debug("parent_select", "Saw retry unavailable server");
      return rec->unavailable_server_retry_responses->contains(response_code);
    } else if (retry_type() == PARENT_RETRY_SIMPLE) {
      Debug("parent_select", "Saw retry simple retry");
      return rec->simple_server_retry_responses->contains(response_code);
    } else {
      return false;
    }
  }

  bool
  bypass_ok() const
  {
    if (is_api_result()) {
      return false;
    } else {
      // Caller should check for a valid result beforehand.
      ink_assert(result != PARENT_UNDEFINED);
      ink_assert(is_some());
      return rec->bypass_ok();
    }
  }

  void
  print()
  {
    printf("ParentResult - hostname: %s, port: %d, retry: %s, line_number: %d, last_parent: %d, start_parent: %d, wrap_around: %s, "
           "last_lookup: %d, result: %s\n",
           hostname, port, (retry) ? "true" : "false", line_number, last_parent, start_parent, (wrap_around) ? "true" : "false",
           last_lookup, ParentResultStr[result]);
  }

private:
  // Internal use only
  //   Not to be modified by HTTP
  int line_number;
  ParentRecord *rec;
  uint32_t last_parent;
  uint32_t start_parent;
  uint32_t last_group;
  bool wrap_around;
  bool mapWrapped[2];
  // state for consistent hash.
  int last_lookup;
  ATSConsistentHashIter chashIter[MAX_GROUP_RINGS];

  friend class NextHopSelectionStrategy;
  friend class NextHopRoundRobin;
  friend class NextHopConsistentHash;
  friend class ParentConsistentHash;
  friend class ParentRoundRobin;
  friend class ParentConfigParams;
  friend class ParentRecord;
  friend class ParentSelectionStrategy;
};

struct ParentSelectionPolicy {
  int32_t ParentRetryTime;
  int32_t ParentEnable;
  int32_t FailThreshold;
  ParentSelectionPolicy();
};

//
// API definition.
class ParentSelectionStrategy
{
public:
  int max_retriers = 0;

  ParentSelectionStrategy() { REC_ReadConfigInteger(max_retriers, "proxy.config.http.parent_proxy.max_trans_retries"); }
  //
  // Return the pRecord.
  virtual pRecord *getParents(ParentResult *result) = 0;
  // void selectParent(bool firstCall, ParentResult *result, RequestData *rdata, unsigned int fail_threshold, unsigned int
  // retry_time)
  //
  // The implementation parent lookup.
  //
  virtual void selectParent(bool firstCall, ParentResult *result, RequestData *rdata, unsigned int fail_threshold,
                            unsigned int retry_time) = 0;

  // uint32_t numParents(ParentResult *result);
  //
  // Returns the number of parent records in a strategy.
  //
  virtual uint32_t numParents(ParentResult *result) const = 0;
  void markParentDown(ParentResult *result, unsigned int fail_threshold, unsigned int retry_time);
  void markParentUp(ParentResult *result);

  // virtual destructor.
  virtual ~ParentSelectionStrategy(){};
};

class ParentConfigParams : public ConfigInfo
{
public:
  explicit ParentConfigParams(P_table *_parent_table);
  ~ParentConfigParams() override;

  bool apiParentExists(HttpRequestData *rdata);
  void findParent(HttpRequestData *rdata, ParentResult *result, unsigned int fail_threshold, unsigned int retry_time);
  void nextParent(HttpRequestData *rdata, ParentResult *result, unsigned int fail_threshold, unsigned int retry_time);
  bool parentExists(HttpRequestData *rdata);

  // implementation of functions from ParentSelectionStrategy.
  void
  selectParent(bool firstCall, ParentResult *result, RequestData *rdata, unsigned int fail_threshold, unsigned int retry_time)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result->rec->selection_strategy != nullptr);
      return result->rec->selection_strategy->selectParent(firstCall, result, rdata, fail_threshold, retry_time);
    }
  }

  void
  markParentDown(ParentResult *result, unsigned int fail_threshold, unsigned int retry_time)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result->rec->selection_strategy != nullptr);
      result->rec->selection_strategy->markParentDown(result, fail_threshold, retry_time);
    }
  }

  void
  markParentUp(ParentResult *result)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result != nullptr);
      result->rec->selection_strategy->markParentUp(result);
    }
  }

  uint32_t
  numParents(ParentResult *result)
  {
    if (result->is_api_result()) {
      return 1;
    } else {
      ink_release_assert(result->rec->selection_strategy != nullptr);
      return result->rec->selection_strategy->numParents(result);
    }
  }

  P_table *parent_table;
  ParentRecord *DefaultParent;
  ParentSelectionPolicy policy;
};

class HttpRequestData;

struct ParentConfig {
public:
  static void startup();
  static void reconfigure();
  static void print();
  static void set_parent_table(P_table *pTable, ParentRecord *rec, int num_elements);

  static ParentConfigParams *
  acquire()
  {
    return (ParentConfigParams *)configProcessor.get(ParentConfig::m_id);
  }

  static void
  release(ParentConfigParams *strategy)
  {
    configProcessor.release(ParentConfig::m_id, strategy);
  }

  static int m_id;
};

// Helper Functions
ParentRecord *createDefaultParent(char *val);

// Unit Test Functions
void show_result(ParentResult *aParentResult);
void br(HttpRequestData *h, const char *os_hostname, sockaddr const *dest_ip = nullptr); // short for build request
int verify(ParentResult *r, ParentResultType e, const char *h, int p);

/*
  For supporting multiple Socks servers, we essentially use the
  ParentSelection infrastructure. Only the initialization is different.
  If needed, we will have to implement most of the functions in
  ParentSection.cc for Socks as well. For right now we will just use
  ParentSelection

  All the members in ParentConfig are static. Right now
  we will duplicate the code for these static functions.
*/
struct SocksServerConfig {
  static void startup();
  static void reconfigure();
  static void print();

  static ParentConfigParams *
  acquire()
  {
    return (ParentConfigParams *)configProcessor.get(SocksServerConfig::m_id);
  }
  static void
  release(ParentConfigParams *params)
  {
    configProcessor.release(SocksServerConfig::m_id, params);
  }

  static int m_id;
};
