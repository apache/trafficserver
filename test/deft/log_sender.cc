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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Diags.h"
#include "snprintf.h"

#include "log_sender.h"
#include "test_utils.h"
#include "raf_cmd.h"

/* Constants */
static const int SIZE_32K = 32768;

LogSender::LogSender():
log_file_name(NULL), output_log_buffer(NULL), FD_Handler()
{
  my_handler = (SCont_Handler) & LogSender::handle_output;
}

LogSender::~LogSender()
{

  if (log_file_name) {
    free(log_file_name);
    log_file_name = NULL;
  }

  if (output_log_buffer) {
    delete output_log_buffer;
    output_log_buffer = NULL;
  }
}

void
LogSender::start_to_file(const char *file_arg)
{

  log_file_name = strdup(file_arg);

  this->fd = open(log_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (this->fd < 0) {
    Fatal("Unable to open log file %s : %s", log_file_name, strerror(errno));
  }

  this->poll_interest = POLL_INTEREST_NONE;

  SIO::add_fd_handler(this);

  output_log_buffer = new sio_buffer(SIZE_32K);

}

void
LogSender::start_to_net(unsigned int ip, int port)
{
  log_file_name = (char *) malloc(64);

  sprintf(log_file_name, "%u.%u.%u.%u:%d",
              ((unsigned char *) &ip)[0], ((unsigned char *) &ip)[1],
              ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3], port);

  this->fd = SIO::make_client(ip, port);
  if (this->fd < 0) {
    Fatal("Unable to open log collator %s", log_file_name);
  }
  // Send the RAF command to tell the collator we want to log
  RafCmd request;
  RafCmd response;
  sio_buffer read_buffer;
  int timeout_ms = 30 * 1000;

  request(0) = strdup("0");
  request(1) = strdup("log");

  const char *rmsg;
  rmsg = send_raf_cmd(fd, &request, &timeout_ms);

  if (rmsg == NULL) {
    rmsg = read_raf_resp(fd, &read_buffer, &response, &timeout_ms);
  }

  if (rmsg) {
    close(this->fd);
    this->fd = -1;
    Fatal("Unable to setup log collation : %s", rmsg);
  } else {
    this->poll_interest = POLL_INTEREST_NONE;
    SIO::add_fd_handler(this);

    output_log_buffer = new sio_buffer(SIZE_32K);
  }
}

void
LogSender::handle_output(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(this->fd == pfd->fd);
  ink_debug_assert(event == SEVENT_POLL);

  int avail = output_log_buffer->read_avail();

  int r;
  do {
    r = write(this->fd, output_log_buffer->start(), avail);
  } while (r < 0 && errno == EINTR);


  if (r < 0) {
    if (errno != EAGAIN) {
      Error("Output to log file %s failed : %s", log_file_name, strerror(errno));
      close(this->fd);
      this->fd = -1;
      this->poll_interest = POLL_INTEREST_NONE;
      SIO::remove_fd_handler(this);
      return;
    }
  } else {
    output_log_buffer->consume(r);
  }

  if (output_log_buffer->read_avail() == 0) {
    this->poll_interest = POLL_INTEREST_NONE;
  }
}

void
LogSender::add_to_output_log(const char *start, const char *end)
{

  if (this->fd < 0) {
    return;
  }

  output_log_buffer->fill(start, end - start);
  this->poll_interest = POLL_INTEREST_WRITE;
}

void
LogSender::flush_output()
{

  if (this->fd >= 0) {
    int timeout = 30 * 1000;    // FIX - shouldn't hardcode!
    const char *r_msg = write_buffer(this->fd, output_log_buffer, &timeout);

    if (r_msg) {
      Error("failed to flush log buffer: %s", r_msg);
    }
  } else {
    Error("flush failed due to broken output");
  }
}

void
LogSender::close_output()
{

  if (this->fd >= 0) {
    close(this->fd);
    this->fd = -1;
    this->poll_interest = POLL_INTEREST_NONE;
    SIO::remove_fd_handler(this);
  }
}

const char *
LogSender::roll_log_file(const char *roll_name)
{

  if (log_file_name == NULL) {
    return "error: not using a log file";
  }

  flush_output();

  // Check to see if the new name already exists
  int r;
  do {
    r = access(roll_name, F_OK);
  } while (r < 0 && errno == EINTR);

  if (r == 0) {
    do {
      r = unlink(roll_name);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      Error("[LogSender::roll_log_file] failed to unlink new name: %s", strerror(errno));
      return "unlink failed";
    }
  }
  // Create a hard link to the new name
  do {
    r =::link(log_file_name, roll_name);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    Error("[LogSender::roll_log_file] failed to create link : %s", strerror(errno));
    return "link failed";
  }

  close(fd);

  // Unlink the old copy
  do {
    r = unlink(log_file_name);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    Error("[LogSender::roll_log_file] failed to unlink old file: %s", strerror(errno));
    // FIX - do we need to do anything else here?
  }

  this->fd = open(log_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (this->fd < 0) {
    Fatal("[LogSender::roll_log_file] Unable to open new log file %s : %s", log_file_name, strerror(errno));
    return "open failed";
  }

  return NULL;
}
