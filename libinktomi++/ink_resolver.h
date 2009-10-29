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

/*
 * ink_resolver.h --
 *
 *   structures required by resolver library etc.
 *
 * 
 *
 */

#ifndef _ink_resolver_h_
#define	_ink_resolver_h_

#include "ink_platform.h"

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */


#define MAXNSRR 32

  struct __res_state_rr
  {
    int retrans;                /* retransmition time interval */
    int retry;                  /* number of times to retransmit */
    u_long options;             /* option flags - see below. */
    int nscount;                /* number of name servers */
    struct sockaddr_in nsaddr_list[MAXNSRR];    /* address of name server */

#define nsaddr  nsaddr_list[0]  /* for backward compatibility */

    u_short id;                 /* current packet id */
    char *dnsrch[MAXDNSRCH + 1];        /* components of domain to search */
    char defdname[MAXDNAME];    /* default domain */
    u_long pfcode;              /* RES_PRF_ flags - see below. */
    unsigned ndots:4;           /* threshold for initial abs. query */
    unsigned nsort:4;           /* number of elements in sort_list[] */
    char unused[3];
    struct
    {
      struct in_addr addr;
      unsigned int mask;
    } sort_list[MAXRESOLVSORT];
    char pad[72];               /* On an i38this means 512b total. */
  };

  int ink_res_init(struct __res_state &p_res, unsigned long *pHostList,
                   int *pPort = 0, char *pDefDomain = 0, char *pSearchList = 0);
  int ink_res_init_rr(struct __res_state_rr &p_res, unsigned long *pHostList,
                      int *pPort = 0, char *pDefDomain = 0, char *pSearchList = 0);
  int ink_res_mkquery(struct __res_state &, int, const char *, int, int,
                      const unsigned char *, int, const unsigned char *, unsigned char *, int);
  int ink_res_mkquery_rr(struct __res_state_rr &, int, const char *, int, int,
                         const unsigned char *, int, const unsigned char *, unsigned char *, int);

#if (HOST_OS != linux)
  int inet_aton(register const char *cp, struct in_addr *addr);
#endif

#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* _ink_resolver_h_ */
