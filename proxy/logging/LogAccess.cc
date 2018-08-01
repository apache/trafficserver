/** @file

  This file implements the LogAccess class.

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

  @section description
  This file implements the LogAccess class.  However, LogAccess is an
  abstract base class, providing an interface that logging uses to get
  information from a module, such as HTTP.  Each module derives a
  specific implementation from this base class (such as LogAccessHttp), and
  implements the virtual accessor functions there.

  The LogAccess class also defines a set of static functions that are used
  to provide support for marshalling and unmarshalling support for the other
  LogAccess derived classes.
 */
#include "ts/ink_platform.h"

#include "HTTP.h"

#include "P_Net.h"
#include "P_Cache.h"
#include "I_Machine.h"
#include "LogAccess.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogUtils.h"
#include "LogFormat.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "Log.h"

char INVALID_STR[] = "!INVALID_STR!";

#define LOG_ACCESS_DEFAULT_FIELD(name, impl) \
  int LogAccess::name(char *buf) { impl; }
/*-------------------------------------------------------------------------
  LogAccess::init
  -------------------------------------------------------------------------*/

void
LogAccess::init()
{
  if (initialized) {
    return;
  }
  //
  // Here is where we would perform any initialization code.
  //

  initialized = true;
}

/*-------------------------------------------------------------------------
  The following functions provide a default implementation for the base
  class marshalling routines so that each subsequent LogAccess* class only
  has to implement those functions that are to override this default
  implementation.
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_plugin_identity_id, DEFAULT_INT_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_plugin_identity_tag, DEFAULT_STR_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_client_host_ip, DEFAULT_IP_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_host_interface_ip, DEFAULT_IP_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_lookup_url_canon, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_host_port, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_auth_user_name, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_text, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_http_method, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_url, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_url_canon, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_unmapped_url_canon, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_unmapped_url_path, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_unmapped_url_host, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_url_path, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_url_scheme, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  This case is special because it really stores 2 ints.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_http_version(char *buf)
{
  if (buf) {
    int64_t major = 0;
    int64_t minor = 0;
    marshal_int(buf, major);
    marshal_int((buf + INK_MIN_ALIGN), minor);
  }
  return (2 * INK_MIN_ALIGN);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_protocol_version, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_header_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_content_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_tcp_reused, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_is_ssl, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_ssl_reused, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_security_protocol, DEFAULT_STR_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_client_security_cipher_suite, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_finish_status_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_id, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_uuid, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_rx_error_code, DEFAULT_STR_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_client_tx_error_code, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_timestamp_sec, DEFAULT_INT_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_timestamp_ms, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_content_type, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_reason_phrase, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_squid_len, DEFAULT_INT_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_client_req_squid_len, DEFAULT_INT_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_squid_len, DEFAULT_INT_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_squid_len, DEFAULT_INT_FIELD)
LOG_ACCESS_DEFAULT_FIELD(marshal_cache_resp_squid_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_content_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_status_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_resp_header_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_finish_status_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_result_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_result_subcode, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_hit_miss, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_header_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_content_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_server_ip, DEFAULT_IP_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_server_port, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_hierarchy_route, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_next_hop_ip, DEFAULT_IP_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_next_hop_port, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_retry_after_time, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_host_name(char *buf)
{
  char *str        = nullptr;
  int len          = 0;
  Machine *machine = Machine::instance();

  if (machine) {
    str = machine->hostname;
  }

  len = LogAccess::strlen(str);

  if (buf) {
    marshal_str(buf, str, len);
  }

  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_host_ip(char *buf)
{
  return marshal_ip(buf, &Machine::instance()->ip.sa);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_req_is_ssl, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_host_ip, DEFAULT_IP_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_host_name, DEFAULT_STR_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_status_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_content_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_header_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  This case is special because it really stores 2 ints.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_resp_http_version(char *buf)
{
  if (buf) {
    int64_t major = 0;
    int64_t minor = 0;
    marshal_int(buf, major);
    marshal_int((buf + INK_MIN_ALIGN), minor);
  }
  return (2 * INK_MIN_ALIGN);
}

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_time_ms, DEFAULT_INT_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_server_resp_time_s, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_transact_count, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
-------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_server_connect_attempts, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_resp_status_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_resp_content_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_resp_header_len, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  This case is special because it really stores 2 ints.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_cache_resp_http_version(char *buf)
{
  if (buf) {
    int64_t major = 0;
    int64_t minor = 0;
    marshal_int(buf, major);
    marshal_int((buf + INK_MIN_ALIGN), minor);
  }
  return (2 * INK_MIN_ALIGN);
}

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_write_code, DEFAULT_INT_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_cache_write_transform_code, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_transfer_time_ms, DEFAULT_INT_FIELD)

LOG_ACCESS_DEFAULT_FIELD(marshal_transfer_time_s, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_file_size, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_http_header_field(LogField::Container /* container ATS_UNUSED */, char * /* field ATS_UNUSED */, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
int
LogAccess::marshal_http_header_field_escapify(LogField::Container /* container ATS_UNUSED */, char * /* field ATS_UNUSED */,
                                              char *buf)
{
  DEFAULT_STR_FIELD;
}

LOG_ACCESS_DEFAULT_FIELD(marshal_proxy_host_port, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------

  The following functions have a non-virtual base-class implementation.
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_entry_type(char *buf)
{
  if (buf) {
    int64_t val = (int64_t)entry_type();
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_process_uuid(char *buf)
{
  int len = round_strlen(TS_UUID_STRING_LEN + 1);

  if (buf) {
    const char *str = (char *)Machine::instance()->uuid.getString();
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_http_connection_id, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LOG_ACCESS_DEFAULT_FIELD(marshal_client_http_transaction_id, DEFAULT_INT_FIELD)

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_config_int_var(char *config_var, char *buf)
{
  if (buf) {
    int64_t val = (int64_t)REC_ConfigReadInteger(config_var);
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_config_str_var(char *config_var, char *buf)
{
  char *str = nullptr;
  str       = REC_ConfigReadString(config_var);
  int len   = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  ats_free(str);
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone(TSMilestonesType ms, char *buf)
{
  DEFAULT_INT_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_fmt_sec(TSMilestonesType ms, char *buf)
{
  DEFAULT_INT_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_fmt_squid(TSMilestonesType ms, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_fmt_netscape(TSMilestonesType ms, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_fmt_date(TSMilestonesType ms, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_fmt_time(TSMilestonesType ms, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_milestone_diff(TSMilestonesType ms1, TSMilestonesType ms2, char *buf)
{
  DEFAULT_INT_FIELD;
}

// To allow for a generic marshal_record function, rather than
// multiple functions (one per data type) we always marshal a record
// as a string of a fixed length.  We use a fixed length because the
// marshal_record function can be called with a null *buf to request
// the length of the record, and later with a non-null *buf to
// actually request the record to be inserted in the buffer, and both
// calls should return the same number of characters. If we did not
// enforce a fixed size, this would not necessarily be the case
// because records --statistics in particular-- can potentially change
// between one call and the other.
//
int
LogAccess::marshal_record(char *record, char *buf)
{
  const unsigned int max_chars = MARSHAL_RECORD_LENGTH;

  if (nullptr == buf) {
    return max_chars;
  }

  const char *record_not_found_msg          = "RECORD_NOT_FOUND";
  const unsigned int record_not_found_chars = ::strlen(record_not_found_msg) + 1;

  char ascii_buf[max_chars];
  const char *out_buf;
  unsigned int num_chars;

#define LOG_INTEGER RECD_INT
#define LOG_COUNTER RECD_COUNTER
#define LOG_FLOAT RECD_FLOAT
#define LOG_STRING RECD_STRING

  RecDataT stype = RECD_NULL;
  bool found     = false;

  if (RecGetRecordDataType(record, &stype) != REC_ERR_OKAY) {
    out_buf   = "INVALID_RECORD";
    num_chars = ::strlen(out_buf) + 1;
  } else {
    if (LOG_INTEGER == stype || LOG_COUNTER == stype) {
      // we assume MgmtInt and MgmtIntCounter are int64_t for the
      // conversion below, if this ever changes we should modify
      // accordingly
      //
      ink_assert(sizeof(int64_t) >= sizeof(RecInt) && sizeof(int64_t) >= sizeof(RecCounter));

      // so that a 64 bit integer will fit (including sign and eos)
      //
      ink_assert(max_chars > 21);

      int64_t val = (int64_t)(LOG_INTEGER == stype ? REC_readInteger(record, &found) : REC_readCounter(record, &found));

      if (found) {
        out_buf = int64_to_str(ascii_buf, max_chars, val, &num_chars);
        ink_assert(out_buf);
      } else {
        out_buf   = (char *)record_not_found_msg;
        num_chars = record_not_found_chars;
      }
    } else if (LOG_FLOAT == stype) {
      // we assume MgmtFloat is at least a float for the conversion below
      // (the conversion itself assumes a double because of the %e)
      // if this ever changes we should modify accordingly
      //
      ink_assert(sizeof(double) >= sizeof(RecFloat));

      RecFloat val = REC_readFloat(record, &found);

      if (found) {
        // snprintf does not support "%e" in the format
        // and we want to use "%e" because it is the most concise
        // notation

        num_chars = snprintf(ascii_buf, sizeof(ascii_buf), "%e", val) + 1; // include eos

        // the "%e" field above should take 13 characters at most
        //
        ink_assert(num_chars <= max_chars);

        // the following should never be true
        //
        if (num_chars > max_chars) {
          // data does not fit, output asterisks
          out_buf   = "***";
          num_chars = ::strlen(out_buf) + 1;
        } else {
          out_buf = ascii_buf;
        }
      } else {
        out_buf   = (char *)record_not_found_msg;
        num_chars = record_not_found_chars;
      }
    } else if (LOG_STRING == stype) {
      out_buf = REC_readString(record, &found);

      if (found) {
        if (out_buf != nullptr && out_buf[0] != 0) {
          num_chars = ::strlen(out_buf) + 1;
          if (num_chars > max_chars) {
            // truncate string and write ellipsis at the end
            memcpy(ascii_buf, out_buf, max_chars - 4);
            ascii_buf[max_chars - 1] = 0;
            ascii_buf[max_chars - 2] = '.';
            ascii_buf[max_chars - 3] = '.';
            ascii_buf[max_chars - 4] = '.';
            out_buf                  = ascii_buf;
            num_chars                = max_chars;
          }
        } else {
          out_buf   = "NULL";
          num_chars = ::strlen(out_buf) + 1;
        }
      } else {
        out_buf   = (char *)record_not_found_msg;
        num_chars = record_not_found_chars;
      }
    } else {
      out_buf   = "INVALID_MgmtType";
      num_chars = ::strlen(out_buf) + 1;
      ink_assert(!"invalid MgmtType for requested record");
    }
  }

  ink_assert(num_chars <= max_chars);
  memcpy(buf, out_buf, num_chars);

  return max_chars;
}

/*-------------------------------------------------------------------------
  LogAccess::marshal_str

  Copy the given string to the destination buffer, including the trailing
  NULL.  For binary formatting, we need the NULL to distinguish the end of
  the string, and we'll remove it for ascii formatting.
  ASSUMES dest IS NOT NULL.
  The array pointed to by dest must be at least padded_len in length.
  -------------------------------------------------------------------------*/

void
LogAccess::marshal_str(char *dest, const char *source, int padded_len)
{
  if (source == nullptr || source[0] == 0 || padded_len == 0) {
    source = DEFAULT_STR;
  }
  ink_strlcpy(dest, source, padded_len);

#ifdef DEBUG
  //
  // what padded_len should be, if there is no padding, is strlen()+1.
  // if not, then we needed to pad and should touch the intermediate
  // bytes to avoid UMR errors when the buffer is written.
  //
  size_t real_len = (::strlen(source) + 1);
  while ((int)real_len < padded_len) {
    dest[real_len] = '$';
    real_len++;
  }
#endif
}

/*-------------------------------------------------------------------------
  LogAccess::marshal_mem

  This is a version of marshal_str that works with unterminated strings.
  In this case, we'll copy the buffer and then add a trailing null that
  the rest of the system assumes.
  -------------------------------------------------------------------------*/

void
LogAccess::marshal_mem(char *dest, const char *source, int actual_len, int padded_len)
{
  if (source == nullptr || source[0] == 0 || actual_len == 0) {
    source     = DEFAULT_STR;
    actual_len = DEFAULT_STR_LEN;
    ink_assert(actual_len < padded_len);
  }
  memcpy(dest, source, actual_len);
  dest[actual_len] = 0; // add terminating null

#ifdef DEBUG
  //
  // what len should be, if there is no padding, is strlen()+1.
  // if not, then we needed to pad and should touch the intermediate
  // bytes to avoid UMR errors when the buffer is written.
  //
  int real_len = actual_len + 1;
  while (real_len < padded_len) {
    dest[real_len] = '$';
    real_len++;
  }
#endif
}

/*-------------------------------------------------------------------------
  LogAccess::marshal_ip

  Marshal an IP address in a reasonably compact way. If the address isn't
  valid (NULL or not IP) then marshal an invalid address record.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_ip(char *dest, sockaddr const *ip)
{
  LogFieldIpStorage data;
  int len = sizeof(data._ip);
  if (nullptr == ip) {
    data._ip._family = AF_UNSPEC;
  } else if (ats_is_ip4(ip)) {
    if (dest) {
      data._ip4._family = AF_INET;
      data._ip4._addr   = ats_ip4_addr_cast(ip);
    }
    len = sizeof(data._ip4);
  } else if (ats_is_ip6(ip)) {
    if (dest) {
      data._ip6._family = AF_INET6;
      data._ip6._addr   = ats_ip6_addr_cast(ip);
    }
    len = sizeof(data._ip6);
  } else {
    data._ip._family = AF_UNSPEC;
  }

  if (dest) {
    memcpy(dest, &data, len);
  }
  return INK_ALIGN_DEFAULT(len);
}

inline int
LogAccess::unmarshal_with_map(int64_t code, char *dest, int len, Ptr<LogFieldAliasMap> map, const char *msg)
{
  long int codeStrLen = 0;

  switch (map->asString(code, dest, len, (size_t *)&codeStrLen)) {
  case LogFieldAliasMap::INVALID_INT:
    if (msg) {
      const int bufSize = 64;
      char invalidCodeMsg[bufSize];
      codeStrLen = snprintf(invalidCodeMsg, 64, "%s(%" PRId64 ")", msg, code);
      if (codeStrLen < bufSize && codeStrLen < len) {
        ink_strlcpy(dest, invalidCodeMsg, len);
      } else {
        codeStrLen = -1;
      }
    } else {
      codeStrLen = -1;
    }
    break;
  case LogFieldAliasMap::BUFFER_TOO_SMALL:
    codeStrLen = -1;
    break;
  }

  return codeStrLen;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_int

  Return the integer pointed at by the buffer and advance the buffer
  pointer past the int.  The int will be converted back to host byte order.
  -------------------------------------------------------------------------*/

int64_t
LogAccess::unmarshal_int(char **buf)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  int64_t val;

  // TODO: this used to do nthol, do we need to worrry? TS-1156.
  val = *((int64_t *)(*buf));
  *buf += INK_MIN_ALIGN;
  return val;
}

/*-------------------------------------------------------------------------
  unmarshal_itoa

  This routine provides a fast conversion from a binary int to a string.
  It returns the number of characters formatted.  "dest" must point to the
  LAST character of an array large enough to store the complete formatted
  number.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_itoa(int64_t val, char *dest, int field_width, char leading_char)
{
  ink_assert(dest != nullptr);

  char *p = dest;

  if (val <= 0) {
    *p-- = '0';
    while (dest - p < field_width) {
      *p-- = leading_char;
    }
    return (int)(dest - p);
  }

  while (val) {
    *p-- = '0' + (val % 10);
    val /= 10;
  }
  while (dest - p < field_width) {
    *p-- = leading_char;
  }
  return (int)(dest - p);
}

/*-------------------------------------------------------------------------
  unmarshal_itox

  This routine provides a fast conversion from a binary int to a hex string.
  It returns the number of characters formatted.  "dest" must point to the
  LAST character of an array large enough to store the complete formatted
  number.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_itox(int64_t val, char *dest, int field_width, char leading_char)
{
  ink_assert(dest != nullptr);

  char *p             = dest;
  static char table[] = "0123456789abcdef?";

  for (int i = 0; i < (int)(sizeof(int64_t) * 2); i++) {
    *p-- = table[val & 0xf];
    val >>= 4;
  }
  while (dest - p < field_width) {
    *p-- = leading_char;
  }

  return (int64_t)(dest - p);
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_int_to_str

  Return the string representation of the integer pointed at by buf.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_int_to_str(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char val_buf[128];
  int64_t val = unmarshal_int(buf);
  int val_len = unmarshal_itoa(val, val_buf + 127);

  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_int_to_str_hex

  Return the string representation (hexadecimal) of the integer pointed at by buf.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_int_to_str_hex(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char val_buf[128];
  int64_t val = unmarshal_int(buf);
  int val_len = unmarshal_itox(val, val_buf + 127);

  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_str

  Retrieve the string from the location pointed at by the buffer and
  advance the pointer past the string.  The local strlen function is used
  to advance the pointer, thus matching the corresponding strlen that was
  used to lay the string into the buffer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_str(char **buf, char *dest, int len, LogSlice *slice)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char *val_buf = *buf;
  int val_len   = (int)::strlen(val_buf);

  *buf += LogAccess::strlen(val_buf); // this is how it was stored

  if (slice && slice->m_enable) {
    int offset, n;

    n = slice->toStrOffset(val_len, &offset);
    if (n <= 0) {
      return 0;
    }

    if (n >= len) {
      return -1;
    }

    memcpy(dest, (val_buf + offset), n);
    return n;
  }

  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

int
LogAccess::unmarshal_ttmsf(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  int64_t val = unmarshal_int(buf);
  double secs = (double)val / 1000;
  int val_len = snprintf(dest, len, "%.3f", secs);
  return val_len;
}

int
LogAccess::unmarshal_int_to_date_str(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  int64_t value = unmarshal_int(buf);
  char *strval  = LogUtils::timestamp_to_date_str(value);
  int strlen    = (int)::strlen(strval);

  memcpy(dest, strval, strlen);
  return strlen;
}

int
LogAccess::unmarshal_int_to_time_str(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  int64_t value = unmarshal_int(buf);
  char *strval  = LogUtils::timestamp_to_time_str(value);
  int strlen    = (int)::strlen(strval);

  memcpy(dest, strval, strlen);
  return strlen;
}

int
LogAccess::unmarshal_int_to_netscape_str(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  int64_t value = unmarshal_int(buf);
  char *strval  = LogUtils::timestamp_to_netscape_str(value);
  int strlen    = (int)::strlen(strval);

  memcpy(dest, strval, strlen);
  return strlen;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_http_method

  Retrieve the int pointed at by the buffer and treat as an HttpMethod
  enumerated type.  Then lookup the string representation for that enum and
  return the string.  Advance the buffer pointer past the enum.
  -------------------------------------------------------------------------*/
/*
int
LogAccess::unmarshal_http_method (char **buf, char *dest, int len)
{
    return unmarshal_str (buf, dest, len);
}
*/
/*-------------------------------------------------------------------------
  LogAccess::unmarshal_http_version

  The http version is marshalled as two consecutive integers, the first for
  the major number and the second for the minor number.  Retrieve both
  numbers and return the result as "HTTP/major.minor".
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_version(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  static const char *http = "HTTP/";
  static int http_len     = (int)::strlen(http);

  char val_buf[128];
  char *p = val_buf;

  memcpy(p, http, http_len);
  p += http_len;

  int res1 = unmarshal_int_to_str(buf, p, 128 - http_len);
  if (res1 < 0) {
    return -1;
  }
  p += res1;
  *p++     = '.';
  int res2 = unmarshal_int_to_str(buf, p, 128 - http_len - res1 - 1);
  if (res2 < 0) {
    return -1;
  }

  int val_len = http_len + res1 + res2 + 1;
  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_http_text

  The http text is simply the fields http_method (cqhm) + url (cqu) +
  http_version (cqhv), all right next to each other, in that order.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_text(char **buf, char *dest, int len, LogSlice *slice)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char *p = dest;

  //    int res1 = unmarshal_http_method (buf, p, len);
  int res1 = unmarshal_str(buf, p, len);
  if (res1 < 0) {
    return -1;
  }
  p += res1;
  *p++     = ' ';
  int res2 = unmarshal_str(buf, p, len - res1 - 1, slice);
  if (res2 < 0) {
    return -1;
  }
  p += res2;
  *p++     = ' ';
  int res3 = unmarshal_http_version(buf, p, len - res1 - res2 - 2);
  if (res3 < 0) {
    return -1;
  }
  return res1 + res2 + res3 + 2;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_http_status

  An http response status code (pssc,sssc) is just an INT, but it's always
  formatted with three digits and leading zeros.  So, we need a special
  version of unmarshal_int_to_str that does this leading zero formatting.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_status(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char val_buf[128];
  int64_t val = unmarshal_int(buf);
  int val_len = unmarshal_itoa(val, val_buf + 127, 3, '0');
  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_ip

  Retrieve an IP address directly.
  -------------------------------------------------------------------------*/
int
LogAccess::unmarshal_ip(char **buf, IpEndpoint *dest)
{
  int len = sizeof(LogFieldIp); // of object processed.

  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  LogFieldIp *raw = reinterpret_cast<LogFieldIp *>(*buf);
  if (AF_INET == raw->_family) {
    LogFieldIp4 *ip4 = static_cast<LogFieldIp4 *>(raw);
    ats_ip4_set(dest, ip4->_addr);
    len = sizeof(*ip4);
  } else if (AF_INET6 == raw->_family) {
    LogFieldIp6 *ip6 = static_cast<LogFieldIp6 *>(raw);
    ats_ip6_set(dest, ip6->_addr);
    len = sizeof(*ip6);
  } else {
    ats_ip_invalidate(dest);
  }
  len = INK_ALIGN_DEFAULT(len);
  *buf += len;
  return len;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_ip_to_str

  Retrieve the IP addresspointed at by the buffer and convert to a
  string in standard format. The string is written to @a dest and its
  length (not including nul) is returned. @a *buf is advanced.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_ip_to_str(char **buf, char *dest, int len)
{
  IpEndpoint ip;
  int zret = -1;

  if (len > 0) {
    unmarshal_ip(buf, &ip);
    if (!ats_is_ip(&ip)) {
      *dest = '0';
      zret  = 1;
    } else if (ats_ip_ntop(&ip, dest, len)) {
      zret = static_cast<int>(::strlen(dest));
    }
  }
  return zret;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_ip_to_hex

  Retrieve the int pointed at by the buffer and treat as an IP
  address.  Convert to a string in byte oriented hexadeciaml and
  return the string.  Advance the buffer pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_ip_to_hex(char **buf, char *dest, int len)
{
  int zret = -1;
  IpEndpoint ip;

  if (len > 0) {
    unmarshal_ip(buf, &ip);
    if (!ats_is_ip(&ip)) {
      *dest = '0';
      zret  = 1;
    } else {
      zret = ats_ip_to_hex(&ip.sa, dest, len);
    }
  }
  return zret;
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_hierarchy

  Retrieve the int pointed at by the buffer and treat as a
  SquidHierarchyCode.  Use this as an index into the local string
  conversion tables and return the string equivalent to the enum.
  Advance the buffer pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_hierarchy(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "INVALID_CODE"));
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_finish_status

  Retrieve the int pointed at by the buffer and treat as a finish code.
  Use the enum as an index into a string table and return the string equiv
  of the enum.  Advance the pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_finish_status(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_FINISH_CODE"));
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_cache_code

  Retrieve the int pointed at by the buffer and treat as a SquidLogCode.
  Use this to index into the local string tables and return the string
  equiv of the enum.  Advance the pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_cache_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "ERROR_UNKNOWN"));
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_cache_hit_miss

  Retrieve the int pointed at by the buffer and treat as a SquidHitMissCode.
  Use this to index into the local string tables and return the string
  equiv of the enum.  Advance the pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_cache_hit_miss(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "HIT_MISS_UNKNOWN"));
}

/*-------------------------------------------------------------------------
  LogAccess::unmarshal_entry_type
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_entry_type(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_ENTRY_TYPE"));
}

int
LogAccess::unmarshal_cache_write_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_CACHE_WRITE_CODE"));
}

int
LogAccess::unmarshal_record(char **buf, char *dest, int len)
{
  ink_assert(buf != nullptr);
  ink_assert(*buf != nullptr);
  ink_assert(dest != nullptr);

  char *val_buf = *buf;
  int val_len   = (int)::strlen(val_buf);
  *buf += MARSHAL_RECORD_LENGTH; // this is how it was stored
  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

/*-------------------------------------------------------------------------
  resolve_logfield_string

  This function resolves the given custom log format string using the given
  LogAccess context and returns the resulting string, which is ats_malloc'd.
  The caller is responsible for ats_free'ing the return result.  If there are
  any problems, NULL is returned.
  -------------------------------------------------------------------------*/
char *
resolve_logfield_string(LogAccess *context, const char *format_str)
{
  if (!context) {
    Debug("log-resolve", "No context to resolve?");
    return nullptr;
  }

  if (!format_str) {
    Debug("log-resolve", "No format to resolve?");
    return nullptr;
  }

  Debug("log-resolve", "Resolving: %s", format_str);

  //
  // Divide the format string into two parts: one for the printf-style
  // string and one for the symbols.
  //
  char *printf_str = nullptr;
  char *fields_str = nullptr;
  int n_fields     = LogFormat::parse_format_string(format_str, &printf_str, &fields_str);

  //
  // Perhaps there were no fields to resolve?  Then just return the
  // format_str. Nothing to free here either.
  //
  if (!n_fields) {
    Debug("log-resolve", "No fields found; returning copy of format_str");
    ats_free(printf_str);
    ats_free(fields_str);
    return ats_strdup(format_str);
  }

  Debug("log-resolve", "%d fields: %s", n_fields, fields_str);
  Debug("log-resolve", "printf string: %s", printf_str);

  LogFieldList fields;
  bool contains_aggregates;
  int field_count = LogFormat::parse_symbol_string(fields_str, &fields, &contains_aggregates);

  if (field_count != n_fields) {
    Error("format_str contains %d invalid field symbols", n_fields - field_count);
    ats_free(printf_str);
    ats_free(fields_str);
    return nullptr;
  }
  //
  // Ok, now marshal the data out of the LogAccess object and into a
  // temporary storage buffer.  Make sure the LogAccess context is
  // initialized first.
  //
  Debug("log-resolve", "Marshaling data from LogAccess into buffer ...");
  context->init();
  unsigned bytes_needed = fields.marshal_len(context);
  char *buf             = (char *)ats_malloc(bytes_needed);
  unsigned bytes_used   = fields.marshal(context, buf);

  ink_assert(bytes_needed == bytes_used);
  Debug("log-resolve", "    %u bytes marshalled", bytes_used);

  //
  // Now we can "unmarshal" the data from the buffer into a string,
  // combining it with the data from the printf string.  The problem is,
  // we're not sure how much space it will take when it's unmarshalled.
  // So, we'll just guess.
  //
  char *result = (char *)ats_malloc(8192);
  unsigned bytes_resolved =
    LogBuffer::resolve_custom_entry(&fields, printf_str, buf, result, 8191, LogUtils::timestamp(), 0, LOG_SEGMENT_VERSION);
  ink_assert(bytes_resolved < 8192);

  if (!bytes_resolved) {
    ats_free(result);
    result = nullptr;
  } else {
    result[bytes_resolved] = 0; // NULL terminate
  }

  ats_free(printf_str);
  ats_free(fields_str);
  ats_free(buf);

  return result;
}
