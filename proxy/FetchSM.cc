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

static const char *http_method[] = {
  "NONE",
  "GET",
  "POST",
  "CONNECT",
  "DELETE",
  "HEAD",
  "PURGE",
  "PUT",
  "LAST",
};

#define DEBUG_TAG "FetchSM"
#define FETCH_LOCK_RETRY_TIME HRTIME_MSECONDS(10)

ClassAllocator < FetchSM > FetchSMAllocator("FetchSMAllocator");
void
FetchSM::cleanUp()
{
  Debug(DEBUG_TAG, "[%s] calling cleanup", __FUNCTION__);

  if (resp_is_chunked > 0 && (fetch_flags & TS_FETCH_FLAGS_DECHUNK)) {
    chunked_handler.clear();
   }

  free_MIOBuffer(req_buffer);
  free_MIOBuffer(resp_buffer);
  mutex.clear();
  http_parser_clear(&http_parser);
  client_response_hdr.destroy();
  ats_free(client_response);
  cont_mutex.clear();

  PluginVC *vc = (PluginVC *) http_vc;

  vc->do_io_close();
  FetchSMAllocator.free(this);
}

void
FetchSM::httpConnect()
{
  Debug(DEBUG_TAG, "[%s] calling httpconnect write", __FUNCTION__);
  http_vc = TSHttpConnectWithProtoStack(&_addr.sa, proto_stack);

  PluginVC *vc = (PluginVC *) http_vc;

  read_vio = vc->do_io_read(this, INT64_MAX, resp_buffer);
  write_vio = vc->do_io_write(this, getReqLen() + req_content_length, req_reader);
}

char* FetchSM::resp_get(int *length) {
  *length = client_bytes;
  return client_response;
}

int
FetchSM::InvokePlugin(int event, void *data)
{
  EThread *mythread = this_ethread();

  MUTEX_TAKE_LOCK(contp->mutex,mythread);

  int ret = contp->handleEvent(event,data);

  MUTEX_UNTAKE_LOCK(contp->mutex,mythread);

  return ret;
}

bool
FetchSM::has_body()
{
  int status_code;
  HTTPHdr *hdr;

  if (!header_done)
    return false;

  //
  // The following code comply with HTTP/1.1:
  // http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
  //

  if (req_method == TS_FETCH_METHOD_HEAD)
    return false;

  hdr = &client_response_hdr;

  status_code = hdr->status_get();
  if (status_code < 200 || status_code == 204 || status_code == 304)
    return false;

  if (check_chunked())
    return true;

  resp_content_length = hdr->value_get_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
  if (!resp_content_length)
    return false;

  return true;
}

bool
FetchSM::check_body_done()
{
  if (!check_chunked()) {
    if (resp_content_length == resp_recived_body_len + resp_reader->read_avail())
      return true;

    return false;
  }

  //
  // TODO: check whether the chunked body is done
  //
  return true;
}

bool
FetchSM::check_chunked()
{
  int ret;
  StrList slist;
  HTTPHdr *hdr = &client_response_hdr;

  if (resp_is_chunked >= 0)
    return resp_is_chunked;

  ink_release_assert(header_done);

  resp_is_chunked = 0;
  ret = hdr->value_get_comma_list(MIME_FIELD_TRANSFER_ENCODING,
                                  MIME_LEN_TRANSFER_ENCODING, &slist);
  if (ret) {
    for (Str *f = slist.head; f != NULL; f = f->next) {
      if (f->len == 0)
        continue;

      size_t len = sizeof("chunked") - 1;
      len = len > f->len ? f->len : len;
      if (!strncasecmp(f->str, "chunked", len)) {
        resp_is_chunked = 1;
        if (fetch_flags & TS_FETCH_FLAGS_DECHUNK) {
          ChunkedHandler *ch = &chunked_handler;
          ch->init_by_action(resp_reader, ChunkedHandler::ACTION_DECHUNK);
          ch->dechunked_reader = ch->dechunked_buffer->alloc_reader();
          ch->state = ChunkedHandler::CHUNK_READ_SIZE;
          resp_reader->dealloc();
        }
        return true;
      }
    }
  }

  return resp_is_chunked;
}

int
FetchSM::dechunk_body()
{
  ink_assert(resp_is_chunked > 0);
  //
  // Return Value:
  //  - 0: need to read more data.
  //  - TS_FETCH_EVENT_EXT_BODY_READY.
  //  - TS_FETCH_EVENT_EXT_BODY_DONE.
  //
  if (chunked_handler.process_chunked_content())
    return TS_FETCH_EVENT_EXT_BODY_DONE;

  if (chunked_handler.dechunked_reader->read_avail())
    return TS_FETCH_EVENT_EXT_BODY_READY;

  return 0;
}

void
FetchSM::InvokePluginExt(int error_event)
{
  int event;
  EThread *mythread = this_ethread();

  //
  // Increasing *recursion* to prevent
  // FetchSM being deleted by callback.
  //
  recursion++;

  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
    MUTEX_TAKE_LOCK(cont_mutex, mythread);
  }

  if (!contp)
    goto out;

  if (error_event) {
    contp->handleEvent(error_event, this);
    goto out;
  }

  if (!has_sent_header) {
    contp->handleEvent(TS_FETCH_EVENT_EXT_HEAD_DONE, this);
    has_sent_header = true;
  }

  if (!has_body()) {
    contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
    goto out;
  }

  Debug(DEBUG_TAG, "[%s] chunked:%d, content_len: %" PRId64 ", recived_len: %" PRId64 ", avail: %" PRId64 "\n",
        __FUNCTION__, resp_is_chunked, resp_content_length, resp_recived_body_len,
        resp_is_chunked > 0 ? chunked_handler.chunked_reader->read_avail() : resp_reader->read_avail());

  if (resp_is_chunked > 0) {
    if (!chunked_handler.chunked_reader->read_avail())
      goto out;
  } else if (!resp_reader->read_avail()) {
      goto out;
  }

  if (!check_chunked()) {
    if (!check_body_done())
      contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_READY, this);
    else
      contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
  } else if (fetch_flags & TS_FETCH_FLAGS_DECHUNK){
    do {
      if (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL) {
        chunked_handler.state = ChunkedHandler::CHUNK_READ_SIZE_START;
      }

      event = dechunk_body();
      if (!event) {
        read_vio->reenable();
        goto out;
      }

      contp->handleEvent(event, this);
    } while (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL);
  } else if (check_body_done()){
    contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
  } else {
    contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_READY, this);
  }

out:
  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
    MUTEX_UNTAKE_LOCK(cont_mutex, mythread);
  }
  recursion--;

  if (!contp && !recursion)
    cleanUp();

  return;
}

void
FetchSM::get_info_from_buffer(IOBufferReader *the_reader)
{
  char *buf, *info;
  int64_t read_avail, read_done;
  IOBufferBlock *blk;
  IOBufferReader *reader = the_reader;

  if (!reader) {
    client_bytes = 0;
    return ;
  }

  read_avail = reader->read_avail();
  Debug(DEBUG_TAG, "[%s] total avail %" PRId64 , __FUNCTION__, read_avail);
  if (!read_avail) {
    client_bytes = 0;
    return;
  }

  info = (char *)ats_malloc(sizeof(char) * (read_avail+1));
  client_response = info;

  if (!check_chunked()) {
    /* Read the data out of the reader */
    while (read_avail > 0) {
      if (reader->block != NULL)
        reader->skip_empty_blocks();
      blk = reader->block;

      // This is the equivalent of TSIOBufferBlockReadStart()
      buf = blk->start() + reader->start_offset;
      read_done = blk->read_avail() - reader->start_offset;

      if (read_done > 0) {
        memcpy(info, buf, read_done);
        reader->consume(read_done);
        read_avail -= read_done;
        info += read_done;
        client_bytes += read_done;
      }
    }
    client_response[client_bytes] = '\0';
    return;
  }

  reader = chunked_handler.dechunked_reader;
  do {
    if (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL) {
      chunked_handler.state = ChunkedHandler::CHUNK_READ_SIZE_START;
    }

    if (!dechunk_body())
      break;

    /* Read the data out of the reader */
    read_avail = reader->read_avail();
    while (read_avail > 0) {
      if (reader->block != NULL)
        reader->skip_empty_blocks();
      blk = reader->block;

      // This is the equivalent of TSIOBufferBlockReadStart()
      buf = blk->start() + reader->start_offset;
      read_done = blk->read_avail() - reader->start_offset;

      if (read_done > 0) {
        memcpy(info, buf, read_done);
        reader->consume(read_done);
        read_avail -= read_done;
        info += read_done;
        client_bytes += read_done;
      }
    }
  } while (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL);

  client_response[client_bytes] = '\0';
  return;
}

void
FetchSM::process_fetch_read(int event)
{
  Debug(DEBUG_TAG, "[%s] I am here read", __FUNCTION__);
  int64_t bytes;
  int bytes_used;

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    bytes = resp_reader->read_avail();
    Debug(DEBUG_TAG, "[%s] number of bytes in read ready %" PRId64, __FUNCTION__, bytes);
    if (header_done == 0 && ((fetch_flags & TS_FETCH_FLAGS_STREAM) || callback_options == AFTER_HEADER)) {
      if (client_response_hdr.parse_resp(&http_parser, resp_reader, &bytes_used, 0) == PARSE_DONE) {
        header_done = 1;
        if (fetch_flags & TS_FETCH_FLAGS_STREAM)
          return InvokePluginExt();
        else
          InvokePlugin( callback_events.success_event_id, (void *) &client_response_hdr);
      }
    } else {
      if (fetch_flags & TS_FETCH_FLAGS_STREAM)
        return InvokePluginExt();
      else
        InvokePlugin(TS_FETCH_EVENT_EXT_BODY_READY, this);
    }
    read_vio->reenable();
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt();
    if(callback_options == AFTER_HEADER || callback_options == AFTER_BODY) {
      get_info_from_buffer(resp_reader);
      InvokePlugin( callback_events.success_event_id, (void *) this);
    }
    Debug(DEBUG_TAG, "[%s] received EOS", __FUNCTION__);
    cleanUp();
    break;
  case TS_EVENT_ERROR:
  default:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt(event);
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
    req_finished = true;
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    // data is processed in chunks of 32k; if there is more than 32k
    // of input data, we have to continue reenabling until all data is
    // read (we have already written all the data to the buffer)
    if (req_reader->read_avail() > 0)
      ((PluginVC *) http_vc)->reenable(write_vio);
    break;
  case TS_EVENT_ERROR:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt(event);
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
    if (fetch_flags & TS_FETCH_FLAGS_STREAM) {
      InvokePluginExt(event);
      return 1;
    }
    InvokePlugin( callback_events.failure_event_id, NULL);
    cleanUp();
  }
  return 1;
}

void
FetchSM::ext_init(Continuation *cont, TSFetchMethod method,
                  const char *url, const char *version,
                  const sockaddr *client_addr, int flags)
{
  init_comm();

  if (flags & TS_FETCH_FLAGS_NEWLOCK) {
    mutex = new_ProxyMutex();
    cont_mutex = cont->mutex;
  } else {
    mutex = cont->mutex;
  }

  contp = cont;
  _addr.assign(client_addr);

  //
  // Enable stream IO automatically.
  //
  fetch_flags = (TS_FETCH_FLAGS_STREAM | flags);

  //
  // These options are not used when enable
  // stream IO.
  //
  memset(&callback_options, 0, sizeof(callback_options));
  memset(&callback_events, 0, sizeof(callback_events));

  req_method = method;
  req_buffer->write(http_method[method], strlen(http_method[method]));
  req_buffer->write(" ", 1);
  req_buffer->write(url, strlen(url));
  req_buffer->write(" ", 1);
  req_buffer->write(version, strlen(version));
  req_buffer->write("\r\n", 2);
}

void
FetchSM::ext_add_header(const char *name, int name_len,
                        const char *value, int value_len)
{
  if (TS_MIME_LEN_CONTENT_LENGTH == name_len &&
      !strncasecmp(TS_MIME_FIELD_CONTENT_LENGTH, name, name_len)) {
    req_content_length = atoll(value);
  }

  req_buffer->write(name, name_len);
  req_buffer->write(": ", 2);
  req_buffer->write(value, value_len);
  req_buffer->write("\r\n", 2);
}

void
FetchSM::ext_lanuch()
{
  req_buffer->write("\r\n", 2);
  httpConnect();
}

void
FetchSM::ext_write_data(const void *data, size_t len)
{
  if (header_done && (fetch_flags & TS_FETCH_FLAGS_NEWLOCK)) {
    MUTEX_TAKE_LOCK(mutex, this_ethread());
  }

  req_buffer->write(data, len);

  //
  // Before header_done, FetchSM may not
  // be initialized.
  //
  if (header_done)
    write_vio->reenable();

  if (header_done && (fetch_flags & TS_FETCH_FLAGS_NEWLOCK)) {
    MUTEX_UNTAKE_LOCK(mutex, this_ethread());
  }
}

ssize_t
FetchSM::ext_read_data(char *buf, size_t len)
{
  const char *start;
  TSIOBufferReader reader;
  TSIOBufferBlock blk, next_blk;
  int64_t already, blk_len, need, wavail;

  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
    MUTEX_TRY_LOCK(lock, mutex, this_ethread());
    if (!lock)
      return 0;
  }

  if (!header_done)
    return 0;

  if (check_chunked() && (fetch_flags & TS_FETCH_FLAGS_DECHUNK))
    reader = (tsapi_bufferreader*)chunked_handler.dechunked_reader;
  else
    reader = (TSIOBufferReader)resp_reader;

  already = 0;
  blk = TSIOBufferReaderStart(reader);

  while (blk) {

    wavail = len - already;

    next_blk = TSIOBufferBlockNext(blk);
    start = TSIOBufferBlockReadStart(blk, reader, &blk_len);

    need = blk_len > wavail ? wavail : blk_len;

    memcpy(&buf[already], start, need);
    already += need;

    if (already >= (int64_t)len)
      break;

    blk = next_blk;
  }

  resp_recived_body_len += already;
  TSIOBufferReaderConsume(reader, already);

  read_vio->reenable();
  return already;
}

void
FetchSM::ext_destroy()
{
  contp = NULL;

  if (recursion)
    return;

  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
    MUTEX_TRY_LOCK(lock, mutex, this_ethread());
    if (!lock) {
      eventProcessor.schedule_in(this, FETCH_LOCK_RETRY_TIME);
      return;
    }
  }

  cleanUp();
}

void
FetchSM::ext_set_user_data(void *data)
{
  user_data = data;
}

void*
FetchSM::ext_get_user_data()
{
  return user_data;
}

void
FetchSM::ext_set_proto_stack(TSClientProtoStack proto_stack)
{
  this->proto_stack = proto_stack;
}

TSClientProtoStack
FetchSM::ext_get_proto_stack()
{
  return proto_stack;
}

TSMBuffer
FetchSM::resp_hdr_bufp()
{
  HdrHeapSDKHandle *heap;
  heap = (HdrHeapSDKHandle *)&client_response_hdr;

  return (TSMBuffer)heap;
}

TSMLoc
FetchSM::resp_hdr_mloc()
{
  return (TSMLoc)client_response_hdr.m_http;
}
