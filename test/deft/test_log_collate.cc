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

   test_log_collate.cc

   Description:

   
 ****************************************************************************/

#include "test_log_collate.h"
#include "log_sender.h"
#include "raf_cmd.h"

#include "ink_args.h"
#include "snprintf.h"
#include "Diags.h"

/* Argument Stuff */
int collate_port = 12301;

static char error_tags[1024];
static char action_tags[1024];
static char log_file[1024];

ArgumentDescription argument_descriptions[] = {
  {"port", 'p', "Collatel Port", "I", &collate_port, NULL, NULL},
  {"log_file", 'L', "Log File", "S1023", log_file, NULL, NULL},
  {"debug_tags", 'T', "Debug Tags", "S1023", error_tags, "DEFT_LC_DEBUG", NULL},
  {"action_tags", 'B', "Behavior Tags", "S1023", action_tags, NULL, NULL},
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage}
};
int n_argument_descriptions = SIZE(argument_descriptions);

/* Globals */
Diags *diags = NULL;
LogAcceptHandler *accept_handler = NULL;
LogSender *log_sender = NULL;
LogCollateHandler *shutdown_waiter = NULL;

/* Constants */
static const int SIZE_32K = 32768;

LogAcceptHandler::LogAcceptHandler():
FD_Handler()
{
}

LogAcceptHandler::~LogAcceptHandler()
{
}

void
LogAcceptHandler::start(int port)
{

  this->fd = SIO::open_server(port);
  this->poll_interest = POLL_INTEREST_READ;
  this->my_handler = (SCont_Handler) & LogAcceptHandler::handle_accept;

  SIO::add_fd_handler(this);
}

void
LogAcceptHandler::stop()
{
  close(this->fd);
  this->fd = -1;
  this->poll_interest = POLL_INTEREST_NONE;;
  SIO::remove_fd_handler(this);
}

void
LogAcceptHandler::handle_accept(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_assert(this->fd == pfd->fd);

  /* FIX: should check for error in pfd revents */
  int new_fd = SIO::accept_sock(fd);

  if (new_fd > 0) {
    Debug("socket", "new accept on fd %d", fd);
    LogCollateHandler *new_log_h = new LogCollateHandler();
    new_log_h->start(new_fd);
  }
}

struct ExitHandler:public S_Continuation
{
  ExitHandler();
  void handle_exit(s_event_t, void *);
};

ExitHandler::ExitHandler():S_Continuation()
{
  my_handler = (SCont_Handler) & ExitHandler::handle_exit;
}

void
ExitHandler::handle_exit(s_event_t event, void *data)
{

  ink_release_assert(event == SEVENT_EXIT_NOTIFY);
  int status = (int) data;

  if (accept_handler) {
    accept_handler->stop();
    accept_handler = NULL;
  }

  log_sender->flush_output();
  log_sender->close_output();

  exit(status);
}


LogCollateHandler::LogCollateHandler():
lc_mode(LC_RAF), input_buffer(NULL), timer_event(NULL), SioRafServer()
{
}

LogCollateHandler::~LogCollateHandler()
{

  if (lc_mode == LC_COLLATE) {
    active_loggers--;

    if (active_loggers == 0 && shutdown_waiter != NULL) {
      shutdown_waiter->handle_event(SEVENT_PROC_STATE_CHANGE, NULL);
    }
  }

  if (input_buffer) {
    delete input_buffer;
    input_buffer = NULL;
  }
}

int
  LogCollateHandler::active_loggers = 0;

void
LogCollateHandler::start(int new_fd)
{
  this->fd = new_fd;
  this->poll_interest = POLL_INTEREST_READ;
  this->my_handler = (SCont_Handler) & SioRafServer::handle_read_cmd;

  cmd_buffer = new sio_buffer;
  input_buffer = new sio_buffer(SIZE_32K);

  SIO::add_fd_handler(this);
}

void
LogCollateHandler::dispatcher()
{

  const char *cmd_str = (*raf_cmd)[1];

  if (strcasecmp(cmd_str, "log") == 0) {
    lc_mode = LC_COLLATE;
    send_raf_resp(raf_cmd, 0, "start sending the log");
    active_loggers++;
  } else if (strcasecmp(cmd_str, "isalive") == 0) {
    send_raf_resp(raf_cmd, 0, "alive");
  } else if (strcasecmp(cmd_str, "shutdown") == 0) {
    process_cmd_shutdown();
  } else if (strcasecmp(cmd_str, "roll_log") == 0) {
    process_cmd_log_roll();
  } else {
    send_raf_resp(raf_cmd, 1, "unknown cmd '%s'", cmd_str);
  }
}

void
LogCollateHandler::process_cmd_log_roll()
{

  if (raf_cmd->length() < 3) {
    send_raf_resp(raf_cmd, 1, "insufficient arguments to log roll");
    return;
  }

  char new_name[1024];
  snprintf(new_name, 1023, "%s.%s", log_file, (*raf_cmd)[2]);
  new_name[1023] = '\0';

  const char *result_str = log_sender->roll_log_file(new_name);

  if (result_str == NULL) {
    send_raf_resp(raf_cmd, 0, "roll successful");
  } else {
    send_raf_resp(raf_cmd, 1, "roll failed : %s", result_str);
  }
}

void
LogCollateHandler::process_cmd_shutdown()
{

  // No further accepts
  accept_handler->poll_interest = POLL_INTEREST_NONE;

  int wait_time_s = 15;
  if (raf_cmd->length() >= 3) {
    wait_time_s = atoi((*raf_cmd)[2]);
    if (wait_time_s <= 0) {
      Warning("bad wait time to shutdown cmd : %s", (*raf_cmd)[2]);
      wait_time_s = 15;
    }
  }

  if (active_loggers == 0) {
    exit_mode = RAF_EXIT_PROCESS;
    send_raf_resp(raf_cmd, 0, "exiting...");
  } else {
    if (shutdown_waiter != NULL) {
      send_raf_resp(raf_cmd, 1, "shutdown already in progress");
    } else {
      poll_interest = POLL_INTEREST_NONE;
      shutdown_waiter = this;
      timer_event = SIO::schedule_in(this, wait_time_s * 1000);
      my_handler = (SCont_Handler) & LogCollateHandler::wait_for_shutdown_complete;
    }
  }
}

void
LogCollateHandler::wait_for_shutdown_complete(s_event_t event, void *data)
{

  ink_debug_assert(shutdown_waiter == this);
  shutdown_waiter = NULL;
  exit_mode = RAF_EXIT_PROCESS;

  switch (event) {
  case SEVENT_TIMER:
    ink_debug_assert(data == timer_event);
    timer_event = NULL;
    send_raf_resp(raf_cmd, 1, "exiting even though writers still exist");
    break;
  case SEVENT_PROC_STATE_CHANGE:
    timer_event->cancel();
    timer_event = NULL;
    send_raf_resp(raf_cmd, 0, "exiting...");
    break;
  default:
    ink_release_assert(0);
  }
}

void
LogCollateHandler::response_complete()
{

  this->poll_interest = POLL_INTEREST_READ;

  if (lc_mode == LC_COLLATE) {
    this->my_handler = (SCont_Handler) & LogCollateHandler::handle_log_input;
  } else {
    this->my_handler = (SCont_Handler) & SioRafServer::handle_read_cmd;
  }
}


void
LogCollateHandler::handle_log_input(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(this->fd == pfd->fd);
  ink_debug_assert(event == SEVENT_POLL);

  int avail = input_buffer->expand_to(SIZE_32K);
  int r;
  do {
    r = read(this->fd, input_buffer->end(), avail);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (errno != EAGAIN) {
      // FIX: write to output log as well!
      Error("read error: %s", strerror(errno));
      delete this;
    }
    return;
  } else if (r == 0) {
    Debug("collate", "input connection closed");
    delete this;
    return;
  } else {
    input_buffer->fill(r);

    char *last_newline = NULL;
    while ((last_newline = input_buffer->memchr('\n')) != NULL) {
      char *start = input_buffer->start();
      int line_len = (last_newline - start) + 1;
      log_sender->add_to_output_log(start, start + line_len);
      input_buffer->consume(line_len);
    }
  }
}

void
init_output_log()
{

  if (*log_file == '\0') {
    pid_t mypid = getpid();
    snprintf(log_file, 1023, "test_collate_log.%d", mypid);
    log_file[1023] = '\0';
    Note("No log specified - using %s", log_file);
  }

  log_sender = new LogSender();
  log_sender->start_to_file(log_file);
}

int
main(int argc, char **argv)
{

  process_args(argument_descriptions, n_argument_descriptions, argv);

  diags = new Diags(error_tags, action_tags);
  diags->config.outputs[DL_Diag].to_stdout = true;
  diags->show_location = 0;

  if (*error_tags) {
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  }

  if (*action_tags) {
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);
  }

  SIO::add_exit_handler(new ExitHandler);
  init_output_log();

  // Start the control port 
  accept_handler = new LogAcceptHandler();
  accept_handler->start(collate_port);

  SIO::run_loop();

  return 0;
}
