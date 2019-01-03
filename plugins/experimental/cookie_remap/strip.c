/*
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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>
#include <inttypes.h>

#include "strip.h"

static int copy_whitespace(const char **r, const char *in_end, char **w, const char *out_end);

static int strip_whitespace(const char **r, const char **in_end);

/* Determine if there is room to store len bytes starting at p for an
 * object that ends at maxp.  This is not as simple as a less-than
 * comparison, because our code may increment p well beyond the end of
 * the object it originally pointed to (in complete violation of what
 * ANSI C says is legitimate).  The result is that p may wrap around.
 * This has been  observed with using stack buffers as arguments
 * from 32 bit programs running on 64-bit RHEL.
 */
#define ROOM(p, len, maxp) (((maxp) - ((p) + (len))) >= 0)

/* write c into *p if there's room, always incrementing *p.  This
 * implementation uses a do-loop to avoid several syntactic issues
 * when this macro is expanded in the context of if-then-else constructs.
 * It is expected that the compiler will optimize away the "while (0)"
 */
#define WRITE_C_IF_ROOM(p, maxp, c) \
  do {                              \
    if (ROOM(*(p), 1, (maxp)))      \
      **(p) = (c);                  \
    (*(p))++;                       \
  } while (0)

/* write s into *p if there's room, always adding slen to *p . This
 * implementation uses a do-loop to avoid several syntactic issues
 * when this macro is expanded in the context of if-then-else constructs.
 * It is expected that the compiler will optimize away the "while 0"
 */
#define WRITE_STR_IF_ROOM(p, maxp, s, slen) \
  do {                                      \
    if (ROOM(*(p), (slen), (maxp)))         \
      memcpy(*(p), (s), (slen));            \
    *(p) += (slen);                         \
  } while (0)

/* Write count spaces into *p if there's room, always adding count to *p.
 * The count argument is set to zero at the end of execution. This
 * implementation uses a do-loop to avoid several syntactic issues when
 * this macro is expanded in the context of if-then-else constructs.
 * It is expected that the compiler will optimize away the "while 0"
 */
#define WRITE_SPACES_IF_ROOM(p, maxp, slen) \
  do {                                      \
    if (ROOM(*(p), (slen), (maxp)))         \
      memset(*(p), ' ', (slen));            \
    *(p) += (slen);                         \
    (slen) = 0;                             \
  } while (0)

/*
 * File-scope data
 */

static const unsigned int allowed_flags = (STRIP_FLAG_LEAVE_WHITESP | STRIP_FLAG_STRIP_LOW | STRIP_FLAG_STRIP_HIGH |
                                           STRIP_FLAG_UNSAFE_QUOTES | STRIP_FLAG_UNSAFE_SLASHES | STRIP_FLAG_UNSAFE_SPACES);

static int
stripped_core(const char *r, const char *in_end, char **w, const char *out_end, unsigned int flags)
{
  int leading        = 1;    /* haven't yet written a non-space */
  int in_js_entity   = 0;    /* are we inside a javascript entity? */
  char in_quote_char = '\0'; /* in quoted region? which kind: '\'' or '"' */
  int space          = 0;    /* number of spaces pending */
  int stripped       = 0;    /* have we stripped since last output? */
  int in_tag         = 0;    /* are we inside a tag? */

  /* parse the string, stripping risky characters/sequences */
  for (/* already established */; r < in_end; r++) {
    unsigned char c = *r;
    if (in_tag) {
      switch (c) {
      case '>':
        if (!in_quote_char) {
          in_tag = 0;
        }
        break;

      case '"':
      case '\'':
        if (!in_quote_char) {
          in_quote_char = c;
        } else if (in_quote_char == c) {
          in_quote_char = '\0';
        }
        break;

      default:
        break; /* eat everything between < and > */
      }
    } else if (in_js_entity) {
      switch (c) {
      case '}':
        if (!in_quote_char) {
          in_js_entity = 0;
          if (r + 1 < in_end && *(r + 1) == ';') {
            r++;
          }
        }
        break;

      case '"':
      case '\'':
        if (!in_quote_char) {
          in_quote_char = c;
        } else if (in_quote_char == c) {
          in_quote_char = '\0';
        }
        break;

      default:
        break; /* eat everything between < and > */
      }
    } else {
      if (c == '<') {
        in_tag   = 1;
        stripped = 1;
      } else if (c == '&' && r + 1 < in_end && *(r + 1) == '{') {
        in_js_entity = 1;
        stripped     = 1;
        r++;
      } else if ((c < 0x07 && (flags & STRIP_FLAG_STRIP_LOW)) || (c >= 0x80 && (flags & STRIP_FLAG_STRIP_HIGH)) ||
                 (c == '"' && !(flags & STRIP_FLAG_UNSAFE_QUOTES)) || (c == '\'' && !(flags & STRIP_FLAG_UNSAFE_QUOTES)) ||
                 (c == '\\' && !(flags & STRIP_FLAG_UNSAFE_SLASHES)) || c == '>') {
        stripped = 1;
      } else if (c == ' ') {
        space++; /* don't collapse existing spaces */
      } else {
        /* we're ready to write an output character */
        if (leading) {
          leading  = 0; /* first non-whitespace character */
          stripped = 0;
          if (!(flags & STRIP_FLAG_LEAVE_WHITESP)) {
            space = 0;
          }
        }

        /* flush pending spaces */
        if (!space && stripped && !(flags & STRIP_FLAG_UNSAFE_SPACES)) {
          space = 1; /* replace stripped sequence with space */
        }
        stripped = 0; /* reset until next stripped sequence */
        WRITE_SPACES_IF_ROOM(w, out_end, space);

        /* Process as single character. */
        WRITE_C_IF_ROOM(w, out_end, c);
      }
    }
  }

  /* Restore trailing whitespace if asked */
  if (flags & STRIP_FLAG_LEAVE_WHITESP)
    WRITE_SPACES_IF_ROOM(w, out_end, space);

  return STRIP_RESULT_OK;
}

int
get_stripped(const char *in, ssize_t in_len, char *out, int *out_len, unsigned int flags)
{
  int retval = STRIP_RESULT_OK;
  const char *r, *in_end; /* where we read from, read limit */
  char *w, *out_end;      /* where we write to, write limit */

  /* validate params */
  if (in == NULL || in_len < 0 || out_len == NULL || *out_len < 0 || (out == NULL && *out_len > 0) || (flags & (~allowed_flags))) {
    if (out != NULL && out_len != NULL && *out_len > 0) {
      *out     = '\0';
      *out_len = 1;
    }
    return STRIP_RESULT_BAD_PARAM;
  }

  /* make room for null terminator in output and remove if present in in */
  (*out_len) -= out ? 1 : 0; /* make space for '\0' unless NULL out */
  if (in_len > 0 && in[in_len - 1] == '\0') {
    in_len--; /* don't count null terminator in input */
  }

  /* establish our read and write limits */
  r       = in;
  w       = out;
  in_end  = in + in_len;
  out_end = out + *out_len;

  /* strip leading and trailing whitespace, unless asked not to */
  if (!(flags & STRIP_FLAG_LEAVE_WHITESP)) {
    strip_whitespace(&r, &in_end);
  } else {
    copy_whitespace(&r, in_end, &w, out_end);
  }

  /* handle empty input case (null terminated or not) */
  if ((!(flags & STRIP_FLAG_LEAVE_WHITESP) && r >= in_end) || ((flags & STRIP_FLAG_LEAVE_WHITESP) && in_len == 0)) {
    WRITE_C_IF_ROOM(&w, out_end, '\0'); /* make out empty string */
    *out_len = 1;
    return STRIP_RESULT_EMPTY_IN; /* input is empty string */
  }

  /* call the core function that does actual checking and stripping */
  retval = stripped_core(r, in_end, &w, out_end, flags);

  /* null terminate */
  out_end += out_end ? 1 : 0;         /* undo decrement at start */
  WRITE_C_IF_ROOM(&w, out_end, '\0'); /* try to term at end of output */

  /* report the required/used length */
  *out_len = w - out;

  /* see if we ran out of space, but were otherwise ok */
  if (w > out_end && retval == STRIP_RESULT_OK) {
    retval = STRIP_RESULT_OUTLEN_SMALL;
  }

  if (retval != STRIP_RESULT_OK) {
    /* return the empty string on all errors */
    WRITE_C_IF_ROOM(&out, out_end, '\0'); /* make out the empty string */
    if (retval != STRIP_RESULT_OUTLEN_SMALL) {
      *out_len = 1; /* even if retried, we won't use more than 1 byte */
    }
  }

  return retval;
}

/*
 * Copy sequence of whitespace from r to w
 */
static int
copy_whitespace(const char **r, const char *in_end, char **w, const char *out_end)
{
  char c;
  while (*r < in_end && (c = **r) && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
    WRITE_C_IF_ROOM(w, out_end, c);
    (*r)++;
  }
  return 0;
}

static int
strip_whitespace(const char **r, const char **in_end)
{
  char c;
  while (*r < *in_end && (c = **r) && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
    (*r)++;
  }
  while (*in_end > *r && (c = *((*in_end) - 1)) && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
    (*in_end)--;
  }
  return 0;
}
