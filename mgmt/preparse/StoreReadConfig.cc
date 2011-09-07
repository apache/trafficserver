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
/****************************************************************************

  StoreReadConfig.cc


 ****************************************************************************/

char *
parseStorageFile(int fd)
{
  int ln = 0;
  char line[256];
  const char *err = NULL;
  while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
    // update lines

    line[255] = 0;
    ln++;

    // skip comments and blank lines

    if (*line == '#')
      continue;
    char *n = line;
    n += strspn(n, " \t");
    if (!*n)
      continue;

    // parse

    char *e = strpbrk(n, " \t\n");
    int64_t size = -1;
    while (*e && !isdigit(*e))
      e++;
    if (e && *e) {
      // coverity[secure_coding]
      if (1 != sscanf(e, "%" PRId64 "", &size)) {
        err = "error parsing size";
        goto Lfail;
      }
    }
  }
  return NULL;
Lfail:
  int e_size = 1000;
  char *e = (char *)ats_malloc(e_size);

  snprintf(e, e_size, "Error reading storage.config: %s line %d\n", err, ln);
  return e;
}


char *
parseStorageFile(FILE * fp)
{
  return (parseStorageFile(fileno(fp)));
}
