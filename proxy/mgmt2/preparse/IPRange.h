/** @file

  Matching of a given IP addr against ranges of addrs

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

#ifndef _IPRange_h_
#define _IPRange_h_

#include "libts.h"

#define MAX_IP 400
#define MAX_IP_RANGES 400
#define MAXLINESIZE 400

struct IPRange
{
public:

  /** @return true if ip matches, false otherwise. */
  bool match(unsigned int ip);

  /**
    @return 0 for successful read, error string (malloc'ed) otherwise,
      identifier_str is the string that is used to select the relevant
      lines.

  */
  char *read_table_from_file(int fd, const char *identifier_str, bool localip = TRUE);
    IPRange():n_ips(0), n_ip_ranges(0)
  {

    for (int i = 0; i < MAX_IP; i++)
    {
      ips[i] = 0;
    }

    for (int i = 0; i < MAX_IP_RANGES; i++) {
      ip_ranges_start[i] = 0;
      ip_ranges_finish[i] = 0;
    }


  }

private:
  int n_ips, n_ip_ranges;
  unsigned int ips[MAX_IP];
  unsigned int ip_ranges_start[MAX_IP_RANGES];
  unsigned int ip_ranges_finish[MAX_IP_RANGES];
};
#endif
