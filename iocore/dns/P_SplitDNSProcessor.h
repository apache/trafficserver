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
 * P_SplitDNSProcessor.h - Interface to DNS server selection
 *
 *
 ****************************************************************************/

#ifndef _P_SPLIT_DNSProcessor_H_
#define _P_SPLIT_DNSProcessor_H_

/*
#include "P_DNS.h"
#include "I_SplitDNS.h"
#include "I_Lock.h"
#include "ControlBase.h"
#include "ControlMatcher.h"
*/
#include "ProxyConfig.h"

/* ---------------------------
   forward declarations ...
   --------------------------- */
void ink_split_dns_init(ModuleVersion version);

struct RequestData;

struct matcher_line;

class SplitDNSRecord;
struct SplitDNSResult;

struct DNSServer;

enum DNSResultType {
  DNS_SRVR_UNDEFINED = 0,
  DNS_SRVR_SPECIFIED,
  DNS_SRVR_FAIL,
};

typedef ControlMatcher<SplitDNSRecord, SplitDNSResult> DNS_table;

/* --------------------------------------------------------------
   **                struct SplitDNSResult
   -------------------------------------------------------------- */
struct SplitDNSResult {
  SplitDNSResult();

  /* ------------
     public
     ------------ */
  DNSResultType r;

  DNSServer *get_dns_record();
  int get_dns_srvr_count();

  /* ------------
     private
     ------------ */
  int m_line_number;

  SplitDNSRecord *m_rec;
  bool m_wrap_around;
};

/* --------------------------------------------------------------
   **                struct SplitDNS
   -------------------------------------------------------------- */
struct SplitDNS : public ConfigInfo {
  SplitDNS();
  ~SplitDNS();

  void *getDNSRecord(const char *hostname);
  void findServer(RequestData *rdata, SplitDNSResult *result);

  DNS_table *m_DNSSrvrTable;

  int32_t m_SplitDNSlEnable;

  /* ----------------------------
     required by the alleged fast
     path
     ---------------------------- */
  bool m_bEnableFastPath;
  void *m_pxLeafArray;
  int m_numEle;
};

/* --------------------------------------------------------------
   SplitDNSConfig::isSplitDNSEnabled()
   -------------------------------------------------------------- */
TS_INLINE bool
SplitDNSConfig::isSplitDNSEnabled()
{
  return (gsplit_dns_enabled ? true : false);
}

//
// End API to outside world
//

/* --------------------------------------------------------------
   **                class DNSRequestData

   A record for an single server
   -------------------------------------------------------------- */
class DNSRequestData : public RequestData
{
public:
  DNSRequestData();

  char *get_string();

  const char *get_host();

  sockaddr const *get_ip();        // unused required virtual method.
  sockaddr const *get_client_ip(); // unused required virtual method.

  const char *m_pHost;
};

/* --------------------------------------------------------------
   DNSRequestData::get_string()
   -------------------------------------------------------------- */
TS_INLINE
DNSRequestData::DNSRequestData() : m_pHost(0)
{
}

/* --------------------------------------------------------------
   DNSRequestData::get_string()
   -------------------------------------------------------------- */
TS_INLINE char *
DNSRequestData::get_string()
{
  return ats_strdup((char *)m_pHost);
}

/* --------------------------------------------------------------
   DNSRequestData::get_host()
   -------------------------------------------------------------- */
TS_INLINE const char *
DNSRequestData::get_host()
{
  return m_pHost;
}

/* --------------------------------------------------------------
   DNSRequestData::get_ip()
   -------------------------------------------------------------- */
TS_INLINE sockaddr const *
DNSRequestData::get_ip()
{
  return NULL;
}

/* --------------------------------------------------------------
   DNSRequestData::get_client_ip()
   -------------------------------------------------------------- */
TS_INLINE sockaddr const *
DNSRequestData::get_client_ip()
{
  return NULL;
}

/* --------------------------------------------------------------
   *                 class SplitDNSRecord

   A record for a configuration line in the splitdns.config file
   -------------------------------------------------------------- */
class SplitDNSRecord : public ControlBase
{
public:
  SplitDNSRecord();
  ~SplitDNSRecord();

  config_parse_error Init(matcher_line *line_info);

  const char *ProcessDNSHosts(char *val);
  const char *ProcessDomainSrchList(char *val);
  const char *ProcessDefDomain(char *val);

  void UpdateMatch(SplitDNSResult *result, RequestData *rdata);
  void Print();

  DNSServer m_servers;
  int m_dnsSrvr_cnt;
  int m_domain_srch_list;
};

/* --------------------------------------------------------------
   SplitDNSRecord::SplitDNSRecord()
   -------------------------------------------------------------- */
TS_INLINE
SplitDNSRecord::SplitDNSRecord() : m_dnsSrvr_cnt(0), m_domain_srch_list(0)
{
}

/* --------------------------------------------------------------
   SplitDNSRecord::~SplitDNSRecord()
   -------------------------------------------------------------- */
TS_INLINE SplitDNSRecord::~SplitDNSRecord()
{
}

/* ------------------
   Helper Functions
   ------------------ */

SplitDNSRecord *createDefaultServer();
void reloadDefaultParent(char *val);
void reloadParentFile();

#endif
