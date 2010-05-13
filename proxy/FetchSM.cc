/** @file

  Implements callin functions for plugins

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

#include "ink_config.h"
#include "FetchSM.h"
#include <stdio.h>
#include "HTTP.h"
#include "PluginVC.h"

#define DEBUG_TAG "FetchSM"

ClassAllocator < FetchSM > FetchSMAllocator("FetchSMAllocator");
void
FetchSM::cleanUp()
{
  INKDebug(DEBUG_TAG, "[%s] calling cleanup", __FUNCTION__);
  free_MIOBuffer(response_buffer);
  free_MIOBuffer(req_buffer);
  free_MIOBuffer(resp_buffer);
  mutex.clear();
  http_parser_clear(&http_parser);
  client_response_hdr.destroy();
  INKfree(client_response);
  PluginVC *vc = (PluginVC *) http_vc;
  vc->do_io_close();
  FetchSMAllocator.free(this);
}

void
FetchSM::httpConnect()
{
  INKDebug(DEBUG_TAG, "[%s] calling httpconnect write", __FUNCTION__);
  INKHttpConnect(_ip, _port, &(http_vc));
  PluginVC *vc = (PluginVC *) http_vc;
  read_vio = vc->do_io_read(this, INT_MAX, resp_buffer);
  write_vio = vc->do_io_write(this, getReqLen(), req_reader);
}

char* FetchSM::resp_get(int *length) {
*length = client_bytes;
return client_response;
}

int FetchSM::InvokePlugin(int event, void *data)
{
  Continuation *cont = (Continuation*) contp;
  EThread *mythread = this_ethread();
  MUTEX_TAKE_LOCK(cont->mutex,mythread);
  int ret = cont->handleEvent(event,data);
  MUTEX_UNTAKE_LOCK(cont->mutex,mythread);
  return ret;
}
void
FetchSM::get_info_from_buffer(INKIOBufferReader the_reader)
{
  char *info;
//  char *info_start;

  int read_avail, read_done;
  INKIOBufferBlock blk;
  char *buf;

  if (!the_reader)
    return ;

  read_avail = INKIOBufferReaderAvail(the_reader);
  INKDebug(DEBUG_TAG, "[%s] total avail %d", __FUNCTION__, read_avail);
  //size_t hdr_size = _headers.size();
  //info = (char *) INKmalloc(sizeof(char) * (read_avail+1) + hdr_size);
  info = (char *) INKmalloc(sizeof(char) * (read_avail+1));
  if (info == NULL)
    return ;
  client_response = info;
  //strncpy(info, _headers.data(), hdr_size);
  //info += hdr_size;

  /* Read the data out of the reader */
  while (read_avail > 0) {
    blk = INKIOBufferReaderStart(the_reader);
    buf = (char *) INKIOBufferBlockReadStart(blk, the_reader, &read_done);
    memcpy(info, buf, read_done);
    if (read_done > 0) {
      INKIOBufferReaderConsume(the_reader, read_done);
      read_avail -= read_done;
      info += read_done;
    }
  }

}

void
FetchSM::process_fetch_read(int event)
{

  INKDebug(DEBUG_TAG, "[%s] I am here read", __FUNCTION__);
  int bytes;
  int bytes_used;
  int actual_bytes_copied = 0;
  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
    bytes = resp_reader->read_avail();
    INKDebug(DEBUG_TAG, "[%s] number of bytes in read ready %d", __FUNCTION__, bytes);
    while (actual_bytes_copied < bytes) {
       actual_bytes_copied = response_buffer->write(resp_reader, bytes, 0);
      resp_reader->consume(actual_bytes_copied);
      bytes = resp_reader->read_avail();
    }
    resp_reader->consume(bytes);
    if (header_done == 0 && callback_options == AFTER_HEADER) {
      if (client_response_hdr.parse_resp(&http_parser, response_reader, &bytes_used, 0) == PARSE_DONE) {
        //InvokePlugin( INK_EVENT_INTERNAL_60201, (void *) &client_response_hdr);
        InvokePlugin( callback_events.success_event_id, (void *) &client_response_hdr);
        header_done = 1;
      }
    }
    read_vio->reenable();
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
  case INK_EVENT_VCONN_EOS:
    if(callback_options == AFTER_HEADER || callback_options == AFTER_BODY) {
    bytes = response_reader->read_avail();

    get_info_from_buffer(response_reader);
    INKDebug(DEBUG_TAG, "[%s] number of bytes %d", __FUNCTION__, bytes);
    if(client_response!=NULL)
      client_response[bytes] = '\0';
      //client_response[bytes + _headers.size()] = '\0';
    INKDebug(DEBUG_TAG, "[%s] Completed data fetch of size %d, notifying caller", __FUNCTION__, bytes);
    //InvokePlugin( INK_EVENT_INTERNAL_60200, (void *) client_response);
   client_bytes = bytes;
    //InvokePlugin( INK_EVENT_INTERNAL_60200, (void *) this);
      InvokePlugin( callback_events.success_event_id, (void *) this);
    }

    INKDebug(DEBUG_TAG, "[%s] received EOS", __FUNCTION__);
    cleanUp();
    break;
  case INK_EVENT_ERROR:
  default:
    //InvokePlugin(INK_EVENT_ERROR, NULL);
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
    break;

  }
}

void
FetchSM::process_fetch_write(int event)
{
  INKDebug(DEBUG_TAG, "[%s] calling process write", __FUNCTION__);
  switch (event) {
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    //INKVConnShutdown(http_vc, 0, 1) ; why does not this work???
    req_finished = true;
    break;
  case INK_EVENT_VCONN_WRITE_READY:
  case INK_EVENT_ERROR:
    //InvokePlugin( INK_EVENT_ERROR, NULL);
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
  default:
    break;
  }
}

int
FetchSM::fetch_handler(int event, void *edata)
{
  INKDebug(DEBUG_TAG, "[%s] calling fetch_plugin", __FUNCTION__);

  if (edata == read_vio) {
    process_fetch_read(event);
  } else if (edata == write_vio) {
    process_fetch_write(event);
  } else {
    //InvokePlugin( INK_EVENT_ERROR, NULL);
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
  }
  return 1;
}
