/** @file

  Loading @c IpMap from a configuration file.

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

// Copied from IPRange.cc for backwards compatibility.

#include <ts/IpMap.h>
#include <ts/IpMapConf.h>
#include <ts/ink_memory.h>

static size_t const ERR_STRING_LEN = 256;
static size_t const MAX_LINE_SIZE  = 2048;

// Returns 0 if successful, 1 if failed
// line  Input text (source line).
// n     Amount of data in @a line.
// i     [in,out] Offset in line.
// addr  [out] Destination for address.
// err   Buffer for error string (must be ERR_STRING_LEN big).
int
read_addr(char *line, int n, int *i, sockaddr *addr, char *err)
{
  int k;
  char dst[INET6_ADDRSTRLEN];
  char *src        = line + *i;
  bool bracketed_p = false;

  // Allow enclosing brackets to be more consistent but
  // don't bother passing it to @c ntop.
  if ((*i < n) && ('[' == *src)) {
    ++*i, ++src, bracketed_p = true;
  }

  for (k = 0; k < INET6_ADDRSTRLEN && *i < n && (isxdigit(*src) || '.' == *src || ':' == *src); ++k, ++*i, ++src) {
    dst[k] = *src;
  }

  if (bracketed_p && (!(*i < n) || (']' != *src))) {
    snprintf(err, ERR_STRING_LEN, "Unclosed brackets");
    return EINVAL;
  }

  if (k == sizeof(dst)) {
    snprintf(err, ERR_STRING_LEN, "IP address too long");
    return EINVAL;
  }

  dst[k] = '\0';
  if (0 != ats_ip_pton(dst, addr)) {
    snprintf(err, ERR_STRING_LEN, "IP address '%s' improperly formatted", dst);
    return EINVAL;
  }
  return 0;
}

char *
Load_IpMap_From_File(IpMap *map, int fd, const char *key_str)
{
  char *zret = nullptr;
  int fd2    = dup(fd); // dup to avoid closing the original file.
  FILE *f    = nullptr;

  if (fd2 >= 0) {
    f = fdopen(fd2, "r");
  }

  if (f != nullptr) {
    zret = Load_IpMap_From_File(map, f, key_str);
    fclose(f);
  } else {
    zret = (char *)ats_malloc(ERR_STRING_LEN);
    snprintf(zret, ERR_STRING_LEN, "Unable to reopen file descriptor as stream %d:%s", errno, strerror(errno));
  }
  return zret;
}

// Skip space in line, returning true if more data is available
// (not end of line).
//
// line    Source line.
// n       Line length.
// offset  Current offset
static inline bool
skip_space(char *line, int n, int &offset)
{
  while (offset < n && isspace(line[offset])) {
    ++offset;
  }
  return offset < n;
}

// Returns 0 if successful, error string otherwise
char *
Load_IpMap_From_File(IpMap *map, FILE *f, const char *key_str)
{
  int i, n, line_no;
  int key_len = strlen(key_str);
  IpEndpoint laddr, raddr;
  char line[MAX_LINE_SIZE];
  char err_buff[ERR_STRING_LEN];

  // First hardcode 127.0.0.1 into the table
  map->mark(INADDR_LOOPBACK);

  line_no = 0;
  while (fgets(line, MAX_LINE_SIZE, f)) {
    ++line_no;
    n = strlen(line);
    // Find first white space which terminates the line key.
    for (i = 0; i < n && !isspace(line[i]); ++i) {
      ;
    }
    if (i != key_len || 0 != strncmp(line, key_str, key_len)) {
      continue;
    }
    // Now look for IP address
    while (true) {
      if (!skip_space(line, n, i)) {
        break;
      }

      if (0 != read_addr(line, n, &i, &laddr.sa, err_buff)) {
        char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
        snprintf(error_str, ERR_STRING_LEN, "Invalid input configuration (%s) at line %d offset %d - '%s'", err_buff, line_no, i,
                 line);
        return error_str;
      }

      if (!skip_space(line, n, i) || line[i] == ',') {
        // You have read an IP address. Enter it in the table
        map->mark(&laddr);
        if (i == n) {
          break;
        } else {
          ++i;
        }
      } else if (line[i] == '-') {
        // What you have just read is the start of the range,
        // Now, read the end of the IP range
        ++i;
        if (!skip_space(line, n, i)) {
          char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
          snprintf(error_str, ERR_STRING_LEN, "Invalid input (unterminated range) at line %d offset %d - '%s'", line_no, i, line);
          return error_str;
        } else if (0 != read_addr(line, n, &i, &raddr.sa, err_buff)) {
          char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
          snprintf(error_str, ERR_STRING_LEN, "Invalid input (%s) at line %d offset %d - '%s'", err_buff, line_no, i, line);
          return error_str;
        }
        map->mark(&laddr.sa, &raddr.sa);
        if (!skip_space(line, n, i)) {
          break;
        }
        if (line[i] != ',') {
          char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
          snprintf(error_str, ERR_STRING_LEN, "Invalid input (expecting comma) at line %d offset %d - '%s'", line_no, i, line);
          return error_str;
        }
        ++i;
      } else {
        char *error_str = (char *)ats_malloc(ERR_STRING_LEN);
        snprintf(error_str, ERR_STRING_LEN, "Invalid input (expecting dash or comma) at line %d offset %d", line_no, i);
        return error_str;
      }
    }
  }
  return nullptr;
}
