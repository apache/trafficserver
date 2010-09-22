/** @file

  Traffic Server SDK API header file

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

  @section developers Developers

  Developers, when adding a new element to an enum, append it. DO NOT
  insert it.  Otherwise, binary compatibility of plugins will be broken!

 */

#ifndef __INK_API_H__
#define __INK_API_H__

#include <sys/types.h>
#include <stdint.h>

#define inkapi
#define inkexp
#define inkimp

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#define INK_HTTP_VERSION(a,b)  ((((a) & 0xFFFF) << 16) | ((b) & 0xFFFF))
#define INK_HTTP_MINOR(v)      ((v) & 0xFFFF)
#define INK_HTTP_MAJOR(v)      (((v) >> 16) & 0xFFFF)
#define __INK_RES_PATH(x)   #x
#define _INK_RES_PATH(x)    __INK_RES_PATH (x)
#define INK_RES_PATH(x)     x __FILE__ ":" _INK_RES_PATH (__LINE__)
#define INK_RES_MEM_PATH    INK_RES_PATH ("memory/")
#define INK_MAX_USER_NAME_LEN 256

#if __GNUC__ >= 3
#ifndef INK_DEPRECATED
#define INK_DEPRECATED __attribute__ ((deprecated))
#endif
#else
#define INK_DEPRECATED
#endif

  /**
      The following struct is used by INKPluginRegister(). It stores
      registration information about the plugin.

   */
  typedef struct
  {
    char *plugin_name;
    char *vendor_name;
    char *support_email;
  } INKPluginRegistrationInfo;

  /**
      This set of enums are possible values returned by
      INKHttpHdrParseReq() and INKHttpHdrParseResp().

   */
  typedef enum
  {
    INK_PARSE_ERROR = -1,
    INK_PARSE_DONE = 0,
    INK_PARSE_OK = 1,
    INK_PARSE_CONT = 2
  } INKParseResult;

  /**
      This set of enums represents the possible HTTP types that
      can be assigned to an HTTP header. When a header is created
      with INKHttpHdrCreate(), it is automatically assigned a type of
      INK_HTTP_TYPE_UNKNOWN. You can modify the HTTP type ONCE after it
      the header is created, using INKHttpHdrTypeSet(). After setting the
      HTTP type once, you cannot set it again. Use INKHttpHdrTypeGet()
      to obtain the INKHttpType of an HTTP header.

   */
  typedef enum
  {
    INK_HTTP_TYPE_UNKNOWN,
    INK_HTTP_TYPE_REQUEST,
    INK_HTTP_TYPE_RESPONSE
  } INKHttpType;

  /**
      This set of enums represents possible return values from
      INKHttpHdrStatusGet(), which retrieves the status code from an
      HTTP response header (INKHttpHdrStatusGet() retrieves status codes
      only from headers of type INK_HTTP_TYPE_RESPONSE). You can also set
      the INKHttpStatus of a response header using INKHttpHdrStatusSet().

   */
  typedef enum
  {
    INK_HTTP_STATUS_NONE = 0,

    INK_HTTP_STATUS_CONTINUE = 100,
    INK_HTTP_STATUS_SWITCHING_PROTOCOL = 101,

    INK_HTTP_STATUS_OK = 200,
    INK_HTTP_STATUS_CREATED = 201,
    INK_HTTP_STATUS_ACCEPTED = 202,
    INK_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION = 203,
    INK_HTTP_STATUS_NO_CONTENT = 204,
    INK_HTTP_STATUS_RESET_CONTENT = 205,
    INK_HTTP_STATUS_PARTIAL_CONTENT = 206,

    INK_HTTP_STATUS_MULTIPLE_CHOICES = 300,
    INK_HTTP_STATUS_MOVED_PERMANENTLY = 301,
    INK_HTTP_STATUS_MOVED_TEMPORARILY = 302,
    INK_HTTP_STATUS_SEE_OTHER = 303,
    INK_HTTP_STATUS_NOT_MODIFIED = 304,
    INK_HTTP_STATUS_USE_PROXY = 305,
    INK_HTTP_STATUS_TEMPORARY_REDIRECT = 307,

    INK_HTTP_STATUS_BAD_REQUEST = 400,
    INK_HTTP_STATUS_UNAUTHORIZED = 401,
    INK_HTTP_STATUS_PAYMENT_REQUIRED = 402,
    INK_HTTP_STATUS_FORBIDDEN = 403,
    INK_HTTP_STATUS_NOT_FOUND = 404,
    INK_HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    INK_HTTP_STATUS_NOT_ACCEPTABLE = 406,
    INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED = 407,
    INK_HTTP_STATUS_REQUEST_TIMEOUT = 408,
    INK_HTTP_STATUS_CONFLICT = 409,
    INK_HTTP_STATUS_GONE = 410,
    INK_HTTP_STATUS_LENGTH_REQUIRED = 411,
    INK_HTTP_STATUS_PRECONDITION_FAILED = 412,
    INK_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE = 413,
    INK_HTTP_STATUS_REQUEST_URI_TOO_LONG = 414,
    INK_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415,

    INK_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    INK_HTTP_STATUS_NOT_IMPLEMENTED = 501,
    INK_HTTP_STATUS_BAD_GATEWAY = 502,
    INK_HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
    INK_HTTP_STATUS_GATEWAY_TIMEOUT = 504,
    INK_HTTP_STATUS_HTTPVER_NOT_SUPPORTED = 505
  } INKHttpStatus;

  /**
      This set of enums represents the possible hooks where you can
      set up continuation callbacks. The functions used to register a
      continuation for a particular hook are:

      INKHttpHookAdd: adds a global hook. You can globally add
      any hook except for INK_HTTP_REQUEST_TRANSFORM_HOOK and
      INK_HTTP_RESPONSE_TRANSFORM_HOOK.

      The following hooks can ONLY be added globally:
       - INK_HTTP_SELECT_ALT_HOOK
       - INK_HTTP_SSN_START_HOOK
       - INK_HTTP_SSN_CLOSE_HOOK

      INKHttpSsnHookAdd: adds a transaction hook to each transaction
      within a session. You can only use transaction hooks with this call:
       - INK_HTTP_READ_REQUEST_HDR_HOOK
       - INK_HTTP_OS_DNS_HOOK
       - INK_HTTP_SEND_REQUEST_HDR_HOOK
       - INK_HTTP_READ_CACHE_HDR_HOOK
       - INK_HTTP_READ_RESPONSE_HDR_HOOK
       - INK_HTTP_SEND_RESPONSE_HDR_HOOK
       - INK_HTTP_REQUEST_TRANSFORM_HOOK
       - INK_HTTP_RESPONSE_TRANSFORM_HOOK
       - INK_HTTP_TXN_START_HOOK
       - INK_HTTP_TXN_CLOSE_HOOK

      INKHttpTxnHookAdd: adds a callback at a specific point within
      an HTTP transaction. The following hooks can be used with this
      function:
       - INK_HTTP_READ_REQUEST_HDR_HOOK
       - INK_HTTP_OS_DNS_HOOK
       - INK_HTTP_SEND_REQUEST_HDR_HOOK
       - INK_HTTP_READ_CACHE_HDR_HOOK
       - INK_HTTP_READ_RESPONSE_HDR_HOOK
       - INK_HTTP_SEND_RESPONSE_HDR_HOOK
       - INK_HTTP_REQUEST_TRANSFORM_HOOK
       - INK_HTTP_RESPONSE_TRANSFORM_HOOK
       - INK_HTTP_TXN_CLOSE_HOOK

      The two transform hooks can ONLY be added as transaction hooks.

      INK_HTTP_LAST_HOOK _must_ be the last element. Only right place
      to insert a new element is just before INK_HTTP_LAST_HOOK.

   */
  typedef enum
  {
    INK_HTTP_READ_REQUEST_HDR_HOOK,
    INK_HTTP_OS_DNS_HOOK,
    INK_HTTP_SEND_REQUEST_HDR_HOOK,
    INK_HTTP_READ_CACHE_HDR_HOOK,
    INK_HTTP_READ_RESPONSE_HDR_HOOK,
    INK_HTTP_SEND_RESPONSE_HDR_HOOK,
    INK_HTTP_REQUEST_TRANSFORM_HOOK,
    INK_HTTP_RESPONSE_TRANSFORM_HOOK,
    INK_HTTP_SELECT_ALT_HOOK,
    INK_HTTP_TXN_START_HOOK,
    INK_HTTP_TXN_CLOSE_HOOK,
    INK_HTTP_SSN_START_HOOK,
    INK_HTTP_SSN_CLOSE_HOOK,
    INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK,
    INK_HTTP_READ_REQUEST_PRE_REMAP_HOOK,
    INK_HTTP_LAST_HOOK
  } INKHttpHookID;

  typedef enum
  {
    INK_CACHE_PLUGIN_HOOK,
    INK_CACHE_LOOKUP_HOOK,
    INK_CACHE_READ_HOOK,
    INK_CACHE_WRITE_HOOK,
    INK_CACHE_DELETE_HOOK,
    INK_CACHE_LAST_HOOK
  } INKCacheHookID;

  /**
      INKEvents are sent to continuations when they are called back.
      The INKEvent provides the continuation's handler function with
      information about the callback. Based on the event it receives,
      the handler function can decide what to do.

   */
  typedef enum
  {
    INK_EVENT_NONE = 0,
    INK_EVENT_IMMEDIATE = 1,
    INK_EVENT_TIMEOUT = 2,
    INK_EVENT_ERROR = 3,
    INK_EVENT_CONTINUE = 4,

    INK_EVENT_VCONN_READ_READY = 100,
    INK_EVENT_VCONN_WRITE_READY = 101,
    INK_EVENT_VCONN_READ_COMPLETE = 102,
    INK_EVENT_VCONN_WRITE_COMPLETE = 103,
    INK_EVENT_VCONN_EOS = 104,
    INK_EVENT_VCONN_INACTIVITY_TIMEOUT = 105,

    INK_EVENT_NET_CONNECT = 200,
    INK_EVENT_NET_CONNECT_FAILED = 201,
    INK_EVENT_NET_ACCEPT = 202,
    INK_EVENT_NET_ACCEPT_FAILED = 204,

    /* EVENTS 206 - 212 for internal use */
    INK_EVENT_INTERNAL_206 = 206,
    INK_EVENT_INTERNAL_207 = 207,
    INK_EVENT_INTERNAL_208 = 208,
    INK_EVENT_INTERNAL_209 = 209,
    INK_EVENT_INTERNAL_210 = 210,
    INK_EVENT_INTERNAL_211 = 211,
    INK_EVENT_INTERNAL_212 = 212,

    INK_EVENT_HOST_LOOKUP = 500,
    INK_EVENT_CACHE_OPEN_READ = 1102,
    INK_EVENT_CACHE_OPEN_READ_FAILED = 1103,
    INK_EVENT_CACHE_OPEN_WRITE = 1108,
    INK_EVENT_CACHE_OPEN_WRITE_FAILED = 1109,
    INK_EVENT_CACHE_REMOVE = 1112,
    INK_EVENT_CACHE_REMOVE_FAILED = 1113,
    INK_EVENT_CACHE_SCAN = 1120,
    INK_EVENT_CACHE_SCAN_FAILED = 1121,
    INK_EVENT_CACHE_SCAN_OBJECT = 1122,
    INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED = 1123,
    INK_EVENT_CACHE_SCAN_OPERATION_FAILED = 1124,
    INK_EVENT_CACHE_SCAN_DONE = 1125,

    INK_EVENT_CACHE_LOOKUP = 1126,
    INK_EVENT_CACHE_READ = 1127,
    INK_EVENT_CACHE_DELETE = 1128,
    INK_EVENT_CACHE_WRITE = 1129,
    INK_EVENT_CACHE_WRITE_HEADER = 1130,
    INK_EVENT_CACHE_CLOSE = 1131,
    INK_EVENT_CACHE_LOOKUP_READY = 1132,
    INK_EVENT_CACHE_LOOKUP_COMPLETE = 1133,
    INK_EVENT_CACHE_READ_READY = 1134,
    INK_EVENT_CACHE_READ_COMPLETE = 1135,

    /* EVENT 1200 for internal use */
    INK_EVENT_INTERNAL_1200 = 1200,

    /* EVENT 3900 is corresponding to event AIO_EVENT_DONE defined in I_AIO.h */
    INK_AIO_EVENT_DONE = 3900,

    INK_EVENT_HTTP_CONTINUE = 60000,
    INK_EVENT_HTTP_ERROR = 60001,
    INK_EVENT_HTTP_READ_REQUEST_HDR = 60002,
    INK_EVENT_HTTP_OS_DNS = 60003,
    INK_EVENT_HTTP_SEND_REQUEST_HDR = 60004,
    INK_EVENT_HTTP_READ_CACHE_HDR = 60005,
    INK_EVENT_HTTP_READ_RESPONSE_HDR = 60006,
    INK_EVENT_HTTP_SEND_RESPONSE_HDR = 60007,
    INK_EVENT_HTTP_REQUEST_TRANSFORM = 60008,
    INK_EVENT_HTTP_RESPONSE_TRANSFORM = 60009,
    INK_EVENT_HTTP_SELECT_ALT = 60010,
    INK_EVENT_HTTP_TXN_START = 60011,
    INK_EVENT_HTTP_TXN_CLOSE = 60012,
    INK_EVENT_HTTP_SSN_START = 60013,
    INK_EVENT_HTTP_SSN_CLOSE = 60014,
    INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE = 60015,
    INK_EVENT_HTTP_READ_REQUEST_PRE_REMAP = 60016,
    INK_EVENT_MGMT_UPDATE = 60100,

    /* EVENTS 60200 - 60202 for internal use */
    INK_EVENT_INTERNAL_60200 = 60200,
    INK_EVENT_INTERNAL_60201 = 60201,
    INK_EVENT_INTERNAL_60202 = 60202
  } INKEvent;

  typedef enum
  { INK_SRVSTATE_STATE_UNDEFINED = 0,
    INK_SRVSTATE_ACTIVE_TIMEOUT,
    INK_SRVSTATE_BAD_INCOMING_RESPONSE,
    INK_SRVSTATE_CONNECTION_ALIVE,
    INK_SRVSTATE_CONNECTION_CLOSED,
    INK_SRVSTATE_CONNECTION_ERROR,
    INK_SRVSTATE_INACTIVE_TIMEOUT,
    INK_SRVSTATE_OPEN_RAW_ERROR,
    INK_SRVSTATE_PARSE_ERROR,
    INK_SRVSTATE_TRANSACTION_COMPLETE,
    INK_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_F,
    INK_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_M
  } INKServerState;

  typedef enum
  {
    INK_LOOKUP_UNDEFINED_LOOKUP,
    INK_LOOKUP_ICP_SUGGESTED_HOST,
    INK_LOOKUP_PARENT_PROXY,
    INK_LOOKUP_ORIGIN_SERVER,
    INK_LOOKUP_INCOMING_ROUTER,
    INK_LOOKUP_HOST_NONE
  } INKLookingUpType;

  typedef enum
  {
    INK_CACHE_LOOKUP_MISS,
    INK_CACHE_LOOKUP_HIT_STALE,
    INK_CACHE_LOOKUP_HIT_FRESH,
    INK_CACHE_LOOKUP_SKIPPED
  } INKCacheLookupResult;

  typedef enum
  {
    INK_CACHE_DATA_TYPE_NONE,
    INK_CACHE_DATA_TYPE_HTTP,
    INK_CACHE_DATA_TYPE_OTHER
  } INKCacheDataType;

  typedef enum
  {
    INK_CACHE_ERROR_NO_DOC = -20400,
    INK_CACHE_ERROR_DOC_BUSY = -20401,
    INK_CACHE_ERROR_NOT_READY = -20407
  } INKCacheError;

  typedef enum
  {
    INK_CACHE_SCAN_RESULT_DONE = 0,
    INK_CACHE_SCAN_RESULT_CONTINUE = 1,
    INK_CACHE_SCAN_RESULT_DELETE = 10,
    INK_CACHE_SCAN_RESULT_DELETE_ALL_ALTERNATES,
    INK_CACHE_SCAN_RESULT_UPDATE,
    INK_CACHE_SCAN_RESULT_RETRY
  } INKCacheScanResult;

  typedef enum
  {
    INK_DATA_ALLOCATE,
    INK_DATA_MALLOCED,
    INK_DATA_CONSTANT
  } INKIOBufferDataFlags;

  typedef enum
  {
    INK_VC_CLOSE_ABORT = -1,
    INK_VC_CLOSE_NORMAL = 1
  } INKVConnCloseFlags;

  typedef enum
  {
    INK_IOBUFFER_SIZE_INDEX_128 = 0,
    INK_IOBUFFER_SIZE_INDEX_256 = 1,
    INK_IOBUFFER_SIZE_INDEX_512 = 2,
    INK_IOBUFFER_SIZE_INDEX_1K = 3,
    INK_IOBUFFER_SIZE_INDEX_2K = 4,
    INK_IOBUFFER_SIZE_INDEX_4K = 5,
    INK_IOBUFFER_SIZE_INDEX_8K = 6,
    INK_IOBUFFER_SIZE_INDEX_16K = 7,
    INK_IOBUFFER_SIZE_INDEX_32K = 8
  } INKIOBufferSizeIndex;

  /**
      Starting 2.0, SDK now follows same versioning as Traffic Server.

   */
  typedef enum
  {
    INK_SDK_VERSION_2_0 = 0
  } INKSDKVersion;

  typedef enum
  {
    INK_ERROR = -1,
    INK_SUCCESS = 0
  } INKReturnCode;

  typedef enum
  {
     NO_CALLBACK = 0,
     AFTER_HEADER ,
     AFTER_BODY
  } INKFetchWakeUpOptions;
  extern inkapi const void *INK_ERROR_PTR;

  typedef int INK32;
  typedef unsigned int INKU32;
  typedef long long INK64;
  typedef unsigned long long INKU64;

  /* These typedefs are used with the corresponding INKMgmt*Get functions
     for storing the values retrieved by those functions. For example,
     INKMgmtCounterGet() retrieves an INKMgmtCounter. */
  typedef INK64 INKMgmtInt;
  typedef INK64 INKMgmtCounter;
  typedef float INKMgmtFloat;
  typedef char *INKMgmtString;

  typedef void *INKFile;

  typedef void *INKMLoc;
  typedef void *INKMBuffer;
  typedef void *INKHttpSsn;
  typedef void *INKHttpTxn;
  typedef void *INKHttpAltInfo;
  typedef void *INKMimeParser;
  typedef void *INKHttpParser;
  typedef void *INKCacheKey;
  typedef void *INKCacheHttpInfo;
  typedef void *INKCacheTxn;

  typedef void *INKVIO;
  typedef void *INKThread;
  typedef void *INKMutex;
  typedef void *INKConfig;
  typedef void *INKCont;
  typedef void *INKAction;
  typedef void *INKVConn;
  typedef void *INKIOBuffer;
  typedef void *INKIOBufferData;
  typedef void *INKIOBufferBlock;
  typedef void *INKIOBufferReader;
  typedef void *INKHostLookupResult;

  typedef void *(*INKThreadFunc) (void *data);
  typedef int (*INKEventFunc) (INKCont contp, INKEvent event, void *edata);
  typedef void (*INKConfigDestroyFunc) (void *data);
  typedef struct
  {
     int success_event_id;
     int failure_event_id;
     int timeout_event_id;
  }INKFetchEvent;
  typedef struct INKFetchUrlParams
  {
     const char *request;
     int request_len;
     unsigned int ip;
     int port;
     INKCont contp;
     INKFetchEvent events;
     INKFetchWakeUpOptions options;
     struct INKFetchUrlParams *next;
  }INKFetchUrlParams_t;

  /* --------------------------------------------------------------------------
     Init */

  /**
      This function must be defined by all plugins. Traffic Server
      calls this initialization routine when it loads the plugin (at
      startup), and sets argc and argv appropriately based on the values
      in plugin.config.

      @param argc the number of initial values specified in plugin.config,
        plus one. If only the name of your plugin shared object is
        specified in plugin.config, argc=1.
      @param argv the vector of arguments. The length of argv is argc.
        argv[0] is the name of the plugin shared library. Subsequent
        values of argv are initialization values specified in
        plugin.config.

   */
  extern inkexp void INKPluginInit(int argc, const char *argv[]);

  /* --------------------------------------------------------------------------
     License */
  /**
      This function lets Traffic Server know that a license key is
      required for the plugin. You implement this function to return the
      necessary value. If a license is required, Traffic Server looks
      at the plugin.db file for the license key. If this function is
      not defined, a license is not required. Use the gen_key tool to
      generate license keys.

      @return Zero if no license is required. Returns 1 if a license
        is required.
  */
  extern inkexp int INKPluginLicenseRequired(void);

  /* --------------------------------------------------------------------------
     URL schemes */
  extern inkapi const char *INK_URL_SCHEME_FILE;
  extern inkapi const char *INK_URL_SCHEME_FTP;
  extern inkapi const char *INK_URL_SCHEME_GOPHER;
  extern inkapi const char *INK_URL_SCHEME_HTTP;
  extern inkapi const char *INK_URL_SCHEME_HTTPS;
  extern inkapi const char *INK_URL_SCHEME_MAILTO;
  extern inkapi const char *INK_URL_SCHEME_NEWS;
  extern inkapi const char *INK_URL_SCHEME_NNTP;
  extern inkapi const char *INK_URL_SCHEME_PROSPERO;
  extern inkapi const char *INK_URL_SCHEME_TELNET;
  extern inkapi const char *INK_URL_SCHEME_WAIS;

  /* --------------------------------------------------------------------------
     URL scheme string lengths */
  extern inkapi int INK_URL_LEN_FILE;
  extern inkapi int INK_URL_LEN_FTP;
  extern inkapi int INK_URL_LEN_GOPHER;
  extern inkapi int INK_URL_LEN_HTTP;
  extern inkapi int INK_URL_LEN_HTTPS;
  extern inkapi int INK_URL_LEN_MAILTO;
  extern inkapi int INK_URL_LEN_NEWS;
  extern inkapi int INK_URL_LEN_NNTP;
  extern inkapi int INK_URL_LEN_PROSPERO;
  extern inkapi int INK_URL_LEN_TELNET;
  extern inkapi int INK_URL_LEN_WAIS;

  /* --------------------------------------------------------------------------
     MIME fields */
  extern inkapi const char *INK_MIME_FIELD_ACCEPT;
  extern inkapi const char *INK_MIME_FIELD_ACCEPT_CHARSET;
  extern inkapi const char *INK_MIME_FIELD_ACCEPT_ENCODING;
  extern inkapi const char *INK_MIME_FIELD_ACCEPT_LANGUAGE;
  extern inkapi const char *INK_MIME_FIELD_ACCEPT_RANGES;
  extern inkapi const char *INK_MIME_FIELD_AGE;
  extern inkapi const char *INK_MIME_FIELD_ALLOW;
  extern inkapi const char *INK_MIME_FIELD_APPROVED;
  extern inkapi const char *INK_MIME_FIELD_AUTHORIZATION;
  extern inkapi const char *INK_MIME_FIELD_BYTES;
  extern inkapi const char *INK_MIME_FIELD_CACHE_CONTROL;
  extern inkapi const char *INK_MIME_FIELD_CLIENT_IP;
  extern inkapi const char *INK_MIME_FIELD_CONNECTION;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_BASE;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_ENCODING;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_LANGUAGE;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_LENGTH;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_LOCATION;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_MD5;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_RANGE;
  extern inkapi const char *INK_MIME_FIELD_CONTENT_TYPE;
  extern inkapi const char *INK_MIME_FIELD_CONTROL;
  extern inkapi const char *INK_MIME_FIELD_COOKIE;
  extern inkapi const char *INK_MIME_FIELD_DATE;
  extern inkapi const char *INK_MIME_FIELD_DISTRIBUTION;
  extern inkapi const char *INK_MIME_FIELD_ETAG;
  extern inkapi const char *INK_MIME_FIELD_EXPECT;
  extern inkapi const char *INK_MIME_FIELD_EXPIRES;
  extern inkapi const char *INK_MIME_FIELD_FOLLOWUP_TO;
  extern inkapi const char *INK_MIME_FIELD_FROM;
  extern inkapi const char *INK_MIME_FIELD_HOST;
  extern inkapi const char *INK_MIME_FIELD_IF_MATCH;
  extern inkapi const char *INK_MIME_FIELD_IF_MODIFIED_SINCE;
  extern inkapi const char *INK_MIME_FIELD_IF_NONE_MATCH;
  extern inkapi const char *INK_MIME_FIELD_IF_RANGE;
  extern inkapi const char *INK_MIME_FIELD_IF_UNMODIFIED_SINCE;
  extern inkapi const char *INK_MIME_FIELD_KEEP_ALIVE;
  extern inkapi const char *INK_MIME_FIELD_KEYWORDS;
  extern inkapi const char *INK_MIME_FIELD_LAST_MODIFIED;
  extern inkapi const char *INK_MIME_FIELD_LINES;
  extern inkapi const char *INK_MIME_FIELD_LOCATION;
  extern inkapi const char *INK_MIME_FIELD_MAX_FORWARDS;
  extern inkapi const char *INK_MIME_FIELD_MESSAGE_ID;
  extern inkapi const char *INK_MIME_FIELD_NEWSGROUPS;
  extern inkapi const char *INK_MIME_FIELD_ORGANIZATION;
  extern inkapi const char *INK_MIME_FIELD_PATH;
  extern inkapi const char *INK_MIME_FIELD_PRAGMA;
  extern inkapi const char *INK_MIME_FIELD_PROXY_AUTHENTICATE;
  extern inkapi const char *INK_MIME_FIELD_PROXY_AUTHORIZATION;
  extern inkapi const char *INK_MIME_FIELD_PROXY_CONNECTION;
  extern inkapi const char *INK_MIME_FIELD_PUBLIC;
  extern inkapi const char *INK_MIME_FIELD_RANGE;
  extern inkapi const char *INK_MIME_FIELD_REFERENCES;
  extern inkapi const char *INK_MIME_FIELD_REFERER;
  extern inkapi const char *INK_MIME_FIELD_REPLY_TO;
  extern inkapi const char *INK_MIME_FIELD_RETRY_AFTER;
  extern inkapi const char *INK_MIME_FIELD_SENDER;
  extern inkapi const char *INK_MIME_FIELD_SERVER;
  extern inkapi const char *INK_MIME_FIELD_SET_COOKIE;
  extern inkapi const char *INK_MIME_FIELD_SUBJECT;
  extern inkapi const char *INK_MIME_FIELD_SUMMARY;
  extern inkapi const char *INK_MIME_FIELD_TE;
  extern inkapi const char *INK_MIME_FIELD_TRANSFER_ENCODING;
  extern inkapi const char *INK_MIME_FIELD_UPGRADE;
  extern inkapi const char *INK_MIME_FIELD_USER_AGENT;
  extern inkapi const char *INK_MIME_FIELD_VARY;
  extern inkapi const char *INK_MIME_FIELD_VIA;
  extern inkapi const char *INK_MIME_FIELD_WARNING;
  extern inkapi const char *INK_MIME_FIELD_WWW_AUTHENTICATE;
  extern inkapi const char *INK_MIME_FIELD_XREF;
  extern inkapi const char *INK_MIME_FIELD_X_FORWARDED_FOR;

  /* --------------------------------------------------------------------------
     MIME fields string lengths */
  extern inkapi int INK_MIME_LEN_ACCEPT;
  extern inkapi int INK_MIME_LEN_ACCEPT_CHARSET;
  extern inkapi int INK_MIME_LEN_ACCEPT_ENCODING;
  extern inkapi int INK_MIME_LEN_ACCEPT_LANGUAGE;
  extern inkapi int INK_MIME_LEN_ACCEPT_RANGES;
  extern inkapi int INK_MIME_LEN_AGE;
  extern inkapi int INK_MIME_LEN_ALLOW;
  extern inkapi int INK_MIME_LEN_APPROVED;
  extern inkapi int INK_MIME_LEN_AUTHORIZATION;
  extern inkapi int INK_MIME_LEN_BYTES;
  extern inkapi int INK_MIME_LEN_CACHE_CONTROL;
  extern inkapi int INK_MIME_LEN_CLIENT_IP;
  extern inkapi int INK_MIME_LEN_CONNECTION;
  extern inkapi int INK_MIME_LEN_CONTENT_BASE;
  extern inkapi int INK_MIME_LEN_CONTENT_ENCODING;
  extern inkapi int INK_MIME_LEN_CONTENT_LANGUAGE;
  extern inkapi int INK_MIME_LEN_CONTENT_LENGTH;
  extern inkapi int INK_MIME_LEN_CONTENT_LOCATION;
  extern inkapi int INK_MIME_LEN_CONTENT_MD5;
  extern inkapi int INK_MIME_LEN_CONTENT_RANGE;
  extern inkapi int INK_MIME_LEN_CONTENT_TYPE;
  extern inkapi int INK_MIME_LEN_CONTROL;
  extern inkapi int INK_MIME_LEN_COOKIE;
  extern inkapi int INK_MIME_LEN_DATE;
  extern inkapi int INK_MIME_LEN_DISTRIBUTION;
  extern inkapi int INK_MIME_LEN_ETAG;
  extern inkapi int INK_MIME_LEN_EXPECT;
  extern inkapi int INK_MIME_LEN_EXPIRES;
  extern inkapi int INK_MIME_LEN_FOLLOWUP_TO;
  extern inkapi int INK_MIME_LEN_FROM;
  extern inkapi int INK_MIME_LEN_HOST;
  extern inkapi int INK_MIME_LEN_IF_MATCH;
  extern inkapi int INK_MIME_LEN_IF_MODIFIED_SINCE;
  extern inkapi int INK_MIME_LEN_IF_NONE_MATCH;
  extern inkapi int INK_MIME_LEN_IF_RANGE;
  extern inkapi int INK_MIME_LEN_IF_UNMODIFIED_SINCE;
  extern inkapi int INK_MIME_LEN_KEEP_ALIVE;
  extern inkapi int INK_MIME_LEN_KEYWORDS;
  extern inkapi int INK_MIME_LEN_LAST_MODIFIED;
  extern inkapi int INK_MIME_LEN_LINES;
  extern inkapi int INK_MIME_LEN_LOCATION;
  extern inkapi int INK_MIME_LEN_MAX_FORWARDS;
  extern inkapi int INK_MIME_LEN_MESSAGE_ID;
  extern inkapi int INK_MIME_LEN_NEWSGROUPS;
  extern inkapi int INK_MIME_LEN_ORGANIZATION;
  extern inkapi int INK_MIME_LEN_PATH;
  extern inkapi int INK_MIME_LEN_PRAGMA;
  extern inkapi int INK_MIME_LEN_PROXY_AUTHENTICATE;
  extern inkapi int INK_MIME_LEN_PROXY_AUTHORIZATION;
  extern inkapi int INK_MIME_LEN_PROXY_CONNECTION;
  extern inkapi int INK_MIME_LEN_PUBLIC;
  extern inkapi int INK_MIME_LEN_RANGE;
  extern inkapi int INK_MIME_LEN_REFERENCES;
  extern inkapi int INK_MIME_LEN_REFERER;
  extern inkapi int INK_MIME_LEN_REPLY_TO;
  extern inkapi int INK_MIME_LEN_RETRY_AFTER;
  extern inkapi int INK_MIME_LEN_SENDER;
  extern inkapi int INK_MIME_LEN_SERVER;
  extern inkapi int INK_MIME_LEN_SET_COOKIE;
  extern inkapi int INK_MIME_LEN_SUBJECT;
  extern inkapi int INK_MIME_LEN_SUMMARY;
  extern inkapi int INK_MIME_LEN_TE;
  extern inkapi int INK_MIME_LEN_TRANSFER_ENCODING;
  extern inkapi int INK_MIME_LEN_UPGRADE;
  extern inkapi int INK_MIME_LEN_USER_AGENT;
  extern inkapi int INK_MIME_LEN_VARY;
  extern inkapi int INK_MIME_LEN_VIA;
  extern inkapi int INK_MIME_LEN_WARNING;
  extern inkapi int INK_MIME_LEN_WWW_AUTHENTICATE;
  extern inkapi int INK_MIME_LEN_XREF;
  extern inkapi int INK_MIME_LEN_X_FORWARDED_FOR;

  /* --------------------------------------------------------------------------
     HTTP values */
  extern inkapi const char *INK_HTTP_VALUE_BYTES;
  extern inkapi const char *INK_HTTP_VALUE_CHUNKED;
  extern inkapi const char *INK_HTTP_VALUE_CLOSE;
  extern inkapi const char *INK_HTTP_VALUE_COMPRESS;
  extern inkapi const char *INK_HTTP_VALUE_DEFLATE;
  extern inkapi const char *INK_HTTP_VALUE_GZIP;
  extern inkapi const char *INK_HTTP_VALUE_IDENTITY;
  extern inkapi const char *INK_HTTP_VALUE_KEEP_ALIVE;
  extern inkapi const char *INK_HTTP_VALUE_MAX_AGE;
  extern inkapi const char *INK_HTTP_VALUE_MAX_STALE;
  extern inkapi const char *INK_HTTP_VALUE_MIN_FRESH;
  extern inkapi const char *INK_HTTP_VALUE_MUST_REVALIDATE;
  extern inkapi const char *INK_HTTP_VALUE_NONE;
  extern inkapi const char *INK_HTTP_VALUE_NO_CACHE;
  extern inkapi const char *INK_HTTP_VALUE_NO_STORE;
  extern inkapi const char *INK_HTTP_VALUE_NO_TRANSFORM;
  extern inkapi const char *INK_HTTP_VALUE_ONLY_IF_CACHED;
  extern inkapi const char *INK_HTTP_VALUE_PRIVATE;
  extern inkapi const char *INK_HTTP_VALUE_PROXY_REVALIDATE;
  extern inkapi const char *INK_HTTP_VALUE_PUBLIC;
  extern inkapi const char *INK_HTTP_VALUE_SMAX_AGE;

  /* --------------------------------------------------------------------------
     HTTP values string lengths */
  extern inkapi int INK_HTTP_LEN_BYTES;
  extern inkapi int INK_HTTP_LEN_CHUNKED;
  extern inkapi int INK_HTTP_LEN_CLOSE;
  extern inkapi int INK_HTTP_LEN_COMPRESS;
  extern inkapi int INK_HTTP_LEN_DEFLATE;
  extern inkapi int INK_HTTP_LEN_GZIP;
  extern inkapi int INK_HTTP_LEN_IDENTITY;
  extern inkapi int INK_HTTP_LEN_KEEP_ALIVE;
  extern inkapi int INK_HTTP_LEN_MAX_AGE;
  extern inkapi int INK_HTTP_LEN_MAX_STALE;
  extern inkapi int INK_HTTP_LEN_MIN_FRESH;
  extern inkapi int INK_HTTP_LEN_MUST_REVALIDATE;
  extern inkapi int INK_HTTP_LEN_NONE;
  extern inkapi int INK_HTTP_LEN_NO_CACHE;
  extern inkapi int INK_HTTP_LEN_NO_STORE;
  extern inkapi int INK_HTTP_LEN_NO_TRANSFORM;
  extern inkapi int INK_HTTP_LEN_ONLY_IF_CACHED;
  extern inkapi int INK_HTTP_LEN_PRIVATE;
  extern inkapi int INK_HTTP_LEN_PROXY_REVALIDATE;
  extern inkapi int INK_HTTP_LEN_PUBLIC;
  extern inkapi int INK_HTTP_LEN_SMAX_AGE;

  /* --------------------------------------------------------------------------
     HTTP methods */
  extern inkapi const char *INK_HTTP_METHOD_CONNECT;
  extern inkapi const char *INK_HTTP_METHOD_DELETE;
  extern inkapi const char *INK_HTTP_METHOD_GET;
  extern inkapi const char *INK_HTTP_METHOD_HEAD;
  extern inkapi const char *INK_HTTP_METHOD_ICP_QUERY;
  extern inkapi const char *INK_HTTP_METHOD_OPTIONS;
  extern inkapi const char *INK_HTTP_METHOD_POST;
  extern inkapi const char *INK_HTTP_METHOD_PURGE;
  extern inkapi const char *INK_HTTP_METHOD_PUT;
  extern inkapi const char *INK_HTTP_METHOD_TRACE;

  /* --------------------------------------------------------------------------
     HTTP methods string lengths */
  extern inkapi int INK_HTTP_LEN_CONNECT;
  extern inkapi int INK_HTTP_LEN_DELETE;
  extern inkapi int INK_HTTP_LEN_GET;
  extern inkapi int INK_HTTP_LEN_HEAD;
  extern inkapi int INK_HTTP_LEN_ICP_QUERY;
  extern inkapi int INK_HTTP_LEN_OPTIONS;
  extern inkapi int INK_HTTP_LEN_POST;
  extern inkapi int INK_HTTP_LEN_PURGE;
  extern inkapi int INK_HTTP_LEN_PUT;
  extern inkapi int INK_HTTP_LEN_TRACE;

  /* --------------------------------------------------------------------------
     MLoc Constants */
  /**
      Use INK_NULL_MLOC as the parent in calls that require a parent
      when an INKMLoc does not have a parent INKMLoc. For example if
      the INKMLoc is obtained by a call to INKHttpTxnClientReqGet(),

   */
  extern inkapi const INKMLoc INK_NULL_MLOC;

  /* --------------------------------------------------------------------------
     Memory */
#define INKmalloc(s)      _INKmalloc ((s), INK_RES_MEM_PATH)
#define INKrealloc(p,s)   _INKrealloc ((p), (s), INK_RES_MEM_PATH)
#define INKstrdup(p)      _INKstrdup ((p), -1, INK_RES_MEM_PATH)
#define INKstrndup(p,n)   _INKstrdup ((p), (n), INK_RES_MEM_PATH)
#define INKfree(p)        _INKfree (p)

  inkapi void *_INKmalloc(unsigned int size, const char *path);
  inkapi void *_INKrealloc(void *ptr, unsigned int size, const char *path);
  inkapi char *_INKstrdup(const char *str, int length, const char *path);
  inkapi void _INKfree(void *ptr);

  /* --------------------------------------------------------------------------
     Component object handles */
  /**
      Releases the INKMLoc mloc created from the INKMLoc parent.
      If there is no parent INKMLoc, use INK_NULL_MLOC.

      @param bufp marshal buffer containing the INKMLoc handle to be
        released.
      @param parent location of the parent object from which the handle
        was created.
      @param mloc location of the handle to be released.

   */
  inkapi INKReturnCode INKHandleMLocRelease(INKMBuffer bufp, INKMLoc parent, INKMLoc mloc);

  /**
      Releases the string str created from the INKMLoc parent. Do not use
      INKHandleStringRelease() for strings created by INKUrlStringGet();
      in that special case, free the string with INKfree().

      @param bufp marshal buffer containing the string to be released.
      @param parent location of the parent object from which the string
        handle was created.
      @param str pointer to the string to be released.

   */
  inkapi INKReturnCode INKHandleStringRelease(INKMBuffer bufp, INKMLoc parent, const char *str);

  /* --------------------------------------------------------------------------
     Install and plugin locations */
  /**
      Gets the path of the directory in which Traffic Server is installed.
      Use this function to specify the location of files that the
      plugin uses.

      @return pointer to Traffic Server install directory.

   */
  inkapi const char *INKInstallDirGet(void);

  /**
      Gets the path of the directory of Traffic Server configuration.

      @return pointer to Traffic Server configuration directory.

   */
  inkapi const char *INKConfigDirGet(void);

  /**
      Gets the path of the plugin directory relative to the Traffic Server
      install directory. For example, to open the file "config_ui.txt" in
      the plugin directory:

      @code
      INKfopen("INKPluginInstallDirGet()/INKPluginDirGet()/config_ui.txt");
      @endcode

      @return pointer to plugin directory relative to Traffic Server install
      directory.

   */
  inkapi const char *INKPluginDirGet(void);

  /* --------------------------------------------------------------------------
     Traffic Server Version */
  /**
      Gets the version of Traffic Server currently running. Use this
      function to make sure that the plugin version and Traffic Server
      version are compatible. See the SDK sample code for usage.

      @return pointer to version of Traffic Server running the plugin.

   */
  inkapi const char *INKTrafficServerVersionGet(void);

  /* --------------------------------------------------------------------------
     Plugin registration */

  /**
      This function registers your plugin with a particular version
      of Traffic Server SDK. Use this function to make sure that the
      Traffic Server version currently running also supports your plugin.
      See the SDK sample code for usage.

      @param sdk_version earliest version of the Traffic Server SDK that
        supports your plugin.
      @param plugin_info contains registration information about your
        plugin. See INKPluginRegistrationInfo.
      @return 0 if the plugin registration failed.

   */
  inkapi int INKPluginRegister(INKSDKVersion sdk_version, INKPluginRegistrationInfo * plugin_info);
  inkapi INKReturnCode INKPluginInfoRegister(INKPluginRegistrationInfo * plugin_info);

  /* --------------------------------------------------------------------------
     Files */
  /**
      Opens a file for reading or writing and returns a descriptor for
      accessing the file. The current implementation cannot open a file
      for both reading or writing. See the SDK Programmer's Guide for
      sample code.

      @param filename file to be opened.
      @param mode specifies whether to open the file for reading or
        writing. If mode is "r" then the file is opened for reading.
        If mode is "w" then the file is opened for writing. Currently
        "r" and "w" are the only two valid modes for opening a file.
      @return descriptor for the file that INKfopen opens. Descriptors of
        type INKFile can be greater than 256.

   */
  inkapi INKFile INKfopen(const char *filename, const char *mode);

  /**
      Closes the file to which filep points and frees the data structures
      and buffers associated with it. If the file was opened for writing,
      any pending data is flushed.

      @param filep file to be closed.

   */
  inkapi void INKfclose(INKFile filep);

  /**
      Attempts to read length bytes of data from the file pointed to by
      filep into the buffer buf.

      @param filep name of the file to read from.
      @param buf buffer to read into.
      @param length amount of data to read, in bytes.
      @return number of bytes read. If end of the file, it returns 0.
        If the file was not opened for reading or if an error occurs
        while reading the file, it returns -1.

   */
  inkapi int INKfread(INKFile filep, void *buf, int length);

  /**
      Attempts to write length bytes of data from the buffer buf
      to the file filep. Make sure that filep is open for writing.
      You might want to check the number of bytes written (INKfwrite()
      returns this value) against the value of length. If it is less,
      there might be insufficient space on disk, for example.

      @param filep file to write into.
      @param buf buffer containing the data to be written.
      @param length amount of data to write to filep, in bytes.
      @return number of bytes written to filep. If the file was not
        opened for writing, it returns -1. If an error occurs while
        writing, it returns the number of bytes successfully written.

   */
  inkapi int INKfwrite(INKFile filep, const void *buf, int length);

  /**
      Flushes pending data that has been buffered up in memory from
      previous calls to INKfwrite().

      @param filep file to flush.

   */
  inkapi void INKfflush(INKFile filep);

  /**
      Reads a line from the file pointed to by filep into the buffer buf.
      Lines are terminated by a line feed character, '\n'. The line
      placed in the buffer includes the line feed character and is
      terminated with a NULL. If the line is longer than length bytes
      then only the first length-minus-1 bytes are placed in buf.

      @param filep file to read from.
      @param buf buffer to read into.
      @param length size of the buffer to read into.
      @return pointer to the string read into the buffer buf.

   */
  inkapi char *INKfgets(INKFile filep, char *buf, int length);

  /* --------------------------------------------------------------------------
     Error logging */
  /**
      Writes printf-style error messages to the Traffic Server error
      log. One advantage of INKError over printf is that each call is
      atomically placed into the error log and is not garbled with other
      error entries. This is not an issue in single-threaded programs
      but is a definite nuisance in multi-threaded programs.

      @param fmt printf format description.
      @param ... argument for the printf format description.

  */
  inkapi void INKError(const char *fmt, ...);

  /* --------------------------------------------------------------------------
     Assertions */
  inkapi int _INKReleaseAssert(const char *txt, const char *f, int l);
  inkapi int _INKAssert(const char *txt, const char *f, int l);

#define INKReleaseAssert(EX) \
            (void)((EX) || (_INKReleaseAssert(#EX, __FILE__, __LINE__)))

#define INKAssert(EX) \
            (void)((EX) || (_INKAssert(#EX, __FILE__, __LINE__)))

  /* --------------------------------------------------------------------------
     Marshal buffers */
  /**
      Creates a new marshal buffer and initializes the reference count
      to 1.

   */
  inkapi INKMBuffer INKMBufferCreate(void);

  /**
      Ignores the reference count and destroys the marshal buffer bufp.
      The internal data buffer associated with the marshal buffer is
      also destroyed if the marshal buffer allocated it.

      @param bufp marshal buffer to be destroyed.

   */
  inkapi INKReturnCode INKMBufferDestroy(INKMBuffer bufp);

  /* --------------------------------------------------------------------------
     URLs */
  /**
      Creates a new URL within the marshal buffer bufp. Returns a
      location for the URL within the marshal buffer.

      @param bufp marshal buffer containing the new URL.
      @return location of the created URL.

   */
  inkapi INKMLoc INKUrlCreate(INKMBuffer bufp);

  /**
      Destroys the URL located at url_loc within the marshal buffer
      bufp. Do not forget to release the INKMLoc url_loc with a call
      to INKHandleMLocRelease().

      @param bufp marshal buffer containing the URL to be destroyed.
      @param offset location of the URL to be destroyed.

   */
  inkapi INKReturnCode INKUrlDestroy(INKMBuffer bufp, INKMLoc offset);

  /**
      Copies the URL located at src_url within src_bufp to a URL
      location within the marshal buffer dest_bufp, and returns the
      INKMLoc location of the copied URL. Unlike INKUrlCopy(), you do
      not have to create the destination URL before cloning. Release
      the returned INKMLoc handle with a call to INKHandleMLocRelease().

      @param dest_bufp marshal buffer containing the cloned URL.
      @param src_bufp marshal buffer containing the URL to be cloned.
      @param src_url location of the URL to be cloned, within the marshal
        buffer src_bufp.
      @return location of the newly created URL.

   */
  inkapi INKMLoc INKUrlClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_url);

  /**
      Copies the contents of the URL at lcoation src_loc within the
      marshal buffer src_bufp to the location dest_loc within the marshal
      buffer dest_bufp. INKUrlCopy() works correctly even if src_bufp
      and dest_bufp point to different marshal buffers. Important: create
      the destination URL before copying into it. Use INKUrlCreate().

      @param dest_bufp marshal buffer to contain the copied URL.
      @param dest_offset location of the URL to be copied.
      @param src_bufp marshal buffer containing the source URL.
      @param src_offset location of the source URL within src_bufp.

   */
  inkapi INKReturnCode INKUrlCopy(INKMBuffer dest_bufp, INKMLoc dest_offset, INKMBuffer src_bufp, INKMLoc src_offset);

  /**
      Formats a URL stored in an INKMBuffer into an INKIOBuffer.

      @param bufp marshal buffer contain the URL to be printed.
      @param offset location of the URL within bufp.
      @param iobufp destination INKIOBuffer for the URL.

   */
  inkapi INKReturnCode INKUrlPrint(INKMBuffer bufp, INKMLoc offset, INKIOBuffer iobufp);

  /**
      Parses a URL. The start pointer is both an input and an output
      parameter and marks the start of the URL to be parsed. After
      a successful parse, the start pointer equals the end pointer.
      The end pointer must be one byte after the last character you
      want to parse. The URL parsing routine assumes that everything
      between start and end is part of the URL. It is up to higher level
      parsing routines, such as INKHttpHdrParseReq(), to determine the
      actual end of the URL. Returns INK_PARSE_ERROR if an error occurs,
      otherwise INK_PARSE_DONE is returned to indicate success.

      @param bufp marshal buffer containing the URL to be parsed.
      @param offset location of the URL to be parsed.
      @param start points to the start of the URL to be parsed AND at
        the end of a successful parse it will equal the end pointer.
      @param end must be one byte after the last character.
      @return INK_PARSE_ERROR or INK_PARSE_DONE.

   */
  inkapi int INKUrlParse(INKMBuffer bufp, INKMLoc offset, const char **start, const char *end);

  /**
      Calculates the length of the URL located at url_loc within the
      marshal buffer bufp if it were returned as a string. This length
      is the same as the length returned by INKUrlStringGet().

      @param bufp marshal buffer containing the URL whose length you want.
      @param offset location of the URL within the marshal buffer bufp.
      @return string length of the URL.

   */
  inkapi int INKUrlLengthGet(INKMBuffer bufp, INKMLoc offset);

  /**
      Constructs a string representation of the URL located at url_loc
      within bufp. INKUrlStringGet() stores the length of the allocated
      string in the parameter length. This is the same length that
      INKUrlLengthGet() returns. The returned string is allocated by a
      call to INKmalloc(). It should be freed by a call to INKfree().
      If length is NULL then no attempt is made to dereference it.

      @param bufp marshal buffer containing the URL you want to get.
      @param offset location of the URL within bufp.
      @param length string length of the URL.
      @return The URL as a string.

   */
  inkapi char *INKUrlStringGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Retrieves the scheme portion of the URL located at url_loc within
      the marshal buffer bufp. INKUrlSchemeGet() places the length of
      the string in the length argument. If the length is NULL then no
      attempt is made to dereference it.

      @param bufp marshal buffer storing the URL.
      @param offset location of the URL within bufp.
      @param length length of the returned string.
      @return The scheme portion of the URL, as a string.

   */
  inkapi const char *INKUrlSchemeGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the scheme portion of the URL located at url_loc within
      the marshal buffer bufp to the string value. If length is -1
      then INKUrlSchemeSet() assumes that value is null-terminated.
      Otherwise, the length of the string value is taken to be length.
      INKUrlSchemeSet() copies the string to within bufp, so it is OK
      to modify or delete value after calling INKUrlSchemeSet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param value value to set the URL's scheme to.
      @param length string stored in value.

   */
  inkapi INKReturnCode INKUrlSchemeSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /* --------------------------------------------------------------------------
     Internet specific URLs */
  /**
      Retrieves the user portion of the URL located at url_loc
      within bufp. Note: the returned string is not guaranteed to
      be null-terminated. Release the returned string with a call to
      INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length length of the returned string.
      @return user portion of the URL.

   */
  inkapi const char *INKUrlUserGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the user portion of the URL located at url_loc within bufp
      to the string value. If length is -1 then INKUrlUserSet() assumes
      that value is null-terminated. Otherwise, the length of the string
      value is taken to be length. INKUrlUserSet() copies the string to
      within bufp, so it is OK to modify or delete value after calling
      INKUrlUserSet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL whose user is to be set.
      @param value holds the new user name.
      @param length string length of value.

   */
  inkapi INKReturnCode INKUrlUserSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /**
      Retrieves the password portion of the URL located at url_loc
      within bufp. INKUrlPasswordGet() places the length of the returned
      string in the length argument. Note: the returned string is
      not guaranteed to be null-terminated. Release with a call to
      INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset
      @param length of the returned password string.
      @return password portion of the URL.

   */
  inkapi const char *INKUrlPasswordGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the password portion of the URL located at url_loc within
      bufp to the string value. If length is -1 then INKUrlPasswordSet()
      assumes that value is null-terminated. Otherwise, the length
      of value is taken to be length. INKUrlPasswordSet() copies the
      string to within bufp, so it is okay to modify or delete value
      after calling INKUrlPasswordSet().

      @param bufp marshal buffer containing the URL.
      @param offset
      @param value new password.
      @param length of the new password.

   */
  inkapi INKReturnCode INKUrlPasswordSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /**
      Retrieves the host portion of the URL located at url_loc
      within bufp. Note: the returned string is not guaranteed to be
      null-terminated. Release with a call to INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length of the returned string.
      @return Host portion of the URL.

   */
  inkapi const char *INKUrlHostGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the host portion of the URL at url_loc to the string value.
      If length is -1 then INKUrlHostSet() assumes that value is
      null-terminated. Otherwise, the length of the string value is
      taken to be length. The string is copied to within bufp, so you
      can modify or delete value after calling INKUrlHostSet().

      @param bufp marshal buffer containing the URL to modify.
      @param offset location of the URL.
      @param value new host name for the URL.
      @param length string length of the new host name of the URL.

   */
  inkapi INKReturnCode INKUrlHostSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /**
      Retrieves the port portion of the URL located at url_loc.

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @return port portion of the URL.

   */
  inkapi int INKUrlPortGet(INKMBuffer bufp, INKMLoc offset);

  /**
      Sets the port portion of the URL located at url_loc.

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param port new port setting for the URL.

   */
  inkapi INKReturnCode INKUrlPortSet(INKMBuffer bufp, INKMLoc offset, int port);

  /* --------------------------------------------------------------------------
     HTTP specific URLs */
  /**
      Retrieves the path portion of the URL located at url_loc within
      bufp. INKUrlPathGet() places the length of the returned string in
      the length argument. Note: the returned string is not guaranteed to
      be null-terminated. Release with a call to INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length of the returned string.
      @return path portion of the URL.

   */
  inkapi const char *INKUrlPathGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the path portion of the URL located at url_loc within bufp
      to the string value. If length is -1 then INKUrlPathSet() assumes
      that value is null-terminated. Otherwise, the length of the value
      is taken to be length. INKUrlPathSet() copies the string into bufp,
      so you can modify or delete value after calling INKUrlPathSet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param value new path string for the URL.
      @param length of the new path string.

   */
  inkapi INKReturnCode INKUrlPathSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /* --------------------------------------------------------------------------
     FTP specific URLs */
  /**
      Retrieves the FTP type of the URL located at url_loc within bufp.

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @return FTP type of the URL.

   */
  inkapi int INKUrlFtpTypeGet(INKMBuffer bufp, INKMLoc offset);

  /**
      Sets the FTP type portion of the URL located at url_loc within
      bufp to the value type.

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL to modify.
      @param type new FTP type for the URL.

   */
  inkapi INKReturnCode INKUrlFtpTypeSet(INKMBuffer bufp, INKMLoc offset, int type);

  /* --------------------------------------------------------------------------
     HTTP specific URLs */
  /**
      Retrieves the HTTP params portion of the URL located at url_loc
      within bufp. The length of the returned string is in the length
      argument. Note: the returned string is not guaranteed to be
      null-terminated. Release with a call to INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length of the returned string.
      @return HTTP params portion of the URL.

   */
  inkapi const char *INKUrlHttpParamsGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the HTTP params portion of the URL located at url_loc within
      bufp to the string value. If length is -1 that INKUrlHttpParamsSet()
      assumes that value is null-terminated. Otherwise, the length of
      the string value is taken to be length. INKUrlHttpParamsSet()
      copies the string to within bufp, so you can modify or delete
      value after calling INKUrlHttpParamsSet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param value HTTP params string to set in the URL.
      @param length string length of the new HTTP params value.

   */
  inkapi INKReturnCode INKUrlHttpParamsSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /**
      Retrieves the HTTP query portion of the URL located at url_loc
      within bufp. The length of the returned string is in the length
      argument. Note: the returned string is not guaranteed to be
      null-terminated. Release with a call to INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length of the returned string.
      @return HTTP query portion of the URL.

   */
  inkapi const char *INKUrlHttpQueryGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the HTTP query portion of the URL located at url_loc within
      bufp to value. If length is -1, the string value is assumed to
      be null-terminated; otherwise, the length of value is taken to be
      length. INKUrlHttpQuerySet() copies the string to within bufp, so
      you can modify or delete value after calling INKUrlHttpQuerySet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL within bufp.
      @param value new HTTP query string for the URL.
      @param length of the new HTTP query string.

   */
  inkapi INKReturnCode INKUrlHttpQuerySet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /**
      Retrieves the HTTP fragment portion of the URL located at url_loc
      within bufp. The length of the returned string is in the length
      argument. Note: the returned string is not guaranteed to be
      null-terminated. Release with a call to INKHandleStringRelease().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL.
      @param length of the returned string.
      @return HTTP fragment portion of the URL.

   */
  inkapi const char *INKUrlHttpFragmentGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /**
      Sets the HTTP fragment portion of the URL located at url_loc
      within bufp to value. If length is -1, the string value is
      assumed to be null-terminated; otherwise, the length of value
      is taken to be length. INKUrlHttpFragmentSet() copies the string
      to within bufp, so you can modify or delete value after calling
      INKUrlHttpFragmentSet().

      @param bufp marshal buffer containing the URL.
      @param offset location of the URL within bufp.
      @param value new HTTP fragment string for the URL.
      @param length of the new HTTP query string.

   */
  inkapi INKReturnCode INKUrlHttpFragmentSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);

  /* --------------------------------------------------------------------------
     MIME headers */

  /**
      Creates a MIME parser. The parser's data structure contains
      information about the header being parsed. A single MIME
      parser can be used multiple times, though not simultaneously.
      Before being used again, the parser must be cleared by calling
      INKMimeParserClear().

   */
  inkapi INKMimeParser INKMimeParserCreate(void);

  /**
      Clears the specified MIME parser so that it can be used again.

      @param parser to be cleared.

   */
  inkapi INKReturnCode INKMimeParserClear(INKMimeParser parser);

  /**
      Destroys the specified MIME parser and frees the associated memory.

      @param parser to destroy.
   */
  inkapi INKReturnCode INKMimeParserDestroy(INKMimeParser parser);

  /**
      Creates a new MIME header within bufp. Release with a call to
      INKHandleMLocRelease().

      @param bufp marshal buffer to contain the new MIME header.
      @return location of the new MIME header within bufp.

   */
  inkapi INKMLoc INKMimeHdrCreate(INKMBuffer bufp);

  /**
      Destroys the MIME header located at hdr_loc within bufp.

      @param bufp marshal buffer containing the MIME header to destroy.
      @param offset location of the MIME header.

   */
  inkapi INKReturnCode INKMimeHdrDestroy(INKMBuffer bufp, INKMLoc offset);

  /**
      Copies a specified MIME header to a specified marshal buffer,
      and returns the location of the copied MIME header within the
      destination marshal buffer. Unlike INKMimeHdrCopy(), you do not
      have to create the destination MIME header before cloning. Release
      the returned INKMLoc handle with a call to INKHandleMLocRelease().

      @param dest_bufp destination marshal buffer.
      @param src_bufp source marshal buffer.
      @param src_hdr location of the source MIME header.
      @return location of the copied MIME header.

   */
  inkapi INKMLoc INKMimeHdrClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_hdr);

  /**
      Copies the contents of the MIME header located at src_loc
      within src_bufp to the MIME header located at dest_loc within
      dest_bufp. INKMimeHdrCopy() works correctly even if src_bufp and
      dest_bufp point to different marshal buffers. Important: you must
      create the destination MIME header before copying into it--use
      INKMimeHdrCreate().

      @param dest_bufp is the destination marshal buffer.
      @param dest_offset
      @param src_bufp is the source marshal buffer.
      @param src_offset

   */
  inkapi INKReturnCode INKMimeHdrCopy(INKMBuffer dest_bufp, INKMLoc dest_offset, INKMBuffer src_bufp,
                                      INKMLoc src_offset);

  /**
      Formats the MIME header located at hdr_loc within bufp into the
      INKIOBuffer iobufp.

      @param bufp marshal buffer containing the header to be copied to
        an INKIOBuffer.
      @param offset
      @param iobufp target INKIOBuffer.

   */
  inkapi INKReturnCode INKMimeHdrPrint(INKMBuffer bufp, INKMLoc offset, INKIOBuffer iobufp);

  /**
      Parses a MIME header. The MIME header must have already been
      allocated and both bufp and hdr_loc must point within that header.
      It is possible to parse a MIME header a single byte at a time
      using repeated calls to INKMimeHdrParse(). As long as an error
      does not occur, INKMimeHdrParse() consumes each single byte and
      asks for more.

      @param parser parses the specified MIME header.
      @param bufp marshal buffer containing the MIME header to be parsed.
      @param offset
      @param start both an input and output. On input, the start
        argument points to the current position of the buffer being
        parsed. On return, start is modified to point past the last
        character parsed.
      @param end points to one byte after the end of the buffer.
      @return One of 3 possible int values:
        - INK_PARSE_ERROR if there is a parsing error.
        - INK_PARSE_DONE is returned when a "\r\n\r\n" pattern is
          encountered, indicating the end of the header.
        - INK_PARSE_CONT is returned if parsing of the header stopped
          because the end of the buffer was reached.

   */
  inkapi int INKMimeHdrParse(INKMimeParser parser, INKMBuffer bufp, INKMLoc offset, const char **start,
                             const char *end);

  /**
      Calculates the length of the MIME header located at hdr_loc if it
      were returned as a string. This the length of the MIME header in
      its unparsed form.

      @param bufp marshal buffer containing the MIME header.
      @param offset location of the MIME header.
      @return string length of the MIME header located at hdr_loc.

   */
  inkapi int INKMimeHdrLengthGet(INKMBuffer bufp, INKMLoc offset);

  /**
      Removes and destroys all the MIME fields within the MIME header
      located at hdr_loc within the marshal buffer bufp. Important:
      do not forget to release any corresponding MIME field string
      values or INKMLoc handles using INKHandleStringRelease() or
      INKHandleMLocRelease().

      @param bufp marshal buffer containing the MIME header.
      @param offset location of the MIME header.

   */
  inkapi INKReturnCode INKMimeHdrFieldsClear(INKMBuffer bufp, INKMLoc offset);

  /**
      Returns a count of the number of MIME fields within the MIME header
      located at hdr_loc within the marshal buffer bufp.

      @param bufp marshal buffer containing the MIME header.
      @param offset location of the MIME header within bufp.
      @return number of MIME fields within the MIME header located
        at hdr_loc.

   */
  inkapi int INKMimeHdrFieldsCount(INKMBuffer bufp, INKMLoc offset);

  /**
      Retrieves the location of a specified MIME field within the
      MIME header located at hdr_loc within bufp. The idx parameter
      specifies which field to retrieve. The fields are numbered from 0
      to INKMimeHdrFieldsCount(bufp, hdr_loc) - 1. If idx does not lie
      within that range then INKMimeHdrFieldGet returns 0. Release the
      returned handle with a call to INKHandleMLocRelease.

      @param bufp marshal buffer containing the MIME header.
      @param hdr location of the MIME header.
      @param idx index of the field to get with base at 0.
      @return location of the specified MIME field.

   */
  inkapi INKMLoc INKMimeHdrFieldGet(INKMBuffer bufp, INKMLoc hdr, int idx);

  /**
      Retrieves the INKMLoc location of a specfied MIME field from within
      the MIME header located at hdr. The name and length parameters
      specify which field to retrieve. For each MIME field in the MIME
      header, a case insensitive string comparison is done between
      the field name and name. If INKMimeHdrFieldFind() cannot find the
      requested field, it returns 0. Note that the string comparison done
      by INKMimeHdrFieldFind() is slower than the pointer comparison
      done by INKMimeHdrFieldRetrieve(). Release the returned INKMLoc
      handle with a call to INKHandleMLocRelease().

      @param bufp marshal buffer containing the MIME header field to find.
      @param hdr location of the MIME header containing the field.
      @param name of the field to retrieve.
      @param length string length of the string name. If length is -1,
        then name is assumed to be null-terminated.
      @return location of the requested MIME field. If the field could
        not be found, returns 0.

   */
  inkapi INKMLoc INKMimeHdrFieldFind(INKMBuffer bufp, INKMLoc hdr, const char *name, int length);

  /**
      Returns the INKMLoc location of a specified MIME field from within
      the MIME header located at hdr. The retrieved_str parameter
      specifies which field to retrieve. For each MIME field in the
      MIME header, a pointer comparison is done between the field name
      and retrieved_str. This is a much quicker retrieval function
      than INKMimeHdrFieldFind() since it obviates the need for a
      string comparison. However, retrieved_str must be one of the
      predefined field names of the form INK_MIME_FIELD_XXX for the
      call to succeed. Release the returned INKMLoc handle with a call
      to INKHandleMLocRelease().

      @param bufp marshal buffer containing the MIME field.
      @param hdr location of the MIME header containing the field.
      @param retrieved_str specifies the field to retrieve. Must be
        one of the predefined field names of the form INK_MIME_FIELD_XXX.
      @return location of the requested MIME field. If the requested
        field cannot be found, returns 0.

   */
  inkapi INKMLoc INKMimeHdrFieldRetrieve(INKMBuffer bufp, INKMLoc hdr, const char *retrieved_str);

  inkapi INKReturnCode INKMimeHdrFieldAppend(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);

  /**
      Removes the MIME field located at field within bufp from the
      header located at hdr within bufp. If the specified field cannot
      be found in the list of fields associated with the header then
      nothing is done.

      Note: removing the field does not destroy the field, it only
      detaches the field, hiding it from the printed output. The field
      can be reattached with a call to INKMimeHdrFieldInsert(). If you
      do not use the detached field you should destroy it with a call to
      INKMimeHdrFieldDestroy() and release the handle field with a call
      to INKHandleMLocRelease(). The INKMimeHdrFieldDelete() function
      does both a remove and a delete, if that is what you want to do.

      @param bufp contains the MIME field to remove.
      @param hdr location of the header containing the MIME field to
        be removed. This header could be an HTTP header or MIME header.
      @param field is the location of the field to remove.

   */
  inkapi INKReturnCode INKMimeHdrFieldRemove(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);

  inkapi INKMLoc INKMimeHdrFieldCreate(INKMBuffer bufp, INKMLoc hdr);

  /**
      Destroys the MIME field located at field within bufp. You must
      release the INKMLoc field with a call to INKHandleMLocRelease().

      @param bufp contains the MIME field to be destroyed.
      @param hdr location of the parent header containing the field
        to be destroyed. This could be the location of a MIME header or
        HTTP header.
      @param field location of the field to be destroyed.

   */
  inkapi INKReturnCode INKMimeHdrFieldDestroy(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);

  inkapi INKMLoc INKMimeHdrFieldClone(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMBuffer src_bufp, INKMLoc src_hdr,
                                      INKMLoc src_field);
  inkapi INKReturnCode INKMimeHdrFieldCopy(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMLoc dest_field,
                                           INKMBuffer src_bufp, INKMLoc src_hdr, INKMLoc src_field);
  inkapi INKReturnCode INKMimeHdrFieldCopyValues(INKMBuffer dest_bufp, INKMLoc dest_hdr, INKMLoc dest_field,
                                                 INKMBuffer src_bufp, INKMLoc src_hdr, INKMLoc src_field);
  inkapi INKMLoc INKMimeHdrFieldNext(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);
  inkapi INKMLoc INKMimeHdrFieldNextDup(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);
  inkapi int INKMimeHdrFieldLengthGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);
  inkapi const char *INKMimeHdrFieldNameGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int *length);
  inkapi INKReturnCode INKMimeHdrFieldNameSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *name,
                                              int length);

  inkapi INKReturnCode INKMimeHdrFieldValuesClear(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);
  inkapi int INKMimeHdrFieldValuesCount(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);

  inkapi INKReturnCode INKMimeHdrFieldValueStringGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                     const char **value, int *value_len_ptr);
  inkapi INKReturnCode INKMimeHdrFieldValueIntGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int *value);
  inkapi INKReturnCode INKMimeHdrFieldValueUintGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                   unsigned int *value);
  inkapi INKReturnCode INKMimeHdrFieldValueDateGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t * value);
  inkapi INKReturnCode INKMimeHdrFieldValueStringSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                     const char *value, int length);
  inkapi INKReturnCode INKMimeHdrFieldValueIntSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value);
  inkapi INKReturnCode INKMimeHdrFieldValueUintSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                   unsigned int value);
  inkapi INKReturnCode INKMimeHdrFieldValueDateSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value);

  inkapi INKReturnCode INKMimeHdrFieldValueAppend(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                  const char *value, int length);
  inkapi INKReturnCode INKMimeHdrFieldValueStringInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                        const char *value, int length);
  inkapi INKReturnCode INKMimeHdrFieldValueIntInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value);
  inkapi INKReturnCode INKMimeHdrFieldValueUintInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx,
                                                      unsigned int value);
  inkapi INKReturnCode INKMimeHdrFieldValueDateInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value);

  inkapi INKReturnCode INKMimeHdrFieldValueDelete(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx);

  /* --------------------------------------------------------------------------
     HTTP headers */
  inkapi INKHttpParser INKHttpParserCreate(void);
  inkapi INKReturnCode INKHttpParserClear(INKHttpParser parser);
  inkapi INKReturnCode INKHttpParserDestroy(INKHttpParser parser);

  inkapi INKMLoc INKHttpHdrCreate(INKMBuffer bufp);

  /**
      Destroys the HTTP header located at hdr_loc within the marshal
      buffer bufp. Do not forget to release the handle hdr_loc with a
      call to INKHandleMLocRelease().

   */
  inkapi INKReturnCode INKHttpHdrDestroy(INKMBuffer bufp, INKMLoc offset);

  inkapi INKMLoc INKHttpHdrClone(INKMBuffer dest_bufp, INKMBuffer src_bufp, INKMLoc src_hdr);

  /**
      Copies the contents of the HTTP header located at src_loc within
      src_bufp to the HTTP header located at dest_loc within dest_bufp.
      INKHttpHdrCopy() works correctly even if src_bufp and dest_bufp
      point to different marshal buffers. Make sure that you create the
      destination HTTP header before copying into it.

      Note: INKHttpHdrCopy() appends the port number to the domain
      of the URL portion of the header. For example, a copy of
      http://www.example.com appears as http://www.example.com:80 in
      the destination buffer.

      @param dest_bufp marshal buffer to contain the copied header.
      @param dest_offset location of the copied header.
      @param src_bufp marshal buffer containing the source header.
      @param src_offset location of the source header.

   */
  inkapi INKReturnCode INKHttpHdrCopy(INKMBuffer dest_bufp, INKMLoc dest_offset, INKMBuffer src_bufp,
                                      INKMLoc src_offset);

  inkapi INKReturnCode INKHttpHdrPrint(INKMBuffer bufp, INKMLoc offset, INKIOBuffer iobufp);

  /**
      Parses an HTTP request header. The HTTP header must have already
      been created, and must reside inside the marshal buffer bufp.
      The start argument points to the current position of the string
      buffer being parsed. The end argument points to one byte after the
      end of the buffer to be parsed. On return, INKHttpHdrParseReq()
      modifies start to point past the last character parsed.

      It is possible to parse an HTTP request header a single byte at
      a time using repeated calls to INKHttpHdrParseReq(). As long as
      an error does not occur, the INKHttpHdrParseReq() function will
      consume that single byte and ask for more.

      @param parser parses the HTTP header.
      @param bufp marshal buffer containing the HTTP header to be parsed.
      @param offset location of the HTTP header within bufp.
      @param start both an input and output. On input, it points to the
        current position of the string buffer being parsed. On return,
        start is modified to point past the last character parsed.
      @param end points to one byte after the end of the buffer to be parsed.
      @return status of the parse:
        - INK_PARSE_ERROR means there was a parsing error.
        - INK_PARSE_DONE means that the end of the header was reached
          (the parser encountered a "\r\n\r\n" pattern).
        - INK_PARSE_CONT means that parsing of the header stopped because
          the parser reached the end of the buffer (large headers can
          span multiple buffers).

   */
  inkapi int INKHttpHdrParseReq(INKHttpParser parser, INKMBuffer bufp, INKMLoc offset, const char **start,
                                const char *end);

  inkapi int INKHttpHdrParseResp(INKHttpParser parser, INKMBuffer bufp, INKMLoc offset, const char **start,
                                 const char *end);

  inkapi int INKHttpHdrLengthGet(INKMBuffer bufp, INKMLoc offset);

  inkapi INKHttpType INKHttpHdrTypeGet(INKMBuffer bufp, INKMLoc offset);
  inkapi INKReturnCode INKHttpHdrTypeSet(INKMBuffer bufp, INKMLoc offset, INKHttpType type);

  inkapi int INKHttpHdrVersionGet(INKMBuffer bufp, INKMLoc offset);
  inkapi INKReturnCode INKHttpHdrVersionSet(INKMBuffer bufp, INKMLoc offset, int ver);

  inkapi const char *INKHttpHdrMethodGet(INKMBuffer bufp, INKMLoc offset, int *length);
  inkapi INKReturnCode INKHttpHdrMethodSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);
  inkapi INKMLoc INKHttpHdrUrlGet(INKMBuffer bufp, INKMLoc offset);
  inkapi INKReturnCode INKHttpHdrUrlSet(INKMBuffer bufp, INKMLoc offset, INKMLoc url);

  inkapi INKHttpStatus INKHttpHdrStatusGet(INKMBuffer bufp, INKMLoc offset);
  inkapi INKReturnCode INKHttpHdrStatusSet(INKMBuffer bufp, INKMLoc offset, INKHttpStatus status);
  inkapi const char *INKHttpHdrReasonGet(INKMBuffer bufp, INKMLoc offset, int *length);
  inkapi INKReturnCode INKHttpHdrReasonSet(INKMBuffer bufp, INKMLoc offset, const char *value, int length);
  inkapi const char *INKHttpHdrReasonLookup(INKHttpStatus status);

  /* --------------------------------------------------------------------------
     Threads */
  inkapi INKThread INKThreadCreate(INKThreadFunc func, void *data);
  inkapi INKThread INKThreadInit(void);
  inkapi INKReturnCode INKThreadDestroy(INKThread thread);
  inkapi INKThread INKThreadSelf(void);

  /* --------------------------------------------------------------------------
     Mutexes */
  inkapi INKMutex INKMutexCreate(void);
  inkapi INKReturnCode INKMutexLock(INKMutex mutexp);
  inkapi INKReturnCode INKMutexLockTry(INKMutex mutexp, int *lock);

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMutexTryLock(INKMutex mutexp);

  inkapi INKReturnCode INKMutexUnlock(INKMutex mutexp);

  /* --------------------------------------------------------------------------
     cachekey */
  /**
      Creates (allocates memory for) a new cache key.

      @param new_key represents an object to be cached.

   */
  inkapi INKReturnCode INKCacheKeyCreate(INKCacheKey * new_key);

  /**
      Generates a key for an object to be cached (written to the cache).

      @param key to be associated with the cached object. Before
        calling INKCacheKeySetDigest() you must create the key with
        INKCacheKeyCreate().
      @param input string that uniquely identifies the object. In most
        cases, it is the URL of the object.
      @param length of the string input.

   */
  inkapi INKReturnCode INKCacheKeyDigestSet(INKCacheKey key, const unsigned char *input, int length);

  inkapi INKReturnCode INKCacheKeyDigestFromUrlSet(INKCacheKey key, INKMLoc url);

  /**
      Associates a host name to the cache key. Use this function if the
      cache has been partitioned by hostname. The hostname tells the
      cache which partition to use for the object.

      @param key of the cached object.
      @param hostname to associate with the cache key.
      @param host_len length of the string hostname.

   */
  inkapi INKReturnCode INKCacheKeyHostNameSet(INKCacheKey key, const unsigned char *hostname, int host_len);

  inkapi INKReturnCode INKCacheKeyPinnedSet(INKCacheKey key, time_t pin_in_cache);

  /**
      Destroys a cache key. You must destroy cache keys when you are
      finished with them, i.e. after all reads and writes are completed.

      @param key to be destroyed.

   */
  inkapi INKReturnCode INKCacheKeyDestroy(INKCacheKey key);

  /* --------------------------------------------------------------------------
     cache url */
  inkapi INKReturnCode INKSetCacheUrl(INKHttpTxn txnp, const char *url);

  /* --------------------------------------------------------------------------
     cache plugin */
  inkapi INKReturnCode INKCacheKeyGet(INKCacheTxn txnp, void **key, int *length);
  inkapi INKReturnCode INKCacheHeaderKeyGet(INKCacheTxn txnp, void **key, int *length);
  inkapi INKIOBufferReader INKCacheBufferReaderGet(INKCacheTxn txnp);
  inkapi INKHttpTxn INKCacheGetStateMachine(INKCacheTxn txnp);

  /* --------------------------------------------------------------------------
     Configuration */
  inkapi unsigned int INKConfigSet(unsigned int id, void *data, INKConfigDestroyFunc funcp);
  inkapi INKConfig INKConfigGet(unsigned int id);
  inkapi void INKConfigRelease(unsigned int id, INKConfig configp);
  inkapi void *INKConfigDataGet(INKConfig configp);

  /* --------------------------------------------------------------------------
     Management */
  inkapi INKReturnCode INKMgmtUpdateRegister(INKCont contp, const char *plugin_name, const char *path);
  inkapi int INKMgmtIntGet(const char *var_name, INKMgmtInt * result);
  inkapi int INKMgmtCounterGet(const char *var_name, INKMgmtCounter * result);
  inkapi int INKMgmtFloatGet(const char *var_name, INKMgmtFloat * result);
  inkapi int INKMgmtStringGet(const char *var_name, INKMgmtString * result);

  /* --------------------------------------------------------------------------
     Continuations */
  inkapi INKCont INKContCreate(INKEventFunc funcp, INKMutex mutexp);
  inkapi INKReturnCode INKContDestroy(INKCont contp);
  inkapi INKReturnCode INKContDataSet(INKCont contp, void *data);
  inkapi void *INKContDataGet(INKCont contp);
  inkapi INKAction INKContSchedule(INKCont contp, unsigned int timeout);
  inkapi INKAction INKHttpSchedule(INKCont contp, INKHttpTxn txnp, unsigned int timeout);
  inkapi int INKContCall(INKCont contp, INKEvent event, void *edata);
  inkapi INKMutex INKContMutexGet(INKCont contp);

  /* --------------------------------------------------------------------------
     HTTP hooks */
  inkapi INKReturnCode INKHttpHookAdd(INKHttpHookID id, INKCont contp);

  /* --------------------------------------------------------------------------
     Cache hook */
  inkapi INKReturnCode INKCacheHookAdd(INKCacheHookID id, INKCont contp);

  /* --------------------------------------------------------------------------
     HTTP sessions */
  inkapi INKReturnCode INKHttpSsnHookAdd(INKHttpSsn ssnp, INKHttpHookID id, INKCont contp);
  inkapi INKReturnCode INKHttpSsnReenable(INKHttpSsn ssnp, INKEvent event);

  /* --------------------------------------------------------------------------
     HTTP transactions */
  inkapi INKReturnCode INKHttpTxnHookAdd(INKHttpTxn txnp, INKHttpHookID id, INKCont contp);
  inkapi INKHttpSsn INKHttpTxnSsnGet(INKHttpTxn txnp);
  inkapi int INKHttpTxnClientReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi INKReturnCode INKHttpTxnPristineUrlGet(INKHttpTxn txnp, INKMBuffer *bufp, INKMLoc *url_loc);
  inkapi int INKHttpTxnClientRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi int INKHttpTxnServerReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi int INKHttpTxnServerRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi int INKHttpTxnCachedReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi int INKHttpTxnCachedRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi void INKHttpTxnSetRespCacheableSet(INKHttpTxn txnp);
  inkapi void INKHttpTxnSetReqCacheableSet(INKHttpTxn txnp);
  inkapi int INKFetchPageRespGet (INKHttpTxn txnp, INKMBuffer *bufp, INKMLoc *offset);
  inkapi char* INKFetchRespGet (INKHttpTxn txnp, int *length);
  inkapi INKReturnCode INKHttpTxnCacheLookupStatusGet(INKHttpTxn txnp, int *lookup_status);

  inkapi int INKHttpTxnTransformRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
  inkapi unsigned int INKHttpTxnClientIPGet(INKHttpTxn txnp);
  inkapi int INKHttpTxnClientFdGet(INKHttpTxn txnp);
  inkapi INKReturnCode INKHttpTxnClientRemotePortGet(INKHttpTxn txnp, int *port);
  inkapi int INKHttpTxnClientIncomingPortGet(INKHttpTxn txnp);
  inkapi unsigned int INKHttpTxnServerIPGet(INKHttpTxn txnp);
  inkapi unsigned int INKHttpTxnNextHopIPGet(INKHttpTxn txnp);
  inkapi INKReturnCode INKHttpTxnErrorBodySet(INKHttpTxn txnp, char *buf, int buflength, char *mimetype);

  /**
      Retrieves the parent proxy hostname and port, if parent
      proxying is enabled. If parent proxying is not enabled,
      INKHttpTxnParentProxyGet() sets hostname to NULL and port to -1.

      @param txnp HTTP transaction whose parent proxy to get.
      @param hostname of the parent proxy.
      @param port parent proxy's port.

   */
  inkapi INKReturnCode INKHttpTxnParentProxyGet(INKHttpTxn txnp, char **hostname, int *port);

  /**
      Sets the parent proxy name and port. The string hostname is copied
      into the INKHttpTxn; you can modify or delete the string after
      calling INKHttpTxnParentProxySet().

      @param txnp HTTP transaction whose parent proxy to set.
      @param hostname parent proxy host name string.
      @param port parent proxy port to set.

   */
  inkapi INKReturnCode INKHttpTxnParentProxySet(INKHttpTxn txnp, char *hostname, int port);

  inkapi INKReturnCode INKHttpTxnUntransformedRespCache(INKHttpTxn txnp, int on);
  inkapi INKReturnCode INKHttpTxnTransformedRespCache(INKHttpTxn txnp, int on);

  /**
      Notifies the HTTP transaction txnp that the plugin is
      finished processing the current hook. The plugin tells the
      transaction to either continue (INK_EVENT_HTTP_CONTINUE) or stop
      (INK_EVENT_HTTP_ERROR).

      You must always reenable the HTTP transaction after the processing
      of each transaction event. However, never reenable twice.
      Reenabling twice is a serious error.

      @param txnp transaction to be reenabled.
      @param event tells the transaction how to continue:
        - INK_EVENT_HTTP_CONTINUE, which means that the transaction
          should continue.
        - INK_EVENT_HTTP_ERROR which terminates the transaction
          and sends an error to the client if no response has already
          been sent.

   */
  inkapi INKReturnCode INKHttpTxnReenable(INKHttpTxn txnp, INKEvent event);
  inkapi INKReturnCode INKHttpCacheReenable(INKCacheTxn txnp, const INKEvent event, const void *data,
                                            const INKU64 size);
  inkapi INKReturnCode INKHttpTxnFollowRedirect(INKHttpTxn txnp, int on);
  inkapi int INKHttpTxnGetMaxArgCnt(void);
  inkapi INKReturnCode INKHttpTxnSetArg(INKHttpTxn txnp, int arg_idx, void *arg);
  inkapi INKReturnCode INKHttpTxnGetArg(INKHttpTxn txnp, int arg_idx, void **arg);

  inkapi int INKHttpTxnGetMaxHttpRetBodySize(void);
  inkapi INKReturnCode INKHttpTxnSetHttpRetBody(INKHttpTxn txnp, const char *body_msg, int plain_msg);
  inkapi INKReturnCode INKHttpTxnSetHttpRetStatus(INKHttpTxn txnp, INKHttpStatus http_retstatus);
  inkapi int INKHttpTxnActiveTimeoutSet(INKHttpTxn txnp, int timeout);
  inkapi int INKHttpTxnConnectTimeoutSet(INKHttpTxn txnp, int timeout);
  inkapi int INKHttpTxnNoActivityTimeoutSet(INKHttpTxn txnp, int timeout);
  inkapi int INKHttpTxnDNSTimeoutSet(INKHttpTxn txnp, int timeout);

  inkapi INKServerState INKHttpTxnServerStateGet(INKHttpTxn txnp);

  /* --------------------------------------------------------------------------
     Intercepting Http Transactions */

  /**
      Allows a plugin take over the servicing of the request as though
      it was the origin server. contp will be sent INK_EVENT_NET_ACCEPT.
      The edata passed with INK_NET_EVENT_ACCEPT is an INKVConn just as
      it would be for a normal accept. The plugin must act as if it is
      an http server and read the http request and body off the INKVConn
      and send an http response header and body.

      INKHttpTxnIntercept() must be called be called from only
      INK_HTTP_READ_REQUEST_HOOK. Using INKHttpTxnIntercept will
      bypass the Traffic Server cache. If response sent by the plugin
      should be cached, use INKHttpTxnServerIntercept() instead.
      INKHttpTxnIntercept() primary use is allow plugins to serve data
      about their functioning directly.

      INKHttpTxnIntercept() must only be called once per transaction.

      @param contp continuation called to handle the interception.
      @param txnp transaction to be intercepted.
      @return INK_SUCCESS on success, INK_ERROR on failure.

   */
  inkapi INKReturnCode INKHttpTxnIntercept(INKCont contp, INKHttpTxn txnp);

  /**
      Allows a plugin take over the servicing of the request as though
      it was the origin server. In the event a request needs to be
      made to the server for transaction txnp, contp will be sent
      INK_EVENT_NET_ACCEPT. The edata passed with INK_NET_EVENT_ACCEPT
      is an INKVConn just as it would be for a normal accept. The plugin
      must act as if it is an http server and read the http request and
      body off the INKVConn and send an http response header and body.

      INKHttpTxnInterceptServer() must be not be called after
      the connection to the server has taken place. The last hook
      last hook in that INKHttpTxnIntercept() can be called from is
      INK_HTTP_READ_CACHE_HDR_HOOK. If a connection to the server is
      not necessary, contp is not called.

      The reponse from the plugin is cached subject to standard
      and configured http caching rules. Should the plugin wish the
      response not be cached, the plugin must use appropriate http
      response headers to prevent caching. The primary purpose of
      INKHttpTxnInterceptServer() is allow plugins to provide gateways
      to other protocols or to allow to plugin to it's own transport for
      the next hop to the server. INKHttpTxnInterceptServer() overrides
      parent cache configuration.

      INKHttpTxnInterceptServer() must only be called once per
      transaction.

      @param contp continuation called to handle the interception
      @param txnp transaction to be intercepted.
      @return INK_SUCCESS on success, INK_ERROR on failure.

   */
  inkapi INKReturnCode INKHttpTxnServerIntercept(INKCont contp, INKHttpTxn txnp);

  /* --------------------------------------------------------------------------
     Initiate Http Connection */
  /**
      Allows the plugin to initiate an http connection. The INKVConn the
      plugin receives as the result of successful operates identically to
      one created through INKNetConnect. Aside from allowing the plugin
      to set the client ip and port for logging, the functionality of
      INKHttpConnect() is identical to connecting to localhost on the
      proxy port with INKNetConnect(). INKHttpConnect() is more efficient
      than INKNetConnect() to localhost since it avoids the overhead of
      passing the data through the operating system.

      @param log_ip ip address (in network byte order) that connection
        will be logged as coming from.
      @param log_port port (in network byte order) that connection will
        be logged as coming from.
      @param vc will be set to point to the new INKVConn on success.
      @return INK_SUCCESS on success, INK_ERROR on failure.

   */
  inkapi INKReturnCode INKHttpConnect(unsigned int log_ip, int log_port, INKVConn * vc);
  inkapi INKReturnCode INKFetchUrl(const char *request,int request_len, unsigned int ip, int port , INKCont contp, INKFetchWakeUpOptions callback_options,INKFetchEvent event);
  inkapi INKReturnCode INKFetchPages(INKFetchUrlParams_t *params);

  /* Check if HTTP State machine is internal or not */
  inkapi int INKHttpIsInternalRequest(INKHttpTxn txnp);

  /* --------------------------------------------------------------------------
     HTTP alternate selection */
  inkapi INKReturnCode INKHttpAltInfoClientReqGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * offset);
  inkapi INKReturnCode INKHttpAltInfoCachedReqGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * offset);
  inkapi INKReturnCode INKHttpAltInfoCachedRespGet(INKHttpAltInfo infop, INKMBuffer * bufp, INKMLoc * offset);
  inkapi INKReturnCode INKHttpAltInfoQualitySet(INKHttpAltInfo infop, float quality);

  /* --------------------------------------------------------------------------
     Actions */
  inkapi INKReturnCode INKActionCancel(INKAction actionp);
  inkapi int INKActionDone(INKAction actionp);

  /* --------------------------------------------------------------------------
     VConnections */
  inkapi INKVIO INKVConnReadVIOGet(INKVConn connp);
  inkapi INKVIO INKVConnWriteVIOGet(INKVConn connp);
  inkapi int INKVConnClosedGet(INKVConn connp);

  inkapi INKVIO INKVConnRead(INKVConn connp, INKCont contp, INKIOBuffer bufp, int nbytes);
  inkapi INKVIO INKVConnWrite(INKVConn connp, INKCont contp, INKIOBufferReader readerp, int nbytes);
  inkapi INKReturnCode INKVConnClose(INKVConn connp);
  inkapi INKReturnCode INKVConnAbort(INKVConn connp, int error);
  inkapi INKReturnCode INKVConnShutdown(INKVConn connp, int read, int write);

  /* --------------------------------------------------------------------------
     Cache VConnections */
  inkapi INKReturnCode INKVConnCacheObjectSizeGet(INKVConn connp, int *obj_size);

  /* --------------------------------------------------------------------------
     Transformations */
  inkapi INKVConn INKTransformCreate(INKEventFunc event_funcp, INKHttpTxn txnp);
  inkapi INKVConn INKTransformOutputVConnGet(INKVConn connp);

  /* --------------------------------------------------------------------------
     Net VConnections */
  /**
      Returns the IP address of the remote host with which Traffic Server
      is connected through the vconnection vc.

      @param vc representing a connection that your plugin has opened
        between Traffic Server and a (remote) host.
      @param ip will be set to the IP address of the remote host in
        network byte order. Note: this value is 32-bit, for IPv4.

   */
  inkapi INKReturnCode INKNetVConnRemoteIPGet(INKVConn vc, unsigned int *ip);

  inkapi INKReturnCode INKNetVConnRemotePortGet(INKVConn vc, int *port);

  /**
      Opens a network connection to the host specified by ip on the port
      specified by port. If the connection is successfully opened, contp
      is called back with the event INK_EVENT_NET_CONNECT and the new
      network vconnection will be passed in the event data parameter.
      If the connection is not successful, contp is called back with
      the event INK_EVENT_NET_CONNECT_FAILED.

      Note: on Solaris, it is possible to receive INK_EVENT_NET_CONNECT
      even if the connection failed, because of the implementation of
      network sockets in the underlying operating system. There is an
      exception: if a plugin tries to open a connection to a port on
      its own host machine, then INK_EVENT_NET_CONNECT is sent only
      if the connection is successfully opened. In general, however,
      your plugin needs to look for an INK_EVENT_VCONN_WRITE_READY to
      be sure that the connection is successfully opened.

      @param contp continuation that is called back when the attempted
        net connection either succeeds or fails.
      @param ip of the host to connect to, in network byte order.
      @param port of the host to connect to, in host byte order.
      @return something allows you to check if the connection is complete,
        or cancel the attempt to connect.

   */
  inkapi INKAction INKNetConnect(INKCont contp, unsigned int ip, int port);

  inkapi INKAction INKNetAccept(INKCont contp, int port);

  /* --------------------------------------------------------------------------
     DNS Lookups */
  inkapi INKAction INKHostLookup(INKCont contp, char *hostname, int namelen);
  inkapi INKReturnCode INKHostLookupResultIPGet(INKHostLookupResult lookup_result, unsigned int *ip);

  /* --------------------------------------------------------------------------
     Cache VConnections */
  /**
      Asks the Traffic Server cache if the object corresponding to key
      exists in the cache and can be read. If the object can be read,
      the Traffic Server cache calls the continuation contp back with
      the event INK_EVENT_CACHE_OPEN_READ. In this case, the cache also
      passes contp a cache vconnection and contp can then initiate a
      read operation on that vconnection using INKVConnRead.

      If the object cannot be read, the cache calls contp back with
      the event INK_EVENT_CACHE_OPEN_READ_FAILED. The user (contp)
      has the option to cancel the action returned by INKCacheRead.
      Note that reentrant calls are possible, i.e. the cache can call
      back the user (contp) in the same call.

      @param contp continuation to be called back if a read operation
        is permissible.
      @param key cache key corresponding to the object to be read.
      @return something allowing the user to cancel or schedule the
        cache read.

   */
  inkapi INKAction INKCacheRead(INKCont contp, INKCacheKey key);

  /**
      Asks the Traffic Server cache if contp can start writing the
      object (corresponding to key) to the cache. If the object
      can be written, the cache calls contp back with the event
      INK_EVENT_CACHE_OPEN_WRITE. In this case, the cache also passes
      contp a cache vconnection and contp can then initiate a write
      operation on that vconnection using INKVConnWrite. The object
      is not committed to the cache until the vconnection is closed.
      When all data has been transferred, the user (contp) must do
      an INKVConnClose. In case of any errors, the user MUST do an
      INKVConnAbort(contp, 0).

      If the object cannot be written, the cache calls contp back with
      the event INK_EVENT_CACHE_OPEN_WRITE_FAILED. This can happen,
      for example, if there is another object with the same key being
      written to the cache. The user (contp) has the option to cancel
      the action returned by INKCacheWrite.

      Note that reentrant calls are possible, i.e. the cache can call
      back the user (contp) in the same call.

      @param contp continuation that the cache calls back (telling it
        whether the write operation can proceed or not).
      @param key cache key corresponding to the object to be cached.
      @return something allowing the user to cancel or schedule the
        cache write.

   */
  inkapi INKAction INKCacheWrite(INKCont contp, INKCacheKey key);

  /**
      Removes the object corresponding to key from the cache. If the
      object was removed successfully, the cache calls contp back
      with the event INK_EVENT_CACHE_REMOVE. If the object was not
      found in the cache, the cache calls contp back with the event
      INK_EVENT_CACHE_REMOVE_FAILED.

      In both of these callbacks, the user (contp) does not have to do
      anything. The user does not get any vconnection from the cache,
      since no data needs to be transferred. When the cache calls
      contp back with INK_EVENT_CACHE_REMOVE, the remove has already
      been commited.

      @param contp continuation that the cache calls back reporting the
        success or failure of the remove.
      @param key cache key corresponding to the object to be removed.
      @return something allowing the user to cancel or schedule the
        remove.

   */
  inkapi INKAction INKCacheRemove(INKCont contp, INKCacheKey key);
  inkapi INKReturnCode INKCacheReady(int *is_ready);
  inkapi INKAction INKCacheScan(INKCont contp, INKCacheKey key, int KB_per_second);

  /* --------------------------------------------------------------------------
     VIOs */
  inkapi INKReturnCode INKVIOReenable(INKVIO viop);
  inkapi INKIOBuffer INKVIOBufferGet(INKVIO viop);
  inkapi INKIOBufferReader INKVIOReaderGet(INKVIO viop);
  inkapi int INKVIONBytesGet(INKVIO viop);
  inkapi INKReturnCode INKVIONBytesSet(INKVIO viop, int nbytes);
  inkapi int INKVIONDoneGet(INKVIO viop);
  inkapi INKReturnCode INKVIONDoneSet(INKVIO viop, int ndone);
  inkapi int INKVIONTodoGet(INKVIO viop);
  inkapi INKMutex INKVIOMutexGet(INKVIO viop);
  inkapi INKCont INKVIOContGet(INKVIO viop);
  inkapi INKVConn INKVIOVConnGet(INKVIO viop);

  /* --------------------------------------------------------------------------
     Buffers */
  inkapi INKIOBuffer INKIOBufferCreate(void);

  /**
      Creates a new INKIOBuffer of the specified size. With this function,
      you can create smaller buffers than the 32K buffer created by
      INKIOBufferCreate(). In some situations using smaller buffers can
      improve performance.

      @param index size of the new INKIOBuffer to be created.
      @param new INKIOBuffer of the specified size.

   */
  inkapi INKIOBuffer INKIOBufferSizedCreate(INKIOBufferSizeIndex index);

  /**
      The watermark of an INKIOBuffer is the minimum number of bytes
      of data that have to be in the buffer before calling back any
      continuation that has initiated a read operation on this buffer.
      INKIOBufferWaterMarkGet() will provide the size of the watermark,
      in bytes, for a specified INKIOBuffer.

      @param bufp buffer whose watermark the function gets.
      @param water_mark will be set to the current watermark of the
        provided INKIOBuffer.

   */
  inkapi INKReturnCode INKIOBufferWaterMarkGet(INKIOBuffer bufp, int *water_mark);

  /**
      The watermark of an INKIOBuffer is the minimum number of bytes
      of data that have to be in the buffer before calling back any
      continuation that has initiated a read operation on this buffer.
      As a writer feeds data into the INKIOBuffer, no readers are called
      back until the amount of data reaches the watermark. Setting
      a watermark can improve performance because it avoids frequent
      callbacks to read small amounts of data. INKIOBufferWaterMarkSet()
      assigns a watermark to a particular INKIOBuffer.

      @param bufp buffer whose water mark the function sets.
      @param water_mark watermark setting, as a number of bytes.

   */
  inkapi INKReturnCode INKIOBufferWaterMarkSet(INKIOBuffer bufp, int water_mark);

  inkapi INKReturnCode INKIOBufferDestroy(INKIOBuffer bufp);
  inkapi INKIOBufferBlock INKIOBufferStart(INKIOBuffer bufp);
  inkapi int INKIOBufferCopy(INKIOBuffer bufp, INKIOBufferReader readerp, int length, int offset);

  /**
      Writes length bytes of data contained in the string buf to the
      INKIOBuffer bufp. Returns the number of bytes of data successfully
      written to the INKIOBuffer.

      @param bufp is the INKIOBuffer to write into.
      @param buf string to write into the INKIOBuffer.
      @param length of the string buf.
      @return length of data successfully copied into the buffer,
        in bytes.

   */
  inkapi int INKIOBufferWrite(INKIOBuffer bufp, const void *buf, int length);
  inkapi INKReturnCode INKIOBufferProduce(INKIOBuffer bufp, int nbytes);

  inkapi INKIOBufferBlock INKIOBufferBlockNext(INKIOBufferBlock blockp);
  inkapi const char *INKIOBufferBlockReadStart(INKIOBufferBlock blockp, INKIOBufferReader readerp, int *avail);
  inkapi int INKIOBufferBlockReadAvail(INKIOBufferBlock blockp, INKIOBufferReader readerp);
  inkapi char *INKIOBufferBlockWriteStart(INKIOBufferBlock blockp, int *avail);
  inkapi int INKIOBufferBlockWriteAvail(INKIOBufferBlock blockp);

  inkapi INKIOBufferReader INKIOBufferReaderAlloc(INKIOBuffer bufp);
  inkapi INKIOBufferReader INKIOBufferReaderClone(INKIOBufferReader readerp);
  inkapi INKReturnCode INKIOBufferReaderFree(INKIOBufferReader readerp);
  inkapi INKIOBufferBlock INKIOBufferReaderStart(INKIOBufferReader readerp);
  inkapi INKReturnCode INKIOBufferReaderConsume(INKIOBufferReader readerp, int nbytes);
  inkapi int INKIOBufferReaderAvail(INKIOBufferReader readerp);


  /* --------------------------------------------------------------------------
     Stats based on librecords (this is prefered API until we rewrite stats).
     This system has a limitation of up to 10,000 stats max, controlled via
     proxy.config.stat_api.max_stats_allowed (default is 512).

     This is available as of Apache TS v2.2.*/
  typedef enum
    {
      TS_STAT_TYPE_INT = 1,
      TS_STAT_TYPE_FLOAT,
      TS_STAT_TYPE_STRING,
      TS_STAT_TYPE_COUNTER,
    } TSStatDataType;

  typedef enum
    {
      TS_STAT_PERSISTENT = 1,
      TS_STAT_NON_PERSISTENT
    } TSStatPersistence;

  typedef enum
    {
      TS_STAT_SYNC_SUM = 0,
      TS_STAT_SYNC_COUNT,
      TS_STAT_SYNC_AVG,
      TS_STAT_SYNC_TIMEAVG,
    } TSStatSync;

  inkapi int TSRegisterStat(const char *the_name, TSStatDataType the_type, TSStatPersistence persist, TSStatSync sync);

  inkapi INKReturnCode TSStatIntIncrement(int the_stat, INK64 amount);
  inkapi INKReturnCode TSStatIntDecrement(int the_stat, INK64 amount);
  inkapi INKReturnCode TSStatFloatIncrement(int the_stat, float amount);
  inkapi INKReturnCode TSStatFloatDecrement(int the_stat, float amount);

  inkapi INKReturnCode TSStatIntGet(int the_stat, INK64* value);
  inkapi INKReturnCode TSStatIntSet(int the_stat, INK64 value);
  inkapi INKReturnCode TSStatFloatGet(int the_stat, float* value);
  inkapi INKReturnCode TSStatFloatSet(int the_stat, float value);


  /* --------------------------------------------------------------------------
     This is the old stats system, it's completely deprecated, and should not
     be used. It has serious limitations both in scalability and performance. */
  typedef enum
    {
      INKSTAT_TYPE_INT64,
      INKSTAT_TYPE_FLOAT
    } INKStatTypes;

  typedef void *INKStat;
  typedef void *INKCoupledStat;

  /* --------------------------------------------------------------------------
     uncoupled stats */
  /** @deprecated */
  inkapi INKStat INKStatCreate(const char *the_name, INKStatTypes the_type);
  /** @deprecated */
  inkapi INKReturnCode INKStatIntAddTo(INKStat the_stat, INK64 amount);
  /** @deprecated */
  inkapi INKReturnCode INKStatFloatAddTo(INKStat the_stat, float amount);
  /** @deprecated */
  inkapi INKReturnCode INKStatDecrement(INKStat the_stat);
  /** @deprecated */
  inkapi INKReturnCode INKStatIncrement(INKStat the_stat);
  /** @deprecated */
  inkapi INKReturnCode INKStatIntGet(INKStat the_stat, INK64 * value);
  /** @deprecated */
  inkapi INKReturnCode INKStatFloatGet(INKStat the_stat, float *value);
  /** @deprecated */
  inkapi INKReturnCode INKStatIntSet(INKStat the_stat, INK64 value);
  /** @deprecated */
  inkapi INKReturnCode INKStatFloatSet(INKStat the_stat, float value);

  /** These were removed with the old version of TS */
  /** @deprecated */
  inkapi INK_DEPRECATED INK64 INKStatIntRead(INKStat the_stat);

  /** @deprecated */
  inkapi INK_DEPRECATED float INKStatFloatRead(INKStat the_stat);

  /* --------------------------------------------------------------------------
     coupled stats */
  /** @deprecated */
  inkapi INKCoupledStat INKStatCoupledGlobalCategoryCreate(const char *the_name);
  /** @deprecated */
  inkapi INKCoupledStat INKStatCoupledLocalCopyCreate(const char *the_name, INKCoupledStat global_copy);
  /** @deprecated */
  inkapi INKReturnCode INKStatCoupledLocalCopyDestroy(INKCoupledStat local_copy);
  /** @deprecated */
  inkapi INKStat INKStatCoupledGlobalAdd(INKCoupledStat global_copy, const char *the_name, INKStatTypes the_type);
  /** @deprecated */
  inkapi INKStat INKStatCoupledLocalAdd(INKCoupledStat local_copy, const char *the_name, INKStatTypes the_type);
  /** @deprecated */
  inkapi INKReturnCode INKStatsCoupledUpdate(INKCoupledStat local_copy);


  /* --------------------------------------------------------------------------
     tracing api */

  inkapi int INKIsDebugTagSet(const char *t);
  inkapi void INKDebug(const char *tag, const char *format_str, ...);
  extern int diags_on_for_plugins;
#define INKDEBUG if (diags_on_for_plugins) INKDebug

  /* --------------------------------------------------------------------------
     logging api */

  /**
      The following enum values are flags, so they should be powers
      of two. With the exception of INK_LOG_MODE_INVALID_FLAG, they
      are all used to configure the creation of an INKTextLogObject
      through the mode argument to INKTextLogObjectCreate().
      INK_LOG_MODE_INVALID_FLAG is used internally to check the validity
      of this argument. Insert new flags before INK_LOG_MODE_INVALID_FLAG,
      and set INK_LOG_MODE_INVALID_FLAG to the largest power of two of
      the enum.

   */
  enum
  {
    INK_LOG_MODE_ADD_TIMESTAMP = 1,
    INK_LOG_MODE_DO_NOT_RENAME = 2,
    INK_LOG_MODE_INVALID_FLAG = 4
  };

  /**
      This type represents a custom log file that you create with
      INKTextLogObjectCreate(). Your plugin writes entries into this
      log file using INKTextLogObjectWrite().

   */
  typedef void *INKTextLogObject;

  /**

      Creates a new custom log file that your plugin can write to. You
      can design the fields and inputs to the log file using the
      INKTextLogObjectWrite() function. The logs you create are treated
      like ordinary logs; they are rolled if log rolling is enabled. (Log
      collation is not supported though).

      @param filename new log file being created. The new log file
        is created in the logs directory. You can specify a path to a
        subdirectory within the log directory, e.g. subdir/filename,
        but make sure you create the subdirectory first. If you do
        not specify a file name extension, the extension ".log" is
        automatically added.
      @param mode is one (or both) of the following:
        - INK_LOG_MODE_ADD_TIMESTAMP Whenever the plugin makes a log
          entry using INKTextLogObjectWrite (see below), it prepends
          the entry with a timestamp.
        - INK_LOG_MODE_DO_NOT_RENAME This means that if there is a
          filename conflict, Traffic Server should not attempt to rename
          the custom log. The consequence of a name conflict is that the
          custom log will simply not be created, e.g. suppose you call:
            @code
            log = INKTextLogObjectCreate("squid" , mode, NULL, &error);
            @endcode
          If mode is INK_LOG_MODE_DO_NOT_RENAME, you will NOT get a new
          log (you'll get a null pointer) if squid.log already exists.
          If mode is not INK_LOG_MODE_DO_NOT_RENAME, Traffic Server
          tries to rename the log to a new name (it will try squid_1.log).
      @param new_log_obj new custom log file.
      @return error code:
        - INK_LOG_ERROR_NO_ERROR No error; the log object has been
          created successfully.
        - INK_LOG_ERROR_OBJECT_CREATION Log object not created. This
          error is rare and would most likely be caused by the system
          running out of memory.
        - INK_LOG_ERROR_FILENAME_CONFLICTS You get this error if mode =
          INK_LOG_MODE_DO_NOT_RENAME, and if there is a naming conflict.
          The log object is not created.
        - INK_LOG_ERROR_FILE_ACCESS Log object not created because of
          a file access problem (for example, no write permission to the
          logging directory, or a specified subdirectory for the log file
          does not exist).

   */
  inkapi INKReturnCode INKTextLogObjectCreate(const char *filename, int mode, INKTextLogObject * new_log_obj);

  /**
      Writes a printf-style formatted statement to an INKTextLogObject
      (a plugin custom log).

      @param the_object log object to write to. You must first create
        this object with INKTextLogObjectCreate().
      @param format printf-style formatted statement to be printed.
      @param ... parameters in the formatted statement. A newline is
        automatically added to the end.
      @return one of the following errors:
        - INK_LOG_ERROR_NO_ERROR Means that the write was successful.
        - INK_LOG_ERROR_LOG_SPACE_EXHAUSTED Means that Traffic Server
          ran out of disk space for logs. If you see this error you might
          want to roll logs more often.
        - INK_LOG_ERROR_INTERNAL_ERROR Indicates some internal problem
          with a log entry (such as an entry larger than the size of the
          log write buffer). This error is very unusual.

   */
  inkapi INKReturnCode INKTextLogObjectWrite(INKTextLogObject the_object, char *format, ...);

  /**
      This immediately flushes the contents of the log write buffer for
      the_object to disk. Use this call only if you want to make sure that
      log entries are flushed immediately. This call has a performance
      cost. Traffic Server flushes the log buffer automatically about
      every 1 second.

      @param the_object custom log file whose write buffer is to be
        flushed.

   */
  inkapi INKReturnCode INKTextLogObjectFlush(INKTextLogObject the_object);

  /**
      Destroys a log object and releases the memory allocated to it.
      Use this call if you are done with the log.

      @param  the_object custom log to be destroyed.

   */
  inkapi INKReturnCode INKTextLogObjectDestroy(INKTextLogObject the_object);

  /**
      Set log header.

      @return INK_SUCCESS or INK_ERROR.

   */
  inkapi INKReturnCode INKTextLogObjectHeaderSet(INKTextLogObject the_object, const char *header);

  /**
      Enable/disable rolling.

      @return INK_SUCCESS or INK_ERROR.

   */
  inkapi INKReturnCode INKTextLogObjectRollingEnabledSet(INKTextLogObject the_object, int rolling_enabled);

  /**
      Set the rolling interval.

      @return INK_SUCCESS or INK_ERROR.

   */
  inkapi INKReturnCode INKTextLogObjectRollingIntervalSecSet(INKTextLogObject the_object, int rolling_interval_sec);

  /**
      Set the rolling offset.

      @return INK_SUCCESS or INK_ERROR.

   */
  inkapi INKReturnCode INKTextLogObjectRollingOffsetHrSet(INKTextLogObject the_object, int rolling_offset_hr);

  /**
      Async disk IO read

      @return INK_SUCCESS or INK_ERROR.
   */
  inkapi INKReturnCode INKAIORead(int fd, INKU64 offset, char* buf, INKU64 buffSize, INKCont contp);

  /**
      Async disk IO buffer get

      @return char* to the buffer
   */
  inkapi char* INKAIOBufGet(void* data);

  /**
      Async disk IO get number of bytes

      @return the number of bytes
   */
  inkapi int INKAIONBytesGet(void* data);

  /**
      Async disk IO write

      @return INK_SUCCESS or INK_ERROR.
   */
  inkapi INKReturnCode INKAIOWrite(int fd, INKU64 offset, char* buf, const INKU64 bufSize, INKCont contp);

  /**
      Async disk IO set number of threads

      @return INK_SUCCESS or INK_ERROR.
   */
  inkapi INKReturnCode INKAIOThreadNumSet(int thread_num);

  /** 
      Check if transaction was aborted (due client/server errors etc.)

      @return 1 if transaction was aborted
  */
  inkapi int INKHttpTxnAborted(INKHttpTxn txnp);

  /* --------------------------------------------------------------------------
     Deprecated Functions
     Use of the following functions is strongly discouraged. These
     functions may incur performance penalties and may not be supported
     in future releases. */

  /** @deprecated
      The reason is even if VConn is created using this API, it is
      still useless. For example, if we do INKVConnRead(), the read
      operation returns read_vio. If we do INKVIOReenable(read_vio),
      it actually calls:

      @code
      void VIO::reenable() {
        if (vc_server) vc_server->reenable(this);
      }
      @endcode

      vc_server->reenable calls:

      @code
      VConnection::reenable(VIO);
      @endcode

      This function is virtual in VConnection.h. It is defined separately for
      UnixNet, NTNet and CacheVConnection.

      Thus, unless VConn is either NetVConnection or CacheVConnection, it can't
      be instantiated for functions like reenable.

      In addition, this function has never been used.

   */
  inkapi INKVConn INKVConnCreate(INKEventFunc event_funcp, INKMutex mutexp);

  /* --------------------------------------------------------------------------
     Deprecated Buffer Functions */

  /** @deprecated */
  inkapi INK_DEPRECATED INKReturnCode INKIOBufferAppend(INKIOBuffer bufp, INKIOBufferBlock blockp);

  /** @deprecated */
  inkapi INK_DEPRECATED INKIOBufferData INKIOBufferDataCreate(void *data, int size, INKIOBufferDataFlags flags);

  /** @deprecated */
  inkapi INK_DEPRECATED INKIOBufferBlock INKIOBufferBlockCreate(INKIOBufferData datap, int size, int offset);

  /* --------------------------------------------------------------------------
     Deprecated MBuffer functions */

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMBufferDataSet(INKMBuffer bufp, void *data);

  /** @deprecated */
  inkapi INK_DEPRECATED void *INKMBufferDataGet(INKMBuffer bufp, int *length);

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMBufferLengthGet(INKMBuffer bufp);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMBufferRef(INKMBuffer bufp);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMBufferUnref(INKMBuffer bufp);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMBufferCompress(INKMBuffer bufp);

  /* --------------------------------------------------------------------------
     YTS Team, yamsat Plugin */

  /** @deprecated */
  inkapi INK_DEPRECATED int INKHttpTxnCreateRequest(INKHttpTxn txnp, const char *, const char *, int);

  /* --------------------------------------------------------------------------
     Deprecated MIME field functions --- use INKMimeHdrFieldXXX instead */

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldCreate(INKMBuffer bufp);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldDestroy(INKMBuffer bufp, INKMLoc offset);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldCopy(INKMBuffer dest_bufp, INKMLoc dest_offset, INKMBuffer src_bufp, INKMLoc src_offset);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldCopyValues(INKMBuffer dest_bufp, INKMLoc dest_offset, INKMBuffer src_bufp,
                                     INKMLoc src_offset);

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldNext(INKMBuffer bufp, INKMLoc offset);

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMimeFieldLengthGet(INKMBuffer bufp, INKMLoc offset);

  /** @deprecated */
  inkapi INK_DEPRECATED const char *INKMimeFieldNameGet(INKMBuffer bufp, INKMLoc offset, int *length);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldNameSet(INKMBuffer bufp, INKMLoc offset, const char *name, int length);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValuesClear(INKMBuffer bufp, INKMLoc offset);

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMimeFieldValuesCount(INKMBuffer bufp, INKMLoc offset);

  /** @deprecated */
  inkapi INK_DEPRECATED const char *INKMimeFieldValueGet(INKMBuffer bufp, INKMLoc offset, int idx, int *length);

  /** @deprecated */
  inkapi INK_DEPRECATED int INKMimeFieldValueGetInt(INKMBuffer bufp, INKMLoc offset, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED unsigned int INKMimeFieldValueGetUint(INKMBuffer bufp, INKMLoc offset, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED time_t INKMimeFieldValueGetDate(INKMBuffer bufp, INKMLoc offset, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueSet(INKMBuffer bufp, INKMLoc offset, int idx, const char *value, int length);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueSetInt(INKMBuffer bufp, INKMLoc offset, int idx, int value);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueSetUint(INKMBuffer bufp, INKMLoc offset, int idx, unsigned int value);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueSetDate(INKMBuffer bufp, INKMLoc offset, int idx, time_t value);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueAppend(INKMBuffer bufp, INKMLoc offset, int idx, const char *value, int length);

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldValueInsert(INKMBuffer bufp, INKMLoc offset, const char *value, int length, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldValueInsertInt(INKMBuffer bufp, INKMLoc offset, int value, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldValueInsertUint(INKMBuffer bufp, INKMLoc offset, unsigned int value, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED INKMLoc INKMimeFieldValueInsertDate(INKMBuffer bufp, INKMLoc offset, time_t value, int idx);

  /** @deprecated */
  inkapi INK_DEPRECATED void INKMimeFieldValueDelete(INKMBuffer bufp, INKMLoc offset, int idx);

  /* --------------------------------------------------------------------------
     Deprecated MIME field functions in SDK3.0 */

  /** @deprecated Use INKMimeHdrFieldAppend() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx);

  /** @deprecated Use INKMimeHdrFieldValueStringInsert() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueInsert(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *value, int length, int idx);

  /** @deprecated Use INKMimeHdrFieldValueIntInsert() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueInsertInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int value, int idx);

  /** @deprecated Use INKMimeHdrFieldValueUintInsert() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueInsertUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, unsigned int value, int idx);

  /** @deprecated Use INKMimeHdrFieldValueDateInsert() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueInsertDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, time_t value, int idx);

  /** @deprecated Use INKMimeHdrFieldValueStringGet() instead */
  inkapi INK_DEPRECATED const char *INKMimeHdrFieldValueGet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int *value_len_ptr);

  /** @deprecated Use INKMimeHdrFieldValueIntGet() instead */
  inkapi INK_DEPRECATED int INKMimeHdrFieldValueGetInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx);

  /** @deprecated Use INKMimeHdrFieldValueUintGet() instead */
  inkapi INK_DEPRECATED unsigned int INKMimeHdrFieldValueGetUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx);

  /** @deprecated Use INKMimeHdrFieldValueDateGet() instead */
  inkapi INK_DEPRECATED time_t INKMimeHdrFieldValueGetDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx);

  /** @deprecated Use INKMimeHdrFieldValueStringSet() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueSet(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, const char *value, int length);

  /** @deprecated Use INKMimeHdrFieldValueIntSet() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueSetInt(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, int value);

  /** @deprecated Use INKMimeHdrFieldValueUintSet() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueSetUint(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, unsigned int value);

  /** @deprecated Use INKMimeHdrFieldValueDateSet() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldValueSetDate(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int idx, time_t value);

  /** @deprecated Use INKMimeHdrFieldValueDestroy() instead */
  inkapi INK_DEPRECATED INKReturnCode INKMimeHdrFieldDelete(INKMBuffer bufp, INKMLoc hdr, INKMLoc field);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __INK_API_H__ */

