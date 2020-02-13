/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "normalize.h"
#include "common.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>

/* Remove Dot Algorithm outlined in RFC3986 section 5.2.4
 * Function writes normalizes path and writes to ret_buffer */
int
remove_dot_segments(const char *path, int path_ct, char *ret_buffer, int buff_ct)
{
  /* Ensure buffer is at least the size of the path */
  if (buff_ct < path_ct) {
    PluginDebug("Path buffer not large enough");
    return -1;
  }

  /* Create an input buffer that we can change */
  char inBuff[path_ct + 1];
  memset(inBuff, 0, path_ct + 1);
  strcpy(inBuff, path);

  const char *path_end = inBuff + path_ct;
  char *seg_start      = inBuff;
  char *seg_end;
  char *write_buffer = ret_buffer;
  int seg_len;

  for (;;) {
    if (seg_start == path_end) {
      break;
    }
    seg_end = seg_start + 1;

    /* Parse such that Seg start/end contain the next full path segment */
    while (seg_end != path_end && *seg_end != '/') {
      seg_end++;
    }

    seg_len = seg_end - seg_start + 1;

    /* Remove starting ../ or ./ from input buffer */
    if (!strncmp(seg_start, "../", seg_len) || !strncmp(seg_start, "./", seg_len)) {
      if (seg_end != path_end) {
        seg_end++;
      }
    }

    /* Remove starting /./ or /. from input buffer and replace with '/' in output buffer */
    else if (!strncmp(seg_start, "/./", seg_len) || !strncmp(seg_start, "/.", seg_len)) {
      *write_buffer = '/';
      write_buffer++;
      if (seg_end != path_end) {
        seg_end++;
      }
    }

    /* Replace /../ or /.. with / in write_buffer and remove preceding segment */
    else if (!strncmp(seg_start, "/../", seg_len) || !strncmp(seg_start, "/..", seg_len)) {
      int prev_len = 0;
      while (*write_buffer != '/' && write_buffer != ret_buffer) {
        prev_len++;
        write_buffer--;
      }
      memset(write_buffer, 0, prev_len);

      /* Replace segment with '/' in input buffer */
      if (seg_end != path_end) {
        seg_start[seg_len - 1] = '/';
      } else {
        seg_start[seg_len - 2] = '/';
        seg_end--;
      }
    }

    /* Remove starting '.' or '..' from input buffer */
    else if (!strncmp(seg_start, ".", seg_len) || !strncmp(seg_start, "..", seg_len)) {
      if (seg_end != path_end) {
        seg_end++;
      }
    }
    /* Place the current path segment to the output buffer including initial '/' but not the next '/' */
    else {
      /* Write first forward slash to buffer */
      if (*seg_start == '/') {
        *write_buffer = *seg_start;
        write_buffer++;
        seg_start++;
      }

      /* Write subsequent characters to buffer */
      while (*seg_start != '/') {
        *write_buffer = *seg_start;
        write_buffer++;
        if (*seg_start == 0) {
          break;
        }
        seg_start++;
      }
    }
    seg_start = seg_end;
  }

  PluginDebug("Normalized Path: %s", ret_buffer);
  return strlen(ret_buffer);
}

/* Function percent decodes uri_ct characters of the string uri and writes it to the decoded_uri
 * buffer. If lower is true, it sets all characters including decoded ones to lower case.
 * The function returns the length of the decoded string or -1 if there was a parsing error
 * TODO: ADD functionality to ignore unicode non-standard characters and leave them encoded. Read RFC regarding normalization and
 * determine if this is compliant.
 */
int
percent_decode(const char *uri, int uri_ct, char *decoded_uri, bool lower)
{
  static const char *reserved_string = ":/?#[]@!$&\'()*+,;=";

  if (uri_ct <= 0) {
    return 0;
  }

  int offset = 0;
  int i;
  for (i = 0; i < uri_ct; i++) {
    if (uri[i] == '%') {
      /* The next two characters are interpreted as the hex encoded value. Store in encodedVal */
      if (uri_ct < i + 2) {
        goto decode_failure;
      }
      char encodedVal[2] = {0};
      int j;
      for (j = 0; j < 2; j++) {
        if (isxdigit(uri[i + j + 1])) {
          encodedVal[j] = uri[i + j + 1];
        } else {
          goto decode_failure;
        }
      }
      int hexVal = 0;
      char decodeChar;
      sscanf(encodedVal, "%2x", &hexVal);
      decodeChar = (char)hexVal;
      /* If encoded value is a reserved char, leave encoded*/
      if (strchr(reserved_string, decodeChar)) {
        decoded_uri[i - offset]     = uri[i];
        decoded_uri[i + 1 - offset] = toupper(uri[i + 1]);
        decoded_uri[i + 2 - offset] = toupper(uri[i + 2]);
      }
      /* If not a reserved char, decode using the decoded_uri buffer */
      else {
        if (lower) {
          decoded_uri[i - offset] = tolower(decodeChar);
        } else {
          decoded_uri[i - offset] = decodeChar;
        }
        offset = offset + 2;
      }
      i = i + 2;
    }
    /* Write non-encoded values to decoded buffer */
    else {
      if (lower) {
        decoded_uri[i - offset] = tolower(uri[i]);
      } else {
        decoded_uri[i - offset] = uri[i];
      }
    }
  }

  /* Return the size of the newly decoded string */
  return uri_ct - offset;

decode_failure:
  PluginDebug("ERROR Decoding URI");
  return -1;
}

/* This function takes a uri and an initialized buffer to populate with the normalized uri.
 * Returns non zero for error
 *
 * The buffer provided must be at least the length of the uri + 1 as the normalized uri will
 * potentially be one char larger than the original uri if a backslash is added to the path.
 *
 *   The normalization function returns a string with the following modifications
 *   1. Lowecase protocol/domain
 *   2. Path segments .. and . are removed from path
 *   3. Alphabetical percent encoded octet values are toupper
 *   4. Non-reserved percent encoded octet values are decoded
 *   5. The Port is removed if it is default
 *   6. Defaults to a single backslash for the path segment if path segment is empty
 */
int
normalize_uri(const char *uri, int uri_ct, char *normal_uri, int normal_ct)
{
  PluginDebug("Normalizing URI: %s", uri);

  /* Buffer provided must be large enough to store the uri plus one additional char */
  const char *uri_end  = uri + uri_ct;
  const char *buff_end = normal_uri + normal_ct;

  if ((normal_uri == NULL) || (normal_uri && normal_ct < uri_ct + 1)) {
    PluginDebug("Buffer to Normalize URI not large enough.");
    return -1;
  }

  /* Initialize a path buffer to pass to path normalization function later on */
  char path_buffer[normal_ct];
  memset(path_buffer, 0, normal_ct);

  /* Comp variables store starting/ending indexes for each uri component as uri is parsed.
   * Write buffer traverses the normalized uri buffer as we build the normalized string.
   */
  const char *comp_start = uri;
  const char *comp_end   = uri;
  char *write_buffer     = normal_uri;
  bool https             = false;

  /* Parse the protocol which will end with a colon */
  while (*comp_end != ':' && comp_end != uri_end) {
    *write_buffer = tolower(*comp_end);
    comp_end++;
    write_buffer++;
  }

  if (comp_end == uri_end) {
    PluginDebug("Reached End of String prematurely");
    goto normalize_failure;
  }

  /* Copy the colon */
  *write_buffer = *comp_end;
  comp_end++;
  write_buffer++;

  /* Ensure the protocol is either http or https */
  if (strcmp("https:", normal_uri) == 0) {
    https = true;
  } else if (strcmp("http:", normal_uri)) {
    PluginDebug("String is neither http or https");
    goto normalize_failure;
  }

  /* Protocol must be terminated by two forward slashes */
  int i;
  for (i = 0; i < 2; i++) {
    if (comp_end == uri_end || *comp_end != '/') {
      goto normalize_failure;
    }
    *write_buffer = *comp_end;
    comp_end++;
    write_buffer++;
  }

  if (comp_end == uri_end) {
    goto normalize_failure;
  }

  /* Comp_start is index of start of authority component */
  int comp_ct;
  comp_start = comp_end;

  /* Set comp start/end to contain authority component */
  bool userInfo = false;
  while (comp_end != uri_end && *comp_end != '/' && *comp_end != '?' && *comp_end != '#') {
    /* If we encounter userinfo, decode it without altering case and set comp_start/end to only include hostname/port */
    if (*comp_end == '@' && userInfo == false) {
      comp_ct = comp_end - comp_start;
      comp_ct = percent_decode(comp_start, comp_ct, write_buffer, false);
      if (comp_ct < 0) {
        goto normalize_failure;
      }
      comp_start   = comp_end;
      userInfo     = true;
      write_buffer = write_buffer + comp_ct;
    }
    comp_end++;
  }

  /* UserInfo without a hostname is invalid */
  if (userInfo == true && comp_end == uri_end) {
    goto normalize_failure;
  }

  comp_ct = comp_end - comp_start;

  /* - comp start/end holds indices in original uri of hostname/port
   * - write_buffer holds pointer to start of hostname/port written to the decode buffer
   * - comp_ct holds size of hostname/port in original uri
   */

  /* Parse and decode the hostname and port and set to lower case */
  comp_ct = percent_decode(comp_start, comp_ct, write_buffer, true);

  if (comp_ct < 0) {
    goto normalize_failure;
  }

  /* Remove the port from the buffer if default */
  while (*write_buffer != 0) {
    if (*write_buffer == ':') {
      if (https == true && !strncmp(write_buffer, ":443", 5)) {
        memset(write_buffer, 0, 4);
        break;
      } else if (https == false && !strncmp(write_buffer, ":80", 4)) {
        memset(write_buffer, 0, 3);
        break;
      }
    }
    write_buffer++;
  }

  comp_start = comp_end;

  /* If we have reached the end of the authority section with an empty path component, add a trailing backslash */
  if (*comp_end == 0 || *comp_end == '?' || *comp_end == '#') {
    *write_buffer = '/';
    write_buffer++;
  }

  /* If there is a path component, normalize it */
  else {
    /* Set comp start/end pointers to contain the path component */
    while (*comp_end != '?' && *comp_end != '#' && *comp_end != 0) {
      comp_end++;
    }
    /* Decode the path component without altering case and store it to the path_buffer*/
    comp_ct = comp_end - comp_start;
    comp_ct = percent_decode(comp_start, comp_ct, path_buffer, false);

    if (comp_ct < 0) {
      goto normalize_failure;
    }

    /* Remove the . and .. segments from the path and write the now normalized path to the output buffer */
    PluginDebug("Removing Dot Segments");
    int buff_ct = buff_end - write_buffer;
    comp_ct     = remove_dot_segments(path_buffer, comp_ct, write_buffer, buff_ct);

    if (comp_ct < 0) {
      PluginDebug("Failure removing dot segments from path");
      goto normalize_failure;
    }
    write_buffer = write_buffer + comp_ct;
  }

  /* If there is any uri remaining after the path, decode and set case to lower */
  if (comp_end != uri_end) {
    comp_start = comp_end;
    comp_ct    = uri_end - comp_start;
    comp_ct    = percent_decode(comp_start, comp_ct, write_buffer, false);
    if (comp_ct < 0) {
      goto normalize_failure;
    }
  }

  PluginDebug("Normalized URI:  %s", normal_uri);
  return 0;

normalize_failure:
  PluginDebug("URI Normalization Failure. URI does not fit http or https schemes.");
  return -1;
}
