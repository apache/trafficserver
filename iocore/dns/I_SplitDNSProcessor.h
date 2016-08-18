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
 * I_SplitDNSProcessor.h - Interface to DNS server selection
 *
 *
 ****************************************************************************/

#ifndef _I_SPLIT_DNSProcessor_H_
#define _I_SPLIT_DNSProcessor_H_

struct SplitDNS;

/* --------------------------------------------------------------
   **                struct SplitDNSConfig
   -------------------------------------------------------------- */

struct SplitDNSConfig {
  static void startup();

  static bool isSplitDNSEnabled();

  static void reconfigure();
  static SplitDNS *acquire();
  static void release(SplitDNS *params);
  static void print();

  static int m_id;
  static Ptr<ProxyMutex> dnsHandler_mutex;
  static ConfigUpdateHandler<SplitDNSConfig> *splitDNSUpdate;

  static int gsplit_dns_enabled;
};

#endif
