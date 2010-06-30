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
 Logging - LogAccess.h


 ***************************************************************************/
#if !defined (INK_NO_LOG)
#ifndef LOG_ACCESS_H
#define LOG_ACCESS_H
#include "inktomi++.h"
#include "LogField.h"

/*-------------------------------------------------------------------------
  LogAccess

  This class defines the logging system interface for extracting
  information required to process a log entry.  This accessor is
  implemented as an abstract base class with virtual functions for
  accessing the data based on the derived class.

  NOTE that some accessors are declared non-virtual, meaning that they
  provide an implementation in this base class that is NOT TO BE
  OVERRIDDEN.  This is used for fields that are already part of the
  LogEntryHeader, and thus do not need to be marshalled.  So, these
  functions do nothing and return 0.

  Each function has the ability to marshal its data into a buffer that is
  provided, and return the number of bytes that were marshalled.  In the
  absence of a marshalling buffer, the routines will simply return the
  number of bytes that would be needed to marshal.  This allows for the
  same functions to be used for both buffer length computations and data
  movement.

  Logging deals with values of just two possible data types: integers
  (including enum) and strings.   Because the integers are multi-byte
  values that might need special alignment needs when being marshalled,
  this base class provides a static member function called marshal_int()
  that handles this (including checking for a NULL buffer).  The template
  for implementing integer and enum marshalling routines is:

      int marshal_some_int_value (char *buf)
      {
          if (buf) {
	      int64 val = what_the_value_should_be;
              marshal_int (buf, val);
	  }
	  return INK_MIN_ALIGN;
      }

  String values don't need byte swapping, but we do want to ensure things
  like trailing NULLS and padding.  The best way to do this is to provide a
  marshalling routine that takes a source buffer, a string, and its length,
  and makes sure the string is copied into the source buffer with a
  trailing NULL.  We've also provided our own strlen() function within the
  class to adjust for trailing NULL as well.  So, here is how it would look
  to actually use these functions for marshalling a string value:

      int marshal_some_string_value (char *buf)
      {
	  char *str = compute_or_locate_string_value ();
	  int len = LogAccess::strlen (str);
	  if (buf) {
	      marshal_str (buf, str, len);
	  }
	  return len;
      }

  -------------------------------------------------------------------------*/

#include "ink_bool.h"

// DEFAULT_STR_LEN MUST be less than INK_MIN_ALIGN
#define DEFAULT_STR	"-"
#define DEFAULT_STR_LEN 1

#define DEFAULT_INT_FIELD {\
    if (buf) { \
      int64 i = 0; \
      marshal_int (buf, i); \
    } \
    return INK_MIN_ALIGN; \
}

#define DEFAULT_STR_FIELD {\
    char * str = NULL; \
    int len = INK_MIN_ALIGN; \
    if (buf) { \
      marshal_str (buf, str, len); \
    } \
    return (len); \
}

// should be at least 22 bytes to always accomodate a converted
// MgmtInt, MgmtIntCounter or MgmtFloat. 22 bytes is enough for 64 bit
// ints + sign + eos, and enough for %e floating point representation
// + eos
//
#define MARSHAL_RECORD_LENGTH 32

enum LogEntryType
{
  LOG_ENTRY_HTTP = 0,
  LOG_ENTRY_ICP,
  LOG_ENTRY_MIXT,
  N_LOG_ENTRY_TYPES
};

enum LogFinishCodeType
{
  LOG_FINISH_FIN = 0,
  LOG_FINISH_INTR,
  LOG_FINISH_TIMEOUT,
  N_LOG_FINISH_CODE_TYPES
};

enum LogCacheWriteCodeType
{
  LOG_CACHE_WRITE_NONE = 0,
  LOG_CACHE_WRITE_LOCK_MISSED,
  LOG_CACHE_WRITE_LOCK_ABORTED,
  LOG_CACHE_WRITE_ERROR,
  LOG_CACHE_WRITE_COMPLETE,
  N_LOG_CACHE_WRITE_TYPES
};


class LogAccess
{                               // Abstract Base Class
public:
  inkcoreapi LogAccess():initialized(false)
  {
  };
  inkcoreapi virtual ~ LogAccess()
  {
  };
  inkcoreapi virtual void init();

  virtual LogEntryType entry_type() = 0;

  //
  // client -> proxy fields
  //
  inkcoreapi virtual int marshal_client_host_ip(char *);        // INT
  inkcoreapi virtual int marshal_client_auth_user_name(char *); // STR
  int marshal_client_req_timestamp_sec(char *); // INT

  inkcoreapi virtual int marshal_client_req_text(char *);       // STR
  inkcoreapi virtual int marshal_client_req_http_method(char *);        // STR
  inkcoreapi virtual int marshal_client_req_url(char *);        // STR
  inkcoreapi virtual int marshal_client_req_url_canon(char *);  // STR
  inkcoreapi virtual int marshal_client_req_unmapped_url_canon(char *); // STR
  inkcoreapi virtual int marshal_client_req_unmapped_url_path(char *);  // STR
  inkcoreapi virtual int marshal_client_req_url_path(char *);   // STR
  inkcoreapi virtual int marshal_client_req_url_scheme(char *); // STR
  inkcoreapi virtual int marshal_client_req_http_version(char *);       // INT
  inkcoreapi virtual int marshal_client_req_header_len(char *); // INT
  inkcoreapi virtual int marshal_client_req_body_len(char *);   // INT
  inkcoreapi virtual int marshal_client_finish_status_code(char *);     // INT
  inkcoreapi virtual int marshal_client_gid(char *);    // INT
  inkcoreapi virtual int marshal_client_accelerator_id(char *); // INT

  //
  // proxy -> client fields
  //
  inkcoreapi virtual int marshal_proxy_resp_content_type(char *);       // STR
  inkcoreapi virtual int marshal_proxy_resp_squid_len(char *);  // INT
  inkcoreapi virtual int marshal_proxy_resp_content_len(char *);        // INT
  inkcoreapi virtual int marshal_proxy_resp_status_code(char *);        // INT
  inkcoreapi virtual int marshal_proxy_resp_header_len(char *); // INT
  inkcoreapi virtual int marshal_proxy_resp_origin_bytes(char *);       // INT
  inkcoreapi virtual int marshal_proxy_resp_cache_bytes(char *);        // INT
  inkcoreapi virtual int marshal_proxy_finish_status_code(char *);      // INT
  inkcoreapi virtual int marshal_cache_result_code(char *);     // INT

  //
  // proxy -> server fields
  //
  inkcoreapi virtual int marshal_proxy_req_header_len(char *);  // INT
  inkcoreapi virtual int marshal_proxy_req_body_len(char *);    // INT
  inkcoreapi virtual int marshal_proxy_req_server_name(char *); // STR
  inkcoreapi virtual int marshal_proxy_req_server_ip(char *);   // INT
  inkcoreapi virtual int marshal_proxy_hierarchy_route(char *); // INT
  inkcoreapi virtual int marshal_proxy_host_name(char *);       // STR
  inkcoreapi virtual int marshal_proxy_host_ip(char *); // STR

  //
  // server -> proxy fields
  //
  inkcoreapi virtual int marshal_server_host_ip(char *);        // INT
  inkcoreapi virtual int marshal_server_host_name(char *);      // STR
  inkcoreapi virtual int marshal_server_resp_status_code(char *);       // INT
  inkcoreapi virtual int marshal_server_resp_content_len(char *);       // INT
  inkcoreapi virtual int marshal_server_resp_header_len(char *);        // INT
  inkcoreapi virtual int marshal_server_resp_http_version(char *);      // INT

  //
  // congestion control -- client_retry_after_time
  //
  inkcoreapi virtual int marshal_client_retry_after_time(char *);       // INT

  //
  // cache write fields
  //
  inkcoreapi virtual int marshal_cache_write_code(char *);      // INT
  inkcoreapi virtual int marshal_cache_write_transform_code(char *);    // INT

  // other fields
  //
  inkcoreapi virtual int marshal_transfer_time_ms(char *);      // INT
  inkcoreapi virtual int marshal_transfer_time_s(char *);       // INT
  inkcoreapi virtual int marshal_bandwidth(char *);     // INT
  inkcoreapi virtual int marshal_file_size(char *);     // INT
  int marshal_entry_type(char *);       // INT
  inkcoreapi virtual int marshal_time_to_first_client_byte_ms(char *);  // INT
  inkcoreapi virtual int marshal_stream_type(char *);   // STR
  inkcoreapi virtual int marshal_external_plugin_transaction_id(char *);        // INT
  // MIXT SDK_VER_2
  inkcoreapi virtual int marshal_external_plugin_string(char *);        // STR
  // MIXT SDK_VER_2
  inkcoreapi virtual int marshal_stream_duration_ms(char *);    // INT

  // streaming media fields
  //
  inkcoreapi virtual int marshal_client_dns_name(char *);       // STR
  inkcoreapi virtual int marshal_client_os(char *);     // STR
  inkcoreapi virtual int marshal_client_os_version(char *);     // STR
  inkcoreapi virtual int marshal_client_cpu(char *);    // STR
  inkcoreapi virtual int marshal_client_player_version(char *); // STR
  inkcoreapi virtual int marshal_client_player_language(char *);        // STR
  inkcoreapi virtual int marshal_client_user_agent(char *);     // STR
  inkcoreapi virtual int marshal_referer_url(char *);   // STR
  inkcoreapi virtual int marshal_audio_codec(char *);   // STR
  inkcoreapi virtual int marshal_video_codec(char *);   // STR
  inkcoreapi virtual int marshal_client_bytes_received(char *); // INT
  inkcoreapi virtual int marshal_client_pkts_received(char *);  // INT
  inkcoreapi virtual int marshal_client_lost_pkts(char *);      // INT
  inkcoreapi virtual int marshal_client_lost_net_pkts(char *);  // INT
  inkcoreapi virtual int marshal_client_lost_continuous_pkts(char *);   // INT
  inkcoreapi virtual int marshal_client_pkts_ecc_recover(char *);       // INT
  inkcoreapi virtual int marshal_client_pkts_resent_recover(char *);    // INT
  inkcoreapi virtual int marshal_client_resend_request(char *); // INT
  inkcoreapi virtual int marshal_client_buffer_count(char *);   // INT
  inkcoreapi virtual int marshal_client_buffer_ts(char *);      // INT
  inkcoreapi virtual int marshal_client_quality_per(char *);    // INT

  // named fields from within a http header
  //
  inkcoreapi virtual int marshal_http_header_field(LogField::Container container, char *field, char *buf);
  inkcoreapi virtual int marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf);

  //
  // named records.config int variables
  //
  int marshal_config_int_var(char *config_var, char *buf);

  //
  // named records.config string variables
  //
  int marshal_config_str_var(char *config_var, char *buf);

  //
  // generic record access
  //
  int marshal_record(char *record, char *buf);

  //
  // unmarshalling routines
  //
  // They used to return a string; now they unmarshal directly into the
  // destination buffer supplied.
  //
  static int64 unmarshal_int(char **buf);
  static int unmarshal_itoa(int64 val, char *dest, int field_width = 0, char leading_char = ' ');
  static int unmarshal_itox(int64 val, char *dest, int field_width = 0, char leading_char = ' ');
  static int unmarshal_int_to_str(char **buf, char *dest, int len);
  static int unmarshal_int_to_str_hex(char **buf, char *dest, int len);
  static int unmarshal_str(char **buf, char *dest, int len);
  static int unmarshal_ttmsf(char **buf, char *dest, int len);
  static int unmarshal_http_version(char **buf, char *dest, int len);
  static int unmarshal_http_text(char **buf, char *dest, int len);
  static int unmarshal_http_status(char **buf, char *dest, int len);
  static int unmarshal_ip(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  static int unmarshal_hierarchy(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  static int unmarshal_finish_status(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  static int unmarshal_cache_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  static int unmarshal_entry_type(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);
  static int unmarshal_cache_write_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);

  static int unmarshal_with_map(int64 code, char *dest, int len, Ptr<LogFieldAliasMap> map, const char *msg = 0);

  static int unmarshal_record(char **buf, char *dest, int len);

  //
  // our own strlen function that pads strings to even int64 boundaries
  // so that there are no alignment problems with the int values.
  //
  static int round_strlen(int len);
  static int strlen(char *str);

public:
  inkcoreapi void static marshal_int(char *dest, int64 source);
  inkcoreapi void static marshal_str(char *dest, const char *source, int padded_len);
  inkcoreapi void static marshal_mem(char *dest, const char *source, int actual_len, int padded_len);

  bool initialized;

private:
  // -- member functions that are not allowed --
  LogAccess(const LogAccess & rhs);     // no copies
  LogAccess & operator=(LogAccess & rhs);       // or assignment
};

inline int
LogAccess::round_strlen(int len)
{
  return INK_ALIGN_DEFAULT(len);
}

/*-------------------------------------------------------------------------
  LogAccess::strlen

  Take trailing null and alignment padding into account.  This makes sure
  that strings in the LogBuffer are laid out properly.
  -------------------------------------------------------------------------*/

inline int
LogAccess::strlen(char *str)
{
  if (str == NULL || str[0] == 0) {
    return round_strlen(sizeof(DEFAULT_STR));
  } else {
    return (int) (round_strlen(((int)::strlen(str) + 1)));      // actual bytes for string
  }
}

inline void
LogAccess::marshal_int(char *dest, int64 source)
{
  // TODO: This used to do htonl on the source
  *((int64 *)dest) = source;
}

/*-------------------------------------------------------------------------
  resolve_logfield_string

  This external function takes a format string and a LogAccess context and
  resolves any known fields to return a new, resolved string.
  -------------------------------------------------------------------------*/

char *resolve_logfield_string(LogAccess * context, const char *format_str);

#endif
#endif // INK_NO_LOG
