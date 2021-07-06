/*
 * Copyright (c) 1985, 1993
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
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
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

#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"

#include <sys/types.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#ifdef HAVE_ARPA_NAMESER_COMPAT_H
#include <arpa/nameser_compat.h>
#endif
#include <netdb.h>
#include <resolv.h>
#include <cstdio>
#include <cstring>

#include "tscore/ink_error.h"
#include "tscore/ink_resolver.h"

#define SPRINTF(x) (sprintf x)

/*%
 * Form all types of queries.
 * Returns the size of the result or -1.
 */
int
ink_res_mkquery(ink_res_state statp, int op,               /*!< opcode of query  */
                const char *dname,                         /*!< domain name  */
                int _class, int type,                      /*!< _class and type of query  */
                const u_char *data,                        /*!< resource record data  */
                int datalen,                               /*!< length of data  */
                const u_char * /* newrr_in  ATS_UNUSED */, /*!< new rr for modify or append  */
                u_char *buf,                               /*!< buffer to put query  */
                int buflen)                                /*!< size of buffer  */
{
  HEADER *hp;
  u_char *cp, *ep;
  int n;
  u_char *dnptrs[20], **dpp, **lastdnptr;

  /*
   * Initialize header fields.
   */
  if ((buf == nullptr) || (buflen < HFIXEDSZ)) {
    return (-1);
  }
  memset(buf, 0, HFIXEDSZ);
  hp         = reinterpret_cast<HEADER *>(buf);
  hp->id     = htons(++statp->id);
  hp->opcode = op;
  hp->rd     = (statp->options & INK_RES_RECURSE) != 0U;
  hp->rcode  = NOERROR;
  cp         = buf + HFIXEDSZ;
  ep         = buf + buflen;
  dpp        = dnptrs;
  *dpp++     = buf;
  *dpp++     = nullptr;
  lastdnptr  = dnptrs + sizeof dnptrs / sizeof dnptrs[0];
  /*
   * perform opcode specific processing
   */
  switch (op) {
  case QUERY: /*FALLTHROUGH*/
  case NS_NOTIFY_OP:
    if (ep - cp < QFIXEDSZ) {
      return (-1);
    }
    if ((n = dn_comp(dname, cp, ep - cp - QFIXEDSZ, dnptrs, lastdnptr)) < 0) {
      return (-1);
    }
    cp += n;
    NS_PUT16(type, cp);
    NS_PUT16(_class, cp);
    hp->qdcount = htons(1);
    if (op == QUERY || data == nullptr) {
      break;
    }
    /*
     * Make an additional record for completion domain.
     */
    if ((ep - cp) < RRFIXEDSZ) {
      return (-1);
    }
    n = dn_comp(reinterpret_cast<const char *>(data), cp, ep - cp - RRFIXEDSZ, dnptrs, lastdnptr);
    if (n < 0) {
      return (-1);
    }
    cp += n;
    NS_PUT16(T_NULL, cp);
    NS_PUT16(_class, cp);
    NS_PUT32(0, cp);
    NS_PUT16(0, cp);
    hp->arcount = htons(1);
    break;

  case IQUERY:
    /*
     * Initialize answer section
     */
    if (ep - cp < 1 + RRFIXEDSZ + datalen) {
      return (-1);
    }
    *cp++ = '\0'; /*%< no domain name */
    NS_PUT16(type, cp);
    NS_PUT16(_class, cp);
    NS_PUT32(0, cp);
    NS_PUT16(datalen, cp);
    if (datalen) {
      memcpy(cp, data, datalen);
      cp += datalen;
    }
    hp->ancount = htons(1);
    break;

  default:
    return (-1);
  }
  return (cp - buf);
}

/* Public. */

/*%
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	is this character visible and not a space when printed ?
 *
 * return:
 *\li	boolean.
 */
static int
printable(int ch)
{
  return (ch > 0x20 && ch < 0x7f);
}

static const char digits[] = "0123456789";

static int
labellen(const u_char *lp)
{
  int bitlen;
  u_char l = *lp;

  if ((l & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
    /* should be avoided by the caller */
    return (-1);
  }

  if ((l & NS_CMPRSFLGS) == INK_NS_TYPE_ELT) {
    if (l == INK_DNS_LABELTYPE_BITSTRING) {
      if ((bitlen = *(lp + 1)) == 0) {
        bitlen = 256;
      }
      return ((bitlen + 7) / 8 + 1);
    }
    return (-1); /*%< unknown ELT */
  }
  return (l);
}

static int
decode_bitstring(const unsigned char **cpp, char *dn, const char *eom)
{
  const unsigned char *cp = *cpp;
  char *beg               = dn, tc;
  int b, blen, plen, i;

  if ((blen = (*cp & 0xff)) == 0) {
    blen = 256;
  }
  plen = (blen + 3) / 4;
  plen += sizeof("\\[x/]") + (blen > 99 ? 3 : (blen > 9) ? 2 : 1);
  if (dn + plen >= eom) {
    return (-1);
  }

  cp++;
  i = SPRINTF((dn, "\\[x"));
  if (i < 0) {
    return (-1);
  }
  dn += i;
  for (b = blen; b > 7; b -= 8, cp++) {
    i = SPRINTF((dn, "%02x", *cp & 0xff));
    if (i < 0) {
      return (-1);
    }
    dn += i;
  }
  if (b > 4) {
    tc = *cp++;
    i  = SPRINTF((dn, "%02x", tc & (0xff << (8 - b))));
    if (i < 0) {
      return (-1);
    }
    dn += i;
  } else if (b > 0) {
    tc = *cp++;
    i  = SPRINTF((dn, "%1x", ((tc >> 4) & 0x0f) & (0x0f << (4 - b))));
    if (i < 0) {
      return (-1);
    }
    dn += i;
  }
  i = SPRINTF((dn, "/%d]", blen));
  if (i < 0) {
    return (-1);
  }
  dn += i;

  *cpp = cp;
  return (dn - beg);
}

/*%
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	is this character special ("in need of quoting") ?
 *
 * return:
 *\li	boolean.
 */
static int
special(int ch)
{
  switch (ch) {
  case 0x22: /*%< '"' */
  case 0x2E: /*%< '.' */
  case 0x3B: /*%< ';' */
  case 0x5C: /*%< '\\' */
  case 0x28: /*%< '(' */
  case 0x29: /*%< ')' */
  /* Special modifiers in zone files. */
  case 0x40: /*%< '@' */
  case 0x24: /*%< '$' */
    return (1);
  default:
    return (0);
  }
}

/*%
 *	Convert an encoded domain name to printable ascii as per RFC1035.

 * return:
 *\li	Number of bytes written to buffer, or -1 (with errno set)
 *
 * notes:
 *\li	The root is returned as "."
 *\li	All other domains are returned in non absolute form
 */
int
ink_ns_name_ntop(const u_char *src, char *dst, size_t dstsiz)
{
  const u_char *cp;
  char *dn, *eom;
  u_char c;
  unsigned n;
  int l;

  cp  = src;
  dn  = dst;
  eom = dst + dstsiz;

  while ((n = *cp++) != 0) {
    if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
      /* Some kind of compression pointer. */
      errno = EMSGSIZE;
      return (-1);
    }
    if (dn != dst) {
      if (dn >= eom) {
        errno = EMSGSIZE;
        return (-1);
      }
      *dn++ = '.';
    }
    if ((l = labellen(cp - 1)) < 0) {
      errno = EMSGSIZE; /*%< XXX */
      return (-1);
    }
    if (dn + l >= eom) {
      errno = EMSGSIZE;
      return (-1);
    }
    if ((n & NS_CMPRSFLGS) == INK_NS_TYPE_ELT) {
      int m;

      if (n != INK_DNS_LABELTYPE_BITSTRING) {
        /* XXX: labellen should reject this case */
        errno = EINVAL;
        return (-1);
      }
      if ((m = decode_bitstring(&cp, dn, eom)) < 0) {
        errno = EMSGSIZE;
        return (-1);
      }
      dn += m;
      continue;
    }
    for ((void)nullptr; l > 0; l--) {
      c = *cp++;
      if (special(c)) {
        if (dn + 1 >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = '\\';
        *dn++ = static_cast<char>(c);
      } else if (!printable(c)) {
        if (dn + 3 >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = '\\';
        *dn++ = digits[c / 100];
        *dn++ = digits[(c % 100) / 10];
        *dn++ = digits[c % 10];
      } else {
        if (dn >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = static_cast<char>(c);
      }
    }
  }
  if (dn == dst) {
    if (dn >= eom) {
      errno = EMSGSIZE;
      return (-1);
    }
    *dn++ = '.';
  }
  if (dn >= eom) {
    errno = EMSGSIZE;
    return (-1);
  }
  *dn++ = '\0';
  return (dn - dst);
}

/*%
 *	Convert an encoded domain name to printable ascii as per RFC1035.

 * return:
 *\li	Number of bytes written to buffer, or -1 (with errno set)
 *
 * notes:
 *\li	The root is returned as "."
 *\li	All other domains are returned in non absolute form
 */
#if defined(linux)
int
ns_name_ntop(const u_char *src, char *dst, size_t dstsiz) __THROW
#else
int
ns_name_ntop(const u_char *src, char *dst, size_t dstsiz)
#endif
{
  const u_char *cp;
  char *dn, *eom;
  u_char c;
  unsigned n;
  int l;

  cp  = src;
  dn  = dst;
  eom = dst + dstsiz;

  while ((n = *cp++) != 0) {
    if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
      /* Some kind of compression pointer. */
      errno = EMSGSIZE;
      return (-1);
    }
    if (dn != dst) {
      if (dn >= eom) {
        errno = EMSGSIZE;
        return (-1);
      }
      *dn++ = '.';
    }
    if ((l = labellen(cp - 1)) < 0) {
      errno = EMSGSIZE; /*%< XXX */
      return (-1);
    }
    if (dn + l >= eom) {
      errno = EMSGSIZE;
      return (-1);
    }
    if ((n & NS_CMPRSFLGS) == INK_NS_TYPE_ELT) {
      int m;

      if (n != INK_DNS_LABELTYPE_BITSTRING) {
        /* XXX: labellen should reject this case */
        errno = EINVAL;
        return (-1);
      }
      if ((m = decode_bitstring(&cp, dn, eom)) < 0) {
        errno = EMSGSIZE;
        return (-1);
      }
      dn += m;
      continue;
    }
    for ((void)nullptr; l > 0; l--) {
      c = *cp++;
      if (special(c)) {
        if (dn + 1 >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = '\\';
        *dn++ = static_cast<char>(c);
      } else if (!printable(c)) {
        if (dn + 3 >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = '\\';
        *dn++ = digits[c / 100];
        *dn++ = digits[(c % 100) / 10];
        *dn++ = digits[c % 10];
      } else {
        if (dn >= eom) {
          errno = EMSGSIZE;
          return (-1);
        }
        *dn++ = static_cast<char>(c);
      }
    }
  }
  if (dn == dst) {
    if (dn >= eom) {
      errno = EMSGSIZE;
      return (-1);
    }
    *dn++ = '.';
  }
  if (dn >= eom) {
    errno = EMSGSIZE;
    return (-1);
  }
  *dn++ = '\0';
  return (dn - dst);
}

HostResStyle
ats_host_res_from(int family, HostResPreferenceOrder const &order)
{
  bool v4 = false, v6 = false;
  HostResPreference client = AF_INET6 == family ? HOST_RES_PREFER_IPV6 : HOST_RES_PREFER_IPV4;

  for (auto p : order) {
    if (HOST_RES_PREFER_CLIENT == p) {
      p = client; // CLIENT -> actual value
    }
    if (HOST_RES_PREFER_IPV4 == p) {
      if (v6) {
        return HOST_RES_IPV6;
      } else {
        v4 = true;
      }
    } else if (HOST_RES_PREFER_IPV6 == p) {
      if (v4) {
        return HOST_RES_IPV4;
      } else {
        v6 = true;
      }
    } else {
      break;
    }
  }
  if (v4) {
    return HOST_RES_IPV4_ONLY;
  } else if (v6) {
    return HOST_RES_IPV6_ONLY;
  }
  return HOST_RES_NONE;
}

void
ats_force_order_by_family(sockaddr const *addr, HostResPreferenceOrder order)
{
  HostResPreferenceOrder::size_type pos{0};
  if (ats_is_ip6(addr)) {
    order[pos++] = HOST_RES_PREFER_IPV6;
  } else if (ats_is_ip4(addr)) {
    order[pos++] = HOST_RES_PREFER_IPV4;
  }
  for (; pos < order.size(); pos++) {
    order[pos] = HOST_RES_PREFER_NONE;
  }
}
