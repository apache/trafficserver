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

#include "inktomi++.h"

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

uint32
ink_inet_addr(const char *s)
{
  uint32 u[4];
  uint8 *pc = (uint8 *) s;
  int n = 0;
  uint32 base = 10;

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
    return htonl((uint32) - 1);

  switch (n) {
  case 1:
    return htonl(u[0]);
  case 2:
    if (u[0] > 0xff || u[1] > 0xffffff)
      return htonl((uint32) - 1);
    return htonl((u[0] << 24) | u[1]);
  case 3:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xffff)
      return htonl((uint32) - 1);
    return htonl((u[0] << 24) | (u[1] << 16) | u[2]);
  case 4:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xff || u[3] > 0xff)
      return htonl((uint32) - 1);
    return htonl((u[0] << 24) | (u[1] << 16) | (u[2] << 8) | u[3]);
  }
  return htonl((uint32) - 1);
}

const char *ink_inet_ntop(const struct sockaddr *addr, char *dst, size_t size)
{
  void *address = NULL;

  switch (addr->sa_family) {
  case AF_INET:
    address = &((struct sockaddr_in *)addr)->sin_addr;
    break;
  case AF_INET6:
    address = &((struct sockaddr_in6 *)addr)->sin6_addr;
    break;
  }

  return inet_ntop(addr->sa_family, address, dst, size);
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
