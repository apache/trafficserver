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

/************* ***************************
 *
 *  WebHttpContext.h - the http web-ui transaction state
 *
 *
 ****************************************************************************/

#ifndef _WEB_HTTP_CONTEXT_H_
#define _WEB_HTTP_CONTEXT_H_

#include "ink_hash_table.h"
#include "TextBuffer.h"

#include "WebGlobals.h"
#include "mgmtapi.h"
#include "WebHttpMessage.h"
#include "WebUtils.h"

struct WebHttpContext {
  uint32_t request_state;       // client request state
  uint32_t server_state;        // bit-mask of enabled server features
  httpMessage *request;         // client request
  httpResponse *response_hdr;   // server response headers
  textBuffer *response_bdy;     // server repsonse body
  textBuffer *submit_warn;      // submit warn text
  textBuffer *submit_note;      // submit info text
  InkHashTable *query_data_ht;  // client query name/value hash-table
  InkHashTable *post_data_ht;   // client POST name/value hash-table
  InkHashTable *submit_warn_ht; // ht of warn submission records
  InkHashTable *submit_note_ht; // ht of info submission records
  sockaddr_in *client_info;     // client conection information
  SocketInfo si;                // socket information

  char *top_level_render_file; // top-level file to render
  char *cache_query_result;    // cache inspector query result

  const char *default_file; // default file
  const char *doc_root;     // document root
  int doc_root_len;         // length of doc_root
};

WebHttpContext *WebHttpContextCreate(WebHttpConInfo *whci);
void WebHttpContextDestroy(WebHttpContext *whc);

#endif // _WEB_HTTP_CONTEXT_H_
