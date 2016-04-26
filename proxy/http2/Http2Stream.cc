/** @file

  Http2Stream.cc

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

#include "HTTP2.h"
#include "Http2Stream.h"
#include "Http2ClientSession.h"
#include "../http/HttpSM.h"

ClassAllocator<Http2Stream> http2StreamAllocator("http2StreamAllocator");

int
Http2Stream::main_event_handler(int event, void *edata)
{
  Event *e = static_cast<Event *>(edata);

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  if (e == cross_thread_event) {
    cross_thread_event = NULL;
  }

  if (e == active_event) {
    event = VC_EVENT_ACTIVE_TIMEOUT;
    active_event = NULL;
  } else if (e == inactive_event) {
    if (inactive_timeout_at && inactive_timeout_at < Thread::get_hrtime()) {
      event = VC_EVENT_INACTIVITY_TIMEOUT;
      clear_inactive_timer();
    }
  }
  switch (event) {
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    if (current_reader && read_vio.ntodo() > 0) {
      SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
      read_vio._cont->handleEvent(event, &read_vio);
    } else if (current_reader && write_vio.ntodo() > 0) {
      SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
      write_vio._cont->handleEvent(event, &write_vio);
    }
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    inactive_timeout_at = Thread::get_hrtime() + inactive_timeout;
    if (e->cookie == &write_vio) {
      if (write_vio.mutex) {
        SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
        if (write_vio._cont && this->current_reader)
          write_vio._cont->handleEvent(event, &write_vio);
      }
    } else {
      update_write_request(write_vio.get_reader(), INT64_MAX, true);
    }
    break;
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_READ_READY:
    inactive_timeout_at = Thread::get_hrtime() + inactive_timeout;
    if (e->cookie == &read_vio) {
      if (read_vio.mutex) {
        SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
        if (read_vio._cont && this->current_reader)
          read_vio._cont->handleEvent(event, &read_vio);
      }
    } else {
      this->update_read_request(INT64_MAX, true);
    }
    break;
  case VC_EVENT_EOS: {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    // Clean up after yourself if this was an EOS
    ink_release_assert(this->closed);
    this->destroy();
    break;
  }
  }
  return 0;
}

void
Http2Stream::send_request(Http2ConnectionState &cstate)
{
  // Convert header to HTTP/1.1 format
  http2_convert_header_from_2_to_1_1(&_req_header);

  // Write header to a buffer.  Borrowing logic from HttpSM::write_header_into_buffer.
  // Seems like a function like this ought to be in HTTPHdr directly
  int bufindex;
  int dumpoffset = 0;
  int done, tmp;
  IOBufferBlock *block;
  do {
    bufindex = 0;
    tmp = dumpoffset;
    block = request_buffer.get_current_block();
    if (!block) {
      request_buffer.add_block();
      block = request_buffer.get_current_block();
    }
    done = _req_header.print(block->start(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    request_buffer.fill(bufindex);
    if (!done) {
      request_buffer.add_block();
    }
  } while (!done);

  // Is there a read_vio request waiting?
  this->update_read_request(INT64_MAX, true);
}

bool
Http2Stream::change_state(uint8_t type, uint8_t flags)
{
  switch (_state) {
  case HTTP2_STREAM_STATE_IDLE:
    if (type == HTTP2_FRAME_TYPE_HEADERS) {
      if (end_stream && flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
        _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else {
        _state = HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_CONTINUATION) {
      if (end_stream && flags & HTTP2_FLAGS_CONTINUATION_END_HEADERS) {
        _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else {
        _state = HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_PUSH_PROMISE) {
      // XXX Server Push have been supported yet.
    } else {
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_OPEN:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM) {
      _state = HTTP2_STREAM_STATE_CLOSED;
    } else if (type == HTTP2_FRAME_TYPE_DATA && flags & HTTP2_FLAGS_DATA_END_STREAM) {
      _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
    } else {
      // Currently ATS supports only HTTP/2 server features
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_RESERVED_LOCAL:
    // Currently ATS supports only HTTP/2 server features
    return false;

  case HTTP2_STREAM_STATE_RESERVED_REMOTE:
    // XXX Server Push have been supported yet.
    return false;

  case HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL:
    // Currently ATS supports only HTTP/2 server features
    return false;

  case HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM || (type == HTTP2_FRAME_TYPE_HEADERS && flags & HTTP2_FLAGS_HEADERS_END_STREAM) ||
        (type == HTTP2_FRAME_TYPE_DATA && flags & HTTP2_FLAGS_DATA_END_STREAM)) {
      _state = HTTP2_STREAM_STATE_CLOSED;
    } else {
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_CLOSED:
    // No state changing
    return false;

  default:
    return false;
  }

  return true;
}

VIO *
Http2Stream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (buf) {
    read_vio.buffer.writer_for(buf);
  } else {
    read_vio.buffer.clear();
  }

  read_vio.mutex = c ? c->mutex : this->mutex;
  read_vio._cont = c;
  read_vio.nbytes = nbytes;
  read_vio.ndone = 0;
  read_vio.vc_server = this;
  read_vio.op = VIO::READ;

  // Is there already data in the request_buffer?  If so, copy it over and then
  // schedule a READ_READY or READ_COMPLETE event after we return.
  update_read_request(nbytes, true);

  return &read_vio;
}

VIO *
Http2Stream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner)
{
  if (abuffer) {
    write_vio.buffer.reader_for(abuffer);
  } else {
    write_vio.buffer.clear();
  }
  write_vio.mutex = c ? c->mutex : this->mutex;
  write_vio._cont = c;
  write_vio.nbytes = nbytes;
  write_vio.ndone = 0;
  write_vio.vc_server = this;
  write_vio.op = VIO::WRITE;
  response_reader = response_buffer.alloc_reader();
  return update_write_request(abuffer, nbytes, false) ? &write_vio : NULL;
}

// Initiated from SM
void
Http2Stream::do_io_close(int /* flags */)
{
  current_reader = NULL; // SM on the way out
  if (!sent_delete) {
    Debug("http2_stream", "do_io_close stream %d", this->get_id());

    // Only close if we are done sending data back to the client
    if (parent && (!this->is_body_done() || this->response_is_data_available())) {
      Debug("http2_stream", "%d: Undo close to pass data", this->get_id());
      closed = false;             // "unclose" so this gets picked up later when the netvc side is done
      this->reenable(&write_vio); // Kick the mechanism to get any remaining data pushed out
      return;
    }
    closed = true;
    sent_delete = true;

    if (parent) {
      // Make sure any trailing end of stream frames are sent
      static_cast<Http2ClientSession *>(parent)->connection_state.send_data_frame(this);

      // Remove ourselves from the stream list
      static_cast<Http2ClientSession *>(parent)->connection_state.delete_stream(this);
    }
    parent = NULL;

    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

    clear_timers();

    if (cross_thread_event != NULL)
      cross_thread_event->cancel();
    cross_thread_event = NULL;

    // Send an event to get the stream to kill itself
    // Thus if any events for the stream are in the queue, they will be handled first.
    // We have marked the stream closed, so no new events should be queued
    cross_thread_event = this_ethread()->schedule_imm(this, VC_EVENT_EOS);
  }
}

// Initiated from the Http2 side
void
Http2Stream::initiating_close()
{
  if (!closed) {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    Debug("http2_stream", "initiating_close stream %d", this->get_id());
    closed = true;
    // leaving the reference to the SM, so we can detatch from the SM
    // when we actually destroy
    // current_reader = NULL;

    parent = NULL;
    clear_timers();

    // This should result in do_io_close or release being called.  That will schedule the final
    // kill yourself signal
    // Send the SM the EOS signal
    bool sent_write_complete = false;
    if (current_reader) {
      // Push out any last IO events
      if (write_vio._cont) {
        SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
        // Are we done?
        if (write_vio.nbytes == write_vio.ndone) {
          Debug("http2_stream", "handle write from destroy stream=%d event=%d", this->_id, VC_EVENT_WRITE_COMPLETE);
          write_vio._cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &write_vio);
        } else {
          write_vio._cont->handleEvent(VC_EVENT_EOS, &write_vio);
          Debug("http2_stream", "handle write from destroy stream=%d event=%d", this->_id, VC_EVENT_EOS);
        }
        sent_write_complete = true;
      }
    }
    // Send EOS to let SM know that we aren't sticking around
    if (current_reader && read_vio._cont) {
      // Only bother with the EOS if we haven't sent the write complete
      if (!sent_write_complete) {
        SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
        Debug("http2_stream", "send EOS to read cont stream=%d", this->_id);
        read_vio._cont->handleEvent(VC_EVENT_EOS, &read_vio);
      }
    } else if (current_reader) {
      SCOPED_MUTEX_LOCK(lock, current_reader->mutex, this_ethread());
      current_reader->handleEvent(VC_EVENT_EOS);
    } else if (!sent_write_complete) {
      // Send an event to get the stream to kill itself
      // Thus if any events for the stream are in the queue, they will be handled first.
      // We have marked the stream closed, so no new events should be queued
      if (cross_thread_event != NULL)
        cross_thread_event->cancel();
      cross_thread_event = this_ethread()->schedule_imm(this, VC_EVENT_EOS);
    }
  }
}

void
Http2Stream::update_read_request(int64_t read_len, bool send_update)
{
  if (closed || this->current_reader == NULL)
    return;
  if (this->get_thread() != this_ethread()) {
    SCOPED_MUTEX_LOCK(stream_lock, this->mutex, this_ethread());
    if (cross_thread_event == NULL) {
      // Send to the right thread
      cross_thread_event = this->get_thread()->schedule_imm(this, VC_EVENT_READ_READY, NULL);
    }
    return;
  }
  ink_release_assert(this->get_thread() == this_ethread());
  if (send_update) {
    SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
    if (read_vio.nbytes > 0 && read_vio.ndone <= read_vio.nbytes) {
      // If this vio has a different buffer, we must copy
      ink_release_assert(this_ethread() == this->_thread);
      if (read_vio.buffer.writer() != (&request_buffer)) {
        int64_t num_to_read = read_vio.nbytes - read_vio.ndone;
        if (num_to_read > read_len)
          num_to_read = read_len;
        if (num_to_read > 0) {
          int bytes_added = read_vio.buffer.writer()->write(request_reader, num_to_read);
          if (bytes_added > 0) {
            request_reader->consume(bytes_added);
            read_vio.ndone += bytes_added;
            int send_event = (read_vio.nbytes == read_vio.ndone) ? VC_EVENT_READ_COMPLETE : VC_EVENT_READ_READY;
            this_ethread()->schedule_imm(this, send_event, &read_vio);
            // this->handleEvent(send_event, &read_vio);
          }
          ink_release_assert(!this->closed);
        }
      } else {
        // Try to be smart and only signal if there was additional data
        if (request_reader->read_avail() > 0) {
          int send_event = (read_vio.nbytes == read_vio.ndone) ? VC_EVENT_READ_COMPLETE : VC_EVENT_READ_READY;
          this_ethread()->schedule_imm(this, send_event, &read_vio);
          // this->handleEvent(send_event, &read_vio);
          ink_release_assert(!this->closed);
        }
      }
    }
  }
}

bool
Http2Stream::update_write_request(IOBufferReader *buf_reader, int64_t write_len, bool send_update)
{
  bool retval = true;
  if (closed || parent == NULL)
    return retval;
  if (this->get_thread() != this_ethread()) {
    SCOPED_MUTEX_LOCK(stream_lock, this->mutex, this_ethread());
    if (cross_thread_event == NULL) {
      // Send to the right thread
      cross_thread_event = this->get_thread()->schedule_imm(this, VC_EVENT_WRITE_READY, NULL);
    }
    return retval;
  }
  ink_release_assert(this->get_thread() == this_ethread());
  Http2ClientSession *parent = static_cast<Http2ClientSession *>(this->get_parent());
  // Copy over data in the abuffer into resp_buffer.  Then schedule a WRITE_READY or
  // WRITE_COMPLETE event
  SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
  int64_t total_added = 0;
  if (write_vio.nbytes > 0 && write_vio.ndone < write_vio.nbytes) {
    int64_t num_to_write = write_vio.nbytes - write_vio.ndone;
    if (num_to_write > write_len)
      num_to_write = write_len;
    int64_t bytes_avail = buf_reader->read_avail();
    if (bytes_avail > num_to_write)
      bytes_avail = num_to_write;
    while (total_added < bytes_avail) {
      int64_t bytes_added = response_buffer.write(buf_reader, bytes_avail);
      buf_reader->consume(bytes_added);
      total_added += bytes_added;
    }
  }
  bool is_done = (this->response_process_data());
  if (total_added > 0 || is_done) {
    write_vio.ndone += total_added;
    int send_event = (write_vio.nbytes == write_vio.ndone || is_done) ? VC_EVENT_WRITE_COMPLETE : VC_EVENT_WRITE_READY;

    // Process the new data
    if (!this->response_header_done) {
      // Still parsing the response_header
      int bytes_used = 0;
      int state = this->response_header.parse_resp(&http_parser, this->response_reader, &bytes_used, false);
      // this->response_reader->consume(bytes_used);
      switch (state) {
      case PARSE_DONE: {
        this->response_header_done = true;

        // Send the response header back
        parent->connection_state.send_headers_frame(this);

        // See if the response is chunked.  Set up the dechunking logic if it is
        this->response_initialize_data_handling();

        // If there is additional data, send it along in a data frame.  Or if this was header only
        // make sure to send the end of stream
        if (this->response_is_data_available() || send_event == VC_EVENT_WRITE_COMPLETE) {
          if (send_event != VC_EVENT_WRITE_COMPLETE) {
            this_ethread()->schedule_imm(this, VC_EVENT_WRITE_READY, &write_vio);
          } else {
            this->mark_body_done();
            retval = false;
          }
          // Send the data frame
          parent->connection_state.send_data_frame(this);
        }
        break;
      }
      case PARSE_CONT:
        // Let it ride for next time
        break;
      default:
        break;
      }
    } else {
      if (send_event == VC_EVENT_WRITE_COMPLETE) {
        // Defer sending the write complete until the send_data_frame has sent it all
        // this_ethread()->schedule_imm(this, send_event, &write_vio);
        this->mark_body_done();
        parent->connection_state.send_data_frame(this);
        retval = false;
      } else {
        this_ethread()->schedule_imm(this, VC_EVENT_WRITE_READY, &write_vio);
        parent->connection_state.send_data_frame(this);
        // write_vio._cont->handleEvent(send_event, &write_vio);
      }
    }

    Debug("http2_stream", "write update stream_id=%d event=%d", this->get_id(), send_event);
  }
  return retval;
}

void
Http2Stream::reenable(VIO *vio)
{
  if (this->parent) {
    if (vio->op == VIO::WRITE) {
      SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
      update_write_request(vio->get_reader(), INT64_MAX, true);
    } else if (vio->op == VIO::READ) {
      SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
      update_read_request(INT64_MAX, true);
    }
  }
}

void
Http2Stream::destroy()
{
  Debug("http2_stream", "Destroy stream %d. Sent %d bytes", this->_id, this->bytes_sent);

  // Clean up the write VIO in case of inactivity timeout
  this->do_io_write(NULL, 0, NULL);

  HTTP2_DECREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT, _thread);
  ink_hrtime end_time = Thread::get_hrtime();
  HTTP2_SUM_THREAD_DYN_STAT(HTTP2_STAT_TOTAL_TRANSACTIONS_TIME, _thread, end_time - _start_time);
  _req_header.destroy();
  response_header.destroy();

  // Drop references to all buffer data
  request_buffer.clear();
  response_buffer.clear();

  // Free the mutexes in the VIO
  read_vio.mutex.clear();
  write_vio.mutex.clear();

  if (header_blocks) {
    ats_free(header_blocks);
  }
  chunked_handler.clear();
  super::destroy();
  THREAD_FREE(this, http2StreamAllocator, this_ethread());
}

bool
check_stream_thread(Continuation *cont)
{
  Http2Stream *stream = dynamic_cast<Http2Stream *>(cont);
  if (stream) {
    return stream->get_thread() == this_ethread();
  } else
    return true;
}
bool
check_continuation(Continuation *cont)
{
  Http2Stream *stream = dynamic_cast<Http2Stream *>(cont);
  return stream == NULL;
}

bool
Http2Stream::response_initialize_data_handling()
{
  bool is_done = false;
  const char *name = "transfer-encoding";
  const char *value = "chunked";
  int chunked_index = response_header.value_get_index(name, strlen(name), value, strlen(value));
  // -1 means this value was not found for this field
  if (chunked_index >= 0) {
    Debug("http2_stream", "Response is chunked");
    chunked = true;
    this->chunked_handler.init_by_action(this->response_reader, ChunkedHandler::ACTION_DECHUNK);
    this->chunked_handler.state = ChunkedHandler::CHUNK_READ_SIZE;
    this->chunked_handler.dechunked_reader = this->chunked_handler.dechunked_buffer->alloc_reader();
    this->response_reader->dealloc();
    this->response_reader = NULL;
    // Get things going if there is already data waiting
    if (this->chunked_handler.chunked_reader->is_read_avail_more_than(0)) {
      is_done = response_process_data();
    }
  }
  return is_done;
}

bool
Http2Stream::response_process_data()
{
  bool done = false;
  if (chunked) {
    do {
      if (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL) {
        chunked_handler.state = ChunkedHandler::CHUNK_READ_SIZE_START;
      }
      done = this->chunked_handler.process_chunked_content();
    } while (chunked_handler.state == ChunkedHandler::CHUNK_FLOW_CONTROL);
  }
  return done;
}

bool
Http2Stream::response_is_data_available() const
{
  IOBufferReader *reader = this->response_get_data_reader();
  return reader ? reader->is_read_avail_more_than(0) : false;
}

IOBufferReader *
Http2Stream::response_get_data_reader() const
{
  return (chunked) ? chunked_handler.dechunked_reader : response_reader;
}

void
Http2Stream::set_active_timeout(ink_hrtime timeout_in)
{
  active_timeout = timeout_in;
  clear_active_timer();
  if (active_timeout > 0) {
    active_event = this_ethread()->schedule_in(this, active_timeout);
  }
}

void
Http2Stream::set_inactivity_timeout(ink_hrtime timeout_in)
{
  inactive_timeout = timeout_in;
  if (inactive_timeout > 0) {
    inactive_timeout_at = Thread::get_hrtime() + inactive_timeout;
    if (!inactive_event) {
      inactive_event = this_ethread()->schedule_every(this, HRTIME_SECONDS(1));
    }
  } else {
    clear_inactive_timer();
  }
}

void
Http2Stream::cancel_inactivity_timeout()
{
  set_inactivity_timeout(0);
}

void
Http2Stream::clear_inactive_timer()
{
  inactive_timeout_at = 0;
  if (inactive_event) {
    inactive_event->cancel();
    inactive_event = NULL;
  }
}
void
Http2Stream::clear_active_timer()
{
  if (active_event) {
    active_event->cancel();
    active_event = NULL;
  }
}
void
Http2Stream::clear_timers()
{
  clear_inactive_timer();
  clear_active_timer();
}
