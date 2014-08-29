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

#include "ink_apidefs.h"

#include "P_RecProcess.h"

#include "libts.h"

#define MAX_PARENTS 64

struct RequestData;

struct matcher_line;
struct ParentResult;
class ParentRecord;

enum ParentResultType
{
  PARENT_UNDEFINED, PARENT_DIRECT,
  PARENT_SPECIFIED, PARENT_AGENT, PARENT_FAIL
};

typedef ControlMatcher<ParentRecord, ParentResult> P_table;

//
// API to outside world
//
struct ParentResult
{
  ParentResult()
    : r(PARENT_UNDEFINED), hostname(NULL), port(0), line_number(0), epoch(NULL), rec(NULL),
      last_parent(0), start_parent(0), wrap_around(false), retry(false)
  { memset(foundParents, 0, sizeof(foundParents)); };

  // For outside consumption
  ParentResultType r;
  const char *hostname;
  int port;

  // Internal use only
  //   Not to be modified by HTTP
  int line_number;
  P_table *epoch;               // A pointer to the table used.
  ParentRecord *rec;
  uint32_t last_parent;
  uint32_t start_parent;
  bool wrap_around;
  bool retry;
  //Arena *a;
  ATSConsistentHashIter chashIter;
  bool foundParents[MAX_PARENTS];
};

class HttpRequestData;

struct ParentConfigParams:public ConfigInfo
{
  ParentConfigParams();
  ~ParentConfigParams();

  // void findParent(RequestData* rdata, ParentResult* result)
  //
  //   Does initial parent lookup
  //
  inkcoreapi void findParent(HttpRequestData *rdata, ParentResult *result);

  // void markParentDown(ParentResult* rsult)
  //
  //    Marks the parent pointed to by result as down
  //
  inkcoreapi void markParentDown(ParentResult *result);

  // void recordRetrySuccess
  //
  //    After a successful retry, http calls this function
  //      to clear the bits indicating the parent is down
  //
  void recordRetrySuccess(ParentResult *result);

  // void nextParent(RequestData* rdata, ParentResult* result);
  //
  //    Marks the parent pointed to by result as down and attempts
  //      to find the next parent
  //
  inkcoreapi void nextParent(HttpRequestData *rdata, ParentResult *result);

  // bool parentExists(HttpRequestData* rdata)
  //
  //   Returns true if there is a parent matching the request data and
  //   false otherwise
  bool parentExists(HttpRequestData *rdata);

  // bool apiParentExists(HttpRequestData* rdata)
  //
  //   Retures true if a parent has been set through the api
  bool apiParentExists(HttpRequestData *rdata);

  P_table *ParentTable;
  ParentRecord *DefaultParent;
  int32_t ParentRetryTime;
  int32_t ParentEnable;
  int32_t FailThreshold;
  int32_t DNS_ParentOnly;
};

struct ParentConfig
{
public:
  static void startup();
  static void reconfigure();
  static void print();

  inkcoreapi static ParentConfigParams *acquire() { return (ParentConfigParams *) configProcessor.get(ParentConfig::m_id); }
  inkcoreapi static void release(ParentConfigParams *params) { configProcessor.release(ParentConfig::m_id, params); }


  static int m_id;
};
//
// End API to outside world
//


// struct pRecord
//
//    A record for an invidual parent
//
struct pRecord : ATSConsistentHashNode
{
  char hostname[MAXDNAME + 1];
  int port;
  time_t failedAt;
  int failCount;
  int32_t upAt;
  const char *scheme;           // for which parent matches (if any)
  int idx;
  float weight;
};

enum ParentRR_t
{
  P_NO_ROUND_ROBIN = 0,
  P_STRICT_ROUND_ROBIN,
  P_HASH_ROUND_ROBIN,
  P_CONSISTENT_HASH
};

// class ParentRecord : public ControlBase
//
//   A record for a configuration line in the parent.config
//    file
//
class ParentRecord: public ControlBase
{
public:
  ParentRecord()
    : parents(NULL), num_parents(0), round_robin(P_NO_ROUND_ROBIN), rr_next(0), go_direct(true), chash(NULL)
  { }

  ~ParentRecord();

  char *Init(matcher_line *line_info);
  bool DefaultInit(char *val);
  void UpdateMatch(ParentResult *result, RequestData *rdata);
  void FindParent(bool firstCall, ParentResult *result, RequestData *rdata, ParentConfigParams *config);
  void Print();
  pRecord *parents;
  int num_parents;

  bool bypass_ok() const { return go_direct; }

  const char *scheme;
  //private:
  const char *ProcessParents(char *val);
  void buildConsistentHash(void);
  ParentRR_t round_robin;
  volatile uint32_t rr_next;
  bool go_direct;
  ATSConsistentHash *chash;
};

// Helper Functions
ParentRecord *createDefaultParent(char *val);
void reloadDefaultParent(char *val);
void reloadParentFile();
int parentSelection_CB(const char *name, RecDataT data_type, RecData data, void *cookie);

// Unit Test Functions
void show_result(ParentResult *aParentResult);
void br(HttpRequestData *h, const char *os_hostname, sockaddr const* dest_ip = NULL);       // short for build request
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
struct SocksServerConfig
{
  static void startup();
  static void reconfigure();
  static void print();

  static ParentConfigParams *acquire() { return (ParentConfigParams *) configProcessor.get(SocksServerConfig::m_id); }
  static void release(ParentConfigParams *params) { configProcessor.release(SocksServerConfig::m_id, params); }

  static int m_id;
};

#endif
