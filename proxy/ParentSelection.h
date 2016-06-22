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

#ifndef _PARENT_SELECTION_H_
#define _PARENT_SELECTION_H_

#include "Main.h"
#include "ProxyConfig.h"
#include "ControlBase.h"
#include "ControlMatcher.h"
#include "P_RecProcess.h"
#include "ts/ConsistentHash.h"
#include "ts/Tokenizer.h"
#include "ts/ink_apidefs.h"

#include <algorithm>
#include <vector>

#define MAX_PARENTS 64

struct RequestData;
struct matcher_line;
struct ParentResult;
class ParentRecord;
class ParentSelectionStrategy;

enum ParentResultType {
  PARENT_UNDEFINED,
  PARENT_DIRECT,
  PARENT_SPECIFIED,
  PARENT_AGENT,
  PARENT_FAIL,
};

enum ParentRR_t {
  P_NO_ROUND_ROBIN = 0,
  P_STRICT_ROUND_ROBIN,
  P_HASH_ROUND_ROBIN,
  P_CONSISTENT_HASH,
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

// struct pRecord
//
//    A record for an invidual parent
//
struct pRecord : ATSConsistentHashNode {
  char hostname[MAXDNAME + 1];
  int port;
  time_t failedAt;
  int failCount;
  int32_t upAt;
  const char *scheme; // for which parent matches (if any)
  int idx;
  float weight;
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
  ParentRecord()
    : parents(NULL),
      secondary_parents(NULL),
      num_parents(0),
      num_secondary_parents(0),
      ignore_query(false),
      rr_next(0),
      go_direct(true),
      parent_is_proxy(true),
      selection_strategy(NULL),
      unavailable_server_retry_responses(NULL),
      parent_retry(PARENT_RETRY_NONE),
      max_simple_retries(1),
      max_unavailable_server_retries(1)
  {
  }

  ~ParentRecord();

  config_parse_error Init(matcher_line *line_info);
  bool DefaultInit(char *val);
  void UpdateMatch(ParentResult *result, RequestData *rdata);
  void Print();
  pRecord *parents;
  pRecord *secondary_parents;
  int num_parents;
  int num_secondary_parents;

  bool
  bypass_ok() const
  {
    return go_direct;
  }

  const char *scheme;
  // private:
  const char *ProcessParents(char *val, bool isPrimary);
  bool ignore_query;
  volatile uint32_t rr_next;
  bool go_direct;
  bool parent_is_proxy;
  ParentSelectionStrategy *selection_strategy;
  UnavailableServerResponseCodes *unavailable_server_retry_responses;
  ParentRetry_t parent_retry;
  int max_simple_retries;
  int max_unavailable_server_retries;
};

// If the parent was set by the external customer api,
//   our HttpRequestData structure told us what parent to
//   use and we are only called to preserve clean interface
//   between HttpTransact & the parent selection code.  The following
ParentRecord *const extApiRecord = (ParentRecord *)0xeeeeffff;

struct ParentResult {
  ParentResult() { reset(); }
  // For outside consumption
  ParentResultType result;
  const char *hostname;
  int port;
  bool retry;

  void
  reset()
  {
    ink_zero(*this);
    line_number = -1;
    result      = PARENT_UNDEFINED;
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
    if (rec == NULL) {
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
    return (retry_type() & PARENT_RETRY_UNAVAILABLE_SERVER) && rec->unavailable_server_retry_responses->contains(response_code);
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

private:
  // Internal use only
  //   Not to be modified by HTTP
  int line_number;
  P_table *epoch; // A pointer to the table used.
  ParentRecord *rec;
  uint32_t last_parent;
  uint32_t start_parent;
  bool wrap_around;
  // state for consistent hash.
  int last_lookup;
  ATSConsistentHashIter chashIter[2];

  friend class ParentConsistentHash;
  friend class ParentRoundRobin;
  friend class ParentConfigParams;
  friend class ParentRecord;
};

struct ParentSelectionPolicy {
  int32_t ParentRetryTime;
  int32_t ParentEnable;
  int32_t FailThreshold;
  int32_t DNS_ParentOnly;
  ParentSelectionPolicy();
};

//
// API definition.
class ParentSelectionStrategy
{
public:
  // void selectParent(const ParentSelectionPolicy *policy, bool firstCall, ParentResult *result, RequestData *rdata)
  //
  // The implementation parent lookup.
  //
  virtual void selectParent(const ParentSelectionPolicy *policy, bool firstCall, ParentResult *result, RequestData *rdata) = 0;

  // void markParentDown(const ParentSelectionPolicy *policy, ParentResult *result)
  //
  //    Marks the parent pointed to by result as down
  //
  virtual void markParentDown(const ParentSelectionPolicy *policy, ParentResult *result) = 0;

  // uint32_t numParents(ParentResult *result);
  //
  // Returns the number of parent records in a strategy.
  //
  virtual uint32_t numParents(ParentResult *result) const = 0;

  // void markParentUp
  //
  //    After a successful retry, http calls this function
  //      to clear the bits indicating the parent is down
  //
  virtual void markParentUp(ParentResult *result) = 0;

  // virtual destructor.
  virtual ~ParentSelectionStrategy(){};
};

class ParentConfigParams : public ConfigInfo
{
public:
  explicit ParentConfigParams(P_table *_parent_table);
  ~ParentConfigParams(){};

  bool apiParentExists(HttpRequestData *rdata);
  void findParent(HttpRequestData *rdata, ParentResult *result);
  void nextParent(HttpRequestData *rdata, ParentResult *result);
  bool parentExists(HttpRequestData *rdata);

  // implementation of functions from ParentSelectionStrategy.
  void
  selectParent(bool firstCall, ParentResult *result, RequestData *rdata)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result->rec->selection_strategy != NULL);
      return result->rec->selection_strategy->selectParent(&policy, firstCall, result, rdata);
    }
  }

  void
  markParentDown(ParentResult *result)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result->rec->selection_strategy != NULL);
      result->rec->selection_strategy->markParentDown(&policy, result);
    }
  }

  uint32_t
  numParents(ParentResult *result)
  {
    if (result->is_api_result()) {
      return 1;
    } else {
      ink_release_assert(result->rec->selection_strategy != NULL);
      return result->rec->selection_strategy->numParents(result);
    }
  }

  void
  markParentUp(ParentResult *result)
  {
    if (!result->is_api_result()) {
      ink_release_assert(result != NULL);
      result->rec->selection_strategy->markParentUp(result);
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
void reloadDefaultParent(char *val);
void reloadParentFile();
int parentSelection_CB(const char *name, RecDataT data_type, RecData data, void *cookie);

// Unit Test Functions
void show_result(ParentResult *aParentResult);
void br(HttpRequestData *h, const char *os_hostname, sockaddr const *dest_ip = NULL); // short for build request
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

#endif
