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
 * 3. Neither the name of the University nor the names of its contributors
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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"

#include <resolv.h>
#include <arpa/nameser.h>

#if defined(openbsd)
#define NS_INT16SZ INT16SZ
#define NS_INT32SZ INT32SZ
#define NS_CMPRSFLGS INDIR_MASK
#define NS_GET16 GETSHORT
#define NS_GET32 GETLONG
#define NS_PUT16 PUTSHORT
#define NS_PUT32 PUTLONG
#endif

#ifndef T_DNAME
#define T_DNAME ns_t_dname
#endif
#define INK_RES_F_VC 0x00000001       /*%< socket is TCP */
#define INK_RES_F_CONN 0x00000002     /*%< socket is connected */
#define INK_RES_F_EDNS0ERR 0x00000004 /*%< EDNS0 caused errors */
#define INK_RES_F__UNUSED 0x00000008  /*%< (unused) */
#define INK_RES_F_LASTMASK 0x000000F0 /*%< ordinal server of last res_nsend */
#define INK_RES_F_LASTSHIFT 4         /*%< bit position of LASTMASK "flag" */
#define INK_RES_GETLAST(res) (((res)._flags & INK_RES_F_LASTMASK) >> INK_RES_F_LASTSHIFT)

/* res_findzonecut2() options */
#define INK_RES_EXHAUSTIVE 0x00000001 /*%< always do all queries */
#define INK_RES_IPV4ONLY 0x00000002   /*%< IPv4 only */
#define INK_RES_IPV6ONLY 0x00000004   /*%< IPv6 only */

/*%
 *  * Resolver options (keep these in synch with res_debug.c, please)
 [amc] Most of these are never used. AFAICT it's RECURSE and DEBUG only.
 *   */
#define INK_RES_INIT 0x00000001           /*%< address initialized */
#define INK_RES_DEBUG 0x00000002          /*%< print debug messages */
#define INK_RES_AAONLY 0x00000004         /*%< authoritative answers only (!IMPL)*/
#define INK_RES_USEVC 0x00000008          /*%< use virtual circuit */
#define INK_RES_PRIMARY 0x00000010        /*%< query primary server only (!IMPL) */
#define INK_RES_IGNTC 0x00000020          /*%< ignore trucation errors */
#define INK_RES_RECURSE 0x00000040        /*%< recursion desired */
#define INK_RES_DEFNAMES 0x00000080       /*%< use default domain name */
#define INK_RES_STAYOPEN 0x00000100       /*%< Keep TCP socket open */
#define INK_RES_DNSRCH 0x00000200         /*%< search up local domain tree */
#define INK_RES_INSECURE1 0x00000400      /*%< type 1 security disabled */
#define INK_RES_INSECURE2 0x00000800      /*%< type 2 security disabled */
#define INK_RES_NOALIASES 0x00001000      /*%< shuts off HOSTALIASES feature */
#define INK_RES_USE_INET6 0x00002000      /*%< use/map IPv6 in gethostbyname() */
#define INK_RES_ROTATE 0x00004000         /*%< rotate ns list after each query */
#define INK_RES_NOCHECKNAME 0x00008000    /*%< do not check names for sanity. */
#define INK_RES_KEEPTSIG 0x00010000       /*%< do not strip TSIG records */
#define INK_RES_BLAST 0x00020000          /*%< blast all recursive servers */
#define INK_RES_NSID 0x00040000           /*%< request name server ID */
#define INK_RES_NOTLDQUERY 0x00100000     /*%< don't unqualified name as a tld */
#define INK_RES_USE_DNSSEC 0x00200000     /*%< use DNSSEC using OK bit in OPT */
/* #define INK_RES_DEBUG2   0x00400000 */ /* nslookup internal */
/* KAME extensions: use higher bit to avoid conflict with ISC use */
#define INK_RES_USE_DNAME 0x10000000 /*%< use DNAME */
#define INK_RES_USE_EDNS0 0x40000000 /*%< use EDNS0 if configured */

#define INK_RES_DEFAULT (INK_RES_RECURSE | INK_RES_DEFNAMES | INK_RES_DNSRCH)

#define INK_MAXNS 32           /*%< max # name servers we'll track */
#define INK_MAXDFLSRCH 3       /*%< # default domain levels to try */
#define INK_MAXDNSRCH 6        /*%< max # domains in search path */
#define INK_LOCALDOMAINPARTS 2 /*%< min levels in name that is "local" */
#define INK_RES_TIMEOUT 5      /*%< min. seconds between retries */
#define INK_RES_TIMEOUT 5      /*%< min. seconds between retries */
#define INK_RES_MAXNDOTS 15    /*%< should reflect bit field size */
#define INK_RES_MAXRETRANS 30  /*%< only for resolv.conf/RES_OPTIONS */
#define INK_RES_MAXRETRY 5     /*%< only for resolv.conf/RES_OPTIONS */
#define INK_RES_DFLRETRY 2     /*%< Default #/tries. */
#define INK_RES_MAXTIME 65535  /*%< Infinity, in milliseconds. */

#define INK_NS_TYPE_ELT 0x40 /*%< EDNS0 extended label type */
#define INK_DNS_LABELTYPE_BITSTRING 0x41

/// IP family preference for DNS resolution.
/// Used for configuration.
enum HostResPreference {
  HOST_RES_PREFER_NONE = 0, ///< Invalid / init value.
  HOST_RES_PREFER_CLIENT,   ///< Prefer family of client connection.
  HOST_RES_PREFER_IPV4,     ///< Prefer IPv4.
  HOST_RES_PREFER_IPV6      ///< Prefer IPv6
};
/// # of preference values.
static int const N_HOST_RES_PREFERENCE = HOST_RES_PREFER_IPV6 + 1;
/// # of entries in a preference ordering.
static int const N_HOST_RES_PREFERENCE_ORDER = 3;
/// Storage for preference ordering.
typedef HostResPreference HostResPreferenceOrder[N_HOST_RES_PREFERENCE_ORDER];
/// Global, hard wired default value for preference ordering.
extern HostResPreferenceOrder const HOST_RES_DEFAULT_PREFERENCE_ORDER;
/// Global (configurable) default.
extern HostResPreferenceOrder host_res_default_preference_order;
/// String versions of @c FamilyPreference
extern const char *const HOST_RES_PREFERENCE_STRING[N_HOST_RES_PREFERENCE];

/// IP family to use in a DNS query for a host address.
/// Used during DNS query operations.
enum HostResStyle {
  HOST_RES_NONE = 0,  ///< No preference / unspecified / init value.
  HOST_RES_IPV4,      ///< Use IPv4 if possible.
  HOST_RES_IPV4_ONLY, ///< Resolve on IPv4 addresses.
  HOST_RES_IPV6,      ///< Use IPv6 if possible.
  HOST_RES_IPV6_ONLY  ///< Resolve only IPv6 addresses.
};

/// Strings for host resolution styles
extern const char *const HOST_RES_STYLE_STRING[];

/// Caclulate the effective resolution preferences.
extern HostResStyle ats_host_res_from(int family,            ///< Connection family
                                      HostResPreferenceOrder ///< Preference ordering.
);
/// Calculate the host resolution style to force a family match to @a addr.
extern HostResStyle ats_host_res_match(sockaddr const *addr);

/** Parse a host resolution configuration string.
 */
extern void parse_host_res_preference(const char *value,           ///< [in] Configuration string.
                                      HostResPreferenceOrder order /// [out] Order to update.
);

#ifndef NS_GET16
#define NS_GET16(s, cp)                                                  \
  do {                                                                   \
    const u_char *t_cp = (const u_char *)(cp);                           \
    (s)                = ((uint16_t)t_cp[0] << 8) | ((uint16_t)t_cp[1]); \
    (cp) += NS_INT16SZ;                                                  \
  } while (0)
#endif

#ifndef NS_GET32
#define NS_GET32(l, cp)                                                                                                          \
  do {                                                                                                                           \
    const u_char *t_cp = (const u_char *)(cp);                                                                                   \
    (l)                = ((uint32_t)t_cp[0] << 24) | ((uint32_t)t_cp[1] << 16) | ((uint32_t)t_cp[2] << 8) | ((uint32_t)t_cp[3]); \
    (cp) += NS_INT32SZ;                                                                                                          \
  } while (0)
#endif

#ifndef NS_PUT16
#define NS_PUT16(s, cp)            \
  do {                             \
    uint16_t t_s = (uint16_t)(s);  \
    u_char *t_cp = (u_char *)(cp); \
    *t_cp++      = t_s >> 8;       \
    *t_cp        = t_s;            \
    (cp) += NS_INT16SZ;            \
  } while (0)
#endif

#ifndef NS_PUT32
#define NS_PUT32(l, cp)            \
  do {                             \
    uint32_t t_l = (uint32_t)(l);  \
    u_char *t_cp = (u_char *)(cp); \
    *t_cp++      = t_l >> 24;      \
    *t_cp++      = t_l >> 16;      \
    *t_cp++      = t_l >> 8;       \
    *t_cp        = t_l;            \
    (cp) += NS_INT32SZ;            \
  } while (0)
#endif

// Do we really need these to be C compatible? - AMC
struct ts_imp_res_state {
  int retrans; /*%< retransmission time interval */
  int retry;   /*%< number of times to retransmit */
#ifdef sun
  unsigned options; /*%< option flags - see below. */
#else
  u_long options; /*%< option flags - see below. */
#endif
  int nscount;                       /*%< number of name servers */
  IpEndpoint nsaddr_list[INK_MAXNS]; /*%< address of name server */
  u_short id;                        /*%< current message id */
  char *dnsrch[MAXDNSRCH + 1];       /*%< components of domain to search */
  char defdname[256];                /*%< default domain (deprecated) */
#ifdef sun
  unsigned pfcode; /*%< RES_PRF_ flags - see below. */
#else
  u_long pfcode;  /*%< RES_PRF_ flags - see below. */
#endif
  unsigned ndots : 4; /*%< threshold for initial abs. query */
  unsigned nsort : 4; /*%< number of elements in sort_list[] */
  char unused[3];
  int res_h_errno;              /*%< last one set for this context */
  int _vcsock;                  /*%< PRIVATE: for res_send VC i/o */
  unsigned _flags;              /*%< PRIVATE: see below */
  unsigned _pad;                /*%< make _u 64 bit aligned */
  uint16_t _nstimes[INK_MAXNS]; /*%< ms. */
};
typedef ts_imp_res_state *ink_res_state;

int ink_res_init(ink_res_state, IpEndpoint const *pHostList, size_t pHostListSize, int dnsSearch, const char *pDefDomain = nullptr,
                 const char *pSearchList = nullptr, const char *pResolvConf = nullptr);

int ink_res_mkquery(ink_res_state, int, const char *, int, int, const unsigned char *, int, const unsigned char *, unsigned char *,
                    int);

int ink_ns_name_ntop(const u_char *src, char *dst, size_t dstsiz);

/** Initialize global values for HttpProxyPort / Host Resolution.
 */
void ts_host_res_global_init();

/** Generate a string representation of a host resolution preference ordering.
    @return The length of the string.
 */
int ts_host_res_order_to_string(HostResPreferenceOrder const &order, ///< order to print
                                char *out,                           ///< Target buffer for string.
                                int size                             ///< Size of buffer.
);
