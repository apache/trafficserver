/** @file

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

#include "swoc/bwf_ip.h"
#include "tscore/Encoding.h"

/*-------------------------------------------------------------------------
  Encoding::escapify_url_common

  This routine will escapify a URL to remove spaces (and perhaps other ugly
  characters) from a URL and replace them with a hex escape sequence.
  Since the escapes are larger (multi-byte) than the characters being
  replaced, the string returned will be longer than the string passed.

  This is a worker function called by escapify_url and pure_escapify_url.  These
  functions differ on whether the function tries to detect and avoid
  double URL encoding (escapify_url) or not (pure_escapify_url)
  -------------------------------------------------------------------------*/

namespace
{
char *
escapify_url_common(Arena *arena, char *url, size_t len_in, int *len_out, char *dst, size_t dst_size, const unsigned char *map,
                    bool pure_escape)
{
  // codes_to_escape is a bitmap encoding the codes that should be escaped.
  // These are all the codes defined in section 2.4.3 of RFC 2396
  // (control, space, delims, and unwise) plus the tilde. In RFC 2396
  // the tilde is an "unreserved" character, but we escape it because
  // historically this is what the traffic_server has done.
  // Note that we leave codes beyond 127 unmodified.
  //
  // NOTE: any updates to this table should result in an update to:
  // tools/escape_mapper/escape_mapper.cc.
  static const unsigned char codes_to_escape[32] = {
    0xFF, 0xFF, 0xFF,
    0xFF,             // control
    0xB4,             // space " # %
    0x00, 0x00,       //
    0x0A,             // < >
    0x00, 0x00, 0x00, //
    0x1E, 0x80,       // [ \ ] ^ `
    0x00, 0x00,       //
    0x1F,             // { | } ~ DEL
    0x00, 0x00, 0x00,
    0x00, // all non-ascii characters unmodified
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00 //               .
  };

  static char hex_digit[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  if (!url || (dst && dst_size < len_in)) {
    *len_out = 0;
    return nullptr;
  }

  if (!map) {
    map = codes_to_escape;
  }

  // Count specials in the url, assuming that there won't be any.
  //
  int   count      = 0;
  char *p          = url;
  char *in_url_end = url + len_in;

  while (p < in_url_end) {
    unsigned char c = *p;
    if (map[c / 8] & (1 << (7 - c % 8))) {
      ++count;
    }
    ++p;
  }

  if (!count) {
    // The common case, no escapes, so just return the source string.
    //
    *len_out = len_in;
    if (dst) {
      ink_strlcpy(dst, url, dst_size);
    }
    return url;
  }

  // For each special char found, we'll need an escape string, which is
  // three characters long.  Count this and allocate the string required.
  //
  // make sure we take into account the characters we are substituting
  // for when we calculate out_len !!! in other words,
  // out_len = len_in + 3*count - count
  //
  size_t out_len = len_in + 2 * count;

  if (dst && (out_len + 1) > dst_size) {
    *len_out = 0;
    return nullptr;
  }

  // To play it safe, we null terminate the string we return in case
  // a module that expects null-terminated strings calls escapify_url,
  // so we allocate an extra byte for the EOS
  //
  char *new_url;

  if (dst) {
    new_url = dst;
  } else {
    new_url = arena->str_alloc(out_len + 1);
  }

  char *from = url;
  char *to   = new_url;

  while (from < in_url_end) {
    unsigned char c = *from;
    if (map[c / 8] & (1 << (7 - c % 8))) {
      /*
       * If two characters following a '%' don't need to be encoded, then it must
       * mean that the three character sequence is already encoded.  Just copy it over.
       */
      if (!pure_escape && (*from == '%') && ((from + 2) < in_url_end)) {
        unsigned char c1            = *(from + 1);
        unsigned char c2            = *(from + 2);
        bool          needsEncoding = ((map[c1 / 8] & (1 << (7 - c1 % 8))) || (map[c2 / 8] & (1 << (7 - c2 % 8))));
        if (!needsEncoding) {
          out_len -= 2;
          Debug("log-utils", "character already encoded..skipping %c, %c, %c", *from, *(from + 1), *(from + 2));
          *to++ = *from++;
          continue;
        }
      }

      *to++ = '%';
      *to++ = hex_digit[c / 16];
      *to++ = hex_digit[c % 16];
    } else {
      *to++ = *from;
    }
    from++;
  }
  *to = '\0'; // null terminate string

  *len_out = out_len;
  return new_url;
}
} // namespace

namespace Encoding
{
char *
escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst, size_t dst_size, const unsigned char *map)
{
  return escapify_url_common(arena, url, len_in, len_out, dst, dst_size, map, false);
}

char *
pure_escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst, size_t dst_size, const unsigned char *map)
{
  return escapify_url_common(arena, url, len_in, len_out, dst, dst_size, map, true);
}
}; // namespace Encoding
