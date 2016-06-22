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

#include "ts/ink_config.h"
#include "FetchSM.h"
#include <stdio.h>
#include "HTTP.h"
#include "PluginVC.h"

#define DEBUG_TAG "FetchSM"
#define FETCH_LOCK_RETRY_TIME HRTIME_MSECONDS(10)

ClassAllocator<FetchSM> FetchSMAllocator("FetchSMAllocator");
void
FetchSM::cleanUp()
{
  Debug(DEBUG_TAG, "[%s] calling cleanup", __FUNCTION__);

  if (!ink_atomic_cas(&destroyed, false, true)) {
    Debug(DEBUG_TAG, "Error: Double delete on FetchSM, this:%p", this);
    return;
  }

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
  http_vc->do_io_close();
  FetchSMAllocator.free(this);
}

void
FetchSM::httpConnect()
{
  PluginIdentity *pi = dynamic_cast<PluginIdentity *>(contp);
  char const *tag    = pi ? pi->getPluginTag() : "fetchSM";
  int64_t id         = pi ? pi->getPluginId() : 0;

  Debug(DEBUG_TAG, "[%s] calling httpconnect write pi=%p tag=%s id=%" PRId64, __FUNCTION__, pi, tag, id);
  http_vc = reinterpret_cast<PluginVC *>(TSHttpConnectWithPluginId(&_addr.sa, tag, id));

  /*
   * TS-2906: We need a way to unset internal request when using FetchSM, the use case for this
   * is SPDY when it creates outgoing requests it uses FetchSM and the outgoing requests
   * are spawned via SPDY SYN packets which are definitely not internal requests.
   */
  if (!is_internal_request) {
    PluginVC *other_side = reinterpret_cast<PluginVC *>(http_vc)->get_other_side();
    if (other_side != NULL) {
      other_side->set_is_internal_request(false);
    }
  }

  read_vio  = http_vc->do_io_read(this, INT64_MAX, resp_buffer);
  write_vio = http_vc->do_io_write(this, getReqLen() + req_content_length, req_reader);
}

char *
FetchSM::resp_get(int *length)
{
  *length = client_bytes;
  return client_response;
}

int
FetchSM::InvokePlugin(int event, void *data)
{
  EThread *mythread = this_ethread();

  MUTEX_TAKE_LOCK(contp->mutex, mythread);

  int ret = contp->handleEvent(event, data);

  MUTEX_UNTAKE_LOCK(contp->mutex, mythread);

  return ret;
}

bool
FetchSM::has_body()
{
  int status_code;
  HTTPHdr *hdr;

  if (!header_done)
    return false;

  if (is_method_head)
    return false;
  //
  // The following code comply with HTTP/1.1:
  // http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
  //

  hdr = &client_response_hdr;

  status_code = hdr->status_get();
  if (status_code < 200 || status_code == 204 || status_code == 304)
    return false;

  if (check_chunked())
    return true;

  resp_content_length = hdr->value_get_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
  if (!resp_content_length) {
    if (check_connection_close()) {
      return true;
    } else {
      return false;
    }
  }

  return true;
}

bool
FetchSM::check_body_done()
{
  if (!check_chunked()) {
    if (resp_content_length == resp_received_body_len + resp_reader->read_avail())
      return true;

    return false;
  }

  //
  // TODO: check whether the chunked body is done
  //
  return true;
}

bool
FetchSM::check_for_field_value(char const *name, size_t name_len, char const *value, size_t value_len)
{
  bool zret = false; // not found.
  StrList slist;
  HTTPHdr *hdr = &client_response_hdr;
  int ret      = hdr->value_get_comma_list(name, name_len, &slist);

  ink_release_assert(header_done);

  if (ret) {
    for (Str *f = slist.head; f != NULL; f = f->next) {
      if (f->len == value_len && 0 == strncasecmp(f->str, value, value_len)) {
        Debug(DEBUG_TAG, "[%s] field '%.*s', value '%.*s'", __FUNCTION__, static_cast<int>(name_len), name,
              static_cast<int>(value_len), value);
        zret = true;
        break;
      }
    }
  }
  return zret;
}

bool
FetchSM::check_chunked()
{
  static char const CHUNKED_TEXT[] = "chunked";
  static size_t const CHUNKED_LEN  = sizeof(CHUNKED_TEXT) - 1;

  if (resp_is_chunked < 0) {
    resp_is_chunked = static_cast<int>(
      this->check_for_field_value(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING, CHUNKED_TEXT, CHUNKED_LEN));

    if (resp_is_chunked && (fetch_flags & TS_FETCH_FLAGS_DECHUNK)) {
      ChunkedHandler *ch = &chunked_handler;
      ch->init_by_action(resp_reader, ChunkedHandler::ACTION_DECHUNK);
      ch->dechunked_reader = ch->dechunked_buffer->alloc_reader();
      ch->state            = ChunkedHandler::CHUNK_READ_SIZE;
      resp_reader->dealloc();
    }
  }
  return resp_is_chunked > 0;
}

bool
FetchSM::check_connection_close()
{
  static char const CLOSE_TEXT[] = "close";
  static size_t const CLOSE_LEN  = sizeof(CLOSE_TEXT) - 1;

  if (resp_received_close < 0) {
    resp_received_close =
      static_cast<int>(this->check_for_field_value(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, CLOSE_TEXT, CLOSE_LEN));
  }
  return resp_received_close > 0;
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
FetchSM::InvokePluginExt(int fetch_event)
{
  int event;
  EThread *mythread        = this_ethread();
  bool read_complete_event = (fetch_event == TS_EVENT_VCONN_READ_COMPLETE) || (fetch_event == TS_EVENT_VCONN_EOS);

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

  if (fetch_event && !read_complete_event) {
    contp->handleEvent(fetch_event, this);
    goto out;
  }

  if (!has_sent_header) {
    if (fetch_event != TS_EVENT_VCONN_EOS) {
      contp->handleEvent(TS_FETCH_EVENT_EXT_HEAD_DONE, this);
      has_sent_header = true;
    } else {
      contp->handleEvent(fetch_event, this);
      goto out;
    }
  }

  // TS-3112: always check 'contp' after handleEvent()
  // since handleEvent effectively calls the plugin (or SPDY layer)
  // which may call TSFetchDestroy in error conditions.
  // TSFetchDestroy sets contp to NULL, but, doesn't destroy FetchSM yet,
  // since, it¹s in a tight loop protected by 'recursion' counter.
  // When handleEvent returns, 'recursion' is decremented and contp is
  // already null, so, FetchSM gets destroyed.
  if (!contp)
    goto out;

  if (!has_body()) {
    contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
    goto out;
  }

  Debug(DEBUG_TAG, "[%s] chunked:%d, content_len: %" PRId64 ", received_len: %" PRId64 ", avail: %" PRId64 "\n", __FUNCTION__,
        resp_is_chunked, resp_content_length, resp_received_body_len,
        resp_is_chunked > 0 ? chunked_handler.chunked_reader->read_avail() : resp_reader->read_avail());

  if (resp_is_chunked > 0) {
    if (!chunked_handler.chunked_reader->read_avail()) {
      if (read_complete_event) {
        contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
      }
      goto out;
    }
  } else if (!resp_reader->read_avail()) {
    if (read_complete_event) {
      contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
    }
    goto out;
  }

  if (!check_chunked()) {
    if (!check_body_done() && !read_complete_event)
      contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_READY, this);
    else
      contp->handleEvent(TS_FETCH_EVENT_EXT_BODY_DONE, this);
  } else if (fetch_flags & TS_FETCH_FLAGS_DECHUNK) {
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

      // contp may be null after handleEvent
      if (!contp)
        goto out;

    } while (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL);
  } else if (check_body_done()) {
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
    return;
  }

  read_avail = reader->read_avail();
  Debug(DEBUG_TAG, "[%s] total avail %" PRId64, __FUNCTION__, read_avail);
  if (!read_avail) {
    client_bytes = 0;
    return;
  }

  info            = (char *)ats_malloc(sizeof(char) * (read_avail + 1));
  client_response = info;

  // To maintain backwards compatability we don't allow chunking when it's not streaming.
  if (!(fetch_flags & TS_FETCH_FLAGS_STREAM) || !check_chunked()) {
    /* Read the data out of the reader */
    while (read_avail > 0) {
      if (reader->block != NULL)
        reader->skip_empty_blocks();
      blk = reader->block;

      // This is the equivalent of TSIOBufferBlockReadStart()
      buf       = blk->start() + reader->start_offset;
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
      buf       = blk->start() + reader->start_offset;
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
  int64_t total_bytes_copied = 0;

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    // duplicate the bytes for backward compatibility with TSFetchUrl()
    if (!(fetch_flags & TS_FETCH_FLAGS_STREAM)) {
      bytes = resp_reader->read_avail();
      Debug(DEBUG_TAG, "[%s] number of bytes in read ready %" PRId64, __FUNCTION__, bytes);

      while (total_bytes_copied < bytes) {
        int64_t actual_bytes_copied;
        actual_bytes_copied = resp_buffer->write(resp_reader, bytes, 0);
        Debug(DEBUG_TAG, "[%s] copied %" PRId64 " bytes", __FUNCTION__, actual_bytes_copied);
        if (actual_bytes_copied <= 0) {
          break;
        }
        total_bytes_copied += actual_bytes_copied;
      }
      Debug(DEBUG_TAG, "[%s] total copied %" PRId64 " bytes", __FUNCTION__, total_bytes_copied);
      resp_reader->consume(total_bytes_copied);
    }

    if (header_done == 0 && ((fetch_flags & TS_FETCH_FLAGS_STREAM) || callback_options == AFTER_HEADER)) {
      if (client_response_hdr.parse_resp(&http_parser, resp_reader, &bytes_used, 0) == PARSE_DONE) {
        header_done = 1;
        if (fetch_flags & TS_FETCH_FLAGS_STREAM)
          return InvokePluginExt();
        else
          InvokePlugin(callback_events.success_event_id, (void *)&client_response_hdr);
      }
    } else {
      if (fetch_flags & TS_FETCH_FLAGS_STREAM)
        return InvokePluginExt();
    }
    read_vio->reenable();
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt(event);
    if (callback_options == AFTER_HEADER || callback_options == AFTER_BODY) {
      get_info_from_buffer(resp_reader);
      InvokePlugin(callback_events.success_event_id, (void *)this);
    }
    Debug(DEBUG_TAG, "[%s] received EOS", __FUNCTION__);
    cleanUp();
    break;
  case TS_EVENT_ERROR:
  default:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt(event);
    InvokePlugin(callback_events.failure_event_id, NULL);
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
      ((PluginVC *)http_vc)->reenable(write_vio);
    break;
  case TS_EVENT_ERROR:
    if (fetch_flags & TS_FETCH_FLAGS_STREAM)
      return InvokePluginExt(event);
    InvokePlugin(callback_events.failure_event_id, NULL);
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
    InvokePlugin(callback_events.failure_event_id, NULL);
    cleanUp();
  }
  return 1;
}

void
FetchSM::ext_init(Continuation *cont, const char *method, const char *url, const char *version, const sockaddr *client_addr,
                  int flags)
{
  init_comm();

  if (flags & TS_FETCH_FLAGS_NEWLOCK) {
    mutex      = new_ProxyMutex();
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
  if (fetch_flags & TS_FETCH_FLAGS_NOT_INTERNAL_REQUEST) {
    set_internal_request(false);
  }

  //
  // These options are not used when enable
  // stream IO.
  //
  memset(&callback_options, 0, sizeof(callback_options));
  memset(&callback_events, 0, sizeof(callback_events));

  int method_len = strlen(method);
  req_buffer->write(method, method_len);
  req_buffer->write(" ", 1);
  req_buffer->write(url, strlen(url));
  req_buffer->write(" ", 1);
  req_buffer->write(version, strlen(version));
  req_buffer->write("\r\n", 2);

  if ((method_len == HTTP_LEN_HEAD) && !memcmp(method, HTTP_METHOD_HEAD, HTTP_LEN_HEAD)) {
    is_method_head = true;
  }
}

void
FetchSM::ext_add_header(const char *name, int name_len, const char *value, int value_len)
{
  if (TS_MIME_LEN_CONTENT_LENGTH == name_len && !strncasecmp(TS_MIME_FIELD_CONTENT_LENGTH, name, name_len)) {
    req_content_length = atoll(value);
  }

  req_buffer->write(name, name_len);
  req_buffer->write(": ", 2);
  req_buffer->write(value, value_len);
  req_buffer->write("\r\n", 2);
}

void
FetchSM::ext_launch()
{
  req_buffer->write("\r\n", 2);
  httpConnect();
}

void
FetchSM::ext_write_data(const void *data, size_t len)
{
  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
    MUTEX_TAKE_LOCK(mutex, this_ethread());
  }
  req_buffer->write(data, len);

  Debug(DEBUG_TAG, "[%s] re-enabling write_vio, header_done %u", __FUNCTION__, header_done);
  write_vio->reenable();

  if (fetch_flags & TS_FETCH_FLAGS_NEWLOCK) {
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
    if (!lock.is_locked())
      return 0;
  }

  if (!header_done)
    return 0;

  if (check_chunked() && (fetch_flags & TS_FETCH_FLAGS_DECHUNK))
    reader = (tsapi_bufferreader *)chunked_handler.dechunked_reader;
  else
    reader = (TSIOBufferReader)resp_reader;

  already = 0;
  blk     = TSIOBufferReaderStart(reader);

  while (blk) {
    wavail = len - already;

    next_blk = TSIOBufferBlockNext(blk);
    start    = TSIOBufferBlockReadStart(blk, reader, &blk_len);

    need = blk_len > wavail ? wavail : blk_len;

    memcpy(&buf[already], start, need);
    already += need;

    if (already >= (int64_t)len)
      break;

    blk = next_blk;
  }

  resp_received_body_len += already;
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
    if (!lock.is_locked()) {
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

void *
FetchSM::ext_get_user_data()
{
  return user_data;
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
