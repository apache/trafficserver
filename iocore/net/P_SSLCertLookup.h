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

#ifndef _ssl_Cert_Lookup_h_
#define _ssl_Cert_Lookup_h_
#include "inktomi++.h"
#include "P_SSLNetProcessor.h"

#define PATH_NAME_MAX         511

class SSLCertLookup
{
  bool buildTable();
  const char *extractIPAndCert(matcher_line * line_info, char **addr, char **cert, char **priKey);
  int addInfoToHash(char *strAddr, char *cert, char *serverPrivateKey);

  InkHashTable *SSLCertLookupHashTable;
  char config_file_path[PATH_NAME_MAX];
  SslConfigParams *param;

public:
    bool multipleCerts;
  void init(SslConfigParams * param);
  SSL_CTX *findInfoInHash(char *strAddr);
    SSLCertLookup();
   ~SSLCertLookup();
};

extern SSLCertLookup sslCertLookup;

#endif
