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

#include <tscore/ink_align.h>
#include "tscore/ink_config.h"
#include "tscore/ink_string.h"
#include <tscore/ink_assert.h>

#include "tscpp/util/ts_bw_format.h"

#ifdef TEST_LOG_UTILS

#include "proxy/logging/unit-tests/test_LogUtils.h"

#else

#include "proxy/hdrs/MIME.h"

#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string_view>
#include <cstdint>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "records/P_RecProcess.h"

#include "proxy/logging/LogUtils.h"
#include "proxy/logging/LogLimits.h"

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
  static long last_timestamp = 0;

  // safety check
  if (timestamp < 0) {
    static char bad_time[] = "Bad timestamp";
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
    long zone      = -tms->tm_gmtoff; // double negative!
    int offset;
    char sign;

    if (zone >= 0) {
      offset = zone / 60;
      sign   = '-';
    } else {
      offset = zone / -60;
      sign   = '+';
    }

    static char gmtstr[16];
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

  // safety check
  if (timestamp < 0) {
    static char bad_time[] = "Bad timestamp";
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

  // safety check
  if (timestamp < 0) {
    static char bad_time[] = "Bad timestamp";
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
  LogUtils::strip_trailing_newline

  This routine examines the given string buffer to see if the last
  character before the trailing NULL is a newline ('\n').  If so, it will
  be replaced with a NULL, thus stripping it and reducing the length of
  the string by one.
  -------------------------------------------------------------------------*/

void
LogUtils::strip_trailing_newline(char *buf)
{
  if (buf != nullptr) {
    int len = ::strlen(buf);
    if (len > 0) {
      if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
      }
    }
  }
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
  char *p = static_cast<char *>(memchr(type_str, ';', *type_len));
  if (p) {
    *type_len = p - type_str;
  }
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

swoc::TextView
LogUtils::get_unrolled_filename(swoc::TextView rolled_filename)
{
  auto unrolled_name = rolled_filename;

  // A rolled log will look something like:
  //   squid.log_some.hostname.com.20191029.18h15m02s-20191029.18h30m02s.old
  auto suffix = rolled_filename;

  if (auto idx = suffix.find('.'); idx != swoc::TextView::npos) {
    suffix.remove_prefix(idx + 1);
    // Using the above squid.log example, suffix now looks like:
    //   log_some.hostname.com.20191029.18h15m02s-20191029.18h30m02s.old

    // Some suffixes do not have the hostname.  Rolled diags.log files will look
    // something like this, for example:
    //   diags.log.20191114.21h43m16s-20191114.21h43m17s.old
    //
    // For these, the second delimiter will be a period. For this reason, we also
    // split_prefix_at with a period as well.
    if (swoc::TextView::npos != (idx = suffix.find_first_of("_."))) {
      suffix.remove_prefix(idx + 1);
      // ' + 1' to remove the '_' or second '.':
      return unrolled_name.remove_suffix(suffix.size() + 1);
    }
  }
  // If there isn't a '.' or an '_' after the first '.', then this
  // doesn't look like a rolled file.
  return unrolled_name;
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
    if (size_bytes) {
      *size_bytes = stat_data.st_size;
    }
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
      char *prefix = nullptr;

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
        if (size_bytes) {
          *size_bytes = 0;
        }
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
      if (limit_data.rlim_cur != static_cast<rlim_t>(RLIM_INFINITY)) {
        if (has_size_limit) {
          *has_size_limit = true;
        }
        if (current_size_limit_bytes) {
          *current_size_limit_bytes = limit_data.rlim_cur;
        }
      } else {
        if (has_size_limit) {
          *has_size_limit = false;
        }
      }
    }
  }

  return ret_val;
}

namespace
{
// Get a string out of a MIMEField using one of its member functions, and put it into a buffer writer, terminated with a nul.
//
void
marshalStr(swoc::FixedBufferWriter &bw, const MIMEField &mf, const char *(MIMEField::*get_func)(int *length) const)
{
  int length;
  const char *data = (mf.*get_func)(&length);

  if (!data or (*data == '\0')) {
    // Empty string.  This is a problem, since it would result in two successive nul characters, which indicates the end of the
    // marshaled hearer.  Change the string to a single blank character.

    static const char Blank[] = " ";
    data                      = Blank;
    length                    = 1;
  }

  bw << std::string_view(data, length) << '\0';
}

void
unmarshalStr(swoc::FixedBufferWriter &bw, const char *&data)
{
  bw << '{';

  while (*data) {
    bw << *(data++);
  }

  // Skip over terminal nul.
  ++data;

  bw << '}';
}

} // end anonymous namespace

namespace LogUtils
{
// Marshals header tags and values together, with a single terminating nul character.  Returns buffer space required.  'buf' points
// to where to put the marshaled data.  If 'buf' is null, no data is marshaled, but the function returns the amount of space that
// would have been used.
//
int
marshalMimeHdr(MIMEHdr *hdr, char *buf)
{
  std::size_t bwSize = buf ? SIZE_MAX : 0;

  swoc::FixedBufferWriter bw(buf, bwSize);

  if (hdr) {
    for (auto const &mfp : *hdr) {
      marshalStr(bw, mfp, &MIMEField::name_get);
      marshalStr(bw, mfp, &MIMEField::value_get);
    }
  }

  bw << '\0';

  return int(INK_ALIGN_DEFAULT(bw.extent()));
}

// Unmarshaled/printable format is {{{tag1}:{value1}}{{tag2}:{value2}} ... }
//
int
unmarshalMimeHdr(char **buf, char *dest, int destLength)
{
  ink_assert(buf != nullptr);

  const char *data = *buf;

  ink_assert(data != nullptr);

  swoc::FixedBufferWriter bw(dest, destLength);

  bw.write('{');

  int pairEndFallback{0}, pairEndFallback2{0}, pairSeparatorFallback{0};

  while (*data) {
    if (!bw.error()) {
      pairEndFallback2 = pairEndFallback;
      pairEndFallback  = bw.size();
    }

    // Add open bracket of pair.
    //
    bw.write('{');

    // Unmarshal field name.
    unmarshalStr(bw, data);

    bw.write(':');

    if (!bw.error()) {
      pairSeparatorFallback = bw.size();
    }

    // Unmarshal field value.
    unmarshalStr(bw, data);

    // Add close bracket of pair.
    bw.write('}');

  } // end for loop

  bw.write('}');

  if (bw.error()) {
    // The output buffer wasn't big enough.
    bw.discard(bw.extent() - destLength); // clip to max buffer size.

    static std::string_view FULL_ELLIPSES("...}}}");
    ink_assert(bw.size() == size_t(destLength));

    if ((pairSeparatorFallback > pairEndFallback) and ((pairSeparatorFallback + FULL_ELLIPSES.size()) < size_t(destLength))) {
      // In the report, we can show the existence of the last partial tag/value pair, and maybe part of the value.  If we only
      // show part of the value, we want to end it with an elipsis, to make it clear it's not complete.

      bw.discard(FULL_ELLIPSES.size());
      bw.write(FULL_ELLIPSES);

    } else if (pairEndFallback and (pairEndFallback < destLength)) {
      bw.discard(destLength - pairEndFallback);
      bw.write('}');

    } else if ((pairSeparatorFallback > pairEndFallback2) and
               ((pairSeparatorFallback + FULL_ELLIPSES.size()) < size_t(destLength))) {
      bw.discard(FULL_ELLIPSES.size());
      bw.write(FULL_ELLIPSES);

    } else if (pairEndFallback2 and (pairEndFallback2 < destLength)) {
      bw.discard(destLength - pairEndFallback2);
      bw.write('}');

    } else if (destLength > 1) {
      bw.discard(1);
      bw.write('}');
    } else {
      bw.clear();
    }
  }

  *buf += INK_ALIGN_DEFAULT(data - *buf + 1);

  return bw.size();
}

} // end namespace LogUtils
