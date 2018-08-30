/*
 * Copyright (c) 1985, 1989, 1993
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

#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#ifdef HAVE_ARPA_NAMESER_COMPAT_H
#include <arpa/nameser_compat.h>
#endif
#include <cstdio>
#include <cctype>
#include <resolv.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include "tscore/ink_string.h"
#include "tscore/ink_resolver.h"
#include "tscore/ink_inet.h"
#include "tscore/Tokenizer.h"

#if !defined(isascii) /* XXX - could be a function */
#define isascii(c) (!(c & 0200))
#endif

HostResPreferenceOrder const HOST_RES_DEFAULT_PREFERENCE_ORDER = {HOST_RES_PREFER_IPV4, HOST_RES_PREFER_IPV6, HOST_RES_PREFER_NONE};

HostResPreferenceOrder host_res_default_preference_order;

const char *const HOST_RES_PREFERENCE_STRING[N_HOST_RES_PREFERENCE] = {"only", "client", "ipv4", "ipv6"};

const char *const HOST_RES_STYLE_STRING[] = {"invalid", "IPv4", "IPv4 only", "IPv6", "IPv6 only"};

/*%
 * This routine is for closing the socket if a virtual circuit is used and
 * the program wants to close it.  This provides support for endhostent()
 * which expects to close the socket.
 *
 * This routine is not expected to be user visible.
 */
static void
ink_res_nclose(ink_res_state statp)
{
  if (statp->_vcsock >= 0) {
    (void)close(statp->_vcsock);
    statp->_vcsock = -1;
    statp->_flags &= ~(INK_RES_F_VC | INK_RES_F_CONN);
  }
}

static void
ink_res_setservers(ink_res_state statp, IpEndpoint const *set, int cnt)
{
  /* close open servers */
  ink_res_nclose(statp);

  /* cause rtt times to be forgotten */
  statp->nscount = 0;

  /* The goal here seems to be to compress the source list (@a set) by
     squeezing out invalid addresses. We handle the special case where
     the destination and sourcea are the same.
  */
  int nserv = 0;
  for (IpEndpoint const *limit = set + cnt; nserv < INK_MAXNS && set < limit; ++set) {
    IpEndpoint *dst = &statp->nsaddr_list[nserv];

    if (dst == set) {
      if (ats_is_ip(&set->sa)) {
        ++nserv;
      }
    } else if (ats_ip_copy(&dst->sa, &set->sa)) {
      ++nserv;
    }
  }
  statp->nscount = nserv;
}

int
ink_res_getservers(ink_res_state statp, sockaddr *set, int cnt)
{
  int zret              = 0; // return count.
  IpEndpoint const *src = statp->nsaddr_list;

  for (int i = 0; i < statp->nscount && i < cnt; ++i, ++src) {
    if (ats_ip_copy(set, &src->sa)) {
      ++set;
      ++zret;
    }
  }
  return zret;
}

static void
ink_res_setoptions(ink_res_state statp, const char *options, const char *source ATS_UNUSED)
{
  const char *cp = options;
  int i;

#ifdef DEBUG
  if (statp->options & INK_RES_DEBUG)
    printf(";; res_setoptions(\"%s\", \"%s\")...\n", options, source);
#endif
  while (*cp) {
    /* skip leading and inner runs of spaces */
    while (*cp == ' ' || *cp == '\t') {
      cp++;
    }
    /* search for and process individual options */
    if (!strncmp(cp, "ndots:", sizeof("ndots:") - 1)) {
      i = atoi(cp + sizeof("ndots:") - 1);
      if (i <= INK_RES_MAXNDOTS) {
        statp->ndots = i;
      } else {
        statp->ndots = INK_RES_MAXNDOTS;
      }
#ifdef DEBUG
      if (statp->options & INK_RES_DEBUG)
        printf(";;\tndots=%d\n", statp->ndots);
#endif
    } else if (!strncmp(cp, "timeout:", sizeof("timeout:") - 1)) {
      i = atoi(cp + sizeof("timeout:") - 1);
      if (i <= INK_RES_MAXRETRANS) {
        statp->retrans = i;
      } else {
        statp->retrans = INK_RES_MAXRETRANS;
      }
#ifdef DEBUG
      if (statp->options & INK_RES_DEBUG)
        printf(";;\ttimeout=%d\n", statp->retrans);
#endif
#ifdef SOLARIS2
    } else if (!strncmp(cp, "retrans:", sizeof("retrans:") - 1)) {
      /*
       * For backward compatibility, 'retrans' is
       * supported as an alias for 'timeout', though
       * without an imposed maximum.
       */
      statp->retrans = atoi(cp + sizeof("retrans:") - 1);
    } else if (!strncmp(cp, "retry:", sizeof("retry:") - 1)) {
      /*
       * For backward compatibility, 'retry' is
       * supported as an alias for 'attempts', though
       * without an imposed maximum.
       */
      statp->retry = atoi(cp + sizeof("retry:") - 1);
#endif /* SOLARIS2 */
    } else if (!strncmp(cp, "attempts:", sizeof("attempts:") - 1)) {
      i = atoi(cp + sizeof("attempts:") - 1);
      if (i <= INK_RES_MAXRETRY) {
        statp->retry = i;
      } else {
        statp->retry = INK_RES_MAXRETRY;
      }
#ifdef DEBUG
      if (statp->options & INK_RES_DEBUG)
        printf(";;\tattempts=%d\n", statp->retry);
#endif
    } else if (!strncmp(cp, "debug", sizeof("debug") - 1)) {
#ifdef DEBUG
      if (!(statp->options & INK_RES_DEBUG)) {
        printf(";; res_setoptions(\"%s\", \"%s\")..\n", options, source);
        statp->options |= INK_RES_DEBUG;
      }
      printf(";;\tdebug\n");
#endif
    } else if (!strncmp(cp, "no_tld_query", sizeof("no_tld_query") - 1) ||
               !strncmp(cp, "no-tld-query", sizeof("no-tld-query") - 1)) {
      statp->options |= INK_RES_NOTLDQUERY;
    } else if (!strncmp(cp, "inet6", sizeof("inet6") - 1)) {
      statp->options |= INK_RES_USE_INET6;
    } else if (!strncmp(cp, "rotate", sizeof("rotate") - 1)) {
      statp->options |= INK_RES_ROTATE;
    } else if (!strncmp(cp, "no-check-names", sizeof("no-check-names") - 1)) {
      statp->options |= INK_RES_NOCHECKNAME;
    }
#ifdef INK_RES_USE_EDNS0
    else if (!strncmp(cp, "edns0", sizeof("edns0") - 1)) {
      statp->options |= INK_RES_USE_EDNS0;
    }
#endif
    else if (!strncmp(cp, "dname", sizeof("dname") - 1)) {
      statp->options |= INK_RES_USE_DNAME;
    } else {
      /* XXX - print a warning here? */
    }
    /* skip to next run of spaces */
    while (*cp && *cp != ' ' && *cp != '\t') {
      cp++;
    }
  }
}

static unsigned
ink_res_randomid()
{
  struct timeval now;

  gettimeofday(&now, nullptr);
  return (0xffff & (now.tv_sec ^ now.tv_usec ^ getpid()));
}

/*%
 * Set up default settings.  If the configuration file exist, the values
 * there will have precedence.  Otherwise, the server address is set to
 * INADDR_ANY and the default domain name comes from the gethostname().
 *
 * An interrim version of this code (BIND 4.9, pre-4.4BSD) used 127.0.0.1
 * rather than INADDR_ANY ("0.0.0.0") as the default name server address
 * since it was noted that INADDR_ANY actually meant ``the first interface
 * you "ifconfig"'d at boot time'' and if this was a SLIP or PPP interface,
 * it had to be "up" in order for you to reach your own name server.  It
 * was later decided that since the recommended practice is to always
 * install local static routes through 127.0.0.1 for all your network
 * interfaces, that we could solve this problem without a code change.
 *
 * The configuration file should always be used, since it is the only way
 * to specify a default domain.  If you are running a server on your local
 * machine, you should say "nameserver 0.0.0.0" or "nameserver 127.0.0.1"
 * in the configuration file.
 *
 * Return 0 if completes successfully, -1 on error
 *
 * @internal This function has to be reachable by res_data.c but not publicly.
 */
int
ink_res_init(ink_res_state statp,         ///< State object to update.
             IpEndpoint const *pHostList, ///< Additional servers.
             size_t pHostListSize,        ///< # of entries in @a pHostList.
             int dnsSearch,               /// Option of search_default_domains.
             const char *pDefDomain,      ///< Default domain (may be nullptr).
             const char *pSearchList,     ///< Unknown
             const char *pResolvConf      ///< Path to configuration file.
)
{
  FILE *fp;
  char *cp, **pp;
  int n;
  char buf[BUFSIZ];
  size_t nserv   = 0;
  int haveenv    = 0;
  int havesearch = 0;
  int dots;
  size_t maxns = INK_MAXNS;

  // INK_RES_SET_H_ERRNO(statp, 0);
  statp->res_h_errno = 0;

  statp->retrans = INK_RES_TIMEOUT;
  statp->retry   = INK_RES_DFLRETRY;
  statp->options = INK_RES_DEFAULT;
  statp->id      = ink_res_randomid();

  statp->nscount = 0;
  statp->ndots   = 1;
  statp->pfcode  = 0;
  statp->_vcsock = -1;
  statp->_flags  = 0;

#ifdef SOLARIS2
  /*
   * The old libresolv derived the defaultdomain from NIS/NIS+.
   * We want to keep this behaviour
   */
  {
    char buf[sizeof(statp->defdname)], *cp;
    int ret;

    if ((ret = sysinfo(SI_SRPC_DOMAIN, buf, sizeof(buf))) > 0 && (unsigned int)ret <= sizeof(buf)) {
      if (buf[0] == '+')
        buf[0] = '.';
      cp = strchr(buf, '.');
      cp = (cp == nullptr) ? buf : (cp + 1);
      ink_strlcpy(statp->defdname, cp, sizeof(statp->defdname));
    }
  }
#endif /* SOLARIS2 */

  /* Allow user to override the local domain definition */
  if ((cp = getenv("LOCALDOMAIN")) != nullptr) {
    (void)ink_strlcpy(statp->defdname, cp, sizeof(statp->defdname));
    haveenv++;

    /*
     * Set search list to be blank-separated strings
     * from rest of env value.  Permits users of LOCALDOMAIN
     * to still have a search list, and anyone to set the
     * one that they want to use as an individual (even more
     * important now that the rfc1535 stuff restricts searches)
     */
    cp    = statp->defdname;
    pp    = statp->dnsrch;
    *pp++ = cp;
    for (n = 0; *cp && pp < statp->dnsrch + INK_MAXDNSRCH; cp++) {
      if (*cp == '\n') { /*%< silly backwards compat */
        break;
      } else if (*cp == ' ' || *cp == '\t') {
        *cp = 0;
        n   = 1;
      } else if (n) {
        *pp++      = cp;
        n          = 0;
        havesearch = 1;
      }
    }
    /* null terminate last domain if there are excess */
    while (*cp != '\0' && *cp != ' ' && *cp != '\t' && *cp != '\n') {
      cp++;
    }
    *cp   = '\0';
    *pp++ = nullptr;
  }

  /* ---------------------------------------------
     Default domain name and doamin Search list:

     if we are supplied a default domain name,
     and/or search list we will use it. Otherwise,
     we will skip to using  what is present in the
     conf file
     ---------------------------------------------- */

  if (pDefDomain && '\0' != *pDefDomain && '\n' != *pDefDomain) {
    ink_strlcpy(statp->defdname, pDefDomain, sizeof(statp->defdname));
    if ((cp = strpbrk(statp->defdname, " \t\n")) != nullptr) {
      *cp = '\0';
    }
  }
  if (pSearchList && '\0' != *pSearchList && '\n' != *pSearchList) {
    ink_strlcpy(statp->defdname, pSearchList, sizeof(statp->defdname));
    if ((cp = strchr(statp->defdname, '\n')) != nullptr) {
      *cp = '\0';
      /*
       * Set search list to be blank-separated strings
       * on rest of line.
       */
    }
    cp    = statp->defdname;
    pp    = statp->dnsrch;
    *pp++ = cp;
    for (n = 0; *cp && pp < statp->dnsrch + INK_MAXDNSRCH; cp++) {
      if (*cp == ' ' || *cp == '\t') {
        *cp = 0;
        n   = 1;
      } else if (n) {
        *pp++ = cp;
        n     = 0;
      }
    }
    /* null terminate last domain if there are excess */
    while (*cp != '\0' && *cp != ' ' && *cp != '\t') {
      cp++;
    }
    *cp        = '\0';
    *pp++      = nullptr;
    havesearch = 1;
  }

  /* -------------------------------------------
     we must be provided with atleast a named!
     ------------------------------------------- */
  if (pHostList) {
    if (pHostListSize > INK_MAXNS) {
      pHostListSize = INK_MAXNS;
    }
    for (; nserv < pHostListSize && ats_is_ip(&pHostList[nserv].sa); ++nserv) {
      ats_ip_copy(&statp->nsaddr_list[nserv].sa, &pHostList[nserv].sa);
    }
  }

#define MATCH(line, name) \
  (!strncmp(line, name, sizeof(name) - 1) && (line[sizeof(name) - 1] == ' ' || line[sizeof(name) - 1] == '\t'))

  if (pResolvConf && ((fp = fopen(pResolvConf, "r")) != nullptr)) {
    /* read the config file */
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
      /* skip comments */
      if (*buf == ';' || *buf == '#') {
        continue;
      }
      /* read default domain name */
      if (MATCH(buf, "domain")) {
        if (haveenv) { /*%< skip if have from environ */
          continue;
        }
        cp = buf + sizeof("domain") - 1;
        while (*cp == ' ' || *cp == '\t') {
          cp++;
        }
        if ((*cp == '\0') || (*cp == '\n')) {
          continue;
        }
        ink_strlcpy(statp->defdname, cp, sizeof(statp->defdname));
        if ((cp = strpbrk(statp->defdname, " \t\n")) != nullptr) {
          *cp = '\0';
        }
        havesearch = 0;
        continue;
      }
      /* set search list */
      if (MATCH(buf, "search")) {
        if (haveenv) { /*%< skip if have from environ */
          continue;
        }
        cp = buf + sizeof("search") - 1;
        while (*cp == ' ' || *cp == '\t') {
          cp++;
        }
        if ((*cp == '\0') || (*cp == '\n')) {
          continue;
        }
        ink_strlcpy(statp->defdname, cp, sizeof(statp->defdname));
        if ((cp = strchr(statp->defdname, '\n')) != nullptr) {
          *cp = '\0';
          /*
           * Set search list to be blank-separated strings
           * on rest of line.
           */
        }
        cp    = statp->defdname;
        pp    = statp->dnsrch;
        *pp++ = cp;
        for (n = 0; *cp && pp < statp->dnsrch + INK_MAXDNSRCH; cp++) {
          if (*cp == ' ' || *cp == '\t') {
            *cp = 0;
            n   = 1;
          } else if (n) {
            *pp++ = cp;
            n     = 0;
          }
        }
        /* null terminate last domain if there are excess */
        while (*cp != '\0' && *cp != ' ' && *cp != '\t') {
          cp++;
        }
        *cp        = '\0';
        *pp++      = nullptr;
        havesearch = 1;
        continue;
      }
      /* read nameservers to query */
      if (MATCH(buf, "nameserver") && nserv < maxns) {
        cp = buf + sizeof("nameserver") - 1;
        while (*cp == ' ' || *cp == '\t') {
          cp++;
        }
        if ((*cp != '\0') && (*cp != '\n')) {
          std::string_view host(cp, strcspn(cp, ";# \t\n"));
          if (0 == ats_ip_pton(host, &statp->nsaddr_list[nserv].sa)) {
            // If there was no port in the config, lets use NAMESERVER_PORT
            if (ats_ip_port_host_order(&statp->nsaddr_list[nserv].sa) == 0) {
              ats_ip_port_cast(&statp->nsaddr_list[nserv].sa) = htons(NAMESERVER_PORT);
            }
            ++nserv;
          }
        }
        continue;
      }
      if (MATCH(buf, "options")) {
        ink_res_setoptions(statp, buf + sizeof("options") - 1, "conf");
        continue;
      }
    }
    (void)fclose(fp);
  }

  if (nserv > 0) {
    statp->nscount = nserv;
  }

  if (statp->defdname[0] == 0 && gethostname(buf, sizeof(statp->defdname) - 1) == 0 && (cp = strchr(buf, '.')) != nullptr) {
    ink_strlcpy(statp->defdname, cp + 1, sizeof(statp->defdname));
  }

  /* find components of local domain that might be searched */
  if (havesearch == 0) {
    pp    = statp->dnsrch;
    *pp++ = statp->defdname;
    *pp   = nullptr;

    if (dnsSearch == 1) {
      dots = 0;
      for (cp = statp->defdname; *cp; cp++) {
        dots += (*cp == '.');
      }

      cp = statp->defdname;
      while (pp < statp->dnsrch + INK_MAXDFLSRCH) {
        if (dots < INK_LOCALDOMAINPARTS) {
          break;
        }
        cp    = strchr(cp, '.') + 1; /*%< we know there is one */
        *pp++ = cp;
        dots--;
      }
      *pp = nullptr;
    }
#ifdef DEBUG
    if (statp->options & INK_RES_DEBUG) {
      printf(";; res_init()... default dnsrch list:\n");
      for (pp = statp->dnsrch; *pp; pp++)
        printf(";;\t%s\n", *pp);
      printf(";;\t..END..\n");
    }
#endif
  }

  /* export all ns servers to DNSprocessor. */
  ink_res_setservers(statp, &statp->nsaddr_list[0], statp->nscount);

  if ((cp = getenv("RES_OPTIONS")) != nullptr) {
    ink_res_setoptions(statp, cp, "env");
  }
  statp->options |= INK_RES_INIT;
  return (statp->res_h_errno);
}

void
parse_host_res_preference(const char *value, HostResPreferenceOrder order)
{
  Tokenizer tokens(";/|");
  // preference from the config string.
  int np = 0;                        // index in to @a m_host_res_preference
  bool found[N_HOST_RES_PREFERENCE]; // redundancy check array
  int n;                             // # of tokens
  int i;                             // index

  n = tokens.Initialize(value);

  for (i = 0; i < N_HOST_RES_PREFERENCE; ++i) {
    found[i] = false;
  }

  for (i = 0; i < n && np < N_HOST_RES_PREFERENCE_ORDER; ++i) {
    const char *elt = tokens[i];
    // special case none/only because that terminates the sequence.
    if (0 == strcasecmp(elt, HOST_RES_PREFERENCE_STRING[HOST_RES_PREFER_NONE])) {
      found[HOST_RES_PREFER_NONE] = true;
      order[np]                   = HOST_RES_PREFER_NONE;
      break;
    } else {
      // scan the other types
      HostResPreference ep = HOST_RES_PREFER_NONE;
      for (int ip = HOST_RES_PREFER_NONE + 1; ip < N_HOST_RES_PREFERENCE; ++ip) {
        if (0 == strcasecmp(elt, HOST_RES_PREFERENCE_STRING[ip])) {
          ep = static_cast<HostResPreference>(ip);
          break;
        }
      }
      if (HOST_RES_PREFER_NONE != ep && !found[ep]) { // ignore duplicates
        found[ep]   = true;
        order[np++] = ep;
      }
    }
  }

  if (!found[HOST_RES_PREFER_NONE]) {
    // If 'only' wasn't explicit, fill in the rest by default.
    if (!found[HOST_RES_PREFER_IPV4]) {
      order[np++] = HOST_RES_PREFER_IPV4;
    }
    if (!found[HOST_RES_PREFER_IPV6]) {
      order[np++] = HOST_RES_PREFER_IPV6;
    }
    if (np < N_HOST_RES_PREFERENCE_ORDER) { // was N_HOST_RES_PREFERENCE)
      order[np] = HOST_RES_PREFER_NONE;
    }
  }
}

int
ts_host_res_order_to_string(HostResPreferenceOrder const &order, char *out, int size)
{
  int zret   = 0;
  bool first = true;
  for (auto i : order) {
    /* Note we use a semi-colon here because this must be compatible
     * with the -httpport command line option which uses comma to
     * separate port descriptors so we cannot use that to separate
     * resolution key words.
     */
    zret += snprintf(out + zret, size - zret, "%s%s", !first ? ";" : "", HOST_RES_PREFERENCE_STRING[i]);
    if (HOST_RES_PREFER_NONE == i) {
      break;
    }
    first = false;
  }
  return zret;
}
