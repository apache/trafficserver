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
#include "I_Machine.h"

static Machine *machine = NULL;

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

Machine::Machine(char *ahostname, unsigned int aip):
hostname(ahostname),
ip(aip)
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
      // lowest IP address

      ip = (unsigned int) -1;   // 0xFFFFFFFF
      for (int i = 0; r->h_addr_list[i]; i++)
        if (ip > *(unsigned int *) r->h_addr_list[i])
          ip = *(unsigned int *) r->h_addr_list[i];
      if (ip == (unsigned int) -1)
        ip = 0;
    }
    //ip = htonl(ip); for the alpha!
  } else {

    ip = aip;

    ink_gethostbyaddr_r_data data;
    struct hostent *r = ink_gethostbyaddr_r((char *) &ip, sizeof(int),
                                            AF_INET, &data);

    if (r == NULL) {
      unsigned char x[4];
      memset(x, 0, sizeof(x));
      *(uint32 *) & x = (uint32) ip;
      Debug("machine_debug", "unable to reverse DNS %u.%u.%u.%u: %d", x[0], x[1], x[2], x[3], data.herrno);
    } else
      hostname = xstrdup(r->h_name);
  }
  if (hostname)
    hostname_len = strlen(hostname);
  else
    hostname_len = 0;

  {
    unsigned char x[4];
    memset(x, 0, sizeof(x));
    *(uint32 *) & x = (uint32) ip;
    const size_t ip_string_size = sizeof(char) * 16;
    ip_string = (char *) xmalloc(ip_string_size);
    snprintf(ip_string, ip_string_size, "%hu.%hu.%hu.%hu", x[0], x[1], x[2], x[3]);
  }
}

Machine::~Machine()
{
  if (hostname)
    xfree(hostname);
  if (ip_string)
    xfree(ip_string);
}
