/** @file

  This class extends the logging system interface as implemented by the
  HttpStateMachineGet class.

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

#pragma once

#include "tscore/ink_align.h"
#include "LogField.h"

class HTTPHdr;
class HttpSM;
class IpClass;
union IpEndpoint;

/*-------------------------------------------------------------------------
  LogAccess

  This class defines the logging system interface for extracting
  information required to process a log entry.  This accessor is
  implemented as an abstract base class with functions for
  accessing the data based on the derived class.

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
              int64_t val = what_the_value_should_be;
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

// DEFAULT_STR_LEN MUST be less than INK_MIN_ALIGN
#define DEFAULT_STR "-"
#define DEFAULT_STR_LEN 1

extern char INVALID_STR[];

enum LogEntryType {
  LOG_ENTRY_HTTP = 0,
  N_LOG_ENTRY_TYPES,
};

enum LogFinishCodeType {
  LOG_FINISH_FIN = 0,
  LOG_FINISH_INTR,
  LOG_FINISH_TIMEOUT,
  N_LOG_FINISH_CODE_TYPES,
};

enum LogCacheWriteCodeType {
  LOG_CACHE_WRITE_NONE = 0,
  LOG_CACHE_WRITE_LOCK_MISSED,
  LOG_CACHE_WRITE_LOCK_ABORTED,
  LOG_CACHE_WRITE_ERROR,
  LOG_CACHE_WRITE_COMPLETE,
  N_LOG_CACHE_WRITE_TYPES
};

class LogAccess
{
public:
  inkcoreapi
  LogAccess()
  {
  }

  explicit LogAccess(HttpSM *sm);

  inkcoreapi ~LogAccess() {}
  inkcoreapi void init();

  //
  // client -> proxy fields
  //
  inkcoreapi int marshal_client_host_ip(char *);                // STR
  inkcoreapi int marshal_host_interface_ip(char *);             // STR
  inkcoreapi int marshal_client_host_port(char *);              // INT
  inkcoreapi int marshal_client_auth_user_name(char *);         // STR
  inkcoreapi int marshal_client_req_timestamp_sec(char *);      // INT
  inkcoreapi int marshal_client_req_timestamp_ms(char *);       // INT
  inkcoreapi int marshal_client_req_text(char *);               // STR
  inkcoreapi int marshal_client_req_http_method(char *);        // STR
  inkcoreapi int marshal_client_req_url(char *);                // STR
  inkcoreapi int marshal_client_req_url_canon(char *);          // STR
  inkcoreapi int marshal_client_req_unmapped_url_canon(char *); // STR
  inkcoreapi int marshal_client_req_unmapped_url_path(char *);  // STR
  inkcoreapi int marshal_client_req_unmapped_url_host(char *);  // STR
  inkcoreapi int marshal_client_req_url_path(char *);           // STR
  inkcoreapi int marshal_client_req_url_scheme(char *);         // STR
  inkcoreapi int marshal_client_req_http_version(char *);       // INT
  inkcoreapi int marshal_client_req_protocol_version(char *);   // STR
  inkcoreapi int marshal_client_req_squid_len(char *);          // INT
  inkcoreapi int marshal_client_req_header_len(char *);         // INT
  inkcoreapi int marshal_client_req_content_len(char *);        // INT
  inkcoreapi int marshal_client_req_tcp_reused(char *);         // INT
  inkcoreapi int marshal_client_req_is_ssl(char *);             // INT
  inkcoreapi int marshal_client_req_ssl_reused(char *);         // INT
  inkcoreapi int marshal_client_req_is_internal(char *);        // INT
  inkcoreapi int marshal_client_req_mptcp_state(char *);        // INT
  inkcoreapi int marshal_client_security_protocol(char *);      // STR
  inkcoreapi int marshal_client_security_cipher_suite(char *);  // STR
  inkcoreapi int marshal_client_security_curve(char *);         // STR
  inkcoreapi int marshal_client_finish_status_code(char *);     // INT
  inkcoreapi int marshal_client_req_id(char *);                 // INT
  inkcoreapi int marshal_client_req_uuid(char *);               // STR
  inkcoreapi int marshal_client_rx_error_code(char *);          // STR
  inkcoreapi int marshal_client_tx_error_code(char *);          // STR
  inkcoreapi int marshal_client_req_all_header_fields(char *);  // STR

  //
  // proxy -> client fields
  //
  inkcoreapi int marshal_proxy_resp_content_type(char *);      // STR
  inkcoreapi int marshal_proxy_resp_reason_phrase(char *);     // STR
  inkcoreapi int marshal_proxy_resp_squid_len(char *);         // INT
  inkcoreapi int marshal_proxy_resp_content_len(char *);       // INT
  inkcoreapi int marshal_proxy_resp_status_code(char *);       // INT
  inkcoreapi int marshal_proxy_resp_header_len(char *);        // INT
  inkcoreapi int marshal_proxy_finish_status_code(char *);     // INT
  inkcoreapi int marshal_cache_result_code(char *);            // INT
  inkcoreapi int marshal_cache_result_subcode(char *);         // INT
  inkcoreapi int marshal_proxy_host_port(char *);              // INT
  inkcoreapi int marshal_cache_hit_miss(char *);               // INT
  inkcoreapi int marshal_proxy_resp_all_header_fields(char *); // STR

  //
  // proxy -> server fields
  //
  inkcoreapi int marshal_proxy_req_header_len(char *);        // INT
  inkcoreapi int marshal_proxy_req_squid_len(char *);         // INT
  inkcoreapi int marshal_proxy_req_content_len(char *);       // INT
  inkcoreapi int marshal_proxy_req_server_ip(char *);         // INT
  inkcoreapi int marshal_proxy_req_server_port(char *);       // INT
  inkcoreapi int marshal_proxy_hierarchy_route(char *);       // INT
  inkcoreapi int marshal_next_hop_ip(char *);                 // STR
  inkcoreapi int marshal_next_hop_port(char *);               // INT
  inkcoreapi int marshal_proxy_host_name(char *);             // STR
  inkcoreapi int marshal_proxy_host_ip(char *);               // STR
  inkcoreapi int marshal_proxy_req_is_ssl(char *);            // INT
  inkcoreapi int marshal_proxy_req_all_header_fields(char *); // STR

  //
  // server -> proxy fields
  //
  inkcoreapi int marshal_server_host_ip(char *);                // INT
  inkcoreapi int marshal_server_host_name(char *);              // STR
  inkcoreapi int marshal_server_resp_status_code(char *);       // INT
  inkcoreapi int marshal_server_resp_squid_len(char *);         // INT
  inkcoreapi int marshal_server_resp_content_len(char *);       // INT
  inkcoreapi int marshal_server_resp_header_len(char *);        // INT
  inkcoreapi int marshal_server_resp_http_version(char *);      // INT
  inkcoreapi int marshal_server_resp_time_ms(char *);           // INT
  inkcoreapi int marshal_server_resp_time_s(char *);            // INT
  inkcoreapi int marshal_server_transact_count(char *);         // INT
  inkcoreapi int marshal_server_connect_attempts(char *);       // INT
  inkcoreapi int marshal_server_resp_all_header_fields(char *); // STR

  //
  // cache -> client fields
  //
  inkcoreapi int marshal_cache_resp_status_code(char *);       // INT
  inkcoreapi int marshal_cache_resp_squid_len(char *);         // INT
  inkcoreapi int marshal_cache_resp_content_len(char *);       // INT
  inkcoreapi int marshal_cache_resp_header_len(char *);        // INT
  inkcoreapi int marshal_cache_resp_http_version(char *);      // INT
  inkcoreapi int marshal_cache_resp_all_header_fields(char *); // STR

  inkcoreapi void set_client_req_url(char *, int);                // STR
  inkcoreapi void set_client_req_url_canon(char *, int);          // STR
  inkcoreapi void set_client_req_unmapped_url_canon(char *, int); // STR
  inkcoreapi void set_client_req_unmapped_url_path(char *, int);  // STR
  inkcoreapi void set_client_req_unmapped_url_host(char *, int);  // STR
  inkcoreapi void set_client_req_url_path(char *, int);           // STR

  //
  // congestion control -- client_retry_after_time
  //
  inkcoreapi int marshal_client_retry_after_time(char *); // INT

  //
  // cache write fields
  //
  inkcoreapi int marshal_cache_write_code(char *);           // INT
  inkcoreapi int marshal_cache_write_transform_code(char *); // INT

  // other fields
  //
  inkcoreapi int marshal_transfer_time_ms(char *);                            // INT
  inkcoreapi int marshal_transfer_time_s(char *);                             // INT
  inkcoreapi int marshal_file_size(char *);                                   // INT
  inkcoreapi int marshal_plugin_identity_id(char *);                          // INT
  inkcoreapi int marshal_plugin_identity_tag(char *);                         // STR
  inkcoreapi int marshal_process_uuid(char *);                                // STR
  inkcoreapi int marshal_client_http_connection_id(char *);                   // INT
  inkcoreapi int marshal_client_http_transaction_id(char *);                  // INT
  inkcoreapi int marshal_client_http_transaction_priority_weight(char *);     // INT
  inkcoreapi int marshal_client_http_transaction_priority_dependence(char *); // INT
  inkcoreapi int marshal_cache_lookup_url_canon(char *);                      // STR
  inkcoreapi int marshal_client_sni_server_name(char *);                      // STR
  inkcoreapi int marshal_version_build_number(char *);                        // STR

  // named fields from within a http header
  //
  inkcoreapi int marshal_http_header_field(LogField::Container container, char *field, char *buf);
  inkcoreapi int marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf);

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
  // milestones access
  //
  inkcoreapi int marshal_milestone(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_sec(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_squid(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_netscape(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_date(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_time(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_fmt_ms(TSMilestonesType ms, char *buf);
  inkcoreapi int marshal_milestone_diff(TSMilestonesType ms1, TSMilestonesType ms2, char *buf);
  inkcoreapi void set_http_header_field(LogField::Container container, char *field, char *buf, int len);
  //
  // unmarshalling routines
  //
  // They used to return a string; now they unmarshal directly into the
  // destination buffer supplied.
  //
  static int64_t unmarshal_int(char **buf);
  static int unmarshal_itoa(int64_t val, char *dest, int field_width = 0, char leading_char = ' ');
  static int unmarshal_itox(int64_t val, char *dest, int field_width = 0, char leading_char = ' ');
  static int unmarshal_int_to_str(char **buf, char *dest, int len);
  static int unmarshal_int_to_str_hex(char **buf, char *dest, int len);
  static int unmarshal_str(char **buf, char *dest, int len, LogSlice *slice = nullptr);
  static int unmarshal_ttmsf(char **buf, char *dest, int len);
  static int unmarshal_int_to_date_str(char **buf, char *dest, int len);
  static int unmarshal_int_to_time_str(char **buf, char *dest, int len);
  static int unmarshal_int_to_netscape_str(char **buf, char *dest, int len);
  static int unmarshal_http_version(char **buf, char *dest, int len);
  static int unmarshal_http_text(char **buf, char *dest, int len, LogSlice *slice = nullptr);
  static int unmarshal_http_status(char **buf, char *dest, int len);
  static int unmarshal_ip(char **buf, IpEndpoint *dest);
  static int unmarshal_ip_to_str(char **buf, char *dest, int len);
  static int unmarshal_ip_to_hex(char **buf, char *dest, int len);
  static int unmarshal_hierarchy(char **buf, char *dest, int len, const Ptr<LogFieldAliasMap> &map);
  static int unmarshal_finish_status(char **buf, char *dest, int len, const Ptr<LogFieldAliasMap> &map);
  static int unmarshal_cache_code(char **buf, char *dest, int len, const Ptr<LogFieldAliasMap> &map);
  static int unmarshal_cache_hit_miss(char **buf, char *dest, int len, const Ptr<LogFieldAliasMap> &map);
  static int unmarshal_cache_write_code(char **buf, char *dest, int len, const Ptr<LogFieldAliasMap> &map);
  static int unmarshal_client_protocol_stack(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map);

  static int unmarshal_with_map(int64_t code, char *dest, int len, const Ptr<LogFieldAliasMap> &map, const char *msg = nullptr);

  static int unmarshal_record(char **buf, char *dest, int len);

  //
  // our own strlen function that pads strings to even int64_t boundaries
  // so that there are no alignment problems with the int values.
  //
  static int round_strlen(int len);
  static int strlen(const char *str);

public:
  inkcoreapi static void marshal_int(char *dest, int64_t source);
  inkcoreapi static void marshal_str(char *dest, const char *source, int padded_len);
  inkcoreapi static void marshal_mem(char *dest, const char *source, int actual_len, int padded_len);
  inkcoreapi static int marshal_ip(char *dest, sockaddr const *ip);

  // noncopyable
  // -- member functions that are not allowed --
  LogAccess(const LogAccess &rhs) = delete;      // no copies
  LogAccess &operator=(LogAccess &rhs) = delete; // or assignment

private:
  HttpSM *m_http_sm;

  Arena m_arena;

  HTTPHdr *m_client_request;
  HTTPHdr *m_proxy_response;
  HTTPHdr *m_proxy_request;
  HTTPHdr *m_server_response;
  HTTPHdr *m_cache_response;

  char *m_client_req_url_str;
  int m_client_req_url_len;
  char *m_client_req_url_canon_str;
  int m_client_req_url_canon_len;
  char *m_client_req_unmapped_url_canon_str;
  int m_client_req_unmapped_url_canon_len;
  char *m_client_req_unmapped_url_path_str;
  int m_client_req_unmapped_url_path_len;
  char *m_client_req_unmapped_url_host_str;
  int m_client_req_unmapped_url_host_len;
  char const *m_client_req_url_path_str;
  int m_client_req_url_path_len;
  char *m_proxy_resp_content_type_str;
  int m_proxy_resp_content_type_len;
  char *m_proxy_resp_reason_phrase_str;
  int m_proxy_resp_reason_phrase_len;
  char *m_cache_lookup_url_canon_str;
  int m_cache_lookup_url_canon_len;

  void validate_unmapped_url();
  void validate_unmapped_url_path();

  void validate_lookup_url();
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
LogAccess::strlen(const char *str)
{
  if (str == nullptr || str[0] == 0) {
    return round_strlen(sizeof(DEFAULT_STR));
  } else {
    return (int)(round_strlen(((int)::strlen(str) + 1))); // actual bytes for string
  }
}

inline void
LogAccess::marshal_int(char *dest, int64_t source)
{
  // TODO: This used to do htonl on the source. TS-1156
  *((int64_t *)dest) = source;
}

/*-------------------------------------------------------------------------
  resolve_logfield_string

  This external function takes a format string and a LogAccess context and
  resolves any known fields to return a new, resolved string.
  -------------------------------------------------------------------------*/

char *resolve_logfield_string(LogAccess *context, const char *format_str);
