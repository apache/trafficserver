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

/*
  This file is created for Prefetch API and is used only by Hughes
*/

/// Type of connection.
typedef enum
{
  ILL_BLAST = 0,
  UDP_BLAST,
  TCP_BLAST,
  MULTICAST_BLAST
} PrefetchBlastType;

typedef struct {
  PrefetchBlastType type;
  IpEndpoint ip;
} PrefetchBlastData;

typedef struct
{

  /*request header */
  TSMBuffer request_buf;
  TSMLoc request_loc;

  /*response header */
  TSMBuffer response_buf;
  TSMLoc response_loc;

  /*child ip addr in network order */
  IpEndpoint client_ip;

  /*the embedded url parsed by the parser */
  const char *embedded_url;

  /* flag which says if a perticular embedded url is present in the cache */
  int present_in_cache;

  /* Reader for the buffer which contains the prefetched object */
  TSIOBuffer object_buf;
  TSIOBufferReader object_buf_reader;

  /* This specifies if we need to invoke the OBJECT_HOOK and whether we
     need to send the buffer to child as well
     This should set inside EMBEDDED_URL_HOOK by the user
   */
  int object_buf_status;

  /** Method of sending data to child.

      If set to @c MULTICAST_BLAST then the corresponding address
      value must be set to a multicast address to use.
   */
  PrefetchBlastData url_blast;
  PrefetchBlastData url_response_blast;

} TSPrefetchInfo;

typedef int (*TSPrefetchHook) (int hook, TSPrefetchInfo * prefetch_info);

enum
{                               /* return type for TSPrefetchHook */
  TS_PREFETCH_CONTINUE,
  TS_PREFETCH_DISCONTINUE
};

enum
{
  TS_PREFETCH_OBJ_BUF_NOT_NEEDED = 0,
  TS_PREFETCH_OBJ_BUF_NEEDED,  /* The user wants the buffer but does not
                                   want it to be transmitted to the child */
  TS_PREFETCH_OBJ_BUF_NEEDED_N_TRANSMITTED     /* The object should
                                                   be transmitted as well */
};

enum
{                               /* prefetch hooks */
  TS_PREFETCH_PRE_PARSE_HOOK,
  /* This hook is invoked just before we begin to parse a document
     request and response headers are available.
     Return value: TS_PREFETCH_CONTINUE  :continue parsing
     TS_PREFETCH_DISCONTIUE: don't bother parser
   */

  TS_PREFETCH_EMBEDDED_URL_HOOK,
  /* This hook is invoked when a URL is extracted.
     url_proto and url_response_proto contain the default protocols used
     for sending the url and actual url object respectively to the child.
     The hook can change thes to one of the 3 methods mentioned above.
     Return value: TS_PREFETCH_CONTINUE  : prefetch this url.
     TS_PREFETCH_DISCONTIUE: don't bother prefetching this
     url
   */

  TS_PREFETCH_EMBEDDED_OBJECT_HOOK
    /* This hook is invoked when the user wants to have access to the buffer
       of the embedded object we prefetched. We pass in the buffer reader.
       The reader contains the data in the format specified in the Prefetch
       document (with 12 byte header etc).
       It is the users responsibility to free the reader.
       The only valid field in the PrefetchInfo structure object_buf_reader.
       embedded_url, object_buf, object_buf_reader, and object_buf_status are
       set in TSPrefetchInfo passed as arguments
     */
    /* more hooks */
};

tsapi int TSPrefetchStart();
/* This starts the Prefetch engine in Traffic Server
   Return value 0 indicates success.*/

tsapi int TSPrefetchHookSet(int hook_no, TSPrefetchHook hook_fn);
/* Registers a hook for the given hook_no.
   A hook is already present, it is replace by hook_fn
   return value 0 indicates success */
