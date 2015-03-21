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
 *  WebHttpContext.cc - the http web-ui transaction state
 *
 *
 ****************************************************************************/

#include "ink_config.h"
#include "WebHttpContext.h"

//-------------------------------------------------------------------------
// WebHttpContextCreate
//
// Wraps a WebHttpContext around a WebHttpConInfo and WebHttpConInfo's
// internal WebContext.  Note that the returned WebHttpContext will
// keep pointers into the WebHttpConInfo and WebContext structures.
// Be careful not to delete/free the WebHttpConInfo or WebContext
// before WebHttpContext is done with them.
//-------------------------------------------------------------------------

WebHttpContext *
WebHttpContextCreate(WebHttpConInfo *whci)
{
  WebHttpContext *whc = (WebHttpContext *)ats_malloc(sizeof(WebHttpContext));

  // memset to 0; note strings are zero'd too
  memset(whc, 0, sizeof(WebHttpContext));

  whc->request = new httpMessage();
  whc->response_hdr = new httpResponse();
  whc->response_bdy = new textBuffer(8192);
  whc->submit_warn = new textBuffer(256);
  whc->submit_note = new textBuffer(256);
  whc->submit_warn_ht = ink_hash_table_create(InkHashTableKeyType_String);
  whc->submit_note_ht = ink_hash_table_create(InkHashTableKeyType_String);
  whc->si.fd = whci->fd;

  // keep pointers into the context passed to us
  whc->client_info = whci->clientInfo;
  whc->default_file = whci->context->defaultFile;
  whc->doc_root = whci->context->docRoot;
  whc->doc_root_len = whci->context->docRootLen;

  // set server_state
  if (whci->context == &autoconfContext) {
    whc->server_state |= WEB_HTTP_SERVER_STATE_AUTOCONF;
  }

  return whc;
}

//-------------------------------------------------------------------------
// WebHttpContextDestroy
//-------------------------------------------------------------------------

void
WebHttpContextDestroy(WebHttpContext *whc)
{
  if (whc) {
    if (whc->request)
      delete (whc->request);
    if (whc->response_hdr)
      delete (whc->response_hdr);
    if (whc->response_bdy)
      delete (whc->response_bdy);
    if (whc->submit_warn)
      delete (whc->submit_warn);
    if (whc->submit_note)
      delete (whc->submit_note);
    if (whc->query_data_ht)
      ink_hash_table_destroy_and_xfree_values(whc->query_data_ht);
    if (whc->post_data_ht)
      ink_hash_table_destroy_and_xfree_values(whc->post_data_ht);
    if (whc->submit_warn_ht)
      ink_hash_table_destroy(whc->submit_warn_ht);
    if (whc->submit_note_ht)
      ink_hash_table_destroy(whc->submit_note_ht);

    ats_free(whc->top_level_render_file);
    ats_free(whc->cache_query_result);
    ats_free(whc);
  }
}
