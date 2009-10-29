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



#ifndef _COMMON_H_
#define _COMMON_H_

#include "list.h"

/* Debug tags */
#define LOW  "asml"             /* Low level functions calls, intermediate vars and buffers */
#define MED  "asmlm"            /* Trace all major functions calls, in and out buffers */
#define HIGH "asmlmh"           /* Major steps */

#define MAGIC_ALIVE 0xfeedbabe  /* For live structures */
#define MAGIC_DEAD  0xdeadbeef  /* For deallocated structures */

/* Default port number used to connect to TS */
#define TS_DEFAULT_PORT         8280

/* Default port for Web Servers */
#define HTTP_DEFAULT_PORT       80

/* Max read retries */
#define CACHE_READ_MAX_RETRIES  8
/* msec to wait before retrying */
#define CACHE_READ_RETRY_DELAY   15

/* Format of an Http request. Used to send request on TS socket back */
#define BLOCK_HTTP_REQUEST_FORMAT "GET %s HTTP/1.0\r\n%s\r\n%s:true\r\n\r\n"

/* Special header for template and block pages */
#define HEADER_X_TEMPLATE   "X-Template"
#define HEADER_X_NOCACHE    "X-NoCache"
#define HEADER_X_BLOCK      "X-Block"
#define HEADER_NO_CACHE     "Cache-Control: no-cache"

#define CONTENT_TYPE_TEXT_HTML      "text/html"
#define CONTENT_TYPE_TEXT_HTML_LEN  9

/* Maximum size, in character for a dynamic statement */
#define DYN_TAG_MAX_SIZE        512

/* Constants used to extract dynamic tags */
#define DYNAMIC_START "<DYNAMIC>"
#define DYNAMIC_END "</DYNAMIC>"

#define DYNAMIC_ATTR_BLOCKNAME "BLOCKNAME"

/* A dynamic statement contains an URL parameter */
#define DYNAMIC_ATTR_URL "URL"

/* The URL parameter can contain the ${QSTRING} variable */
/* #define DYNAMIC_ATTR_URL_VAR_QUERYSTRING "${QSTRING}" */
#define DYNAMIC_ATTR_URL_VAR_QUERYSTRING "QSTRING"

/* Dynamic statement says whether the block is cacheable or not */
#define DYNAMIC_ATTR_CACHEABLE "CACHEABLE"
#define DYNAMIC_ATTR_CACHEABLE_DEFAULT_VALUE 0
#define DYNAMIC_ATTR_CACHEABLE_VALUE_FALSE "false"
#define DYNAMIC_ATTR_CACHEABLE_VALUE_TRUE  "true"

/* A dynamic statement may have a TTL parameter */
#define DYNAMIC_ATTR_TTL "TTL"
#define DYNAMIC_ATTR_TTL_DEFAULT_VALUE  30

#define DYNAMIC_ATTR_QUERY "QUERY"
#define DYNAMIC_ATTR_COOKIES "COOKIES"

/* Constants used to extract block tags in document to include */
#define BLOCK_START  "<BLOCK>"
#define BLOCK_END    "</BLOCK>"

/* Used for parsing the input buffer */
#define CHARS_WINDOW_SIZE 64

/* Maximum size of an URL in a dynamic statement */
#define URL_MAX_SIZE 256

/* Suffix appended to url of template before storing it in the cache */
#define TEMPLATE_CACHE_SUFFIX ".template"

/* TEMP */
/* template id write in metadata block in cache */
#define TEMPLATE_ID  1973


typedef enum
{
  STATE_INPUT_BUFFER,           /* Read response and bufferize it */
  STATE_PARSE_BUFFER,           /* Parse input buffer to extract dynamic tags */
  STATE_CACHE_PREPARE_READ,     /* Lookup dynamic block in the cache */
  STATE_CACHE_RETRY_READ,
  STATE_CACHE_READ,             /* Read dynamic block from the cache */
  STATE_CACHE_PREPARE_WRITE,    /* Try to do a write cache */
  STATE_CACHE_WRITE,            /* Write dynamic block to the cache */
  STATE_CACHE_REMOVE,           /* Remove a stale block from cache */
  STATE_TS_CONNECT,             /* Create socket back to ts to fetch embedded obj */
  STATE_TS_WRITE,               /* Write request to ts socket back */
  STATE_TS_READ,                /* Read doc from ts socket back */
  STATE_OUTPUT_WRITE,           /* Send assembled page to client */
  STATE_ERROR,                  /* Error */
  STATE_DEAD
} AsmStateType;


/* These info are store along with block content into the cache */
typedef struct
{
  /* Date the block was written into the cache */
  time_t write_time;
  /* Id of template */
  int template_id;
} BlockMetaData;



typedef struct
{
  /* Store the current state of the assembly process */
  AsmStateType state;

  /* Store current transaction */
  INKHttpTxn txn;

  /* The input is the Http response coming from the OS */
  INKIOBuffer input_buffer;
  INKIOBufferReader input_parse_reader;

  /* The ouput is the transformed Http response sent to the client */
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
  INKVConn output_vc;
  INKVIO output_vio;

  /* used to cancel any pending action when we exit */
  INKAction pending_action;

  /* Connection via socket back to TS  */
  INKVConn ts_vc;
  INKVIO ts_vio;

  /* cache input = Http request sent to TS on socket back */
  INKIOBuffer ts_input_buffer;
  INKIOBufferReader ts_input_reader;

  /* cache output = result sent by TS on socket back */
  INKIOBuffer ts_output_buffer;
  INKIOBufferReader ts_output_reader;

  /* The block is bufferized in this iobuffer */
  INKIOBuffer block_buffer;
  INKIOBufferReader block_reader;

  /* Connection to the cache */
  INKVConn cache_vc;
  INKVIO cache_read_vio;
  INKVIO cache_write_vio;

  /* Buffer/Reader to read/write block to the cache */
  INKIOBuffer cache_read_buffer;
  INKIOBuffer cache_write_buffer;
  INKIOBufferReader cache_read_reader;
  INKIOBufferReader cache_write_reader;

  int cache_read_retry_counter;

  /* key used to store/fetch block from the cache */
  INKCacheKey block_key;

  /* TTL for the block in the cache */
  int block_ttl;

  /* URL of the block to be included */
  char *block_url;

  /* Is this block cacheable ? 0=no 1=yes */
  int block_is_cacheable;

  /* Meta data associated with the block */
  BlockMetaData block_metadata;

  /* To extract from client's request the pairs name/value
     for query string params and cookies */
  PairList query;
  PairList cookies;

  /* keep the full query string of the current transaction here */
  char *query_string;

  unsigned int magic;

} AsmData;


/* This structure is associated to the transaction.
   Not used in the transformatin */
typedef struct
{
  /* To store requested Url and Url actually used for cache lookup */
  INKMBuffer request_url_buf;
  INKMLoc request_url_loc;

  INKMBuffer template_url_buf;
  INKMLoc template_url_loc;

  /* flag used to know whether or not transformation has been already set up */
  int transform_created;

  unsigned int magic;
} TxnData;


#endif
