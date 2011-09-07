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

/*****************************************************************************
 *
 *  RemapReadConfig.cc - Parser to validate the remap.config for the
 *                         User Interface
 *
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "ink_string.h"
#include "ink_file.h"
#include "Tokenizer.h"
#include "URL.h"
#include "MIME.h"
#include "ParseRules.h"

char *
parseRemapFile(int fd)
{
  int entry = 0;
  char line[512];
  const char *err = NULL;
  char *errBuf;
  Tokenizer whiteTok(" \t\r\n");
  int numToks;
  const char *map_from;
  const char *map_from_start;
  const char *map_to;
  bool forward_map;

  // For testing of URLs
  URL fromURL;
  URL toURL;
  int length;

  int fromSchemeLen, toSchemeLen;
  int fromHostLen, toHostLen;
  int fromPathLen, toPathLen;
  const char *fromScheme;
  const char *toScheme;
  const char *fromHost;
  const char *fromPath;
  const char *toPath;

  while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
    if (*line != '#' && *line != '\0') {
      entry++;

      numToks = whiteTok.Initialize(line, SHARE_TOKS);
      if (numToks == 0) {
        // Handle empty (whitespace only) line
        // However, do not count it in the entry count
        entry--;
        continue;
      } else if (numToks < 3) { //INKqa09603: can have 3 or 4 fields
        err = "Missing field";
        goto FAIL;
      } else {

        // Check to see whether is a reverse or forward mapping
        if (strcasecmp("reverse_map", whiteTok[0]) == 0) {
          forward_map = false;
        } else if (strcasecmp("map", whiteTok[0]) == 0) {
          forward_map = true;
        } else {
          err = "Unknown mapping type";
          goto FAIL;
        }

        map_from = whiteTok[1];
        length = strlen(map_from);

        // URL::create modified map from so keep a point to
        //   the beginning of the string
        map_from_start = map_from;
        fromURL.create(NULL);
        if (fromURL.parse(&map_from, map_from + length) != PARSE_DONE) {
          err = "Malformed From URL";
          goto FAIL;
        }

        map_to = whiteTok[2];
        length = strlen(map_to);
        toURL.create(NULL);
        if (toURL.parse(&map_to, map_to + length) != PARSE_DONE) {
          err = "Malformed To URL";
          goto FAIL;
        }

        fromScheme = fromURL.scheme_get(&fromSchemeLen);
        toScheme = toURL.scheme_get(&toSchemeLen);

        if ((fromScheme != URL_SCHEME_HTTP && fromScheme != URL_SCHEME_HTTPS) ||
            (toScheme != URL_SCHEME_HTTP && toScheme != URL_SCHEME_HTTPS)) {
          err = "Only http and https remappings are supported";
          goto FAIL;
        }
        // Check to see if we have a complete URL, if not
        //  we should only a path component
        if (strstr(map_from_start, "://") == NULL) {
          if (*map_from_start != '/') {
            err = "Relative remappings must begin with a /";
            goto FAIL;
          }
        }
        // Check to see the fromHost remapping is a relative one
        fromHost = fromURL.host_get(&fromHostLen);
        if (fromHost == NULL) {
          if (forward_map) {
            if (*map_from_start != '/') {
              err = "Relative remappings must begin with a /";
              goto FAIL;
            }
          } else {
            err = "Remap source in reverse mappings requires a hostname";
            goto FAIL;
          }
        }

        if (toURL.host_get(&toHostLen) == NULL) {
          err = "The remap destinations require a hostname";
          goto FAIL;
        }
        // Make sure that there are not any unsafe characters in
        //   the URLs
        fromPath = fromURL.path_get(&fromPathLen);
        while (fromPathLen > 0) {
          if (ParseRules::is_unsafe(*fromPath)) {
            err = "Unsafe character in `From` URL";
            goto FAIL;
          }
          fromPath++;
          fromPathLen--;
        }
        toPath = toURL.path_get(&toPathLen);
        while (toPathLen > 0) {
          if (ParseRules::is_unsafe(*toPath)) {
            err = "Unsafe character in `To` URL";
            goto FAIL;
          }
          toPath++;
          toPathLen--;
        }
      }
    }
  }
  return NULL;

FAIL:
  errBuf = (char *)ats_malloc(1024);
  snprintf(errBuf, 1024, "[Entry %d] %s", entry, err);
  return errBuf;
}

char *
parseRemapFile(FILE * fp)
{
  return (parseRemapFile(fileno(fp)));
}
