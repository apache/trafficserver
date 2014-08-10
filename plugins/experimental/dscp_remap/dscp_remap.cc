/**

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ts/ts.h>
#include <ts/remap.h>

static const char* PLUGIN_NAME = "dscp_remap";

struct DscpRemapInstance {
  int tos;

  DscpRemapInstance() : tos(0) { } ;
};

TSReturnCode
TSRemapInit(TSRemapInterface* api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", (size_t)(errbuf_size - 1));
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is succesfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* errbuf, int errbuf_size)
{
  int dscp;
  DscpRemapInstance* di;

  dscp = atoi(argv[2]);

  di = new DscpRemapInstance();
  *ih = (void *) di;

  di->tos = dscp << 2;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void* ih)
{
  DscpRemapInstance* di = static_cast<DscpRemapInstance*>(ih);
  delete di;
}

TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  int sockfd;
  DscpRemapInstance* di = static_cast<DscpRemapInstance*>(ih);
  int tos = di->tos;
  int retval;

  retval = TSHttpTxnClientFdGet(txnp, &sockfd);
  if (retval != TS_SUCCESS) {
    TSError("Error getting sockfd: %d\n", retval);
    return TSREMAP_NO_REMAP;
  }

  // Find out if this is a v4 or v6 connection, there's a different way of
  // setting the marking
  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  const struct sockaddr *client_addr = TSHttpSsnClientAddrGet(ssnp);
  if (client_addr->sa_family == AF_INET6) {
    retval = setsockopt(sockfd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos));
  } else {
    retval = setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
  }

  if (retval != TS_SUCCESS) {
    TSError("Error setting sockfd: %d\n", retval);
  }

  return TSREMAP_NO_REMAP;
}
