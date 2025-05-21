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

#pragma once

#include "iocore/dns/SplitDNSProcessor.h"
#include "P_DNSProcessor.h"

#include "proxy/ControlBase.h"
#include "proxy/ControlMatcher.h"

#include "iocore/eventsystem/ConfigProcessor.h"

#include "tscore/HostLookup.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_assert.h"

#include <swoc/TextView.h>

#include <cstdint>
#include <memory>

/* ---------------------------
   forward declarations ...
   --------------------------- */
void ink_split_dns_init(ts::ModuleVersion version);

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

using DNS_table = ControlMatcher<SplitDNSRecord, SplitDNSResult>;

/* --------------------------------------------------------------
   **                struct SplitDNSResult
   -------------------------------------------------------------- */
struct SplitDNSResult {
  SplitDNSResult();

  /* ------------
     public
     ------------ */
  DNSResultType r = DNS_SRVR_UNDEFINED;

  int m_line_number = 0;

  SplitDNSRecord *m_rec = nullptr;
};

/* --------------------------------------------------------------
   **                struct SplitDNS
   -------------------------------------------------------------- */
struct SplitDNS : public ConfigInfo {
  SplitDNS();
  ~SplitDNS() override;

  void *getDNSRecord(swoc::TextView hostname);
  void  findServer(RequestData *rdata, SplitDNSResult *result);

  std::unique_ptr<DNS_table> m_DNSSrvrTable = nullptr;

  int32_t m_SplitDNSlEnable = 0;

  /* ----------------------------
     required by the alleged fast
     path
     ---------------------------- */
  bool                   m_bEnableFastPath = false;
  HostLookup::LeafArray *m_pxLeafArray     = nullptr;
  int                    m_numEle          = 0;
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
  DNSRequestData() = default;

  char *
  get_string() override
  {
    ink_release_assert(!"Do not get a writeable string from a DNS request");
  };
  const char *get_host() override;

  sockaddr const *get_ip() override;        // unused required virtual method.
  sockaddr const *get_client_ip() override; // unused required virtual method.

  swoc::TextView m_pHost;
};

/* --------------------------------------------------------------
   DNSRequestData::get_host()
   -------------------------------------------------------------- */
inline const char *
DNSRequestData::get_host()
{
  return m_pHost.data();
}

/* --------------------------------------------------------------
   DNSRequestData::get_ip()
   -------------------------------------------------------------- */
inline sockaddr const *
DNSRequestData::get_ip()
{
  return nullptr;
}

/* --------------------------------------------------------------
   DNSRequestData::get_client_ip()
   -------------------------------------------------------------- */
inline sockaddr const *
DNSRequestData::get_client_ip()
{
  return nullptr;
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

  Result Init(matcher_line *line_info);

  const char *ProcessDNSHosts(char *val);
  const char *ProcessDomainSrchList(char *val);
  const char *ProcessDefDomain(char *val);

  void UpdateMatch(SplitDNSResult *result, RequestData *rdata);
  void Print() const;

  DNSServer m_servers;
  int       m_dnsSrvr_cnt      = 0;
  int       m_domain_srch_list = 0;
};

/* --------------------------------------------------------------
   SplitDNSRecord::SplitDNSRecord()
   -------------------------------------------------------------- */
TS_INLINE
SplitDNSRecord::SplitDNSRecord() {}

/* --------------------------------------------------------------
   SplitDNSRecord::~SplitDNSRecord()
   -------------------------------------------------------------- */
TS_INLINE SplitDNSRecord::~SplitDNSRecord() {}
