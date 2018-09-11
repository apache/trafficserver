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

/****************************************************************************

   HttpTunnel.cc

   Description:


****************************************************************************/

#include "tscore/ink_config.h"
#include "HttpConfig.h"
#include "HttpTunnel.h"
#include "HttpSM.h"
#include "HttpDebugNames.h"
#include "tscore/ParseRules.h"

static const int min_block_transfer_bytes = 256;
static const char *const CHUNK_HEADER_FMT = "%" PRIx64 "\r\n";
// This should be as small as possible because it will only hold the
// header and trailer per chunk - the chunk body will be a reference to
// a block in the input stream.
static int const CHUNK_IOBUFFER_SIZE_INDEX = MIN_IOBUFFER_SIZE;

ChunkedHandler::ChunkedHandler()
  : action(ACTION_UNSET),
    chunked_reader(nullptr),
    dechunked_buffer(nullptr),
    dechunked_size(0),
    dechunked_reader(nullptr),
    chunked_buffer(nullptr),
    chunked_size(0),
    truncation(false),
    skip_bytes(0),
    state(CHUNK_READ_CHUNK),
    cur_chunk_size(0),
    bytes_left(0),
    last_server_event(VC_EVENT_NONE),
    running_sum(0),
    num_digits(0),
    max_chunk_size(DEFAULT_MAX_CHUNK_SIZE),
    max_chunk_header_len(0)
{
}

void
ChunkedHandler::init(IOBufferReader *buffer_in, HttpTunnelProducer *p)
{
  if (p->do_chunking) {
    init_by_action(buffer_in, ACTION_DOCHUNK);
  } else if (p->do_dechunking) {
    init_by_action(buffer_in, ACTION_DECHUNK);
  } else {
    init_by_action(buffer_in, ACTION_PASSTHRU);
  }
  return;
}

void
ChunkedHandler::init_by_action(IOBufferReader *buffer_in, Action action)
{
  running_sum    = 0;
  num_digits     = 0;
  cur_chunk_size = 0;
  bytes_left     = 0;
  truncation     = false;
  this->action   = action;

  switch (action) {
  case ACTION_DOCHUNK:
    dechunked_reader                   = buffer_in->mbuf->clone_reader(buffer_in);
    dechunked_reader->mbuf->water_mark = min_block_transfer_bytes;
    chunked_buffer                     = new_MIOBuffer(CHUNK_IOBUFFER_SIZE_INDEX);
    chunked_size                       = 0;
    break;
  case ACTION_DECHUNK:
    chunked_reader   = buffer_in->mbuf->clone_reader(buffer_in);
    dechunked_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_256);
    dechunked_size   = 0;
    break;
  case ACTION_PASSTHRU:
    chunked_reader = buffer_in->mbuf->clone_reader(buffer_in);
    break;
  default:
    ink_release_assert(!"Unknown action");
  }

  return;
}

void
ChunkedHandler::clear()
{
  switch (action) {
  case ACTION_DOCHUNK:
    free_MIOBuffer(chunked_buffer);
    break;
  case ACTION_DECHUNK:
    free_MIOBuffer(dechunked_buffer);
    break;
  case ACTION_PASSTHRU:
  default:
    break;
  }

  return;
}

void
ChunkedHandler::set_max_chunk_size(int64_t size)
{
  max_chunk_size       = size ? size : DEFAULT_MAX_CHUNK_SIZE;
  max_chunk_header_len = snprintf(max_chunk_header, sizeof(max_chunk_header), CHUNK_HEADER_FMT, max_chunk_size);
}

void
ChunkedHandler::read_size()
{
  int64_t bytes_used;
  bool done = false;

  while (chunked_reader->read_avail() > 0 && !done) {
    const char *tmp   = chunked_reader->start();
    int64_t data_size = chunked_reader->block_read_avail();

    ink_assert(data_size > 0);
    bytes_used = 0;

    while (data_size > 0) {
      bytes_used++;
      if (state == CHUNK_READ_SIZE) {
        // The http spec says the chunked size is always in hex
        if (ParseRules::is_hex(*tmp)) {
          num_digits++;
          running_sum *= 16;

          if (ParseRules::is_digit(*tmp)) {
            running_sum += *tmp - '0';
          } else {
            running_sum += ParseRules::ink_tolower(*tmp) - 'a' + 10;
          }
        } else {
          // We are done parsing size
          if (num_digits == 0 || running_sum < 0) {
            // Bogus chunk size
            state = CHUNK_READ_ERROR;
            done  = true;
            break;
          } else {
            state = CHUNK_READ_SIZE_CRLF; // now look for CRLF
          }
        }
      } else if (state == CHUNK_READ_SIZE_CRLF) { // Scan for a linefeed
        if (ParseRules::is_lf(*tmp)) {
          Debug("http_chunk", "read chunk size of %d bytes", running_sum);
          bytes_left = (cur_chunk_size = running_sum);
          state      = (running_sum == 0) ? CHUNK_READ_TRAILER_BLANK : CHUNK_READ_CHUNK;
          done       = true;
          break;
        }
      } else if (state == CHUNK_READ_SIZE_START) {
        if (ParseRules::is_lf(*tmp)) {
          running_sum = 0;
          num_digits  = 0;
          state       = CHUNK_READ_SIZE;
        }
      }
      tmp++;
      data_size--;
    }
    chunked_reader->consume(bytes_used);
  }
}

// int ChunkedHandler::transfer_bytes()
//
//   Transfer bytes from chunked_reader to dechunked buffer
//   Use block reference method when there is a sufficient
//   size to move.  Otherwise, uses memcpy method
//
int64_t
ChunkedHandler::transfer_bytes()
{
  int64_t block_read_avail, moved, to_move, total_moved = 0;

  // Handle the case where we are doing chunked passthrough.
  if (!dechunked_buffer) {
    moved = std::min(bytes_left, chunked_reader->read_avail());
    chunked_reader->consume(moved);
    bytes_left = bytes_left - moved;
    return moved;
  }

  while (bytes_left > 0) {
    block_read_avail = chunked_reader->block_read_avail();

    to_move = std::min(bytes_left, block_read_avail);
    if (to_move <= 0) {
      break;
    }

    if (to_move >= min_block_transfer_bytes) {
      moved = dechunked_buffer->write(chunked_reader, bytes_left);
    } else {
      // Small amount of data available.  We want to copy the
      // data rather than block reference to prevent the buildup
      // of too many small blocks which leads to stack overflow
      // on deallocation
      moved = dechunked_buffer->write(chunked_reader->start(), to_move);
    }

    if (moved > 0) {
      chunked_reader->consume(moved);
      bytes_left = bytes_left - moved;
      dechunked_size += moved;
      total_moved += moved;
    } else {
      break;
    }
  }
  return total_moved;
}

void
ChunkedHandler::read_chunk()
{
  int64_t b = transfer_bytes();

  ink_assert(bytes_left >= 0);
  if (bytes_left == 0) {
    Debug("http_chunk", "completed read of chunk of %" PRId64 " bytes", cur_chunk_size);

    state = CHUNK_READ_SIZE_START;
  } else if (bytes_left > 0) {
    Debug("http_chunk", "read %" PRId64 " bytes of an %" PRId64 " chunk", b, cur_chunk_size);
  }
}

void
ChunkedHandler::read_trailer()
{
  int64_t bytes_used;
  bool done = false;

  while (chunked_reader->is_read_avail_more_than(0) && !done) {
    const char *tmp   = chunked_reader->start();
    int64_t data_size = chunked_reader->block_read_avail();

    ink_assert(data_size > 0);
    for (bytes_used = 0; data_size > 0; data_size--) {
      bytes_used++;

      if (ParseRules::is_cr(*tmp)) {
        // For a CR to signal we are almost done, the preceding
        //  part of the line must be blank and next character
        //  must a LF
        state = (state == CHUNK_READ_TRAILER_BLANK) ? CHUNK_READ_TRAILER_CR : CHUNK_READ_TRAILER_LINE;
      } else if (ParseRules::is_lf(*tmp)) {
        // For a LF to signal we are done reading the
        //   trailer, the line must have either been blank
        //   or must have have only had a CR on it
        if (state == CHUNK_READ_TRAILER_CR || state == CHUNK_READ_TRAILER_BLANK) {
          state = CHUNK_READ_DONE;
          Debug("http_chunk", "completed read of trailers");
          done = true;
          break;
        } else {
          // A LF that does not terminate the trailer
          //  indicates a new line
          state = CHUNK_READ_TRAILER_BLANK;
        }
      } else {
        // A character that is not a CR or LF indicates
        //  the we are parsing a line of the trailer
        state = CHUNK_READ_TRAILER_LINE;
      }
      tmp++;
    }
    chunked_reader->consume(bytes_used);
  }
}

bool
ChunkedHandler::process_chunked_content()
{
  while (chunked_reader->is_read_avail_more_than(0) && state != CHUNK_READ_DONE && state != CHUNK_READ_ERROR) {
    switch (state) {
    case CHUNK_READ_SIZE:
    case CHUNK_READ_SIZE_CRLF:
    case CHUNK_READ_SIZE_START:
      read_size();
      break;
    case CHUNK_READ_CHUNK:
      read_chunk();
      break;
    case CHUNK_READ_TRAILER_BLANK:
    case CHUNK_READ_TRAILER_CR:
    case CHUNK_READ_TRAILER_LINE:
      read_trailer();
      break;
    case CHUNK_FLOW_CONTROL:
      return false;
    default:
      ink_release_assert(0);
      break;
    }
  }
  return (state == CHUNK_READ_DONE || state == CHUNK_READ_ERROR);
}

bool
ChunkedHandler::generate_chunked_content()
{
  char tmp[16];
  bool server_done = false;
  int64_t r_avail;

  ink_assert(max_chunk_header_len);

  switch (last_server_event) {
  case VC_EVENT_EOS:
  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    server_done = true;
    break;
  }

  while ((r_avail = dechunked_reader->read_avail()) > 0 && state != CHUNK_WRITE_DONE) {
    int64_t write_val = std::min(max_chunk_size, r_avail);

    state = CHUNK_WRITE_CHUNK;
    Debug("http_chunk", "creating a chunk of size %" PRId64 " bytes", write_val);

    // Output the chunk size.
    if (write_val != max_chunk_size) {
      int len = snprintf(tmp, sizeof(tmp), CHUNK_HEADER_FMT, write_val);
      chunked_buffer->write(tmp, len);
      chunked_size += len;
    } else {
      chunked_buffer->write(max_chunk_header, max_chunk_header_len);
      chunked_size += max_chunk_header_len;
    }

    // Output the chunk itself.
    //
    // BZ# 54395 Note - we really should only do a
    //   block transfer if there is sizable amount of
    //   data (like we do for the case where we are
    //   removing chunked encoding in ChunkedHandler::transfer_bytes()
    //   However, I want to do this fix with as small a risk
    //   as possible so I'm leaving this issue alone for
    //   now
    //
    chunked_buffer->write(dechunked_reader, write_val);
    chunked_size += write_val;
    dechunked_reader->consume(write_val);

    // Output the trailing CRLF.
    chunked_buffer->write("\r\n", 2);
    chunked_size += 2;
  }

  if (server_done) {
    state = CHUNK_WRITE_DONE;

    // Add the chunked transfer coding trailer.
    chunked_buffer->write("0\r\n\r\n", 5);
    chunked_size += 5;
    return true;
  }
  return false;
}

HttpTunnelProducer::HttpTunnelProducer()
  : consumer_list(),
    self_consumer(nullptr),
    vc(nullptr),
    vc_handler(nullptr),
    read_vio(nullptr),
    read_buffer(nullptr),
    buffer_start(nullptr),
    vc_type(HT_HTTP_SERVER),
    chunking_action(TCA_PASSTHRU_DECHUNKED_CONTENT),
    do_chunking(false),
    do_dechunking(false),
    do_chunked_passthru(false),
    init_bytes_done(0),
    nbytes(0),
    ntodo(0),
    bytes_read(0),
    handler_state(0),
    last_event(0),
    num_consumers(0),
    alive(false),
    read_success(false),
    flow_control_source(nullptr),
    name(nullptr)
{
}

uint64_t
HttpTunnelProducer::backlog(uint64_t limit)
{
  uint64_t zret = 0;
  // Calculate the total backlog, the # of bytes inside ATS for this producer.
  // We go all the way through each chain to the ending sink and take the maximum
  // over those paths. Do need to be careful about loops which can occur.
  for (HttpTunnelConsumer *c = consumer_list.head; c; c = c->link.next) {
    if (c->alive && c->write_vio) {
      uint64_t n = 0;
      if (HT_TRANSFORM == c->vc_type) {
        n += static_cast<TransformVCChain *>(c->vc)->backlog(limit);
      } else {
        IOBufferReader *r = c->write_vio->get_reader();
        if (r) {
          n += static_cast<uint64_t>(r->read_avail());
        }
      }
      if (n >= limit) {
        return n;
      }

      if (!c->is_sink()) {
        HttpTunnelProducer *dsp = c->self_producer;
        if (dsp) {
          n += dsp->backlog();
        }
      }
      if (n >= limit) {
        return n;
      }
      if (n > zret) {
        zret = n;
      }
    }
  }

  if (chunked_handler.chunked_reader) {
    zret += static_cast<uint64_t>(chunked_handler.chunked_reader->read_avail());
  }

  return zret;
}

/*  We set the producers in a flow chain specifically rather than
    using a tunnel level variable in order to handle bi-directional
    tunnels correctly. In such a case the flow control on producers is
    not related so a single value for the tunnel won't work.
*/
void
HttpTunnelProducer::set_throttle_src(HttpTunnelProducer *srcp)
{
  HttpTunnelProducer *p  = this;
  p->flow_control_source = srcp;
  for (HttpTunnelConsumer *c = consumer_list.head; c; c = c->link.next) {
    if (!c->is_sink()) {
      p = c->self_producer;
      if (p) {
        p->set_throttle_src(srcp);
      }
    }
  }
}

HttpTunnelConsumer::HttpTunnelConsumer()
  : link(),
    producer(nullptr),
    self_producer(nullptr),
    vc_type(HT_HTTP_CLIENT),
    vc(nullptr),
    buffer_reader(nullptr),
    vc_handler(nullptr),
    write_vio(nullptr),
    skip_bytes(0),
    bytes_written(0),
    handler_state(0),
    alive(false),
    write_success(false),
    name(nullptr)
{
}

HttpTunnel::HttpTunnel() : Continuation(nullptr) {}

void
HttpTunnel::init(HttpSM *sm_arg, Ptr<ProxyMutex> &amutex)
{
  HttpConfigParams *params = sm_arg->t_state.http_config_param;
  sm                       = sm_arg;
  active                   = false;
  mutex                    = amutex;
  ink_release_assert(reentrancy_count == 0);
  SET_HANDLER(&HttpTunnel::main_handler);
  flow_state.enabled_p = params->oride.flow_control_enabled;
  if (params->oride.flow_low_water_mark > 0) {
    flow_state.low_water = params->oride.flow_low_water_mark;
  }
  if (params->oride.flow_high_water_mark > 0) {
    flow_state.high_water = params->oride.flow_high_water_mark;
  }
  // This should always be true, we handled default cases back in HttpConfig::reconfigure()
  ink_assert(flow_state.low_water <= flow_state.high_water);
}

void
HttpTunnel::reset()
{
  ink_assert(active == false);
#ifdef DEBUG
  for (int i = 0; i < MAX_PRODUCERS; ++i) {
    ink_assert(producers[i].alive == false);
  }
  for (int j = 0; j < MAX_CONSUMERS; ++j) {
    ink_assert(consumers[j].alive == false);
  }
#endif

  num_producers = 0;
  num_consumers = 0;
  memset(consumers, 0, sizeof(consumers));
  memset(producers, 0, sizeof(producers));
}

void
HttpTunnel::kill_tunnel()
{
  for (auto &producer : producers) {
    if (producer.vc != nullptr) {
      chain_abort_all(&producer);
    }
    ink_assert(producer.alive == false);
  }
  active = false;
  this->deallocate_buffers();
  this->reset();
}

HttpTunnelProducer *
HttpTunnel::alloc_producer()
{
  for (int i = 0; i < MAX_PRODUCERS; ++i) {
    if (producers[i].vc == nullptr) {
      num_producers++;
      ink_assert(num_producers <= MAX_PRODUCERS);
      return producers + i;
    }
  }
  ink_release_assert(0);
  return nullptr;
}

HttpTunnelConsumer *
HttpTunnel::alloc_consumer()
{
  for (int i = 0; i < MAX_CONSUMERS; i++) {
    if (consumers[i].vc == nullptr) {
      num_consumers++;
      ink_assert(num_consumers <= MAX_CONSUMERS);
      return consumers + i;
    }
  }
  ink_release_assert(0);
  return nullptr;
}

int
HttpTunnel::deallocate_buffers()
{
  int num = 0;
  ink_release_assert(active == false);
  for (auto &producer : producers) {
    if (producer.read_buffer != nullptr) {
      ink_assert(producer.vc != nullptr);
      free_MIOBuffer(producer.read_buffer);
      producer.read_buffer  = nullptr;
      producer.buffer_start = nullptr;
      num++;
    }

    if (producer.chunked_handler.dechunked_buffer != nullptr) {
      ink_assert(producer.vc != nullptr);
      free_MIOBuffer(producer.chunked_handler.dechunked_buffer);
      producer.chunked_handler.dechunked_buffer = nullptr;
      num++;
    }

    if (producer.chunked_handler.chunked_buffer != nullptr) {
      ink_assert(producer.vc != nullptr);
      free_MIOBuffer(producer.chunked_handler.chunked_buffer);
      producer.chunked_handler.chunked_buffer = nullptr;
      num++;
    }
    producer.chunked_handler.max_chunk_header_len = 0;
  }
  return num;
}

void
HttpTunnel::set_producer_chunking_action(HttpTunnelProducer *p, int64_t skip_bytes, TunnelChunkingAction_t action)
{
  p->chunked_handler.skip_bytes = skip_bytes;
  p->chunking_action            = action;

  switch (action) {
  case TCA_CHUNK_CONTENT:
    p->chunked_handler.state = p->chunked_handler.CHUNK_WRITE_CHUNK;
    break;
  case TCA_DECHUNK_CONTENT:
  case TCA_PASSTHRU_CHUNKED_CONTENT:
    p->chunked_handler.state = p->chunked_handler.CHUNK_READ_SIZE;
    break;
  default:
    break;
  };
}

void
HttpTunnel::set_producer_chunking_size(HttpTunnelProducer *p, int64_t size)
{
  p->chunked_handler.set_max_chunk_size(size);
}

// HttpTunnelProducer* HttpTunnel::add_producer
//
//   Adds a new producer to the tunnel
//
HttpTunnelProducer *
HttpTunnel::add_producer(VConnection *vc, int64_t nbytes_arg, IOBufferReader *reader_start, HttpProducerHandler sm_handler,
                         HttpTunnelType_t vc_type, const char *name_arg)
{
  HttpTunnelProducer *p;

  Debug("http_tunnel", "[%" PRId64 "] adding producer '%s'", sm->sm_id, name_arg);

  ink_assert(reader_start->mbuf);
  if ((p = alloc_producer()) != nullptr) {
    p->vc              = vc;
    p->nbytes          = nbytes_arg;
    p->buffer_start    = reader_start;
    p->read_buffer     = reader_start->mbuf;
    p->vc_handler      = sm_handler;
    p->vc_type         = vc_type;
    p->name            = name_arg;
    p->chunking_action = TCA_PASSTHRU_DECHUNKED_CONTENT;

    p->do_chunking         = false;
    p->do_dechunking       = false;
    p->do_chunked_passthru = false;

    p->init_bytes_done = reader_start->read_avail();
    if (p->nbytes < 0) {
      p->ntodo = p->nbytes;
    } else { // The byte count given us includes bytes
      //  that alread may be in the buffer.
      //  ntodo represents the number of bytes
      //  the tunneling mechanism needs to read
      //  for the producer
      p->ntodo = p->nbytes - p->init_bytes_done;
      ink_assert(p->ntodo >= 0);
    }

    // We are static, the producer is never "alive"
    //   It just has data in the buffer
    if (vc == HTTP_TUNNEL_STATIC_PRODUCER) {
      ink_assert(p->ntodo == 0);
      p->alive        = false;
      p->read_success = true;
    } else {
      p->alive = true;
    }
  }
  return p;
}

// void HttpTunnel::add_consumer
//
//    Adds a new consumer to the tunnel.  The producer must
//    be specified and already added to the tunnel.  Attaches
//    the new consumer to the entry for the existing producer
//
//    Returns true if the consumer successfully added.  Returns
//    false if the consumer was not added because the source failed
//
HttpTunnelConsumer *
HttpTunnel::add_consumer(VConnection *vc, VConnection *producer, HttpConsumerHandler sm_handler, HttpTunnelType_t vc_type,
                         const char *name_arg, int64_t skip_bytes)
{
  Debug("http_tunnel", "[%" PRId64 "] adding consumer '%s'", sm->sm_id, name_arg);

  // Find the producer entry
  HttpTunnelProducer *p = get_producer(producer);
  ink_release_assert(p);

  // Check to see if the producer terminated
  //  without sending all of its data
  if (p->alive == false && p->read_success == false) {
    Debug("http_tunnel", "[%" PRId64 "] consumer '%s' not added due to producer failure", sm->sm_id, name_arg);
    return nullptr;
  }
  // Initialize the consumer structure
  HttpTunnelConsumer *c = alloc_consumer();
  c->producer           = p;
  c->vc                 = vc;
  c->alive              = true;
  c->skip_bytes         = skip_bytes;
  c->vc_handler         = sm_handler;
  c->vc_type            = vc_type;
  c->name               = name_arg;

  // Register the consumer with the producer
  p->consumer_list.push(c);
  p->num_consumers++;

  return c;
}

void
HttpTunnel::chain(HttpTunnelConsumer *c, HttpTunnelProducer *p)
{
  p->self_consumer = c;
  c->self_producer = p;
  // If the flow is already throttled update the chained producer.
  if (c->producer->is_throttled()) {
    p->set_throttle_src(c->producer->flow_control_source);
  }
}

// void HttpTunnel::tunnel_run()
//
//    Makes the tunnel go
//
void
HttpTunnel::tunnel_run(HttpTunnelProducer *p_arg)
{
  Debug("http_tunnel", "tunnel_run started, p_arg is %s", p_arg ? "provided" : "NULL");
  if (p_arg) {
    producer_run(p_arg);
  } else {
    HttpTunnelProducer *p;

    ink_assert(active == false);

    for (int i = 0; i < MAX_PRODUCERS; ++i) {
      p = producers + i;
      if (p->vc != nullptr && (p->alive || (p->vc_type == HT_STATIC && p->buffer_start != nullptr))) {
        producer_run(p);
      }
    }
  }

  // It is possible that there was nothing to do
  //   due to a all transfers being zero length
  //   If that is the case, call the state machine
  //   back to say we are done
  if (!is_tunnel_alive()) {
    active = false;
    sm->handleEvent(HTTP_TUNNEL_EVENT_DONE, this);
  }
}

void
HttpTunnel::producer_run(HttpTunnelProducer *p)
{
  // Determine whether the producer has a cache-write consumer,
  // since all chunked content read by the producer gets dechunked
  // prior to being written into the cache.
  HttpTunnelConsumer *c, *cache_write_consumer = nullptr;
  bool transform_consumer = false;

  for (c = p->consumer_list.head; c; c = c->link.next) {
    if (c->vc_type == HT_CACHE_WRITE) {
      cache_write_consumer = c;
      break;
    }
  }

  // bz57413
  for (c = p->consumer_list.head; c; c = c->link.next) {
    if (c->vc_type == HT_TRANSFORM) {
      transform_consumer = true;
      break;
    }
  }

  // Determine whether the producer is to perform chunking,
  // dechunking, or chunked-passthough of the incoming response.
  TunnelChunkingAction_t action = p->chunking_action;

  // [bug 2579251] static producers won't have handler set
  if (p->vc != HTTP_TUNNEL_STATIC_PRODUCER) {
    if (action == TCA_CHUNK_CONTENT) {
      p->do_chunking = true;
    } else if (action == TCA_DECHUNK_CONTENT) {
      p->do_dechunking = true;
    } else if (action == TCA_PASSTHRU_CHUNKED_CONTENT) {
      p->do_chunked_passthru = true;

      // Dechunk the chunked content into the cache.
      if (cache_write_consumer != nullptr) {
        p->do_dechunking = true;
      }
    }
  }

  int64_t consumer_n;
  int64_t producer_n;

  ink_assert(p->vc != nullptr);
  active = true;

  IOBufferReader *chunked_buffer_start = nullptr, *dechunked_buffer_start = nullptr;
  if (p->do_chunking || p->do_dechunking || p->do_chunked_passthru) {
    p->chunked_handler.init(p->buffer_start, p);

    // Copy the header into the chunked/dechunked buffers.
    if (p->do_chunking) {
      // initialize a reader to chunked buffer start before writing to keep ref count
      chunked_buffer_start = p->chunked_handler.chunked_buffer->alloc_reader();
      p->chunked_handler.chunked_buffer->write(p->buffer_start, p->chunked_handler.skip_bytes);
    }
    if (p->do_dechunking) {
      // bz57413
      Debug("http_tunnel", "[producer_run] do_dechunking p->chunked_handler.chunked_reader->read_avail() = %" PRId64 "",
            p->chunked_handler.chunked_reader->read_avail());

      // initialize a reader to dechunked buffer start before writing to keep ref count
      dechunked_buffer_start = p->chunked_handler.dechunked_buffer->alloc_reader();

      // If there is no transformation then add the header to the buffer, else the
      // client already has got the header from us, no need for it in the buffer.
      if (!transform_consumer) {
        p->chunked_handler.dechunked_buffer->write(p->buffer_start, p->chunked_handler.skip_bytes);

        Debug("http_tunnel", "[producer_run] do_dechunking::Copied header of size %" PRId64 "", p->chunked_handler.skip_bytes);
      }
    }
  }

  int64_t read_start_pos = 0;
  if (p->vc_type == HT_CACHE_READ && sm->t_state.range_setup == HttpTransact::RANGE_NOT_TRANSFORM_REQUESTED) {
    ink_assert(sm->t_state.num_range_fields == 1); // we current just support only one range entry
    read_start_pos = sm->t_state.ranges[0]._start;
    producer_n     = (sm->t_state.ranges[0]._end - sm->t_state.ranges[0]._start) + 1;
    consumer_n     = (producer_n + sm->client_response_hdr_bytes);
  } else if (p->nbytes >= 0) {
    consumer_n = p->nbytes;
    producer_n = p->ntodo;
  } else {
    consumer_n = (producer_n = INT64_MAX);
  }

  // Do the IO on the consumers first so
  //  data doesn't disappear out from
  //  under the tunnel
  for (c = p->consumer_list.head; c;) {
    // Create a reader for each consumer.  The reader allows
    // us to implement skip bytes
    if (c->vc_type == HT_CACHE_WRITE) {
      switch (action) {
      case TCA_CHUNK_CONTENT:
      case TCA_PASSTHRU_DECHUNKED_CONTENT:
        c->buffer_reader = p->read_buffer->clone_reader(p->buffer_start);
        break;
      case TCA_DECHUNK_CONTENT:
      case TCA_PASSTHRU_CHUNKED_CONTENT:
        c->buffer_reader = p->chunked_handler.dechunked_buffer->clone_reader(dechunked_buffer_start);
        break;
      default:
        break;
      }
    }
    // Non-cache consumers.
    else if (action == TCA_CHUNK_CONTENT) {
      c->buffer_reader = p->chunked_handler.chunked_buffer->clone_reader(chunked_buffer_start);
    } else if (action == TCA_DECHUNK_CONTENT) {
      c->buffer_reader = p->chunked_handler.dechunked_buffer->clone_reader(dechunked_buffer_start);
    } else {
      c->buffer_reader = p->read_buffer->clone_reader(p->buffer_start);
    }

    // Consume bytes of the reader if we skipping bytes
    if (c->skip_bytes > 0) {
      ink_assert(c->skip_bytes <= c->buffer_reader->read_avail());
      c->buffer_reader->consume(c->skip_bytes);
    }
    int64_t c_write = consumer_n;

    // INKqa05109 - if we don't know the length leave it at
    //  INT64_MAX or else the cache may bounce the write
    //  because it thinks the document is too big.  INT64_MAX
    //  is a special case for the max document size code
    //  in the cache
    if (c_write != INT64_MAX) {
      c_write -= c->skip_bytes;
    }
    // Fix for problems with not chunked content being chunked and
    // not sending the entire data.  The content length grows when
    // it is being chunked.
    if (p->do_chunking == true) {
      c_write = INT64_MAX;
    }

    if (c_write == 0) {
      // Nothing to do, call back the cleanup handlers
      c->write_vio = nullptr;
      consumer_handler(VC_EVENT_WRITE_COMPLETE, c);
    } else {
      // In the client half close case, all the data that will be sent
      // from the client is already in the buffer.  Go ahead and set
      // the amount to read since we know it.  We will forward the FIN
      // to the server on VC_EVENT_WRITE_COMPLETE.
      if (p->vc_type == HT_HTTP_CLIENT) {
        ProxyClientTransaction *ua_vc = static_cast<ProxyClientTransaction *>(p->vc);
        if (ua_vc->get_half_close_flag()) {
          c_write          = c->buffer_reader->read_avail();
          p->alive         = false;
          p->handler_state = HTTP_SM_POST_SUCCESS;
        }
      }
      c->write_vio = c->vc->do_io_write(this, c_write, c->buffer_reader);
      ink_assert(c_write > 0);
    }

    c = c->link.next;
  }

  // YTS Team, yamsat Plugin
  // Allocate and copy partial POST data to buffers. Check for the various parameters
  // including the maximum configured post data size
  if ((p->vc_type == HT_BUFFER_READ && sm->is_postbuf_valid()) ||
      (p->alive && sm->t_state.method == HTTP_WKSIDX_POST && sm->enable_redirection && p->vc_type == HT_HTTP_CLIENT)) {
    Debug("http_redirect", "[HttpTunnel::producer_run] client post: %" PRId64 " max size: %" PRId64 "",
          p->buffer_start->read_avail(), HttpConfig::m_master.post_copy_size);

    // (note that since we are not dechunking POST, this is the chunked size if chunked)
    if (p->buffer_start->read_avail() > HttpConfig::m_master.post_copy_size) {
      Warning("http_redirect, [HttpTunnel::producer_handler] post exceeds buffer limit, buffer_avail=%" PRId64 " limit=%" PRId64 "",
              p->buffer_start->read_avail(), HttpConfig::m_master.post_copy_size);
      sm->disable_redirect();
      if (p->vc_type == HT_BUFFER_READ) {
        producer_handler(VC_EVENT_ERROR, p);
        return;
      }
    } else {
      sm->postbuf_copy_partial_data();
    }
  } // end of added logic for partial POST

  if (p->do_chunking) {
    // remove the chunked reader marker so that it doesn't act like a buffer guard
    p->chunked_handler.chunked_buffer->dealloc_reader(chunked_buffer_start);
    p->chunked_handler.dechunked_reader->consume(p->chunked_handler.skip_bytes);

    // If there is data to process in the buffer, do it now
    producer_handler(VC_EVENT_READ_READY, p);
  } else if (p->do_dechunking || p->do_chunked_passthru) {
    // remove the dechunked reader marker so that it doesn't act like a buffer guard
    if (p->do_dechunking && dechunked_buffer_start) {
      p->chunked_handler.dechunked_buffer->dealloc_reader(dechunked_buffer_start);
    }

    // bz57413
    // If there is no transformation plugin, then we didn't add the header, hence no need to consume it
    Debug("http_tunnel", "[producer_run] do_dechunking p->chunked_handler.chunked_reader->read_avail() = %" PRId64 "",
          p->chunked_handler.chunked_reader->read_avail());
    if (!transform_consumer && (p->chunked_handler.chunked_reader->read_avail() >= p->chunked_handler.skip_bytes)) {
      p->chunked_handler.chunked_reader->consume(p->chunked_handler.skip_bytes);
      Debug("http_tunnel", "[producer_run] do_dechunking p->chunked_handler.skip_bytes = %" PRId64 "",
            p->chunked_handler.skip_bytes);
    }
    // if(p->chunked_handler.chunked_reader->read_avail() > 0)
    // p->chunked_handler.chunked_reader->consume(
    // p->chunked_handler.skip_bytes);

    producer_handler(VC_EVENT_READ_READY, p);
    if (sm->get_postbuf_done() && p->vc_type == HT_HTTP_CLIENT) { // read_avail() == 0
      // [bug 2579251]
      // Ugh, this is horrible but in the redirect case they are running a the tunnel again with the
      // now closed/empty producer to trigger PRECOMPLETE.  If the POST was chunked, producer_n is set
      // (incorrectly) to INT64_MAX.  It needs to be set to 0 to prevent triggering another read.
      producer_n = 0;
    }
  }

  if (p->alive) {
    ink_assert(producer_n >= 0);

    if (producer_n == 0) {
      // Everything is already in the buffer so mark the producer as done.  We need to notify
      // state machine that everything is done.  We use a special event to say the producers is
      // done but we didn't do anything
      p->alive         = false;
      p->read_success  = true;
      p->handler_state = HTTP_SM_POST_SUCCESS;
      Debug("http_tunnel", "[%" PRId64 "] [tunnel_run] producer already done", sm->sm_id);
      producer_handler(HTTP_TUNNEL_EVENT_PRECOMPLETE, p);
    } else {
      if (read_start_pos > 0) {
        p->read_vio = ((CacheVC *)p->vc)->do_io_pread(this, producer_n, p->read_buffer, read_start_pos);
      } else {
        p->read_vio = p->vc->do_io_read(this, producer_n, p->read_buffer);
      }
    }
  }

  // Now that the tunnel has started, we must remove producer's reader so
  // that it doesn't act like a buffer guard
  if (p->read_buffer && p->buffer_start) {
    p->read_buffer->dealloc_reader(p->buffer_start);
  }
  p->buffer_start = nullptr;
}

int
HttpTunnel::producer_handler_dechunked(int event, HttpTunnelProducer *p)
{
  ink_assert(p->do_chunking);

  Debug("http_tunnel", "[%" PRId64 "] producer_handler_dechunked [%s %s]", sm->sm_id, p->name,
        HttpDebugNames::get_event_name(event));

  // We only interested in translating certain events
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_EOS:
    p->last_event = p->chunked_handler.last_server_event = event;
    if (p->chunked_handler.generate_chunked_content()) { // We are done, make sure the consumer is activated
      HttpTunnelConsumer *c;
      for (c = p->consumer_list.head; c; c = c->link.next) {
        if (c->alive) {
          c->write_vio->nbytes = p->chunked_handler.chunked_size;
          // consumer_handler(VC_EVENT_WRITE_COMPLETE, c);
        }
      }
    }
    break;
  };
  // Since we will consume all the data if the server is actually finished
  //   we don't have to translate events like we do in the
  //   case producer_handler_chunked()
  return event;
}

// int HttpTunnel::producer_handler_chunked(int event, HttpTunnelProducer* p)
//
//   Handles events from chunked producers.  It calls the chunking handlers
//    if appropriate and then translates the event we got into a suitable
//    event to represent the unchunked state, and does chunked bookeeping
//
int
HttpTunnel::producer_handler_chunked(int event, HttpTunnelProducer *p)
{
  ink_assert(p->do_dechunking || p->do_chunked_passthru);

  Debug("http_tunnel", "[%" PRId64 "] producer_handler_chunked [%s %s]", sm->sm_id, p->name, HttpDebugNames::get_event_name(event));

  // We only interested in translating certain events
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_EOS:
    break;
  default:
    return event;
  }

  p->last_event = p->chunked_handler.last_server_event = event;
  bool done                                            = p->chunked_handler.process_chunked_content();

  // If we couldn't understand the encoding, return
  //   an error
  if (p->chunked_handler.state == ChunkedHandler::CHUNK_READ_ERROR) {
    Debug("http_tunnel", "[%" PRId64 "] producer_handler_chunked [%s chunk decoding error]", sm->sm_id, p->name);
    p->chunked_handler.truncation = true;
    // FIX ME: we return EOS here since it will cause the
    //  the client to be reenabled.  ERROR makes more
    //  sense but no reenables follow
    return VC_EVENT_EOS;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
    if (done) {
      return VC_EVENT_READ_COMPLETE;
    }
    break;
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_EOS:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    if (!done) {
      p->chunked_handler.truncation = true;
    }
    break;
  }

  return event;
}

//
// bool HttpTunnel::producer_handler(int event, HttpTunnelProducer* p)
//
//   Handles events from producers.
//
//   If the event is interesting only to the tunnel, this
//    handler takes all necessary actions and returns false
//    If the event is interesting to the state_machine,
//    it calls back the state machine and returns true
//
//
bool
HttpTunnel::producer_handler(int event, HttpTunnelProducer *p)
{
  HttpTunnelConsumer *c;
  HttpProducerHandler jump_point;
  bool sm_callback = false;

  Debug("http_tunnel", "[%" PRId64 "] producer_handler [%s %s]", sm->sm_id, p->name, HttpDebugNames::get_event_name(event));

  // Handle chunking/dechunking/chunked-passthrough if necessary.
  if (p->do_chunking) {
    event = producer_handler_dechunked(event, p);

    // If we were in PRECOMPLETE when this function was called
    // and we are doing chunking, then we just wrote the last
    // chunk in the the function call above.  We are done with the
    // tunnel.
    if (event == HTTP_TUNNEL_EVENT_PRECOMPLETE) {
      event = VC_EVENT_EOS;
    }
  } else if (p->do_dechunking || p->do_chunked_passthru) {
    event = producer_handler_chunked(event, p);
  } else {
    p->last_event = event;
  }

  // YTS Team, yamsat Plugin
  // Copy partial POST data to buffers. Check for the various parameters including
  // the maximum configured post data size
  if ((p->vc_type == HT_BUFFER_READ && sm->is_postbuf_valid()) ||
      (sm->t_state.method == HTTP_WKSIDX_POST && sm->enable_redirection &&
       (event == VC_EVENT_READ_READY || event == VC_EVENT_READ_COMPLETE) && p->vc_type == HT_HTTP_CLIENT)) {
    Debug("http_redirect", "[HttpTunnel::producer_handler] [%s %s]", p->name, HttpDebugNames::get_event_name(event));

    if ((sm->postbuf_buffer_avail() + sm->postbuf_reader_avail()) > HttpConfig::m_master.post_copy_size) {
      Warning("http_redirect, [HttpTunnel::producer_handler] post exceeds buffer limit, buffer_avail=%" PRId64
              " reader_avail=%" PRId64 " limit=%" PRId64 "",
              sm->postbuf_buffer_avail(), sm->postbuf_reader_avail(), HttpConfig::m_master.post_copy_size);
      sm->disable_redirect();
      if (p->vc_type == HT_BUFFER_READ) {
        event = VC_EVENT_ERROR;
      }
    } else {
      sm->postbuf_copy_partial_data();
      if (event == VC_EVENT_READ_COMPLETE || event == HTTP_TUNNEL_EVENT_PRECOMPLETE || event == VC_EVENT_EOS) {
        sm->set_postbuf_done(true);
      }
    }
  } // end of added logic for partial copy of POST

  Debug("http_redirect", "[HttpTunnel::producer_handler] enable_redirection: [%d %d %d] event: %d", p->alive == true,
        sm->enable_redirection, (p->self_consumer && p->self_consumer->alive == true), event);

  switch (event) {
  case VC_EVENT_READ_READY:
    // Data read from producer, reenable consumers
    for (c = p->consumer_list.head; c; c = c->link.next) {
      if (c->alive && c->write_vio) {
        c->write_vio->reenable();
      }
    }
    break;

  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    // If the write completes on the stack (as it can for http2), then
    // consumer could have called back by this point.  Must treat this as
    // a regular read complete (falling through to the following cases).

  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    // The producer completed
    p->alive = false;
    if (p->read_vio) {
      p->bytes_read = p->read_vio->ndone;
    } else {
      // If we are chunked, we can receive the whole document
      //   along with the header without knowing it (due to
      //   the message length being a property of the encoding)
      //   In that case, we won't have done a do_io so there
      //   will not be vio
      p->bytes_read = 0;
    }

    // callback the SM to notify of completion
    //  Note: we need to callback the SM before
    //  reenabling the consumers as the reenable may
    //  make the data visible to the consumer and
    //  initiate async I/O operation.  The SM needs to
    //  set how much I/O to do before async I/O is
    //  initiated
    jump_point = p->vc_handler;
    (sm->*jump_point)(event, p);
    sm_callback = true;
    p->update_state_if_not_set(HTTP_SM_POST_SUCCESS);

    // Data read from producer, reenable consumers
    for (c = p->consumer_list.head; c; c = c->link.next) {
      if (c->alive && c->write_vio) {
        c->write_vio->reenable();
      }
    }
    break;

  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    if (p->alive) {
      p->alive      = false;
      p->bytes_read = p->read_vio->ndone;
      // Clear any outstanding reads so they don't
      // collide with future tunnel IO's
      p->vc->do_io_read(nullptr, 0, nullptr);
      // Interesting tunnel event, call SM
      jump_point = p->vc_handler;
      (sm->*jump_point)(event, p);
      sm_callback = true;
      // Failure case anyway
      p->update_state_if_not_set(HTTP_SM_POST_UA_FAIL);
    }
    break;

  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
  default:
    // Producers should not get these events
    ink_release_assert(0);
    break;
  }

  return sm_callback;
}

void
HttpTunnel::consumer_reenable(HttpTunnelConsumer *c)
{
  HttpTunnelProducer *p = c->producer;

  if (p && p->alive
#ifndef LAZY_BUF_ALLOC
      && p->read_buffer->write_avail() > 0
#endif
  ) {
    // Only do flow control if enabled and the producer is an external
    // source.  Otherwise disable by making the backlog zero. Because
    // the backlog short cuts quit when the value is equal (or
    // greater) to the target, we use strict comparison only for
    // checking low water, otherwise the flow control can stall out.
    uint64_t backlog         = (flow_state.enabled_p && p->is_source()) ? p->backlog(flow_state.high_water) : 0;
    HttpTunnelProducer *srcp = p->flow_control_source;

    if (backlog >= flow_state.high_water) {
      if (is_debug_tag_set("http_tunnel")) {
        Debug("http_tunnel", "Throttle   %p %" PRId64 " / %" PRId64, p, backlog, p->backlog());
      }
      p->throttle(); // p becomes srcp for future calls to this method
    } else {
      if (srcp && srcp->alive && c->is_sink()) {
        // Check if backlog is below low water - note we need to check
        // against the source producer, not necessarily the producer
        // for this consumer. We don't have to recompute the backlog
        // if they are the same because we know low water <= high
        // water so the value is sufficiently accurate.
        if (srcp != p) {
          backlog = srcp->backlog(flow_state.low_water);
        }
        if (backlog < flow_state.low_water) {
          if (is_debug_tag_set("http_tunnel")) {
            Debug("http_tunnel", "Unthrottle %p %" PRId64 " / %" PRId64, p, backlog, p->backlog());
          }
          srcp->unthrottle();
          if (srcp->read_vio) {
            srcp->read_vio->reenable();
          }
          // Kick source producer to get flow ... well, flowing.
          this->producer_handler(VC_EVENT_READ_READY, srcp);
        } else {
          // We can stall for small thresholds on network sinks because this event happens
          // before the actual socket write. So we trap for the buffer becoming empty to
          // make sure we get an event to unthrottle after the write.
          if (HT_HTTP_CLIENT == c->vc_type) {
            NetVConnection *netvc = dynamic_cast<NetVConnection *>(c->write_vio->vc_server);
            if (netvc) { // really, this should always be true.
              netvc->trapWriteBufferEmpty();
            }
          }
        }
      }
      if (p->read_vio) {
        p->read_vio->reenable();
      }
    }
  }
}

//
// bool HttpTunnel::consumer_handler(int event, HttpTunnelConsumer* p)
//
//   Handles events from consumers.
//
//   If the event is interesting only to the tunnel, this
//    handler takes all necessary actions and returns false
//    If the event is interesting to the state_machine,
//    it calls back the state machine and returns true
//
//
bool
HttpTunnel::consumer_handler(int event, HttpTunnelConsumer *c)
{
  bool sm_callback = false;
  HttpConsumerHandler jump_point;
  HttpTunnelProducer *p = c->producer;

  Debug("http_tunnel", "[%" PRId64 "] consumer_handler [%s %s]", sm->sm_id, c->name, HttpDebugNames::get_event_name(event));

  ink_assert(c->alive == true);

  switch (event) {
  case VC_EVENT_WRITE_READY:
    this->consumer_reenable(c);
    break;

  case VC_EVENT_WRITE_COMPLETE:
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    ink_assert(c->alive);
    ink_assert(c->buffer_reader);
    c->alive = false;

    c->bytes_written = c->write_vio ? c->write_vio->ndone : 0;

    // Interesting tunnel event, call SM
    jump_point = c->vc_handler;
    (sm->*jump_point)(event, c);
    // Make sure the handler_state is set
    // Necessary for post tunnel end processing
    if (c->producer && c->producer->handler_state == 0) {
      if (event == VC_EVENT_WRITE_COMPLETE) {
        c->producer->handler_state = HTTP_SM_POST_SUCCESS;
      } else if (c->vc_type == HT_HTTP_SERVER) {
        c->producer->handler_state = HTTP_SM_POST_UA_FAIL;
      } else if (c->vc_type == HT_HTTP_CLIENT) {
        c->producer->handler_state = HTTP_SM_POST_SERVER_FAIL;
      }
    }
    sm_callback = true;

    // Deallocate the reader after calling back the sm
    //  because buffer problems are easier to debug
    //  in the sm when the reader is still valid
    if (c->buffer_reader) {
      c->buffer_reader->mbuf->dealloc_reader(c->buffer_reader);
      c->buffer_reader = nullptr;
    }

    // Since we removed a consumer, it may now be
    //   possbile to put more stuff in the buffer
    // Note: we reenable only after calling back
    //    the SM since the reenabling has the side effect
    //    updating the buffer state for the VConnection
    //    that is being reenabled
    if (p->alive && p->read_vio
#ifndef LAZY_BUF_ALLOC
        && p->read_buffer->write_avail() > 0
#endif
    ) {
      if (p->is_throttled()) {
        this->consumer_reenable(c);
      } else {
        p->read_vio->reenable();
      }
    }
    // [amc] I don't think this happens but we'll leave a debug trap
    // here just in case.
    if (p->is_throttled()) {
      Debug("http_tunnel", "Special event %s on %p with flow control on", HttpDebugNames::get_event_name(event), p);
    }
    break;

  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  default:
    // Consumers should not get these events
    ink_release_assert(0);
    break;
  }

  return sm_callback;
}

// void HttpTunnel::chain_abort_all(HttpTunnelProducer* p)
//
//    Abort the producer and everyone still alive
//     downstream of the producer
//
void
HttpTunnel::chain_abort_all(HttpTunnelProducer *p)
{
  HttpTunnelConsumer *c = p->consumer_list.head;

  while (c) {
    if (c->alive) {
      c->alive     = false;
      c->write_vio = nullptr;
      c->vc->do_io_close(EHTTP_ERROR);
      update_stats_after_abort(c->vc_type);
    }

    if (c->self_producer) {
      // Must snip the link before recursively
      // freeing to avoid looks introduced by
      // blind tunneling
      HttpTunnelProducer *selfp = c->self_producer;
      c->self_producer          = nullptr;
      chain_abort_all(selfp);
    }

    c = c->link.next;
  }

  if (p->alive) {
    p->alive = false;
    if (p->read_vio) {
      p->bytes_read = p->read_vio->ndone;
    }
    if (p->self_consumer) {
      p->self_consumer->alive = false;
    }
    p->read_vio = nullptr;
    p->vc->do_io_close(EHTTP_ERROR);
    update_stats_after_abort(p->vc_type);
  }
}

// void HttpTunnel::chain_finish_internal(HttpTunnelProducer* p)
//
//    Internal function for finishing all consumers.  Takes
//       chain argument about where to finish just immediate
//       consumer or all those downstream
//
void
HttpTunnel::finish_all_internal(HttpTunnelProducer *p, bool chain)
{
  ink_assert(p->alive == false);
  HttpTunnelConsumer *c         = p->consumer_list.head;
  int64_t total_bytes           = 0;
  TunnelChunkingAction_t action = p->chunking_action;

  while (c) {
    if (c->alive) {
      if (c->vc_type == HT_CACHE_WRITE) {
        switch (action) {
        case TCA_CHUNK_CONTENT:
        case TCA_PASSTHRU_DECHUNKED_CONTENT:
          total_bytes = p->bytes_read + p->init_bytes_done;
          break;
        case TCA_DECHUNK_CONTENT:
        case TCA_PASSTHRU_CHUNKED_CONTENT:
          total_bytes = p->chunked_handler.skip_bytes + p->chunked_handler.dechunked_size;
          break;
        default:
          break;
        }
      } else if (action == TCA_CHUNK_CONTENT) {
        total_bytes = p->chunked_handler.skip_bytes + p->chunked_handler.chunked_size;
      } else if (action == TCA_DECHUNK_CONTENT) {
        total_bytes = p->chunked_handler.skip_bytes + p->chunked_handler.dechunked_size;
      } else {
        total_bytes = p->bytes_read + p->init_bytes_done;
      }

      if (c->write_vio) {
        c->write_vio->nbytes = total_bytes - c->skip_bytes;
        ink_assert(c->write_vio->nbytes >= 0);

        if (c->write_vio->nbytes < 0) {
          // TODO: Wtf, printf?
          fprintf(stderr, "[HttpTunnel::finish_all_internal] ERROR: Incorrect total_bytes - c->skip_bytes = %" PRId64 "\n",
                  (int64_t)(total_bytes - c->skip_bytes));
        }
      }

      if (chain == true && c->self_producer) {
        chain_finish_all(c->self_producer);
      }
      // The IO Core will not call us back if there
      //   is nothing to do.  Check to see if there is
      //   nothing to do and take the appripriate
      //   action
      if (c->write_vio && c->write_vio->nbytes == c->write_vio->ndone) {
        consumer_handler(VC_EVENT_WRITE_COMPLETE, c);
      }
    }

    c = c->link.next;
  }
}

// void HttpTunnel::chain_abort_cache_write(HttpProducer* p)
//
//    Terminates all cache writes.  Used to prevent truncated
//     documents from being stored in the cache
//
void
HttpTunnel::chain_abort_cache_write(HttpTunnelProducer *p)
{
  HttpTunnelConsumer *c = p->consumer_list.head;

  while (c) {
    if (c->alive) {
      if (c->vc_type == HT_CACHE_WRITE) {
        ink_assert(c->self_producer == nullptr);
        c->write_vio = nullptr;
        c->vc->do_io_close(EHTTP_ERROR);
        c->alive = false;
        HTTP_DECREMENT_DYN_STAT(http_current_cache_connections_stat);
      } else if (c->self_producer) {
        chain_abort_cache_write(c->self_producer);
      }
    }
    c = c->link.next;
  }
}

// void HttpTunnel::close_vc(HttpTunnelProducer* p)
//
//    Closes the vc associated with the producer and
//      updates the state of the self_consumer
//
void
HttpTunnel::close_vc(HttpTunnelProducer *p)
{
  ink_assert(p->alive == false);
  HttpTunnelConsumer *c = p->self_consumer;

  if (c && c->alive) {
    c->alive = false;
    if (c->write_vio) {
      c->bytes_written = c->write_vio->ndone;
    }
  }

  p->vc->do_io_close();
}

// void HttpTunnel::close_vc(HttpTunnelConsumer* c)
//
//    Closes the vc associated with the consumer and
//      updates the state of the self_producer
//
void
HttpTunnel::close_vc(HttpTunnelConsumer *c)
{
  ink_assert(c->alive == false);
  HttpTunnelProducer *p = c->self_producer;

  if (p && p->alive) {
    p->alive = false;
    if (p->read_vio) {
      p->bytes_read = p->read_vio->ndone;
    }
  }

  c->vc->do_io_close();
}

// int HttpTunnel::main_handler(int event, void* data)
//
//   Main handler for the tunnel.  Vectors events
//   based on whether they are from consumers or
//   producers
//
int
HttpTunnel::main_handler(int event, void *data)
{
  HttpTunnelProducer *p = nullptr;
  HttpTunnelConsumer *c = nullptr;
  bool sm_callback      = false;

  ++reentrancy_count;

  ink_assert(sm->magic == HTTP_SM_MAGIC_ALIVE);

  // Find the appropriate entry
  if ((p = get_producer((VIO *)data)) != nullptr) {
    sm_callback = producer_handler(event, p);
  } else {
    if ((c = get_consumer((VIO *)data)) != nullptr) {
      ink_assert(c->write_vio == (VIO *)data || c->vc == ((VIO *)data)->vc_server);
      sm_callback = consumer_handler(event, c);
    } else {
      internal_error(); // do nothing
    }
  }

  // We called a vc handler, the tunnel might be
  //  finished.  Check to see if there are any remaining
  //  VConnections alive.  If not, notifiy the state machine
  //
  // Don't call out if we are nested
  if (call_sm || (sm_callback && !is_tunnel_alive())) {
    if (reentrancy_count == 1) {
      reentrancy_count = 0;
      active           = false;
      sm->handleEvent(HTTP_TUNNEL_EVENT_DONE, this);
      return EVENT_DONE;
    } else {
      call_sm = true;
    }
  }
  --reentrancy_count;
  return EVENT_CONT;
}

void
HttpTunnel::update_stats_after_abort(HttpTunnelType_t t)
{
  switch (t) {
  case HT_CACHE_READ:
  case HT_CACHE_WRITE:
    HTTP_DECREMENT_DYN_STAT(http_current_cache_connections_stat);
    break;
  default:
    // Handled here:
    // HT_HTTP_SERVER, HT_HTTP_CLIENT,
    // HT_TRANSFORM, HT_STATIC
    break;
  };
}

void
HttpTunnel::internal_error()
{
}
