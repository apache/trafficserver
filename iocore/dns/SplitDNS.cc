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
 *  SplitDNS.cc - Implementation of "split" DNS (as the name says)
 *
 *
 ****************************************************************************/

#include "inktomi++.h"

#ifdef SPLIT_DNS

#include "P_SplitDNS.h"

#include <sys/types.h>
//#include "ControlMatcher.h"
//#include "Main.h"
//#include "Error.h"
//#include "Continuation.h"
//#include "Event.h"
//#include "EventProcessor.h"
//#include "Config.h"
//#include "HTTP.h"

#include "MatcherUtils.h"
//#include "Tokenizer.h"
//#include "HttpTransact.h"
#include "HostLookup.h"

SplitDNSConfigProcessor SplitDNSconfigProcessor;

/* --------------------------------------------------------------
   this file is built using "ParentSelection.cc as a template.
   -------------    ------------------------------------------------- */

/* --------------------------------------------------------------
   globals
   -------------------------------------------------------------- */
static const char modulePrefix[] = "[SplitDNS]";
static Ptr<ProxyMutex> reconfig_mutex = NULL;

static ClassAllocator<DNSRequestData> DNSReqAllocator("DNSRequestDataAllocator");

/* --------------------------------------------------------------
   used by a lot of protocols. We do not have dest ip in most
   cases.
   -------------------------------------------------------------- */
const matcher_tags sdns_dest_tags = {
  "dest_host", "dest_domain", NULL, "url_regex", NULL, true
};


/* --------------------------------------------------------------
   config Callback Prototypes
   -------------------------------------------------------------- */
enum SplitDNSCB_t
{

  SDNS_FILE_CB,
  SDNS_ENABLE_CB
};


static const char *SDNSResultStr[] = {
  "DNSServer_Undefined",
  "DNSServer_Specified",
  "DNSServer_Default",
  "DNSServer_Failed"
};


int SplitDNSConfig::m_id = 0;
int SplitDNSConfig::gsplit_dns_enabled = 0;
Ptr<ProxyMutex> SplitDNSConfig::dnsHandler_mutex = 0;


/* --------------------------------------------------------------
   SplitDNSResult::SplitDNSResult()
   -------------------------------------------------------------- */
inline SplitDNSResult::SplitDNSResult()
: r(DNS_SRVR_UNDEFINED), m_line_number(0), m_rec(0), m_wrap_around(false)
{
}


/* --------------------------------------------------------------
   SplitDNS::SplitDNS()
   -------------------------------------------------------------- */
SplitDNS::SplitDNS()
: m_DNSSrvrTable(NULL), m_DefaultDNSSrvr(NULL), m_SplitDNSlEnable(0), m_def_domain_len(0),
  m_bEnableFastPath(false), m_pxLeafArray(NULL), m_numEle(0)
{
  memset(m_def_domain, 0, MAXDNAME);
}


/* --------------------------------------------------------------
   SplitDNS::SplitDNS()
   -------------------------------------------------------------- */
SplitDNS::~SplitDNS()
{

  if (m_DNSSrvrTable) {
    delete m_DNSSrvrTable;
  }

  if (m_DefaultDNSSrvr) {
    delete m_DefaultDNSSrvr;
  }

}


/* --------------------------------------------------------------
   SplitDNSConfig::acquire()
   -------------------------------------------------------------- */
SplitDNS *
SplitDNSConfig::acquire()
{
  return (SplitDNS *) SplitDNSconfigProcessor.get(SplitDNSConfig::m_id);
}


/* --------------------------------------------------------------
   SplitDNSConfig::release()
   -------------------------------------------------------------- */
void
SplitDNSConfig::release(SplitDNS * params)
{
  SplitDNSconfigProcessor.release(SplitDNSConfig::m_id, params);
}

/* --------------------------------------------------------------
   SplitDNSConfig::startup()
   -------------------------------------------------------------- */
void
SplitDNSConfig::startup()
{
  reconfig_mutex = new_ProxyMutex();
  dnsHandler_mutex = new_ProxyMutex();
  reconfigure();
}


/* --------------------------------------------------------------
   SplitDNSConfig::reconfigure()
   -------------------------------------------------------------- */
void
SplitDNSConfig::reconfigure()
{

  SplitDNS *params;
  params = NEW(new SplitDNS);

  IOCORE_ReadConfigInt32(gsplit_dns_enabled, "proxy.config.dns.splitDNS.enabled");

  params->m_SplitDNSlEnable = gsplit_dns_enabled;

  if (0 == gsplit_dns_enabled)
    return;

  params->m_DNSSrvrTable = NEW(new DNS_table("proxy.config.dns.splitdns.filename", modulePrefix, &sdns_dest_tags));

  params->m_numEle = params->m_DNSSrvrTable->getEntryCount();
  if (0 == params->m_DNSSrvrTable || (0 == params->m_numEle)) {

    Warning("No NAMEDs provided! Disabling SplitDNS");
    gsplit_dns_enabled = 0;
    return;
  }

  if (0 != params->m_DNSSrvrTable->getHostMatcher() &&
      0 == params->m_DNSSrvrTable->getReMatcher() &&
      0 == params->m_DNSSrvrTable->getIPMatcher() && 4 >= params->m_numEle) {

    HostLookup *pxHL = params->m_DNSSrvrTable->getHostMatcher()->getHLookup();
    params->m_pxLeafArray = (void *) pxHL->getLArray();
    params->m_bEnableFastPath = true;
  }

  char *def_domain = IOCORE_ConfigReadString("proxy.config.dns.splitdns.def_domain");

  if (def_domain) {
    ink_strncpy(params->m_def_domain, def_domain, MAXDNAME);
    xfree(def_domain);
  } else {
    // If def_domain comes back NULL, then either: the record
    // doesn't exist -or- the record does exist but is currently set
    // to NULL.  In this case, just clear the m_def_domain.
    memset(params->m_def_domain, 0, MAXDNAME);
  }

  /* ----------------------------
     Handle default DNS server
     ---------------------------- */

  params->m_DefaultDNSSrvr = createDefaultServer();

  if ((0 == params->m_def_domain[0]) && (0 != params->m_DefaultDNSSrvr) && (true == params->m_bEnableFastPath)) {

    ink_strncpy(params->m_def_domain, params->m_DefaultDNSSrvr->m_servers.x_def_domain, MAXDNAME);
  }
  params->m_def_domain_len = strlen(params->m_def_domain);

  m_id = SplitDNSconfigProcessor.set(m_id, params);

  if (is_debug_tag_set("splitdns_config")) {
    SplitDNSConfig::print();
  }

}


/* --------------------------------------------------------------
   SplitDNSConfig::print()
   -------------------------------------------------------------- */
void
SplitDNSConfig::print()
{

  SplitDNS *params = SplitDNSConfig::acquire();

  Debug("splitdns_config", "DNS Server Selection Config\n");
  Debug("splitdns_config", "\tEnabled \n", params->m_SplitDNSlEnable);

  if (params->m_DefaultDNSSrvr == NULL) {

    Debug("splitdns_config", "\tNo Default DNS Server\n");
  } else {

    Debug("splitdns_config", "\tDefault DNS Server:\n");
    params->m_DefaultDNSSrvr->Print();
  }

  Debug("splitdns_config", "  ");

  params->m_DNSSrvrTable->Print();

  SplitDNSConfig::release(params);
}


/* --------------------------------------------------------------
   SplitDNS::getDNSRecord()
   -------------------------------------------------------------- */
void *
SplitDNS::getDNSRecord(char *hostname)
{
  char szHostName[MAXDNAME];
  char *pInHost = hostname;
  size_t remaining_str_size = sizeof szHostName;

  if (0 < m_def_domain_len) {
    if (0 == strchr(hostname, '.')) {

      int hlen = strlen(hostname) + m_def_domain_len;

      if (MAXDNAME > hlen) {
        ink_strncpy(szHostName, hostname, remaining_str_size);
        remaining_str_size -= strlen(hostname);
        strncat(szHostName, ".", remaining_str_size);
        remaining_str_size--;
        strncat(szHostName, m_def_domain, remaining_str_size);

        pInHost = &szHostName[0];
      }
    }
  }

  DNSRequestData *pRD = DNSReqAllocator.alloc();
  pRD->m_pHost = pInHost;

  SplitDNSResult res;
  findServer(pRD, &res);

  pRD->m_pHost = 0;
  DNSReqAllocator.free(pRD);

  if (DNS_SRVR_UNDEFINED == res.r) {

    Warning("Failed to match a valid DNS server!");
    ink_assert(!"Failed to match a valid DNS server");

  } else if (DNS_SRVR_DEFAULT == res.r) {

    Warning("Failed to match a valid DNS server! Using defaults ...");
    return (void *) &(res.m_rec->m_servers);
    //ink_assert(!"Failed to match a valid DNS server");
  }

  else if (DNS_SRVR_SPECIFIED == res.r) {

    return (void *) &(res.m_rec->m_servers);
  }

  return NULL;
}


/* --------------------------------------------------------------
   SplitDNS::findServer()
   -------------------------------------------------------------- */
void
SplitDNS::findServer(RD * rdata, SplitDNSResult * result)
{
  DNS_table *tablePtr = m_DNSSrvrTable;
  SplitDNSRecord *defaultPtr = m_DefaultDNSSrvr;
  SplitDNSRecord *rec;

  ink_assert(result->r == DNS_SRVR_UNDEFINED);

  if (m_SplitDNSlEnable == 0) {
    result->r = DNS_SRVR_UNDEFINED;
    return;
  }

  result->m_rec = NULL;
  result->m_line_number = 0xffffffff;
  result->m_wrap_around = false;

  /* ---------------------------
     the 'alleged' fast path ...
     --------------------------- */
  if (m_bEnableFastPath) {

    SplitDNSRecord *data_ptr = 0;
    char *pHost = (char *) rdata->get_host();
    if (0 == pHost) {
      Warning("SplitDNS: No host to match !");
      return;
    }

    int len = strlen(pHost);
    HostLeaf *pxHL = (HostLeaf *) m_pxLeafArray;
    for (int i = 0; i < m_numEle; i++) {

      if (0 == pxHL)
        break;

      if (false == pxHL[i].isNot && pxHL[i].len > len)
        continue;

      int idx = len - pxHL[i].len;
      char *pH = &pHost[idx];
      char *pMatch = (char *) pxHL[i].match;
      char cNot = *pMatch;

      if ('!' == cNot)
        pMatch++;

      int res = memcmp(pH, pMatch, pxHL[i].len);

      if ((0 != res && '!' == cNot) || (0 == res && '!' != cNot)) {
        data_ptr = (SplitDNSRecord *) pxHL[i].opaque_data;
        data_ptr->UpdateMatch(result, rdata);
        break;
      }
    }
  } else {

    tablePtr->Match(rdata, result);
  }

  rec = result->m_rec;

  if (rec == NULL) {

    /* ------------------------------------------
       return default DNS server.
       however, still return DNS_SRVR_UNDEFINED
       ------------------------------------------ */

    if (defaultPtr != NULL) {

      rec = result->m_rec = defaultPtr;
      result->r = DNS_SRVR_DEFAULT;

    } else {

      result->r = DNS_SRVR_UNDEFINED;
    }
    return;

  } else {
    result->r = DNS_SRVR_SPECIFIED;
  }

  if (is_debug_tag_set("splitdns_config")) {
    const char *host = rdata->get_host();

    switch (result->r) {
    case DNS_SRVR_FAIL:

      Debug("splitdns_config", "Result for %s was %s", host, SDNSResultStr[result->r]);
      break;

    case DNS_SRVR_SPECIFIED:

      Debug("splitdns_config", "Result for %s was dns servers \n", host);
      result->m_rec->Print();
      break;

    default:
      // This covers:
      // DNS_SRVR_UNDEFINED and DNS_SRVR_DEFAULT
      break;
    }
  }
}

/* --------------------------------------------------------------
   SplitDNSRecord::ProcessDNSHosts()
   -------------------------------------------------------------- */
const char *
SplitDNSRecord::ProcessDNSHosts(char *val)
{
  Tokenizer pTok(",; \t\r");
  int numTok;
  const char *current;
  int port = 0;
  char *tmp;
  int totsz = 0, sz = 0;

  numTok = pTok.Initialize(val, SHARE_TOKS);
  if (MAXNS < numTok) {
    numTok = MAXNS;
    Warning("Only first %d DNS servers are tracked", numTok);
  }
  if (numTok == 0) {
    return "No servers specified";
  }

  /* ------------------------------------------------
     Allocate the servers array and Loop through the
     set of servers specified
     ------------------------------------------------ */

  for (int i = 0; i < numTok; i++) {
    current = pTok[i];

    tmp = (char *) strchr(current, ':');
    // coverity[secure_coding]
    if (tmp != NULL && sscanf(tmp + 1, "%d", &port) != 1) {

      return "Malformed DNS port";
    }

    /* ----------------------------------------
       Make sure that is no garbage beyond the
       server port
       ---------------------------------------- */
    if (tmp) {
      char *scan = tmp + 1;
      for (; *scan != '\0' && ParseRules::is_digit(*scan); scan++);
      for (; *scan != '\0' && ParseRules::is_wslfcr(*scan); scan++);

      if (*scan != '\0') {

        return "Garbage trailing entry or invalid separator";
      }

      if (tmp - current > (MAXDNAME - 1)) {
        return "DNS server name (ip) is too long";

      } else if (tmp - current == 0) {
        return "server string is emtpy";
      }

      *tmp = 0;
    }

    unsigned int addr = inet_addr(current);

    if (((uint32_t)-1) == htonl(addr)) {
      return "invalid IP address given for a DNS server";
    }

    m_servers.x_server_ip[i] = addr;
    m_servers.x_dns_server_port[i] = port ? port : NAMESERVER_PORT;

    if ((MAXDNAME * 2 - 1) > totsz) {
      sz = strlen(current);
      memcpy((m_servers.x_dns_ip_line + totsz), current, sz);
      totsz += sz;
    }
  }

  m_dnsSrvr_cnt = numTok;
  return NULL;
}


/* --------------------------------------------------------------
   SplitDNSRecord::ProcessDefDomain()
   -------------------------------------------------------------- */
const char *
SplitDNSRecord::ProcessDefDomain(char *val)
{
  Tokenizer pTok(",; \t\r");
  int numTok;

  numTok = pTok.Initialize(val, SHARE_TOKS);

  if (numTok > 1) {
    return "more than one default domain name specified";
  }

  if (numTok == 0) {
    return "no default domain name specified";
  }

  int len = 0;
  if (pTok[0] && 0 != (len = strlen(pTok[0]))) {

    memcpy(&m_servers.x_def_domain[0], pTok[0], len);
    m_servers.x_def_domain[len] = '\0';
  }

  return NULL;
}


/* --------------------------------------------------------------
   SplitDNSRecord::ProcessDomainSrchList()
   -------------------------------------------------------------- */
const char *
SplitDNSRecord::ProcessDomainSrchList(char *val)
{
  Tokenizer pTok(",; \t\r");
  int numTok;
  int cnt = 0, sz = 0;
  char *pSp = 0;
  const char *current;

  numTok = pTok.Initialize(val, SHARE_TOKS);

  if (numTok == 0) {
    return "No servers specified";
  }

  pSp = &m_servers.x_domain_srch_list[0];

  for (int i = 0; i < numTok; i++) {

    current = pTok[i];
    cnt = sz += strlen(current);

    if (MAXDNAME - 1 < sz)
      break;

    memcpy(pSp, current, cnt);
    pSp += (cnt + 1);
  }

  m_domain_srch_list = numTok;
  return NULL;
}


/* --------------------------------------------------------------
   SplitDNSRecord::Init()

   matcher_line* line_info - contains parsed label/value pairs
   of the current split.config line
   -------------------------------------------------------------- */
char *
SplitDNSRecord::Init(matcher_line * line_info)
{

  const char *errPtr = NULL;
  const int errBufLen = 1024;
  char *errBuf = (char *) xmalloc(errBufLen * sizeof(char));
  const char *tmp;
  char *label;
  char *val;

  this->line_num = line_info->line_num;

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {

    label = line_info->line[0][i];
    val = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, "def_domain") == 0) {

      if (NULL != (errPtr = ProcessDefDomain(val))) {
        snprintf(errBuf, errBufLen, "%s %s at line %d", modulePrefix, errPtr, line_num);
        return errBuf;
      }

      line_info->line[0][i] = NULL;
      line_info->num_el--;

      continue;
    }

    if (strcasecmp(label, "search_list") == 0) {

      if (NULL != (errPtr = ProcessDomainSrchList(val))) {
        snprintf(errBuf, errBufLen, "%s %s at line %d", modulePrefix, errPtr, line_num);
        return errBuf;
      }

      line_info->line[0][i] = NULL;
      line_info->num_el--;

      continue;
    }

    if (strcasecmp(label, "named") == 0) {

      if (NULL != (errPtr = ProcessDNSHosts(val))) {
        snprintf(errBuf, errBufLen, "%s %s at line %d", modulePrefix, errPtr, line_num);
        return errBuf;
      }

      line_info->line[0][i] = NULL;
      line_info->num_el--;
    }
  }

  if (this->m_servers.x_server_ip[0] == 0) {

    snprintf(errBuf, errBufLen, "%s No server specified in splitdns.config at line %d", modulePrefix, line_num);
    return errBuf;

  }

  DNSHandler *dnsH = new DNSHandler;
  ink_res_state res = new __ink_res_state;
  memset(res, 0, sizeof(__ink_res_state));

  if ((-1 == ink_res_init(res,
                          &m_servers.x_server_ip[0],
                          &m_servers.x_dns_server_port[0],
                          &m_servers.x_def_domain[0], &m_servers.x_domain_srch_list[0]))) {

    snprintf(errBuf, errBufLen,
                 "Failed to build res record for the servers %u ... on port %d",
                 m_servers.x_server_ip[0], m_servers.x_dns_server_port[0]);

    return errBuf;
  }
  dnsH->m_res = res;

  dnsH->mutex = SplitDNSConfig::dnsHandler_mutex;
  dnsH->options = res->options;
  dnsH->ip = DEFAULT_DOMAIN_NAME_SERVER;
  dnsH->port = DOMAIN_SERVICE_PORT;

  m_servers.x_dnsH = dnsH;

  SET_CONTINUATION_HANDLER(dnsH, &DNSHandler::startEvent_sdns);
  eventProcessor.schedule_imm(dnsH, ET_DNS);

  /* -----------------------------------------------------
     Process any modifiers to the directive, if they exist
     ----------------------------------------------------- */
  if (line_info->num_el > 0) {

    tmp = ProcessModifiers(line_info);

    if (tmp != NULL) {

      snprintf(errBuf, errBufLen, "%s %s at line %d in splitdns.config", modulePrefix, tmp, line_num);
      return errBuf;
    }
  }

  if (errBuf)
    xfree(errBuf);

  return NULL;
}


/* --------------------------------------------------------------
    SplitDNSRecord::UpdateMatch()
   -------------------------------------------------------------- */
void
SplitDNSRecord::UpdateMatch(SplitDNSResult * result, RD * rdata)
{
  NOWARN_UNUSED(rdata);
  int last_number = result->m_line_number;
  if ((last_number<0) || (last_number> this->line_num)) {

    result->m_rec = this;
    result->m_line_number = this->line_num;

    Debug("splitdns_config", "Matched with 0x%x dns node from line %d", this, this->line_num);
  }
}


/* --------------------------------------------------------------
    SplitDNSRecord::Print()
   -------------------------------------------------------------- */
void
SplitDNSRecord::Print()
{
  struct in_addr address;

  Debug("splitdns_config", "\t\t");
  for (int i = 0; i < m_dnsSrvr_cnt; i++) {

    address.s_addr = m_servers.x_server_ip[i];
    char *pAdr = inet_ntoa(address);

    Debug("splitdns_config", " %s:%d ", pAdr, m_servers.x_dns_server_port[i]);
  }
}


/* --------------------------------------------------------------
    SplitDNSRecord* createDefaultServer()
   -------------------------------------------------------------- */
SplitDNSRecord *
createDefaultServer()
{
  SplitDNSRecord *newRec;

  ink_res_state res = new __ink_res_state;
  memset(res, 0, sizeof(__ink_res_state));

  if (ink_res_init(res, 0, 0, 0, 0) < 0) {
    Warning("no default name server configured!");
    return 0;
  }

  newRec = NEW(new SplitDNSRecord);

  for (int i = 0; i < _res.nscount; i++) {
    newRec->m_servers.x_server_ip[i] = _res.nsaddr_list[i].sin_addr.s_addr;
    newRec->m_servers.x_dns_server_port[i] = ntohs(_res.nsaddr_list[i].sin_port);
  }

  newRec->m_servers.x_dnsH = new DNSHandler;
  newRec->m_servers.x_dnsH->m_res = res;

  newRec->m_servers.x_dnsH->mutex = SplitDNSConfig::dnsHandler_mutex;
  newRec->m_servers.x_dnsH->options = _res.options;
  newRec->m_servers.x_dnsH->ip = DEFAULT_DOMAIN_NAME_SERVER;
  newRec->m_servers.x_dnsH->port = DOMAIN_SERVICE_PORT;

  SET_CONTINUATION_HANDLER(newRec->m_servers.x_dnsH, &DNSHandler::startEvent_sdns);
  eventProcessor.schedule_imm(newRec->m_servers.x_dnsH, ET_DNS);

  newRec->m_dnsSrvr_cnt = _res.nscount;
  ink_strncpy(newRec->m_servers.x_def_domain, _res.defdname, MAXDNAME);

  newRec->m_domain_srch_list = 0;
  return newRec;
}


/* --------------------------------------------------------------
    void * splitDNS_CB()
   -------------------------------------------------------------- */
void
splitDNS_CB(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void) token;
  (void) data_type;
  (void) value;
  (void) data;
  eventProcessor.schedule_imm(NEW(new SDNS_UpdateContinuation(reconfig_mutex)), ET_CALL);
}


class SplitDNSConfigInfoReleaser:public Continuation
{
public:
  SplitDNSConfigInfoReleaser(unsigned int id, SplitDNSConfigInfo * info)
    : Continuation(new_ProxyMutex()), m_id(id), m_info(info)
  {
    SET_HANDLER(&SplitDNSConfigInfoReleaser::handle_event);
  }

  int handle_event(int event, void *edata)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(edata);
    SplitDNSconfigProcessor.release(m_id, m_info);
    delete this;
    return 0;
  }

public:
  unsigned int m_id;
  SplitDNSConfigInfo *m_info;
};

SplitDNSConfigProcessor::SplitDNSConfigProcessor()
 : ninfos(0)
{
  int i;

  for (i = 0; i < MAX_CONFIGS; i++) {
    infos[i] = NULL;
  }
}

unsigned int
SplitDNSConfigProcessor::set(unsigned int id, SplitDNSConfigInfo * info)
{
  SplitDNSConfigInfo *old_info;
  int idx;

  if (id == 0) {
    id = ink_atomic_increment((int *) &ninfos, 1) + 1;
    ink_assert(id != 0);
    ink_assert(id <= MAX_CONFIGS);
  }

  info->m_refcount = 1;

  if (id > MAX_CONFIGS) {
    // invalid index
    Error("[SplitDNSConfigProcessor::set] invalid index");
    return 0;
  }

  idx = id - 1;

  do {
    old_info = (SplitDNSConfigInfo *) infos[idx];
  } while (!ink_atomic_cas_ptr((pvvoidp) & infos[idx], old_info, info));

  if (old_info) {
    eventProcessor.schedule_in(NEW(new SplitDNSConfigInfoReleaser(id, old_info)), HRTIME_SECONDS(60));
  }

  return id;
}

SplitDNSConfigInfo *
SplitDNSConfigProcessor::get(unsigned int id)
{
  SplitDNSConfigInfo *info;
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // return NULL, because we of an invalid index
    return NULL;
  }

  idx = id - 1;
  info = (SplitDNSConfigInfo *) infos[idx];
  if (ink_atomic_increment((int *) &info->m_refcount, 1) < 0) {
    ink_assert(!"not reached");
  }

  return info;
}

void
SplitDNSConfigProcessor::release(unsigned int id, SplitDNSConfigInfo * info)
{
  int val;
  int idx;

  ink_assert(id != 0);
  ink_assert(id <= MAX_CONFIGS);

  if (id == 0 || id > MAX_CONFIGS) {
    // nothing to delete since we have an invalid index
    return;
  }

  idx = id - 1;
  val = ink_atomic_increment((int *) &info->m_refcount, -1);
  if ((infos[idx] != info) && (val == 1)) {
    delete info;
  }
}

void
ink_split_dns_init(ModuleVersion v)
{
  static int init_called = 0;

  ink_release_assert(!checkModuleVersion(v, SPLITDNS_MODULE_VERSION));
  if (init_called)
    return;

  init_called = 1;
}

#endif // SPLIT_DNS
