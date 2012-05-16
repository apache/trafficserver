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

/****************************************************************************

  IPRange.cc

  This file an IPRange object that reads a range of IPS, and does
  matching of a given IP address against those ranges.
 ****************************************************************************/

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "ink_assert.h"
#include "IPRange.h"

#define TEST(_x)

// #define TEST(_x) _x

#define ERR_STRING_LEN 100

// Returns 0 if successful, 1 if failed
int
read_an_ip(char *line, unsigned int *ip, int *i, int n)
{
  int k;
  char s[17];
  while ((*i) < n && isspace(line[(*i)]))
    (*i)++;
  if (*i == n) {
    TEST(printf("Socks Configuration (read_an_ip1): Invalid Syntax in line %s\n", line);
      );
    return 1;
  }
  for (k = 0; k < 17 && (isdigit(line[(*i)]) || (line[(*i)] == '.')); k++, (*i)++) {
    s[k] = line[(*i)];
  }
  if (k == 17) {
    TEST(printf("Socks Configuration (read_an_ip2): Invalid Syntax in line %s, k %d\n", line, k);
      );
    return 1;
  }
  s[k] = '\0';
  k++;
  TEST(printf("IP address read %s\n", s);
    );
  *ip = inet_addr(s);
  if (*ip == (unsigned int) -1) {
    TEST(printf("Socks Configuration: Illegal IP address read %s, %u\n", s, *ip);
      );
    return 1;
  }
  return 0;
}

// Returns 0 if successful, error string otherwise
char *
IPRange::read_table_from_file(int fd, const char *identifier_str, bool localip)
{
  NOWARN_UNUSED(localip);
  int i, j, n, rc, s, line_no;
  char c, line[MAXLINESIZE];
  bool end_of_file;
  unsigned int ip;
  char first_string[MAXLINESIZE];

  // First hardcode 127.0.0.1 into the table
  ips[0] = ntohl(inet_addr("127.0.0.1"));
  n_ips++;

  line_no = 0;
  end_of_file = false;
  do {
    line_no++;
    n = 0;
    while (((s = read(fd, &c, 1)) == 1) && (c != '\n') && n < MAXLINESIZE - 1)
      line[n++] = c;
    if (s <= 0)
      c = EOF;                  // mimic behavior of getc on EOF
    // append null
    line[n] = '\0';
    if (c == (char)EOF)
      end_of_file = true;
    // first_string has the same length as line, so here we disable coverity check
    // coverity[secure_coding]
    rc = sscanf(line, "%s", first_string);
    if ((rc <= 0) || strcmp(first_string, identifier_str))
      continue;
    i = 0;
    while (i < n && isspace(line[i]))
      i++;
    ink_assert(i != n);
    j = 0;
    while (!isspace(line[i])) {
      first_string[j] = line[i];
      i++;
      j++;
    }
    first_string[j] = '\0';
    ink_assert(!strcmp(first_string, identifier_str));
    // Now look for IP address
    while (true) {
      while (i < n && isspace(line[i]))
        i++;
      if (i == n)
        break;
      if (read_an_ip(line, &ip, &i, n) == 1) {
        char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
        snprintf(error_str, ERR_STRING_LEN, "Incorrect Syntax in Socks Configuration at Line %d", line_no);
        return error_str;
      }
      while (i < n && isspace(line[i]))
        i++;
      if (i == n || line[i] == ',') {
        // You have read an IP address. Enter it in the table
        ips[(n_ips)++] = ntohl(ip);
        if (i == n)
          break;
        else
          i++;
      } else if (line[i] == '-') {
        // What you have just read is the start of the range,
        // Now, read the end of the IP range
        i++;
        ip_ranges_start[(n_ip_ranges)] = ntohl(ip);
        if (read_an_ip(line, &ip, &i, n) == 1) {
          char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
          snprintf(error_str, ERR_STRING_LEN, "Incorrect Syntax in Socks Configuration at Line %d", line_no);
          return error_str;
        }
        ip_ranges_finish[(n_ip_ranges)++] = ntohl(ip);
        while (i < n && isspace(line[i]))
          i++;
        if (i == n)
          break;
        if (line[i] != ',') {
          TEST(printf("Socks Configuration (read_table_from_file1):Invalid Syntax in line %s\n", (char *) line);
            );
          char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
          snprintf(error_str, ERR_STRING_LEN, "Incorrect Syntax in Socks Configuration at Line %d", line_no);
          return error_str;
        }
        i++;
      } else {
        TEST(printf("Socks Configuration (read_table_from_file2):Invalid Syntax in line %s\n", (char *) line);
          );
        char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
        snprintf(error_str, ERR_STRING_LEN, "Incorrect Syntax in Socks Configuration at Line %d", line_no);
        return error_str;
      }
    }
  } while (!end_of_file);
  TEST(printf("Socks Conf File Read\n");
    );
  return 0;
}

bool
IPRange::match(unsigned int ip)
{
  int i;
  ip = ntohl(ip);
  for (i = 0; i < n_ip_ranges; i++) {
    if (ip_ranges_start[i] <= ip && ip <= ip_ranges_finish[i]) {
      TEST(printf("Match found in the table for %u\n", ip);
        );
      return true;
    }
  }
  for (i = 0; i < n_ips; i++) {
    if (ips[i] == ip) {
      TEST(printf("Match found in the table for %u\n", ip);
        );
      return true;
    }
  }
  TEST(printf("No Match found in the table for %u\n", ip);
    );
  return false;
}
