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

/***************************************************************************
 LogUtils.cc

 This file contains a set of utility routines that are used throughout the
 logging implementation.

 
 ***************************************************************************/

#include "ink_config.h"
#include "ink_unused.h"

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
#if (HOST_OS == solaris)
#include <netdb.h>
#else
#include "/usr/include/netdb.h" // need following instead of <netdb.h>
#endif


#include "P_RecProcess.h"
#define LOG_SignalManager             REC_SignalManager
// REC_SIGNAL_LOGGING_ERROR    is defined in I_RecSignals.h
// REC_SIGNAL_LOGGING_WARNING  is defined in I_RecSignals.h


#include "Compatability.h"

#include "LogUtils.h"
#include "LogLimits.h"
#include "LogFormatType.h"



/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogUtils::LogUtils(DoNotConstruct object)
{
  ink_release_assert(!"you can't construct a LogUtils object");
}

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
  tms = ink_localtime_r((const time_t *) &timestamp, &res);
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
  static char timebuf[64];      // NOTE: not MT safe
  static char gmtstr[16];
  static long last_timestamp = 0;
  static char bad_time[] = "Bad timestamp";

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
#ifdef NEED_ALTZONE_DEFINED
    time_t altzone = timezone;
#endif
    struct tm res;
    struct tm *tms = ink_localtime_r((const time_t *) &timestamp, &res);
#if (HOST_OS == freebsd) || (HOST_OS == darwin)
    long zone = -tms->tm_gmtoff;        // double negative!
#else
    long zone = (tms->tm_isdst > 0) ? altzone : timezone;
#endif
    int offset;
    char sign;

    if (zone >= 0) {
      offset = zone / 60;
      sign = '-';
    } else {
      offset = zone / -60;
      sign = '+';
    }
    int glen = ink_snprintf(gmtstr, 16, "%c%.2d%.2d",
                            sign, offset / 60, offset % 60);

    strftime(timebuf, 64 - glen, "%d/%b/%Y:%H:%M:%S ", tms);
    strncat(timebuf, gmtstr, sizeof(timebuf) - strlen(timebuf) - 1);
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
  static char timebuf[64];      // NOTE: not MT safe
  static long last_timestamp = 0;
  static char bad_time[] = "Bad timestamp";

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
    struct tm *tms = ink_localtime_r((const time_t *) &timestamp, &res);
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
  static char timebuf[64];      // NOTE: not MT safe
  static long last_timestamp = 0;
  static char bad_time[] = "Bad timestamp";

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
    struct tm *tms = ink_localtime_r((const time_t *) &timestamp, &res);
    strftime(timebuf, 64, "%H:%M:%S", tms);
    last_timestamp = timestamp;
  }
  return timebuf;
}

/*-------------------------------------------------------------------------
  LogUtils::ip_from_host

  This routine performs a DNS lookup on the given hostname and returns the
  associated IP address in the form of a single unsigned int (s_addr).  If
  the host is not found or some other error occurs, then zero is returned.
  -------------------------------------------------------------------------*/

unsigned
LogUtils::ip_from_host(char *host)
{
  unsigned ip = 0;
  ink_gethostbyname_r_data d;
  struct hostent *he = 0;

  ink_assert(host != NULL);

  he = ink_gethostbyname_r(host, &d);

  if (he != NULL) {
    ip = ((struct in_addr *) (he->h_addr_list[0]))->s_addr;
  }

  return ip;
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
    ink_vsnprintf(msg_buf, LOG_MAX_FORMATTED_LINE, msg, ap);
    va_end(ap);
  }

  switch (alarm_type) {
  case LogUtils::LOG_ALARM_ERROR:
    LOG_SignalManager(REC_SIGNAL_LOGGING_ERROR, msg_buf);
    break;

  case LogUtils::LOG_ALARM_WARNING:
    LOG_SignalManager(REC_SIGNAL_LOGGING_WARNING, msg_buf);
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
    int len =::strlen(buf);
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
LogUtils::escapify_url(Arena * arena, char *url, int len_in, int *len_out)
{
  // codes_to_escape is a bitmap encoding the codes that should be escaped.
  // These are all the codes defined in section 2.4.3 of RFC 2396 
  // (control, space, delims, and unwise) plus the tilde. In RFC 2396
  // the tilde is an "unreserved" character, but we escape it because
  // historically this is what the traffic_server has done.
  // Note that we leave codes beyond 127 unmodified.
  //
  static unsigned char codes_to_escape[32] = {
    0xFF, 0xFF, 0xFF, 0xFF,     // control
    0xB4,                       // space " # %
    0x00, 0x00,                 //
    0x0A,                       // < >
    0x00, 0x00, 0x00,           // 
    0x1E, 0x80,                 // [ \ ] ^ `
    0x00, 0x00,                 // 
    0x1F,                       // { | } ~ DEL
    0x00, 0x00, 0x00, 0x00,     // all non-ascii characters unmodified
    0x00, 0x00, 0x00, 0x00,     //               .
    0x00, 0x00, 0x00, 0x00,     //               .
    0x00, 0x00, 0x00, 0x00      //               .
  };

  static char hex_digit[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C',
    'D', 'E', 'F'
  };

  if (!url) {
    *len_out = 0;
    return (NULL);
  }
  // Count specials in the url, assuming that there won't be any.
  //
  int count = 0;
  char *p = url;
  char *in_url_end = url + len_in;
  while (p < in_url_end) {
    register unsigned char c = *p;
    if (codes_to_escape[c / 8] & (1 << (7 - c % 8))) {
      count++;
    }
    p++;
  }

  if (!count) {
    // The common case, no escapes, so just return the source string.
    //
    *len_out = len_in;
    return url;
  }
  // For each special char found, we'll need an escape string, which is
  // three characters long.  Count this and allocate the string required.
  //
  // make sure we take into account the characters we are substituting
  // for when we calculate out_len !!! in other words, 
  // out_len = len_in + 3*count - count
  //
  int out_len = len_in + 2 * count;

  // To play it safe, we null terminate the string we return in case
  // a module that expects null-terminated strings calls escapify_url,
  // so we allocate an extra byte for the EOS
  //
  char *new_url = (char *) arena->str_alloc(out_len + 1);

  char *from = url;
  char *to = new_url;
  while (from < in_url_end) {
    register unsigned char c = *from;
    if (codes_to_escape[c / 8] & (1 << (7 - c % 8))) {
      *to++ = '%';
      *to++ = hex_digit[c / 16];
      *to++ = hex_digit[c % 16];
    } else {
      *to++ = *from;
    }
    from++;
  }
  *to = 0;                      // null terminate string

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
  char *p = (char *) memchr(type_str, ';', *type_len);
  if (p) {
    *type_len = p - type_str;
  }
}

/*------------------------------------------------------------------------- 
  LogUtils::ip_to_hex_str

  This routine simply writes the given IP integer [ip] in the equivalent
  hexadecimal string format "xxxxxxxxxx" into the provided buffer [buf] of
  size [bufLen]. 

  It returns 1 if the provided buffer is not big enough to hold the 
  equivalent ip string (and its null terminator), and 0 otherwise.
  If the buffer is not big enough, only the ip "segments" that completely
  fit into it are written, and the buffer is null terminated.

  If a non-null pointer to size_t [numCharsPtr] is given, then the
  length of the ip string is written there (this is the same one would
  get from calling strlen(buf) after a call to ip_to_str, so the null
  string terminator is not counted).

  -------------------------------------------------------------------------*/

int
LogUtils::ip_to_hex_str(unsigned ip, char *buf, size_t bufLen, size_t * numCharsPtr)
{
  static const char *table = "0123456789abcdef@";
  int retVal = 1;
  if (buf && bufLen > 0) {
    if (bufLen > 8)
      bufLen = 8;
    for (retVal = 0; retVal < (int) bufLen;) {
      buf[retVal++] = (char) table[(ip & 0xf)];
      ip >>= 4;
    }

    if (numCharsPtr) {
      *numCharsPtr = (size_t) retVal;
    }
    retVal = (retVal == 8) ? 0 : 1;
  }
  return retVal;
};

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
LogUtils::timestamp_to_hex_str(unsigned ip, char *buf, size_t bufLen, size_t * numCharsPtr)
{
  static const char *table = "0123456789abcdef@";
  int retVal = 1;
  int shift = 28;
  if (buf && bufLen > 0) {
    if (bufLen > 8)
      bufLen = 8;
    for (retVal = 0; retVal < (int) bufLen;) {
      buf[retVal++] = (char) table[((ip >> shift) & 0xf)];
      shift -= 4;
    }

    if (numCharsPtr) {
      *numCharsPtr = (size_t) retVal;
    }
    retVal = (retVal == 8) ? 0 : 1;
  }
  return retVal;
};

/*
int
LogUtils::ip_to_str (unsigned ip, char *str, unsigned len)
{
    int ret = ink_snprintf (str, len, "%u.%u.%u.%u",
			    (ip >> 24) & 0xff, 
			    (ip >> 16) & 0xff, 
			    (ip >> 8)  & 0xff, 
			    ip         & 0xff);

    return ((ret <= (int)len)? ret : (int)len);
}
*/

/*------------------------------------------------------------------------- 
  LogUtils::ip_to_str

  This routine simply writes the given IP integer [ip] in the equivalent
  string format "aaa.bbb.ccc.ddd" into the provided buffer [buf] of size
  [bufLen]. 

  It returns 1 if the provided buffer is not big enough to hold the 
  equivalent ip string (and its null terminator), and 0 otherwise.
  If the buffer is not big enough, only the ip "segments" that completely
  fit into it are written, and the buffer is null terminated.

  If a non-null pointer to size_t [numCharsPtr] is given, then the
  length of the ip string is written there (this is the same one would
  get from calling strlen(buf) after a call to ip_to_str, so the null
  string terminator is not counted).

  -------------------------------------------------------------------------*/

int
LogUtils::ip_to_str(unsigned ip, char *buf, size_t bufLen, size_t * numCharsPtr)
{

  static const char *table[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "01", "11", "21", "31", "41", "51", "61", "71", "81", "91",
    "02", "12", "22", "32", "42", "52", "62", "72", "82", "92",
    "03", "13", "23", "33", "43", "53", "63", "73", "83", "93",
    "04", "14", "24", "34", "44", "54", "64", "74", "84", "94",
    "05", "15", "25", "35", "45", "55", "65", "75", "85", "95",
    "06", "16", "26", "36", "46", "56", "66", "76", "86", "96",
    "07", "17", "27", "37", "47", "57", "67", "77", "87", "97",
    "08", "18", "28", "38", "48", "58", "68", "78", "88", "98",
    "09", "19", "29", "39", "49", "59", "69", "79", "89", "99",
    "001", "101", "201", "301", "401", "501", "601", "701", "801", "901",
    "011", "111", "211", "311", "411", "511", "611", "711", "811", "911",
    "021", "121", "221", "321", "421", "521", "621", "721", "821", "921",
    "031", "131", "231", "331", "431", "531", "631", "731", "831", "931",
    "041", "141", "241", "341", "441", "541", "641", "741", "841", "941",
    "051", "151", "251", "351", "451", "551", "651", "751", "851", "951",
    "061", "161", "261", "361", "461", "561", "661", "761", "861", "961",
    "071", "171", "271", "371", "471", "571", "671", "771", "871", "971",
    "081", "181", "281", "381", "481", "581", "681", "781", "881", "981",
    "091", "191", "291", "391", "491", "591", "691", "791", "891", "991",
    "002", "102", "202", "302", "402", "502", "602", "702", "802", "902",
    "012", "112", "212", "312", "412", "512", "612", "712", "812", "912",
    "022", "122", "222", "322", "422", "522", "622", "722", "822", "922",
    "032", "132", "232", "332", "432", "532", "632", "732", "832", "932",
    "042", "142", "242", "342", "442", "542", "642", "742", "842", "942",
    "052", "152", "252", "352", "452", "552"
  };

  int retVal = 0;
  register unsigned int numChars = 0;
  register int s;
  register unsigned int n;
  register int shft = 24;
  const char *d;
  for (int i = 0; i < 4; ++i) {
    s = (ip >> shft) & 0xff;
    d = table[s];
    n = (s > 99 ? 4 : (s > 9 ? 3 : 2)); // '.' or '\0' included
    if (bufLen >= n) {
      switch (n) {
      case 4:
        buf[numChars++] = d[2];
      case 3:
        buf[numChars++] = d[1];
      default:
        buf[numChars++] = d[0];
      }
      buf[numChars++] = (i < 3 ? '.' : 0);
      bufLen -= n;
      shft -= 8;
    } else {
      retVal = 1;               // not enough buffer space
      buf[numChars - 1] = 0;    // null terminate the buffer
      break;
    }
  }
  if (numCharsPtr) {
    *numCharsPtr = numChars - 1;
  }
  return retVal;
};

/*------------------------------------------------------------------------- 
  LogUtils::str_to_ip

  This routine converts the string form of an IP address
  ("aaa.bbb.ccc.ddd") to its equivalent integer form.
  -------------------------------------------------------------------------*/

unsigned
LogUtils::str_to_ip(char *ipstr)
{
  unsigned ip = 0;
  unsigned a, b, c, d;
  // coverity[secure_coding]
  int ret = sscanf(ipstr, "%u.%u.%u.%u", &a, &b, &c, &d);
  if (ret == 4) {
    ip = d | (c << 8) | (b << 16) | (a << 24);
  }
  return ip;
}

/*------------------------------------------------------------------------- 
  LogUtils::valid_ipstr_format

  This routine checks for a string formated as an ip address.
  It makes sure that the format is valid, and allows at most three digit
  numbers between the dots, but it does not check for an invalid three digit
  number (e.g., 111.222.333.444 would return true)

  -------------------------------------------------------------------------*/

bool LogUtils::valid_ipstr_format(char *ipstr)
{
  ink_assert(ipstr);

  char
    c;
  bool
    retVal = true;
  bool
    lastDot = true;
  int
    i = 0, numDots = 0;
  int
    numDigits = 0;
  while (c = ipstr[i++], c != 0) {
    if (c == '.') {
      if (lastDot || (++numDots > 3)) {
        retVal = false;         // consecutive dots, or more than 3
        // or dot at beginning
        break;
      }
      lastDot = true;
      numDigits = 0;
    } else if (ParseRules::is_digit(c)) {
      ++numDigits;
      if (numDigits > 3) {
        retVal = false;
        break;
      }
      lastDot = false;
    } else {
      retVal = false;           // no digit or dot
      break;
    }
  }

  // make sure there are no less than three dots and that the last char
  // is not a dot
  //
  return (retVal == true ? (numDots == 3 && !lastDot) : false);
}

// return the seconds remaining until the time of the next roll given
// the current time, the rolling offset, and the rolling interval
//
int
LogUtils::seconds_to_next_roll(time_t time_now, int rolling_offset, int rolling_interval)
{
  struct tm lt;
  ink_localtime_r((const time_t *) &time_now, &lt);
  int sidl = lt.tm_sec + lt.tm_min * 60 + lt.tm_hour * 3600;
  int tr = rolling_offset * 3600;
  return ((tr >= sidl ? (tr - sidl) % rolling_interval : (86400 - (sidl - tr)) % rolling_interval));
}

// Converts the ink64 val argument to a null terminated string, returning a 
// pointer to the beginning of the string.
//
// The string is stored in the provided buffer if the buffer is large 
// enough, otherwise the buffer is not touched, and the function returns NULL.
//
// The argument total_chars returns the number of characters that were
// converted or that would have been converted if the buffer had been large 
// enough. total_chars includes the null terminator.
// 
// The optional arguments req_width and pad_char allow users to
// specify a requested width of the output string (including the null
// terminator), and a padding character to use to reach that width. Padding
// happens only if the resulting string is shorter than the requested width
// and if there is room in the buffer to accomodate the padding.
//
char *
LogUtils::ink64_to_str(char *buf, unsigned int buf_size, ink64 val,
                       unsigned int *total_chars, unsigned int req_width, char pad_char)
{
  const int local_buf_size = 32;
  char local_buf[local_buf_size];
  bool using_local_buffer = false;
  bool negative = false;
  char *out_buf;

  if (buf_size < 22) {
    // ink64 may not fit in provided buffer, use the local one
    out_buf = &local_buf[local_buf_size - 1];
    using_local_buffer = true;
  } else {
    out_buf = &buf[buf_size - 1];
  }

  unsigned int num_chars = 1;   // includes eos
  *out_buf-- = 0;

  if (val < 0) {
    val = -val;
    negative = true;
  }

  if (val < 10) {
    *out_buf-- = '0' + (char) val;
    ++num_chars;
  } else {
    do {
      *out_buf-- = (char) (val % 10) + '0';
      val /= 10;
      ++num_chars;
    } while (val);
  }

  // pad with pad_char if needed
  //
  if (req_width) {
    // add minus sign if padding character is not 0
    if (negative && pad_char != '0') {
      *out_buf = '-';
      ++num_chars;
    } else {
      out_buf++;
    }
    if (req_width > buf_size)
      req_width = buf_size;
    unsigned int num_padding = 0;
    if (req_width > num_chars) {
      num_padding = req_width - num_chars;
      switch (num_padding) {
      case 3:
        *--out_buf = pad_char;
      case 2:
        *--out_buf = pad_char;
      case 1:
        *--out_buf = pad_char;
        break;
      default:
        for (unsigned int i = 0; i < num_padding; ++i, *--out_buf = pad_char);
      }
      num_chars += num_padding;
    }
    // add minus sign if padding character is 0
    if (negative && pad_char == '0') {
      if (num_padding) {
        *out_buf = '-';         // overwrite padding
      } else {
        *--out_buf = '-';
        ++num_chars;
      }
    }
  } else if (negative) {
    *out_buf = '-';
    ++num_chars;
  } else {
    out_buf++;
  }

  if (using_local_buffer) {
    if (num_chars <= buf_size) {
      memcpy(buf, out_buf, num_chars);
      out_buf = buf;
    } else {
      // data does not fit return NULL
      out_buf = NULL;
    }
  }

  if (total_chars)
    *total_chars = num_chars;
  return out_buf;
}

// Formats the incoming timestamp_sec and timestamp_usec into a
// non-null-terminated string of the format "%d.%03d" which is
// 'almost' what squid specifies.  squid specifies "%9.%03d", but
// timestamps of less than 9 digits for the seconds value correspond
// to dates before March 1973, so it is not worth dealing with those.
//
// If the provided buffer buf of size buf_size is big enough to hold the
// resulting string, then places the string in buf WITHOUT A
// NULL-TERMINATING CHARACTER, and returns the number of characters written.
//
// If the provided buffer is too small, leaves it untouched and 
// returns -(required_buffer_size)
//
int
LogUtils::squid_timestamp_to_buf(char *buf, unsigned int buf_size, long timestamp_sec, long timestamp_usec)
{
  int res;
  const int tmp_buf_size = 32;
  char tmp_buf[tmp_buf_size];

  // convert seconds
  //
  unsigned int num_chars_s;
  char *ts_s = LogUtils::ink64_to_str(tmp_buf, tmp_buf_size - 4,
                                      timestamp_sec, &num_chars_s);
  ink_debug_assert(ts_s);

  // convert milliseconds
  //
  tmp_buf[tmp_buf_size - 5] = '.';
  int ms = timestamp_usec / 1000;
  unsigned int num_chars_ms;
  char RELEASE_UNUSED *ts_ms = LogUtils::ink64_to_str(&tmp_buf[tmp_buf_size - 4],
                                                      4, ms, &num_chars_ms, 4, '0');
  ink_debug_assert(ts_ms && num_chars_ms == 4);

  unsigned int chars_to_write = num_chars_s + 3;        // no eos

  if (buf_size >= chars_to_write) {
    memcpy(buf, ts_s, chars_to_write);
    res = chars_to_write;
  } else {
    res = -((int) chars_to_write);
  }

  return res;
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
LogUtils::file_is_writeable(const char *full_filename,
                            off_t * size_bytes, bool * has_size_limit, inku64 * current_size_limit_bytes)
{
  int ret_val = 0;
  int e;
  struct stat stat_data;

  e = stat(full_filename, &stat_data);
  if (e == 0) {
    // stat succeeded, check if full_filename points to a regular
    // file/fifo and if so, check if file has write permission
    //
#ifdef ASCII_PIPE_FORMAT_SUPPORTED
    if (!(stat_data.st_mode & S_IFREG || stat_data.st_mode & S_IFIFO)) {
#else
    if (!(stat_data.st_mode & S_IFREG)) {
#endif
      ret_val = 1;
    } else if (!(stat_data.st_mode & S_IWUSR)) {
      errno = EACCES;
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
        prefix = new char[prefix_len + 1];
        memcpy(prefix, full_filename, prefix_len);
        prefix[prefix_len] = 0;
        dir = prefix;
      } else {
        dir = (char *) ".";     // full_filename has no prefix, use .
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
        delete[]prefix;
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
      if (limit_data.rlim_cur != RLIM_INFINITY) {
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
