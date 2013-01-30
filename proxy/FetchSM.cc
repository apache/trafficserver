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
  Debug(DEBUG_TAG, "[%s] calling cleanup", __FUNCTION__);
  free_MIOBuffer(response_buffer);
  free_MIOBuffer(req_buffer);
  free_MIOBuffer(resp_buffer);
  mutex.clear();
  http_parser_clear(&http_parser);
  client_response_hdr.destroy();
  ats_free(client_response);

  PluginVC *vc = (PluginVC *) http_vc;

  vc->do_io_close();
  FetchSMAllocator.free(this);
}

void
FetchSM::httpConnect()
{
  Debug(DEBUG_TAG, "[%s] calling httpconnect write", __FUNCTION__);
  sockaddr_in addr;
  ats_ip4_set(&addr, _ip, _port);
  http_vc = TSHttpConnect(ats_ip_sa_cast(&addr));

  PluginVC *vc = (PluginVC *) http_vc;

  read_vio = vc->do_io_read(this, INT64_MAX, resp_buffer);
  write_vio = vc->do_io_write(this, getReqLen(), req_reader);
}

char* FetchSM::resp_get(int *length) {
  *length = client_bytes;
  return client_response;
}

int FetchSM::InvokePlugin(int event, void *data)
{
  EThread *mythread = this_ethread();

  MUTEX_TAKE_LOCK(contp->mutex,mythread);

  int ret = contp->handleEvent(event,data);

  MUTEX_UNTAKE_LOCK(contp->mutex,mythread);

  return ret;
}
void
FetchSM::get_info_from_buffer(IOBufferReader *the_reader)
{
  char *info;
//  char *info_start;

  int64_t read_avail, read_done;
  IOBufferBlock *blk;
  char *buf;

  if (!the_reader)
    return ;

  read_avail = the_reader->read_avail();
  Debug(DEBUG_TAG, "[%s] total avail %" PRId64 , __FUNCTION__, read_avail);
  //size_t hdr_size = _headers.size();
  //info = (char *)ats_malloc(sizeof(char) * (read_avail+1) + hdr_size);
  info = (char *)ats_malloc(sizeof(char) * (read_avail+1));
  client_response = info;

  //ink_strlcpy(info, _headers.data(), sizeof(char) * (read_avail+1));
  //info += hdr_size;

  /* Read the data out of the reader */
  while (read_avail > 0) {
    if (the_reader->block != NULL)
      the_reader->skip_empty_blocks();
    blk = the_reader->block;

    // This is the equivalent of TSIOBufferBlockReadStart()
    buf = blk->start() + the_reader->start_offset;
    read_done = blk->read_avail() - the_reader->start_offset;

    if (read_done > 0) {
      memcpy(info, buf, read_done);
      the_reader->consume(read_done);
      read_avail -= read_done;
      info += read_done;
    }
  }
}

void
FetchSM::process_fetch_read(int event)
{
  Debug(DEBUG_TAG, "[%s] I am here read", __FUNCTION__);
  int64_t bytes;
  int bytes_used;
  int64_t total_bytes_copied = 0;

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    bytes = resp_reader->read_avail();
    Debug(DEBUG_TAG, "[%s] number of bytes in read ready %" PRId64"", __FUNCTION__, bytes);
    while (total_bytes_copied < bytes) {
       int64_t actual_bytes_copied;
       actual_bytes_copied = response_buffer->write(resp_reader, bytes, 0);
       if (actual_bytes_copied <= 0) {
           break;
       }
       total_bytes_copied += actual_bytes_copied;
    }
    resp_reader->consume(total_bytes_copied);
    if (header_done == 0 && callback_options == AFTER_HEADER) {
      if (client_response_hdr.parse_resp(&http_parser, response_reader, &bytes_used, 0) == PARSE_DONE) {
        //InvokePlugin( TS_EVENT_INTERNAL_60201, (void *) &client_response_hdr);
        InvokePlugin( callback_events.success_event_id, (void *) &client_response_hdr);
        header_done = 1;
      }
    }
    read_vio->reenable();
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    if(callback_options == AFTER_HEADER || callback_options == AFTER_BODY) {
    bytes = response_reader->read_avail();

    get_info_from_buffer(response_reader);
    Debug(DEBUG_TAG, "[%s] number of bytes %" PRId64"", __FUNCTION__, bytes);
    if(client_response!=NULL)
      client_response[bytes] = '\0';
      //client_response[bytes + _headers.size()] = '\0';
    Debug(DEBUG_TAG, "[%s] Completed data fetch of size %" PRId64", notifying caller", __FUNCTION__, bytes);
    //InvokePlugin( TS_EVENT_INTERNAL_60200, (void *) client_response);
   client_bytes = bytes;
    //InvokePlugin( TS_EVENT_INTERNAL_60200, (void *) this);
      InvokePlugin( callback_events.success_event_id, (void *) this);
    }

    Debug(DEBUG_TAG, "[%s] received EOS", __FUNCTION__);
    cleanUp();
    break;
  case TS_EVENT_ERROR:
  default:
    //InvokePlugin(TS_EVENT_ERROR, NULL);
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
    break;

  }
}

void
FetchSM::process_fetch_write(int event)
{
  Debug(DEBUG_TAG, "[%s] calling process write", __FUNCTION__);
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    //INKVConnShutdown(http_vc, 0, 1) ; why does not this work???
    req_finished = true;
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    // data is processed in chunks of 32k; if there is more than 32k
    // of input data, we have to continue reenabling until all data is
    // read (we have already written all the data to the buffer)
    ((PluginVC *) http_vc)->reenable(write_vio);
    break;
  case TS_EVENT_ERROR:
    //InvokePlugin( TS_EVENT_ERROR, NULL);
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
  default:
    break;
  }
}

int
FetchSM::fetch_handler(int event, void *edata)
{
  Debug(DEBUG_TAG, "[%s] calling fetch_plugin", __FUNCTION__);

  if (edata == read_vio) {
    process_fetch_read(event);
  } else if (edata == write_vio) {
    process_fetch_write(event);
  } else {
      InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
  }
  return 1;
}
