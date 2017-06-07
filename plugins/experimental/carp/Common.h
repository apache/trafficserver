/** @file

  Defines needed by most CARP .cc source files.

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
#ifndef __COMMON_H__
#define __COMMON_H__ 1

#include <string>
#include <vector>
#include <dirent.h>
#include <memory.h>

#include <ts/ts.h>
#include <netdb.h>
#include <unistd.h>

// debug messages viewable by setting 'proxy.config.diags.debug.tags' in 'records.config'

// debug messages during one-time initialization
static const char DEBUG_TAG_INIT[] = "carp.init";

// debug messages triggered on every request serviced
static const char DEBUG_TAG_HOOK[] = "carp.hook";

// debug messages related to the periodic healthcheck thread
static const char DEBUG_TAG_HEALTH[] = "carp.health";

// debug messages related to the periodic healthcheck threads HTTP fetch function
static const char DEBUG_FETCH_TAG[] = "carp.fetch";

static const std::string& CARP_ROUTED_HEADER("ATS-Carp-Routed");
static const std::string& CARPABLE_HEADER("ATS-Carpable");
static const std::string& CARP_FORWARD_HEADER("ATS-Carp-Forward");
static const std::string& CARP_PREMAP_SCHEME("ATS-Carp-Scheme");
static const std::string& CARP_STATUS_HEADER("ATS-Carp-Forward-Status");

//ATS-Carp-Forward-Status possible header values
static const std::string& CARP_NOFORWARDED("CARP_NOFORWARDED"); 
static const std::string& CARP_FORWARDED("CARP_FORWARDED");

static std::string CARP_SCHEME_FOR_HASH(TS_URL_SCHEME_HTTP, TS_URL_LEN_HTTP);
static const int  CARP_PORT_FOR_HASH = 80;

#define DEFAULT_HEALTH_CHECK_TIMEOUT 5 //default health check timeout value

enum {
    TCP_ESTABLISHED = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_LISTEN,
    TCP_CLOSING,    

    TCP_MAX_STATES 
};

void            stringExplode(std::string str, std::string separator, std::vector<std::string>* results);
bool            isPortSelf(unsigned int iPort);
struct hostent* getHostIp(std::string hName, struct hostent *h, char *buf, int buflen);
bool            isSelf(std::string sName, int iPort, struct hostent *pSelf);
bool            addHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header, const std::string value);
bool            getHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header, std::string& value);
bool            removeHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header);
bool            setHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header, const std::string value);
bool            getStringFromSockaddr(const struct sockaddr *sa, std::string& s);
#endif

