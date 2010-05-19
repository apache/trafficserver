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

   proc_manager.cc

   Description:


 ****************************************************************************/

// Avoiding getting the whole libinktomi++ header file since
//  it causes problems with Sunpro 5.0
#define	_inktomiplus_h_

#include "proc_manager.h"
#include "sio_buffer.h"
#include "raf_cmd.h"
#include "test_utils.h"
#include "log_sender.h"

#include "Diags.h"
#include "ink_args.h"
#include "snprintf.h"
#include "rafencode.h"
#include "InkTime.h"
#include "ParseRules.h"
#include "Ptr.h"
#include "Tokenizer.h"
#define INK_LOCKFILE_INCLUDE_REDUCED
#include "ink_lockfile.h"
#undef INK_LOCKFILE_INCLUDE_REDUCED

/* Globals */
int num_active_process = 0;
DLL<ProcRecord> process_list;

AcceptHandler *accept_handler;
LogSender *log_sender = NULL;
Lockfile *lockfile = NULL;

static PortsAvail ports_avail;
static PortsAvail orig_ports_avail;

static char stuff_install_dir[1024] = "";
static char stuff_run_dir[1024] = "";
static char stuff_log_dir[1024] = "";
static char log_file[1024] = "";

/* Constants */
static const int SIZE_32K = 32768;

/* Argument Stuff */
int control_port = 12300;
int quiet_mode = 0;
int remote_start = 0;
static char error_tags[1024];
static char action_tags[1024];
static char stuff_dir[1024] = "proc_stuff";
static char log_collator[1024];
static int kill_wait = 2;

ArgumentFunction print_usage;
ArgumentDescription argument_descriptions[] = {
  {"port", 'p', "Control Port", "I", &control_port, NULL, NULL},
  {"dir", 'd', "Stuff Directory", "S1023", stuff_dir, NULL, NULL},
  {"quiet", 'q', "Quite Mode", "F", &quiet_mode, NULL, NULL},
  {"remote_start", 'r', "Started by test_exec", "F", &remote_start, NULL, NULL},
  {"log_collator", 'L', "Log Collator", "S0123", log_collator, NULL, NULL},
  {"debug_tags", 'T', "Debug Tags", "S1023", error_tags, "DEFT_PM_DEBUG", NULL},
  {"kill_wait", 'k', "Time to wait for a kill to finish",
   "I", &kill_wait, NULL, NULL},
  {"action_tags", 'B', "Behavior Tags", "S1023", action_tags, NULL, NULL},
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage}
};
int n_argument_descriptions = SIZE(argument_descriptions);

void
PM_output_log_line(const char *start, const char *end, const char *iname, const char *stream_id)
{

  struct timeval tp;
  char *buffer, timestamp_buf[48];

  ink_gethrtimeofday(&tp, NULL);
  time_t cur_clock = (time_t) tp.tv_sec;
  buffer = ink_ctime_r(&cur_clock, timestamp_buf);
  sprintf(&(timestamp_buf[19]), ".%03d", (int) (tp.tv_usec / 1000));

  char prefix_buffer[1024];
  int r = snprintf(prefix_buffer, 1024, "[%s %s %s] ",
                       timestamp_buf, iname, stream_id);

  log_sender->add_to_output_log(prefix_buffer, prefix_buffer + r);
  log_sender->add_to_output_log(start, end);

  if (end > start && *(end - 1) != '\n') {
    const char *new_line_buf = "\n";
    log_sender->add_to_output_log(new_line_buf, new_line_buf + 1);
  }
}

void
PM_log_line_va(const char *level, const char *format_str, va_list ap)
{

  char line_buf[2048];
  int r = vsnprintf(line_buf, 2047, format_str, ap);
  if (r >= 2047) {
    line_buf[2047] = '\0';
    r = 2047;
  }

  PM_output_log_line(line_buf, line_buf + r, "proc_manager", level);
}

void
PM_Note(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Note, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  PM_log_line_va("Note", format_str, ap);
  va_end(ap);

}

void
PM_Warning(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Warning, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  PM_log_line_va("Warning", format_str, ap);
  va_end(ap);

}

void
PM_Error(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Error, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  PM_log_line_va("Error", format_str, ap);
  va_end(ap);

}

void
PM_Fatal(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Fatal, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  PM_log_line_va("Fatal", format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  ink_fatal_va(1, (char *) format_str, ap);
  va_end(ap);
}

ProcRecord *
find_instance(const char *name)
{
  ProcRecord *c = process_list.head;

  while (c != NULL) {
    if (strcasecmp(c->instance_name, name) == 0) {
      return c;
    }
    c = c->link.next;
  }

  return NULL;
}

void
add_instance(ProcRecord * pr)
{
  Debug("process", "Adding instance '%s'", pr->instance_name);

  // Our Lists don't handle refcounts automatically so do it manually
  pr->refcount_inc();
  process_list.push(pr);
}

void
remove_instance(ProcRecord * pr)
{

  Debug("process", "Removing instance '%s'", pr->instance_name);

  // Our Lists don't handle refcounts automatically so do it manually
  process_list.remove(pr);
  if (pr->refcount_dec() == 0) {
    pr->free();
  }


  if (is_debug_tag_set("process_list")) {
    sio_buffer plist;

    if (process_list.head == NULL) {
      plist.fill(" (empty)", 8);
    } else {
      ProcRecord *tmp = process_list.head;
      while (tmp) {
        plist.fill(" ", 1);
        plist.fill(tmp->instance_name, strlen(tmp->instance_name));
        tmp = tmp->link.next;
      }
    }
    plist.fill("", 1);
    Debug("process_list", "%s", plist.start());
  }
  // If we've removed all our processes, we need to reclaim
  //   ports since we can easily run out when running a test group
  //   if we do not reclaim
  if (process_list.head == NULL) {
    memcpy(&ports_avail, &orig_ports_avail, sizeof(PortsAvail));
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

  accept_handler->stop();
  log_sender->flush_output();
  log_sender->close_output();

  ProcRecord *p = process_list.head;
  while (p != NULL) {
    if (p->pid > 0) {
      kill(p->pid, SIGTERM);
    }
    p = p->link.next;
  }
  exit(status);
}

AcceptHandler::AcceptHandler():
FD_Handler()
{
}

AcceptHandler::~AcceptHandler()
{
}

void
AcceptHandler::start(int port)
{

  this->fd = SIO::open_server(port);
  this->poll_interest = POLL_INTEREST_READ;
  this->my_handler = (SCont_Handler) & AcceptHandler::handle_accept;

  SIO::add_fd_handler(this);
}

void
AcceptHandler::stop()
{
  close(this->fd);
  this->fd = -1;
  this->poll_interest = POLL_INTEREST_NONE;;
  SIO::remove_fd_handler(this);
}

void
AcceptHandler::handle_accept(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_assert(this->fd == pfd->fd);

  /* FIX: should check for error in pfd revents */
  int new_fd = SIO::accept_sock(fd);

  if (new_fd > 0) {
    Debug("socket", "new accept on fd %d", fd);
    NetCmdHandler *new_cmd_h = new NetCmdHandler();
    new_cmd_h->start(new_fd);
  }
}

typedef void (NetCmdHandler::*CmdHandler_t) (RafCmd *);

struct cmd_dispatch_t
{
  const char *cmd;
  CmdHandler_t fptr;
};

cmd_dispatch_t cmd_dispatcher[] = {
  {"start", &NetCmdHandler::process_start_cmd},
  {"stop", &NetCmdHandler::process_stop_cmd},
  {"create", &NetCmdHandler::process_create_cmd},
  {"destroy", &NetCmdHandler::process_destroy_cmd},
  {"install", &NetCmdHandler::process_install_cmd},
  {"run", &NetCmdHandler::process_run_cmd},
  {"take_pkg", &NetCmdHandler::process_take_pkg_cmd},
  {"show_pkgs", &NetCmdHandler::process_show_pkgs_cmd},
  {"get_file", &NetCmdHandler::process_get_file_cmd},
  {"put_file", &NetCmdHandler::process_put_file_cmd},
  {"stat_file", &NetCmdHandler::process_stat_file_cmd},
  {"arch", &NetCmdHandler::process_arch_cmd},
  {"query", &NetCmdHandler::process_query_cmd},
  {"alloc_port", &NetCmdHandler::process_alloc_port},
  {"log_get", &NetCmdHandler::process_log_get_cmd},
  {"isalive", &NetCmdHandler::process_isalive_cmd},
  {"shutdown", &NetCmdHandler::process_shutdown_cmd},
  {"exit", &NetCmdHandler::process_exit_cmd},
  {"quit", &NetCmdHandler::process_exit_cmd},
  {"bye", &NetCmdHandler::process_exit_cmd},
  {NULL, NULL}
};

NetCmdHandler::NetCmdHandler():
pkg_fd(-1),
pkg_len_left(0),
get_fd(-1),
get_len_left(0),
put_fd(-1),
put_errno(0),
put_input_left(0),
put_output_left(0),
link_content(NULL),
unpacked_dir(NULL),
next_anon_id(0),
proc_watch(NULL),
timer_event(NULL),
stop_prec(NULL),
log_fd(-1), log_read_complete(0), success_prefix(NULL), success_prefix_len(0), log_read_buffer(NULL), SioRafServer()
{
}

NetCmdHandler::~NetCmdHandler()
{

  if (pkg_fd >= 0) {
    close(pkg_fd);
    pkg_fd = -1;
  }

  if (get_fd >= 0) {
    close(get_fd);
    get_fd = -1;
  }

  if (put_fd >= 0) {
    close(put_fd);
    put_fd = -1;
  }

  create_prec = NULL;

  if (link_content) {
    free(link_content);
    link_content = NULL;
  }

  if (unpacked_dir) {
    free(unpacked_dir);
    unpacked_dir = NULL;
  }

  if (proc_watch) {
    proc_watch->cancel();
  }

  if (timer_event) {
    timer_event->cancel();
  }

  stop_prec = NULL;
  ink_debug_assert(log_fd < 0);

  if (success_prefix) {
    free(success_prefix);
    success_prefix = NULL;
  }

  if (log_read_buffer) {
    delete log_read_buffer;
    log_read_buffer = NULL;
  }

}

void
NetCmdHandler::dispatcher()
{

  cmd_dispatch_t *cur = cmd_dispatcher;

  bool cmd_found = false;
  while (cur->cmd != NULL) {
    if (strcasecmp((*raf_cmd)[1], cur->cmd) == 0) {
      cmd_found = true;
      break;
    }
    cur++;
  }

  if (cmd_found) {
    CmdHandler_t func_ptr = cur->fptr;
    (this->*func_ptr) (raf_cmd);
  } else {
    send_raf_resp(raf_cmd, 1, "Unknown cmd '%s'", (*raf_cmd)[1]);
  }
}

void
NetCmdHandler::process_start_cmd(RafCmd * cmd)
{


  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to start cmd");
    return;
  }

  const char *instance_name = (*cmd)[2];

  ProcRecord *proc_record = find_instance(instance_name);

  if (!proc_record) {
    send_raf_resp(cmd, 1, "unknown instance '%s'", instance_name);
    return;
  }

  if (proc_record->pid > 0) {
    send_raf_resp(cmd, 1, "instance '%s' already running", instance_name);
    return;
  }

  ink_debug_assert(proc_record->proc_status != PROC_STATUS_RUNNING);

  for (int i = 3; i < num_args; i++) {

    if (i + 1 >= num_args && (*cmd)[i + 1] != NULL) {
      send_raf_resp(cmd, 1, "no argument to modifier '%s'", (*cmd)[i]);
      return;
    }

    if (strcasecmp((*cmd)[i], "args") == 0) {
      i += 1;
      if (proc_record->tmp_start_args) {
        free(proc_record->tmp_start_args);
      }
      proc_record->tmp_start_args = strdup((*cmd)[i]);
    } else {
      send_raf_resp(cmd, 1, "unknown modifier '%s' to start cmd", (*cmd)[i]);
      return;
    }
  }

  if (proc_record->local_path && proc_record->binary_name) {
    int size = strlen(proc_record->local_path) + 1 + strlen(proc_record->binary_name) + 1;
    proc_record->start_cmd = (char *) malloc(size);
    sprintf(proc_record->start_cmd, "%s/%s", proc_record->local_path, proc_record->binary_name);
  }

  Debug("process", "Start cmd for instance %s is '%s%s%s%s%s'",
        instance_name, proc_record->start_cmd,
        (proc_record->start_args) ? " " : "",
        (proc_record->start_args) ? proc_record->start_args : "",
        (proc_record->tmp_start_args) ? " " : "", (proc_record->tmp_start_args) ? proc_record->tmp_start_args : "");

  proc_record->start_process();
  send_raf_resp(cmd, 0, "started '%s'", instance_name);
}

void
NetCmdHandler::process_stop_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to create cmd");
    return;
  }

  const char *instance_name = (*cmd)[2];

  ProcRecord *tmp = find_instance(instance_name);

  if (tmp != NULL) {
    if (tmp->pid < 0) {
      send_raf_resp(cmd, 1, "instance '%s' already stopped", instance_name);
    } else {
      ink_debug_assert(tmp->proc_status == PROC_STATUS_RUNNING || tmp->proc_status == PROC_STATUS_STOPPING);

      tmp->proc_status = PROC_STATUS_STOPPING;

      // We aren't interested in further cmds while we are doing the stop
      poll_interest = POLL_INTEREST_NONE;
      my_handler = (SCont_Handler) & NetCmdHandler::handle_execute_stop;

      // We'll send a kill.  If we do not get a sig term within
      //   2 seconds, will go to SIGKILL
      int r = kill(tmp->pid, SIGTERM);
      if (r < 0) {
        send_raf_resp(cmd, 1, "kill failed for '%s'", instance_name, strerror(errno));
      } else {
        proc_watch = tmp->set_watch(this);
        timer_event = SIO::schedule_in(this, (kill_wait * 1000));
        stop_prec = tmp;
      }
    }
  } else {
    send_raf_resp(cmd, 1, "unknown instance '%s'", instance_name);
  }
}

void
NetCmdHandler::process_create_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 4) {
    send_raf_resp(cmd, 1, "insufficient arguments to create cmd");
    return;
  }

  const char *instance_name = (*raf_cmd)[2];
  ProcRecord *tmp = find_instance(instance_name);

  if (tmp != NULL) {
    send_raf_resp(cmd, 1, "instance '%s' already exists", instance_name);
    return;
  }

  ProcRecord *new_proc = new ProcRecord;

  for (int i = 3; i < num_args; i++) {

    if (strcasecmp((*cmd)[i], "no_rundir") == 0) {
      new_proc->no_run_dir = 1;
      continue;
    }

    if (strcasecmp((*cmd)[i], "no_install") == 0) {
      new_proc->no_install = 1;
      continue;
    }

    if (i + 1 >= num_args && (*cmd)[i + 1] != NULL) {
      send_raf_resp(cmd, 1, "no argument to modifier '%s'", (*cmd)[i]);
      delete new_proc;
      return;
    }

    if (strcasecmp((*cmd)[i], "config") == 0) {
      i += 1;
      new_proc->config_blob = strdup((*cmd)[i]);
    } else if (strcasecmp((*cmd)[i], "localpath") == 0) {
      i += 1;
      new_proc->local_path = strdup((*cmd)[i]);
    } else if (strcasecmp((*cmd)[i], "binary") == 0) {
      i += 1;
      new_proc->binary_name = strdup((*cmd)[i]);
    } else if (strcasecmp((*cmd)[i], "package") == 0) {
      i += 1;
      new_proc->package_name = strdup((*cmd)[i]);
    } else {
      send_raf_resp(cmd, 1, "unknown modifier '%s' to create cmd", (*cmd)[i]);
      delete new_proc;
      return;
    }
  }

  add_instance(new_proc);

  const char *rmsg = new_proc->init_managed_proc(instance_name);
  if (rmsg != NULL) {
    send_raf_resp(cmd, 1, rmsg);
  }
  // There is an old run dir for the instance, we remove it before
  //   continuing with the install
  rundir_result_t prr = new_proc->init_managed_rundir();
  switch (prr) {
  case PRR_RM_RUN_DIR:
    {
      create_prec = new_proc;
      poll_interest = POLL_INTEREST_NONE;
      my_handler = (SCont_Handler) & NetCmdHandler::handle_create_rundir_rm;
      RecursiveRmDir *rrd = new RecursiveRmDir;
      rrd->do_remove_dir(this, new_proc->run_dir, instance_name);
      break;
    }
  case PRR_CONTINUE:
    create_prec = new_proc;
    create_config_and_install();
    break;
  case PRR_ERROR:
    send_raf_resp(cmd, 1, "failed to create run dir");
    break;
  default:
    ink_release_assert(0);
  }
}



void
NetCmdHandler::create_config_and_install()
{

  if (create_prec->config_blob) {
    if (create_prec->write_config(create_prec->config_blob) != 0) {
      send_raf_resp(raf_cmd, 1, "failed to create blob file");
      remove_instance(create_prec);
      create_prec = NULL;
      return;
    } else {
      free(create_prec->config_blob);
      create_prec->config_blob = NULL;
    }
  }

  if (create_prec->no_install == 0) {

    char *installer_name = NULL;

    if (create_prec->package_name) {
      installer_name = create_prec->find_installer();
    } else {
      remove_instance(create_prec);
      create_prec = NULL;
      send_raf_resp(raf_cmd, 1, "no package instantitor specified for '%s'", create_prec->instance_name);
      return;
    }

    if (installer_name == NULL) {
      remove_instance(create_prec);
      send_raf_resp(raf_cmd, 1, "could not find instantitor for '%s'", create_prec->instance_name);
    } else {
      poll_interest = POLL_INTEREST_NONE;
      my_handler = (SCont_Handler) & NetCmdHandler::handle_create_completion;

      ink_debug_assert(proc_watch == NULL);
      proc_watch = create_prec->run_installer(this, installer_name);
      free(installer_name);
    }
  } else {
    send_raf_resp(raf_cmd, 0, "created instance '%s'", create_prec->instance_name);
  }

  create_prec = NULL;
}

void
NetCmdHandler::handle_create_rundir_rm(s_event_t event, void *data)
{

  proc_watch = NULL;
  switch (event) {
  case SEVENT_RMDIR_SUCCESS:
  case SEVENT_RMDIR_FAILURE:
    {

      int err;
      const char *rmsg = create_or_verify_dir(create_prec->run_dir, &err);

      if (rmsg != NULL) {
        PM_Error("%s %s for %s : %s", rmsg, create_prec->run_dir, create_prec->instance_name, strerror(err));
        create_prec = NULL;
        remove_instance(create_prec);
        send_raf_resp(raf_cmd, 1, "failed to create run dir");
      } else {
        create_config_and_install();
      }
    }
    break;
  default:
    ink_release_assert(0);
  }
}

void
NetCmdHandler::handle_create_completion(s_event_t event, void *data)
{

  ink_debug_assert(event == SEVENT_PROC_STATE_CHANGE);
  proc_watch = NULL;

  ProcRecord *pr = (ProcRecord *) data;

  const char *instance_name = (*raf_cmd)[2];

  if (pr->install_status == PR_INSTALL_SUCCESS) {

    // Send the list of port bindings following the create response
    const char msg_format[] = "created instance '%s'";
    RafCmd *reply = new RafCmd;
    (*reply) (0) = strdup((*raf_cmd)[0]);
    (*reply) (1) = strdup("0");

    int len = sizeof(msg_format) + strlen(instance_name);
    (*reply) (2) = (char *) malloc(len);
    sprintf((*reply)[2], msg_format, instance_name);

    int i = 3;
    ProcPortBinding *pb = pr->port_bindings.head;
    while (pb != NULL) {
      (*reply) (i) = strdup(pb->name);

      if (pb->bind_type == PROC_PORT_BIND_STRING) {
        (*reply) (i + 1) = strdup(pb->value.str);
      } else {
        char port_buf[32];
        ink_debug_assert(pb->bind_type == PROC_PORT_BIND_INT);
        sprintf(port_buf, "%d", pb->value.port);
        (*reply) (i + 1) = strdup(port_buf);
      }
      i += 2;
      pb = pb->link.next;

    }

    send_raf_resp(reply);
    delete reply;
  } else {
    remove_instance(pr);
    PM_Error("failed to create instance '%s'", instance_name);
    send_raf_resp(raf_cmd, 1, "failed to create instance '%s'", instance_name);
  }
}

void
NetCmdHandler::process_destroy_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to destroy cmd");
    return;
  }

  const char *instance_name = (*cmd)[2];
  ProcRecord *tmp = find_instance(instance_name);

  if (tmp == NULL) {
    send_raf_resp(cmd, 1, "instance '%s' not found", instance_name);
    return;
  }

  if (tmp->pid != -1) {
    send_raf_resp(cmd, 1, "instance '%s' still running", instance_name);
    return;
  }

  remove_instance(tmp);

  // FIX - remove 'run dir' on file system

  send_raf_resp(cmd, 0, "instance '%s' destroyed", instance_name);
}

void
NetCmdHandler::process_install_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 4) {
    send_raf_resp(cmd, 1, "insufficient arguments to destroy cmd");
    return;
  }

  const char *package_name = (*cmd)[2];
  const char *file_name = (*cmd)[3];

  const char *file_name_end = file_name + strlen(file_name);
  const char *ext_ptr;
  int r = check_package_file_extension(file_name, &ext_ptr);

  if (r) {
    PM_Error("bad file extension to install cmd for %s", file_name);
    send_raf_resp(cmd, 1, "bad file extension for %s", file_name);
    return;
  }
  int ext_len = file_name_end - ext_ptr;

  if (link_content) {
    free(link_content);
  }
  int tmp_len = ext_ptr - file_name;
  link_content = (char *) malloc(tmp_len + 1);
  memcpy(link_content, file_name, tmp_len);
  link_content[tmp_len] = '\0';

  int len = strlen(stuff_dir) + 1 + 7 + 1 + strlen(package_name) + 1 + strlen(file_name) + 1;
  char *file_path = (char *) malloc(len);
  sprintf(file_path, "%s/install/%s/%s", stuff_dir, package_name, file_name);

  if (unpacked_dir) {
    free(unpacked_dir);
  }
  unpacked_dir = strdup(file_path);
  unpacked_dir[len - ext_len - 1] = '\0';

  do {
    r = access(file_path, R_OK | W_OK);
  } while (r < 0 && r == EINTR);

  if (r < 0) {
    PM_Error("cmd install failed for %s : %s", file_path, strerror(errno));
    send_raf_resp(cmd, 1, "could not access %s : %s", file_path, strerror(errno));
    free(file_path);
    return;
  }

  ProcRecord *untar = new ProcRecord;

  untar->package_dir = (char *) malloc(len);
  sprintf(untar->package_dir, "%s/install/%s", stuff_dir, package_name);
  untar->run_dir = strdup(untar->package_dir);

  len = 4 + strlen(package_name) + 1;
  untar->instance_name = (char *) malloc(len);
  sprintf(untar->instance_name, "tar_%s", package_name);

  untar->start_cmd = strdup("/bin/sh");

  const char sh_args_fmt[] = "-c \"gunzip -c %s | tar -xf -\"";

  len = sizeof(sh_args_fmt) + strlen(file_name);
  untar->start_args = (char *) malloc(len);
  sprintf(untar->start_args, sh_args_fmt, file_name);

  // FIX - should stick a timeout here as well
  untar->set_watch(this);

  poll_interest = POLL_INTEREST_NONE;
  my_handler = (SCont_Handler) & NetCmdHandler::handle_install_completion;

  add_instance(untar);
  untar->start_process();
}

void
NetCmdHandler::handle_install_completion(s_event_t event, void *data)
{

  ink_debug_assert(event == SEVENT_PROC_STATE_CHANGE);
  proc_watch = NULL;

  ProcRecord *pr = (ProcRecord *) data;
  const char *package_name = (*raf_cmd)[2];

  if (pr->exit_status != 0) {
    PM_Error("install failed for '%s'", package_name);
    send_raf_resp(raf_cmd, 1, "install failed for '%s'", package_name);
    return;
  }

  int r;
  do {
    r = access(unpacked_dir, F_OK);
  } while (r != 0 && errno == EINTR);

  if (r < 0) {
    PM_Error("install failed for %s - can not access unpacked dir: %s", package_name, strerror(errno));
    send_raf_resp(raf_cmd, 1, "install failed for %s - can not access unpacked dir: %s", package_name, strerror(errno));
    return;
  }

  int alen = strlen(stuff_dir) + 1 + 7 + 1 + strlen(package_name) + 1 + 6 + 1;
  char *active_link = (char *) malloc(alen);
  sprintf(active_link, "%s/install/%s/active", stuff_dir, package_name);

  struct stat stat_info;
  do {
    r = lstat(active_link, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r == 0) {
    do {
      r = unlink(active_link);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      PM_Warning("failed to unlink %s for %s", active_link, package_name);
    }
  }

  do {
    r = symlink(link_content, active_link);
  } while (r < 0 && errno == EINTR);

  free(active_link);

  if (r < 0) {
    PM_Error("install failed for %s - can not create active link: %s", package_name, strerror(errno));
    send_raf_resp(raf_cmd, 1, "install failed for %s - can not create active link: %s", package_name, strerror(errno));
    return;
  }

  send_raf_resp(raf_cmd, 0, "install succeeded for '%s'", package_name);
}

char *
NetCmdHandler::generate_anon_instance_name()
{
  char *result = (char *) malloc(32);
  sprintf(result, "anon%d", next_anon_id);
  next_anon_id++;
  return result;
}

char *
NetCmdHandler::setup_anon_run_dir()
{

  // FIX ME - buffer overruns
  char tmp[1024];

  snprintf(tmp, 1023, "%s/run/_anon", stuff_dir);
  tmp[1023] = '\0';

  int err;
  const char *rmsg = create_or_verify_dir(tmp, &err);

  if (rmsg != NULL) {
    PM_Error("%s %s : %s", rmsg, tmp, strerror(err));
    return NULL;
  }

  return strdup(tmp);
}

char *
NetCmdHandler::find_anon_binary_path(const char *binary)
{

  char tmp[1024];

  const char *path = getenv("PATH");
  Tokenizer path_tok(":");
  int num_path_els = path_tok.Initialize(path);

  for (int i = 0; i < num_path_els; i++) {
    snprintf(tmp, 1023, "%s/%s", path_tok[i], binary);
    tmp[1023] = '\0';

    int r;
    do {
      r = access(tmp, X_OK);
    } while (r < 0 && r == EINTR);

    if (r == 0) {
      return strdup(tmp);
    }
  }

  return NULL;
}

int
check_anon_binary(const char *binary)
{
  int r;
  do {
    r = access(binary, X_OK);
  } while (r < 0 && r == EINTR);

  if (r == 0) {
    return 1;
  }

  PM_Error("run cmd for %s failed : %s", binary, strerror(errno));
  return 0;
}

void
NetCmdHandler::process_run_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to run cmd");
    return;
  }

  const char *binary = (*raf_cmd)[2];
  const char *args = NULL;
  const char *master_instance = NULL;

  for (int i = 3; i < num_args; i++) {

    if (i + 1 >= num_args && (*cmd)[i + 1] != NULL) {
      send_raf_resp(cmd, 1, "no argument to modifier '%s'", (*cmd)[i]);
      return;
    }

    if (strcasecmp((*cmd)[i], "args") == 0) {
      i += 1;
      args = (*cmd)[i];
    } else if (strcasecmp((*cmd)[i], "master") == 0) {
      i += 1;
      master_instance = (*cmd)[i];
    } else {
      send_raf_resp(cmd, 1, "unknown modifier '%s' to run cmd", (*cmd)[i]);
      return;
    }
  }

  char *bin_path = NULL;
  char *run_dir = NULL;
  ProcRecord *master_pr = NULL;

  if (master_instance) {
    master_pr = find_instance(master_instance);
    if (master_pr == NULL) {
      send_raf_resp(cmd, 1, "instance '%s' not found", master_instance);
      return;
    }
  }


  if (binary[0] == '/') {
    bin_path = strdup(binary);
  } else if (master_pr) {
    int len = strlen(master_pr->run_dir) + 1 + strlen(binary);
    bin_path = (char *) malloc(len + 1);
    sprintf(bin_path, "%s/%s", master_pr->run_dir, binary);
  } else {

    bin_path = find_anon_binary_path(binary);
    if (bin_path == NULL) {
      send_raf_resp(cmd, 1, "%s not found in PATH", binary);
      return;
    }
  }

  if (check_anon_binary(bin_path) == 0) {
    send_raf_resp(cmd, 1, "%s not executable", bin_path);
    free(bin_path);
    return;
  }

  if (master_pr) {
    run_dir = strdup(master_pr->run_dir);
  } else {
    run_dir = setup_anon_run_dir();
    if (run_dir == NULL) {
      free(bin_path);
      send_raf_resp(cmd, 1, "unable to create run directory");
      return;
    }
  }

  char *iname = generate_anon_instance_name();

  ProcRecord *new_proc = new ProcRecord;

  new_proc->start_cmd = bin_path;
  new_proc->instance_name = strdup(iname);
  new_proc->proc_category = PC_MANAGED_PROCESS;
  new_proc->run_dir = run_dir;

  if (args) {
    new_proc->start_args = strdup(args);
  }

  add_instance(new_proc);

  Debug("process", "Run cmd for %s", bin_path);

  new_proc->start_process();
  send_raf_resp(cmd, 0, "%s", iname);
  free(iname);
}

// format of the get file cmd is "get_file [file_name]
//   response format:  <raf_id> <status_code> <length>
void
NetCmdHandler::process_get_file_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to get_file cmd");
    return;
  }

  const char *file_name = (*cmd)[2];

  int read_fd;
  do {
    read_fd = open(file_name, O_RDONLY);
  } while (read_fd < 0 && errno == EINTR);

  if (read_fd < 0) {
    send_raf_resp(cmd, 1, "open_failed: %s", strerror(errno));
    return;
  }

  int r;
  struct stat stat_info;

  do {
    r = fstat(read_fd, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    close(read_fd);
    send_raf_resp(cmd, 1, "stat_failed: %s", strerror(errno));
    return;
  }

  if (resp_buffer == NULL) {
    resp_buffer = new sio_buffer;
  }

  get_fd = read_fd;
  get_len_left = stat_info.st_size;

  Debug("get_file", "succeeded for %s : %d bytes", file_name, get_len_left);

  char tmp_buf[512];
  r = snprintf(tmp_buf, 511, "%s 0 %d\n", (*cmd)[0], get_len_left);
  tmp_buf[511] = '\0';
  this->resp_buffer->fill(tmp_buf, r);

  poll_interest = POLL_INTEREST_WRITE;
  my_handler = (SCont_Handler) & NetCmdHandler::handle_get_file;
}

// format of the stat_file cmd is "stat_file [file_name]
void
NetCmdHandler::process_stat_file_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to get_file cmd");
    return;
  }

  const char *file_name = (*cmd)[2];

  int r;
  struct stat stat_info;
  do {
    r = stat(file_name, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    send_raf_resp(cmd, 1, "stat_failed: %s", strerror(errno));
    return;
  }

  RafCmd reply;
  char num_buf[64];

  // Fix should encode field 0
  reply(0) = strdup((*cmd)[0]);
  reply(1) = strdup("0");
  reply(2) = strdup("size");

  snprintf(num_buf, 63, "%lld", (int64) stat_info.st_size);
  num_buf[63] = '\0';
  reply(3) = strdup(num_buf);

  reply(4) = strdup("mod_date");
  snprintf(num_buf, 63, "%b32d", (uint32) stat_info.st_mtime);
  num_buf[63] = '\0';
  reply(5) = strdup(num_buf);

  send_raf_resp(&reply);
}

// format of the get file cmd is "get_file [file_name]
//   request format:  <raf_id> put_file <filename> <length> <mode>
void
NetCmdHandler::process_put_file_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 4) {
    send_raf_resp(cmd, 1, "insufficient arguments to put_file cmd");
    return;
  }

  const char *file_name = (*cmd)[2];
  const char *file_length = (*cmd)[3];

  put_output_left = atoi(file_length);
  if (put_output_left == 0 && strcmp(file_length, "0") != 0) {
    send_raf_resp(cmd, 1, "bad lenght to put_file");
    return;
  }
  // Figure out how much more we have left to read.  We could have
  //   received all or part of the file with the request
  put_input_left = put_output_left;
  put_input_left -= cmd_buffer->read_avail();
  if (put_input_left < 0) {
    put_input_left = 0;
  }

  unsigned int file_mode = 0644;
  if (num_args >= 5) {
    unsigned int tmp = 0;
    const char *mode_str = (*cmd)[4];
    int r = sscanf(mode_str, "%o", &tmp);

    if (r == 1) {
      // We only want permission bits.  For now, we can't
      //   handle setuid or setgid  bits either
      unsigned int mode_mask = S_IRWXU | S_IRWXG | S_IRWXO;
      file_mode = tmp & mode_mask;
    } else {
      PM_Warning("Bad file mode argument to put_file : %s", mode_str);
    }
  }

  int write_fd;
  do {
    write_fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, (mode_t) file_mode);
  } while (write_fd < 0 && errno == EINTR);

  if (write_fd < 0) {
    send_raf_resp(cmd, 1, "open_failed: %s", strerror(errno));
    return;
  }

  put_fd = write_fd;

  poll_interest = POLL_INTEREST_READ;
  my_handler = (SCont_Handler) & NetCmdHandler::handle_put_file;

  handle_put_file(SEVENT_NONE, NULL);
}



void
NetCmdHandler::handle_get_file(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(this->fd == pfd->fd);
  ink_debug_assert(event == SEVENT_POLL || event == SEVENT_TIMER);

  int r;
  int read_avail = resp_buffer->read_avail();

  if (read_avail<SIZE_32K && get_len_left> 0) {
    int act_on = (get_len_left > SIZE_32K) ? SIZE_32K : get_len_left;

    resp_buffer->expand_to(act_on);
    do {
      r = read(get_fd, resp_buffer->end(), act_on);
    } while (r < 0 && errno == EINTR);

    if (r <= 0) {
      PM_Error("read for raf 'get_file' failed : %s", (r == 0) ? "eof" : strerror(errno));
      delete this;
      return;
    } else {
      resp_buffer->fill(r);
      get_len_left -= r;
    }
  }

  read_avail = resp_buffer->read_avail();

  if (read_avail > 0) {
    do {
      r = write(this->fd, resp_buffer->start(), read_avail);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      if (r != EAGAIN) {
        PM_Error("write for raf 'get_file' failed : %s", strerror(errno));
        delete this;
        return;
      }
    } else {
      resp_buffer->consume(r);
    }
  }

  read_avail = resp_buffer->read_avail();

  // Check to see if we are finished
  if (read_avail == 0 && get_len_left == 0) {
    response_complete();
  }
}

void
NetCmdHandler::handle_put_file(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(event == SEVENT_NONE || this->fd == pfd->fd);
  ink_debug_assert(event == SEVENT_POLL || event == SEVENT_NONE);

  int r;
  bool input_finished = false;

  // Handle the zero length file or entire file received with the request
  if (put_input_left == 0) {
    if (put_output_left == 0) {
      close(put_fd);
      put_fd = -1;
      send_raf_resp(raf_cmd, 0, "put succeeded");
      return;
    } else {
      input_finished = true;
    }
  }
  // Read more data
  if (event == SEVENT_POLL) {
    int todo = put_input_left;

    ink_debug_assert(todo > 0);
    if (todo > SIZE_32K) {
      todo = SIZE_32K;
    }
    cmd_buffer->expand_to(SIZE_32K);

    do {
      r = read(this->fd, cmd_buffer->end(), todo);
    } while (r < 0 && errno == EINTR);

    if (r <= 0) {
      PM_Error("read failed for put_file : %s", (r == 0) ? "eos" : strerror(errno));
      exit_mode = RAF_EXIT_CONN;
      close(put_fd);
      put_fd = -1;
      send_raf_resp(raf_cmd, 1, "read failed : %s", (r == 0) ? "eos" : strerror(errno));
      return;
    } else {
      cmd_buffer->fill(r);
      put_input_left -= r;

      if (put_input_left == 0) {
        input_finished = true;
      }
    }
  }
  // Write data
  ink_debug_assert(put_output_left != 0);
  if (put_output_left >= 0) {
    int avail = cmd_buffer->read_avail();
    int todo = avail;

    if (todo > put_output_left) {
      todo = put_output_left;
    }

    do {
      r = write(put_fd, cmd_buffer->start(), todo);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      put_errno = errno;
      put_output_left = -1;
      close(put_fd);
      put_fd = -1;
    } else {
      ink_debug_assert(todo == r);
      cmd_buffer->consume(r);
      put_output_left -= r;

      if (put_output_left == 0) {
        ink_debug_assert(input_finished == true && put_input_left == 0);
        close(put_fd);
        put_fd = -1;
        send_raf_resp(raf_cmd, 0, "put succeeded");
        return;
      }
    }
  }

  if (put_output_left < 0) {
    // The write previously failed.  Just consume the data
    int avail = cmd_buffer->read_avail();
    cmd_buffer->consume(avail);

    if (input_finished) {
      send_raf_resp(raf_cmd, 1, "put failed : %s", strerror(put_errno));
    }
  }
}

// format of the take pkg cmd is "take_pkg [pkg_name] [file_name] [length]
void
NetCmdHandler::process_take_pkg_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 5) {
    send_raf_resp(cmd, 1, "insufficient arguments to take_pkg cmd");
    return;
  }

  const char *package_name = (*cmd)[2];
  const char *file_name = (*cmd)[3];
  const char *length_str = (*cmd)[4];

  int len = atoi(length_str);

  if (len <= 0) {
    exit_mode = RAF_EXIT_CONN;  // Don't try to continue after a bad take_pkg
    send_raf_resp(cmd, 1, "bad length to take pkg cmd - disconnecting...");

    return;
  }

  if (strchr(file_name, '/') || *file_name == '\0') {
    send_raf_resp(cmd, 1, "invalid_file_name %s", file_name);
    exit_mode = RAF_EXIT_CONN;  // Don't try to continue after a bad take_pkg
    return;
  }
  // FIX ME - buffer overrun
  char tmp[1024];
  snprintf(tmp, 1023, "%s/install/%s", stuff_dir, package_name);
  tmp[1023] = '\0';

  int r;
  do {
    r = access(tmp, F_OK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    do {
      r = mkdir(tmp, 0755);
    } while (r < 0 && errno == EINTR);

    if (mkdir < 0) {
      exit_mode = RAF_EXIT_CONN;        // Don't try to continue after a bad take_pkg
      PM_Error("failed to create dir %s for '%s': %s", tmp, package_name, strerror(errno));
      send_raf_resp(cmd, 1, "failed to create dir %s for '%s': %s", tmp, package_name, strerror(errno));
      return;
    }
  }

  snprintf(tmp, 1023, "%s/install/%s/%s", stuff_dir, package_name, file_name);
  tmp[1023] = '\0';

  int output_fd;
  do {
    output_fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  } while (output_fd < 0 && errno == EINTR);

  if (output_fd < 0) {
    exit_mode = RAF_EXIT_CONN;  // Don't try to continue after a bad take_pkg
    PM_Error("failed to create pkg_file %s: %s", tmp, strerror(errno));
    send_raf_resp(cmd, 1, "failed to create pkg_file %s: %s", tmp, strerror(errno));
    return;
  }

  pkg_fd = output_fd;
  pkg_len_left = len;

  // FIX - what if entire body is already in the buffer?

  poll_interest = POLL_INTEREST_READ;
  my_handler = (SCont_Handler) & NetCmdHandler::handle_take_pkg;

  // It's possible that the entire pacakge body is already in the
  //   buffer.  Schedule an event so we always get a call to
  //   handle_take_pkg.  We do not call handle_take_pkg ourselves
  //   since causes problems with the consume in process_cmd()
  timer_event = SIO::schedule_in(this, 1);
}

void
NetCmdHandler::handle_take_pkg(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_assert(event == SEVENT_TIMER || this->fd == pfd->fd);

  ink_debug_assert(event == SEVENT_POLL || event == SEVENT_TIMER);

  if (timer_event) {
    if (event == SEVENT_POLL) {
      timer_event->cancel();
    }
    timer_event = NULL;
  }

  int r;
  int wavail = cmd_buffer->expand_to(32768);
  do {
    r = read(this->fd, cmd_buffer->end(), wavail);
  } while (r < 0 && errno == EINTR);

  if (r < 0 || r == 0) {
    if (r == 0 || errno != EAGAIN) {
      exit_mode = RAF_EXIT_CONN;        // Don't try to continue after a bad take_pkg
      PM_Error("read of pkg_file for %s failed: %s", (*raf_cmd)[2], strerror(errno));
      send_raf_resp(raf_cmd, 1, "read of pkg_file failed: %s", strerror(errno));
      return;
    }
  } else {
    cmd_buffer->fill(r);
  }


  if (cmd_buffer->read_avail() > 0) {
    int todo = cmd_buffer->read_avail();

    if (todo > pkg_len_left) {
      todo = pkg_len_left;
    }

    do {
      r = write(pkg_fd, cmd_buffer->start(), todo);
    } while (r < 0 && r == EINTR);

    if (r < 0) {
      if (r != EAGAIN) {
        exit_mode = RAF_EXIT_CONN;      // Don't try to continue after a bad take_pkg
        PM_Error("write to pkg_file for %s failed: %s", (*raf_cmd)[2], strerror(errno));
        send_raf_resp(raf_cmd, 1, "write to pkg_file failed: %s", strerror(errno));
        return;
      }
    } else {
      pkg_len_left -= r;
      cmd_buffer->consume(r);
    }
  }

  if (pkg_len_left == 0) {
    close(pkg_fd);
    pkg_fd = -1;
    send_raf_resp(raf_cmd, 0, "success");
  }
}

void
NetCmdHandler::process_show_pkgs_cmd(RafCmd * cmd)
{

  // FIX ME - buffer overrun
  char tmp[1024];
  snprintf(tmp, 1023, "%s/install", stuff_dir);
  tmp[1023] = '\0';

  DIR *d = opendir(tmp);

  if (d == NULL) {
    PM_Error("unable to open pkg dir: %s", strerror(errno));
    send_raf_resp(raf_cmd, 1, "unable to open pkg dir: %s", strerror(errno));
    return;
  }

  int r;
  char active_link[1024];
  char lc[1024];

  RafCmd *resp = new RafCmd;
  (*resp) (0) = strdup((*raf_cmd)[0]);
  (*resp) (1) = strdup("0");
  int i = 2;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
      continue;
    }

    snprintf(active_link, 1023, "%s/install/%s/active", stuff_dir, de->d_name);
    active_link[1023] = '\0';

    r = readlink(active_link, lc, 1023);

    if (r < 0) {
      Warning("failed to read active link for package %s : %s", de->d_name, strerror(errno));
    } else {
      lc[r] = '\0';
      (*resp) (i) = strdup(de->d_name);
      (*resp) (i + 1) = strdup(lc);
      i += 2;
    }
  }
  closedir(d);

  send_raf_resp(resp);
  delete resp;
}

void
NetCmdHandler::process_arch_cmd(RafCmd * cmd)
{

  char *a = get_arch_str();
  send_raf_resp(raf_cmd, 0, "%s", a);
  free(a);
}

void
NetCmdHandler::process_isalive_cmd(RafCmd * cmd)
{
  send_raf_resp(raf_cmd, 0, "alive");
}

void
NetCmdHandler::process_alloc_port(RafCmd * cmd)
{

  int return_port;

  if (ports_avail.first < ports_avail.last) {
    return_port = ports_avail.first;
    ports_avail.first++;

    char int_buf[32];
    sprintf(int_buf, "%d", return_port);
    send_raf_resp(cmd, 0, int_buf);

  } else {
    send_raf_resp(cmd, 1, "ports exhausted");
  }
}


void
NetCmdHandler::process_query_cmd(RafCmd * cmd)
{

  int num_args = cmd->length();

  if (num_args < 3) {
    send_raf_resp(cmd, 1, "insufficient arguments to query cmd");
    return;
  }

  const char *query_str = (*cmd)[2];

  Tokenizer slash_tok("/");
  int num_q_el = slash_tok.Initialize(query_str);

  if (num_q_el == 0) {
    send_raf_resp(cmd, 1, "malformed query");
    return;
  }

  RafCmd *raf_resp = new RafCmd;
  (*raf_resp) (0) = strdup((*cmd)[0]);
  (*raf_resp) (1) = strdup("0");

  const char *q_top_level = slash_tok[0];

  if (strcasecmp(q_top_level, "*") == 0 && num_q_el == 1) {
    (*raf_resp) (2) = strdup("*");
    (*raf_resp) (3) = strdup("/processes");
  } else if (strcasecmp(q_top_level, "processes") == 0) {
    if (num_q_el > 2) {
      handle_query_process_info(&slash_tok, raf_resp);
    } else {
      send_raf_resp(cmd, 1, "bad subpath for /processes");
      delete raf_resp;
      return;
    }
  } else {
    send_raf_resp(cmd, 1, "unknown query item '%s'", q_top_level);
    delete raf_resp;
    return;
  }

  send_raf_resp(raf_resp);
  delete raf_resp;
}

bool
NetCmdHandler::handle_query_process_info(Tokenizer * slash_tok, RafCmd * raf_resp)
{

  const char *q_proc_name = (*slash_tok)[1];
  const char *q_proc_value = (*slash_tok)[2];
  int next_index = 2;

  if (strcasecmp(q_proc_name, "*") == 0) {
    ProcRecord *pr = process_list.head;

    while (pr != NULL) {
      output_query_process_info(pr, q_proc_value, raf_resp, &next_index);
      pr = pr->link.next;
    }

  } else {
    ProcRecord *pr = find_instance(q_proc_name);

    if (pr == NULL) {
      send_raf_resp(raf_cmd, 1, "unknown process '%s'");
      return false;
    }

    output_query_process_info(pr, q_proc_value, raf_resp, &next_index);
  }

  return true;
}


void
NetCmdHandler::output_query_process_info(ProcRecord * pr, const char *q_proc_value, RafCmd * raf_resp, int *next_index)
{

  bool all_values = false;
  if (strcasecmp(q_proc_value, "*") == 0) {
    output_query_process_int(pr, "pid", raf_resp, next_index, pr->pid);

    output_query_process_int(pr, "exit_status", raf_resp, next_index, pr->exit_status);

    all_values = true;
  } else if (strcasecmp(q_proc_value, "pid") == 0) {
    output_query_process_int(pr, q_proc_value, raf_resp, next_index, pr->pid);
  } else if (strcasecmp(q_proc_value, "exit_status") == 0) {
    output_query_process_int(pr, q_proc_value, raf_resp, next_index, pr->exit_status);
  } else {
    output_query_process_str(pr, q_proc_value, raf_resp, next_index, "<unknown attribute>");
  }
}

void
NetCmdHandler::output_query_process_int(ProcRecord * pr,
                                        const char *q_proc_value, RafCmd * raf_resp, int *next_index, int value)
{
  char tmp[1024];

  snprintf(tmp, 1023, "/processes/%s/%s", pr->instance_name, q_proc_value);
  tmp[1023] = '\0';

  (*raf_resp) (*next_index) = strdup(tmp);
  (*next_index)++;

  sprintf(tmp, "%d", value);
  (*raf_resp) (*next_index) = strdup(tmp);
  (*next_index)++;
}


void
NetCmdHandler::output_query_process_str(ProcRecord * pr,
                                        const char *q_proc_value, RafCmd * raf_resp, int *next_index, const char *value)
{
  char tmp[1024];

  snprintf(tmp, 1023, "/processes/%s/%s", pr->instance_name, q_proc_value);
  tmp[1023] = '\0';

  (*raf_resp) (*next_index) = strdup(tmp);
  (*next_index)++;

  snprintf(tmp, 1023, "%s", value);
  (*raf_resp) (*next_index) = strdup(tmp);
  tmp[1023] = '\0';
  (*next_index)++;
}

void
NetCmdHandler::process_log_get_cmd(RafCmd * cmd)
{

  ink_debug_assert(log_fd < 0);

  do {
    log_fd = open(log_file, O_RDONLY);
  } while (log_fd < 0 && errno == EINTR);

  if (log_fd < 0) {
    send_raf_resp(raf_cmd, 1, "unable to open log file: %s", strerror(errno));
    return;
  }
  // FIX ME - should raf encode the identifier
  int id_len = strlen((*raf_cmd)[0]);
  success_prefix_len = id_len + 3;
  if (success_prefix) {
    free(success_prefix);
  }
  success_prefix = (char *) malloc(success_prefix_len + 1);
  memcpy(success_prefix, (*raf_cmd)[0], id_len);
  memcpy(success_prefix + id_len, " 0 ", 3);
  success_prefix[success_prefix_len] = '\0';

  if (resp_buffer == NULL) {
    resp_buffer = new sio_buffer;
  }

  if (log_read_buffer == NULL) {
    log_read_buffer = new sio_buffer;
  }

  poll_interest = POLL_INTEREST_WRITE;
  my_handler = (SCont_Handler) & NetCmdHandler::handle_send_log;
  handle_send_log(SEVENT_NONE, NULL);

}

void
NetCmdHandler::fill_log_resp_buffer(int eof)
{

  while (log_read_buffer->read_avail() > 0) {

    char *line_end;
    line_end = log_read_buffer->memchr('\n');

    if (line_end == NULL) {
      if (eof == 0) {
        // Incomplete line with more data yet to come
        return;
      } else {
        // We're at end of file so whatever is left is the log
        //  line
        //
        //  Append a \n
        log_read_buffer->fill("\n", 1);
        line_end = log_read_buffer->end();
      }

    }

    resp_buffer->fill(success_prefix, success_prefix_len);

    char *line_start = log_read_buffer->start();
    int line_len = (line_end - line_start) + 1;
    resp_buffer->fill(line_start, line_len);
    log_read_buffer->consume(line_len);
  }
}

void
NetCmdHandler::handle_send_log(s_event_t event, void *data)
{

  ink_debug_assert(event == SEVENT_POLL || event == SEVENT_NONE);

  // Try to send out more data
  if (event == SEVENT_POLL) {
    int r;
    do {
      r = write(this->fd, resp_buffer->start(), resp_buffer->read_avail());
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      if (errno != EAGAIN) {
        PM_Warning("write failed : %s", strerror(errno));
        delete this;
        return;
      }
    } else {
      resp_buffer->consume(r);
    }
  }
  // Fill the open space with new log data
  int client_read_avail = resp_buffer->read_avail();

  if (client_read_avail < 32768) {
    int to_add = 32768 - client_read_avail;
    int write_avail = log_read_buffer->expand_to(to_add);

    int r = read(log_fd, log_read_buffer->end(), write_avail);

    if (r < 0) {
      close(log_fd);
      log_fd = -1;
      fill_log_resp_buffer(1);
      send_raf_resp(raf_cmd, 1, "(done) error: read of log file failed: %s", strerror(errno));
      return;
    } else if (r == 0) {
      close(log_fd);
      log_fd = -1;
      fill_log_resp_buffer(1);
      send_raf_resp(raf_cmd, 0, "(done)");
    } else {
      log_read_buffer->fill(r);
      fill_log_resp_buffer(0);
    }
  }
}


void
NetCmdHandler::process_exit_cmd(RafCmd * cmd)
{
  exit_mode = RAF_EXIT_CONN;
  send_raf_resp(cmd, 0, "exiting...");
}

void
NetCmdHandler::process_shutdown_cmd(RafCmd * cmd)
{
  exit_mode = RAF_EXIT_PROCESS;
  send_raf_resp(cmd, 0, "shutdown...");
}

void
NetCmdHandler::handle_execute_stop(s_event_t event, void *data)
{

  Debug("net_cmd", "handle_execute_stop received event %d", event);

  switch (event) {
  case SEVENT_PROC_STATE_CHANGE:
    {
      ProcRecord *prec = (ProcRecord *) data;
      ink_debug_assert(prec == stop_prec);

      proc_watch = NULL;
      timer_event->cancel();
      timer_event = NULL;

      // We successfully killed the process
      send_raf_resp(raf_cmd, 0, "instance '%s' stopped", (*raf_cmd)[2]);
      break;
    }
  case SEVENT_TIMER:
    {
      // Process didn't die after two seconds so send
      //   SIGKILL
      ink_debug_assert(timer_event == data);
      timer_event = NULL;

      int r = kill(stop_prec->pid, SIGKILL);

      if (r < 0) {
        // Kill cmd failed
        proc_watch->cancel();
        send_raf_resp(raf_cmd, 1, "kill failed for '%s'", (*raf_cmd)[2], strerror(errno));
      } else {
        timer_event = SIO::schedule_in(this, 2000);
      }
      break;
    }
  default:
    ink_release_assert(0);
  }
}

LogHandler::LogHandler():
proc_record(NULL), stream_id(NULL), read_buffer(NULL), FD_Handler()
{
}

LogHandler::~LogHandler()
{

  SIO::remove_fd_handler(this);

  if (read_buffer) {
    delete read_buffer;
    read_buffer = NULL;
  }

  proc_record = NULL;
}

void
LogHandler::start(ProcRecord * pr, int new_fd, const char *sid)
{
  this->fd = new_fd;
  this->proc_record = pr;
  this->stream_id = sid;

  read_buffer = new sio_buffer;

  this->poll_interest = POLL_INTEREST_READ;
  this->my_handler = (SCont_Handler) & LogHandler::handle_log_data;

  SIO::add_fd_handler(this);
}

void
LogHandler::output_log_line(char *start, char *end)
{
  PM_output_log_line(start, end, proc_record->instance_name, stream_id);
}

void
LogHandler::handle_log_data(s_event_t event, void *data)
{

  ink_debug_assert(event == SEVENT_POLL);

  int r;
  int avail = read_buffer->expand_to(1025);
  do {
    r = read(fd, read_buffer->end(), avail - 1);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (errno != EAGAIN) {
      PM_Error("Could not read %s from %s : %s", proc_record->instance_name, stream_id, strerror(errno));
      delete(this);
      return;
    }
  } else if (r == 0) {
    Debug("log_data", "%s closed %s", proc_record->instance_name, stream_id);
    delete this;
  } else {
    read_buffer->fill(r);
    *(read_buffer->end()) = '\0';

    Debug("log_data", "%s(%s): %s", proc_record->instance_name, stream_id, read_buffer->start());

    char *end;
    while ((end = read_buffer->memchr('\n')) != NULL) {
      end += 1;                 // point end to be past the '\n'
      output_log_line(read_buffer->start(), end);
      read_buffer->consume(end - read_buffer->start());
    }
  }
}

ProcPortBinding::ProcPortBinding():
name(NULL), bind_type(PROC_PORT_BIND_INT), link()
{
  value.port = -1;
}

ProcPortBinding::~ProcPortBinding()
{
  if (name) {
    free(name);
    name = NULL;
  }

  if (bind_type == PROC_PORT_BIND_STRING && value.str != NULL) {
    free(value.str);
    value.str = NULL;
  }
}


ProcRecord::ProcRecord():
notify_list(),
pid(-1),
instance_name(NULL),
proc_category(PC_UNKNOWN),
proc_status(PROC_STATUS_CREATED),
install_status(PR_NO_INSTALL),
parent(NULL),
exit_status(0),
destroy_on_proc_exit(0),
run_dir(NULL),
no_run_dir(0),
no_install(0),
config_blob(NULL),
config_file(NULL),
local_path(NULL),
binary_name(NULL),
start_args(NULL),
tmp_start_args(NULL), package_name(NULL), package_dir(NULL), start_cmd(NULL), env_vars(NULL), link(), port_bindings()
{
}

ProcRecord::~ProcRecord()
{

  if (instance_name) {
    ::free(instance_name);
    instance_name = NULL;
  }

  if (run_dir) {
    ::free(run_dir);
    run_dir = NULL;
  }

  if (config_blob) {
    ::free(config_blob);
    config_blob = NULL;
  }

  if (config_file) {
    ::free(config_file);
    config_file = NULL;
  }

  if (local_path) {
    ::free(local_path);
    local_path = NULL;
  }

  if (binary_name) {
    ::free(binary_name);
    binary_name = NULL;
  }

  if (start_args) {
    ::free(start_args);
    start_args = NULL;
  }

  if (tmp_start_args) {
    ::free(tmp_start_args);
    tmp_start_args = NULL;
  }

  if (package_name) {
    ::free(package_name);
    package_name = NULL;
  }

  if (package_dir) {
    ::free(package_dir);
    package_dir = NULL;
  }

  if (start_cmd) {
    ::free(start_cmd);
    start_args = NULL;
  }

  if (env_vars) {
    destroy_argv(env_vars);
    env_vars = NULL;
  }

  parent = NULL;

  ProcPortBinding *pb;
  while (pb = port_bindings.pop()) {
    delete pb;
  }
}

const char *
ProcRecord::init_managed_proc(const char *iname)
{

  proc_category = PC_MANAGED_PROCESS;
  instance_name = strdup(iname);

  const char *bin_dir = NULL;
  if (local_path == NULL && package_name != NULL) {
    // We're using a package to run the process

    // FIX ME - buffer overruns
    char tmp[1024];
    snprintf(tmp, 1023, "%s/install/%s/active", stuff_dir, package_name);
    tmp[1023] = '\0';
    package_dir = strdup(tmp);
    bin_dir = package_dir;
  } else if (local_path != NULL) {
    bin_dir = local_path;
  }

  if (bin_dir) {
    ProcPortBinding *bin_dir_bind = new ProcPortBinding;
    bin_dir_bind->name = strdup("bin_dir");
    bin_dir_bind->bind_type = PROC_PORT_BIND_STRING;
    bin_dir_bind->value.str = strdup(bin_dir);
    port_bindings.push(bin_dir_bind);
  }

  return NULL;
}

rundir_result_t
ProcRecord::init_managed_rundir()
{

  if (no_run_dir == 0) {
    // FIX ME - buffer overruns
    char tmp[1024];

    snprintf(tmp, 1023, "%s/run/%s", stuff_dir, instance_name);
    tmp[1023] = '\0';
    run_dir = strdup(tmp);

    ProcPortBinding *run_dir_bind = new ProcPortBinding;
    run_dir_bind->name = strdup("run_dir");
    run_dir_bind->bind_type = PROC_PORT_BIND_STRING;
    run_dir_bind->value.str = strdup(run_dir);
    port_bindings.push(run_dir_bind);

    // Check to see if run dir already exists
    struct stat stat_info;
    int r;
    do {
      r = stat(run_dir, &stat_info);
    } while (r < 0 && errno == EINTR);

    if (r == 0) {
      return PRR_RM_RUN_DIR;

    } else {
      // No rundir

      int err;
      const char *rmsg = create_or_verify_dir(run_dir, &err);

      if (rmsg != NULL) {
        PM_Error("%s %s for %s : %s", rmsg, run_dir, instance_name, strerror(err));
        return PRR_ERROR;
      } else {
        return PRR_CONTINUE;
      }
    }

  }

  return PRR_CONTINUE;
}

// char* ProcRecord::find_installer()
//
//   caller frees return value
char *
ProcRecord::find_installer()
{

  const char *idir = (local_path) ? local_path : package_dir;

  // Fix me - buffer overrun
  int prefix_len;
  char installer_name_prefix[1024];
  snprintf(installer_name_prefix, 1023, "%s-instantiate", package_name);
  installer_name_prefix[1023] = '\0';
  prefix_len = strlen(installer_name_prefix);

  const char *search_dirs[] = { "", "bin" };
  int num_search_dirs = 2;

  for (int i = 0; i < num_search_dirs; i++) {

    char dir_str[1024];
    snprintf(dir_str, 1023, "%s%s%s", idir,
                 (*(search_dirs[i]) != '\0') ? "/" : "", (*(search_dirs[i]) != '\0') ? search_dirs[i] : "");
    dir_str[1023] = '\0';

    DIR *d = opendir(dir_str);

    if (d == NULL) {
      Debug("install", "[ProcRecord::find_installer] opendir %s for %s failed : %s",
            dir_str, instance_name, strerror(errno));
      continue;
    }

    struct dirent *dp;
    while ((dp = readdir(d)) != NULL) {
      if (strncmp(dp->d_name, installer_name_prefix, prefix_len) == 0) {
        int result_len = strlen(search_dirs[i]) + 1 + strlen(dp->d_name) + 1;
        char *de = (char *) malloc(result_len);
        sprintf(de, "%s%s%s",
                (*(search_dirs[i]) != '\0') ? search_dirs[i] : "", (*(search_dirs[i]) != '\0') ? "/" : "", dp->d_name);
        closedir(d);
        Debug("install", "[ProcRecord::find_installer] found %s for %s", de, instance_name);

        return de;
      }
    }

    closedir(d);
  }
  PM_Error("Could not find installer for %s", instance_name);
  return NULL;
}


S_Action *
ProcRecord::run_installer(S_Continuation * cont, const char *installer_name)
{

  install_status = PR_INSTALL_RUNNING;
  ProcRecord *install_rec = new ProcRecord();

  install_rec->proc_category = PC_INSTALL_PROCESS;
  install_rec->parent = this;

  int len = strlen("install_") + strlen(instance_name) + 1;
  install_rec->instance_name = (char *) malloc(len);
  sprintf(install_rec->instance_name, "install_%s", instance_name);

  ink_debug_assert(this->package_name != NULL || this->local_path != NULL);

  if (local_path) {
    install_rec->local_path = strdup(local_path);
  } else {
    // We're using a package to run the process
    install_rec->package_name = strdup(package_name);
    install_rec->package_dir = strdup(package_dir);
  }

  install_rec->start_cmd = strdup(installer_name);

  add_instance(install_rec);
  const char *r_msg = install_rec->start_process();

  S_Action *rval;
  if (r_msg != NULL) {
    install_status = PR_INSTALL_FAIL;
    EventForwarder *ef = new EventForwarder;
    rval = ef->forward_event(cont, SEVENT_PROC_STATE_CHANGE, this);
  } else {
    rval = set_watch(cont);
  }

  return rval;
}

void
ProcRecord::process_installer_cmd_line(const char *val_start, const char *val_end)
{

  if (start_cmd != NULL) {
    ::free(start_cmd);
  }

  if (start_args != NULL) {
    ::free(start_args);
    start_args = NULL;
  }

  int len = val_end - val_start;
  start_cmd = (char *) malloc(len + 1);
  memcpy(start_cmd, val_start, len);
  start_cmd[len] = '\0';


  // Look for the space the demarks the start of the arguments
  //  from the end of the binary path
  for (int i = 0; i < len; i++) {
    if (isspace(start_cmd[i])) {
      start_cmd[i] = '\0';

      if (i + 1 != len) {
        start_args = strdup((&start_cmd[i]) + 1);
      }
      break;
    }
  }

  Debug("install", "%s: setting start_cmd to '%s'", instance_name, start_cmd);
  Debug("install", "%s: setting start_args to '%s'", instance_name, start_args ? start_args : "<NULL>");
}

void
ProcRecord::process_installer_env_vars(const char *val_start, const char *val_end)
{

  // It isn't a RAF cmd but quoting rules are useful
  RafCmd env_var_set;
  int input_len;
  if (val_end > val_start && *(val_end - 1) == '\n') {
    input_len = (val_end - val_start) - 1;
  } else {
    input_len = val_end - val_start;
  }
  env_var_set.process_cmd((char *) val_start, input_len);

  int num_vars = env_var_set.length();
  if (num_vars == 0) {
    PM_Error("%s: empty env_vars installer output", instance_name);
    return;
  }

  if (env_vars) {
    destroy_argv(env_vars);
  }

  env_vars = (char **) malloc(sizeof(char *) * (num_vars + 1));

  for (int i = 0; i < num_vars; i++) {
    env_vars[i] = strdup(env_var_set[i]);
    Debug("install", "%s: adding env var %s", instance_name, env_vars[i]);

  }

  env_vars[num_vars] = NULL;
}

void
ProcRecord::process_installer_port_binding(const char *val_start, const char *val_end)
{

  const char *tmp = val_start;
  int bindings_added = 0;

  while (tmp < val_end) {
    const char *name_start;
    const char *name_end;
    const char *port_start;
    const char *port_end;

    // Skip past leading whitespace
    while (isspace(*tmp) && tmp < val_end) {
      tmp++;
    }

    name_start = tmp;

    // Search for the end of the name
    while (!isspace(*tmp) && tmp < val_end) {
      tmp++;
    }

    name_end = tmp;

    int name_len = name_end - name_start;

    if (name_len == 0) {
      if (bindings_added == 0) {
        PM_Error("%s: port binding failed - no name", instance_name);
      }
      return;
    }

    // Skip past whitespace
    while (isspace(*tmp) && tmp < val_end) {
      tmp++;
    }

    port_start = tmp;

    // Search for the end of the port
    while (!isspace(*tmp) && tmp < val_end) {
      tmp++;
    }

    port_end = tmp;

    int port_len = port_end - port_start;

    if (port_len == 0) {
      PM_Error("%s: port binding failed - no port", instance_name);
    }

    int port_num = ink_atoi(port_start, port_len);

    if (port_num <= 0) {
      PM_Error("%s: port binding failed - invalid port", instance_name);
    }

    ProcPortBinding *pb = new ProcPortBinding;
    pb->name = (char *) malloc(name_len + 1);
    memcpy(pb->name, name_start, name_len);
    pb->name[name_len] = '\0';
    pb->bind_type = PROC_PORT_BIND_INT;
    pb->value.port = port_num;

    Debug("install", "%s: adding port binding %s:%d", instance_name, pb->name, pb->value.port);

    port_bindings.push(pb);
    bindings_added++;
  }
}

void
ProcRecord::process_installer_ports_used(const char *val_start, const char *val_end)
{

  const char *tmp = val_start;

  // Search for the end of the nuame
  while (!isspace(*tmp) && tmp < val_end) {
    tmp++;
  }

  int used = ink_atoi(val_start, tmp - val_start);

  if (used < 0) {
    PM_Error("%s: ports used reports negative number", instance_name);
  } else if (used == 0 && (tmp == val_start || *val_start != 0)) {
    PM_Error("%s: ports used bad number reported", instance_name);
  } else {
    ports_avail.first += used;
    Debug("install", "%s: %d ports used", instance_name, used);

    if (ports_avail.first > ports_avail.last) {
      PM_Error("%s: too many ports used", instance_name);
      // FIX do something smarter here
    }
  }
}

#define PROC_INST_ERROR(fstr) \
{ \
      int line_len = end - start; \
      char* line = (char*)  malloc(line_len); \
      memcpy(line, start, line_len-1); \
      line[line_len-1] = '\0'; \
      PM_Error(fstr " from installer %s: %s", instance_name, line); \
}



void
ProcRecord::process_installer_output(sio_buffer * buf)
{

  const char *end;
  while ((end = buf->memchr('\n')) != NULL) {
    end += 1;                   // point end to be past the '\n'

    const char *start = buf->start();
    const char *colon = buf->memchr(':');

    if (colon == NULL) {
      PROC_INST_ERROR("Invalid output line");
    } else {
      const char *val_start = colon + 1;

      // Skip leading white space
      while (isspace(*val_start) && val_start < end) {
        val_start++;
      }

      int name_len = colon - start;
      if (name_len == 8 && strncasecmp(start, "cmd_line", 8) == 0) {
        process_installer_cmd_line(val_start, end);
      } else if (name_len == 12 && strncasecmp(start, "port_binding", 12) == 0) {
        process_installer_port_binding(val_start, end);
      } else if (name_len == 10 && strncasecmp(start, "ports_used", 10) == 0) {
        process_installer_ports_used(val_start, end);
      } else if (name_len == 8 && strncasecmp(start, "env_vars", 8) == 0) {
        process_installer_env_vars(val_start, end);
      } else {
        PROC_INST_ERROR("Invalid id tag on line");
      }
    }

    buf->consume(end - buf->start());
  }
}

#undef PROC_INST_ERROR

const char *
ProcRecord::create_pipe(int *pipe_array, const char *id)
{

  int r;
  do {
    r = pipe(pipe_array);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    PM_Error("Pipe (%s) creation failed : %s", id, strerror(errno));
    return "Pipe creation failed";
  }

  return NULL;
}

void
ProcRecord::set_nonblock(int fd, const char *id)
{
  int r;
  do {
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    PM_Error("Failed to non-block on %s pipe", id);
  }
}

const char *
ProcRecord::start_process()
{

  int stdout_pipe[2];
  int stderr_pipe[2];
  int install_in_pipe[2];
  int install_out_pipe[2];

  const char *r_msg;

  ink_debug_assert(proc_status != PROC_STATUS_RUNNING);
  proc_status = PROC_STATUS_RUNNING;

  r_msg = create_pipe(stdout_pipe, "stdout");
  if (r_msg)
    return r_msg;

  r_msg = create_pipe(stderr_pipe, "stderr");
  if (r_msg)
    return r_msg;

  if (proc_category == PC_INSTALL_PROCESS) {

    r_msg = create_pipe(install_in_pipe, "install_in");
    if (r_msg)
      return r_msg;

    r_msg = create_pipe(install_out_pipe, "install_out");
    if (r_msg)
      return r_msg;
  }

  pid_t new_pid;
  new_pid = fork();

  if (new_pid < 0) {
    PM_Error("Fork failed : %s", strerror(errno));
    return "fork failed";
  } else if (new_pid == 0) {
    /* Child Process */

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // FIX - use fd limit
    for (int i = 3; i < 1024; i++) {
      if (i != stdout_pipe[1] && i != stderr_pipe[1] &&
          (proc_category != PC_INSTALL_PROCESS && (i == install_in_pipe[0] || i == install_out_pipe[1]))) {
        close(i);
      }
    }

    int r;

    if (env_vars) {

      char **cur = env_vars;
      while (*cur != NULL) {
        r = putenv(*cur);

        if (cur < 0) {
          PM_Error("Could add env var %s", *cur);
        }
        cur++;
      }
    }

    if (proc_category == PC_INSTALL_PROCESS) {
      close(install_in_pipe[1]);
      close(install_out_pipe[0]);

      do {
        r = dup2(install_in_pipe[0], 0);
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        PM_Fatal("Could not dup to stdin");
      }
      close(install_in_pipe[0]);

      // NOTE: for the installers we don't use stdout to read
      //   information from the installer since subprocesses of
      //   the installer may be forwarding us ouput and error
      //   messages that we don't care about and will just
      //   cause confusion.  Instead we pass the fd number
      //   of the pipe we read the installer output from
      //   on the command line so the installer process
      //   knows what file descriptor to output the data
      //   on

      start_args = (char *) malloc(64);
      sprintf(start_args, "-d %d", install_out_pipe[1]);
    }

    do {
      r = dup2(stdout_pipe[1], 1);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      PM_Fatal("Could not dup to stdout");
    }
    close(stdout_pipe[1]);

    do {
      r = dup2(stderr_pipe[1], 2);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      PM_Fatal("Could not dup to stderr");
    }
    close(stderr_pipe[1]);

    const char *chd;
    if (proc_category == PC_INSTALL_PROCESS || no_run_dir) {
      if (local_path) {
        chd = local_path;
      } else {
        chd = package_dir;
      }
    } else {
      chd = run_dir;
    }

    Debug("child", "Changing to directory: %s", chd);
    do {
      r = chdir(chd);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      PM_Fatal("%s : could not change to run dir %s : %s", instance_name, chd, strerror(errno));
    }

    char **argv;
    if (start_args && tmp_start_args) {
      int len = strlen(start_args) + 1 + strlen(tmp_start_args) + 1;
      char *tmp = (char *) malloc(len);
      sprintf(tmp, "%s %s", start_args, tmp_start_args);
      argv = build_argv(start_cmd, tmp);
      ::free(tmp);
    } else if (tmp_start_args) {
      argv = build_argv(start_cmd, tmp_start_args);
    } else {
      argv = build_argv(start_cmd, start_args);
    }

    Debug("child", "Child execing cmd: %s", start_cmd);
    if (execv(start_cmd, argv) < 0) {
      PM_Fatal("Could not exec in child : %s", strerror(errno));
    }
    return NULL;
  } else {
    /* Parent Process */
    pid = new_pid;

    if (tmp_start_args) {
      ::free(tmp_start_args);
      tmp_start_args = NULL;
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    set_nonblock(stdout_pipe[0], "stdout");
    set_nonblock(stderr_pipe[0], "stderr");

    LogHandler *lh = new LogHandler();
    lh->start(this, stdout_pipe[0], "stdout");

    lh = new LogHandler();
    lh->start(this, stderr_pipe[0], "stderr");

    if (proc_category == PC_INSTALL_PROCESS) {
      close(install_in_pipe[0]);
      close(install_out_pipe[1]);

      set_nonblock(install_in_pipe[1], "install_in");
      set_nonblock(install_out_pipe[0], "install_out");

      InstallerHandler *ih = new InstallerHandler();
      ih->init(this, install_in_pipe[1], install_out_pipe[0]);

    }
    return NULL;
  }
}

S_Action *
ProcRecord::set_watch(S_Continuation * c)
{

  if (pid < 0) {
    // The process is not running - FIX
  }

  S_Action *a = new S_Action;
  a->s_cont = c;

  notify_list.push(a, a->action_link);

  return a;
}

void
ProcRecord::notify_watchers()
{

  S_Action *a = notify_list.head;
  S_Action *next;

  while (a != NULL) {
    next = a->action_link.next;
    a->s_cont->handle_event(SEVENT_PROC_STATE_CHANGE, this);
    notify_list.remove(a, a->action_link);
    delete a;
    a = next;
  }
}

int
ProcRecord::write_config(const char *config)
{

  // FIX ME - buffer overruns
  char tmp[1024];

  snprintf(tmp, 1023, "%s/%s", run_dir, "config_blob");
  tmp[1023] = '\0';
  config_file = strdup(tmp);

  int config_fd;
  do {
    config_fd = open(config_file, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  } while (config_fd < 0 && errno == EINTR);

  if (config_fd < 0) {
    PM_Error("Unable to open config blob file %s : %s", config_file, strerror(errno));
    return 1;
  }

  int len = strlen(config);
  int done = 0;

  while (done < len) {
    int r = write(config_fd, config + done, len - done);

    if (r < 0) {
      if (errno != EINTR && errno != EAGAIN) {
        PM_Error("Unable to write to config blob file %s : %s", config_file, strerror(errno));
        // Error writing the file
        close(config_fd);
        unlink(config_file);
        ::free(config_file);
        config_file = NULL;
        return 1;
      }
    } else {
      done += r;
    }
  }

  close(config_fd);
  return 0;
}

InstallerSendInput::InstallerSendInput():
send_buffer(NULL), master(NULL), FD_Handler()
{
}

InstallerSendInput::~InstallerSendInput()
{

  if (fd >= 0) {
    SIO::remove_fd_handler(this);
  }

  if (send_buffer) {
    delete send_buffer;
    send_buffer = NULL;
  }
}

void
InstallerSendInput::start_send(InstallerHandler * master_arg, ProcRecord * p_rec, int fd_arg)
{
  master = master_arg;
  this->fd = fd_arg;

  ink_debug_assert(send_buffer == NULL);
  send_buffer = new sio_buffer();

  add_pair("bin_dir", (p_rec->local_path) ? p_rec->local_path : p_rec->package_dir);

  if (p_rec->no_run_dir == 0) {
    add_pair("run_dir", p_rec->run_dir);
  } else {
    add_pair("no_run_dir", "1");
  }

  if (p_rec->config_file) {
    add_pair("config_file", p_rec->config_file);
  }

  char port_str[128];
  sprintf(port_str, "%d-%d", ports_avail.first, ports_avail.last);
  add_pair("ports_avail", port_str);

  poll_interest = POLL_INTEREST_WRITE;
  my_handler = (SCont_Handler) & InstallerSendInput::handle_send;

  SIO::add_fd_handler(this);
}

void
InstallerSendInput::handle_send(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(event == SEVENT_POLL);
  ink_debug_assert(this->fd == pfd->fd);

  int todo = send_buffer->read_avail();

  int r;
  do {
    r = write(this->fd, send_buffer->start(), todo);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (r == EAGAIN) {
      // Try again later
      return;
    } else {
      PM_Warning("write to %s failed : %s", master->installer_rec->instance_name, strerror(errno));
      master->set_send_status(-1);
      delete this;
      return;
    }
  } else if (r == 0) {
    PM_Warning("write pipe to %s closed", master->installer_rec->instance_name);
    master->set_send_status(-1);
  } else {
    send_buffer->consume(r);

    if (send_buffer->read_avail() == 0) {

      Debug("install", "Write finished for %s", master->installer_rec->instance_name);
      master->set_send_status(1);
      delete this;
    }
  }
}

void
InstallerSendInput::add_pair(const char *name, const char *value)
{

  send_buffer->fill(name, strlen(name));
  send_buffer->fill(": ", 2);
  send_buffer->fill(value, strlen(value));
  send_buffer->fill("\n", 1);
}

InstallerReadOutput::InstallerReadOutput():
read_buffer(NULL), master(NULL)
{
}

InstallerReadOutput::~InstallerReadOutput()
{
  if (fd >= 0) {
    SIO::remove_fd_handler(this);
  }

  if (read_buffer) {
    delete read_buffer;
    read_buffer = NULL;
  }
}

void
InstallerReadOutput::start_read(InstallerHandler * master_arg, int fd_arg)
{

  master = master_arg;
  this->fd = fd_arg;

  ink_debug_assert(read_buffer == NULL);
  read_buffer = new sio_buffer;

  poll_interest = POLL_INTEREST_READ;
  my_handler = (SCont_Handler) & InstallerReadOutput::handle_read;;

  SIO::add_fd_handler(this);
}

void
InstallerReadOutput::handle_read(s_event_t event, void *data)
{

  struct pollfd *pfd = (struct pollfd *) data;
  ink_debug_assert(event == SEVENT_POLL);
  ink_debug_assert(this->fd == pfd->fd);

  int r;
  int offset = read_buffer->read_avail();
  int avail = read_buffer->expand_to(2048);
  do {
    r = read(this->fd, read_buffer->start() + offset, avail);
  } while (r < 0 && r == EINTR);

  if (r < 0) {
    if (errno == EAGAIN) {
      // Try again later
      return;
    } else {
      PM_Warning("read from %s failed : %s", master->installer_rec->instance_name, strerror(errno));

      master->set_read_status(-1);
      delete this;
      return;
    }
  } else if (r == 0) {
    Debug("install", "read closed for %s", master->installer_rec->instance_name);
    master->set_read_status(1);
    delete this;
  } else {
    read_buffer->fill(r);
  }
}

InstallerHandler::InstallerHandler():
installer_rec(NULL),
error_seen(false),
send_status(0),
read_status(0), timeout_event(NULL), watch_proc(NULL), output_reader(NULL), input_sender(NULL), S_Continuation()
{
}

InstallerHandler::~InstallerHandler()
{

  if (timeout_event) {
    timeout_event->cancel();
    timeout_event = NULL;
  }

  if (watch_proc) {
    watch_proc->cancel();
    watch_proc = NULL;
  }

  installer_rec = NULL;
}

void
InstallerHandler::init(ProcRecord * prec, int input_fd, int output_fd)
{

  installer_rec = prec;
  output_reader = new InstallerReadOutput;
  input_sender = new InstallerSendInput;

  output_reader->start_read(this, output_fd);
  input_sender->start_send(this, prec->parent, input_fd);

  my_handler = (SCont_Handler) & InstallerHandler::watch_installer;
  watch_proc = prec->set_watch(this);
  timeout_event = SIO::schedule_in(this, 90 * 1000);
}

void
InstallerHandler::watch_installer(s_event_t event, void *data)
{

  switch (event) {
  case SEVENT_TIMER:
    ink_debug_assert(data == timeout_event);
    timeout_event = NULL;

    // We've timed out
    PM_Warning("timed out running %s", installer_rec->instance_name);
    handle_install_error();

    break;
  case SEVENT_PROC_STATE_CHANGE:
    ink_debug_assert(data == installer_rec);
    watch_proc = NULL;

    // The installer has finished
    if (installer_rec->exit_status != 0) {
      PM_Warning("non-zero exit status from %s", installer_rec->instance_name);
      handle_install_error();
    } else if (send_status == 0) {
      // The installer did not read everything - can't be good
      PM_Warning("send did not complete for %s", installer_rec->instance_name);
      handle_install_error();
    } else if (read_status < 0 || send_status < 0) {
      PM_Warning(" %s communication with %s failed",
                 (read_status < 0) ? "read" : "write", installer_rec->instance_name);
    } else if (read_status == 0) {
      // We're waiting for the read to complete.  Do nothing
    } else {
      handle_install_success();
    }
    break;
  default:
    ink_release_assert(0);
  }
}

void
InstallerHandler::handle_install_error()
{

  error_seen = true;

  if (timeout_event) {
    timeout_event->cancel();
    timeout_event = NULL;
  }

  if (watch_proc) {
    // FIX - the damn installer hasn't exited yet.  We should
    //   really try to kill it properly :-(
    kill(installer_rec->pid, SIGTERM);

    watch_proc->cancel();
    watch_proc = NULL;
  }

  installer_rec->parent->install_status = PR_INSTALL_FAIL;
  installer_rec->parent->notify_watchers();

  if (read_status == 0) {
    // Still waiting for the read from the installer to
    //   complete.  Don't go nuking stuff yet!
    return;
  }

  remove_instance(installer_rec);
  installer_rec = NULL;

  delete this;
}

void
InstallerHandler::handle_install_success()
{

  if (timeout_event) {
    timeout_event->cancel();
    timeout_event = NULL;
  }

  installer_rec->parent->install_status = PR_INSTALL_SUCCESS;
  installer_rec->parent->notify_watchers();

  ink_debug_assert(installer_rec->pid == -1);

  remove_instance(installer_rec);
  installer_rec = NULL;

  delete this;
}

void
InstallerHandler::set_read_status(int status)
{
  read_status = status;

  if (is_debug_tag_set("install")) {
    output_reader->read_buffer->expand_to(1);
    *(output_reader->read_buffer->end()) = '\0';
    Debug("install", "%s params (%d): %s",
          installer_rec->instance_name, output_reader->read_buffer->read_avail(), output_reader->read_buffer->start());
  }

  if (read_status == 1) {
    installer_rec->parent->process_installer_output(output_reader->read_buffer);
  }
  // FIX - processs parameters

  if (watch_proc == NULL) {

    // Process has already exited
    if (read_status == 1 && error_seen == false) {
      handle_install_success();
    } else {
      handle_install_error();
    }
  }
}

void
InstallerHandler::set_send_status(int status)
{
  send_status = status;
}

EventForwarder::EventForwarder():
cont(NULL), event(SEVENT_NONE), data(NULL), S_Continuation()
{
  my_handler = (SCont_Handler) & EventForwarder::handle_event;
}

EventForwarder::~EventForwarder()
{
}

S_Action *
EventForwarder::forward_event(S_Continuation * c, s_event_t e, void *d)
{
  cont = c;
  event = e;
  data = d;

  return SIO::schedule_in(this, 1);
}

void
EventForwarder::handle_event(s_event_t e, void *d)
{
  ink_debug_assert(e == SEVENT_TIMER);

  cont->handle_event(e, d);
}

RecursiveRmDir::RecursiveRmDir():
S_Continuation(), dir(NULL), rm_proc(NULL), timer_event(NULL), action()
{
}

RecursiveRmDir::~RecursiveRmDir()
{

  if (dir) {
    free(dir);
    dir = NULL;
  }

  if (timer_event) {
    timer_event->cancel();
    timer_event = NULL;
  }

  if (rm_proc_action) {
    rm_proc_action->cancel();
    rm_proc_action = NULL;
  }
}


void
RecursiveRmDir::handle_rm_complete(s_event_t e, void *d)
{

  ProcRecord *pr = (ProcRecord *) d;
  s_event_t call_e = SEVENT_NONE;
  switch (e) {
  case SEVENT_PROC_STATE_CHANGE:
    ink_debug_assert(pr == rm_proc);
    if (pr->exit_status != 0) {
      call_e = SEVENT_RMDIR_FAILURE;
      PM_Warning("rmdir failed on %s", dir);
    } else {
      Debug("rm", "rmdir succeeded on %s", dir);
      call_e = SEVENT_RMDIR_SUCCESS;
    }
    rm_proc = NULL;
    rm_proc_action = NULL;

    if (timer_event) {
      timer_event->cancel();
      timer_event = NULL;
    }
    break;
  case SEVENT_TIMER:
    // A timer event means we had an error but could not
    //  send it due to the prohibition on reentrant callbacks
    ink_debug_assert(d == timer_event);
    timer_event = NULL;

    rm_proc_action->cancel();
    rm_proc_action = NULL;
    rm_proc = NULL;

    call_e = SEVENT_RMDIR_FAILURE;
    break;
  default:
    ink_release_assert(0);
  }

  if (action.cancelled == 0) {
    action.s_cont->handle_event(call_e, this);
  }

  delete this;
}

S_Action *
RecursiveRmDir::do_remove_dir(S_Continuation * cont, const char *dir_to_rm, const char *tag)
{

  // Set handler at top.  If we run into an error condition, we'll send
  //   an event to ourselves since reentrant callbacks are not permitted
  //   in the SIO model
  my_handler = (SCont_Handler) & RecursiveRmDir::handle_rm_complete;
  action.s_cont = cont;

  // We can't very well rm a directory when we don't have a name or
  //   know where the 'rm' binary is
  if (dir_to_rm == NULL || dir_to_rm[0] == '\0' || rm_bin_path == NULL || rm_bin_path == '\0') {
    SIO::schedule_in(this, 0);
    return &action;
  }

  struct stat stat_info;
  int r;
  do {
    r = stat(dir_to_rm, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    PM_Error("Unable to find old instance dir %s : %s", dir_to_rm, strerror(errno));
    SIO::schedule_in(this, 0);
    return &action;;
  }

  if (stat_info.st_mode & S_IFDIR == 0) {
    PM_Error("Unable to find old instance dir %s is not a directory", dir_to_rm);
    SIO::schedule_in(this, 0);
    return &action;;
  }

  this->dir = strdup(dir_to_rm);
  ProcRecord *rm = new ProcRecord;
  this->rm_proc = rm;

  rm->destroy_on_proc_exit = 1;
  rm->package_dir = strdup(stuff_install_dir);
  rm->run_dir = strdup(stuff_run_dir);

  int len = 3 + strlen(tag) + 1;
  rm->instance_name = (char *) malloc(len);
  sprintf(rm->instance_name, "rm_%s", tag);

  rm->start_cmd = strdup(rm_bin_path);

  const char rm_args_fmt[] = "-rf %s";

  len = sizeof(rm_args_fmt) + strlen(dir);
  rm->start_args = (char *) malloc(len);
  sprintf(rm->start_args, rm_args_fmt, dir);

  Debug("rm", "args to rm \"%s\" for %s", rm->start_args, rm->instance_name);

  timer_event = SIO::schedule_in(this, 60 * 1000);
  rm_proc_action = rm->set_watch(this);

  add_instance(rm);
  rm->start_process();

  return &action;
}

char *
  RecursiveRmDir::rm_bin_path = NULL;

void
find_rm_bin()
{
  const char *
    check_locations[] = {
    "/bin/rm",
    "/usr/bin/rm",
    "/usr/local/bin/rm",
    NULL
  };

  const char **
    tmp = check_locations;

  while (*tmp != NULL) {
    int
      r;
    do {
      r = access(*tmp, X_OK);
    } while (r < 0 && errno == EINTR);

    if (r == 0) {
      RecursiveRmDir::rm_bin_path = strdup(*tmp);
      Debug("rm", "Set rm_bin_path to %s", RecursiveRmDir::rm_bin_path);
      return;
    }

    tmp++;
  }

  PM_Warning("Unable to find rm binary");
}


static int
  sigchld_received = 0;
static int
  exit_signal_received = 0;

void
process_child_exit(pid_t pid, int status)
{

  bool
    proc_found = false;
  ProcRecord *
    cur = process_list.head;

  while (cur != NULL) {

    if (cur->pid == pid) {
      proc_found = true;

      if (WIFEXITED(status)) {
        int
          exit_status = WEXITSTATUS(status);
        cur->exit_status = exit_status;
        if (exit_status == 0) {
          cur->proc_status = PROC_STATUS_STOPPED;
          PM_Note("Child process pid %d terminated normally", pid);
        } else {
          if (cur->proc_status != PROC_STATUS_STOPPING) {
            PM_Warning("Child process %s (pid %d) terminated abnormally with status %d",
                       cur->instance_name, pid, exit_status);
            cur->proc_status = PROC_STATUS_FAIL;
          } else {
            cur->proc_status = PROC_STATUS_STOPPED;
          }
        }
      } else if (WIFSIGNALED(status)) {
        if (cur->proc_status != PROC_STATUS_STOPPING) {
          PM_Warning("Child process %s (pid %d) killed by signal %d", cur->instance_name, pid, WTERMSIG(status));
          cur->proc_status = PROC_STATUS_FAIL;
        } else {
          cur->proc_status = PROC_STATUS_STOPPED;
        }
        cur->exit_status = -WTERMSIG(status);
      } else if (WIFSTOPPED(status)) {
        PM_Warning("Child process pid %d reported stopped", pid);
      } else {
        PM_Fatal("Unknown exit reason %d for pid %d", status, pid);
      }

      cur->pid = -1;
      cur->notify_watchers();

      if (cur->destroy_on_proc_exit) {
        remove_instance(cur);
      }
      break;
    }

    cur = cur->link.next;
  }

  if (!proc_found) {
    PM_Error("Unable to find instance record for exited child pid %d", pid);
  }
}

void
process_sigchld()
{

  int
    chld_status = 0;

  pid_t
    r;
  do {
    do {
      r = waitpid(-1, &chld_status, WNOHANG);
      Debug("process", "waitpid returned %d (%d)", r, chld_status);
    } while (r < 0 && errno == EINTR);

    if (r > 0) {
      process_child_exit(r, chld_status);
    }

  } while (r > 0);
}

void
main_loop()
{

  while (1) {

    if (sigchld_received) {
      sigchld_received = 0;
      process_sigchld();
    }

    if (exit_signal_received) {
      Note("received signal %d, exiting...", exit_signal_received);
      SIO::do_exit(1);
    }

    SIO::run_loop_once();
  }
}

Diags *
  diags = NULL;

void
init_log_stuff()
{
  log_sender = new LogSender();

  if (*log_collator == '\0') {
    pid_t
      mypid = getpid();
    snprintf(log_file, 1023, "%s/log.%d", stuff_log_dir, mypid);
    log_file[1023] = '\0';
    log_sender->start_to_file(log_file);
  } else {
    int
      port = 12301;
    char *
      colon = strchr(log_collator, ':');
    if (colon) {
      *colon = '\0';
      port = atoi(colon + 1);
      if (port == 0) {
        PM_Fatal("Bad port to -L <log_collator> : '%s'", colon + 1);
      }
    }

    struct hostent *
      he;
    he = gethostbyname(log_collator);

    if (he == NULL) {
      PM_Fatal("failed to resolve log_collator : %s", log_collator);
    }

    struct in_addr
      in;
    memcpy(&in.s_addr, *he->h_addr_list, sizeof(in.s_addr));

    log_sender->start_to_net(in.s_addr, port);
  }
}

void
init_dir_stuff()
{

  if (strlen(stuff_dir) == 0) {
    PM_Fatal("--stuff-dir is an empty string");
  }
  // We need an absolute path
  if (stuff_dir[0] != '\0') {
    if (stuff_dir[0] != '/') {
      // FIX - buffer overrun :-(
      char
        new_stuff_dir[1024];
      char *
        r = getcwd(new_stuff_dir, 1024);
      if (r == NULL) {
        PM_Fatal("getcwd failed: %s", strerror(errno));
      }
      int
        len = strlen(new_stuff_dir);

      if (strcmp(stuff_dir, ".") != 0) {
        new_stuff_dir[len] = '/';
        strcpy(new_stuff_dir + len + 1, stuff_dir);
      }
      strcpy(stuff_dir, new_stuff_dir);
    }
  }
  Debug("stuff_dir", "is %s", stuff_dir);

  int
    error_code;
  const char *
    rmsg = create_or_verify_dir(stuff_dir, &error_code);
  if (rmsg) {
    PM_Fatal("no stuff dir %s : %s : %s", rmsg, stuff_dir, strerror(error_code));
  }

  snprintf(stuff_install_dir, 1023, "%s/%s", stuff_dir, "install");
  stuff_install_dir[1023] = '\0';
  create_or_verify_dir(stuff_install_dir, &error_code);
  if (rmsg) {
    PM_Fatal("%s : %s : %s", rmsg, stuff_install_dir, strerror(error_code));
  }

  snprintf(stuff_run_dir, 1023, "%s/%s", stuff_dir, "run");
  stuff_run_dir[1023] = '\0';
  create_or_verify_dir(stuff_run_dir, &error_code);
  if (rmsg) {
    PM_Fatal("%s : %s : %s", rmsg, stuff_install_dir, strerror(error_code));
  }

  snprintf(stuff_log_dir, 1023, "%s/%s", stuff_dir, "log");
  stuff_log_dir[1023] = '\0';
  create_or_verify_dir(stuff_log_dir, &error_code);
  if (rmsg) {
    PM_Fatal("%s : %s : %s", rmsg, stuff_install_dir, strerror(error_code));
  }
}

void
manage_lockfile()
{
  char
    tmp[1024];
  snprintf(tmp, 1023, "%s/%s", stuff_run_dir, "proc_manager.lock");
  tmp[1023] = '\0';

  pid_t
    holding_pid = 0;
  lockfile = new Lockfile(tmp);
  int
    r = lockfile->Get(&holding_pid);

  if (r < 0) {
    Fatal("Error accessing lock file : %s", strerror(-r));
  } else if (r == 0) {
    Fatal("proc_manager lockfile held by pid %d", holding_pid);
  }
}


extern
  "C"
{
  void
  sigchld_handler(int sig)
  {

    ink_debug_assert(sig == SIGCHLD);

    sigchld_received = 1;
  }

  void
  exit_signal_handler(int sig)
  {
    exit_signal_received = sig;
  }
}

void
setup_signals()
{

  struct sigaction
    sig_h;
  memset(&sig_h, 0, sizeof(sig_h));
  sig_h.sa_handler = sigchld_handler;
  sigemptyset(&sig_h.sa_mask);
  sig_h.sa_flags = SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sig_h, NULL);

  // Ignore signals we don't care about like pipe or hup
  int
  ignore_sigs[5] = { SIGPIPE, SIGHUP, SIGUSR1, SIGUSR2, SIGALRM };
  sig_h.sa_handler = SIG_IGN;
  sig_h.sa_flags = 0;

  int
    i;
  for (i = 0; i < 5; i++) {
    sigaction(ignore_sigs[i], &sig_h, NULL);
  }

  // Trap exit signals so we can terminate children
  int
  exit_sigs[3] = { SIGINT, SIGQUIT, SIGTERM };
  sig_h.sa_handler = exit_signal_handler;
  sig_h.sa_flags = 0;

  for (i = 0; i < 3; i++) {
    sigaction(exit_sigs[i], &sig_h, NULL);
  }
}

void
setup_proc_group()
{
  pid_t
    our_proc_group = setsid();

  if (our_proc_group < 0) {
    PM_Fatal("setsid() failed : %s", strerror(errno));
  }
}

void
finish_startup()
{

  SIO::add_exit_handler(new ExitHandler);

//    setup_proc_group();
  init_log_stuff();
  find_rm_bin();

  ports_avail.first = control_port + 1;
  ports_avail.last = ports_avail.first + 48;

  memcpy(&orig_ports_avail, &ports_avail, sizeof(PortsAvail));

  // Start the control port
  accept_handler = new AcceptHandler();
  accept_handler->start(control_port);

  main_loop();
}

static void
close_stdin()
{

  int
    fd;
  do {
    fd = open("/dev/null", O_RDONLY);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    Fatal("could not open /dev/null : %s", strerror(errno));
  }

  int
    r;
  do {
    r = dup2(fd, 0);
  } while (r < 0 && errno == EINTR);

  close(fd);
}

static void
redirect_stdout_stderr()
{

  char
    tmp[1024];
  snprintf(tmp, 1023, "%s/%s", stuff_log_dir, "proc_manager.out");
  tmp[1023] = '\0';

  int
    out_fd;
  do {
    out_fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  } while (out_fd < 0 && errno == EINTR);

  if (out_fd < 0) {
    Fatal("unable to open %s for stdout redirection : %s", tmp, strerror(errno));
  }

  int
    r;
  do {
    r = dup2(out_fd, 1);
  } while (r < 0 && errno == EINTR);

  do {
    dup2(out_fd, 2);
  } while (r < 0 && errno == EINTR);
  close(out_fd);
}

// We've been started by test_exec.
//
//    redirect stdout/stderr to a file
//
//    fork ourselves
//
void
remote_startup()
{

  pid_t
    new_pid = fork();

  if (new_pid < 0) {
    Fatal("fork failed : %s", strerror(errno));
    exit(1);
  } else if (new_pid == 0) {
    /* Child */
    close_stdin();
    redirect_stdout_stderr();

    // We need to reget the lockfile with our new pid
    //   FIX - there is a race here
    pid_t
      h_pid;
    lockfile->Close();
    lockfile->Get(&h_pid);

    finish_startup();
  } else {
    /* Parent */
    write(1, "liftoff\n", 9);
    exit(0);
  }
}

int
main(int argc, char **argv)
{

  setup_signals();
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

  if (quiet_mode) {
    for (int i = 0; i < DiagsLevel_Count; i++) {
      diags->config.outputs[i].to_stdout = false;
      diags->config.outputs[i].to_stderr = false;
      diags->config.outputs[i].to_syslog = false;
    }
  }

  init_dir_stuff();
  manage_lockfile();

  if (remote_start) {
    remote_startup();
  } else {
    finish_startup();
  }

  return 0;
}
