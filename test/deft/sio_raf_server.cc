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

   sio_raf_server.cc

   Description:

   
 ****************************************************************************/

#include <errno.h>

#include "Diags.h"
#include "ink_snprintf.h"
#include "rafencode.h"

#include "sio_raf_server.h"
#include "sio_buffer.h"
#include "raf_cmd.h"


SioRafServer::SioRafServer():
raf_cmd(NULL), exit_mode(RAF_EXIT_NONE), cmd_buffer(NULL), resp_buffer(NULL), FD_Handler()
{
}

SioRafServer::~SioRafServer()
{

  if (cmd_buffer) {
    delete cmd_buffer;
    cmd_buffer = NULL;
  }

  if (resp_buffer) {
    delete resp_buffer;
    resp_buffer = NULL;
  }

  if (raf_cmd) {
    delete raf_cmd;
    raf_cmd = NULL;
  }

  SIO::remove_fd_handler(this);
}

void
SioRafServer::start(int new_fd)
{

  this->fd = new_fd;
  this->poll_interest = POLL_INTEREST_READ;
  this->my_handler = (SCont_Handler) & SioRafServer::handle_read_cmd;

  cmd_buffer = new sio_buffer;

  SIO::add_fd_handler(this);
}

void
SioRafServer::dispatcher()
{

  send_raf_resp(raf_cmd, 1, "Unknown cmd '%s' - no dispatcher", (*raf_cmd)[1]);
}

void
SioRafServer::response_complete()
{
  poll_interest = POLL_INTEREST_READ;
  my_handler = (SCont_Handler) & SioRafServer::handle_read_cmd;
}

void
SioRafServer::handle_write_resp(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(event == SEVENT_POLL);
  ink_debug_assert(this->fd == pfd->fd);

  int todo = resp_buffer->read_avail();

  int end_loop;
  do {
    end_loop = 1;
    int r = write(this->fd, resp_buffer->start(), todo);

    if (r < 0) {
      switch (errno) {
      case EAGAIN:
        // Try again later
        break;
      case EINTR:
        // Try again now
        end_loop = 0;
        break;
      default:
        Warning("write failed : %s", strerror(errno));
        delete this;
        return;
      }
    } else {
      resp_buffer->consume(r);

      if (resp_buffer->read_avail() == 0) {

        delete raf_cmd;
        raf_cmd = NULL;

        switch (exit_mode) {
        case RAF_EXIT_PROCESS:
          {
            // We want to try to send the shutdown response
            //   before the process exits to avoid the other
            //   end seeing a reset
            this->clear_non_block_flag();
            this->set_linger(1, 5);
            delete this;
            SIO::do_exit(0);
            break;
          }
        case RAF_EXIT_CONN:
          delete this;
          break;
        case RAF_EXIT_NONE:
        default:
          response_complete();
          break;
        }
      }
    }
  } while (end_loop == 0);

}

void
SioRafServer::handle_read_cmd(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(event == SEVENT_POLL);
  ink_debug_assert(this->fd == pfd->fd);

  int end_loop;
  do {

    end_loop = 1;
    int avail = cmd_buffer->expand_to(1024);
    int r = read(this->fd, cmd_buffer->end(), avail);

    if (r < 0) {
      switch (errno) {
      case EAGAIN:
        // Try again later
        break;
      case EINTR:
        // Try again now
        end_loop = 0;
        break;
      default:
        if (errno != ECONNRESET) {
          Warning("read failed : %s", strerror(errno));
        }
        delete this;
        return;
      }
    } else if (r == 0) {
      Debug("socket", "Socket closed");
      delete this;
      return;
    } else {
      cmd_buffer->fill(r);
    }
  } while (end_loop == 0);

  char *end = cmd_buffer->memchr('\n');
  if (end) {
    process_cmd(end);
  }
}

void
SioRafServer::process_cmd(char *end)
{

  // Smash the newline to a string terminator since this makes
  //  debug output easy
  *end = '\0';
  Debug("raf_server", "Received cmd: %s", cmd_buffer->start());

  char *cmd_start = cmd_buffer->start();
  int cmd_size = (end - cmd_start) + 1;

  // We do not want to parse the trailing \r\n
  int parse_size;
  if (cmd_size > 2 && cmd_start[cmd_size - 2] == '\r') {
    parse_size = cmd_size - 2;
  } else {
    parse_size = cmd_size - 1;
  }

  if (raf_cmd != NULL) {
    delete raf_cmd;
  }
  raf_cmd = new RafCmd;
  raf_cmd->process_cmd(cmd_start, parse_size);
  cmd_buffer->consume(cmd_size);

  if (raf_cmd->length() >= 2) {
    dispatcher();
  } else {
    send_raf_resp(raf_cmd, 1, "Malformed cmd");
  }
}

void
SioRafServer::send_raf_resp(RafCmd * reply)
{

  Debug("raf_server", "Sending raf response %s %s %s", (*reply)[0], (*reply)[1], (*reply)[2]);

  if (resp_buffer == NULL) {
    resp_buffer = new sio_buffer;
  }

  reply->build_message(resp_buffer);

  poll_interest = POLL_INTEREST_WRITE;
  my_handler = (SCont_Handler) & SioRafServer::handle_write_resp;
}

void
SioRafServer::send_raf_resp(RafCmd * cmd, int result_code, const char *msg_fmt, ...)
{

  char msg_buf[2048];
  va_list ap;
  va_start(ap, msg_fmt);

  Debug("raf_server", "Sending raf response %s", msg_fmt);

  if (resp_buffer) {
    resp_buffer->reset();
  } else {
    resp_buffer = new sio_buffer;
  }

  const char *id;
  if (cmd != NULL && cmd->length() >= 1) {
    id = (*cmd)[0];
  } else {
    id = "?";
  }
  resp_buffer->fill(id, strlen(id));

  sprintf(msg_buf, " %d ", result_code);
  resp_buffer->fill(msg_buf, strlen(msg_buf));

  int r = ink_vsnprintf(msg_buf, 2048, msg_fmt, ap);

  // We need to raf encode the message so that it through as a single
  //   argument
  int enc_len = raf_encodelen(msg_buf, r, 0);
  resp_buffer->expand_to(enc_len);
  int enc_len2 = raf_encode(msg_buf, r, resp_buffer->end(), enc_len, 0);
  ink_release_assert(enc_len == enc_len2);
  resp_buffer->fill(enc_len2);

  resp_buffer->fill("\n", 1);

  va_end(ap);

  poll_interest = POLL_INTEREST_WRITE;
  my_handler = (SCont_Handler) & SioRafServer::handle_write_resp;
}
