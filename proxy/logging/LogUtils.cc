/** @file

 This file contains a set of utility routines that are used throughout the
 logging implementation.

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
#include "ts/ink_config.h"
#include "ts/ink_string.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "P_RecProcess.h"
// REC_SIGNAL_LOGGING_ERROR    is defined in I_RecSignals.h
// REC_SIGNAL_LOGGING_WARNING  is defined in I_RecSignals.h

#include "LogUtils.h"
#include "LogLimits.h"

/*-------------------------------------------------------------------------
  LogUtils::timestamp_to_str

  This routine will convert a timestamp (seconds) into a short string,
  of the format "%Y%m%d.%Hh%Mm%Ss".

  Since the resulting buffer is passed in, this routine is thread-safe.
  Return value is the number of characters placed into the array, not
  including the NULL.
  -------------------------------------------------------------------------*/

int
LogUtils::timestamp_to_str(long timestamp, char *buf, int size)
{
  static const char *format_str = "%Y%m%d.%Hh%Mm%Ss";
  struct tm res;
  struct tm *tms;
  tms = ink_localtime_r((const time_t *)&timestamp, &res);
  return strftime(buf, size, format_str, tms);
}

/*-------------------------------------------------------------------------
  LogUtils::timestamp_to_netscape_str

  This routine will convert a timestamp (seconds) into a string compatible
  with the Netscape logging formats.

  This routine is intended to be called from the (single) logging thread,
  and is therefore NOT MULTITHREADED SAFE.  There is a single, static,
  string buffer that the time string is constructed into and returned.
  -------------------------------------------------------------------------*/

char *
LogUtils::timestamp_to_netscape_str(long timestamp)
{
  static char timebuf[64]; // NOTE: not MT safe
  static char gmtstr[16];
  static long last_timestamp = 0;
  static char bad_time[]     = "Bad timestamp";

  // safety check
  if (timestamp < 0) {
    return bad_time;
  }
  //
  // since we may have many entries per second, lets only do the
  // formatting if we actually have a new timestamp.
  //

  if (timestamp != last_timestamp) {
    //
    // most of this garbage is simply to find out the offset from GMT,
    // taking daylight savings into account.
    //
    struct tm res;
    struct tm *tms = ink_localtime_r((const time_t *)&timestamp, &res);
#if defined(solaris)
    long zone = (tms->tm_isdst > 0) ? altzone : timezone;
#else
    long zone = -tms->tm_gmtoff; // double negative!
#endif
    int offset;
    char sign;

    if (zone >= 0) {
      offset = zone / 60;
      sign   = '-';
    } else {
      offset = zone / -60;
      sign   = '+';
    }
    int glen = snprintf(gmtstr, 16, "%c%.2d%.2d", sign, offset / 60, offset % 60);

    strftime(timebuf, 64 - glen, "%d/%b/%Y:%H:%M:%S ", tms);
    ink_strlcat(timebuf, gmtstr, sizeof(timebuf));
    last_timestamp = timestamp;
  }
  return timebuf;
}

/*-------------------------------------------------------------------------
  LogUtils::timestamp_to_date_str

  This routine will convert a timestamp (seconds) into a W3C compatible
  date string.
  -------------------------------------------------------------------------*/

char *
LogUtils::timestamp_to_date_str(long timestamp)
{
  static char timebuf[64]; // NOTE: not MT safe
  static long last_timestamp = 0;
  static char bad_time[]     = "Bad timestamp";

  // safety check
  if (timestamp < 0) {
    return bad_time;
  }
  //
  // since we may have many entries per second, lets only do the
  // formatting if we actually have a new timestamp.
  //

  if (timestamp != last_timestamp) {
    struct tm res;
    struct tm *tms = ink_localtime_r((const time_t *)&timestamp, &res);
    strftime(timebuf, 64, "%Y-%m-%d", tms);
    last_timestamp = timestamp;
  }
  return timebuf;
}

/*-------------------------------------------------------------------------
  LogUtils::timestamp_to_time_str

  This routine will convert a timestamp (seconds) into a W3C compatible
  time string.
  -------------------------------------------------------------------------*/

char *
LogUtils::timestamp_to_time_str(long timestamp)
{
  static char timebuf[64]; // NOTE: not MT safe
  static long last_timestamp = 0;
  static char bad_time[]     = "Bad timestamp";

  // safety check
  if (timestamp < 0) {
    return bad_time;
  }
  //
  // since we may have many entries per second, lets only do the
  // formatting if we actually have a new timestamp.
  //

  if (timestamp != last_timestamp) {
    struct tm res;
    struct tm *tms = ink_localtime_r((const time_t *)&timestamp, &res);
    strftime(timebuf, 64, "%H:%M:%S", tms);
    last_timestamp = timestamp;
  }
  return timebuf;
}

/*-------------------------------------------------------------------------
  LogUtils::manager_alarm

  This routine provides a convenient abstraction for sending the traffic
  server manager process an alarm.  The logging system can send
  LOG_ALARM_N_TYPES different types of alarms, as defined in LogUtils.h.
  Subsequent alarms of the same type will override the previous alarm
  entry.
  -------------------------------------------------------------------------*/

void
LogUtils::manager_alarm(LogUtils::AlarmType alarm_type, const char *msg, ...)
{
  char msg_buf[LOG_MAX_FORMATTED_LINE];
  va_list ap;

  ink_assert(alarm_type >= 0 && alarm_type < LogUtils::LOG_ALARM_N_TYPES);

  if (msg == NULL) {
    snprintf(msg_buf, sizeof(msg_buf), "No Message");
  } else {
    va_start(ap, msg);
    vsnprintf(msg_buf, LOG_MAX_FORMATTED_LINE, msg, ap);
    va_end(ap);
  }

  switch (alarm_type) {
  case LogUtils::LOG_ALARM_ERROR:
    RecSignalManager(REC_SIGNAL_LOGGING_ERROR, msg_buf);
    break;

  case LogUtils::LOG_ALARM_WARNING:
    RecSignalManager(REC_SIGNAL_LOGGING_WARNING, msg_buf);
    break;

  default:
    ink_assert(false);
  }
}

/*-------------------------------------------------------------------------
  LogUtils::strip_trailing_newline

  This routine examines the given string buffer to see if the last
  character before the trailing NULL is a newline ('\n').  If so, it will
  be replaced with a NULL, thus stripping it and reducing the length of
  the string by one.
  -------------------------------------------------------------------------*/

void
LogUtils::strip_trailing_newline(char *buf)
{
  if (buf != NULL) {
    int len = ::strlen(buf);
    if (len > 0) {
      if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
      }
    }
  }
}

/*-------------------------------------------------------------------------
  LogUtils::escapify_url

  This routine will escapify a URL to remove spaces (and perhaps other ugly
  characters) from a URL and replace them with a hex escape sequence.
  Since the escapes are larger (multi-byte) than the characters being
  replaced, the string returned will be longer than the string passed.
  -------------------------------------------------------------------------*/

char *
LogUtils::escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst, size_t dst_size, const unsigned char *map)
{
  // codes_to_escape is a bitmap encoding the codes that should be escaped.
  // These are all the codes defined in section 2.4.3 of RFC 2396
  // (control, space, delims, and unwise) plus the tilde. In RFC 2396
  // the tilde is an "unreserved" character, but we escape it because
  // historically this is what the traffic_server has done.
  // Note that we leave codes beyond 127 unmodified.
  //
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
    return NULL;
  }

  if (!map)
    map = codes_to_escape;

  // Count specials in the url, assuming that there won't be any.
  //
  int count        = 0;
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
    if (dst)
      ink_strlcpy(dst, url, dst_size);
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

  if (dst && out_len > dst_size) {
    *len_out = 0;
    return NULL;
  }

  // To play it safe, we null terminate the string we return in case
  // a module that expects null-terminated strings calls escapify_url,
  // so we allocate an extra byte for the EOS
  //
  char *new_url;

  if (dst)
    new_url = dst;
  else
    new_url = (char *)arena->str_alloc(out_len + 1);

  char *from = url;
  char *to   = new_url;

  while (from < in_url_end) {
    unsigned char c = *from;
    if (map[c / 8] & (1 << (7 - c % 8))) {
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

/*-------------------------------------------------------------------------
  LogUtils::remove_content_type_attributes

  HTTP allows content types to have attributes following the main type and
  subtype.  For example, attributes of text/html might be charset=iso-8859.
  The content type attributes are not logged, so this function strips them
  from the given buffer, if present.
  -------------------------------------------------------------------------*/

void
LogUtils::remove_content_type_attributes(char *type_str, int *type_len)
{
  if (!type_str) {
    *type_len = 0;
    return;
  }
  // Look for a semicolon and cut out everything after that
  //
  char *p = (char *)memchr(type_str, ';', *type_len);
  if (p) {
    *type_len = p - type_str;
  }
}

/*-------------------------------------------------------------------------
  LogUtils::timestamp_to_hex_str

  This routine simply writes the given timestamp integer [time_t] in the equivalent
  hexadecimal string format "xxxxxxxxxx" into the provided buffer [buf] of
  size [bufLen].

  It returns 1 if the provided buffer is not big enough to hold the
  equivalent ip string (and its null terminator), and 0 otherwise.
  If the buffer is not big enough, only the ip "segments" that completely
  fit into it are written, and the buffer is null terminated.
  -------------------------------------------------------------------------*/

int
LogUtils::timestamp_to_hex_str(unsigned ip, char *buf, size_t bufLen, size_t *numCharsPtr)
{
  static const char *table = "0123456789abcdef@";
  int retVal               = 1;
  int shift                = 28;
  if (buf && bufLen > 0) {
    if (bufLen > 8)
      bufLen = 8;
    for (retVal = 0; retVal < (int)bufLen;) {
      buf[retVal++] = (char)table[((ip >> shift) & 0xf)];
      shift -= 4;
    }

    if (numCharsPtr) {
      *numCharsPtr = (size_t)retVal;
    }
    retVal = (retVal == 8) ? 0 : 1;
  }
  return retVal;
}

/*
int
LogUtils::ip_to_str (unsigned ip, char *str, unsigned len)
{
    int ret = snprintf (str, len, "%u.%u.%u.%u",
                            (ip >> 24) & 0xff,
                            (ip >> 16) & 0xff,
                            (ip >> 8)  & 0xff,
                            ip         & 0xff);

    return ((ret <= (int)len)? ret : (int)len);
}
*/

// return the seconds remaining until the time of the next roll given
// the current time, the rolling offset, and the rolling interval
//
int
LogUtils::seconds_to_next_roll(time_t time_now, int rolling_offset, int rolling_interval)
{
  struct tm lt;
  ink_localtime_r((const time_t *)&time_now, &lt);
  int sidl = lt.tm_sec + lt.tm_min * 60 + lt.tm_hour * 3600;
  int tr   = rolling_offset * 3600;
  return ((tr >= sidl ? (tr - sidl) % rolling_interval : (86400 - (sidl - tr)) % rolling_interval));
}

// Checks if the file pointed to by full_filename either is a regular
// file or a pipe and has write permission, or, if the file does not
// exist, if the path prefix of full_filename names a directory that
// has both execute and write permissions, so there will be no problem
// creating the file. If the size_bytes pointer is not NULL, it returns
// the size of the file through it.
// Also checks the current size limit for the file. If there is a
// limit and has_size_limit is not null, *has_size_limit is set to
// true. If there is no limit and has_size_limit is not null,
// *has_size_limit is set to false.  If there is a limit and if the
// current_size_limit_bytes pointer is not null, it returns the limit
// through it.
//
// returns:
//  0 on success
// -1 on system error (no permission, etc.)
//  1 if the file full_filename points to is neither a regular file
//    nor a pipe
//
int
LogUtils::file_is_writeable(const char *full_filename, off_t *size_bytes, bool *has_size_limit, uint64_t *current_size_limit_bytes)
{
  int ret_val = 0;
  int e;
  struct stat stat_data;

  e = stat(full_filename, &stat_data);
  if (e == 0) {
    // stat succeeded, check if full_filename points to a regular
    // file/fifo and if so, check if file has write permission
    //
    if (!(S_ISREG(stat_data.st_mode) || S_ISFIFO(stat_data.st_mode))) {
      ret_val = 1;
    } else if (!(stat_data.st_mode & S_IWUSR)) {
      errno   = EACCES;
      ret_val = -1;
    }
    if (size_bytes)
      *size_bytes = stat_data.st_size;
  } else {
    // stat failed
    //
    if (errno != ENOENT) {
      // can't stat file
      //
      ret_val = -1;
    } else {
      // file does not exist, check that the prefix is a directory with
      // write and execute permissions

      char *dir;
      char *prefix = 0;

      // search for forward or reverse slash in full_filename
      // starting from the end
      //
      const char *slash = strrchr(full_filename, '/');
      if (slash) {
        size_t prefix_len = slash - full_filename + 1;
        prefix            = new char[prefix_len + 1];
        memcpy(prefix, full_filename, prefix_len);
        prefix[prefix_len] = 0;
        dir                = prefix;
      } else {
        dir = (char *)"."; // full_filename has no prefix, use .
      }

      // check if directory is executable and writeable
      //
      e = access(dir, X_OK | W_OK);
      if (e < 0) {
        ret_val = -1;
      } else {
        if (size_bytes)
          *size_bytes = 0;
      }

      if (prefix) {
        delete[] prefix;
      }
    }
  }

  // check for the current filesize limit
  //
  if (ret_val == 0) {
    struct rlimit limit_data;
    e = getrlimit(RLIMIT_FSIZE, &limit_data);
    if (e < 0) {
      ret_val = -1;
    } else {
      if (limit_data.rlim_cur != (rlim_t)RLIM_INFINITY) {
        if (has_size_limit)
          *has_size_limit = true;
        if (current_size_limit_bytes)
          *current_size_limit_bytes = limit_data.rlim_cur;
      } else {
        if (has_size_limit)
          *has_size_limit = false;
      }
    }
  }

  return ret_val;
}
