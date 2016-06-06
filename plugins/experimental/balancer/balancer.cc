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

#include "balancer.h"
#include <ts/remap.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <iterator>

// Using ink_inet API is cheating, but I was too lazy to write new IPv6 address parsing routines ;)
#include "ts/ink_inet.h"

// The policy type is the first comma-separated token.
static BalancerInstance *
MakeBalancerInstance(const char *opt)
{
  const char *end = strchr(opt, ',');
  size_t len      = end ? std::distance(opt, end) : strlen(opt);

  if (len == lengthof("hash") && strncmp(opt, "hash", len) == 0) {
    return MakeHashBalancer(end ? end + 1 : NULL);
  } else if (len == lengthof("roundrobin") && strncmp(opt, "roundrobin", len) == 0) {
    return MakeRoundRobinBalancer(end ? end + 1 : NULL);
  } else {
    TSError("[balancer] Invalid balancing policy '%.*s'", (int)len, opt);
    return NULL;
  }
}

static BalancerTarget
MakeBalancerTarget(const char *strval)
{
  BalancerTarget target = BalancerTarget();

  union {
    struct sockaddr_storage storage;
    struct sockaddr sa;
  } address;

  memset(&address, 0, sizeof(address));

  // First, check whether we have an address literal.
  if (ats_ip_pton(strval, &address.sa) == 0) {
    char namebuf[INET6_ADDRSTRLEN];

    target.port = ats_ip_port_host_order(&address.sa);
    target.name = ats_ip_ntop(&address.sa, namebuf, sizeof(namebuf));
  } else {
    const char *colon = strrchr(strval, ':');

    if (colon) {
      size_t len = std::distance(strval, colon);

      target.port = strtol(colon + 1, NULL, 10);
      target.name = std::string(strval, len);
    } else {
      target.port = 0;
      target.name = strval;
    }
  }

  if (target.port > INT16_MAX) {
    TSError("[balancer] Ignoring invalid port number for target '%s'", strval);
    target.port = 0;
  }

  return target;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api */, char * /* errbuf */, int /* bufsz */)
{
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size)
{
  static const struct option longopt[] = {{const_cast<char *>("policy"), required_argument, 0, 'p'}, {0, 0, 0, 0}};

  BalancerInstance *balancer = NULL;

  // The first two arguments are the "from" and "to" URL string. We need to
  // skip them, but we also require that there be an option to masquerade as
  // argv[0], so we increment the argument indexes by 1 rather than by 2.
  argc--;
  argv++;

  for (;;) {
    int opt;

    opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);
    switch (opt) {
    case 'p':
      balancer = MakeBalancerInstance(optarg);
      break;
    case -1:
      break;
    default:
      snprintf(errbuf, errbuf_size, "invalid balancer option '%d'", opt);
      delete balancer;
      return TS_ERROR;
    }

    if (opt == -1) {
      break;
    }
  }

  if (!balancer) {
    strncpy(errbuf, "missing balancer policy", errbuf_size);
    return TS_ERROR;
  }

  // Pick up the remaining options as balance targets.
  for (int i = optind; i < argc; ++i) {
    BalancerTarget target = MakeBalancerTarget(argv[i]);

    balancer->push_target(target);
    if (target.port) {
      TSDebug("balancer", "added target -> %s:%u", target.name.c_str(), target.port);
    } else {
      TSDebug("balancer", "added target -> %s", target.name.c_str());
    }
  }

  *instance = balancer;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  delete (BalancerInstance *)instance;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txn, TSRemapRequestInfo *rri)
{
  BalancerInstance *balancer   = (BalancerInstance *)instance;
  const BalancerTarget &target = balancer->balance(txn, rri);

  if (TSIsDebugTagSet("balancer")) {
    char *url;
    int len;

    url = TSHttpTxnEffectiveUrlStringGet(txn, &len);
    if (target.port) {
      TSDebug("balancer", "%s:%u <- %.*s", target.name.c_str(), target.port, len, url);
    } else {
      TSDebug("balancer", "%s <- %.*s", target.name.c_str(), len, url);
    }

    TSfree(url);
  }

  TSUrlHostSet(rri->requestBufp, rri->requestUrl, target.name.data(), target.name.size());

  if (target.port) {
    TSUrlPortSet(rri->requestBufp, rri->requestUrl, target.port);
  }

  return TSREMAP_DID_REMAP;
}
