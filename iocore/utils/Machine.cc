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
#include "I_Machine.h"

// Singleton
static Machine *machine = NULL;

// Moved from HttpTransactHeaders.cc, we should probably move this somewhere ...
#define H(_x) (((_x)>9)?((_x)-10+'A'):((_x)+'0'))
int
nstrhex(char *d, unsigned int i)
{
  unsigned char *p = (unsigned char *) &i;
  d[0] = H(p[0] >> 4);
  d[1] = H(p[0] & 0xF);
  d[2] = H(p[1] >> 4);
  d[3] = H(p[1] & 0xF);
  d[4] = H(p[2] >> 4);
  d[5] = H(p[2] & 0xF);
  d[6] = H(p[3] >> 4);
  d[7] = H(p[3] & 0xF);
  return 8;
}


// Machine class. TODO: This has to deal with IPv6!
Machine *
this_machine()
{
  if (machine == NULL) {
    ink_assert("need to call create_this_machine before accessing" "this_machine()");
  }
  return machine;
}

void
create_this_machine(char *hostname, unsigned int ip)
{
  machine = NEW(new Machine(hostname, ip));
}

Machine::Machine(char *ahostname, unsigned int aip)
  : hostname(ahostname), ip(aip)
{
  if (!aip) {
    char localhost[1024];

    if (!ahostname) {
      ink_release_assert(!gethostname(localhost, 1023));
      ahostname = localhost;
    }
    hostname = xstrdup(ahostname);

    ink_gethostbyname_r_data data;
    struct hostent *r = ink_gethostbyname_r(ahostname, &data);

    if (!r) {
      Warning("unable to DNS %s: %d", ahostname, data.herrno);
      ip = 0;
    } else {
      ip = (unsigned int) -1;   // 0xFFFFFFFF
      for (int i = 0; r->h_addr_list[i]; i++) {
        if (ip > *(unsigned int *) r->h_addr_list[i])
          ip = *(unsigned int *) r->h_addr_list[i];
      }
      if (ip == (unsigned int) -1)
        ip = 0;
    }
    //ip = htonl(ip); for the alpha! TODO
  } else {
    ip = aip;

    ink_gethostbyaddr_r_data data;
    struct hostent *r = ink_gethostbyaddr_r((char *) &ip, sizeof(int), AF_INET, &data);

    if (r == NULL) {
      unsigned char x[4];

      memset(x, 0, sizeof(x));
      *(uint32_t *) & x = (uint32_t) ip;
      Debug("machine_debug", "unable to reverse DNS %hhu.%hhu.%hhu.%hhu: %d", x[0], x[1], x[2], x[3], data.herrno);
    } else
      hostname = xstrdup(r->h_name);
  }

  if (hostname)
    hostname_len = strlen(hostname);
  else
    hostname_len = 0;

  unsigned char x[4];

  memset(x, 0, sizeof(x));
  *(uint32_t *) & x = (uint32_t) ip;
  const size_t ip_string_size = sizeof(char) * 16;
  ip_string = (char *)ats_malloc(ip_string_size);
  snprintf(ip_string, ip_string_size, "%hhu.%hhu.%hhu.%hhu", x[0], x[1], x[2], x[3]);
  ip_string_len = strlen(ip_string);

  ip_hex_string = (char*)ats_malloc(9);
  memset(ip_hex_string, 0, 9);
  nstrhex(ip_hex_string, ip);
  ip_hex_string_len = strlen(ip_hex_string);
}

Machine::~Machine()
{
  ats_free(hostname);
  ats_free(ip_string);
  ats_free(ip_hex_string);
}
