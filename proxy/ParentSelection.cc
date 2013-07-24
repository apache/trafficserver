/** @file

  Implementation of Parent Proxy routing

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
#include "libts.h"
#include "P_EventSystem.h"
#include "ParentSelection.h"
#include "ControlMatcher.h"
#include "Main.h"
#include "Error.h"
#include "ProxyConfig.h"
#include "HTTP.h"
#include "HttpTransact.h"

#define PARENT_RegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc
#define PARENT_ReadConfigInteger REC_ReadConfigInteger
#define PARENT_ReadConfigStringAlloc REC_ReadConfigStringAlloc

typedef ControlMatcher<ParentRecord, ParentResult> P_table;

// Global Vars for Parent Selection
static const char modulePrefix[] = "[ParentSelection]";
static ConfigUpdateHandler<ParentConfig> * parentConfigUpdate = NULL;

// Config var names
static const char *file_var = "proxy.config.http.parent_proxy.file";
static const char *default_var = "proxy.config.http.parent_proxies";
static const char *retry_var = "proxy.config.http.parent_proxy.retry_time";
static const char *enable_var = "proxy.config.http.parent_proxy_routing_enable";
static const char *threshold_var = "proxy.config.http.parent_proxy.fail_threshold";
static const char *dns_parent_only_var = "proxy.config.http.no_dns_just_forward_to_parent";

static const char *ParentResultStr[] = {
  "Parent_Undefined",
  "Parent_Direct",
  "Parent_Specified",
  "Parent_Failed"
};

static const char *ParentRRStr[] = {
  "false",
  "strict",
  "true"
};

//
//  Config Callback Prototypes
//
enum ParentCB_t
{
  PARENT_FILE_CB, PARENT_DEFAULT_CB,
  PARENT_RETRY_CB, PARENT_ENABLE_CB,
  PARENT_THRESHOLD_CB, PARENT_DNS_ONLY_CB
};

// If the parent was set by the external customer api,
//   our HttpRequestData structure told us what parent to
//   use and we are only called to preserve clean interface
//   between HttpTransact & the parent selection code.  The following
ParentRecord *const extApiRecord = (ParentRecord *) 0xeeeeffff;

ParentConfigParams::ParentConfigParams()
  : ParentTable(NULL), DefaultParent(NULL), ParentRetryTime(30), ParentEnable(0), FailThreshold(10), DNS_ParentOnly(0)
{ }

ParentConfigParams::~ParentConfigParams()
{
  if (ParentTable) {
    delete ParentTable;
  }

  if (DefaultParent) {
    delete DefaultParent;
  }
}

int ParentConfig::m_id = 0;

//
//   Begin API functions
//
void
ParentConfig::startup()
{
  parentConfigUpdate = NEW(new ConfigUpdateHandler<ParentConfig>());

  // Load the initial configuration
  reconfigure();

  // Setup the callbacks for reconfiuration
  //   parent table
  parentConfigUpdate->attach(file_var);
  //   default parent
  parentConfigUpdate->attach(default_var);
  //   Retry time
  parentConfigUpdate->attach(retry_var);
  //   Enable
  parentConfigUpdate->attach(enable_var);

  //   Fail Threshold
  parentConfigUpdate->attach(threshold_var);

  //   DNS Parent Only
  parentConfigUpdate->attach(dns_parent_only_var);
}

void
ParentConfig::reconfigure()
{
  char *default_val = NULL;
  int retry_time = 30;
  int enable = 0;
  int fail_threshold;
  int dns_parent_only;

  ParentConfigParams *params;
  params = NEW(new ParentConfigParams);

  // Allocate parent table
  params->ParentTable = NEW(new P_table(file_var, modulePrefix, &http_dest_tags));

  // Handle default parent
  PARENT_ReadConfigStringAlloc(default_val, default_var);
  params->DefaultParent = createDefaultParent(default_val);
  ats_free(default_val);

  // Handle parent timeout
  PARENT_ReadConfigInteger(retry_time, retry_var);
  params->ParentRetryTime = retry_time;

  // Handle parent enable
  PARENT_ReadConfigInteger(enable, enable_var);
  params->ParentEnable = enable;

  // Handle the fail threshold
  PARENT_ReadConfigInteger(fail_threshold, threshold_var);
  params->FailThreshold = fail_threshold;

  // Handle dns parent only
  PARENT_ReadConfigInteger(dns_parent_only, dns_parent_only_var);
  params->DNS_ParentOnly = dns_parent_only;

  m_id = configProcessor.set(m_id, params);

  if (is_debug_tag_set("parent_config")) {
    ParentConfig::print();
  }
}

// void ParentConfig::print
//
//   Debugging function
//
void
ParentConfig::print()
{
  ParentConfigParams *params = ParentConfig::acquire();

  printf("Parent Selection Config\n");
  printf("\tEnabled %d\tRetryTime %d\tParent DNS Only %d\n",
         params->ParentEnable, params->ParentRetryTime, params->DNS_ParentOnly);
  if (params->DefaultParent == NULL) {
    printf("\tNo Default Parent\n");
  } else {
    printf("\tDefault Parent:\n");
    params->DefaultParent->Print();
  }
  printf("  ");
  params->ParentTable->Print();

  ParentConfig::release(params);
}

bool
ParentConfigParams::apiParentExists(HttpRequestData * rdata)
{
  return (rdata->api_info && rdata->api_info->parent_proxy_name != NULL && rdata->api_info->parent_proxy_port > 0);
}

bool
ParentConfigParams::parentExists(HttpRequestData * rdata)
{
  ParentResult junk;

  findParent(rdata, &junk);

  if (junk.r == PARENT_SPECIFIED) {
    return true;
  } else {
    return false;
  }
}

void
ParentConfigParams::findParent(HttpRequestData * rdata, ParentResult * result)
{
  P_table *tablePtr = ParentTable;
  ParentRecord *defaultPtr = DefaultParent;
  ParentRecord *rec;

  ink_assert(result->r == PARENT_UNDEFINED);

  // Check to see if we are enabled
  if (ParentEnable == 0) {
    result->r = PARENT_DIRECT;
    return;
  }
  // Initialize the result structure
  result->rec = NULL;
  result->epoch = tablePtr;
  result->line_number = 0xffffffff;
  result->wrap_around = false;
  // if this variabel is not set, we have problems: the code in
  // FindParent relies on the value of start_parent and when it is not
  // initialized, the code in FindParent can get into an infinite loop!
  result->start_parent = 0;
  result->last_parent = 0;

  // Check to see if the parent was set through the
  //   api
  if (apiParentExists(rdata)) {
    result->r = PARENT_SPECIFIED;
    result->hostname = rdata->api_info->parent_proxy_name;
    result->port = rdata->api_info->parent_proxy_port;
    result->rec = extApiRecord;
    result->epoch = NULL;
    result->start_parent = 0;
    result->last_parent = 0;

    Debug("parent_select", "Result for %s was API set parent %s:%d", rdata->get_host(), result->hostname, result->port);
  }

  tablePtr->Match(rdata, result);
  rec = result->rec;

  if (rec == NULL) {
    // No parents were found
    //
    // If there is a default parent, use it
    if (defaultPtr != NULL) {
      rec = result->rec = defaultPtr;
    } else {
      result->r = PARENT_DIRECT;
      Debug("cdn", "Returning PARENT_DIRECT (no parents were found)");
      return;
    }
  }
  // Loop through the set of parents to see if any are
  //   available
  Debug("cdn", "Calling FindParent from findParent");

  // Bug INKqa08251:
  // If a parent proxy is set by the API,
  // no need to call FindParent()
  if (rec != extApiRecord)
    rec->FindParent(true, result, rdata, this);

  if (is_debug_tag_set("parent_select") || is_debug_tag_set("cdn")) {
    switch (result->r) {
    case PARENT_UNDEFINED:
      Debug("cdn", "PARENT_UNDEFINED");
      break;
    case PARENT_FAIL:
      Debug("cdn", "PARENT_FAIL");
      break;
    case PARENT_DIRECT:
      Debug("cdn", "PARENT_DIRECT");
      break;
    case PARENT_SPECIFIED:
      Debug("cdn", "PARENT_SPECIFIED");
      break;
    default:
      // Handled here:
      // PARENT_AGENT
      break;
    }

    const char *host = rdata->get_host();

    switch (result->r) {
    case PARENT_UNDEFINED:
    case PARENT_FAIL:
    case PARENT_DIRECT:
      Debug("parent_select", "Result for %s was %s", host, ParentResultStr[result->r]);
      break;
    case PARENT_SPECIFIED:
      Debug("parent_select", "sizeof ParentResult = %zu", sizeof(ParentResult));
      Debug("parent_select", "Result for %s was parent %s:%d", host, result->hostname, result->port);
      break;
    default:
      // Handled here:
      // PARENT_AGENT
      break;
    }
  }
}


void
ParentConfigParams::recordRetrySuccess(ParentResult * result)
{
  pRecord *pRec;

  //  Make sure that we are being called back with with a
  //   result structure with a parent that is being retried
  ink_release_assert(result->retry == true);
  ink_assert(result->r == PARENT_SPECIFIED);
  if (result->r != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->rec == extApiRecord) {
    ink_assert(0);
    return;
  }

  ink_assert((int) (result->last_parent) < result->rec->num_parents);
  pRec = result->rec->parents + result->last_parent;

  ink_atomic_swap(&pRec->failedAt, (time_t)0);
  int old_count = ink_atomic_swap(&pRec->failCount, 0);

  if (old_count > 0) {
    Note("http parent proxy %s:%d restored", pRec->hostname, pRec->port);
  }
}

void
ParentConfigParams::markParentDown(ParentResult * result)
{
  time_t now;
  pRecord *pRec;
  int new_fail_count = 0;

  //  Make sure that we are being called back with with a
  //   result structure with a parent
  ink_assert(result->r == PARENT_SPECIFIED);
  if (result->r != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->rec == extApiRecord) {
    return;
  }

  ink_assert((int) (result->last_parent) < result->rec->num_parents);
  pRec = result->rec->parents + result->last_parent;

  // If the parent has already been marked down, just increment
  //   the failure count.  If this is the first mark down on a
  //   parent we need to both set the failure time and set
  //   count to one.  It's possible for the count and time get out
  //   sync due there being no locks.  Therefore the code should
  //   handle this condition.  If this was the result of a retry, we
  //   must update move the failedAt timestamp to now so that we continue
  //   negative cache the parent
  if (pRec->failedAt == 0 || result->retry == true) {
    // Reread the current time.  We want this to be accurate since
    //   it relates to how long the parent has been down.
    now = time(NULL);

    // Mark the parent as down
    ink_atomic_swap(&pRec->failedAt, now);

    // If this is clean mark down and not a failed retry, we
    //   must set the count to reflect this
    if (result->retry == false) {
      new_fail_count = pRec->failCount = 1;
    }

    Debug("parent_select", "Parent %s marked as down %s:%d", (result->retry) ? "retry" : "initially", pRec->hostname, pRec->port);

  } else {
    int old_count = ink_atomic_increment(&pRec->failCount, 1);

    Debug("parent_select", "Parent fail count increased to %d for %s:%d", old_count + 1, pRec->hostname, pRec->port);
    new_fail_count = old_count + 1;
  }

  if (new_fail_count > 0 && new_fail_count == FailThreshold) {
    Note("http parent proxy %s:%d marked down", pRec->hostname, pRec->port);
  }
}

void
ParentConfigParams::nextParent(HttpRequestData * rdata, ParentResult * result)
{
  P_table *tablePtr = ParentTable;

  //  Make sure that we are being called back with a
  //   result structure with a parent
  ink_assert(result->r == PARENT_SPECIFIED);
  if (result->r != PARENT_SPECIFIED) {
    result->r = PARENT_FAIL;
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->rec == extApiRecord) {
    Debug("parent_select", "Retry result for %s was %s", rdata->get_host(), ParentResultStr[result->r]);
    result->r = PARENT_FAIL;
    return;
  }
  // The epoch pointer is a legacy from the time when the tables
  //  would be swapped and deleted in the future.  I'm using the
  //  pointer now to ensure that the ParentConfigParams structure
  //  is properly used.  The table should never change out from
  //  under the a http transaction
  ink_release_assert(tablePtr == result->epoch);

  // Find the next parent in the array
  Debug("cdn", "Calling FindParent from nextParent");
  result->rec->FindParent(false, result, rdata, this);

  switch (result->r) {
  case PARENT_UNDEFINED:
    Debug("cdn", "PARENT_UNDEFINED");
    break;
  case PARENT_FAIL:
    Debug("cdn", "PARENT_FAIL");
    break;
  case PARENT_DIRECT:
    Debug("cdn", "PARENT_DIRECT");
    break;
  case PARENT_SPECIFIED:
    Debug("cdn", "PARENT_SPECIFIED");
    break;
  default:
    // Handled here:
    // PARENT_AGENT
    break;
  }

  if (is_debug_tag_set("parent_select")) {
    const char *host = rdata->get_host();

    switch (result->r) {
    case PARENT_UNDEFINED:
    case PARENT_FAIL:
    case PARENT_DIRECT:
      Debug("parent_select", "Retry result for %s was %s", host, ParentResultStr[result->r]);
      break;
    case PARENT_SPECIFIED:
      Debug("parent_select", "Retry result for %s was parent %s:%d", host, result->hostname, result->port);
      break;
    default:
      // Handled here:
      // PARENT_AGENT
      break;
    }
  }
}

//
//   End API functions
//

void
ParentRecord::FindParent(bool first_call, ParentResult * result, RequestData * rdata, ParentConfigParams * config)
{
  Debug("cdn", "Entering FindParent (the inner loop)");
  int cur_index = 0;
  bool parentUp = false;
  bool parentRetry = false;
  bool bypass_ok = (go_direct == true && config->DNS_ParentOnly == 0);

  HttpRequestData *request_info = (HttpRequestData *) rdata;

  ink_assert(num_parents > 0 || go_direct == true);

  if (first_call == true) {
    if (parents == NULL) {
      // We should only get into this state if
      //   if we are supposed to go dirrect
      ink_assert(go_direct == true);
      goto NO_PARENTS;
    } else if (round_robin == true) {
      cur_index = ink_atomic_increment((int32_t *) & rr_next, 1);
      cur_index = result->start_parent = cur_index % num_parents;
    } else {
      switch (round_robin) {
      case P_STRICT_ROUND_ROBIN:
        cur_index = ink_atomic_increment((int32_t *) & rr_next, 1);
        cur_index = cur_index % num_parents;
        break;
      case P_HASH_ROUND_ROBIN:
        // INKqa12817 - make sure to convert to host byte order
        // Why was it important to do host order here?  And does this have any
        // impact with the transition to IPv6?  The IPv4 functionality is 
        // preserved for now anyway as ats_ip_hash returns the 32-bit address in
        // that case.
        if (rdata->get_client_ip() != NULL) {
            cur_index = ntohl(ats_ip_hash(rdata->get_client_ip())) % num_parents;
        } else {
            cur_index = 0;
        }
        break;
      case P_NO_ROUND_ROBIN:
        cur_index = result->start_parent = 0;
        break;
      default:
        ink_release_assert(0);
      }
    }
  } else {
    // Move to next parent due to failure
    cur_index = (result->last_parent + 1) % num_parents;

    // Check to see if we have wrapped around
    if ((unsigned int) cur_index == result->start_parent) {
      // We've wrapped around so bypass if we can
      if (bypass_ok == true) {
        goto NO_PARENTS;
      } else {
        // Bypass disabled so keep trying, ignoring whether we think
        //   a parent is down or not
      FORCE_WRAP_AROUND:
        result->wrap_around = true;
      }
    }
  }

  // Loop through the array of parent seeing if any are up or
  //   should be retried
  do {
    // DNS ParentOnly inhibits bypassing the parent so always return that t
    if ((parents[cur_index].failedAt == 0) || (parents[cur_index].failCount < config->FailThreshold)) {
      Debug("parent_select", "config->FailThreshold = %d", config->FailThreshold);
      Debug("parent_select", "Selecting a down parent due to little failCount"
            "(faileAt: %u failCount: %d)", (unsigned)parents[cur_index].failedAt, parents[cur_index].failCount);
      parentUp = true;
    } else {
      if ((result->wrap_around) || ((parents[cur_index].failedAt + config->ParentRetryTime) < request_info->xact_start)) {
        Debug("parent_select", "Parent[%d].failedAt = %u, retry = %u,xact_start = %" PRId64 " but wrap = %d", cur_index,
              (unsigned)parents[cur_index].failedAt, config->ParentRetryTime, (int64_t)request_info->xact_start, result->wrap_around);
        // Reuse the parent
        parentUp = true;
        parentRetry = true;
        Debug("parent_select", "Parent marked for retry %s:%d", parents[cur_index].hostname, parents[cur_index].port);

      } else {
        parentUp = false;
      }
    }

    if (parentUp == true) {
      result->r = PARENT_SPECIFIED;
      result->hostname = parents[cur_index].hostname;
      result->port = parents[cur_index].port;
      result->last_parent = cur_index;
      result->retry = parentRetry;
      ink_assert(result->hostname != NULL);
      ink_assert(result->port != 0);
      Debug("parent_select", "Chosen parent = %s.%d", result->hostname, result->port);
      return;
    }

    cur_index = (cur_index + 1) % num_parents;

  } while ((unsigned int) cur_index != result->start_parent);

  // We can't bypass so retry, taking any parent that we can
  if (bypass_ok == false) {
    goto FORCE_WRAP_AROUND;
  }

NO_PARENTS:

  // Could not find a parent
  if (this->go_direct == true) {
    result->r = PARENT_DIRECT;
  } else {
    result->r = PARENT_FAIL;
  }

  result->hostname = NULL;
  result->port = 0;
}

// const char* ParentRecord::ProcessParents(char* val)
//
//   Reads in the value of a "round-robin" or "order"
//     directive and parses out the individual parents
//     allocates and builds the this->parents array
//
//   Returns NULL on success and a static error string
//     on failure
//
const char *
ParentRecord::ProcessParents(char *val)
{
  Tokenizer pTok(",; \t\r");
  int numTok;
  const char *current;
  int port;
  char *tmp;
  const char *errPtr;

  if (parents != NULL) {
    return "Can not specify more than one set of parents";
  }

  numTok = pTok.Initialize(val, SHARE_TOKS);

  if (numTok == 0) {
    return "No parents specified";
  }
  // Allocate the parents array
  this->parents = (pRecord *)ats_malloc(sizeof(pRecord) * numTok);

  // Loop through the set of parents specified
  //
  for (int i = 0; i < numTok; i++) {
    current = pTok[i];

    // Find the parent port
    tmp = (char *) strchr(current, ':');

    if (tmp == NULL) {
      errPtr = "No parent port specified";
      goto MERROR;
    }
    // Read the parent port
    //coverity[secure_coding]
    if (sscanf(tmp + 1, "%d", &port) != 1) {
      errPtr = "Malformed parent port";
      goto MERROR;
    }
    // Make sure that is no garbage beyond the parent
    //   port
    char *scan = tmp + 1;
    for (; *scan != '\0' && ParseRules::is_digit(*scan); scan++);
    for (; *scan != '\0' && ParseRules::is_wslfcr(*scan); scan++);
    if (*scan != '\0') {
      errPtr = "Garbage trailing entry or invalid separator";
      goto MERROR;
    }
    // Check to make sure that the string will fit in the
    //  pRecord
    if (tmp - current > MAXDNAME) {
      errPtr = "Parent hostname is too long";
      goto MERROR;
    } else if (tmp - current == 0) {
      errPtr = "Parent string is emtpy";
      goto MERROR;
    }
    // Update the pRecords
    memcpy(this->parents[i].hostname, current, tmp - current);
    this->parents[i].hostname[tmp - current] = '\0';
    this->parents[i].port = port;
    this->parents[i].failedAt = 0;
    this->parents[i].scheme = scheme;
  }

  num_parents = numTok;
  return NULL;

MERROR:
  ats_free(parents);
  parents = NULL;

  return errPtr;
}

// bool ParentRecord::DefaultInit(char* val)
//
//    Creates the record for a default parent proxy rule
///     established by a config variable
//
//    matcher_line* line_info - contains the value of
//      proxy.config.http.parent_proxies
//
//    Returns true on success and false on failure
//
bool
ParentRecord::DefaultInit(char *val)
{
  const char *errPtr;
  char *errBuf;
  bool alarmAlready = false;

  this->go_direct = true;
  this->round_robin = P_NO_ROUND_ROBIN;
  this->scheme = NULL;
  errPtr = ProcessParents(val);

  if (errPtr != NULL) {
    errBuf = (char *)ats_malloc(1024);
    snprintf(errBuf, 1024, "%s %s for default parent proxy", modulePrefix, errPtr);
    SignalError(errBuf, alarmAlready);
    ats_free(errBuf);
    return false;
  } else {
    return true;
  }
}


// char* ParentRecord::Init(matcher_line* line_info)
//
//    matcher_line* line_info - contains parsed label/value
//      pairs of the current cache.config line
//
//    Returns NULL if everything is OK
//      Otherwise, returns an error string that the caller MUST
//        DEALLOCATE with ats_free()
//
char *
ParentRecord::Init(matcher_line * line_info)
{
  const char *errPtr = NULL;
  char *errBuf;
  const int errBufLen = 1024;
  const char *tmp;
  char *label;
  char *val;
  bool used = false;

  this->line_num = line_info->line_num;
  this->scheme = NULL;

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    used = false;
    label = line_info->line[0][i];
    val = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, "round_robin") == 0) {
      if (strcasecmp(val, "true") == 0) {
        round_robin = P_HASH_ROUND_ROBIN;
      } else if (strcasecmp(val, "strict") == 0) {
        round_robin = P_STRICT_ROUND_ROBIN;
      } else if (strcasecmp(val, "false") == 0) {
        round_robin = P_NO_ROUND_ROBIN;
      } else {
        round_robin = P_NO_ROUND_ROBIN;
        errPtr = "invalid argument to round_robin directive";
      }
      used = true;
    } else if (strcasecmp(label, "parent") == 0) {
      errPtr = ProcessParents(val);
      used = true;
    } else if (strcasecmp(label, "go_direct") == 0) {
      if (strcasecmp(val, "false") == 0) {
        go_direct = false;
      } else if (strcasecmp(val, "true") != 0) {
        errPtr = "invalid argument to go_direct directive";
      } else {
        go_direct = true;
      }
      used = true;
    }
    // Report errors generated by ProcessParents();
    if (errPtr != NULL) {
      errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
      snprintf(errBuf, errBufLen, "%s %s at line %d", modulePrefix, errPtr, line_num);
      return errBuf;
    }

    if (used == true) {
      // Consume the label/value pair we used
      line_info->line[0][i] = NULL;
      line_info->num_el--;
    }
  }

  if (this->parents == NULL && go_direct == false) {
    errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
    snprintf(errBuf, errBufLen, "%s No parent specified in parent.config at line %d", modulePrefix, line_num);
    return errBuf;
  }
  // Process any modifiers to the directive, if they exist
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != NULL) {
      errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
      snprintf(errBuf, errBufLen, "%s %s at line %d in parent.config", modulePrefix, tmp, line_num);
      return errBuf;
    }
    // record SCHEME modifier if present.
    // NULL if not present
    this->scheme = this->getSchemeModText();
    if (this->scheme != NULL) {
      // update parent entries' schemes
      for (int j = 0; j < num_parents; j++) {
        this->parents[j].scheme = this->scheme;
      }
    }
  }

  return NULL;
}

// void ParentRecord::UpdateMatch(ParentResult* result, RequestData* rdata);
//
//    Updates the record ptr in result if the this element
//     appears later in the file
//
void
ParentRecord::UpdateMatch(ParentResult * result, RequestData * rdata)
{
  if (this->CheckForMatch((HttpRequestData *) rdata, result->line_number) == true) {
    result->rec = this;
    result->line_number = this->line_num;

    Debug("parent_select", "Matched with %p parent node from line %d", this, this->line_num);
  }
}

ParentRecord::~ParentRecord()
{
  ats_free(parents);
}

void
ParentRecord::Print()
{
  printf("\t\t");
  for (int i = 0; i < num_parents; i++) {
    printf(" %s:%d ", parents[i].hostname, parents[i].port);
  }
  printf(" rr=%s direct=%s\n", ParentRRStr[round_robin], (go_direct == true) ? "true" : "false");
}

// ParentRecord* createDefaultParent(char* val)
//
//  Atttemtps to allocate and init new ParentRecord
//    for a default parent
//
//  Returns a pointer to the new record on success
//   and NULL on failure
//
ParentRecord *
createDefaultParent(char *val)
{
  ParentRecord *newRec;

  if (val == NULL || *val == '\0') {
    return NULL;
  }

  newRec = NEW(new ParentRecord);
  if (newRec->DefaultInit(val) == true) {
    return newRec;
  } else {
    delete newRec;
    return NULL;
  }
}

//
//ParentConfig equivalent functions for SocksServerConfig
//

int SocksServerConfig::m_id = 0;
static Ptr<ProxyMutex> socks_server_reconfig_mutex;
void
SocksServerConfig::startup()
{
  socks_server_reconfig_mutex = new_ProxyMutex();

  // Load the initial configuration
  reconfigure();

  /* Handle update functions later. Socks does not yet support config update */
}

static int
setup_socks_servers(ParentRecord * rec_arr, int len)
{
  /* This changes hostnames into ip addresses and sets go_direct to false */
  for (int j = 0; j < len; j++) {
    rec_arr[j].go_direct = false;

    pRecord *pr = rec_arr[j].parents;
    int n_parents = rec_arr[j].num_parents;

    for (int i = 0; i < n_parents; i++) {
      IpEndpoint ip4, ip6;
      if (0 == ats_ip_getbestaddrinfo(pr[i].hostname, &ip4, &ip6)) {
        IpEndpoint* ip = ats_is_ip6(&ip6) ? &ip6 : &ip4;
        ats_ip_ntop(ip, pr[i].hostname, MAXDNAME+1);
      } else {
        Warning("Could not resolve socks server name \"%s\". " "Please correct it", pr[i].hostname);
        snprintf(pr[i].hostname, MAXDNAME+1, "255.255.255.255");
      }
    }
  }

  return 0;
}


void
SocksServerConfig::reconfigure()
{
  char *default_val = NULL;
  int retry_time = 30;
  int fail_threshold;

  ParentConfigParams *params;
  params = NEW(new ParentConfigParams);

  // Allocate parent table
  params->ParentTable = NEW(new P_table("proxy.config.socks.socks_config_file", "[Socks Server Selection]",
                                        &socks_server_tags));

  // Handle default parent
  PARENT_ReadConfigStringAlloc(default_val, "proxy.config.socks.default_servers");
  params->DefaultParent = createDefaultParent(default_val);
  ats_free(default_val);

  if (params->DefaultParent)
    setup_socks_servers(params->DefaultParent, 1);
  if (params->ParentTable->ipMatch)
    setup_socks_servers(params->ParentTable->ipMatch->data_array, params->ParentTable->ipMatch->array_len);

  // Handle parent timeout
  PARENT_ReadConfigInteger(retry_time, "proxy.config.socks.server_retry_time");
  params->ParentRetryTime = retry_time;

  // Handle parent enable
  // enable is always true for use. We will come here only if socks is enabled
  params->ParentEnable = 1;

  // Handle the fail threshold
  PARENT_ReadConfigInteger(fail_threshold, "proxy.config.socks.server_fail_threshold");
  params->FailThreshold = fail_threshold;

  // Handle dns parent only
  //PARENT_ReadConfigInteger(dns_parent_only, dns_parent_only_var);
  params->DNS_ParentOnly = 0;

  m_id = configProcessor.set(m_id, params);

  if (is_debug_tag_set("parent_config")) {
    SocksServerConfig::print();
  }
}

void
SocksServerConfig::print()
{
  ParentConfigParams *params = SocksServerConfig::acquire();

  printf("Parent Selection Config for Socks Server\n");
  printf("\tEnabled %d\tRetryTime %d\tParent DNS Only %d\n",
         params->ParentEnable, params->ParentRetryTime, params->DNS_ParentOnly);
  if (params->DefaultParent == NULL) {
    printf("\tNo Default Parent\n");
  } else {
    printf("\tDefault Parent:\n");
    params->DefaultParent->Print();
  }
  printf("  ");
  params->ParentTable->Print();

  SocksServerConfig::release(params);
}

#define TEST_FAIL(str) { \
  printf("%d: %s\n", test_id,str);\
  err= REGRESSION_TEST_FAILED;\
}

void
request_to_data(HttpRequestData * req, sockaddr const* srcip, sockaddr const* dstip, const char *str)
{
  HTTPParser parser;

  ink_zero(req->src_ip);
  ats_ip_copy(&req->src_ip.sa, srcip);
  ink_zero(req->dest_ip);
  ats_ip_copy(&req->dest_ip.sa, dstip);

  req->hdr = NEW(new HTTPHdr);

  http_parser_init(&parser);

  req->hdr->parse_req(&parser, &str, str + strlen(str), true);

  http_parser_clear(&parser);
}


static int passes;
static int fails;

// Parenting Tests
EXCLUSIVE_REGRESSION_TEST(PARENTSELECTION) (RegressionTest * /* t ATS_UNUSED */,
                                            int /* intensity_level ATS_UNUSED */, int *pstatus)
{
  // first, set everything up
  *pstatus = REGRESSION_TEST_INPROGRESS;
  ParentConfig config;
  ParentConfigParams *params = new ParentConfigParams();
  params->FailThreshold = 1;
  params->ParentRetryTime = 5;
  passes = fails = 0;
  config.startup();
  params->ParentEnable = true;
  char tbl[2048];
#define T(x) ink_strlcat(tbl,x, sizeof(tbl));
#define REBUILD params->ParentTable = new P_table("", "ParentSelection Unit Test Table", &http_dest_tags, ALLOW_HOST_TABLE | ALLOW_REGEX_TABLE | ALLOW_URL_TABLE | ALLOW_IP_TABLE | DONT_BUILD_TABLE); params->ParentTable->BuildTableFromString(tbl);
  HttpRequestData *request = NULL;
  ParentResult *result = NULL;
#define REINIT delete request; delete result; request = new HttpRequestData(); result = new ParentResult(); if (!result || !request) { (void)printf("Allocation failed\n"); return; }
#define ST(x) printf ("*** TEST %d *** STARTING ***\n", x);
#define RE(x,y) if (x) { printf("*** TEST %d *** PASSED ***\n", y); passes ++; } else { printf("*** TEST %d *** FAILED *** FAILED *** FAILED ***\n", y); fails++; }
#define FP params->findParent(request, result);

  // Test 1
  tbl[0] = '\0';
  ST(1)
    T("dest_domain=. parent=red:37412,orange:37412,yellow:37412 round_robin=strict\n")
  REBUILD int c, red = 0, orange = 0, yellow = 0;
  for (c = 0; c < 21; c++) {
    REINIT br(request, "fruit_basket.net");
    FP red += verify(result, PARENT_SPECIFIED, "red", 37412);
    orange += verify(result, PARENT_SPECIFIED, "orange", 37412);
    yellow += verify(result, PARENT_SPECIFIED, "yellow", 37412);
  }
  RE(((red == 7) && (orange == 7) && (yellow == 7)), 1)
    // Test 2
    ST(2)
    tbl[0] = '\0';
  T("dest_domain=. parent=green:4325,blue:4325,indigo:4325,violet:4325 round_robin=false\n")
  REBUILD int g = 0, b = 0, i = 0, v = 0;
  for (c = 0; c < 17; c++) {
    REINIT br(request, "fruit_basket.net");
    FP g += verify(result, PARENT_SPECIFIED, "green", 4325);
    b += verify(result, PARENT_SPECIFIED, "blue", 4325);
    i += verify(result, PARENT_SPECIFIED, "indigo", 4325);
    v += verify(result, PARENT_SPECIFIED, "violet", 4325);
  }
  RE((((g == 17) && !b && !i && !v) || (!g && (b == 17) && !i && !v) || (!g && !b && (i == 17) && !v) ||
      (!g && !b && !i && (v == 17))), 2)
    // Test 3 - 6 Parenting Table
    tbl[0] = '\0';
# define TEST_IP4_ADDR "209.131.62.14"
# define TEST_IP6_ADDR "BEEF:DEAD:ABBA:CAFE:1337:1E1F:5EED:C0FF"
  T("dest_ip=" TEST_IP4_ADDR " parent=cat:37,dog:24 round_robin=strict\n")  /* L1 */
  T("dest_ip=" TEST_IP6_ADDR " parent=zwoop:37,jMCg:24 round_robin=strict\n")  /* L1 */
    T("dest_host=www.pilot.net parent=pilot_net:80\n")  /* L2 */
    T("url_regex=snoopy parent=odie:80,garfield:80 round_robin=true\n") /* L3 */
    T("dest_domain=i.am parent=amy:80,katie:80,carissa:771 round_robin=false\n")        /* L4 */
    T("dest_domain=microsoft.net time=03:00-22:10 parent=zoo.net:341\n")        /* L5 */
    T("dest_domain=microsoft.net time=0:00-02:59 parent=zoo.net:347\n") /* L6 */
    T("dest_domain=microsoft.net time=22:11-23:59 parent=zoo.edu:111\n")        /* L7 */
    T("dest_domain=imac.net port=819 parent=genie:80 round_robin=strict\n")     /* L8 */
    T("dest_ip=172.34.61.211 port=3142 parent=orangina:80 go_direct=false\n")   /* L9 */
    T("url_regex=miffy prefix=furry/rabbit parent=nintje:80 go_direct=false\n") /* L10 */
    T("url_regex=kitty suffix=tif parent=hello:80 round_robin=strict go_direct=false\n")        /* L11 */
    T("url_regex=cyclops method=get parent=turkey:80\n")        /* L12 */
    T("url_regex=cyclops method=post parent=club:80\n") /* L13 */
    T("url_regex=cyclops method=put parent=sandwich:80\n")      /* L14 */
    T("url_regex=cyclops method=trace parent=mayo:80\n")        /* L15 */
    T("dest_host=pluto scheme=HTTP parent=strategy:80\n")       /* L16 */
    REBUILD
    // Test 3
    IpEndpoint ip;
    ats_ip_pton(TEST_IP4_ADDR, &ip.sa);
    ST(3) REINIT br(request, "numeric_host", &ip.sa);
  FP RE(verify(result, PARENT_SPECIFIED, "cat", 37) + verify(result, PARENT_SPECIFIED, "dog", 24), 3)
    ats_ip_pton(TEST_IP6_ADDR, &ip.sa);
    ST(4) REINIT br(request, "numeric_host", &ip.sa);
  FP RE(verify(result, PARENT_SPECIFIED, "zwoop", 37) + verify(result, PARENT_SPECIFIED, "jMCg", 24), 4)
    // Test 5
    ST(5) REINIT br(request, "www.pilot.net");
  FP RE(verify(result, PARENT_SPECIFIED, "pilot_net", 80), 5)
    // Test 6
    ST(6) REINIT br(request, "www.snoopy.net");
  const char *snoopy_dog = "http://www.snoopy.com/";
  request->hdr->url_set(snoopy_dog, strlen(snoopy_dog));
  FP RE(verify(result, PARENT_SPECIFIED, "odie", 80) + verify(result, PARENT_SPECIFIED, "garfield", 80), 5)
    // Test 7
    ST(7) REINIT br(request, "a.rabbit.i.am");
  FP RE(verify(result, PARENT_SPECIFIED, "amy", 80) +
        verify(result, PARENT_SPECIFIED, "katie", 80) + verify(result, PARENT_SPECIFIED, "carissa", 771), 6)
    // Test 6+ BUGBUG needs to be fixed
//   ST(7) REINIT
//   br(request, "www.microsoft.net");
//   FP RE( verify(result,PARENT_SPECIFIED,"zoo.net",341) +
//       verify(result,PARENT_SPECIFIED,"zoo.net",347) +
//       verify(result,PARENT_SPECIFIED,"zoo.edu",111) ,7)
    // Test 6++ BUGBUG needs to be fixed
//   ST(7) REINIT
//   br(request, "snow.imac.net:2020");
//   FP RE(verify(result,PARENT_DIRECT,0,0),7)
    // Test 6+++ BUGBUG needs to be fixed
//   ST(8) REINIT
//   br(request, "snow.imac.net:819");
//   URL* u = new URL();
//   char* r = "http://snow.imac.net:819/";
//   u->create(0);
//   u->parse(r,strlen(r));
//   u->port_set(819);
//   request->hdr->url_set(u);
//   ink_assert(request->hdr->url_get()->port_get() == 819);
//   printf("url: %s\n",request->hdr->url_get()->string_get(0));
//   FP RE(verify(result,PARENT_SPECIFIED,"genie",80),8)
    // Test 7 - N Parent Table
    tbl[0] = '\0';
  T("dest_domain=rabbit.net parent=fuzzy:80,fluffy:80,furry:80,frisky:80 round_robin=strict go_direct=true\n")
    REBUILD
    // Test 8
    ST(8) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 7)
    params->markParentDown(result);

  // Test 9
  ST(9) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 8)
    // Test 10
    ST(10) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "furry", 80), 9)
    // Test 11
    ST(11) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 10)
    // restart the loop
    // Test 12
    ST(12) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 11)
    // Test 13
    ST(13) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 12)
    // Test 14
    ST(14) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "furry", 80), 13)
    // Test 15
    ST(15) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 14)
    params->markParentDown(result);

  // restart the loop

  // Test 16
  ST(16) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 15)
    // Test 17
    ST(17) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 16)
    // Test 18
    ST(18) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "furry", 80), 17)
    // Test 19
    ST(19) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 18)
    // restart the loop
    // Test 20
    ST(20) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 19)
    // Test 21
    ST(21) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 20)
    // Test 22
    ST(22) REINIT br(request, "i.am.rabbit.net");
  FP RE(verify(result, PARENT_SPECIFIED, "furry", 80), 21)
    params->markParentDown(result);

  // Test 23 - 32
  for (i = 23; i < 33; i++) {
    ST(i) REINIT br(request, "i.am.rabbit.net");
    FP RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), i)
  }

  params->markParentDown(result);       // now they're all down

  // Test 33 - 132
  for (i = 33; i < 133; i++) {
    ST(i) REINIT br(request, "i.am.rabbit.net");
    FP RE(verify(result, PARENT_DIRECT, 0, 0), i)
  }

  // sleep(5); // parents should come back up; they don't
  sleep(params->ParentRetryTime + 1);

  // Fix: The following tests failed because
  // br() should set xact_start correctly instead of 0.

  // Test 133 - 172
  for (i = 133; i < 173; i++) {
    ST(i) REINIT br(request, "i.am.rabbit.net");
    FP sleep(1);
    switch (i % 4) {
    case 0:
      RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), i) break;
    case 1:
      RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), i) break;
    case 2:
      RE(verify(result, PARENT_SPECIFIED, "furry", 80), i) break;
    case 3:
      RE(verify(result, PARENT_SPECIFIED, "frisky", 80), i) break;
    default:
      ink_assert(0);
    }
  }
  delete request;
  delete result;

  printf("Tests Passed: %d\nTests Failed: %d\n", passes, fails);
  *pstatus = (!fails ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
}

// verify returns 1 iff the test passes
int
verify(ParentResult * r, ParentResultType e, const char *h, int p)
{
  if (is_debug_tag_set("parent_select"))
    show_result(r);
  return (r->r != e) ? 0 : ((e != PARENT_SPECIFIED) ? 1 : (strcmp(r->hostname, h) ? 0 : ((r->port == p) ? 1 : 0)));
}

// br creates an HttpRequestData object
void
br(HttpRequestData * h, const char *os_hostname, sockaddr const* dest_ip)
{
  h->hdr = new HTTPHdr();
  h->hdr->create(HTTP_TYPE_REQUEST);
  h->hostname_str = (char *)ats_strdup(os_hostname);
  h->xact_start = time(NULL);
  ink_zero(h->src_ip);
  ink_zero(h->dest_ip);
  ats_ip_copy(&h->dest_ip.sa, dest_ip);
  h->incoming_port = 80;
  h->api_info = new _HttpApiInfo();
}

// show_result prints out the ParentResult information
void
show_result(ParentResult * p)
{
  switch (p->r) {
  case PARENT_UNDEFINED:
    printf("result is PARENT_UNDEFINED\n");
    break;
  case PARENT_DIRECT:
    printf("result is PARENT_DIRECT\n");
    break;
  case PARENT_SPECIFIED:
    printf("result is PARENT_SPECIFIED\n");
    printf("hostname is %s\n", p->hostname);
    printf("port is %d\n", p->port);
    break;
  case PARENT_FAIL:
    printf("result is PARENT_FAIL\n");
    break;
  default:
    // Handled here:
    // PARENT_AGENT
    break;
  }
}
