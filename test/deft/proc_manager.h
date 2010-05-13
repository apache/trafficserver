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

   proc_manager.h

   Description:


 ****************************************************************************/

#ifndef _PROC_MANAGER_H_
#define _PROC_MANAGER_H_

#include "sio_loop.h"
#include "sio_raf_server.h"
#include "Ptr.h"

class AcceptHandler:public FD_Handler
{
public:
  AcceptHandler();
  ~AcceptHandler();

  void start(int port);
  void stop();
  void handle_accept(s_event_t, void *);
};

class RafCmd;
class ProcRecord;
class sio_buffer;
class Tokenizer;

struct PortsAvail
{
  int first;
  int last;
};

class NetCmdHandler:public SioRafServer
{
public:
  NetCmdHandler();
  virtual ~ NetCmdHandler();

  void handle_execute_stop(s_event_t, void *);
  void handle_send_log(s_event_t event, void *data);

  void handle_create_completion(s_event_t, void *);
  void handle_create_rundir_rm(s_event_t, void *);
  void handle_install_completion(s_event_t, void *);
  void handle_take_pkg(s_event_t, void *);
  void handle_get_file(s_event_t event, void *data);
  void handle_put_file(s_event_t event, void *data);

  void process_start_cmd(RafCmd * cmd);
  void process_stop_cmd(RafCmd * cmd);
  void process_create_cmd(RafCmd * cmd);
  void process_destroy_cmd(RafCmd * cmd);
  void process_run_cmd(RafCmd * cmd);
  void process_install_cmd(RafCmd * cmd);
  void process_log_get_cmd(RafCmd * cmd);
  void process_take_pkg_cmd(RafCmd * cmd);
  void process_show_pkgs_cmd(RafCmd * cmd);
  void process_query_cmd(RafCmd * cmd);
  void process_exit_cmd(RafCmd * cmd);
  void process_shutdown_cmd(RafCmd * cmd);
  void process_arch_cmd(RafCmd * cmd);
  void process_isalive_cmd(RafCmd * cmd);
  void process_alloc_port(RafCmd * cmd);
  void process_get_file_cmd(RafCmd * cmd);
  void process_put_file_cmd(RafCmd * cmd);
  void process_stat_file_cmd(RafCmd * cmd);

protected:
    virtual void dispatcher();

private:
  void create_config_and_install();

  int check_install_file_extension(const char *file_name, const char **ext_ptr);
  void fill_log_resp_buffer(int eof);

  /* Query Stuff */
  bool handle_query_process_info(Tokenizer * slash_tok, RafCmd * raf_resp);
  void output_query_process_info(ProcRecord * pr, const char *q_proc_value, RafCmd * raf_resp, int *next_index);
  void output_query_process_int(ProcRecord * pr, const char *q_proc_value,
                                RafCmd * raf_resp, int *next_index, int value);
  void output_query_process_str(ProcRecord * pr, const char *q_proc_value,
                                RafCmd * raf_resp, int *next_index, const char *value);

  /* Run cmd struff */
  char *generate_anon_instance_name();
  char *setup_anon_run_dir();
  char *find_anon_binary_path(const char *binary);

  // Create cmd vars
    NonAtomicPtr<ProcRecord> create_prec;

  // Take_Pkg cmd vars
  int pkg_fd;
  int pkg_len_left;

  // Get_File cmd vars
  int get_fd;
  int get_len_left;

  // Put_file cmd
  int put_fd;
  int put_errno;
  int put_input_left;
  int put_output_left;

  // Install cmd vars
  char *link_content;
  char *unpacked_dir;

  // run cmd vars
  int next_anon_id;

  // Stop cmd vars
  S_Action *proc_watch;
  S_Event *timer_event;
    NonAtomicPtr<ProcRecord> stop_prec;

  // log_get cmd vars
  int log_fd;
  int log_read_complete;
  char *success_prefix;
  int success_prefix_len;
  sio_buffer *log_read_buffer;

};


class LogHandler:public FD_Handler
{
public:
  LogHandler();
  ~LogHandler();
  void start(ProcRecord * pr, int new_fd, const char *id);
  void handle_log_data(s_event_t, void *);
  void output_log_line(char *start, char *end);

private:
    NonAtomicPtr<ProcRecord> proc_record;
  const char *stream_id;
  sio_buffer *read_buffer;
};

enum proc_category_t
{
  PC_UNKNOWN = 0,
  PC_MANAGED_PROCESS,           // an process we are directly managing
  PC_INSTALL_PROCESS,           // instance installer
  PC_UTLIITY_PROCESS            // does internal work ie: tar
};

enum proc_port_bind_t
{
  PROC_PORT_BIND_INT,
  PROC_PORT_BIND_STRING
};


// struct ProcPortBinding
//
//   The name "port binding" is now a misnomer
//     as the functionality has been extended to
//     arbitrary strings
//
struct ProcPortBinding
{
  ProcPortBinding();
  ~ProcPortBinding();

  char *name;
  proc_port_bind_t bind_type;
  union
  {
    char *str;
    int port;
  } value;

    Link<ProcPortBinding> link;
};

enum proc_status_t
{
  PROC_STATUS_CREATED,
  PROC_STATUS_RUNNING,
  PROC_STATUS_STOPPING,
  PROC_STATUS_STOPPED,
  PROC_STATUS_FAIL
};

enum install_status_t
{
  PR_NO_INSTALL = 0,
  PR_INSTALL_RUNNING,
  PR_INSTALL_SUCCESS,
  PR_INSTALL_FAIL
};

enum rundir_result_t
{
  PRR_RM_RUN_DIR = 0,
  PRR_CONTINUE = 1,
  PRR_ERROR = 2
};

class ProcRecord:public NonAtomicRefCountObj
{
public:
  ProcRecord();
  ~ProcRecord();

  const char *start_process();
  int stop_process();
  int restart();

  const char *init_managed_proc(const char *iname);
  rundir_result_t init_managed_rundir();

  char *find_installer();
  S_Action *run_installer(S_Continuation *, const char *installer_name);
  void process_installer_output(sio_buffer *);

  int write_config(const char *config);

  S_Action *set_watch(S_Continuation *);
  void notify_watchers();
    DLL<S_Action> notify_list;

  pid_t pid;
  char *instance_name;

  proc_category_t proc_category;
  proc_status_t proc_status;
  install_status_t install_status;

    NonAtomicPtr<ProcRecord> parent;

  int exit_status;
  int destroy_on_proc_exit;

  char *run_dir;
  int no_run_dir;

  int no_install;
  char *config_blob;
  char *config_file;

  char *local_path;
  char *binary_name;
  char *start_args;
  char *tmp_start_args;

  char *package_name;
  char *package_dir;

  char *start_cmd;

  char **env_vars;

    Link<ProcRecord> link;

    DLL<ProcPortBinding> port_bindings;

private:
  const char *create_pipe(int *pipe_array, const char *id);
  void set_nonblock(int fd, const char *id);

  void process_installer_cmd_line(const char *val_start, const char *val_end);
  void process_installer_port_binding(const char *val_start, const char *val_end);
  void process_installer_ports_used(const char *val_start, const char *val_end);
  void process_installer_env_vars(const char *val_start, const char *val_end);
};

//
// Package Installer Stuff
//
class InstallerHandler;
class InstallerSendInput:public FD_Handler
{
public:
  InstallerSendInput();
  ~InstallerSendInput();

  void start_send(InstallerHandler * master_arg, ProcRecord * p_rec, int fd_arg);

  void handle_send(s_event_t, void *);

private:
  void add_pair(const char *name, const char *value);

  sio_buffer *send_buffer;
  InstallerHandler *master;
};

class InstallerReadOutput:public FD_Handler
{
public:
  InstallerReadOutput();
  ~InstallerReadOutput();

  void start_read(InstallerHandler * master_in, int fd);
  void handle_read(s_event_t, void *);

  sio_buffer *read_buffer;

private:
    InstallerHandler * master;

};

class InstallerHandler:public S_Continuation
{
public:
  InstallerHandler();
  ~InstallerHandler();

  void init(ProcRecord * prec, int input_fd, int output_fd);
  void watch_installer(s_event_t, void *);

  void set_read_status(int status);
  void set_send_status(int status);

  void handle_install_error();
  void handle_install_success();

    NonAtomicPtr<ProcRecord> installer_rec;
private:
    bool error_seen;
  int read_status;              //<0 is error; == 0 is running;> 0 is success
  int send_status;              //<0 is error; == 0 is running;> 0 is success

  S_Event *timeout_event;
  S_Action *watch_proc;

  InstallerReadOutput *output_reader;
  InstallerSendInput *input_sender;
};

// class EventForwarder : public S_Continuation
//
//   Since reentrant callbacks are prohibited, we need a way to easliy
//     callbacks in the future when are prohibited from doing so now
//     due to still running on the same stack
//
class EventForwarder:public S_Continuation
{
public:
  EventForwarder();
  ~EventForwarder();
  S_Action *forward_event(S_Continuation * cont, s_event_t e, void *d);
  void handle_event(s_event_t, void *);
private:
    S_Continuation * cont;
  s_event_t event;
  void *data;
};

S_Action *recursive_rm_dir(const char *dir_name);

class RecursiveRmDir:public S_Continuation
{
public:
  RecursiveRmDir();
  ~RecursiveRmDir();
  S_Action *do_remove_dir(S_Continuation * cont, const char *dir, const char *tag);
  void handle_rm_complete(s_event_t, void *);
  static char *rm_bin_path;
private:
  char *dir;
  ProcRecord *rm_proc;
  S_Action *rm_proc_action;
  S_Event *timer_event;
  S_Action action;
};

void PM_Fatal(const char *fmt_str, ...);
void PM_Error(const char *fmt_str, ...);
void PM_Warning(const char *fmt_str, ...);
void PM_Note(const char *fmt_str, ...);


#endif
