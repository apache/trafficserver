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
#include "P_EventSystem.h"
#include "ParentSelection.h"
#include "ParentConsistentHash.h"
#include "ParentRoundRobin.h"
#include "ControlMatcher.h"
#include "ProxyConfig.h"
#include "HostStatus.h"
#include "HTTP.h"
#include "HttpTransact.h"
#include "I_Machine.h"

#define MAX_SIMPLE_RETRIES 5
#define MAX_UNAVAILABLE_SERVER_RETRIES 5

typedef ControlMatcher<ParentRecord, ParentResult> P_table;

// Global Vars for Parent Selection
static const char modulePrefix[]                             = "[ParentSelection]";
static ConfigUpdateHandler<ParentConfig> *parentConfigUpdate = nullptr;
static int self_detect                                       = 2;

// Config var names
static const char *file_var      = "proxy.config.http.parent_proxy.file";
static const char *default_var   = "proxy.config.http.parent_proxies";
static const char *retry_var     = "proxy.config.http.parent_proxy.retry_time";
static const char *threshold_var = "proxy.config.http.parent_proxy.fail_threshold";

static const char *ParentResultStr[] = {"PARENT_UNDEFINED", "PARENT_DIRECT", "PARENT_SPECIFIED", "PARENT_AGENT", "PARENT_FAIL"};

//
//  Config Callback Prototypes
//
enum ParentCB_t {
  PARENT_FILE_CB,
  PARENT_DEFAULT_CB,
  PARENT_RETRY_CB,
  PARENT_ENABLE_CB,
  PARENT_THRESHOLD_CB,
  PARENT_DNS_ONLY_CB,
};

ParentSelectionPolicy::ParentSelectionPolicy()
{
  int32_t retry_time     = 0;
  int32_t fail_threshold = 0;

  // Handle parent timeout
  REC_ReadConfigInteger(retry_time, retry_var);
  ParentRetryTime = retry_time;

  // Handle the fail threshold
  REC_ReadConfigInteger(fail_threshold, threshold_var);
  FailThreshold = fail_threshold;
}

ParentConfigParams::ParentConfigParams(P_table *_parent_table) : parent_table(_parent_table), DefaultParent(nullptr), policy()
{
  char *default_val = nullptr;

  // Handle default parent
  REC_ReadConfigStringAlloc(default_val, default_var);
  DefaultParent = createDefaultParent(default_val);
  ats_free(default_val);
}

ParentConfigParams::~ParentConfigParams()
{
  if (parent_table) {
    Debug("parent_select", "~ParentConfigParams(): releasing parent_table %p", parent_table);
  }
  delete parent_table;
  delete DefaultParent;
}

bool
ParentConfigParams::apiParentExists(HttpRequestData *rdata)
{
  return (rdata->api_info && rdata->api_info->parent_proxy_name != nullptr && rdata->api_info->parent_proxy_port > 0);
}

void
ParentConfigParams::findParent(HttpRequestData *rdata, ParentResult *result, unsigned int fail_threshold, unsigned int retry_time)
{
  P_table *tablePtr        = parent_table;
  ParentRecord *defaultPtr = DefaultParent;
  ParentRecord *rec;

  Debug("parent_select", "In ParentConfigParams::findParent(): parent_table: %p.", parent_table);
  ink_assert(result->result == PARENT_UNDEFINED);

  // Initialize the result structure
  result->reset();

  // Check to see if the parent was set through the
  //   api
  if (apiParentExists(rdata)) {
    result->result       = PARENT_SPECIFIED;
    result->hostname     = rdata->api_info->parent_proxy_name;
    result->port         = rdata->api_info->parent_proxy_port;
    result->rec          = extApiRecord;
    result->start_parent = 0;
    result->last_parent  = 0;

    Debug("parent_select", "Result for %s was API set parent %s:%d", rdata->get_host(), result->hostname, result->port);
    return;
  }

  tablePtr->Match(rdata, result);
  rec = result->rec;

  if (rec == nullptr) {
    // No parents were found
    //
    // If there is a default parent, use it
    if (defaultPtr != nullptr) {
      rec = result->rec = defaultPtr;
    } else {
      result->result = PARENT_DIRECT;
      Debug("parent_select", "Returning PARENT_DIRECT (no parents were found)");
      return;
    }
  }

  if (rec != extApiRecord) {
    selectParent(true, result, rdata, fail_threshold, retry_time);
  }

  const char *host = rdata->get_host();

  switch (result->result) {
  case PARENT_UNDEFINED:
    Debug("parent_select", "PARENT_UNDEFINED");
    Debug("parent_select", "Result for %s was %s", host, ParentResultStr[result->result]);
    break;
  case PARENT_FAIL:
    Debug("parent_select", "PARENT_FAIL");
    break;
  case PARENT_DIRECT:
    Debug("parent_select", "PARENT_DIRECT");
    Debug("parent_select", "Result for %s was %s", host, ParentResultStr[result->result]);
    break;
  case PARENT_SPECIFIED:
    Debug("parent_select", "PARENT_SPECIFIED");
    Debug("parent_select", "Result for %s was parent %s:%d", host, result->hostname, result->port);
    break;
  default:
    // Handled here:
    // PARENT_AGENT
    break;
  }
}

void
ParentConfigParams::nextParent(HttpRequestData *rdata, ParentResult *result, unsigned int fail_threshold, unsigned int retry_time)
{
  P_table *tablePtr = parent_table;

  Debug("parent_select", "ParentConfigParams::nextParent(): parent_table: %p, result->rec: %p", parent_table, result->rec);

  //  Make sure that we are being called back with a
  //   result structure with a parent
  ink_assert(result->result == PARENT_SPECIFIED);
  if (result->result != PARENT_SPECIFIED) {
    result->result = PARENT_FAIL;
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->is_api_result()) {
    Debug("parent_select", "Retry result for %s was %s", rdata->get_host(), ParentResultStr[result->result]);
    result->result = PARENT_FAIL;
    return;
  }
  Debug("parent_select", "ParentConfigParams::nextParent(): result->r: %d, tablePtr: %p", result->result, tablePtr);

  // Find the next parent in the array
  Debug("parent_select", "Calling selectParent() from nextParent");
  selectParent(false, result, rdata, fail_threshold, retry_time);

  const char *host = rdata->get_host();

  switch (result->result) {
  case PARENT_UNDEFINED:
    Debug("parent_select", "PARENT_UNDEFINED");
    Debug("parent_select", "Retry result for %s was %s", host, ParentResultStr[result->result]);
    break;
  case PARENT_FAIL:
    Debug("parent_select", "PARENT_FAIL");
    Debug("parent_select", "Retry result for %s was %s", host, ParentResultStr[result->result]);
    break;
  case PARENT_DIRECT:
    Debug("parent_select", "PARENT_DIRECT");
    Debug("parent_select", "Retry result for %s was %s", host, ParentResultStr[result->result]);
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

bool
ParentConfigParams::parentExists(HttpRequestData *rdata)
{
  unsigned int fail_threshold = policy.FailThreshold;
  unsigned int retry_time     = policy.ParentRetryTime;
  ParentResult result;

  findParent(rdata, &result, fail_threshold, retry_time);

  if (result.result == PARENT_SPECIFIED) {
    return true;
  } else {
    return false;
  }
}

int ParentConfig::m_id = 0;

void
ParentConfig::startup()
{
  parentConfigUpdate = new ConfigUpdateHandler<ParentConfig>();

  // Load the initial configuration
  reconfigure();

  // Setup the callbacks for reconfiuration
  //   parent table
  parentConfigUpdate->attach(file_var);
  //   default parent
  parentConfigUpdate->attach(default_var);
  //   Retry time
  parentConfigUpdate->attach(retry_var);
  //   Fail Threshold
  parentConfigUpdate->attach(threshold_var);
}

void
ParentConfig::reconfigure()
{
  Note("parent.config loading ...");

  ParentConfigParams *params = nullptr;

  // Allocate parent table
  P_table *pTable = new P_table(file_var, modulePrefix, &http_dest_tags);

  params = new ParentConfigParams(pTable);
  ink_assert(params != nullptr);

  m_id = configProcessor.set(m_id, params);

  if (is_debug_tag_set("parent_config")) {
    ParentConfig::print();
  }

  Note("parent.config finished loading");
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
  printf("\tRetryTime %d\n", params->policy.ParentRetryTime);
  if (params->DefaultParent == nullptr) {
    printf("\tNo Default Parent\n");
  } else {
    printf("\tDefault Parent:\n");
    params->DefaultParent->Print();
  }
  printf("  ");
  params->parent_table->Print();

  ParentConfig::release(params);
}

UnavailableServerResponseCodes::UnavailableServerResponseCodes(char *val)
{
  Tokenizer pTok(", \t\r");
  int numTok = 0, c;

  if (val == nullptr) {
    Warning("UnavailableServerResponseCodes - unavailable_server_retry_responses is null loading default 503 code.");
    codes.push_back(HTTP_STATUS_SERVICE_UNAVAILABLE);
    return;
  }
  numTok = pTok.Initialize(val, SHARE_TOKS);
  if (numTok == 0) {
    c = atoi(val);
    if (c > 500 && c < 600) {
      codes.push_back(HTTP_STATUS_SERVICE_UNAVAILABLE);
    }
  }
  for (int i = 0; i < numTok; i++) {
    c = atoi(pTok[i]);
    if (c > 500 && c < 600) {
      Debug("parent_select", "loading response code: %d", c);
      codes.push_back(c);
    }
  }
  std::sort(codes.begin(), codes.end());
}

void
ParentRecord::PreProcessParents(const char *val, const int line_num, char *buf, size_t len)
{
  char *_val                      = ats_strndup(val, strlen(val));
  char fqdn[TS_MAX_HOST_NAME_LEN] = {0}, *nm, *token, *savePtr;
  std::string str;
  Machine *machine                   = Machine::instance();
  constexpr char PARENT_DELIMITERS[] = ";, ";
  HostStatus &hs                     = HostStatus::instance();

  token = strtok_r(_val, PARENT_DELIMITERS, &savePtr);
  while (token != nullptr) {
    if ((nm = strchr(token, ':')) != nullptr) {
      size_t len = (nm - token);
      ink_assert(len < sizeof(fqdn));
      memset(fqdn, 0, sizeof(fqdn));
      strncpy(fqdn, token, len);
      if (self_detect && machine->is_self(fqdn)) {
        if (self_detect == 1) {
          Debug("parent_select", "token: %s, matches this machine.  Removing self from parent list at line %d", fqdn, line_num);
          token = strtok_r(nullptr, PARENT_DELIMITERS, &savePtr);
          continue;
        } else {
          Debug("parent_select", "token: %s, matches this machine.  Marking down self from parent list at line %d", fqdn, line_num);
          hs.setHostStatus(fqdn, HostStatus_t::HOST_STATUS_DOWN, 0, Reason::SELF_DETECT);
        }
      }
    } else {
      if (self_detect && machine->is_self(token)) {
        if (self_detect == 1) {
          Debug("parent_select", "token: %s, matches this machine.  Removing self from parent list at line %d", token, line_num);
          token = strtok_r(nullptr, PARENT_DELIMITERS, &savePtr);
          continue;
        } else {
          Debug("parent_select", "token: %s, matches this machine.  Marking down self from parent list at line %d", token,
                line_num);
          hs.setHostStatus(token, HostStatus_t::HOST_STATUS_DOWN, 0, Reason::SELF_DETECT);
        }
      }
    }

    str += token;
    str += ";";
    token = strtok_r(nullptr, PARENT_DELIMITERS, &savePtr);
  }
  strncpy(buf, str.c_str(), len);
  ats_free(_val);
}

// const char* ParentRecord::ProcessParents(char* val, bool isPrimary)
//
//   Reads in the value of a "round-robin" or "order"
//     directive and parses out the individual parents
//     allocates and builds the this->parents array or
//     this->secondary_parents based upon the isPrimary
//     boolean.
//
//   Returns NULL on success and a static error string
//     on failure
//
const char *
ParentRecord::ProcessParents(char *val, bool isPrimary)
{
  Tokenizer pTok(",; \t\r");
  int numTok          = 0;
  const char *current = nullptr;
  int port            = 0;
  char *tmp = nullptr, *tmp2 = nullptr, *tmp3 = nullptr;
  const char *errPtr = nullptr;
  float weight       = 1.0;

  if (parents != nullptr && isPrimary == true) {
    return "Can not specify more than one set of parents";
  }
  if (secondary_parents != nullptr && isPrimary == false) {
    return "Can not specify more than one set of secondary parents";
  }

  numTok = pTok.Initialize(val, SHARE_TOKS);

  if (numTok == 0) {
    return "No parents specified";
  }
  HostStatus &hs = HostStatus::instance();
  // Allocate the parents array
  if (isPrimary) {
    this->parents = (pRecord *)ats_malloc(sizeof(pRecord) * numTok);
  } else {
    this->secondary_parents = (pRecord *)ats_malloc(sizeof(pRecord) * numTok);
  }

  // Loop through the set of parents specified
  //
  for (int i = 0; i < numTok; i++) {
    current = pTok[i];

    // Find the parent port
    tmp = (char *)strchr(current, ':');

    if (tmp == nullptr) {
      errPtr = "No parent port specified";
      goto MERROR;
    }
    // Read the parent port
    // coverity[secure_coding]
    if (sscanf(tmp + 1, "%d", &port) != 1) {
      errPtr = "Malformed parent port";
      goto MERROR;
    }

    // See if there is an optional parent weight
    tmp2 = (char *)strchr(current, '|');

    if (tmp2) {
      if (sscanf(tmp2 + 1, "%f", &weight) != 1) {
        errPtr = "Malformed parent weight";
        goto MERROR;
      }
    }

    tmp3 = (char *)strchr(current, '&');

    // Make sure that is no garbage beyond the parent
    //  port or weight
    if (!tmp3) {
      char *scan;
      if (tmp2) {
        scan = tmp2 + 1;
      } else {
        scan = tmp + 1;
      }
      for (; *scan != '\0' && (ParseRules::is_digit(*scan) || *scan == '.'); scan++) {
        ;
      }
      for (; *scan != '\0' && ParseRules::is_wslfcr(*scan); scan++) {
        ;
      }
      if (*scan != '\0') {
        errPtr = "Garbage trailing entry or invalid separator";
        goto MERROR;
      }
    }
    // Check to make sure that the string will fit in the
    //  pRecord
    if (tmp - current > MAXDNAME) {
      errPtr = "Parent hostname is too long";
      goto MERROR;
    } else if (tmp - current == 0) {
      errPtr = "Parent string is empty";
      goto MERROR;
    }
    // Update the pRecords
    if (isPrimary) {
      memcpy(this->parents[i].hostname, current, tmp - current);
      this->parents[i].hostname[tmp - current] = '\0';
      this->parents[i].port                    = port;
      this->parents[i].failedAt                = 0;
      this->parents[i].failCount               = 0;
      this->parents[i].scheme                  = scheme;
      this->parents[i].idx                     = i;
      this->parents[i].name                    = this->parents[i].hostname;
      this->parents[i].available               = true;
      this->parents[i].weight                  = weight;
      if (tmp3) {
        memcpy(this->parents[i].hash_string, tmp3 + 1, strlen(tmp3));
        this->parents[i].name = this->parents[i].hash_string;
      }
      if (hs.getHostStatus(this->parents[i].hostname) == HostStatus_t::HOST_STATUS_INIT) {
        hs.setHostStatus(this->parents[i].hostname, HOST_STATUS_UP, 0, Reason::MANUAL);
      }
    } else {
      memcpy(this->secondary_parents[i].hostname, current, tmp - current);
      this->secondary_parents[i].hostname[tmp - current] = '\0';
      this->secondary_parents[i].port                    = port;
      this->secondary_parents[i].failedAt                = 0;
      this->secondary_parents[i].failCount               = 0;
      this->secondary_parents[i].scheme                  = scheme;
      this->secondary_parents[i].idx                     = i;
      this->secondary_parents[i].name                    = this->secondary_parents[i].hostname;
      this->secondary_parents[i].available               = true;
      this->secondary_parents[i].weight                  = weight;
      if (tmp3) {
        memcpy(this->secondary_parents[i].hash_string, tmp3 + 1, strlen(tmp3));
        this->secondary_parents[i].name = this->secondary_parents[i].hash_string;
      }
      if (hs.getHostStatus(this->secondary_parents[i].hostname) == HostStatus_t::HOST_STATUS_INIT) {
        hs.setHostStatus(this->secondary_parents[i].hostname, HOST_STATUS_UP, 0, Reason::MANUAL);
      }
    }
    tmp3 = nullptr;
  }

  if (isPrimary) {
    num_parents = numTok;
  } else {
    num_secondary_parents = numTok;
  }

  return nullptr;

MERROR:
  if (isPrimary) {
    ats_free(parents);
    parents = nullptr;
  } else {
    ats_free(secondary_parents);
    secondary_parents = nullptr;
  }

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

  this->go_direct       = true;
  this->ignore_query    = false;
  this->scheme          = nullptr;
  this->parent_is_proxy = true;
  errPtr                = ProcessParents(val, true);

  if (errPtr != nullptr) {
    errBuf = (char *)ats_malloc(1024);
    snprintf(errBuf, 1024, "%s %s for default parent proxy", modulePrefix, errPtr);
    SignalError(errBuf, alarmAlready);
    ats_free(errBuf);
    return false;
  } else {
    ParentRR_t round_robin = P_NO_ROUND_ROBIN;
    Debug("parent_select", "allocating ParentRoundRobin() lookup strategy.");
    selection_strategy = new ParentRoundRobin(this, round_robin);
    return true;
  }
}

// Result ParentRecord::Init(matcher_line* line_info)
//
//    matcher_line* line_info - contains parsed label/value
//      pairs of the current cache.config line
//
//    Returns NULL if everything is OK
//      Otherwise, returns an error string that the caller MUST
//        DEALLOCATE with ats_free()
//
Result
ParentRecord::Init(matcher_line *line_info)
{
  const char *errPtr = nullptr;
  const char *tmp;
  char *label;
  char *val;
  char parent_buf[16384] = {0};
  bool used              = false;
  ParentRR_t round_robin = P_NO_ROUND_ROBIN;
  char buf[128];
  RecInt rec_self_detect = 2;

  this->line_num = line_info->line_num;
  this->scheme   = nullptr;

  if (RecGetRecordInt("proxy.config.http.parent_proxy.self_detect", &rec_self_detect) == REC_ERR_OKAY) {
    self_detect = static_cast<int>(rec_self_detect);
  }

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    used  = false;
    label = line_info->line[0][i];
    val   = line_info->line[1][i];

    if (label == nullptr) {
      continue;
    }

    if (strcasecmp(label, "round_robin") == 0) {
      if (strcasecmp(val, "true") == 0) {
        round_robin = P_HASH_ROUND_ROBIN;
      } else if (strcasecmp(val, "strict") == 0) {
        round_robin = P_STRICT_ROUND_ROBIN;
      } else if (strcasecmp(val, "false") == 0) {
        round_robin = P_NO_ROUND_ROBIN;
      } else if (strcasecmp(val, "consistent_hash") == 0) {
        round_robin = P_CONSISTENT_HASH;
      } else if (strcasecmp(val, "latched") == 0) {
        round_robin = P_LATCHED_ROUND_ROBIN;
      } else {
        round_robin = P_NO_ROUND_ROBIN;
        errPtr      = "invalid argument to round_robin directive";
      }
      used = true;
    } else if (strcasecmp(label, "parent") == 0 || strcasecmp(label, "primary_parent") == 0) {
      PreProcessParents(val, line_num, parent_buf, sizeof(parent_buf) - 1);
      errPtr = ProcessParents(parent_buf, true);
      used   = true;
    } else if (strcasecmp(label, "secondary_parent") == 0) {
      PreProcessParents(val, line_num, parent_buf, sizeof(parent_buf) - 1);
      errPtr = ProcessParents(parent_buf, false);
      used   = true;
    } else if (strcasecmp(label, "go_direct") == 0) {
      if (strcasecmp(val, "false") == 0) {
        go_direct = false;
      } else if (strcasecmp(val, "true") != 0) {
        errPtr = "invalid argument to go_direct directive";
      } else {
        go_direct = true;
      }
      used = true;
    } else if (strcasecmp(label, "qstring") == 0) {
      // qstring=ignore | consider
      if (strcasecmp(val, "ignore") == 0) {
        this->ignore_query = true;
      } else {
        this->ignore_query = false;
      }
      used = true;
    } else if (strcasecmp(label, "parent_is_proxy") == 0) {
      if (strcasecmp(val, "false") == 0) {
        parent_is_proxy = false;
      } else if (strcasecmp(val, "true") != 0) {
        errPtr = "invalid argument to parent_is_proxy directive";
      } else {
        parent_is_proxy = true;
      }
      used = true;
    } else if (strcasecmp(label, "parent_retry") == 0) {
      if (strcasecmp(val, "simple_retry") == 0) {
        parent_retry = PARENT_RETRY_SIMPLE;
      } else if (strcasecmp(val, "unavailable_server_retry") == 0) {
        parent_retry = PARENT_RETRY_UNAVAILABLE_SERVER;
      } else if (strcasecmp(val, "both") == 0) {
        parent_retry = PARENT_RETRY_BOTH;
      } else {
        errPtr = "invalid argument to parent_retry directive.";
      }
      used = true;
    } else if (strcasecmp(label, "unavailable_server_retry_responses") == 0 && unavailable_server_retry_responses == nullptr) {
      unavailable_server_retry_responses = new UnavailableServerResponseCodes(val);
      used                               = true;
    } else if (strcasecmp(label, "max_simple_retries") == 0) {
      int v = atoi(val);
      if (v >= 1 && v < MAX_SIMPLE_RETRIES) {
        max_simple_retries = v;
        used               = true;
      } else {
        snprintf(buf, sizeof(buf), "invalid argument to max_simple_retries.  Argument must be between 1 and %d.",
                 MAX_SIMPLE_RETRIES);
        errPtr = buf;
      }
    } else if (strcasecmp(label, "max_unavailable_server_retries") == 0) {
      int v = atoi(val);
      if (v >= 1 && v < MAX_UNAVAILABLE_SERVER_RETRIES) {
        max_unavailable_server_retries = v;
        used                           = true;
      } else {
        snprintf(buf, sizeof(buf), "invalid argument to max_unavailable_server_retries.  Argument must be between 1 and %d.",
                 MAX_UNAVAILABLE_SERVER_RETRIES);
        errPtr = buf;
      }
    } else if (strcasecmp(label, "secondary_mode") == 0) {
      int v          = atoi(val);
      secondary_mode = v;
      used           = true;
    }
    // Report errors generated by ProcessParents();
    if (errPtr != nullptr) {
      return Result::failure("%s %s at line %d", modulePrefix, errPtr, line_num);
    }

    if (used == true) {
      // Consume the label/value pair we used
      line_info->line[0][i] = nullptr;
      line_info->num_el--;
    }
  }

  // delete unavailable_server_retry_responses if unavailable_server_retry is not enabled.
  if (unavailable_server_retry_responses != nullptr && !(parent_retry & PARENT_RETRY_UNAVAILABLE_SERVER)) {
    Warning("%s ignoring unavailable_server_retry_responses directive on line %d, as unavailable_server_retry is not enabled.",
            modulePrefix, line_num);
    delete unavailable_server_retry_responses;
    unavailable_server_retry_responses = nullptr;
  } else if (unavailable_server_retry_responses == nullptr && (parent_retry & PARENT_RETRY_UNAVAILABLE_SERVER)) {
    // initialize UnavailableServerResponseCodes to the default value if unavailable_server_retry is enabled.
    Warning("%s initializing UnavailableServerResponseCodes on line %d to 503 default.", modulePrefix, line_num);
    unavailable_server_retry_responses = new UnavailableServerResponseCodes(nullptr);
  }

  if (this->parents == nullptr && go_direct == false) {
    return Result::failure("%s No parent specified in parent.config at line %d", modulePrefix, line_num);
  }
  // Process any modifiers to the directive, if they exist
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != nullptr) {
      return Result::failure("%s %s at line %d in parent.config", modulePrefix, tmp, line_num);
    }
    // record SCHEME modifier if present.
    // NULL if not present
    this->scheme = this->getSchemeModText();
    if (this->scheme != nullptr) {
      // update parent entries' schemes
      for (int j = 0; j < num_parents; j++) {
        this->parents[j].scheme = this->scheme;
      }
    }
  }

  switch (round_robin) {
  // ParentRecord.round_robin defaults to P_NO_ROUND_ROBIN when round_robin
  // is not set in parent.config.  Therefore ParentRoundRobin is the default
  // strategy.  If setting go_direct to true, there should be no parent list
  // in parent.config and ParentRoundRobin::lookup will set parent_result->r
  // to PARENT_DIRECT.
  case P_NO_ROUND_ROBIN:
  case P_STRICT_ROUND_ROBIN:
  case P_HASH_ROUND_ROBIN:
  case P_LATCHED_ROUND_ROBIN:
    Debug("parent_select", "allocating ParentRoundRobin() lookup strategy.");
    selection_strategy = new ParentRoundRobin(this, round_robin);
    break;
  case P_CONSISTENT_HASH:
    Debug("parent_select", "allocating ParentConsistentHash() lookup strategy.");
    selection_strategy = new ParentConsistentHash(this);
    break;
  default:
    ink_release_assert(0);
  }

  return Result::ok();
}

// void ParentRecord::UpdateMatch(ParentResult* result, RequestData* rdata);
//
//    Updates the record ptr in result if the this element
//     appears later in the file
//
void
ParentRecord::UpdateMatch(ParentResult *result, RequestData *rdata)
{
  if (this->CheckForMatch((HttpRequestData *)rdata, result->line_number) == true) {
    result->rec         = this;
    result->line_number = this->line_num;

    Debug("parent_select", "Matched with %p parent node from line %d", this, this->line_num);
  }
}

ParentRecord::~ParentRecord()
{
  ats_free(parents);
  ats_free(secondary_parents);
  delete selection_strategy;
  delete unavailable_server_retry_responses;
}

void
ParentRecord::Print()
{
  printf("\t\t");
  for (int i = 0; i < num_parents; i++) {
    printf(" %s:%d|%f&%s ", parents[i].hostname, parents[i].port, parents[i].weight, parents[i].name);
  }
  printf(" direct=%s\n", (go_direct == true) ? "true" : "false");
  printf(" parent_is_proxy=%s\n", (parent_is_proxy == true) ? "true" : "false");
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

  if (val == nullptr || *val == '\0') {
    return nullptr;
  }

  newRec = new ParentRecord;
  if (newRec->DefaultInit(val) == true) {
    return newRec;
  } else {
    delete newRec;
    return nullptr;
  }
}

//
// ParentConfig equivalent functions for SocksServerConfig
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
setup_socks_servers(ParentRecord *rec_arr, int len)
{
  /* This changes hostnames into ip addresses and sets go_direct to false */
  for (int j = 0; j < len; j++) {
    rec_arr[j].go_direct = false;

    pRecord *pr   = rec_arr[j].parents;
    int n_parents = rec_arr[j].num_parents;

    for (int i = 0; i < n_parents; i++) {
      IpEndpoint ip4, ip6;
      if (0 == ats_ip_getbestaddrinfo(pr[i].hostname, &ip4, &ip6)) {
        IpEndpoint *ip = ats_is_ip6(&ip6) ? &ip6 : &ip4;
        ats_ip_ntop(ip, pr[i].hostname, MAXDNAME + 1);
      } else {
        Warning("Could not resolve socks server name \"%s\". "
                "Please correct it",
                pr[i].hostname);
        snprintf(pr[i].hostname, MAXDNAME + 1, "255.255.255.255");
      }
    }
  }

  return 0;
}

void
SocksServerConfig::reconfigure()
{
  Note("socks.config loading ...");

  char *default_val = nullptr;
  int retry_time    = 30;
  int fail_threshold;

  ParentConfigParams *params = nullptr;

  // Allocate parent table
  P_table *pTable = new P_table("proxy.config.socks.socks_config_file", "[Socks Server Selection]", &socks_server_tags);

  params = new ParentConfigParams(pTable);
  ink_assert(params != nullptr);

  // Handle default parent
  REC_ReadConfigStringAlloc(default_val, "proxy.config.socks.default_servers");
  params->DefaultParent = createDefaultParent(default_val);
  ats_free(default_val);

  if (params->DefaultParent) {
    setup_socks_servers(params->DefaultParent, 1);
  }
  if (params->parent_table->ipMatch) {
    setup_socks_servers(params->parent_table->ipMatch->data_array, params->parent_table->ipMatch->array_len);
  }

  // Handle parent timeout
  REC_ReadConfigInteger(retry_time, "proxy.config.socks.server_retry_time");
  params->policy.ParentRetryTime = retry_time;

  // Handle the fail threshold
  REC_ReadConfigInteger(fail_threshold, "proxy.config.socks.server_fail_threshold");
  params->policy.FailThreshold = fail_threshold;

  m_id = configProcessor.set(m_id, params);

  if (is_debug_tag_set("parent_config")) {
    SocksServerConfig::print();
  }

  Note("socks.config finished loading");
}

void
SocksServerConfig::print()
{
  ParentConfigParams *params = SocksServerConfig::acquire();

  printf("Parent Selection Config for Socks Server\n");
  printf("\tRetryTime %d\n", params->policy.ParentRetryTime);
  if (params->DefaultParent == nullptr) {
    printf("\tNo Default Parent\n");
  } else {
    printf("\tDefault Parent:\n");
    params->DefaultParent->Print();
  }
  printf("  ");
  params->parent_table->Print();

  SocksServerConfig::release(params);
}

#define TEST_FAIL(str)                \
  {                                   \
    printf("%d: %s\n", test_id, str); \
    err = REGRESSION_TEST_FAILED;     \
  }

void
request_to_data(HttpRequestData *req, sockaddr const *srcip, sockaddr const *dstip, const char *str)
{
  HTTPParser parser;

  ink_zero(req->src_ip);
  ats_ip_copy(&req->src_ip.sa, srcip);
  ink_zero(req->dest_ip);
  ats_ip_copy(&req->dest_ip.sa, dstip);

  req->hdr = new HTTPHdr;

  http_parser_init(&parser);

  req->hdr->parse_req(&parser, &str, str + strlen(str), true);

  http_parser_clear(&parser);
}

static int passes;
static int fails;

// Parenting Tests
EXCLUSIVE_REGRESSION_TEST(PARENTSELECTION)(RegressionTest * /* t ATS_UNUSED */, int /* intensity_level ATS_UNUSED */, int *pstatus)
{
  // first, set everything up
  *pstatus = REGRESSION_TEST_INPROGRESS;
  ParentConfig config;
  P_table *ParentTable       = nullptr;
  ParentConfigParams *params = nullptr;
  passes = fails = 0;
  config.startup();
  char tbl[2048];
  HttpRequestData *request    = nullptr;
  ParentResult *result        = nullptr;
  unsigned int fail_threshold = 1;
  unsigned int retry_time     = 5;

#define T(x)                          \
  do {                                \
    ink_strlcat(tbl, x, sizeof(tbl)); \
  } while (0)

#define REBUILD                                                                                                            \
  do {                                                                                                                     \
    delete params;                                                                                                         \
    ParentTable = new P_table("", "ParentSelection Unit Test Table", &http_dest_tags,                                      \
                              ALLOW_HOST_TABLE | ALLOW_REGEX_TABLE | ALLOW_URL_TABLE | ALLOW_IP_TABLE | DONT_BUILD_TABLE); \
    ParentTable->BuildTableFromString(tbl);                                                                                \
    RecSetRecordInt("proxy.config.http.parent_proxy.fail_threshold", fail_threshold, REC_SOURCE_DEFAULT);                  \
    RecSetRecordInt("proxy.config.http.parent_proxy.retry_time", retry_time, REC_SOURCE_DEFAULT);                          \
    params = new ParentConfigParams(ParentTable);                                                                          \
  } while (0)

#define REINIT                             \
  do {                                     \
    if (request != NULL) {                 \
      delete request->hdr;                 \
      ats_free(request->hostname_str);     \
      delete request->api_info;            \
    }                                      \
    delete request;                        \
    delete result;                         \
    request = new HttpRequestData();       \
    result  = new ParentResult();          \
    if (!result || !request) {             \
      (void)printf("Allocation failed\n"); \
      return;                              \
    }                                      \
  } while (0)

#define ST(x)                                    \
  do {                                           \
    printf("*** TEST %d *** STARTING ***\n", x); \
  } while (0)

#define RE(x, y)                                                       \
  do {                                                                 \
    if (x) {                                                           \
      printf("*** TEST %d *** PASSED ***\n", y);                       \
      passes++;                                                        \
    } else {                                                           \
      printf("*** TEST %d *** FAILED *** FAILED *** FAILED ***\n", y); \
      fails++;                                                         \
    }                                                                  \
  } while (0)

#define FP                                                           \
  do {                                                               \
    params->findParent(request, result, fail_threshold, retry_time); \
  } while (0)

  // Test 1
  tbl[0] = '\0';
  ST(1);
  T("dest_domain=. parent=red:37412,orange:37412,yellow:37412 round_robin=strict\n");
  REBUILD;
  int c, red = 0, orange = 0, yellow = 0;
  for (c = 0; c < 21; c++) {
    REINIT;
    br(request, "fruit_basket.net");
    FP;
    red += verify(result, PARENT_SPECIFIED, "red", 37412);
    orange += verify(result, PARENT_SPECIFIED, "orange", 37412);
    yellow += verify(result, PARENT_SPECIFIED, "yellow", 37412);
  }
  RE(((red == 7) && (orange == 7) && (yellow == 7)), 1);
  // Test 2
  ST(2);
  tbl[0] = '\0';
  T("dest_domain=. parent=green:4325,blue:4325,indigo:4325,violet:4325 round_robin=false\n");
  REBUILD;
  int g = 0, b = 0, i = 0, v = 0;
  for (c = 0; c < 17; c++) {
    REINIT;
    br(request, "fruit_basket.net");
    FP;
    g += verify(result, PARENT_SPECIFIED, "green", 4325);
    b += verify(result, PARENT_SPECIFIED, "blue", 4325);
    i += verify(result, PARENT_SPECIFIED, "indigo", 4325);
    v += verify(result, PARENT_SPECIFIED, "violet", 4325);
  }
  RE((((g == 17) && !b && !i && !v) || (!g && (b == 17) && !i && !v) || (!g && !b && (i == 17) && !v) ||
      (!g && !b && !i && (v == 17))),
     2);
  // Test 3 - 6 Parenting Table
  tbl[0] = '\0';
#define TEST_IP4_ADDR "209.131.62.14"
#define TEST_IP6_ADDR "BEEF:DEAD:ABBA:CAFE:1337:1E1F:5EED:C0FF"
  T("dest_ip=" TEST_IP4_ADDR " parent=cat:37,dog:24 round_robin=strict\n");             /* L1 */
  T("dest_ip=" TEST_IP6_ADDR " parent=zwoop:37,jMCg:24 round_robin=strict\n");          /* L1 */
  T("dest_host=www.pilot.net parent=pilot_net:80\n");                                   /* L2 */
  T("url_regex=snoopy parent=odie:80,garfield:80 round_robin=true\n");                  /* L3 */
  T("dest_domain=i.am parent=amy:80,katie:80,carissa:771 round_robin=false\n");         /* L4 */
  T("dest_domain=microsoft.net time=03:00-22:10 parent=zoo.net:341\n");                 /* L5 */
  T("dest_domain=microsoft.net time=0:00-02:59 parent=zoo.net:347\n");                  /* L6 */
  T("dest_domain=microsoft.net time=22:11-23:59 parent=zoo.edu:111\n");                 /* L7 */
  T("dest_domain=imac.net port=819 parent=genie:80 round_robin=strict\n");              /* L8 */
  T("dest_ip=172.34.61.211 port=3142 parent=orangina:80 go_direct=false\n");            /* L9 */
  T("url_regex=miffy prefix=furry/rabbit parent=nintje:80 go_direct=false\n");          /* L10 */
  T("url_regex=kitty suffix=tif parent=hello:80 round_robin=strict go_direct=false\n"); /* L11 */
  T("url_regex=cyclops method=get parent=turkey:80\n");                                 /* L12 */
  T("url_regex=cyclops method=post parent=club:80\n");                                  /* L13 */
  T("url_regex=cyclops method=put parent=sandwich:80\n");                               /* L14 */
  T("url_regex=cyclops method=trace parent=mayo:80\n");                                 /* L15 */
  T("dest_host=pluto scheme=HTTP parent=strategy:80\n");                                /* L16 */
  REBUILD;
  // Test 3
  IpEndpoint ip;
  ats_ip_pton(TEST_IP4_ADDR, &ip.sa);
  ST(3);
  REINIT;
  br(request, "numeric_host", &ip.sa);
  FP;
  RE(verify(result, PARENT_SPECIFIED, "cat", 37) + verify(result, PARENT_SPECIFIED, "dog", 24), 3);
  ats_ip_pton(TEST_IP6_ADDR, &ip.sa);
  ST(4);
  REINIT;
  br(request, "numeric_host", &ip.sa);
  FP;
  RE(verify(result, PARENT_SPECIFIED, "zwoop", 37) + verify(result, PARENT_SPECIFIED, "jMCg", 24), 4);
  // Test 5
  ST(5);
  REINIT;
  br(request, "www.pilot.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "pilot_net", 80), 5);
  // Test 6
  ST(6);
  REINIT;
  br(request, "www.snoopy.net");
  const char *snoopy_dog = "http://www.snoopy.com/";
  request->hdr->url_set(snoopy_dog, strlen(snoopy_dog));
  FP;
  RE(verify(result, PARENT_SPECIFIED, "odie", 80) + verify(result, PARENT_SPECIFIED, "garfield", 80), 5);
  // Test 7
  ST(7);
  REINIT;
  br(request, "a.rabbit.i.am");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "amy", 80) + verify(result, PARENT_SPECIFIED, "katie", 80) +
       verify(result, PARENT_SPECIFIED, "carissa", 771),
     6);
  // Test 6+ BUGBUG needs to be fixed
  //   ST(7); REINIT;
  //   br(request, "www.microsoft.net");
  //   FP; RE( verify(result,PARENT_SPECIFIED,"zoo.net",341) +
  //       verify(result,PARENT_SPECIFIED,"zoo.net",347) +
  //       verify(result,PARENT_SPECIFIED,"zoo.edu",111) ,7);
  // Test 6++ BUGBUG needs to be fixed
  //   ST(7); REINIT;
  //   br(request, "snow.imac.net:2020");
  //   FP; RE(verify(result,PARENT_DIRECT,0,0),7);
  // Test 6+++ BUGBUG needs to be fixed
  //   ST(8); REINIT;
  //   br(request, "snow.imac.net:819");
  //   URL* u = new URL();
  //   char* r = "http://snow.imac.net:819/";
  //   u->create(0);
  //   u->parse(r,strlen(r));
  //   u->port_set(819);
  //   request->hdr->url_set(u);
  //   ink_assert(request->hdr->url_get()->port_get() == 819);
  //   printf("url: %s\n",request->hdr->url_get()->string_get(0));
  //   FP; RE(verify(result,PARENT_SPECIFIED,"genie",80),8);
  // Test 7 - N Parent Table
  tbl[0] = '\0';
  T("dest_domain=rabbit.net parent=fuzzy:80,fluffy:80,furry:80,frisky:80 round_robin=strict go_direct=true\n");
  REBUILD;
  // Test 8
  ST(8);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 7);
  params->markParentDown(result, fail_threshold, retry_time);

  // Test 9
  ST(9);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 8);
  // Test 10
  ST(10);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 9);
  // Test 11
  ST(11);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 10);
  // restart the loop
  // Test 12
  ST(12);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 11);
  // Test 13
  ST(13);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 12);
  // Test 14
  ST(14);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 13);
  // Test 15
  ST(15);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 14);
  params->markParentDown(result, fail_threshold, retry_time);

  // restart the loop

  // Test 16
  ST(16);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 15);
  // Test 17
  ST(17);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 16);
  // Test 18
  ST(18);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 17);
  // Test 19
  ST(19);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 18);
  // restart the loop
  // Test 20
  ST(20);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 19);
  // Test 21
  ST(21);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 20);
  // Test 22
  ST(22);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 21);
  params->markParentDown(result, fail_threshold, retry_time);

  // Test 23 - 32
  for (i = 23; i < 33; i++) {
    ST(i);
    REINIT;
    br(request, "i.am.rabbit.net");
    FP;
    RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), i);
  }

  params->markParentDown(result, 1, 5); // now they're all down

  // Test 33 - 132
  for (i = 33; i < 133; i++) {
    ST(i);
    REINIT;
    br(request, "i.am.rabbit.net");
    FP;
    RE(verify(result, PARENT_DIRECT, nullptr, 0), i);
  }

  // sleep(5); // parents should come back up; they don't
  sleep(params->policy.ParentRetryTime + 1);

  // Fix: The following tests failed because
  // br() should set xact_start correctly instead of 0.

  // Test 133 - 172
  for (i = 133; i < 173; i++) {
    ST(i);
    REINIT;
    br(request, "i.am.rabbit.net");
    FP;
    sleep(1);
    switch (i % 4) {
    case 0:
      RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), i);
      break;
    case 1:
      RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), i);
      break;
    case 2:
      RE(verify(result, PARENT_SPECIFIED, "furry", 80), i);
      break;
    case 3:
      RE(verify(result, PARENT_SPECIFIED, "frisky", 80), i);
      break;
    default:
      ink_assert(0);
    }
  }

  // Test 173
  tbl[0] = '\0';
  ST(173);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0 secondary_parent=furry:80|1.0;frisky:80|1.0 "
    "round_robin=consistent_hash go_direct=false\n");
  REBUILD;
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 173);
  params->markParentDown(result, fail_threshold, retry_time); // fuzzy is down.

  // Test 174
  ST(174);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 174);

  params->markParentDown(result, fail_threshold, retry_time); // frisky is down.

  // Test 175
  ST(175);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 175);

  params->markParentDown(result, fail_threshold, retry_time); // frisky is down.

  // Test 176
  ST(176);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 176);

  params->markParentDown(result, fail_threshold, retry_time); // all are down now.

  // Test 177
  ST(177);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_FAIL, nullptr, 80), 177);

  // Test 178
  tbl[0] = '\0';
  ST(178);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0;furry:80|1.0;frisky:80|1.0 "
    "round_robin=latched go_direct=false\n");
  REBUILD;
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 178);

  params->markParentDown(result, fail_threshold, retry_time); // fuzzy is down

  // Test 179
  ST(179);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 179);

  params->markParentDown(result, fail_threshold, retry_time); // fluffy is down

  // Test 180
  ST(180);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 180);

  params->markParentDown(result, fail_threshold, retry_time); // furry is down

  // Test 181
  ST(181);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 181);

  params->markParentDown(result, fail_threshold, retry_time); // frisky is down and we should be back on fuzzy.

  // Test 182
  ST(182);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_FAIL, nullptr, 80), 182);

  // wait long enough so that fuzzy is retryable.
  sleep(params->policy.ParentRetryTime - 2);

  // Test 183
  ST(183);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 183);

  // Test 184
  // mark fuzzy down with HostStatus API.
  HostStatus &_st = HostStatus::instance();
  _st.setHostStatus("fuzzy", HOST_STATUS_DOWN, 0, Reason::MANUAL);

  ST(184);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 184);

  // Test 185
  // mark fluffy down and expect furry to be chosen
  _st.setHostStatus("fluffy", HOST_STATUS_DOWN, 0, Reason::MANUAL);

  ST(185);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 185);

  // Test 186
  // mark furry and frisky down, fuzzy up and expect fuzzy to be chosen
  _st.setHostStatus("furry", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  _st.setHostStatus("frisky", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);

  ST(186);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 186);

  // Test 187
  // test the HostStatus API with ParentConsistent Hash.
  tbl[0] = '\0';
  ST(187);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0;furry:80|1.0;frisky:80|1.0 "
    "round_robin=consistent_hash go_direct=false\n");
  REBUILD;

  // mark all up.
  _st.setHostStatus("furry", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("fluffy", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("frisky", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);

  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 187);

  // Test 188
  // mark fuzzy down and expect fluffy.
  _st.setHostStatus("fuzzy", HOST_STATUS_DOWN, 0, Reason::MANUAL);

  ST(188);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 188);

  // Test 189
  // mark fuzzy back up and expect fuzzy.
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);

  ST(189);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 189);

  // Test 190
  // mark fuzzy back down and set the host status down
  // then wait for fuzzy to become available.
  // even though fuzzy becomes retryable we should not select it
  // because the host status is set to down.
  params->markParentDown(result, fail_threshold, retry_time);
  // set host status down
  _st.setHostStatus("fuzzy", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  // sleep long enough so that fuzzy is retryable
  sleep(params->policy.ParentRetryTime + 1);
  ST(190);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 190);

  // now set the host status on fuzzy to up and it should now
  // be retried.
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);
  ST(191);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 191);

  // Test 192
  tbl[0] = '\0';
  ST(192);
  T("dest_domain=rabbit.net parent=fuzzy:80,fluffy:80,furry:80,frisky:80 round_robin=false go_direct=true\n");
  REBUILD;
  // mark all up.
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("fluffy", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("furry", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("frisky", HOST_STATUS_UP, 0, Reason::MANUAL);
  // fuzzy should be chosen.
  sleep(1);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 192);

  // Test 193
  // mark fuzzy down and wait for it to become retryable
  ST(193);
  params->markParentDown(result, fail_threshold, retry_time);
  sleep(params->policy.ParentRetryTime + 1);
  // since the host status is down even though fuzzy is
  // retryable, fluffy should be chosen
  _st.setHostStatus("fuzzy", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 193);

  // Test 194
  // set the host status for fuzzy  back up and since its
  // retryable fuzzy should be chosen
  ST(194);
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 194);

  // Test 195
  // secondary_mode=1 (default) is covered by tests cases 173-177 above
  // secondary_mode=2 is tested here
  // fuzzy { frisky furry } fluffy
  tbl[0] = '\0';
  ST(195);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0 secondary_parent=furry:80|1.0;frisky:80|1.0 "
    "round_robin=consistent_hash go_direct=false secondary_mode=2\n");
  REBUILD;
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 195);
  params->markParentDown(result, fail_threshold, retry_time); // fuzzy is down.

  // Test 196
  ST(196);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 196);

  params->markParentDown(result, fail_threshold, retry_time); // fluffy is down.

  // Test 197
  ST(197);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 197);

  params->markParentDown(result, fail_threshold, retry_time); // frisky is down.

  // Test 198
  ST(198);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 198);

  params->markParentDown(result, fail_threshold, retry_time); // all are down now.

  // Test 199
  ST(199);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_FAIL, nullptr, 80), 199);

  // Test 200
  // secondary_mode=3 is tested here first-choice NOT marked down
  // fuzzy { frisky furry } fluffy
  tbl[0] = '\0';
  ST(200);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0 secondary_parent=furry:80|1.0;frisky:80|1.0 "
    "round_robin=consistent_hash go_direct=false secondary_mode=3\n");
  REBUILD;
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fuzzy", 80), 200);
  params->markParentDown(result, fail_threshold, retry_time); // fuzzy is down.

  // Test 201
  ST(201);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 201);

  params->markParentDown(result, fail_threshold, retry_time); // fluffy is down.

  // Test 202
  ST(202);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 202);

  params->markParentDown(result, fail_threshold, retry_time); // frisky is down.

  // Test 203
  ST(203);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 203);

  params->markParentDown(result, fail_threshold, retry_time); // all are down now.

  // Test 204
  ST(204);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_FAIL, nullptr, 80), 204);

  // Test 205
  // secondary_mode=3 is tested here first-choice marked down
  // fuzzy { frisky furry } fluffy
  tbl[0] = '\0';
  ST(205);
  T("dest_domain=rabbit.net parent=fuzzy:80|1.0;fluffy:80|1.0 secondary_parent=furry:80|1.0;frisky:80|1.0 "
    "round_robin=consistent_hash go_direct=false secondary_mode=3\n");
  REBUILD;
  _st.setHostStatus("fuzzy", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "frisky", 80), 205);
  params->markParentDown(result, fail_threshold, retry_time); // frisky is down.

  // Test 206
  ST(206);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "furry", 80), 206);

  params->markParentDown(result, fail_threshold, retry_time); // furry is down.

  // Test 207
  ST(207);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_SPECIFIED, "fluffy", 80), 207);

  params->markParentDown(result, fail_threshold, retry_time); // all are down now.

  // Test 208
  ST(208);
  REINIT;
  br(request, "i.am.rabbit.net");
  FP;
  sleep(1);
  RE(verify(result, PARENT_FAIL, nullptr, 80), 208);

  // Tests 209 through 211 test that host selection is based upon the hash_string

  // Test 209
  // fuzzy { curly larry, moe } fluffy
  tbl[0] = '\0';
  ST(209);
  T("dest_domain=stooges.net parent=curly:80|1.0&myhash;joe:80|1.0&hishash;larry:80|1.0&ourhash "
    "round_robin=consistent_hash go_direct=false\n");
  REBUILD;
  REINIT;
  br(request, "i.am.stooges.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "larry", 80), 209);

  // Test 210
  // fuzzy { curly larry, moe } fluffy
  tbl[0] = '\0';
  ST(210);
  T("dest_domain=stooges.net parent=curly:80|1.0&ourhash;joe:80|1.0&hishash;larry:80|1.0&myhash "
    "round_robin=consistent_hash go_direct=false\n");
  REBUILD;
  REINIT;
  br(request, "i.am.stooges.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "curly", 80), 210);

  // Test 211
  // fuzzy { curly larry, moe } fluffy
  tbl[0] = '\0';
  ST(211);
  T("dest_domain=stooges.net parent=curly:80|1.0&ourhash;joe:80|1.0&hishash;larry:80|1.0&myhash "
    "secondary_parent=carol:80|1.0&ourhash;betty:80|1.0&hishash;donna:80|1.0&myhash "
    "round_robin=consistent_hash go_direct=false\n");
  REBUILD;
  REINIT;
  _st.setHostStatus("curly", HOST_STATUS_DOWN, 0, Reason::MANUAL);
  br(request, "i.am.stooges.net");
  FP;
  RE(verify(result, PARENT_SPECIFIED, "carol", 80), 211);

  // cleanup, allow changes to be persisted to records.snap
  // so that subsequent test runs do not fail unexpectedly.
  _st.setHostStatus("curly", HOST_STATUS_UP, 0, Reason::MANUAL);
  _st.setHostStatus("fuzzy", HOST_STATUS_UP, 0, Reason::MANUAL);
  sleep(2);

  delete request;
  delete result;
  delete params;

  printf("Tests Passed: %d\nTests Failed: %d\n", passes, fails);
  *pstatus = (!fails ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
}

// verify returns 1 iff the test passes
int
verify(ParentResult *r, ParentResultType e, const char *h, int p)
{
  if (is_debug_tag_set("parent_select")) {
    show_result(r);
  }
  return (r->result != e) ? 0 : ((e != PARENT_SPECIFIED) ? 1 : (strcmp(r->hostname, h) ? 0 : ((r->port == p) ? 1 : 0)));
}

// br creates an HttpRequestData object
void
br(HttpRequestData *h, const char *os_hostname, sockaddr const *dest_ip)
{
  h->hdr = new HTTPHdr();
  h->hdr->create(HTTP_TYPE_REQUEST);
  h->hostname_str = (char *)ats_strdup(os_hostname);
  h->xact_start   = time(nullptr);
  ink_zero(h->src_ip);
  ink_zero(h->dest_ip);
  ats_ip_copy(&h->dest_ip.sa, dest_ip);
  h->incoming_port = 80;
  h->api_info      = new HttpApiInfo();
}

// show_result prints out the ParentResult information
void
show_result(ParentResult *p)
{
  switch (p->result) {
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
