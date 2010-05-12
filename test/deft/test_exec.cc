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

// Avoiding getting the whole libinktomi++ header file since
//  it causes problems with Sunpro 5.0
#define	_inktomiplus_h_

#include <netdb.h>

#include "ink_args.h"
#include "snprintf.h"
#include "ink_hrtime.h"
#include "Diags.h"
#include "Tokenizer.h"
#include "InkTime.h"

#include "sio_buffer.h"
#include "sio_loop.h"
#include "raf_cmd.h"
#include "rafencode.h"
#include "remote_start.h"
#include "test_exec.h"
#include "test_utils.h"
#include "test_group.h"
#include "test_results.h"

#include "test_interp_glue.h"

/* Constants */
static const int SIZE_32K = 32768;

/* Globals */
UserDirInfo *ud_info;
static TestRunResults *run_results;
pid_t log_collator_pid = -1;
pid_t log_viewer_pid = -1;
int log_viewer_pipe_fd = -1;
int log_collator_port = -1;
int log_collator_fd = -1;
int log_file_fd = -1;
int kill_sig_received = -1;
int kill_in_progress = 0;

static const char rcs_id[] = "2.0";
Diags *diags = NULL;
char cur_script_path[PATH_MAX + 1];

/* Argument Stuff */
int control_port = 12300;
int cmd_timeout = 60;
int manual_startup = 0;
int launch_log_viewer = 0;
int show_version = 0;
int post_to_tinderbox = 0;
int save_results = 0;
int kill_wait = 2;
static char error_tags[1024];
static char action_tags[1024];
static char stuff_path[1024] = "/inktest";
static char package_dir[1024] = "packages";
static char defs_file[1024] = "localhost.def";
static char defs_dir[1024] = "defs";
static char defs_add[1024] = "";
static char test_script[1024] = "jtest.pl";
static char lib_dir[1024] = "scripts/perl_lib";
static char script_dir[1024] = "scripts";
static char log_file[1024] = "test.log";
static char log_parser_dir[1024] = "parsers";
static char log_parser_bin[1024] = "parse_test_log.pl";
static char perl_args[1024] = "";
static char script_args[1024] = "";
static char test_uniquer[128] = "-0";
static char test_group[256] = "";
static char test_group_file[512] = "test_groups.deft";
char tinderbox_machine[256] = "spork.example.com";
char tinderbox_tree[256] = "x_test";
char save_results_dir[512] = "";
char save_results_url[512] = "";
char build_id[512] = "";

ArgumentDescription argument_descriptions[] = {
  {"port", 'p', "Control Port", "I", &control_port, NULL, NULL},
  {"stuff_path", 'd', "Stuff Path", "S1023", stuff_path, NULL, NULL},
  {"test_uniquer", 'u', "Test Uniquer", "S127", test_uniquer, NULL, NULL},
  {"pkg_dir", 'P', "Package Directory", "S1023", package_dir, NULL, NULL},
  {"lib_dir", 'l', "Perl Libraries", "S1023", lib_dir, NULL, NULL},
  {"script_dir", 'S', "Test Script Dir", "S1023", script_dir, NULL, NULL},
  {"defines_file", 'D', "Defines File", "S1023", defs_file, NULL, NULL},
  {"defines_dir", 'W', "Defines Dir", "S1023", defs_dir, NULL, NULL},
  {"defines_add", 'w', "Defines Dir", "S1023", defs_add, NULL, NULL},
  {"script", 's', "Test Script", "S1023", test_script, NULL, NULL},
  {"script_args", 'a', "Script Args", "S1023", script_args, NULL, NULL},
  {"perl_args", 'A', "Perl Args", "S1023", perl_args, NULL, NULL},
  {"manual_start", 'm', "Manual component startup", "F", &manual_startup, NULL, NULL},
  {"kill_wait", 'k', "Time to wait for a kill to finish", "I", &kill_wait, NULL, NULL},
  {"log_file", 'L', "Log File", "S1023", log_file, NULL, NULL},
  {"log_parser_bin", 'y', "Log Parser Bin", "S1023", log_parser_bin, NULL, NULL},
  {"log_parser_dir", 'Y', "Log Parser Dir", "S1023", log_parser_dir, NULL, NULL},
  {"test_group", 'g', "Test Group To Run", "S255", test_group, NULL, NULL},
  {"test_group_file", 'G', "Test Group File", "S511", test_group_file, NULL, NULL},
  {"cmd_timeout", 'z', "Raf Command Timeout", "I", &cmd_timeout, NULL, NULL},
  {"launch_viewer", 'v', "Launch Log Viewer", "F", &launch_log_viewer, NULL, NULL},
  {"tinderbox", 't', "Post Results to Tinderbox", "F", &post_to_tinderbox, NULL, NULL},
  {"tinderbox_machine", 'X', "Tinderbox Machine", "S255", &tinderbox_machine, NULL, NULL},
  {"tinderbox_tree", 'x', "Tinderbox Tree", "S255", &tinderbox_tree, NULL, NULL},
  {"save_results", 'Q', "Save Results", "F", &save_results, NULL, NULL},
  {"save_dir", 'q', "Save Results Dir", "S511", &save_results_dir, NULL, NULL},
  {"save_url", 'U', "Save Results URL", "S511", &save_results_url, NULL, NULL},
  {"build_id", 'b', "Build Id", "S511", &build_id, NULL, NULL},
  {"version", 'V', "Show Version", "F", &show_version, NULL, NULL},
  {"debug_tags", 'T', "Debug Tags", "S1023", error_tags, NULL, NULL},
  {"action_tags", 'B', "Behavior Tags", "S1023", action_tags, NULL, NULL},
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage}
};
int n_argument_descriptions = SIZE(argument_descriptions);

static DLL<HostRecord> host_list;
static DLL<InstanceRecord> instance_list;
static InkHashTable *substitution_hash = NULL;


void
TE_output_log_line(const char *start, const char *end, const char *iname, const char *stream_id)
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

  sio_buffer output_buffer;
  output_buffer.fill(prefix_buffer, r);
  output_buffer.fill(start, end - start);

  if (end > start && *(end - 1) != '\n') {
    const char new_line_buf[] = "\n";
    output_buffer.fill(new_line_buf, 1);
  }

  int *fd = NULL;
  const char *id;
  if (log_collator_fd >= 0) {
    fd = &log_collator_fd;
    id = "collator";

  } else if (log_file_fd >= 0) {
    fd = &log_file_fd;
    id = "file";
  }

  if (fd) {
    int timeout_ms = 5000;
    const char *rmsg = write_buffer(*fd, &output_buffer, &timeout_ms);
    if (rmsg) {
      Warning("write to log %s failed : %s", id, rmsg);
      close(*fd);
      *fd = -1;
    }
  }
}

void
TE_log_line_va(const char *level, const char *format_str, va_list ap)
{

  char line_buf[2048];
  int r = vsnprintf(line_buf, 2047, format_str, ap);
  if (r >= 2047) {
    line_buf[2047] = '\0';
    r = 2047;
  }

  TE_output_log_line(line_buf, line_buf + r, "test_exec", level);
}

void
TE_Status(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Status, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  TE_log_line_va("Status", format_str, ap);
  va_end(ap);

}

void
TE_Note(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Note, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  TE_log_line_va("Note", format_str, ap);
  va_end(ap);

}

void
TE_Warning(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Warning, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  TE_log_line_va("Warning", format_str, ap);
  va_end(ap);

}

void
TE_Error(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Error, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  TE_log_line_va("Error", format_str, ap);
  va_end(ap);

}

void
TE_Fatal(const char *format_str, ...)
{

  va_list ap;

  va_start(ap, format_str);
  diags->print_va(NULL, DL_Fatal, NULL, NULL, format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  TE_log_line_va("Fatal", format_str, ap);
  va_end(ap);

  va_start(ap, format_str);
  ink_fatal_va(1, (char *) format_str, ap);
  va_end(ap);
}


InstanceRecord::InstanceRecord(const char *name):
host_rec(NULL),
link()
{
  instance_name = strdup(name);
  port_bindings = ink_hash_table_create(InkHashTableKeyType_String);
}

InstanceRecord::~InstanceRecord()
{
  free(instance_name);
  ink_hash_table_destroy_and_free_values(port_bindings);
}

void
InstanceRecord::add_port_binding(const char *name, const char *value)
{


  if (ink_hash_table_isbound(port_bindings, (char *) name)) {
    TE_Warning("replacing port binding %s for %s", name, instance_name);
    InkHashTableValue val;
    ink_hash_table_lookup(port_bindings, (char *) name, &val);
    free((char *) val);
    ink_hash_table_delete(port_bindings, (char *) name);
  }

  ink_hash_table_insert(port_bindings, (char *) name, strdup(value));
  Debug("ports", "%s: Adding port binding %s => %s", instance_name, name, value);
}

const char *
InstanceRecord::get_port_binding(const char *name)
{

  InkHashTableValue val;
  int r = ink_hash_table_lookup(port_bindings, (char *) name, &val);

  if (r == 0) {
    return NULL;
  } else {
    return (const char *) val;
  }
}


InstanceRecord *
find_instance_rec(const char *name)
{
  InstanceRecord *tmp = instance_list.head;

  while (tmp) {
    if (strcasecmp(tmp->instance_name, name) == 0) {
      return tmp;
    }
    tmp = tmp->link.next;
  }

  return NULL;
}


HostRecord::HostRecord(const char *name):
arch(NULL), ip(0), port(control_port), fd(-1), next_raf_id(0), link()
{
  hostname = strdup(name);
  read_buffer = new sio_buffer;
  package_table = ink_hash_table_create(InkHashTableKeyType_String);
}

HostRecord::~HostRecord()
{
  free(hostname);

  if (arch != NULL) {
    free(arch);
    arch = NULL;
  }

  delete read_buffer;

  if (fd >= 0) {
    close(fd);
    fd = -1;
  }

  ink_hash_table_destroy_and_free_values(package_table);
}

int
HostRecord::start()
{

  struct hostent *he;
  he = gethostbyname(hostname);

  if (he == NULL) {
    TE_Error("[HostRecord::start] failed to resolve %s", hostname);
    return 1;
  }

  struct in_addr in;
  memcpy(&in.s_addr, *he->h_addr_list, sizeof(in.s_addr));
  this->ip = in.s_addr;

  int r;
  if (!manual_startup) {
    TE_Status("Starting proc_manager on %s", hostname);
    r = remote_start(hostname, this->ip, ud_info, kill_wait);

    if (r < 0) {
      return 1;
    }
  }

  fd = SIO::make_client(in.s_addr, port);

  if (fd < 0) {
    return 1;
  }

  RafCmd req;
  RafCmd resp;

  req(0) = get_id_str();
  req(1) = strdup("arch");

  r = do_raf(this, &req, &resp);

  if (r < 0) {
    TE_Error("[HostRecord::start] raf attempt failed - determine arch of %s", hostname);
    return 1;
  }

  if (resp.length() < 3 || atoi(resp[1]) != 0) {
    TE_Error("[HostRecord::start] raf cmd failed - determine arch of %s", hostname);
    return 1;
  }

  ink_debug_assert(arch == NULL);
  arch = strdup(resp[2]);

  Debug("host", "%s is arch %s", hostname, arch);

  req.clear();
  resp.clear();

  req(0) = get_id_str();
  req(1) = strdup("show_pkgs");

  r = do_raf(this, &req, &resp);

  if (r < 0) {
    TE_Error("[HostRecord::start] raf attempt failed - determine packages on %s", hostname);
    return 1;
  }

  if (resp.length() < 2 || atoi(resp[1]) != 0) {
    TE_Error("[HostRecord::start] raf cmd failed - determine packages on %s", hostname);
    return 1;
  }

  int i;
  int resp_len = resp.length();
  for (i = 2; i + 1 < resp_len; i += 2) {
    Debug("host", "%s: adding package %s %s", this->hostname, resp[i], resp[i + 1]);
    ink_hash_table_insert(package_table, resp[i], strdup(resp[i + 1]));
  }

  return 0;
}

const char *
HostRecord::lookup_package(const char *pkg_name)
{
  InkHashTableValue val = NULL;
  int r = ink_hash_table_lookup(package_table, (char *) pkg_name, &val);

  if (r == 0) {
    return NULL;
  } else {
    return (const char *) val;
  }
}

void
HostRecord::update_package_entry(const char *pkg_name, const char *new_pkg, int new_pkg_len)
{
  InkHashTableValue val = NULL;
  int r = ink_hash_table_lookup(package_table, (char *) pkg_name, &val);

  if (r != 0) {
    free((char *) val);
    ink_hash_table_delete(package_table, (char *) pkg_name);
  }

  char *to_insert = (char *) malloc(new_pkg_len + 1);
  memcpy(to_insert, new_pkg, new_pkg_len);
  to_insert[new_pkg_len] = '\0';

  ink_hash_table_insert(package_table, (char *) pkg_name, to_insert);
}

// caller frees result
char *
HostRecord::get_id_str()
{

  char num_buf[32];
  sprintf(num_buf, "%d", this->next_raf_id);
  this->next_raf_id++;

  return strdup(num_buf);
}


HostRecord *
create_host_rec(const char *hostname)
{
  HostRecord *new_rec = new HostRecord(hostname);

  int r = new_rec->start();

  if (r != 0) {
    delete new_rec;
    return NULL;
  } else {
    host_list.push(new_rec);
    return new_rec;
  }
}


HostRecord *
find_host_rec(const char *hostname)
{
  HostRecord *tmp = host_list.head;

  while (tmp != NULL) {
    if (strcasecmp(hostname, tmp->hostname) == 0) {
      return tmp;
    }
    tmp = tmp->link.next;
  }

  return NULL;
}

UserDirInfo::UserDirInfo():
username(NULL),
shell(NULL),
hostname(NULL),
test_stuff_path(NULL),
test_stuff_dir(NULL),
test_stuff_path_and_dir(NULL),
log_dir(NULL), log_file(NULL), tmp_dir(NULL), log_collator_arg(NULL), package_dir(NULL), port(-1)
{
}

UserDirInfo::~UserDirInfo()
{

  if (username) {
    free(username);
    username = NULL;
  }

  if (shell) {
    free(shell);
    shell = NULL;
  }

  if (hostname) {
    free(hostname);
    hostname = NULL;
  }

  if (ip_str) {
    free(ip_str);
    ip_str = NULL;
  }

  if (test_stuff_path) {
    free(test_stuff_path);
    test_stuff_path = NULL;
  }

  if (test_stuff_dir) {
    free(test_stuff_dir);
    test_stuff_dir = NULL;
  }

  if (test_stuff_path_and_dir) {
    free(test_stuff_path_and_dir);
    test_stuff_path_and_dir = NULL;
  }

  if (tmp_dir) {
    free(tmp_dir);
    tmp_dir = NULL;
  }

  if (log_collator_arg) {
    free(log_collator_arg);
    log_collator_arg = NULL;
  }

  if (package_dir) {
    free(package_dir);
    package_dir = NULL;
  }
}

// char* find_local_package(const char* pkg_name, const char* arch)
//
//   caller frees return value if not NULL
//
char *
find_local_package(const char *pkg_name, const char *arch)
{

  char *rvalue = NULL;
  DIR *d = opendir(package_dir);

  if (d == NULL) {
    TE_Error("failed open local package directory: %s", strerror(errno));
    return NULL;
  }

  bool arch_is_sun_sparc;
  if (strcmp(arch, "SunOS") == 0) {
    arch_is_sun_sparc = true;
  } else {
    arch_is_sun_sparc = false;
  }

  int prefix_len = strlen(pkg_name) + 1 + strlen(arch);
  char *pkg_prefix = (char *) malloc(prefix_len + 1);
  sprintf(pkg_prefix, "%s-%s", pkg_name, arch);

  struct dirent *dp;
  while ((dp = readdir(d)) != NULL) {
    Debug("find_package", "looking at %s", dp->d_name);
    if (strncmp(dp->d_name, pkg_prefix, prefix_len) == 0) {
      // Since SunOS is a prefix to SunOSx86 we need to make
      //   sure we dont' accidently return a x86 package when
      //   we need sparc one
      if (arch_is_sun_sparc == false ||
          (!(dp->d_name[prefix_len] == 'x' &&
             (dp->d_name[prefix_len + 1] == '8' && (dp->d_name[prefix_len + 2] == '6'))))) {
        rvalue = strdup(dp->d_name);
        break;
      }
    }
  }

  free(pkg_prefix);
  closedir(d);

  return rvalue;

}

int
send_file(int to_fd, int from_fd, int bytes, const char *filename, const char *to_id)
{

  sio_buffer input_file_buffer;
  int input_read_left = bytes;
  int timeout_ms = 60000;

  while (input_read_left > 0) {

    int todo = input_read_left;

    if (input_read_left > SIZE_32K) {
      todo = SIZE_32K;
    }

    input_file_buffer.expand_to(todo);

    int r;
    do {
      r = read(from_fd, input_file_buffer.end(), todo);
    } while (r < 0 && errno == EINTR);

    if (r < 0 || r == 0) {
      TE_Error("[send_file] read from file %s failed: %s", filename, strerror(errno));
      return -1;
    } else {
      input_file_buffer.fill(r);
      input_read_left -= r;
    }

    const char *rmsg = write_buffer(to_fd, &input_file_buffer, &timeout_ms);

    if (rmsg) {
      TE_Error("send_file %s to %s failed: %s", filename, to_id, strerror(errno));
      return -1;
    }
  }

  return 0;
}

int
push_package(HostRecord * hrec, const char *pkg_name, const char *pkg_filename, const char *pkg_filepath)
{

  int pkg_fd = -1, r;
  struct stat stat_info;

  char *length_buf;
  RafCmd request;
  RafCmd response;
  int return_value = 1;

  const char *rmsg;
  int timeout_ms = cmd_timeout * 1000;

  Status("Pushing package %s to %s", pkg_name, hrec->hostname);

  do {
    pkg_fd = open(pkg_filepath, O_RDONLY);
  } while (pkg_fd < 0 && errno == EINTR);

  if (pkg_fd < 0) {
    TE_Error("Failed to open package file %s: %s", pkg_filepath, strerror(errno));
    goto CLEANUP;
  }

  do {
    r = fstat(pkg_fd, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    TE_Error("Failed to fstat package file %s: %s", pkg_filepath, strerror(errno));
    goto CLEANUP;
  }

  length_buf = (char *) malloc(32);
  sprintf(length_buf, "%lld", stat_info.st_size);

  // Send the raf request
  request(0) = hrec->get_id_str();
  request(1) = strdup("take_pkg");
  request(2) = strdup(pkg_name);
  request(3) = strdup(pkg_filename);
  request(4) = length_buf;

  rmsg = send_raf_cmd(hrec->fd, &request, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd take_pkg send failed: %s", rmsg);
    return 1;
  }
  // Send the file
  if (send_file(hrec->fd, pkg_fd, stat_info.st_size, pkg_filename, hrec->hostname) < 0) {
    goto CLEANUP;
  }
  // Read the raf response
  rmsg = read_raf_resp(hrec->fd, hrec->read_buffer, &response, &timeout_ms);

  if (rmsg || response.length() < 2 || atoi(response[1]) != 0) {
    TE_Error("raf cmd take_pkg %s to %s failed: %s", pkg_filename, hrec->hostname, rmsg);
    goto CLEANUP;
  }

  request.clear();
  response.clear();

  request(0) = hrec->get_id_str();
  request(1) = strdup("install");
  request(2) = strdup(pkg_name);
  request(3) = strdup(pkg_filename);

  return_value = do_raf(hrec, &request, &response);

CLEANUP:
  if (pkg_fd >= 0) {
    close(pkg_fd);
  }

  return return_value;
}

int
do_package_management(HostRecord * hrec, const char *pkg_name)
{

  int return_value = 1;
  const char *remote_pkg = hrec->lookup_package(pkg_name);
  char *local_pkg = find_local_package(pkg_name, hrec->arch);

  if (local_pkg == NULL) {
    TE_Warning("No local for package of %s %s", pkg_name, hrec->arch);
    return -1;
  }

  const char *local_pkg_ext;
  int r = check_package_file_extension(local_pkg, &local_pkg_ext);
  bool pkg_match = false;

  if (remote_pkg && r == 0) {
    if (strncmp(remote_pkg, local_pkg, local_pkg_ext - local_pkg) == 0) {
      pkg_match = true;
    }
  }

  if (pkg_match == false) {

    // We need to push our copy of the package
    int pkg_path_len = strlen(package_dir) + 1 + strlen(local_pkg);
    char *pkg_path = (char *) malloc(pkg_path_len + 1);
    sprintf(pkg_path, "%s/%s", package_dir, local_pkg);
    Debug("pkg", "Pushing %s to %s", local_pkg, hrec->hostname);

    return_value = push_package(hrec, pkg_name, local_pkg, pkg_path);
    free(pkg_path);

    hrec->update_package_entry(pkg_name, local_pkg, local_pkg_ext - local_pkg);
  } else {
    Debug("pkg", "Package %s alread on %s", local_pkg, hrec->hostname);
    return_value = 0;
  }

  free(local_pkg);
  return return_value;
}

extern "C"
{
  void sigalrm_handler(int sig);
}

// int safe_sleep(int mseconds)
//
//   When we are watching for kill sigs, there's a race
//     between checking for the signal and then entering
//     usleep
//
//   Instead of usleep, handle this properly with sigsuspend
//     and itimer
//
void
safe_sleep(int mseconds)
{

  sigset_t running_mask;
  sigset_t blocked_mask;
  sigset_t empty_mask;

  sigfillset(&blocked_mask);
  sigemptyset(&empty_mask);

  int r = sigprocmask(SIG_SETMASK, &blocked_mask, &running_mask);
  ink_debug_assert(r == 0);

  check_and_process_kill_signal();

  timeval now;
  timeval end_time;

  r = gettimeofday(&now, NULL);
  ink_debug_assert(r == 0);

  end_time.tv_sec = now.tv_sec + (mseconds / 1000);
  end_time.tv_usec = now.tv_usec + (mseconds % 1000);

  // It's possible the test script has reset our sigalrm
  //  handler.  Make sure it's set the way we want it
  struct sigaction sa_old, sa_new;
  memset(&sa_new, 0, sizeof(sa_new));
  sigemptyset(&sa_new.sa_mask);
  sa_new.sa_handler = sigalrm_handler;
  sa_new.sa_flags = 0;
  sigaction(SIGALRM, &sa_new, &sa_old);

  while (now.tv_sec < end_time.tv_sec || (now.tv_sec == end_time.tv_sec && now.tv_usec < end_time.tv_usec)) {

    itimerval sleep_time;
    memset(&sleep_time, 0, sizeof(sleep_time));
    sleep_time.it_value.tv_sec = end_time.tv_sec - now.tv_sec;
    sleep_time.it_value.tv_usec = end_time.tv_usec - now.tv_usec;

    setitimer(ITIMER_REAL, &sleep_time, NULL);

    sigsuspend(&empty_mask);
    check_and_process_kill_signal();

    // Clear the timer
    sleep_time.it_value.tv_sec = 0;
    sleep_time.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &sleep_time, NULL);

    r = gettimeofday(&now, NULL);
    ink_debug_assert(r == 0);
  }

  // In order not to mess up the test script, reset
  //  the signal handler to way it was before we entered
  //  the function
  sigaction(SIGALRM, &sa_old, NULL);

  r = sigprocmask(SIG_SETMASK, &running_mask, NULL);
  ink_debug_assert(r == 0);
}

int
do_raf(HostRecord * hrec, RafCmd * request, RafCmd * response)
{

  int timeout_ms = cmd_timeout * 1000;

  const char *rmsg;
  rmsg = send_raf_cmd(hrec->fd, request, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd send failed: %s", rmsg);
    return -1;
  }

  rmsg = read_raf_resp(hrec->fd, hrec->read_buffer, response, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd read failed: %s", rmsg);
    return -1;
  }

  return 0;
}

int
do_raf(int fd, RafCmd * request, RafCmd * response)
{

  sio_buffer read_buffer;
  int timeout_ms = cmd_timeout * 1000;

  const char *rmsg;
  rmsg = send_raf_cmd(fd, request, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd send failed: %s", rmsg);
    return -1;
  }

  rmsg = read_raf_resp(fd, &read_buffer, response, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd read failed: %s", rmsg);
    return -1;
  }

  return 0;
}

int
pm_base_cmd(HostRecord * hrec, RafCmd * request,
            RafCmd * response, const char *cmd, const char *instance_name, char **args)
{

  // Host already found be to down
  if (hrec->fd < 0) {
    return 1;
  }

  request->clear();
  (*request) (0) = hrec->get_id_str();
  (*request) (1) = strdup(cmd);
  (*request) (2) = strdup(instance_name);

  int i = 3;

  if (args) {
    char **args_start = args;
    while (*args != NULL) {
      (*request) (i) = strdup(*args);
      args++;
      i++;
    }
    destroy_argv(args_start);
  }

  int r = do_raf(hrec, request, response);

  if (r < 0) {
    TE_Error("Lost contact with proc_manager on %s", hrec->hostname);
    close(hrec->fd);
    hrec->fd = -1;
    return 1;
  }

  if (response->length() < 2) {
    return 1;
  }

  const char *status_code = (*response)[1];
  r = atoi(status_code);

  if (r == 0 && *status_code != '0') {
    return 1;
  }

  return r;
}

HostRecord *
find_or_setup_host(const char *host_name, const char *instance_name)
{

  sio_buffer hostname_sub;

  // Check to see if this a virtual hostname name that needs
  //   to be substituted to real hostname
  if (host_name && host_name[0] == '%') {
    int r = do_substitutions(host_name, -1, &hostname_sub, NULL);

    if (r > 0) {
      // Substitution made
      //
      // Null terminate the hostname_sub buf
      hostname_sub.fill("", 1);
      Debug("subs", "Substituted hostname %s to %s for %s", host_name, hostname_sub.start(), instance_name);
      host_name = hostname_sub.start();
    }
  }

  HostRecord *host_rec = find_host_rec(host_name);
  if (host_rec == NULL) {
    host_rec = create_host_rec(host_name);
  }

  return host_rec;
}

int
pm_create_instance(const char *instance_name, const char *host_name, char **args)
{

  check_and_process_kill_signal();

  RafCmd request;
  RafCmd response;

  HostRecord *host_rec = find_or_setup_host(host_name, instance_name);

  if (host_rec == NULL) {
    TE_Error("Creation of instance %s failed", instance_name);
    return 1;
  }

  const char *pkg_name = NULL;
  const char *local_path = NULL;
  if (args) {
    char **tmp = args;
    while (*tmp) {
      if (strcasecmp(*tmp, "package") == 0) {
        if (*(tmp + 1)) {
          pkg_name = *(tmp + 1);
          tmp++;
        }
      } else if (strcasecmp(*tmp, "localpath") == 0) {
        if (*(tmp + 1)) {
          local_path = *(tmp + 1);

          int sub_errors;
          int r = do_subs_and_replace(tmp + 1, &sub_errors);

          if (sub_errors > 0) {
            TE_Warning("Substituions on localpath for %s failed", instance_name);
          } else if (r > 0) {
            Debug("subs", "Made %d subs on localpath for %s", r, instance_name);
          }
          tmp++;
        }
      } else if (strcasecmp(*tmp, "config") == 0) {
        // Do substitutions on the config
        if (*(tmp + 1)) {
          int sub_errors;
          int r = do_subs_and_replace(tmp + 1, &sub_errors);

          if (sub_errors > 0) {
            TE_Warning("Substituions on config for %s failed", instance_name);
          } else if (r > 0) {
            Debug("subs", "Made %d subs on config for %s", r, instance_name);
          }
          tmp++;
        }
      }
      tmp++;
    }
  }

  if (pkg_name && local_path == NULL) {
    if (do_package_management(host_rec, pkg_name) != 0) {
      TE_Error("Package push of %s for instance %s failed", pkg_name, instance_name);
      return 1;
    }

  }

  int r = pm_base_cmd(host_rec, &request, &response, "create", instance_name, args);

  if (r == 0) {
    InstanceRecord *irec = new InstanceRecord(instance_name);
    irec->host_rec = host_rec;
    instance_list.push(irec);

    int resp_len = response.length();

    for (int i = 3; i + 1 < resp_len; i += 2) {
      irec->add_port_binding(response[i], response[i + 1]);
    }
  }

  return r;
}

int
pm_start_instance(const char *instance_name, char **args)
{

  check_and_process_kill_signal();

  RafCmd *request = new RafCmd;
  RafCmd *response = new RafCmd;

  if (args) {
    char **tmp = args;
    while (*tmp) {
      if (strcasecmp(*tmp, "args") == 0) {
        if (*(tmp + 1)) {
          int sub_errors;
          int r = do_subs_and_replace(tmp + 1, &sub_errors);

          if (sub_errors > 0) {
            TE_Warning("Substituions for args to %s failed", instance_name);
          } else if (r > 0) {
            Debug("subs", "Made %d subs on args to %s", r, instance_name);
          }
          tmp++;
        }
      }
      tmp++;
    }
  }

  InstanceRecord *irec = find_instance_rec(instance_name);

  int r;
  if (irec) {
    r = pm_base_cmd(irec->host_rec, request, response, "start", instance_name, args);
  } else {
    r = 1;
    TE_Error("start cmd for unknown instance %s", instance_name);
  }

  delete request;
  delete response;

  return r;
}

int
pm_stop_instance(const char *instance_name, char **args)
{

  check_and_process_kill_signal();

  RafCmd *request = new RafCmd;
  RafCmd *response = new RafCmd;

  InstanceRecord *irec = find_instance_rec(instance_name);

  int r;
  if (irec) {
    r = pm_base_cmd(irec->host_rec, request, response, "stop", instance_name, args);
  } else {
    r = 1;
    TE_Error("stop cmd for unknown instance %s", instance_name);
  }

  delete request;
  delete response;

  return r;
}

int
pm_destroy_instance(const char *instance_name, char **args)
{

  check_and_process_kill_signal();

  RafCmd *request = new RafCmd;
  RafCmd *response = new RafCmd;

  InstanceRecord *irec = find_instance_rec(instance_name);

  int r;
  if (irec) {
    r = pm_base_cmd(irec->host_rec, request, response, "destroy", instance_name, args);
  } else {
    r = 1;
    TE_Error("destroy cmd for unknown instance %s", instance_name);
  }

  delete request;
  delete response;

  if (r == 0) {
    instance_list.remove(irec);
    delete irec;
  }

  return r;
}

// static char* alloc_and_sprintf(int d)
//
//   CALLER frees return value
//
static char *
alloc_and_sprintf(int d)
{
  char *int_buf = (char *) malloc(32);
  sprintf(int_buf, "%d", d);
  return int_buf;
}

static char *
pm_run_internal(HostRecord * host_rec, const char *binary, const char *args, const char *master_instance, int timeout)
{

  RafCmd request;
  RafCmd response;
  int return_int;

  char **raf_argv = (char **) malloc(sizeof(char *) * 5);
  int i = 0;

  if (binary == NULL) {
    alloc_and_sprintf(-2);
  }

  if (args) {
    sio_buffer args_subs;
    int sub_errors = 0;
    int r = do_substitutions(args, strlen(args), &args_subs, &sub_errors);

    if (sub_errors > 0) {
      TE_Warning("pm_run %s had %d substitution errors on args", binary, sub_errors);
    }
    args_subs.fill("", 1);

    raf_argv[i] = strdup("args");
    raf_argv[i + 1] = strdup(args_subs.start());
    i += 2;
  }

  if (master_instance) {
    raf_argv[i] = strdup("master");
    raf_argv[i + 1] = strdup(master_instance);
    i += 2;
  }

  raf_argv[i] = NULL;

  int r = pm_base_cmd(host_rec, &request, &response, "run", binary, raf_argv);

  if (r == 0 && response.length() >= 3) {
    char *instance_name = NULL;
    instance_name = strdup(response[2]);

    InstanceRecord *irec = new InstanceRecord(instance_name);
    irec->host_rec = host_rec;
    instance_list.push(irec);

    if (timeout < 0) {
      return instance_name;
    } else {
      return_int = wait_for_instance_death(instance_name, timeout);
      pm_destroy_instance(instance_name, NULL);
      free(instance_name);
    }
  } else {
    return_int = -2;
  }

  return alloc_and_sprintf(return_int);
}

char *
pm_run_slave(const char *master_instance, const char *binary, const char *args, int timeout)
{

  check_and_process_kill_signal();

  InstanceRecord *irec = find_instance_rec(master_instance);

  if (irec == NULL) {
    TE_Error("run_slave cmd for unknown master instance %s", master_instance);
    return alloc_and_sprintf(-2);
  }

  return pm_run_internal(irec->host_rec, binary, args, master_instance, timeout);
}

char *
pm_run(const char *hostname, const char *binary, const char *args, int timeout)
{

  check_and_process_kill_signal();

  int return_int = -2;
  HostRecord *host_rec = find_or_setup_host(hostname, "anon");

  if (host_rec == NULL) {
    TE_Error("Run cmd %s on %s failed", binary, hostname);
    return alloc_and_sprintf(-2);
  }

  return pm_run_internal(host_rec, binary, args, NULL, timeout);
}


int
add_to_log(const char *log_line)
{

  check_and_process_kill_signal();

  int len = strlen(log_line);
  TE_output_log_line(log_line, log_line + len, "test_script", "log");

  return 0;
}

int
pm_alloc_port(const char *hostname)
{

  check_and_process_kill_signal();

  HostRecord *host_rec = find_or_setup_host(hostname, "alloc_port");

  if (host_rec == NULL) {
    TE_Error("Alloc port on %s failed", hostname);
    return -1;
  }

  RafCmd request;
  RafCmd response;
  request(0) = host_rec->get_id_str();
  request(1) = strdup("alloc_port");

  int r = do_raf(host_rec, &request, &response);

  if (r < 0 || response.length() < 3) {
    return -1;
  }

  const char *status_code = response[1];

  if (status_code == NULL || *status_code != '0' || atoi(status_code) != 0) {
    return -1;
  }

  const char *port_str = response[2];

  int port = atoi(port_str);

  if (port > 0) {
    return port;
  } else {
    return -1;
  }
}

int
set_log_parser(const char *instance, const char *parser)
{

  check_and_process_kill_signal();

  const char fmt_str[] = "log-parser-set %s %s\n";
  int len = strlen(fmt_str) + strlen(instance) + strlen(parser);
  char *line = (char *) malloc(len + 1);
  int output_len = sprintf(line, fmt_str, instance, parser);

  TE_output_log_line(line, line + output_len, "log_parse", "directive");

  return 0;
}

char *
get_var_value(const char *var)
{

  check_and_process_kill_signal();

  if (var == NULL) {
    return NULL;
  }

  sio_buffer result;
  int var_len = strlen(var);

  int r = do_single_substitution(var, var + var_len, &result, 0);

  if (r < 0) {
    return NULL;
  } else {
    // NULL terminate
    result.fill("", 1);
    return strdup(result.start());

  }

  return NULL;
}

int
set_var_value(const char *var, const char *var_value)
{

  check_and_process_kill_signal();

  if (var == NULL) {
    return 1;
  }

  const char *colon;
  if ((colon = strchr(var, ':')) != NULL) {
    // Porting binding var
    char *instance = strdup(var);
    *(instance + (colon - var)) = '\0';
    const char *ivar = instance + (colon - var) + 1;

    InstanceRecord *irec = find_instance_rec(instance);

    if (irec == NULL) {
      TE_Warning("set_var_value for unknown instance '%s'", instance);
      free(instance);
      return 1;
    }

    irec->add_port_binding(ivar, var_value);
    free(instance);
  } else {
    // Normal var
    add_def(var, strdup(var_value));
  }

  return 0;
}

// char* construct_instance_file_path(InstanceRecord* irec, const char* file)
// 
//  CALLER FREES return value 
static char *
construct_instance_file_path(InstanceRecord * irec, const char *file)
{

  // Construct a full path name on the remote
  char *full_path;
  if (*file == '/') {
    full_path = strdup(file);
  } else {
    int len;
    const char *instance_run_dir = irec->get_port_binding("run_dir");
    ink_debug_assert(instance_run_dir != NULL);

    if (instance_run_dir == NULL) {
      return NULL;
    }

    len = strlen(instance_run_dir) + 1 + strlen(file);
    full_path = (char *) malloc(len + 1);
    sprintf(full_path, "%s/%s", instance_run_dir, file);
  }

  return full_path;
}

// char* construct_instance_file_path(InstanceRecord* irec, const char* file)
// 
//  CALLER FREES return value 
static char *
construct_script_file_path(const char *file)
{

  // Construct a full path name on the remote
  char *full_path;
  if (*file == '/') {
    full_path = strdup(file);
  } else {
    int len = strlen(cur_script_path) + 1 + strlen(file);
    full_path = (char *) malloc(len + 1);
    sprintf(full_path, "%s/%s", cur_script_path, file);
  }

  return full_path;
}


static void
set_cur_script_path(const char *default_script_dir, const char *script_name)
{

  if (*script_name == '/') {
    snprintf(cur_script_path, PATH_MAX, "%s", script_name);
    cur_script_path[PATH_MAX] = '\0';
  } else {
    snprintf(cur_script_path, PATH_MAX, "%s/%s", default_script_dir, script_name);
    cur_script_path[PATH_MAX] = '\0';
  }

  // Trim off the script name
  char *last = NULL;
  char *tmp = cur_script_path;

  while (tmp = strchr(tmp, '/')) {
    last = tmp;
    tmp += 1;
  }

  *last = '\0';
}

int
put_instance_file_raw(const char *instance, const char *relative_path, const char *src)
{

  check_and_process_kill_signal();

  int return_value = 1;

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return -1;
  }

  int fd = -1;


  char *src_path = construct_script_file_path(src);
  do {
    fd = open(src_path, O_RDONLY);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    TE_Warning("put_instance_file_raw open failed : %s : %s", src_path, strerror(errno));
    free(src_path);
    return -1;
  }
  free(src_path);

  struct stat stat_info;
  int r;
  do {
    r = fstat(fd, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    close(fd);
    TE_Warning("put_instance_file_raw fstat failed : %s : %s", src, strerror(errno));
    return -1;
  }

  //  Get full path name on the remote
  char *full_path = construct_instance_file_path(irec, relative_path);
  char *length_buf = (char *) malloc(32);
  sprintf(length_buf, "%d", (int) stat_info.st_size);

  char *mode_buf = (char *) malloc(32);
  sprintf(mode_buf, "%o", (unsigned int) stat_info.st_mode);

  Debug("put_file", "sending put_file %s %s %s", full_path, length_buf, mode_buf);

  // Set up 
  RafCmd request;
  RafCmd response;
  request(0) = irec->host_rec->get_id_str();
  request(1) = strdup("put_file");
  request(2) = full_path;
  request(3) = length_buf;
  request(4) = mode_buf;

  int timeout_ms = 60000;
  const char *rmsg = send_raf_cmd(irec->host_rec->fd, &request, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd put_file send failed: %s", rmsg);
    goto CLEANUP;
  }
  // Send the file
  if (send_file(irec->host_rec->fd, fd, stat_info.st_size, relative_path, irec->instance_name) < 0) {
    goto CLEANUP;
  }
  // Read the raf response
  rmsg = read_raf_resp(irec->host_rec->fd, irec->host_rec->read_buffer, &response, &timeout_ms);

  if (rmsg || response.length() < 2 || atoi(response[1]) != 0) {
    TE_Error("raf cmd put_file %s to instance %s failed : %s", src, instance, rmsg ? rmsg : response[2]);
    goto CLEANUP;
  }

  return_value = 0;

CLEANUP:
  Debug("put_file", "put file result : %d", return_value);
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }

  return return_value;
}

int
put_instance_file_subs(const char *instance, const char *relative_path, const char *src)
{

  check_and_process_kill_signal();

  int return_value = 1;

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return -1;
  }

  int fd = -1;
  char *src_path = construct_script_file_path(src);
  do {
    fd = open(src_path, O_RDONLY);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    TE_Warning("put_instance_file_subs open failed : %s : %s", src_path, strerror(errno));
    free(src_path);
    return -1;

  }
  free(src_path);

  struct stat stat_info;
  int r;
  do {
    r = fstat(fd, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    close(fd);
    TE_Warning("put_instance_file_subs fstat failed : %s : %s", src, strerror(errno));
    return -1;
  }

  int eof = 0;
  int timeout_ms = 60000;
  sio_buffer file_buffer;
  const char *r_msg = read_to_buffer(fd, &file_buffer, stat_info.st_size, &eof, &timeout_ms);

  close(fd);

  if (r_msg || eof != 0) {
    TE_Error("put_instance_file_subs read failed : %s", r_msg ? r_msg : "eof");
    return -1;
  }

  int sub_errors = 0;
  sio_buffer sub_buffer;
  r = do_substitutions(file_buffer.start(), file_buffer.read_avail(), &sub_buffer, &sub_errors);

  if (sub_errors > 0) {
    TE_Warning("put_instance_file_subs %s had %d substitution errors", src, sub_errors);
  }
  //  Get full path name on the remote
  char *full_path = construct_instance_file_path(irec, relative_path);
  char *length_buf = (char *) malloc(32);
  sprintf(length_buf, "%d", (int) sub_buffer.read_avail());

  // Handle file mode
  char *mode_buf = (char *) malloc(32);
  sprintf(mode_buf, "%o", (unsigned int) stat_info.st_mode);

  Debug("put_file", "sending put_file %s %s %s", full_path, length_buf, mode_buf);

  // Set up 
  RafCmd request;
  RafCmd response;
  request(0) = irec->host_rec->get_id_str();
  request(1) = strdup("put_file");
  request(2) = full_path;
  request(3) = length_buf;
  request(4) = mode_buf;

  timeout_ms = 60000;
  const char *rmsg = send_raf_cmd(irec->host_rec->fd, &request, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd put_file send failed: %s", rmsg);
    goto CLEANUP;
  }

  r_msg = write_buffer(irec->host_rec->fd, &sub_buffer, &timeout_ms);

  if (rmsg) {
    TE_Error("raf cmd put_file send failed: %s", rmsg);
    goto CLEANUP;
  }
  // Read the raf response
  rmsg = read_raf_resp(irec->host_rec->fd, irec->host_rec->read_buffer, &response, &timeout_ms);

  if (rmsg || response.length() < 2 || atoi(response[1]) != 0) {
    TE_Error("raf cmd put_file_subs %s to instance %s failed : %s", src, instance, rmsg ? rmsg : response[2]);
    goto CLEANUP;
  }

  return_value = 0;

CLEANUP:
  Debug("put_file", "put file w/subs result : %d", return_value);

  return return_value;
}

// int query_process_int(const char* instance, const char* field, int* result)
//
//   Returns 0 if the query succeeeds, otherwise returns one
//   *result is to the query result if the query succeeded
//
int
query_process_int(const char *instance, const char *field, int *result)
{


  char **queryv = (char **) malloc(sizeof(char *) * 2);

  const char *query_format = "/processes/%s/%s";
  int qlen = strlen(query_format) + strlen(instance) + strlen(field);
  char *query_string = (char *) malloc(qlen + 1);
  sprintf(query_string, query_format, instance, field);
  queryv[0] = query_string;
  queryv[1] = NULL;

  char **resultv = raf_proc_manager(instance, "query", queryv);

  int resultv_len = 0;
  char **tmp = resultv;
  while (*tmp != NULL) {
    resultv_len++;
    tmp++;
  }

  int return_value = 1;
  if (resultv_len >= 3) {
    *result = atoi(resultv[2]);
    return_value = 0;
  }

  destroy_argv(resultv);
  return return_value;
}

int
is_instance_alive(const char *instance)
{

  check_and_process_kill_signal();

  if (instance == NULL) {
    return 0;
  }

  int pid;
  int r = query_process_int(instance, "pid", &pid);

  if (r == 0 && pid > 0) {
    return 1;
  } else {
    return 0;
  }
}

int
wait_for_instance_death(const char *instance, int timeout_ms)
{

  check_and_process_kill_signal();

  if (instance == NULL) {
    return -2;
  }

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return -2;
  }
  //  Run a connect/poll loop waiting for either the socket
  //   to become live or timeout
  ink_hrtime start_time = ink_get_based_hrtime_internal();

  while (1) {

    int pid;
    int r = query_process_int(instance, "pid", &pid);

    if (r != 0) {
      TE_Warning("[wait for process death] query failed");
      return -2;
    } else if (pid < 0) {
      // The process has terminated, get it's exit status
      int exit_status;
      r = query_process_int(instance, "exit_status", &exit_status);

      if (r == 0) {
        return exit_status;
      } else {
        return -2;
      }
    } else {
      // The process is still running
      //
      // Check to see if we've run out of time waiting for the process
      if (timeout_ms > 0) {
        ink_hrtime now = ink_get_based_hrtime_internal();
        int ms_left = timeout_ms - ink_hrtime_to_msec(now - start_time);

        if (ms_left < 0) {
          if (pm_stop_instance(instance, NULL) == 0) {
            // We've successfully stoped the instance
            //   report a timeout
            return -1;
          } else {
            // Stop failed - report internal error
            return -2;
          }
        }
      }
      // Wait 100ms before rechecking
      safe_sleep(100);
    }
  }

}


int
wait_for_server_port(const char *instance, const char *port_str, int timeout_ms)
{

  check_and_process_kill_signal();

  if (instance == NULL || port_str == NULL) {
    return -1;
  }

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return -1;
  }
  // We accept three forms for port_str
  //
  //   1) all digits - converted to int
  //   2) %%(var) - deft var substitution done and then converted to int
  //   3) other string - looked up in port bindings has for instance 
  //      and then converted to int
  int port = -1;
  if (isdigit(port_str[0])) {
    port = atoi(port_str);
  } else if (port_str[0] == '%') {
    sio_buffer port_buf;
    int r = do_substitutions(port_str, strlen(port_str), &port_buf, NULL);

    if (r == 1) {
      port_buf.fill("", 1);
      port = atoi(port_buf.start());
    }
  } else {
    const char *p = irec->get_port_binding(port_str);

    if (p) {
      port = atoi(p);
    }
  }

  if (port <= 0) {
    TE_Warning("[wait_for_server] Could not resolve %s port %s", instance, port_str);
    return -1;
  }
  Debug("port", "waiting for port %d on instance %s", port, instance);

  //  Run a connect/poll loop waiting for either the socket
  //   to become live or timeout
  ink_hrtime start_time = ink_get_based_hrtime_internal();
  bool success = false;
  int ms_left;
  int fd = -1;
  do {
    if (fd < 0) {
      do {
        fd = SIO::make_client(irec->host_rec->ip, port);
      } while (fd < 0 && errno == EINTR);
    }

    ink_hrtime now = ink_get_based_hrtime_internal();
    ms_left = timeout_ms - ink_hrtime_to_msec(now - start_time);

    if (ms_left <= 0) {
      ms_left = 0;
    } else if (fd < 0) {
      safe_sleep(1000);
    }

    if (fd >= 0) {
      pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;

      int r = poll(&pfd, 1, ms_left);

      if (r == 0) {
        ms_left = 0;
      } else if (r == 1) {
        if (pfd.revents & POLLOUT) {
          success = true;
        } else {
          // Some sort of error on the socket.  Try again
          close(fd);
          fd = -1;
        }
      } else {
        Debug("port", "[wait for server port] poll failed : %s", strerror(errno));
      }
    }
  } while (ms_left > 0 && success == false);

  if (fd > 0) {
    close(fd);
    fd = -1;
  }

  if (success) {
    Debug("port", "[wait for server port] success");
    return 0;
  } else {
    Debug("port", "[wait for server port] failed");
    return -1;
  }
}

char *
get_instance_file(const char *instance, const char *file)
{

  check_and_process_kill_signal();

  if (instance == NULL || file == NULL) {
    return NULL;
  }

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return NULL;
  }
  //  Get full path name on the remote
  char *full_path = construct_instance_file_path(irec, file);

  RafCmd raf_req;
  RafCmd raf_resp;

  // Construct the raf request
  raf_req(0) = irec->host_rec->get_id_str();
  raf_req(1) = strdup("get_file");
  raf_req(2) = full_path;

  // Make the raf request
  int timeout_ms = 60000;
  const char *r_msg = send_raf_cmd(irec->host_rec->fd, &raf_req, &timeout_ms);

  if (r_msg) {
    TE_Error("send of raf cmd 'get_file' to %s failed: %s", irec->host_rec->hostname, r_msg);
    return NULL;
  }
  // Read the Raf response
  sio_buffer resp_buffer;
  r_msg = read_raf_resp(irec->host_rec->fd, &resp_buffer, &raf_resp, &timeout_ms);

  // Check the raf the response to see if was successful
  if (r_msg) {
    TE_Error("read of raf resp to 'get_file' from %s failed: %s", irec->host_rec->hostname, r_msg);
    return NULL;
  }

  if (raf_resp.length() < 3) {
    TE_Error("malformed raf resp to 'get_file' from %s", irec->host_rec->hostname);
    return NULL;
  }

  const char *raf_result_code = raf_resp[1];
  if (*raf_result_code != '0') {
    TE_Error("raf cmd 'get_file' from %s failed: %s", raf_resp[2]);
    return NULL;
  }

  int file_len = atoi(raf_resp[2]);

  // Construct the local filename
  int name_len = strlen(ud_info->tmp_dir) + 1 + strlen(instance)
    + 1 + strlen(raf_req[0]);
  char *local_file = (char *) malloc(name_len + 1);
  sprintf(local_file, "%s/%s.%s", ud_info->tmp_dir, instance, raf_req[0]);

  // Open up the local file for writing
  int local_fd;
  do {
    local_fd = open(local_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  } while (local_fd < 0 && errno == EINTR);

  if (local_fd < 0) {
    TE_Error("could not create %s for raf cmd 'get_file' : %s", local_file, strerror(errno));
    free(local_file);
    local_file = NULL;
  }
  // Transfer the file
  int read_bytes_left = file_len - resp_buffer.read_avail();
  int write_bytes_left = file_len;
  bool xfer_failed = false;
  timeout_ms = 5 * 60 * 1000;

  while (read_bytes_left > 0 || write_bytes_left > 0) {

    int act_on;
    if (read_bytes_left > 0) {
      if (read_bytes_left > SIZE_32K) {
        act_on = SIZE_32K;
      } else {
        act_on = read_bytes_left;
      }

      int eof;
      int old_avail = resp_buffer.read_avail();
      r_msg = read_to_buffer(irec->host_rec->fd, &resp_buffer, act_on, &eof, &timeout_ms);

      if (eof || r_msg) {
        TE_Error("file xfer for raf cmd 'get_file' from %s failed: %s", instance, (eof != 0) ? "eof" : r_msg);
        xfer_failed = true;
        break;
      } else {
        read_bytes_left -= resp_buffer.read_avail() - old_avail;
      }
    }
    // If we failed open the file, we still consume bytes on the transfer
    if (local_fd < 0) {
      act_on = resp_buffer.read_avail();
      write_bytes_left -= act_on;
      resp_buffer.consume(act_on);
    } else {
      act_on = resp_buffer.read_avail();

      int old_avail = resp_buffer.read_avail();
      r_msg = write_buffer(local_fd, &resp_buffer, &timeout_ms);

      if (r_msg) {
        xfer_failed = true;
        TE_Error("file write for raf cmd 'get_file' from %s failed: %s", instance, r_msg);
        close(local_fd);
        local_fd = -1;
        resp_buffer.consume(resp_buffer.read_avail());
      } else {
        write_bytes_left -= old_avail - resp_buffer.read_avail();
      }
    }
  }

  if (local_fd > 0) {
    close(local_fd);

    if (xfer_failed) {
      free(local_file);
      return NULL;
    } else {
      return local_file;
    }
  } else {
    return NULL;
  }
}

char **
stat_instance_file(const char *instance, const char *file)
{

  check_and_process_kill_signal();

  if (instance == NULL || file == NULL) {
    return NULL;
  }

  InstanceRecord *irec = find_instance_rec(instance);
  if (irec == NULL) {
    return NULL;
  }

  //  Get full path name on the remote
  char *full_path = construct_instance_file_path(irec, file);

  RafCmd raf_req;
  RafCmd raf_resp;

  // Construct the raf request
  raf_req(0) = irec->host_rec->get_id_str();
  raf_req(1) = strdup("stat_file");
  raf_req(2) = full_path;

  int r = do_raf(irec->host_rec, &raf_req, &raf_resp);

  if (r < 0 || raf_resp.length() < 4) {
    return NULL;
  }

  const char *status_code = raf_resp[1];

  if (status_code == NULL || *status_code != '0' || atoi(status_code) != 0) {
    return NULL;
  }

  int num_el = raf_resp.length() - 2;

  char **return_val = (char **) malloc(sizeof(char *) * (num_el + 1));

  for (int i = 0; i < num_el; i++) {
    return_val[i] = strdup(raf_resp[i + 2]);
  }
  return_val[num_el] = NULL;

  return return_val;
}


// char** build_raf_err_argv(const char* err_str)
//
//   Caller must free both return value itself
//     and strings pointed to by return value
//
static char **
build_raf_err_argv(const char *err_str)
{

  char **return_value = (char **) malloc(sizeof(char *) * 3);
  return_value[0] = strdup("1");
  return_value[1] = strdup(err_str);
  return_value[2] = NULL;

  return return_value;
}

// char** raf_host_port()
//
//   Caller must free both return value itself
//     and strings pointed to by return value
//
static char **
raf_host_port(unsigned int ip, int port, const char *raf_cmd, char **raf_args)
{

  RafCmd *request = new RafCmd;
  RafCmd *response = new RafCmd;
  const char *err_str = NULL;
  char **return_value = NULL;

  char **tmp;
  int r;
  int fd = SIO::make_client(ip, port);

  if (fd < 0) {
    err_str = "Connect error";
    goto CLEANUP;
  }

  (*request) (0) = strdup("0");
  (*request) (1) = strdup(raf_cmd);
  if (raf_args) {
    int i = 2;
    tmp = raf_args;
    while (*tmp) {
      int sub_errors;
      int r = do_subs_and_replace(tmp, &sub_errors);
      if (sub_errors > 0) {
        TE_Warning("Subsitution failed for raf args : %s", *tmp);
      }

      (*request) (i) = strdup(*tmp);
      tmp++;
      i++;
    }
    destroy_argv(raf_args);
  }

  r = do_raf(fd, request, response);

  if (r < 0) {
    err_str = "raf cmd failed";
  } else {
    int resp_len = response->length();
    if (resp_len < 2) {
      err_str = "truncated raf resp";
      goto CLEANUP;
    } else {
      resp_len -= 1;
      return_value = (char **) malloc(sizeof(char *) * (resp_len + 1));
      for (int i = 0; i < resp_len; i++) {
        return_value[i] = strdup((*response)[i + 1]);
      }
      return_value[resp_len] = NULL;
    }
  }


CLEANUP:

  if (fd >= 0) {
    close(fd);
  }

  if (err_str) {
    ink_debug_assert(return_value == NULL);
    TE_Error("raf to %u.%u.%u.%u:%d failed: %s",
             ((unsigned char *) &ip)[0], ((unsigned char *) &ip)[1],
             ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3], port, err_str);
    return_value = build_raf_err_argv(err_str);
  }

  delete request;
  delete response;

  return return_value;
}

char **
raf_instance(const char *instance_name, const char *raf_cmd, char **raf_args)
{

  const char *err_str = NULL;
  char **return_value = NULL;

  InstanceRecord *irec = find_instance_rec(instance_name);

  if (irec) {
    int raf_port;
    const char *raf_port_str = irec->get_port_binding("rafPort");

    if (raf_port_str == NULL || (raf_port = atoi(raf_port_str)) == 0) {
      err_str = "No rafPort registered";
    } else {
      return_value = raf_host_port(irec->host_rec->ip, raf_port, raf_cmd, raf_args);
    }
  } else {
    err_str = "No such instance";
  }

  if (err_str) {
    ink_debug_assert(return_value == NULL);
    TE_Error("raf to %s failed: %s", instance_name, err_str);
    return_value = build_raf_err_argv(err_str);
  }

  return return_value;
}

char **
raf_proc_manager(const char *instance_name, const char *raf_cmd, char **raf_args)
{

  check_and_process_kill_signal();

  const char *err_str = NULL;
  char **return_value = NULL;

  InstanceRecord *irec = find_instance_rec(instance_name);

  if (irec) {
    return_value = raf_host_port(irec->host_rec->ip, irec->host_rec->port, raf_cmd, raf_args);
  } else {
    err_str = "No such instance";
  }

  if (err_str) {
    ink_debug_assert(return_value == NULL);
    TE_Error("raf to proc_manager of %s failed: %s", instance_name, err_str);
    return_value = build_raf_err_argv(err_str);
  }

  return return_value;
}

void
stop_and_destroy_all_instances()
{

  check_and_process_kill_signal();

  InstanceRecord *irec = NULL;
  DLL<InstanceRecord> failed_destroys;


  while (instance_list.head) {
    pm_stop_instance(instance_list.head->instance_name, NULL);
    int r = pm_destroy_instance(instance_list.head->instance_name, NULL);

    if (r != 0) {
      irec = instance_list.head;
      instance_list.remove(irec);
      failed_destroys.push(irec);
    }
  }

  ink_debug_assert(instance_list.head == NULL);

  while (failed_destroys.head) {
    irec = failed_destroys.pop();
    instance_list.push(irec);
  }
}

void
shutdown_proc_managers()
{
  HostRecord *hrec = host_list.head;

  while (hrec) {

    RafCmd req;
    RafCmd resp;

    if (hrec->fd > 0) {
      req(0) = hrec->get_id_str();
      req(1) = strdup("shutdown");

      int r = do_raf(hrec, &req, &resp);

      if (r < 0) {
        TE_Error("Failed to stop proc_manager on %s", hrec->hostname);

      }
      close(hrec->fd);
      hrec->fd = -1;
    }

    hrec = hrec->link.next;
  }
}

void
shutdown_log_collator()
{

  if (log_collator_fd >= 0) {
    close(log_collator_fd);
    log_collator_fd = -1;
  } else {
    // The collator is already dead so do not
    //  try to shut it down
    return;
  }

  if (log_collator_port < 0) {
    return;
  }

  RafCmd request;
  RafCmd response;

  request(0) = strdup("0");
  request(1) = strdup("shutdown");
  request(2) = strdup("30");

  unsigned long local_host_ip = inet_addr("127.0.0.1");
  int fd = SIO::make_client(local_host_ip, log_collator_port);

  if (fd < 0) {
    TE_Error("failed to shutdown log collator");
    return;
  }

  int r = do_raf(fd, &request, &response);
  close(fd);

  if (r < 0) {
    TE_Error("failed to shutdown log collator");
  } else if (response.length() < 2 || strcmp(response[1], "0") != 0) {
    TE_Error("collator shutdown shutdown cmd failed: %s %s",
             (response.length() >= 2) ? response[1] : "?", (response.length() >= 3) ? response[1] : "?");
  } else {
    Debug("log", "log collator shutdown succeeded");
  }

  int lc_status;
  reap_and_kill_child(log_collator_pid, &lc_status);
}

static const int SUB_MOD_IP_ADDR = 0x1;
static const int SUB_MOD_IP_RESOLVE = 0x1 << 1;

int
output_substitution(sio_buffer * output_buffer, const char *modifier_str,
                    const char *sub_name, const char *value, int value_len)
{

  int modifiers = 0;

  if (modifier_str) {
    while (*modifier_str != '\0') {
      switch (*modifier_str) {
      case 'i':
        modifiers |= SUB_MOD_IP_ADDR;
        break;
      case 'r':
        modifiers |= SUB_MOD_IP_RESOLVE;
        break;
      default:
        TE_Warning("Bad modifier '%c' on substitution %s ignored", *modifier_str, sub_name);
        break;

      }
      modifier_str++;
    }
  }

  if (modifiers & SUB_MOD_IP_ADDR || modifiers & SUB_MOD_IP_RESOLVE) {

    // We need a NULL terminated string for the lookup
    char *hname = (char *) malloc(value_len + 1);
    memcpy(hname, value, value_len);
    hname[value_len] = '\0';

    bool success = false;
    struct in_addr in;

    if (modifiers & SUB_MOD_IP_ADDR) {
      HostRecord *hrec = find_host_rec(hname);

      if (hrec != NULL) {
        success = true;
        in.s_addr = hrec->ip;
      } else {
        TE_Warning("remote ip subsitution for '%s' failed", sub_name);
      }
    } else {
      struct hostent *he = gethostbyname(hname);

      if (he != NULL) {
        success = true;
        memcpy(&in.s_addr, *he->h_addr_list, sizeof(in.s_addr));
      } else {
        TE_Warning("hostname lookup for substition of '%s' failed : %s", sub_name, strerror(errno));
      }
    }
    free(hname);

    if (success == false) {
      return -1;
    }

    char *ip_str = inet_ntoa(in);
    int len = strlen(ip_str);
    output_buffer->fill(ip_str, len);
    return len;
  } else {
    output_buffer->fill(value, value_len);
    return value_len;
  }
}


int
do_single_substitution(const char *sub_name_start,
                       const char *sub_name_end, sio_buffer * output_buffer, int output_warnings)
{

  int sub_name_len = sub_name_end - sub_name_start;

  if (sub_name_len == 0) {
    if (output_warnings) {
      TE_Warning("empty substitution variable");
    }
    return -1;
  }
  // Make a null terminated string out of the substitution
  //  name so we can pass it various library functions
  char *sub_name = (char *) malloc(sub_name_len + 1);
  FreeOnDestruct freer(sub_name);
  memcpy(sub_name, sub_name_start, sub_name_len);
  sub_name[sub_name_len] = '\0';

  // Names with colons in them are taken to mean
  //   <instance_name>:<port_binding>
  // Names that end with a slash on the end are taken to mean
  //   <var_name>/<modifier>
  // The only modifiers current supported is /i or /r which
  //   asks for an ip addresses. /i says the sub value is
  //   the hostname of a known node and the ip address from
  //   the HostRec structure should be used.  /r says the sub
  //   value is any hostname which should get resolved and it's
  //   ip addr used
  // Currently there is no escaping of the ':', '/', or
  //   anything else for that matter
  char *colon_ptr = strchr(sub_name, ':');
  char *slash_ptr = strchr(sub_name, '/');

  // If the we have a name of the form:
  //   aaa/bbbb:cccc
  // ignore the slash because it's not trailing the name
  if (colon_ptr && slash_ptr && slash_ptr < colon_ptr) {
    slash_ptr = NULL;
  }

  if (slash_ptr) {
    *slash_ptr = '\0';
    slash_ptr++;
  }

  if (colon_ptr) {
    *colon_ptr = '\0';
    InstanceRecord *irec = find_instance_rec(sub_name);

    if (irec == NULL) {
      if (output_warnings) {
        TE_Warning("subsitution %s:%s failed - no such instance", sub_name, colon_ptr + 1);
      }
      return -1;
    } else {
      const char *my_sub = irec->get_port_binding(colon_ptr + 1);

      if (my_sub == NULL) {
        if (output_warnings) {
          TE_Warning("subsitution %s:%s failed - no such port binding", sub_name, colon_ptr + 1);
        }
        return -1;
      } else {
        int len = strlen(my_sub);
        return output_substitution(output_buffer, slash_ptr, sub_name, my_sub, len);
      }
    }
  } else {
    // Straight substituion - consult our hash table
    InkHashTableValue val = NULL;
    int r = ink_hash_table_lookup(substitution_hash, sub_name, &val);

    if (r == 0 || val == NULL) {
      if (output_warnings) {
        TE_Warning("subsitution %s failed", sub_name);
      }
      return -1;
    } else {
      int len = strlen((const char *) val);
      return output_substitution(output_buffer, slash_ptr, sub_name, (const char *) val, len);
    }
  }
}


int
do_substitutions(const char *src, int len, sio_buffer * output, int *errors_ptr)
{

  if (len < 0) {
    len = strlen(src);
  }

  int subs_done = 0;
  int errors = 0;
  const char *current = src;
  int left = len;

  while (left > 0) {
    int consume = 0;
    const char *sub = (const char *) memchr(current, '%', left);

    if (sub) {
      if (left > 4) {
        if (*(sub + 1) == '%' && *(sub + 2) == '(') {
          const char *sub_end = (const char *) memchr(sub + 3, ')', left - 3);

          // Add all text up to this substitution
          output->fill(current, sub - current);

          bool sub_failed = false;

          if (sub_end == NULL) {
            TE_Warning("unended substituion");
            sub_failed = true;
            errors++;
          } else if (do_single_substitution(sub + 3, sub_end, output) < 0) {
            sub_failed = true;
            errors++;
          } else {
            subs_done++;
          }

          // go past the closing ')'
          consume = (sub_end - current) + 1;

          if (sub_failed == true) {
            // just keep going as if nothing happend
            output->fill(sub, (sub_end - sub) + 1);
          }


          left -= consume;
          current += consume;
          ink_debug_assert(left >= 0);
          continue;
        }
      }
      // not really substitution
      consume = (sub - current) + 1;
      output->fill(current, consume);
      left -= consume;
      current += consume;
      ink_debug_assert(left >= 0);
      continue;
    }
    // nothing resembling a substitution - use up the rest
    //   of the data
    output->fill(current, left);
    left = 0;
    current += left;
  }

  if (errors_ptr) {
    *errors_ptr = errors;
  }

  return subs_done;
}

int
do_subs_and_replace(char **src, int *errors)
{

  sio_buffer sub_buffer;

  int r = do_substitutions(*src, -1, &sub_buffer, errors);

  if (r > 0) {
    free(*src);
    int new_config_len = sub_buffer.read_avail();
    *src = (char *) malloc(new_config_len + 1);
    memcpy(*src, sub_buffer.start(), new_config_len);
    (*src)[new_config_len] = '\0';
  }

  return r;
}

void
add_def(const char *name, char *value)
{

  InkHashTableValue hash_value;
  int r = ink_hash_table_lookup(substitution_hash, (char *) name, &hash_value);

  if (r != 0) {
    free(hash_value);
    ink_hash_table_delete(substitution_hash, (char *) name);
  }

  Debug("defs", "Adding pair %s => %s", name, value);
  ink_hash_table_insert(substitution_hash, (char *) name, value);
}

void
process_cmd_line_defs()
{

  char decode_buf[2048];
  const char *startp = defs_add;
  const char *lastp = NULL;
  int left = strlen(defs_add);

  if (left == 0) {
    return;
  }

  while (left > 0) {
    int dec_bytes = raf_decode(startp, left, decode_buf, 2047, &lastp);
    left -= lastp - startp;
    startp = lastp;

    decode_buf[dec_bytes] = '\0';
    char *equal = strchr(decode_buf, '=');

    if (equal) {
      *equal = '\0';
      add_def(decode_buf, strdup(equal + 1));
    } else {
      TE_Warning("Bad syntax on cmd line defs");
    }

  }
}

void
set_internal_defs()
{

  add_def("log_file", ud_info->log_file);
  add_def("tmp_dir", ud_info->tmp_dir);
}


//  int process_defs_line(sio_buffer* defs_buf, char* line_end) {
//
//  looks for a line of the format  %%define(name)(value)
//    and adds the pair to substitutions hash table if it finds
//    one
int
process_defs_line(sio_buffer * defs_buf, char *line_end, int line_num)
{

  char *line_start = defs_buf->start();

  while (line_start < line_end && isspace(*line_start)) {
    line_start++;
  }

  if (line_start == line_end) {
    return 0;
  }
  // Check for comment
  if (*line_start == '#') {
    return 0;
  }

  if (line_start + 9 < line_end) {
    if (memcmp(line_start, "%%define(", 9) != 0) {
      return 0;
    }
  } else {
    return 0;
  }

  line_start += 9;

  char *name_start = line_start;
  char *name_end = (char *) memchr(line_start, ')', line_end - line_start);

  if (name_end == NULL || name_start == name_end) {
    TE_Warning("Malformed entry at line %d in defs_file %s", line_num, defs_file);
    return 1;
  }
  line_start = name_end;

  line_start += 1;

  if (line_start >= line_end || *line_start != '(') {
    TE_Warning("Malformed entry at line %d in defs_file %s", line_num, defs_file);
  }

  line_start += 1;
  char *value_start = line_start;
  char *value_end = (char *) memchr(line_start, ')', line_end - line_start);

  int name_len = name_end - name_start;
  char *name_buf = (char *) malloc(name_len + 1);
  memcpy(name_buf, name_start, name_len);
  name_buf[name_len] = '\0';

  int value_len = value_end - value_start;
  char *value_buf = (char *) malloc(value_len + 1);
  memcpy(value_buf, value_start, value_len);
  value_buf[value_len] = '\0';

  add_def(name_buf, value_buf);

  // Note: the hash table does not make it's own copy of value buf
  //   so we do not free it but instead to turn it over to the
  //   hash table to manage
  free(name_buf);

  return 0;
}

int
load_defs_file()
{

  char *defs_path;
  if (*defs_file == '/') {
    defs_path = strdup(defs_file);
  } else {
    int len = strlen(defs_dir) + 1 + strlen(defs_file) + 1;
    defs_path = (char *) malloc(len);
    sprintf(defs_path, "%s/%s", defs_dir, defs_file);
  }
  FreeOnDestruct freer(defs_path);

  int defs_fd;
  do {
    defs_fd = open(defs_path, O_RDONLY);
  } while (defs_fd < 0 && errno == EINTR);

  if (defs_fd < 0) {
    TE_Error("Could not open defs file %s : %s", defs_path, strerror(errno));
    return -1;
  }

  sio_buffer defs_buf;
  int line_number = 1;

  int r;
  do {

    if (defs_fd >= 0) {
      int wavail = defs_buf.expand_to(SIZE_32K);

      do {
        r = read(defs_fd, defs_buf.end(), wavail);
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        TE_Error("read from defs file failed : %s", strerror(errno));
        return -1;
      } else if (r == 0) {
        // Reached end of file
        close(defs_fd);
        defs_fd = -1;
      } else {
        defs_buf.fill(r);
      }
    }

    char *line_end;
    while ((line_end = defs_buf.memchr('\n'))) {
      process_defs_line(&defs_buf, line_end, line_number);
      defs_buf.consume((line_end - defs_buf.start()) + 1);
      line_number++;
    }

    if (defs_fd<0 && defs_buf.read_avail()> 0) {
      // We've got an incomplete last line but
      // process anyway
      char *line_end = defs_buf.start() + defs_buf.read_avail();
      process_defs_line(&defs_buf, line_end, line_number);
      defs_buf.consume(defs_buf.read_avail());
    }

  } while (defs_fd >= 0);

  ink_debug_assert(defs_buf.read_avail() == 0);

  return 0;
}

void
my_test_script()
{

  char *create_args[] = { "package", "mtest", NULL };

  pm_create_instance("jtest1", "localhost", create_args);
  pm_start_instance("jtest1", NULL);
  sleep(10);
  pm_stop_instance("jtest1", NULL);
  pm_destroy_instance("jtest1", NULL);
}

int
read_log_parser_results(int fd, TestResult * results)
{

  sio_buffer parse_results;

  int timeout_ms = 60000;
  const char *result_msg = read_until(fd, &parse_results, '\n', &timeout_ms);

  if (result_msg) {
    TE_Error("Failed to read results from log parser : %s", result_msg);
    return -1;
  }

  char *new_line = parse_results.memchr('\n');
  *new_line = '\0';

  int errors;
  int warnings;
  int r = sscanf(parse_results.start(),
                 "#### %d Errors; %d  Warnings ####", &errors, &warnings);

  if (r != 2) {
    TE_Error("Malformed results from log parser : %s", parse_results.start());
    return -1;
  } else {
    Debug("parse", "[read_log_parser_results] %s", parse_results.start());
    Debug("parse", "Results: %d errors, %d warnings", errors, warnings);
    results->errors = errors;
    results->warnings = warnings;
  }

  return 0;

}

int
run_log_parser(TestResult * results, const char *output_file, const char *test_case_name, bool html_output)
{

  int len = strlen(log_parser_dir) + 1 + strlen(log_parser_bin) + 1;
  char *parser_path = (char *) malloc(len);
  sprintf(parser_path, "%s/%s", log_parser_dir, log_parser_bin);

  Debug("parser", "[run_log_parser] parse_results %d, outfile %s, html %d",
        (results == NULL) ? 1 : 0, output_file, (int) html_output);

  int r;
  do {
    r = access(parser_path, X_OK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    TE_Error("Can not execute log parser %s : %s", parser_path, strerror(errno));
    free(parser_path);
    return -1;
  } else {
    free(parser_path);
    parser_path = NULL;
  }

  bool pipe_stdout = false;
  int pipe_array[2];
  if (results != NULL) {
    pipe_stdout = true;

    do {
      r = pipe(pipe_array);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      TE_Error("Can create pipe to log parser : %s", strerror(errno));
      pipe_stdout = false;
    }
  }

  pid_t parser_pid = fork();
  if (parser_pid < 0) {
    TE_Error("Can not fork log parser : %s", strerror(errno));
    return -1;
  } else if (parser_pid == 0) {
    /* Child */
    chdir(log_parser_dir);

    // Set up our stdout pipe
    if (pipe_stdout) {
      close(pipe_array[0]);
      dup2(pipe_array[1], 1);
      close(pipe_array[1]);
    }


    int i;
    int next_arg = 0;
    const char *args_array[5];
    for (i = 0; i < 5; i++) {
      args_array[i] = NULL;
    }

    if (output_file != NULL) {
      args_array[next_arg] = "-out";
      args_array[next_arg + 1] = output_file;
      next_arg += 2;
    }

    if (html_output) {
      args_array[next_arg] = "-html";
      next_arg++;
    }

    if (test_case_name) {
      args_array[next_arg] = "-testname";
      args_array[next_arg + 1] = test_case_name;
      next_arg += 2;
    }
    // FIX - use fd limit
    for (i = 3; i < 1024; i++) {
      close(i);
    }

    r = execl(log_parser_bin, log_parser_bin, "-in", ud_info->log_file,
              args_array[0], args_array[1], args_array[2], args_array[3], args_array[4], NULL);

    if (r < 0) {
      TE_Fatal("Can not exec log parser : %s", strerror(errno));
    }
  } else {
    /* Parent */
    if (pipe_stdout) {
      close(pipe_array[1]);
      read_log_parser_results(pipe_array[0], results);
      close(pipe_array[0]);
    }

    int exit_status;
    reap_and_kill_child(parser_pid, &exit_status);
  }

  return 0;
}

int
start_log_file()
{

  int fd;
  do {
    fd = open(ud_info->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    TE_Fatal("Could not open local log file %s : %s", ud_info->log_file, strerror(errno));
    return -1;
  } else {
    log_file_fd = fd;
    return 0;
  }
}

void
stop_log_file()
{
  if (log_file_fd >= 0) {
    close(log_file_fd);
    log_file_fd = -1;
  }
}

int
send_log_collator_roll(const char *test_name)
{

  RafCmd request;
  RafCmd response;

  request(0) = strdup("0");
  request(1) = strdup("roll_log");
  request(2) = strdup(test_name);

  unsigned long local_host_ip = inet_addr("127.0.0.1");
  int fd = SIO::make_client(local_host_ip, log_collator_port);

  if (fd < 0) {
    TE_Error("failed contact log collator for roll log file");
    return -1;
  }

  int r = do_raf(fd, &request, &response);
  close(fd);

  if (response.length() < 2) {
    TE_Error("log roll failed : no response from log_collator");
    return -1;
  } else {
    const char *raf_result_code = response[1];

    if (raf_result_code && raf_result_code[0] == '0' && raf_result_code[1] == '\0') {
      return 0;
    } else {
      TE_Error("log roll failed : %s", (response.length() >= 3) ? response[2] : "no_err_msg");
      return -1;
    }
  }
}

int
roll_log_file(const char *test_name)
{

  if (log_collator_fd >= 0) {
    return send_log_collator_roll(test_name);
  } else {
    // Need to implement no collator roll
    ink_release_assert(0);
    return -1;
  }
}


int
start_log_viewer()
{

  const char log_viewer_dir[] = "parsers";
  const char log_viewer_bin[] = "log_viewer.pl";
  const char log_viewer_path[] = "parsers/log_viewer.pl";

  int r;
  do {
    r = access(log_viewer_path, X_OK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    Error("Could not find log_viewer : %s", strerror(errno));
    return -1;
  }

  int pipe_fd[2];
  bool pipe_to_viewer = false;
  if (test_group[0] != '\0') {
    // If we running with test groups, we communicate with
    //  the log view via a pipe to let it know when
    //  we are switching tests
    do {
      r = pipe(pipe_fd);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      TE_Error("Pipe to log viwwer failed: %s", strerror(errno));
    } else {
      pipe_to_viewer = true;
    }
  }

  pid_t new_pid = fork();

  if (new_pid < 0) {
    if (pipe_to_viewer) {
      close(pipe_fd[0]);
      close(pipe_fd[1]);
    }
    TE_Error("Failed to fork log viewer : %s", strerror(errno));
    return -1;
  } else if (new_pid == 0) {
    /* Child */
    r = chdir(log_viewer_dir);

    if (r < 0) {
      Fatal("Could not change to parsers directory : %s", strerror(errno));
    }
    // FIX - use fd limit
    for (int i = 3; i < 1024; i++) {
      if (pipe_to_viewer == false || i != pipe_fd[0]) {
        close(i);
      }
    }

    char num_buf[32];
    const char *arg1 = NULL;
    const char *arg2 = NULL;

    if (pipe_to_viewer) {
      arg1 = "-s";
      sprintf(num_buf, "%d", pipe_fd[0]);
      arg2 = num_buf;
    } else {
      arg1 = ud_info->log_file;
    }

    r = execl(log_viewer_bin, log_viewer_bin, arg1, arg2, NULL);

    if (r < 0) {
      Fatal("exec of log viewer failed : %s", strerror(errno));
    }
  } else {
    /* Parent */
    log_viewer_pid = new_pid;

    if (pipe_to_viewer) {
      close(pipe_fd[0]);
      log_viewer_pipe_fd = pipe_fd[1];
    }
  }

  return 0;
}

int
start_log_collator()
{

  const char log_collate_name[] = "test_log_collate";

  int r;
  do {
    r = access(log_collate_name, X_OK);
  } while (r < 0 && errno == EINTR);

  char *log_collate_path;
  if (r == 0) {
    log_collate_path = strdup(log_collate_name);
  } else {
    char *arch = get_arch_str();
    char *lc = find_local_package(log_collate_name, arch);

    if (lc == NULL) {
      TE_Fatal("Could not locate test_log_collate");
    }

    int len = strlen(package_dir) + 1 + strlen(lc) + 1;
    log_collate_path = (char *) malloc(len);
    sprintf(log_collate_path, "%s/%s", package_dir, lc);
    free(lc);
  }
  Debug("log", "log collator path is %s", log_collate_path);

  log_collator_port = control_port;
  control_port++;

  Debug("log", "log collator port is %d", log_collator_port);

  pid_t new_pid = fork();

  if (new_pid < 0) {
    free(log_collate_path);
    TE_Error("Failed to fork log collator : %s", strerror(errno));
    return -1;
  } else if (new_pid == 0) {
    /* Child */
    char port_str[32];
    sprintf(port_str, "%d", log_collator_port);

    // FIX - use fd limit
    for (int i = 3; i < 1024; i++) {
      close(i);
    }

    r = execl(log_collate_path, log_collate_path, "-p", port_str, "-L", ud_info->log_file, NULL);

    if (r < 0) {
      Fatal("exec of log collator failed : %s", strerror(errno));
    }
  } else {
    /* Parent */
    free(log_collate_path);
    Debug("log", "forked log collator - pid %d", new_pid);
    log_collator_pid = new_pid;

    RafCmd request;
    RafCmd response;

    request(0) = strdup("0");
    request(1) = strdup("isalive");
    unsigned long local_host_ip = inet_addr("127.0.0.1");
    bool succeeded = false;
    int fd;
    for (int i = 0; i < 30; i++) {
      fd = SIO::make_client(local_host_ip, log_collator_port);

      if (fd > 0) {
        int r = do_raf(fd, &request, &response);

        if (r == 0) {
          succeeded = true;
          break;
        } else {
          close(fd);
        }
      }

      Debug("log", "Attempt %d to contact collator failed", i);
      check_and_process_kill_signal();
      safe_sleep(1000);
    }

    if (succeeded == true) {
      Debug("log", "successful isalive raf to log collator");
      request.clear();
      response.clear();
      request(0) = strdup("1");
      request(1) = strdup("log");

      int r = do_raf(fd, &request, &response);

      if (r == 0) {
        log_collator_fd = fd;
        return 0;
      } else {
        close(fd);
      }
    }

    TE_Error("log", "Could not contact log collator");
    kill(log_collator_pid, SIGTERM);
    log_collator_port = -1;
    return -1;

  }

  return -1;
}

int
remove_dir(const char *dir_name)
{

  Debug("rmdir", "Removing dir %s", dir_name);
  pid_t new_pid = fork();

  if (new_pid < 0) {
    TE_Error("[remove_dir] fork failed : %s", strerror(errno));
    return -1;
  } else if (new_pid == 0) {
    /* Child */

    // FIX - use fd limit
    for (int i = 3; i < 1024; i++) {
      close(i);
    }

    int r = execl("/bin/rm", "/bin/rm", "-rf", dir_name, NULL);

    if (r < 0) {
      Fatal("Could not exec /bin/rm : %s", strerror(errno));
      exit(1);
    }

  } else {
    /* Parent */
    int status;
    pid_t reaped = reap_child(new_pid, &status, 60000);

    if (reaped < 0) {
      reaped = reap_and_kill_child(new_pid, &status);
    }

    if (reaped < 0) {
      TE_Error("[remove_dir] could not reap /bin/rm");
    } else if (status != 0) {
      TE_Error("[remove_dir] /bin/rm failed for %s", dir_name);
    }
  }

  return 0;
}

extern "C"
{
  void run_perl(char **argv);
};

void
prep_and_run_perl(const char *test_script_arg, char **script_args_in)
{

  int len = 0;
  len += strlen(perl_args) + 1;

  if (test_script_arg[0] != '/') {
    len += strlen(script_dir) + 1;
  }
  len += strlen(test_script_arg);

  if (script_args_in == NULL) {
    len += 1 + strlen(script_args);
  }

  {
    static char p_env[1000];
    sprintf(p_env, "PERL5LIB=%s", lib_dir);
    putenv(p_env);
  }

  char *argv_str = (char *) malloc(len + 1 + 2);

  sprintf(argv_str, "%s %s%s%s %s",
              perl_args,
              (test_script_arg[0] == '/') ? "" : script_dir,
              (test_script_arg[0] == '/') ? "" : "/", test_script_arg, (script_args_in == NULL) ? script_args : "");

  char **perl_argv = build_argv("test_exec_perl", argv_str);

  if (script_args_in) {
    perl_argv = append_argv(perl_argv, script_args_in);
  }

  free(argv_str);
  argv_str = NULL;

  set_cur_script_path(script_dir, test_script_arg);

  TE_Status("Running test script %s", test_script_arg);

  if (*build_id != '\0') {
    TE_Status("Build Id: %s", build_id);
  }

  run_perl(perl_argv);
  TE_Status("Completed test script %s", test_script_arg);

  destroy_argv(perl_argv);
}

void
prep_and_run_log_parser(TestResult * tr, const char *test_case_name)
{

  if (save_results) {
    tr->build_output_file_name(test_case_name, "html");
    run_log_parser(tr, tr->output_file, test_case_name, true);
  } else {
    run_log_parser(NULL, NULL, NULL, false);
  }
}

void
find_and_run_tests()
{

  // Start by loading the test group file
  char test_group_path[1024];
  snprintf(test_group_path, 1023, "%s/%s", defs_dir, test_group_file);
  test_group_path[1023] = '\0';

  int r = load_group_file(test_group_path);

  if (test_group[0] != '\0') {
    if (r != 0) {
      TE_Error("Can not run test_group '%s' since loading the group file failed", test_group);
      return;
    }

    test_group_iter *giter = test_group_start(test_group);

    if (giter == NULL) {
      TE_Error("test_group '%s' unknown", test_group);
      return;
    }

    const test_case *cur_test_case;
    while ((cur_test_case = test_group_next(giter)) != NULL) {
      Debug("group", "Running test_case %s from group", cur_test_case->name);

      notify_viewer_new_test(cur_test_case->name);

      TestResult *test_result = run_results->new_result();

      test_result->start(cur_test_case->name);
      prep_and_run_perl(cur_test_case->test_case_elements[0], (char **) cur_test_case->test_case_elements + 1);
      test_result->finish();

      stop_and_destroy_all_instances();

      prep_and_run_log_parser(test_result, cur_test_case->name);
      roll_log_file(cur_test_case->name);
      notify_viewer_log_roll(cur_test_case->name);
    }
    test_group_finish(giter);
    notify_viewer_done();
  } else {

    // Just running a single test case
    test_case my_case;
    TestResult *test_result = run_results->new_result();

    test_result->start(test_script);

    if (lookup_test_case(test_script, &my_case)) {
      prep_and_run_perl(my_case.test_case_elements[0], (char **) my_case.test_case_elements + 1);
    } else {
      prep_and_run_perl(test_script, NULL);
    }

    test_result->finish();
    prep_and_run_log_parser(test_result, test_script);
  }
}


void
write_message_to_log_viewer(sio_buffer * write_buf)
{

  int timeout_ms = 20000;
  const char *r_msg = write_buffer(log_viewer_pipe_fd,
                                   write_buf, &timeout_ms);

  if (r_msg != NULL) {
    TE_Warning("Failed to write message to log_viewer : %s", r_msg);
    close(log_viewer_pipe_fd);
    log_viewer_pipe_fd = -1;
  }
}

void
notify_viewer_new_test(const char *test_name)
{

  if (log_viewer_pipe_fd >= 0) {
    Debug("log_view", "Sending start msg for %s", test_name);
    sio_buffer write_buf;
    write_buf.fill("start", 5);
    write_buf.fill(" ", 1);
    write_buf.fill(test_name, strlen(test_name));
    write_buf.fill(" ", 1);
    write_buf.fill(ud_info->log_file, strlen(ud_info->log_file));
    write_buf.fill("\n", 1);

    write_message_to_log_viewer(&write_buf);

  }
}

void
notify_viewer_log_roll(const char *test_name)
{

  if (log_viewer_pipe_fd >= 0) {
    Debug("log_view", "Sending roll msg for %s", test_name);
    sio_buffer write_buf;
    write_buf.fill("roll", 4);
    write_buf.fill(" ", 1);
    write_buf.fill(test_name, strlen(test_name));
    write_buf.fill("\n", 1);

    write_message_to_log_viewer(&write_buf);

  }
}

void
notify_viewer_done()
{

  if (log_viewer_pipe_fd >= 0) {
    Debug("log_view", "Sending done msg");
    sio_buffer write_buf;
    write_buf.fill("done", 4);
    write_buf.fill("\n", 1);

    write_message_to_log_viewer(&write_buf);

    close(log_viewer_pipe_fd);
    log_viewer_pipe_fd = -1;
  }
}

UserDirInfo *
setup_user_and_dir_info()
{
  uid_t my_uid = getuid();

  if (my_uid < 0) {
    Fatal("Unable to determine uid : %s", strerror(errno));
    return NULL;
  }

  struct passwd *pent = getpwuid(my_uid);

  if (pent == NULL) {
    Fatal("Unable to find user entry : %s", strerror(errno));
    return NULL;
  }

  UserDirInfo *ur = new UserDirInfo;

  ur->username = strdup(pent->pw_name);
  ur->shell = strdup(pent->pw_shell);

  int len = strlen(ur->username) + strlen(test_uniquer) + 1;
  ur->test_stuff_dir = (char *) malloc(len);
  sprintf(ur->test_stuff_dir, "%s%s", ur->username, test_uniquer);

  ur->test_stuff_path = strdup(stuff_path);

  len = strlen(stuff_path) + 1 + strlen(ur->test_stuff_dir) + 1;
  ur->test_stuff_path_and_dir = (char *) malloc(len);
  sprintf(ur->test_stuff_path_and_dir, "%s/%s", stuff_path, ur->test_stuff_dir);

  len = strlen(ur->test_stuff_path_and_dir) + 1 + 3 + 1;
  ur->tmp_dir = (char *) malloc(len);
  sprintf(ur->tmp_dir, "%s/tmp", ur->test_stuff_path_and_dir);

  ur->package_dir = strdup(package_dir);

  ur->hostname = (char *) malloc(256);
  strcpy(ur->hostname, "UNKNOWN");
  int r = gethostname(ur->hostname, 255);
  ur->hostname[255] = '\0';

  // Unqualifed hostnames will break if all the machines
  //   aren't in the same DNS domain and subdomain.  Try
  //   to use an ip address instead
  if (strchr(ur->hostname, '.') == NULL) {
    bool qualify_success = false;
    struct hostent *he = gethostbyname(ur->hostname);
    if (he != NULL) {

      struct in_addr my_addr;
      memcpy(&my_addr, *he->h_addr_list, sizeof(struct in_addr));
      char *tmp_str = inet_ntoa(my_addr);

      if (tmp_str) {
        ur->ip_str = strdup(tmp_str);
        qualify_success = true;
      }
    }

    if (qualify_success == false) {
      Warning("Could not qualify hostname '%s'", ur->hostname);
    }
  }
  Debug("setup", "my hostname is %s", ur->hostname);

  return ur;
}

void
setup_port_stuff(UserDirInfo * ur)
{

  ur->port = control_port;

  if (log_collator_port > 0) {
    ur->log_collator_arg = (char *) malloc(strlen(ur->hostname) + 32 + 1);
    sprintf(ur->log_collator_arg, "%s:%d", (ur->ip_str) ? ur->ip_str : ur->hostname, log_collator_port);
  }
}

static int sigchld_received = 0;
static int children_reaped = 0;

extern "C"
{
  void sigchld_handler(int sig)
  {

    ink_debug_assert(sig == SIGCHLD);

    int reap_complete = 0;
    if (log_viewer_pid > 0)
    {
      int status;
      int r = waitpid(log_viewer_pid, &status, WNOHANG);

      if (r > 0)
      {
        reap_complete = 1;

        if (log_viewer_pipe_fd >= 0)
        {
          close(log_viewer_pipe_fd);
          log_viewer_pipe_fd = -1;
        }
      }
    }

    if (reap_complete == 0) {
      sigchld_received++;
    }
  }

  void sigalrm_handler(int sig)
  {
    ink_debug_assert(sig == SIGALRM);
    /* Do Nothing */
  }

  void exit_signal_handler(int sig)
  {
    kill_sig_received = sig;
  }
}


void
setup_signals()
{

  struct sigaction sig_h;
  memset(&sig_h, 0, sizeof(sig_h));
  sigemptyset(&sig_h.sa_mask);

  // Ignore Sigpipe
  sig_h.sa_handler = SIG_IGN;
  sig_h.sa_flags = 0;
  sigaction(SIGPIPE, &sig_h, NULL);

  // Catch child for reaping
  sig_h.sa_handler = sigchld_handler;
  sig_h.sa_flags = SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sig_h, NULL);

  // Catch sig alarm
  sig_h.sa_handler = sigalrm_handler;
  sig_h.sa_flags = 0;
  sigaction(SIGALRM, &sig_h, NULL);

  // Trap exit signals so we can terminate children
  int exit_sigs[3] = { SIGINT, SIGQUIT, SIGTERM };
  sig_h.sa_handler = exit_signal_handler;
  sig_h.sa_flags = 0;

  for (int i = 0; i < 3; i++) {
    sigaction(exit_sigs[i], &sig_h, NULL);
  }

}

pid_t
reap_and_kill_child(pid_t child_pid, int *exit_status)
{

  pid_t reaped_pid = reap_child(child_pid, exit_status, 30 * 1000);

  int sigs[2] = { SIGTERM, SIGKILL };

  if (reaped_pid == 0) {
    for (int i = 0; i < 2; i++) {
      int r = kill(child_pid, sigs[i]);

      reaped_pid = reap_child(child_pid, exit_status, 30 * 1000);

      // If the kill fails or we got a child process
      //    we are done
      if (r<0 && reaped_pid> 0) {
        break;
      }
    }
  }

  if (reaped_pid > 0) {
    Debug("child", "reaped child pid %d; status %d", reaped_pid, exit_status);
  } else {
    Debug("child", "failed to reap child pid %d", reaped_pid);
  }

  return reaped_pid;
}


pid_t
reap_child(pid_t pid, int *status, int timeout_ms)
{

  //
  // We need to wait for the sigchild so we can reap
  //
  // First we need to block sigchild & sigalarm to prevent
  //   race conditions of the sigchild coming in between
  //   checking for the child and going to sleep to for
  //   the sigchild signal to wake us up
  //
  sigset_t old_mask;
  sigset_t block_sigs;
  sigemptyset(&block_sigs);
  sigaddset(&block_sigs, SIGCHLD);
  sigaddset(&block_sigs, SIGALRM);

  int r = sigprocmask(SIG_BLOCK, &block_sigs, &old_mask);
  ink_debug_assert(r == 0);

  pid_t reaped_pid = waitpid(pid, status, WNOHANG);


  if (reaped_pid > 0) {
    // We've got our child
    children_reaped++;
  } else if (reaped_pid == 0) {
    // Child not available yet.  Wait for the timeout

    if (timeout_ms > 0) {
      itimerval timer_val;
      timer_val.it_value.tv_sec = timeout_ms / 1000;
      timer_val.it_value.tv_usec = (timeout_ms % 1000) * 1000;
      timer_val.it_interval.tv_sec = 0;
      timer_val.it_interval.tv_usec = 0;

      r = setitimer(ITIMER_REAL, &timer_val, NULL);
      ink_debug_assert(r == 0);

      sigsuspend(&old_mask);

      // Clear the itimer
      timer_val.it_value.tv_sec = 0;
      timer_val.it_value.tv_usec = 0;
      r = setitimer(ITIMER_REAL, &timer_val, NULL);
      ink_debug_assert(r == 0);

      // FIX - log collator could die while we are waitng for
      //   remote install process
      reaped_pid = waitpid(pid, status, WNOHANG);

      if (reaped_pid >= 0) {
        children_reaped++;
      }
    }
  }

  r = sigprocmask(SIG_UNBLOCK, &block_sigs, NULL);
  ink_debug_assert(r == 0);

  return reaped_pid;
}

void
init_dir_stuff()
{

  if (strcmp(stuff_path, "") == 0 || strcmp(stuff_path, ".") == 0 || strcmp(stuff_path, "./") == 0) {
    char *tmp = getcwd(stuff_path, 1024);
    if (tmp == NULL) {
      Fatal("getcwd failed : %s", strerror(errno));
    }
  }

  int r;
  struct stat stat_info;
  do {
    r = stat(stuff_path, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    TE_Fatal("Unable to access to stuff path %s : %s", stuff_path, strerror(errno));
  } else if (stat_info.st_mode & S_IFDIR == 0) {
    TE_Error("Unable to access to stuff path %s is not a directory", stuff_path);
  }

  do {
    r = access(stuff_path, R_OK | W_OK | X_OK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (errno == EACCES) {
      TE_Fatal("Insufficient access permissions for stuff dir %s", stuff_path);
    } else {
      TE_Fatal("Unable to check access permissions for stuff dir %s : %s", stuff_path, strerror(errno));
    }
  }

  ud_info = setup_user_and_dir_info();

  int error_code;
  const char *rmsg = create_or_verify_dir(ud_info->test_stuff_path_and_dir, &error_code);
  if (rmsg) {
    TE_Fatal("no stuff dir : %s : %s %s", ud_info->test_stuff_path_and_dir, rmsg, strerror(error_code));
  }

  int len = strlen(ud_info->test_stuff_path_and_dir) + 1 + 3 + 1;
  ud_info->log_dir = (char *) malloc(len);
  sprintf(ud_info->log_dir, "%s/log", ud_info->test_stuff_path_and_dir);

  rmsg = create_or_verify_dir(ud_info->log_dir, &error_code);
  if (rmsg) {
    Fatal("no slog dir : %s : %s %s", ud_info->log_dir, rmsg, strerror(error_code));
  }


  do {
    r = access(ud_info->tmp_dir, R_OK | W_OK | X_OK);
  } while (r < 0 && errno == EINTR);

  if (r == 0) {
    remove_dir(ud_info->tmp_dir);
  }

  rmsg = create_or_verify_dir(ud_info->tmp_dir, &error_code);
  if (rmsg) {
    TE_Fatal("no tmp dir : %s : %s %s", ud_info->tmp_dir, rmsg, strerror(error_code));
  }

  if (log_file[0] == '/') {
    ud_info->log_file = strdup(log_file);
  } else {
    len = strlen(ud_info->log_dir) + 1 + strlen(log_file) + 1;
    ud_info->log_file = (char *) malloc(len);
    sprintf(ud_info->log_file, "%s/%s", ud_info->log_dir, log_file);
  }
}

void
check_and_process_kill_signal()
{
  if (kill_sig_received > 0 && kill_in_progress == 0) {
    kill_in_progress = 1;
    process_kill_signal();
  }
}

void
process_kill_signal()
{
  Error("test_exec received kill signal %d; cleaning up", kill_sig_received);
  stop_and_destroy_all_instances();

  if (!manual_startup) {
    shutdown_proc_managers();
    shutdown_log_collator();
  } else {
    stop_log_file();
  }

  exit(1);
}

int
main(int argc, char **argv)
{

  process_args(argument_descriptions, n_argument_descriptions, argv);

  if (show_version) {
    printf("test_exec: %s", rcs_full_id);
    exit(0);
  }


  diags = new Diags(error_tags, action_tags);
  diags->config.outputs[DL_Diag].to_stdout = true;
  diags->show_location = 0;

  if (*error_tags) {
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  }

  if (*action_tags) {
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);
  }

  setup_signals();
  init_dir_stuff();

  substitution_hash = ink_hash_table_create(InkHashTableKeyType_String);
  load_defs_file();
  process_cmd_line_defs();
  set_internal_defs();

  Debug("main", "Control Port is %d", control_port);
  if (!manual_startup) {
    int r = start_log_collator();
    if (r == 0) {
      Debug("log", "log collator started up; pid %d, port %d", log_collator_pid, log_collator_port);

      if (launch_log_viewer) {
        start_log_viewer();
      }
    } else {
      TE_Fatal("failed to startup up log collator");
    }
  } else {
    start_log_file();
  }

  setup_port_stuff(ud_info);
  TE_Status("test_exec v%s running", rcs_id);

  run_results = new TestRunResults();
  run_results->start((test_group[0] == '\0') ? test_script : test_group, ud_info->username, build_id);

  find_and_run_tests();

  if (!manual_startup) {
    shutdown_proc_managers();
    shutdown_log_collator();
  } else {
    stop_log_file();
  }

  run_results->cleanup_results((post_to_tinderbox ? true : false));
  return 0;
}
