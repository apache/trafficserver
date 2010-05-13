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

   Raf.h

   Description:


 ****************************************************************************/

#ifndef _TS_RAF_H_
#define _TS_RAF_H_

#include "P_Net.h"

#include "MIME.h"

class RafAcceptCont:public Continuation
{
public:
  RafAcceptCont();
  ~RafAcceptCont();
  void start(int accept_port);
  int state_handle_accept(int event, void *data);
private:
    Action * accept_action;
  int accept_port;
};

class RafCont:public Continuation
{
public:
  RafCont(NetVConnection * nvc);
  ~RafCont();
  void kill();
  void run();

  int main_handler(int event, void *data);
  int state_handle_input(int event, void *data);
  int state_handle_output(int event, void *data);
  int state_handle_congest_list(int event, void *data);
  int process_congestion_cmd(char **argv, int argc);
  int process_raf_cmd(const char *cmd_s, const char *cmd_e);
  int process_query_cmd(char **argv, int argc);
  int process_exit_cmd(char **argv, int argc);
  int process_isalive_cmd(char **argv, int argc);

private:
  void free_cmd_strs(char **argv, int argc);
  void process_query_stat(const char *id, char *var);
  void process_query_deadhosts(const char *id);
  void process_congest_list(char **argv, int argc);
  void process_congest_remove_entries(char **argv, int argc);

  void output_raf_error(const char *id, const char *msg);
  void output_resp_hdr(const char *id, int result_code);
  void output_raf_arg(const char *arg);
  void output_raf_msg(const char *arg);

  NetVConnection *net_vc;
  VIO *read_vio;
  VIO *write_vio;

  MIMEScanner scanner;
  Arena arena;

  MIOBuffer *input_buffer;
  IOBufferReader *input_reader;
  MIOBuffer *output_buffer;
  Action *pending_action;
};

typedef int (RafCont::*RafCmdHandler) (char **argv, int argc);

void start_raf();

#endif
