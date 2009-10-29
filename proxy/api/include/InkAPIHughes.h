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

typedef struct
{

  /*request header */
  INKMBuffer request_buf;
  INKMLoc request_loc;

  /*response header */
  INKMBuffer response_buf;
  INKMLoc response_loc;

  /*child ip addr in network order */
  unsigned int client_ip;

  /*the embedded url parsed by the parser */
  const char *embedded_url;

  /* flag which says if a perticular embedded url is present in the cache */
  int present_in_cache;

  /* Reader for the buffer which contains the prefetched object */
  INKIOBuffer object_buf;
  INKIOBufferReader object_buf_reader;

  /* This specifies if we need to invoke the OBJECT_HOOK and whether we
     need to send the buffer to child as well
     This should set inside EMBEDDED_URL_HOOK by the user
   */
  int object_buf_status;

  /*method of sending data to child */
  unsigned int url_proto;
  unsigned int url_response_proto;

} INKPrefetchInfo;

typedef int (*INKPrefetchHook) (int hook, INKPrefetchInfo * prefetch_info);

enum
{                               /* return type for INKPrefetchHook */
  INK_PREFETCH_CONTINUE,
  INK_PREFETCH_DISCONTINUE
};

enum
{
  INK_PREFETCH_PROTO_TCP = 1,
  INK_PREFETCH_PROTO_UDP
};

enum
{
  INK_PREFETCH_OBJ_BUF_NOT_NEEDED = 0,
  INK_PREFETCH_OBJ_BUF_NEEDED,  /* The user wants the buffer but does not
                                   want it to be transmitted to the child */
  INK_PREFETCH_OBJ_BUF_NEEDED_N_TRANSMITTED     /* The object should
                                                   be transmitted as well */
};

enum
{                               /* prefetch hooks */
  INK_PREFETCH_PRE_PARSE_HOOK,
  /* This hook is invoked just before we begin to parse a document
     request and response headers are available.
     Return value: INK_PREFETCH_CONTINUE  :continue parsing
     INK_PREFETCH_DISCONTIUE: don't bother parser
   */

  INK_PREFETCH_EMBEDDED_URL_HOOK,
  /* This hook is invoked when a URL is extracted.
     url_proto and url_response_proto contain the default protocols used
     for sending the url and actual url object respectively to the child.
     The hook can change thes to one of the 3 methods mentioned above.
     Return value: INK_PREFETCH_CONTINUE  : prefetch this url.
     INK_PREFETCH_DISCONTIUE: don't bother prefetching this
     url
   */

  INK_PREFETCH_EMBEDDED_OBJECT_HOOK
    /* This hook is invoked when the user wants to have access to the buffer
       of the embedded object we prefetched. We pass in the buffer reader.
       The reader contains the data in the format specified in the Prefetch
       document (with 12 byte header etc).
       It is the users responsibility to free the reader.
       The only valid field in the PrefetchInfo structure object_buf_reader.
       embedded_url, object_buf, object_buf_reader, and object_buf_status are
       set in INKPrefetchInfo passed as arguments
     */
    /* more hooks */
};

inkapi int INKPrefetchStart();
/* This starts the Prefetch engine in Traffic Server
   Return value 0 indicates success.*/

inkapi int INKPrefetchHookSet(int hook_no, INKPrefetchHook hook_fn);
/* Registers a hook for the given hook_no.
   A hook is already present, it is replace by hook_fn
   return value 0 indicates success */
