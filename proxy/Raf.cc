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

   Raf.cc

   Description:

   
 ****************************************************************************/

#include "Raf.h"
#include "P_RecProcess.h"
#include "rafencode.h"
#include "congest/Congestion.h"
void
start_raf()
{
  RecInt enabled = 0;
  REC_ReadConfigInteger(enabled, "proxy.config.raf.enabled");

  if (enabled) {
    RecInt port = 0;
    REC_ReadConfigInteger(port, "proxy.config.raf.port");
    RafAcceptCont *c = new RafAcceptCont();
    c->start(port);
  }
}

RafAcceptCont::RafAcceptCont():
Continuation(new_ProxyMutex()), accept_action(NULL), accept_port(0)
{
  SET_HANDLER(&RafAcceptCont::state_handle_accept);
}

RafAcceptCont::~RafAcceptCont()
{

  mutex = NULL;

  if (accept_action != NULL) {
    accept_action->cancel();
  }
}

void
RafAcceptCont::start(int accept_port_in)
{
  ink_debug_assert(accept_action == NULL);

  accept_port = accept_port_in;
  accept_action = netProcessor.accept(this, accept_port);
}


int
RafAcceptCont::state_handle_accept(int event, void *data)
{

  switch (event) {
  case NET_EVENT_ACCEPT:
    {
      NetVConnection *new_vc = (NetVConnection *) data;
      unsigned int client_ip = new_vc->get_remote_ip();

      // Only allow connections from localhost for security reasons
      unsigned int lip = 0;
      unsigned char *plip = (unsigned char *) &lip;
      plip[0] = 127;
      plip[1] = 0;
      plip[2] = 0;
      plip[3] = 1;
      if (client_ip != lip) {
        char ip_string[32];
        unsigned char *p = (unsigned char *) &(client_ip);

        snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
        Warning("raf connect by disallowed client %s, closing", ip_string);
        new_vc->do_io_close();
        return EVENT_DONE;
      }

      RafCont *c = new RafCont(new_vc);
      MUTEX_TRY_LOCK(lock, c->mutex, this_ethread());
      c->run();
      break;
    }
  case NET_EVENT_ACCEPT_FAILED:
    Warning("Raf accept failed on port %d", accept_port);
    accept_action = NULL;
    delete this;
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return EVENT_DONE;
}

struct RafCmdEntry
{
  const char *name;
  RafCmdHandler handler;
};

const RafCmdEntry raf_cmd_table[] = {
  {"query", &RafCont::process_query_cmd},
  {"congest", &RafCont::process_congestion_cmd},
  {"isalive", &RafCont::process_isalive_cmd},
  {"exit", &RafCont::process_exit_cmd},
  {"quit", &RafCont::process_exit_cmd}
};
int raf_cmd_entries = SIZE(raf_cmd_table);

RafCont::RafCont(NetVConnection * nvc):
Continuation(new_ProxyMutex()),
net_vc(nvc),
read_vio(NULL), write_vio(NULL), input_buffer(NULL), input_reader(NULL), output_buffer(NULL), pending_action(NULL)
{
  mime_scanner_init(&scanner);

  SET_HANDLER(&RafCont::main_handler);
  Debug("raf", "New Raf Connection Accepted");
}

RafCont::~RafCont()
{

  if (pending_action) {
    pending_action->cancel();
    pending_action = NULL;
  }

  if (net_vc) {
    net_vc->do_io_close();
    net_vc = NULL;
  }

  mime_scanner_clear(&scanner);

  if (input_buffer) {
    free_MIOBuffer(input_buffer);
    input_buffer = NULL;
  }

  if (output_buffer) {
    free_MIOBuffer(output_buffer);
    output_buffer = NULL;
  }
}

void
RafCont::kill()
{
  delete this;
}

void
RafCont::run()
{

  ink_debug_assert(input_buffer == NULL);
  ink_debug_assert(read_vio == NULL);

  input_buffer = new_MIOBuffer();
  input_reader = input_buffer->alloc_reader();

  output_buffer = new_MIOBuffer();
  IOBufferReader *output_reader = output_buffer->alloc_reader();

  net_vc->set_inactivity_timeout(HRTIME_MINUTES(10));

  read_vio = net_vc->do_io_read(this, INT_MAX, input_buffer);
  write_vio = net_vc->do_io_write(this, INT_MAX, output_reader);
}

int
RafCont::main_handler(int event, void *data)
{
  if (event == CONGESTION_EVENT_CONGESTED_LIST_DONE) {
    return state_handle_congest_list(event, data);
  }
  if (data == read_vio) {
    return state_handle_input(event, data);
  } else if (data == write_vio) {
    return state_handle_output(event, data);
  } else {
    ink_release_assert(0);
  }

  return EVENT_DONE;
}

int
RafCont::state_handle_congest_list(int event, void *data)
{
  ink_assert(event == CONGESTION_EVENT_CONGESTED_LIST_DONE);
  write_vio->reenable();
  read_vio->reenable();
/*
    output_buffer->write("\r\n", 2);
    mime_scanner_clear(&scanner);

    // Final cmd
    read_vio->nbytes = read_vio->ndone;
    write_vio->nbytes =
      write_vio->ndone + write_vio->get_reader()->read_avail();
    write_vio->reenable();
    */
  return EVENT_DONE;

}

int
RafCont::state_handle_output(int event, void *data)
{

  Debug("raf", "state_handler_output received event %d", event);

  switch (event) {
  case VC_EVENT_WRITE_READY:
    break;
  case VC_EVENT_WRITE_COMPLETE:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    kill();
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return EVENT_DONE;
}


int
RafCont::state_handle_input(int event, void *data)
{

  Debug("raf", "state_handler_input received event %d", event);

  switch (event) {
  case VC_EVENT_READ_READY:
    {
      int read_avail = input_reader->read_avail();
      const char *output_s;
      const char *output_e;

      MIMEParseResult r = PARSE_CONT;
      while (read_avail > 0) {
        int bavail = input_reader->block_read_avail();
        const char *start = input_reader->start();
        const char *scan_start = start;

        bool output_share;

        r = mime_scanner_get(&scanner, &scan_start, scan_start + bavail,
                             &output_s, &output_e, &output_share, false, MIME_SCANNER_TYPE_LINE);

        input_reader->consume(scan_start - start);
        read_avail = input_reader->read_avail();

        if (r != PARSE_CONT) {
          break;
        }
      }

      switch (r) {
      case PARSE_CONT:
        read_vio->reenable();
        break;
      case PARSE_OK:
        {
          int r = process_raf_cmd(output_s, output_e);
          output_buffer->write("\r\n", 2);
          mime_scanner_clear(&scanner);
          if (r == 0) {
            // Final cmd
            read_vio->nbytes = read_vio->ndone;
            write_vio->nbytes = write_vio->ndone + write_vio->get_reader()->read_avail();
            write_vio->reenable();
          } else if (input_reader->read_avail() > 0) {
            write_vio->reenable();
            state_handle_input(event, data);
          } else {
            write_vio->reenable();
            read_vio->reenable();
          }
          break;
        }
      case PARSE_ERROR:
      case PARSE_DONE:
        // These only occur if eof is set to true on the
        //  call to mime_scanner_get.  Since we never set
        //  eof to true, this case should never occur
        ink_release_assert(0);
        break;
      }

      break;
    }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    kill();
    break;
  }

  return EVENT_DONE;
}

void
RafCont::free_cmd_strs(char **argv, int argc)
{

  for (int i = 0; i < argc; i++) {
    arena.str_free(argv[i]);
  }
}

// int RafCont::process_raf_cmd(char* cmd_s, char* cmd_e)
//
//    Process raf cmd input.
//
//    Returns 1 is we are to keep the socket open & continue reading
//      cmds and 0 if we are to close the socket after sending the
//      response
//
int
RafCont::process_raf_cmd(const char *cmd_s, const char *cmd_e)
{

  int arg_len;
  const char *lastp = NULL;

  int argc = 0;
  char *cmd_ptrs[8];
  memset(cmd_ptrs, 0, sizeof(cmd_ptrs));

  // Loop over the input extracting the first four arguments
  for (int i = 0; i < 8; i++) {

    // Make sure we haven't run out of data
    if (cmd_s >= cmd_e) {
      break;
    }

    arg_len = raf_decodelen(cmd_s, cmd_e - cmd_s, &lastp);
    if (arg_len <= 0) {
      break;
    }

    argc++;
    cmd_ptrs[i] = arena.str_alloc(arg_len);
    arg_len = raf_decode(cmd_s, cmd_e - cmd_s, cmd_ptrs[i], arg_len, &lastp);
    cmd_s = lastp;
    (cmd_ptrs[i])[arg_len] = '\0';
  }

  // Trim CRLF off of last argument
  if (argc > 0) {
    char *last_arg = cmd_ptrs[argc - 1];
    int last_len = strlen(last_arg);

    if (last_len >= 2) {
      if (last_arg[last_len - 1] == '\n') {
        if (last_arg[last_len - 2] == '\r') {
          last_arg[last_len - 2] = '\0';
        } else {
          last_arg[last_len - 1] = '\0';
        }
      }
    }
  }
  // Send error if insufficient number of arguments
  if (argc < 2) {
    char *id;
    if (argc < 1) {
      id = "?";
    } else {
      id = cmd_ptrs[0];
    }
    output_raf_error(id, "No command sent");
    free_cmd_strs(cmd_ptrs, argc);
    return 1;
  }


  RafCmdHandler jump_point;
  for (int j = 0; j < raf_cmd_entries; j++) {
    if (strcmp(cmd_ptrs[1], raf_cmd_table[j].name) == 0) {
      jump_point = raf_cmd_table[j].handler;
      int r = (this->*jump_point) (cmd_ptrs, argc);
      free_cmd_strs(cmd_ptrs, argc);
      return r;
    }
  }

  char msg[257];
  ink_snprintf(msg, 256, "Unknown cmd '%s' sent", cmd_ptrs[1]);
  msg[256] = '\0';
  output_raf_error(cmd_ptrs[0], msg);
  free_cmd_strs(cmd_ptrs, argc);
  return 1;
}

void
RafCont::process_query_stat(const char *id, char *var)
{

  char val_output[257];
  bool r = false;
  RecDataT val_type;
  int rec_err = RecGetRecordDataType(var, &val_type);
  r = (rec_err == REC_ERR_OKAY);

  if (r) {
    switch (val_type) {
    case RECD_INT:
    case RECD_COUNTER:
      {
        RecInt i = 0;
        bool tmp = false;

        if (val_type == RECD_COUNTER) {
          i = REC_readCounter(var, &tmp);
        } else {
          i = REC_readInteger(var, &tmp);
        }
        ink_snprintf(val_output, 256, "%lld", i);
        break;
      }
    case RECD_LLONG:
      {
        bool tmp = false;
        RecLLong i = REC_readLLong(var, &tmp);
        ink_snprintf(val_output, 256, "%lld", i);
        break;
      }
    case RECD_FLOAT:
      {
        bool tmp = false;
        RecFloat f = REC_readFloat(var, &tmp);
        ink_snprintf(val_output, 256, "%f", f);
        break;
      }
    case RECD_STRING:
      {
        bool tmp;
        char *s = REC_readString(var, &tmp);
        ink_snprintf(val_output, 256, "%s", s);
        val_output[256] = '\0';
        xfree(s);
        break;
      }
    default:
      r = false;
      break;
    }
  }

  if (r) {
    output_resp_hdr(id, 0);
    output_raf_arg(var);
    output_raf_arg(val_output);
  } else {
    char msg[257];
    ink_snprintf(msg, 256, "%s not found", var);
    msg[256] = '\0';
    output_raf_error(id, msg);
  }
}

int
RafCont::process_congestion_cmd(char **argv, int argc)
{
  const char list_cmd[] = "list";
  const char remove_cmd[] = "remove";

  int qstring_index = 2;
  while (qstring_index < argc) {
    if ((argv[qstring_index])[0] == '-') {
      qstring_index++;
    } else {
      break;
    }
  }
  if (qstring_index >= argc) {
    output_raf_error(argv[0], "no arguments sent to congest cmd");
    return 1;
  }

  if (strncmp(argv[qstring_index], list_cmd, sizeof(list_cmd) - 1) == 0) {
    qstring_index++;
    process_congest_list(argv + qstring_index, argc - qstring_index);
  } else if (strncmp(argv[qstring_index], remove_cmd, sizeof(remove_cmd) - 1) == 0) {
    qstring_index++;
    process_congest_remove_entries(argv + qstring_index, argc - qstring_index);
  } else {
    char msg[257];
    ink_snprintf(msg, 256, "Node %s not found", argv[qstring_index]);
    msg[256] = '\0';
    output_raf_error(argv[0], msg);
  }
  return 1;
}

void
RafCont::process_congest_remove_entries(char **argv, int argc)
{
  int index = 0;
  while (index < argc) {
    remove_congested_entry(argv[index++], output_buffer);
  }
}

void
RafCont::process_congest_list(char **argv, int argc)
{
  int list_format = 0;
  if (argc > 0) {
    if (strncasecmp(argv[0], "short", 5) == 0) {
      list_format = 0;
    } else if (strncasecmp(argv[0], "long", 4) == 0) {
      list_format = 1;
      if (argc > 1) {
        list_format = atoi(argv[1]);
      }
    }
  }
  Action *action = get_congest_list(this, output_buffer, list_format);
  if (action == ACTION_RESULT_DONE) {
    // state_handle_congest_list(CONGESTION_EVENT_CONGESTED_LIST_DONE, NULL);
  } else {
    pending_action = action;
  }
}

void
RafCont::process_query_deadhosts(const char *id)
{
  Action *action = get_congest_list(this, output_buffer, 0);
  if (action == ACTION_RESULT_DONE) {
    // state_handle_congest_list(CONGESTION_EVENT_CONGESTED_LIST_DONE, NULL);
  } else {
    pending_action = action;
  }
}

int
RafCont::process_query_cmd(char **argv, int argc)
{

  const char stats[] = "/stats/";
  const char config[] = "/conf/yts/";


  int qstring_index = 2;
  while (qstring_index < argc) {
    if ((argv[qstring_index])[0] == '-') {
      qstring_index++;
    } else {
      break;
    }
  }

  if (qstring_index >= argc) {
    output_raf_error(argv[0], "no arguments sent to query cmd");
    return 1;
  }

  if (strcmp(argv[qstring_index], "/*") == 0) {
    output_resp_hdr(argv[0], 0);
    output_raf_msg(" /stats {} /conf/yts {}");
  } else if (strcmp(argv[qstring_index], "deadhosts") == 0) {
    process_query_deadhosts(argv[0]);
    // return 0;
  } else {
    if (strncmp(argv[qstring_index], stats, sizeof(stats) - 1) == 0) {
      char *var = argv[qstring_index] + sizeof(stats) - 1;
      process_query_stat(argv[0], var);
    } else if (strncmp(argv[qstring_index], config, sizeof(config) - 1) == 0) {
      /* Current both stats & config use the same routine to get their info */
      char *var = argv[qstring_index] + sizeof(config) - 1;
      process_query_stat(argv[0], var);
    } else {
      char msg[257];
      ink_snprintf(msg, 256, "Node %s not found", argv[qstring_index]);
      msg[256] = '\0';
      output_raf_error(argv[0], msg);
    }
  }

  return 1;
}

int
RafCont::process_exit_cmd(char **argv, int argc)
{
  output_resp_hdr(argv[0], 0);
  output_raf_arg("Bye!");
  return 0;
}

int
RafCont::process_isalive_cmd(char **argv, int argc)
{
  output_resp_hdr(argv[0], 0);
  output_raf_arg("alive");
  return 1;
}

// void RafCont::output_raf_error(const char* msg)
//
void
RafCont::output_raf_error(const char *id, const char *msg)
{
  output_resp_hdr(id, 1);
  output_raf_msg(msg);
}


void
RafCont::output_resp_hdr(const char *id, int result_code)
{

  output_buffer->write(id, strlen(id));

  if (result_code<0 || result_code> 1) {
    result_code = 1;
  }
  // len= space + result code(0 or 1) + space + terminator
  char buf[4];
  snprintf(buf, sizeof(buf), " %d ", result_code);

  // Don't output trailing space for success
  if (result_code == 0) {
    output_buffer->write(buf, 2);
  } else {
    output_buffer->write(buf, 3);
  }
}

// void RafCont::output_raf_arg(const char* arg)
//  
//  Outputs an encoded raf argument.  Adds a leading space to it
//
void
RafCont::output_raf_arg(const char *arg)
{
  int len = raf_encodelen(arg, -1, 0);
  char *encd = (char *) arena.alloc(len + 1);
  encd[0] = ' ';
  int elen = raf_encode(arg, -1, encd + 1, len, 0);
  output_buffer->write(encd, elen + 1);
  arena.free(encd, len + 1);
}

// void RafCont::output_raf_msg(const char* arg)
//
//   outputs unencoded raf msg (for error msgs)
//
void
RafCont::output_raf_msg(const char *arg)
{
  output_buffer->write(arg, strlen(arg));
}
