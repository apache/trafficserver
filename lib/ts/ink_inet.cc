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

#include "libts.h"

#if defined(darwin)
extern "C"
{
  struct hostent *gethostbyname_r(const char *name, struct hostent *result, char *buffer, int buflen, int *h_errnop);
  struct hostent *gethostbyaddr_r(const char *name, size_t size, int type,
                                  struct hostent *result, char *buffer, int buflen, int *h_errnop);
}
#endif


struct hostent *
ink_gethostbyname_r(char *hostname, ink_gethostbyname_r_data * data)
{
#ifdef RENTRENT_GETHOSTBYNAME
  struct hostent *r = gethostbyname(hostname);
  if (r)
    data->ent = *r;
  data->herrno = errno;

#else //RENTRENT_GETHOSTBYNAME
#if GETHOSTBYNAME_R_GLIBC2

  struct hostent *addrp = NULL;
  int res = gethostbyname_r(hostname, &data->ent, data->buf,
                            INK_GETHOSTBYNAME_R_DATA_SIZE, &addrp,
                            &data->herrno);
  struct hostent *r = NULL;
  if (!res && addrp)
    r = addrp;

#else
  struct hostent *r = gethostbyname_r(hostname, &data->ent, data->buf,
                                      INK_GETHOSTBYNAME_R_DATA_SIZE,
                                      &data->herrno);
#endif
#endif
  return r;
}

struct hostent *
ink_gethostbyaddr_r(char *ip, int len, int type, ink_gethostbyaddr_r_data * data)
{
#if GETHOSTBYNAME_R_GLIBC2
  struct hostent *r = NULL;
  struct hostent *addrp = NULL;
  int res = gethostbyaddr_r((char *) ip, len, type, &data->ent, data->buf,
                            INK_GETHOSTBYNAME_R_DATA_SIZE, &addrp,
                            &data->herrno);
  if (!res && addrp)
    r = addrp;
#else
#ifdef RENTRENT_GETHOSTBYADDR
  struct hostent *r = gethostbyaddr((const void *) ip, len, type);

#else
  struct hostent *r = gethostbyaddr_r((char *) ip, len, type, &data->ent,
                                      data->buf,
                                      INK_GETHOSTBYNAME_R_DATA_SIZE,
                                      &data->herrno);
#endif
#endif //LINUX
  return r;
}

unsigned int
host_to_ip(char *hostname)
{
  struct hostent *he;

  he = gethostbyname(hostname);
  if (he == NULL)
    return INADDR_ANY;

  return *(unsigned int *) he->h_addr;
}

uint32_t
ink_inet_addr(const char *s)
{
  uint32_t u[4];
  uint8_t *pc = (uint8_t *) s;
  int n = 0;
  uint32_t base = 10;

  while (n < 4) {

    u[n] = 0;
    base = 10;

    // handle hex, octal

    if (*pc == '0') {
      if (*++pc == 'x' || *pc == 'X')
        base = 16, pc++;
      else
        base = 8;
    }
    // handle hex, octal, decimal

    while (*pc) {
      if (ParseRules::is_digit(*pc)) {
        u[n] = u[n] * base + (*pc++ - '0');
        continue;
      }
      if (base == 16 && ParseRules::is_hex(*pc)) {
        u[n] = u[n] * 16 + ParseRules::ink_tolower(*pc++) - 'a' + 10;
        continue;
      }
      break;
    }

    n++;
    if (*pc == '.')
      pc++;
    else
      break;
  }

  if (*pc && !ParseRules::is_wslfcr(*pc))
    return htonl((uint32_t) - 1);

  switch (n) {
  case 1:
    return htonl(u[0]);
  case 2:
    if (u[0] > 0xff || u[1] > 0xffffff)
      return htonl((uint32_t) - 1);
    return htonl((u[0] << 24) | u[1]);
  case 3:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xffff)
      return htonl((uint32_t) - 1);
    return htonl((u[0] << 24) | (u[1] << 16) | u[2]);
  case 4:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xff || u[3] > 0xff)
      return htonl((uint32_t) - 1);
    return htonl((u[0] << 24) | (u[1] << 16) | (u[2] << 8) | u[3]);
  }
  return htonl((uint32_t) - 1);
}

const char *ink_inet_ntop(const struct sockaddr *addr, char *dst, size_t size)
{
  char const* zret = 0;

  switch (addr->sa_family) {
  case AF_INET:
    zret = inet_ntop(AF_INET, &ink_inet_ip4_addr_cast(addr), dst, size);
    break;
  case AF_INET6:
    zret = inet_ntop(AF_INET6, &ink_inet_ip6_addr_cast(addr), dst, size);
    break;
  default:
    zret = dst;
    snprintf(dst, size, "*Not IP address [%u]*", addr->sa_family);
    break;
  }
  return zret;
}

uint16_t ink_inet_port(const struct sockaddr *addr)
{
  uint16_t port = 0;

  switch (addr->sa_family) {
  case AF_INET:
    port = ntohs(((struct sockaddr_in *)addr)->sin_port);
    break;
  case AF_INET6:
    port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
    break;
  }

  return port;
}

char const* ink_inet_nptop(
  sockaddr const* addr,
  char* dst, size_t size
) {
  char buff[INET6_ADDRSTRLEN];
  snprintf(dst, size, "%s:%u",
    ink_inet_ntop(addr, buff, sizeof(buff)),
    ink_inet_get_port(addr)
  );
  return dst;
}

int ink_inet_pton(char const* text, sockaddr* addr) {
  int zret = -1;
  addrinfo hints; // [out]
  addrinfo *ai; // [in]
  char* copy; // needed for handling brackets.

  if ('[' == *text) {
    /* Ugly. In a number of places we must use bracket notation
       to support port numbers. Rather than mucking with that
       everywhere, we'll tweak it here. Experimentally we can't
       depend on getaddrinfo to handle it. Note that the text
       buffer size includes space for the nul, so a bracketed
       address is at most that size - 1 + 2 -> size+1.

       It just gets better. In order to bind link local addresses
       the scope_id must be set to the interface index. That's
       most easily done by appending a %intf (where "intf" is the
       name of the interface) to the address. Which makes
       the address potentially larger than the standard maximum.
       So we can't depend on that sizing.
    */

    size_t n = strlen(text);
    copy = static_cast<char*>(alloca(n-1));
    if (']' == text[n-1]) {
      ink_strlcpy(copy, text+1, n-1);
      text = copy;
    } else {
      // Bad format, getaddrinfo isn't going to succeed.
      return zret;
    }
  }

  ink_zero(hints);
  hints.ai_family = PF_UNSPEC;
  hints.ai_flags = AI_NUMERICHOST|AI_PASSIVE;
  if (0 == (zret = getaddrinfo(text, 0, &hints, &ai))) {
    if (ink_inet_is_ip(ai->ai_addr)) {
      if (addr) ink_inet_copy(addr, ai->ai_addr);
      zret = 0;
    }
    freeaddrinfo(ai);
  }
  return zret;
}

uint32_t ink_inet_hash(sockaddr const* addr) {
  union md5sum {
    unsigned char c[16];
    uint32_t i;
  } zret;
  zret.i = 0;

  if (ink_inet_is_ip4(addr)) {
    zret.i = ink_inet_ip4_addr_cast(addr);
  } else if (ink_inet_is_ip6(addr)) {
    ink_code_md5(const_cast<uint8_t*>(ink_inet_addr8_cast(addr)), INK_IP6_SIZE, zret.c);
  }
  return zret.i;
}

int
ink_inet_to_hex(sockaddr const* src, char* dst, size_t len) {
  int zret = 0;
  ink_assert(len);
  char const* dst_limit = dst + len - 1; // reserve null space.
  if (ink_inet_is_ip(src)) {
    uint8_t const* data = ink_inet_addr8_cast(src);
    for ( uint8_t const* src_limit = data + ink_inet_addr_size(src)
        ; data < src_limit && dst+1 < dst_limit
        ; ++data, zret += 2
    ) {
    uint8_t n1 = (*data >> 4) & 0xF; // high nybble.
    uint8_t n0 = *data & 0xF; // low nybble.

    *dst++ = n1 > 9 ? n1 + 'A' - 10 : n1 + '0';
    *dst++ = n0 > 9 ? n0 + 'A' - 10 : n0 + '0';
    }
  }
  *dst = 0; // terminate but don't include that in the length.
  return zret;
}

