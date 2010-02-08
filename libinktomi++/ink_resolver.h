/*
 * Copyright (c) 1983, 1987, 1989
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
  Imported from Bind-9.5.2-P2

  Changes:

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

#ifndef _ink_resolver_h_
#define	_ink_resolver_h_

#include "ink_platform.h"
#include <resolv.h>
#include <arpa/nameser.h>

#define RES_USE_DNSSEC 0x00200000 
#define RES_NOTLDQUERY  0x00100000      /*%< don't unqualified name as a tld */
#define RES_USE_DNAME   0x10000000      /*%< use DNAME */
#define RES_NO_NIBBLE2  0x80000000      /*%< disable alternate nibble lookup */
#define NS_TYPE_ELT  0x40 /*%< EDNS0 extended label type */
#define DNS_LABELTYPE_BITSTRING 0x41

#define MAXNSRR 32

#ifndef NS_GET16
#define NS_GET16(s, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (s) = ((u_int16_t)t_cp[0] << 8) \
            | ((u_int16_t)t_cp[1]) \
            ; \
        (cp) += NS_INT16SZ; \
} while (0)
#endif

#ifndef NS_GET32
#define NS_GET32(l, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (l) = ((u_int32_t)t_cp[0] << 24) \
            | ((u_int32_t)t_cp[1] << 16) \
            | ((u_int32_t)t_cp[2] << 8) \
            | ((u_int32_t)t_cp[3]) \
            ; \
        (cp) += NS_INT32SZ; \
} while (0)
#endif

#ifndef NS_PUT16
#define NS_PUT16(s, cp) do { \
        register u_int16_t t_s = (u_int16_t)(s); \
        register u_char *t_cp = (u_char *)(cp); \
        *t_cp++ = t_s >> 8; \
        *t_cp   = t_s; \
        (cp) += NS_INT16SZ; \
} while (0)
#endif

#ifndef NS_PUT32
#define NS_PUT32(l, cp) do { \
        register u_int32_t t_l = (u_int32_t)(l); \
        register u_char *t_cp = (u_char *)(cp); \
        *t_cp++ = t_l >> 24; \
        *t_cp++ = t_l >> 16; \
        *t_cp++ = t_l >> 8; \
        *t_cp   = t_l; \
        (cp) += NS_INT32SZ; \
} while (0)
#endif

struct __ink_res_state {
  int     retrans;                /*%< retransmission time interval */
  int     retry;                  /*%< number of times to retransmit */
#ifdef sun
  u_int   options;                /*%< option flags - see below. */
#else
  u_long  options;                /*%< option flags - see below. */
#endif
  int     nscount;                /*%< number of name servers */
  struct sockaddr_in
  nsaddr_list[MAXNSRR];     /*%< address of name server */
#define nsaddr  nsaddr_list[0]          /*%< for backward compatibility */
  u_short id;                     /*%< current message id */
  char    *dnsrch[MAXDNSRCH+1];   /*%< components of domain to search */
  char    defdname[256];          /*%< default domain (deprecated) */
#ifdef sun
  u_int   pfcode;                 /*%< RES_PRF_ flags - see below. */
#else
  u_long  pfcode;                 /*%< RES_PRF_ flags - see below. */
#endif
  unsigned ndots:4;               /*%< threshold for initial abs. query */
  unsigned nsort:4;               /*%< number of elements in sort_list[] */
  char    unused[3];
  struct {
    struct in_addr  addr;
    u_int32_t       mask;
  } sort_list[MAXRESOLVSORT];
  res_send_qhook qhook;           /*%< query hook */
  res_send_rhook rhook;           /*%< response hook */
  int     res_h_errno;            /*%< last one set for this context */
  int     _vcsock;                /*%< PRIVATE: for res_send VC i/o */
  u_int   _flags;                 /*%< PRIVATE: see below */
  u_int   _pad;                   /*%< make _u 64 bit aligned */
  union {
    /* On an 32-bit arch this means 512b total. */
    char    pad[72 - 4*sizeof (int) - 2*sizeof (void *)];
    struct {
      u_int16_t               nscount;
      u_int16_t               nstimes[MAXNSRR]; /*%< ms. */
      int                     nssocks[MAXNSRR];
      struct __ink_res_state_ext *ext;    /*%< extention for IPv6 */
    } _ext;
  } _u;
};
typedef __ink_res_state *ink_res_state;

union ink_res_sockaddr_union {
        struct sockaddr_in      sin;
#ifdef IN6ADDR_ANY_INIT
        struct sockaddr_in6     sin6;
#endif
#ifdef ISC_ALIGN64
        int64_t                 __align64;      /*%< 64bit alignment */
#else
        int32_t                 __align32;      /*%< 32bit alignment */
#endif
        char                    __space[128];   /*%< max size */
};

struct __ink_res_state_ext {
        union ink_res_sockaddr_union nsaddrs[MAXNSRR];
        struct sort_list {
                int     af;
                union {
                        struct in_addr  ina;
                        struct in6_addr in6a;
                } addr, mask;
        } sort_list[MAXRESOLVSORT];
        char nsuffix[64];
        char nsuffix2[64];
};


int ink_res_init(ink_res_state, unsigned long *pHostList,
                 int *pPort = 0, char *pDefDomain = 0, char *pSearchList = 0);
int ink_res_mkquery(ink_res_state, int, const char *, int, int,
                    const unsigned char *, int, const unsigned char *, unsigned char *, int);

#if (HOST_OS != linux)
int inet_aton(register const char *cp, struct in_addr *addr);
#endif

int ink_ns_name_ntop(const u_char *src, char *dst, size_t dstsiz);


#endif                          /* _ink_resolver_h_ */
