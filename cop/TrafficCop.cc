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

#include "libts.h"
#include "I_Layout.h"
#include "I_Version.h"
#include "mgmtapi.h"
#include "ClusterCom.h"

#if defined(linux)
#include "sys/utsname.h"
#include "ink_killall.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

union semun
{
  int val;                      /* value for SETVAL */
  struct semid_ds *buf;         /* buffer for IPC_STAT, IPC_SET */
  unsigned short int *array;    /* array for GETALL, SETALL */
  struct seminfo *__buf;        /* buffer for IPC_INFO */
};
#endif  // linux check

// For debugging, turn this on.
// #define TRACE_LOG_COP 1

#define OPTIONS_MAX     32
#define OPTIONS_LEN_MAX 1024
#define MAX_PROXY_PORTS 48

#ifndef WAIT_ANY
#define WAIT_ANY (pid_t) -1
#endif // !WAIT_ANY

#define COP_FATAL    LOG_ALERT
#define COP_WARNING  LOG_ERR
#define COP_DEBUG    LOG_DEBUG

static const char *root_dir;
static const char *runtime_dir;
static const char *config_dir;
static char config_file[PATH_MAX];

static char cop_lockfile[PATH_MAX];
static char manager_lockfile[PATH_MAX];
static char server_lockfile[PATH_MAX];

#if defined(linux)
static bool check_memory_required = false;
#endif
static int check_memory_min_swapfree_kb = 10240;
static int check_memory_min_memfree_kb = 10240;

static int syslog_facility = LOG_DAEMON;
static char syslog_fac_str[PATH_MAX];

static int killsig = SIGKILL;
static int coresig = 0;

static char admin_user[80] = TS_PKGSYSUSER;
static char manager_binary[PATH_MAX] = "traffic_manager";
static char server_binary[PATH_MAX] = "traffic_server";
static char manager_options[OPTIONS_LEN_MAX] = "";

static char log_file[PATH_MAX] = "traffic.out";
static char bin_path[PATH_MAX];

static int autoconf_port = 8083;
static int rs_port = 8088;
static MgmtClusterType cluster_type = NO_CLUSTER;
static int http_backdoor_port = 8084;

static int manager_failures = 0;
static int server_failures = 0;
static int server_not_found = 0;


static const int sleep_time = 10;       // 10 sec
static const int manager_timeout = 3 * 60;      //  3 min
static const int server_timeout = 3 * 60;       //  3 min

// traffic_manager flap detection
#define MANAGER_FLAP_DETECTION 1
#if defined(MANAGER_FLAP_DETECTION)
#define MANAGER_MAX_FLAP_COUNT 3        // if flap this many times, give up for a while
#define MANAGER_FLAP_INTERVAL_MSEC 60000        // if x number of flaps happen in this interval, declare flapping
#define MANAGER_FLAP_RETRY_MSEC 60000   // if flapping, don't try to restart until after this retry duration
static bool manager_flapping = false;   // is the manager flapping?
static int manager_flap_count = 0;      // how many times has the manager flapped?
static ink_hrtime manager_flap_interval_start_time = 0; // first time we attempted to start the manager in past little while)
static ink_hrtime manager_flap_retry_start_time = 0;    // first time we attempted to start the manager in past little while)
#endif

// transient syscall error timeout
#define TRANSIENT_ERROR_WAIT_MS 500

static const int kill_timeout = 1 * 60; //  1 min


static int child_pid = 0;
static int child_status = 0;
static int sem_id = -1;

AppVersionInfo appVersionInfo;
static InkHashTable *configTable = NULL;


static void
cop_log(int priority, const char *format, ...)
{
  va_list args;
  char buffer[8192];
#ifdef TRACE_LOG_COP
  static FILE *trace_file = NULL;
  struct timeval now;
  double now_f;
#endif

  va_start(args, format);

#ifdef TRACE_LOG_COP
  if (!trace_file)
    trace_file = fopen("/tmp/traffic_cop.trace", "w");
  if (trace_file) {
    gettimeofday(&now, NULL);
    now_f = now.tv_sec + now.tv_usec / 1000000.0f;
    switch (priority) {
    case COP_DEBUG:
      fprintf(trace_file, "<%.4f> [DEBUG]: ", now_f);
      break;
    case COP_WARNING:
      fprintf(trace_file, "<%.4f> [WARNING]: ", now_f);
      break;
    case COP_FATAL:
      fprintf(trace_file, "<%.4f> [FATAL]: ", now_f);
      break;
    default:
      fprintf(trace_file, "<%.4f> [unknown]: ", now_f);
      break;
    }

    vfprintf(trace_file, format, args);
    fflush(trace_file);
  }
#endif

  vsprintf(buffer, format, args);
  syslog(priority, "%s", buffer);

  va_end(args);
}

void
chown_file_to_user(const char *file, const char *user)
{
  struct passwd *pwd = NULL;

  if (user && *user) {
    if (*user == '#') {
      int uid = atoi(user + 1);
      if (uid == -1) {
        // XXX: Can this call hapen after setuid?
        uid = (int)geteuid();
      }
      pwd = getpwuid((uid_t)uid);
    }
    else {
      pwd = getpwnam(user);
    }
  }
  if (pwd) {
    if (chown(file, pwd->pw_uid, pwd->pw_gid) < 0) {
      //cop_log(COP_FATAL, "cop couldn't chown the  file: %s\n", file);
    }
  } else {
    cop_log(COP_FATAL, "can't get passwd entry for the admin user\n");
  }
}
static void
sig_child(int signum)
{
  NOWARN_UNUSED(signum);
  int pid = 0;
  int status = 0;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering sig_child(%d)\n", signum);
#endif
  for (;;) {
    pid = waitpid(WAIT_ANY, &status, WNOHANG);

    if (pid <= 0) {
      break;
    }
    // TSqa03086 - We can not log the child status signal from
    //   the signal handler since syslog can deadlock.  Record
    //   the pid and the status in a global for logging
    //   next time through the event loop.  We will occasionally
    //   lose some information if we get two sig childs in rapid
    //   succession
    child_pid = pid;
    child_status = status;
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving sig_child(%d)\n", signum);
#endif
}

// TODO: Use positive instead negative checks
//       This should be #if defined(solaris)
static void
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
sig_fatal(int signum, siginfo_t * t, void *c)
#else
sig_fatal(int signum)
#endif
{
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering sig_fatal(%d)\n", signum);
#endif
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  if (t) {
    if (t->si_code <= 0) {
      cop_log(COP_FATAL, "cop received fatal user signal [%d] from"
              " pid [%d] uid [%d]\n", signum, t->si_pid, t->si_uid);
    } else {
      cop_log(COP_FATAL, "cop received fatal kernel signal [%d], " "reason [%d]\n", signum, t->si_code);
    }
  } else {
#endif
    cop_log(COP_FATAL, "cop received fatal signal [%d]\n", signum);
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  }
#endif
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving sig_fatal(%d)\n", signum);
#endif
  abort();
}

static void
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
sig_alarm_warn(int signum, siginfo_t * t, void *c)
#else
sig_alarm_warn(int signum)
#endif
{
  NOWARN_UNUSED(signum);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering sig_alarm_warn(%d)\n", signum);
#endif
  cop_log(COP_WARNING, "unable to kill traffic_server for the last" " %d seconds\n", kill_timeout);

  // Set us up for another alarm
  alarm(kill_timeout);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving sig_alarm_warn(%d)\n", signum);
#endif
}

static void
sig_ignore(int signum)
{
  NOWARN_UNUSED(signum);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering sig_ignore(%d)\n", signum);
#endif
  // No code here yet...
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving sig_ignore(%d)\n", signum);
#endif
}

static void
set_alarm_death()
{
  struct sigaction action;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering set_alarm_death()\n");
#endif
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  action.sa_handler = NULL;
  action.sa_sigaction = sig_fatal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_handler = sig_fatal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
#endif

  sigaction(SIGALRM, &action, NULL);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving set_alarm_death()\n");
#endif
}

static void
set_alarm_warn()
{
  struct sigaction action;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering set_alarm_warn()\n");
#endif
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  action.sa_handler = NULL;
  action.sa_sigaction = sig_alarm_warn;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_handler = sig_alarm_warn;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
#endif

  sigaction(SIGALRM, &action, NULL);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving set_alarm_warn()\n");
#endif

}

static void
process_syslog_config(void)
{
  int new_fac;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering process_syslog_config()\n");
#endif
  new_fac = facility_string_to_int(syslog_fac_str);

  if (new_fac >= 0 && new_fac != syslog_facility) {
    closelog();
    openlog("traffic_cop", LOG_PID | LOG_NDELAY | LOG_NOWAIT, new_fac);
    syslog_facility = new_fac;
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving process_syslog_config()\n");
#endif
}

// Paranoia: wrap the process termination call within alarms
//           so that when the killing call doesn't return we
//           will still wake up
// FIX THIS: We don't know what to do on NT yet.
static void
safe_kill(const char *lockfile_name, const char *pname, bool group)
{
  Lockfile lockfile(lockfile_name);
  chown_file_to_user(lockfile_name, admin_user);

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering safe_kill(%s, %s, %d)\n", lockfile_name, pname, group);
#endif
  set_alarm_warn();
  alarm(kill_timeout);

  if (group == true) {
    lockfile.KillGroup(killsig, coresig, pname);
  } else {
    lockfile.Kill(killsig, coresig, pname);
  }
  chown_file_to_user(lockfile_name, admin_user);

  alarm(0);
  set_alarm_death();
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving safe_kill(%s, %s, %d)\n", lockfile_name, pname, group);
#endif
}


// ink_hrtime milliseconds()
//
// Returns the result of gettimeofday converted to
// one 64bit int
//
static ink_hrtime
milliseconds(void)
{
  struct timeval curTime;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering milliseconds()\n");
#endif
  ink_gethrtimeofday(&curTime, NULL);
  // Make liberal use of casting to ink_hrtime to ensure the
  //  compiler does not truncate our result
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving milliseconds()\n");
#endif
  return ((ink_hrtime) curTime.tv_sec * 1000) + ((ink_hrtime) curTime.tv_usec / 1000);
}

static void
millisleep(int ms)
{
  struct timespec ts;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering millisleep(%d)\n", ms);
#endif
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms - ts.tv_sec * 1000) * 1000 * 1000;
  nanosleep(&ts, NULL);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving millisleep(%d)\n", ms);
#endif
}

static bool
transient_error(int error, int wait_ms)
{
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering transient_error(%d, %d)\n", error, wait_ms);
#endif

  // switch cases originally from UnixNex::accept_error_seriousness()
  switch (error) {
  case EAGAIN:
  case EINTR:
    break;

  case ENFILE:
  case EMFILE:
  case ENOMEM:
#ifdef ENOBUFS
  case ENOBUFS:
#endif
#if !defined(freebsd) && !defined(darwin)
  case ENOSR:
#endif
    if (wait_ms)
      millisleep(wait_ms);
    break;

  default:
#ifdef TRACE_LOG_COP
    cop_log(COP_DEBUG, "Leaving transient_error(%d, %d) --> false\n", error, wait_ms);
#endif
    return false;
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving transient_error(%d, %d) --> true\n", error, wait_ms);
#endif
  return true;
}

static void
build_config_table(FILE * fp)
{
  int i;
  char *p;
  char buffer[4096], varname[1024];

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering build_config_table(%d)\n", fp);
#endif
  if (configTable != NULL) {
    ink_hash_table_destroy_and_free_values(configTable);
  }
  configTable = ink_hash_table_create(InkHashTableKeyType_String);

  while (!feof(fp)) {

    if (!fgets(buffer, 4096, fp)) {
      break;
    }
    // replace newline with null termination
    p = strchr(buffer, '\n');
    if (p) {
      *p = '\0';
    }
    // skip beginning line spaces
    p = buffer;
    while (*p != '\0' && isspace(*p)) {
      p++;
    }

    // skip blank or comment lines
    if (*p == '#' || *p == '\0') {
      continue;
    }
    // skip the first word
    while (*p != '\0' && !isspace(*p)) {
      p++;
    }

    while (*p != '\0' && isspace(*p)) {
      p++;
    }

    for (i = 0; *p != '\0' && !isspace(*p); i++, p++) {
      varname[i] = *p;
    }
    varname[i] = '\0';

    ink_hash_table_insert(configTable, varname, xstrdup(buffer));
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving build_config_table(%d)\n", fp);
#endif
}

static void
read_config_string(const char *str, char *val, size_t val_len)
{
  InkHashTableValue hval;
  char *p, *buf;

  if (!ink_hash_table_lookup(configTable, str, &hval)) {
    goto ConfigStrFatalError;
  }
  buf = (char *) hval;

  p = strstr(buf, str);
  if (!p) {
    goto ConfigStrFatalError;
  }

  p += strlen(str);
  p = strstr(p, "STRING");
  if (!p) {
    goto ConfigStrFatalError;
  }

  p += sizeof("STRING") - 1;
  while (*p && isspace(*p)) {
    p += 1;
  }

  ink_strncpy(val, p, val_len);
  return;

ConfigStrFatalError:
  cop_log(COP_FATAL, "could not find variable string %s in records.config\n", str);
  exit(1);
}

static void
read_config_int(const char *str, int *val, bool miss_ok = false)
{
  InkHashTableValue hval;
  char *p, *buf;

  if (!ink_hash_table_lookup(configTable, str, &hval)) {
    if (miss_ok) {
      return;
    } else {
      goto ConfigIntFatalError;
    }
  }
  buf = (char *) hval;

  p = strstr(buf, str);
  if (!p) {
    goto ConfigIntFatalError;
  }

  p += strlen(str);
  p = strstr(p, "INT");
  if (!p) {
    goto ConfigIntFatalError;
  }

  p += sizeof("INT") - 1;
  while (*p && isspace(*p)) {
    p += 1;
  }

  *val = atoi(p);
  return;

ConfigIntFatalError:
  cop_log(COP_FATAL, "could not find variable integer %s in records.config\n", str);
  exit(1);
}


static void
read_config()
{
  FILE *fp;
  struct stat stat_buf;
  static time_t last_mod = 0;
  char log_dir[PATH_MAX];
  char log_filename[PATH_MAX];
  int tmp_int;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering read_config()\n");
#endif
  // coverity[fs_check_call]
  if (stat(config_file, &stat_buf) == -1) {
    cop_log(COP_FATAL, "could not stat \"%s\"\n", config_file);
    exit(1);
  }

  if (stat_buf.st_mtime <= last_mod) {  // no change, no need to re-read
    return;
  } else {
    last_mod = stat_buf.st_mtime;
  }

  // we stat the file to get the mtime and only open a read if it has changed, so disable the toctou coverity check
  // coverity[toctou]
  fp = fopen(config_file, "r");
  if (!fp) {
    cop_log(COP_FATAL, "could not open \"%s\"\n", config_file);
    exit(1);
  }

  build_config_table(fp);
  fclose(fp);

  read_config_string("proxy.config.admin.user_id", admin_user, sizeof(admin_user));
  read_config_string("proxy.config.manager_binary", manager_binary, sizeof(manager_binary));
  read_config_string("proxy.config.proxy_binary", server_binary, sizeof(server_binary));
  read_config_string("proxy.config.bin_path", bin_path, sizeof(bin_path));
  Layout::get()->relative(bin_path, sizeof(bin_path), bin_path);
  if (access(bin_path, R_OK) == -1) {
    ink_strlcpy(bin_path, Layout::get()->bindir, sizeof(bin_path));
    if (access(bin_path, R_OK) == -1) {
      cop_log(COP_FATAL, "could not access() \"%s\"\n", bin_path);
      cop_log(COP_FATAL, "please set 'proxy.config.bin_path' \n");
    }
  }
  read_config_string("proxy.config.log.logfile_dir", log_dir, sizeof(log_dir));
  Layout::get()->relative(log_dir, sizeof(log_dir), log_dir);
  if (access(log_dir, W_OK) == -1) {
    ink_strlcpy(log_dir, Layout::get()->logdir, sizeof(log_dir));
    if (access(log_dir, W_OK) == -1) {
      cop_log(COP_FATAL, "could not access() \"%s\"\n", log_dir);
      cop_log(COP_FATAL, "please set 'proxy.config.log.logfile_dir' \n");
    }
  }
  read_config_string("proxy.config.output.logfile", log_filename, sizeof(log_filename));
  Layout::relative_to(log_file, sizeof(log_file), log_dir, log_filename);
  read_config_int("proxy.config.process_manager.mgmt_port", &http_backdoor_port);
  read_config_int("proxy.config.admin.autoconf_port", &autoconf_port);
  read_config_int("proxy.config.cluster.rsport", &rs_port);
  read_config_int("proxy.config.lm.sem_id", &sem_id);

  read_config_int("proxy.local.cluster.type", &tmp_int);
  cluster_type = static_cast<MgmtClusterType>(tmp_int);

  read_config_string("proxy.config.syslog_facility", syslog_fac_str, sizeof(syslog_fac_str));
  process_syslog_config();
  read_config_int("proxy.config.cop.core_signal", &coresig);

  read_config_int("proxy.config.cop.linux_min_swapfree_kb", &check_memory_min_swapfree_kb);
  read_config_int("proxy.config.cop.linux_min_memfree_kb", &check_memory_min_memfree_kb);

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving read_config()\n");
#endif
}


static void
spawn_manager()
{
  char prog[PATH_MAX];
  char *options[OPTIONS_MAX];
  char *last;
  char *tok;
  int log_fd;
  int err;
  int key;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering spawn_manager()\n");
#endif
  // Clean up shared memory segments.
  if (sem_id > 0) {
    key = sem_id;
  } else {
    key = 11452;
  }
  for (;; key++) {
    err = semget(key, 1, 0666);
    if (err < 0) {
      break;
    }
#if defined(solaris) || defined(kfreebsd) || defined(unknown)
    err = semctl(err, 1, IPC_RMID);
#else
    union semun dummy_semun;
    memset(&dummy_semun, 0, sizeof(dummy_semun));
    err = semctl(err, 1, IPC_RMID, dummy_semun);
#endif
    if (err < 0) {
      break;
    }
  }

  Layout::relative_to(prog, sizeof(prog), bin_path, manager_binary);
  if (access(prog, R_OK | X_OK) == -1) {
    cop_log(COP_FATAL, "unable to access() manager binary \"%s\" [%d '%s']\n", prog, errno, strerror(errno));
    exit(1);
  }

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "spawn_manager: Launching %s with options '%s'\n",
          prog, manager_options);
#endif
  int i;
  for (i = 0; i < OPTIONS_MAX; i++) {
    options[i] = NULL;
  }
  options[0] = prog;
  i = 1;
  tok = ink_strtok_r(manager_options, " ", &last);
  options[i++] = tok;
  if (tok != NULL) {
    while (i < OPTIONS_MAX && (tok = ink_strtok_r(NULL, " ", &last))) {
      options[i++] = tok;
    }
  }

  // Move any traffic.out that we can not write to, out
  //  of the way (TSqa2232)
  // coverity[fs_check_call]
  if (access(log_file, W_OK) < 0 && errno == EACCES) {
    char old_log_file[PATH_MAX];
    snprintf(old_log_file, sizeof(old_log_file), "%s.old", log_file);
    // coverity[toctou]
    rename(log_file, old_log_file);
    cop_log(COP_WARNING, "rename %s to %s as it is not accessable.\n", log_file, old_log_file);
  }
  // coverity[toctou]
  if ((log_fd = open(log_file, O_WRONLY | O_APPEND | O_CREAT, 0640)) < 0) {
    cop_log(COP_WARNING, "unable to open log file \"%s\" [%d '%s']\n", log_file, errno, strerror(errno));
  }

  err = fork();
  if (err == 0) {
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    err = execv(prog, options);
#ifdef TRACE_LOG_COP
    cop_log(COP_DEBUG, "Somehow execv(%s, options, NULL) failed (%d)!\n", prog, err);
#endif
    exit(1);
  } else if (err == -1) {
    cop_log(COP_FATAL, "unable to fork [%d '%s']\n", errno, strerror(errno));
    exit(1);
  } else {
    close(log_fd);
  }

  manager_failures = 0;
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving spawn_manager()\n");
#endif
}



static int
poll_read(int fd, int timeout)
{
  struct pollfd info;
  int err;

  info.fd = fd;
  info.events = POLLIN;
  info.revents = 0;

  do {
    err = poll(&info, 1, timeout);
  } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

  if ((err > 0) && (info.revents & POLLIN)) {
    return 1;
  }

  return err;
}

static int
poll_write(int fd, int timeout)
{
  struct pollfd info;
  int err;

  info.fd = fd;
  info.events = POLLOUT;
  info.revents = 0;

  do {
    err = poll(&info, 1, timeout);
  } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));


  if ((err > 0) && (info.revents & POLLOUT)) {
    return 1;
  }

  return err;
}

static int
open_socket(int port, const char *ip = NULL, char *ip_to_bind = NULL)
{

  int sock = 0;
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct addrinfo *result_to_bind = NULL;
  char port_str[8] = {'\0'};
  int err = 0;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering open_socket(%d, %s, %s)\n", port, ip, ip_to_bind);
#endif
  if (!ip) {
    ip = "127.0.0.1";
  }

  snprintf(port_str, sizeof(port_str), "%d", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  err = getaddrinfo(ip, port_str, &hints, &result);
  if (err != 0) {
    cop_log (COP_WARNING, "(test) unable to get address info [%d %s] at ip %s, port %s\n", err, gai_strerror(err), ip, port_str);
    goto getaddrinfo_error;
  }

  // Create a socket
  do {
    sock = socket(result->ai_family, result->ai_socktype, 0);
  } while ((sock < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

  if (sock < 0) {
    cop_log(COP_WARNING, "(test) unable to create socket [%d '%s']\n", errno, strerror(errno));
    goto error;
  }

  if (ip_to_bind) {
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = result->ai_family;
    hints.ai_socktype = result->ai_socktype;

    err = getaddrinfo(ip_to_bind, NULL, &hints, &result_to_bind);
    if (err != 0) {
      cop_log (COP_WARNING, "(test) unable to get address info [%d %s] at ip %s\n", err, gai_strerror(err), ip_to_bind);
      freeaddrinfo(result_to_bind);
      goto error;
    }

    if (safe_bind(sock, result_to_bind->ai_addr, result_to_bind->ai_addrlen) < 0) {
      cop_log (COP_WARNING, "(test) unable to bind socket [%d '%s']\n", errno, strerror (errno));
    }

    freeaddrinfo(result_to_bind);
  }

  // Put the socket in non-blocking mode...just to be extra careful
  // that we never block.
  do {
    err = fcntl(sock, F_SETFL, O_NONBLOCK);
  } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

  if (err < 0) {
    cop_log(COP_WARNING, "(test) unable to put socket in non-blocking mode [%d '%s']\n", errno, strerror(errno));
    goto error;
  }
  // Connect to the specified port on the machine we're running on.
  do {
    err = connect(sock, result->ai_addr, result->ai_addrlen);
  } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

  if ((err < 0) && (errno != EINPROGRESS)) {
    cop_log(COP_WARNING, "(test) unable to connect to server [%d '%s'] at port %d\n", errno, strerror(errno), port);
    goto error;
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving open_socket(%d, %s, %s) --> %d\n", port, ip, ip_to_bind, sock);
#endif
  freeaddrinfo(result);
  return sock;

error:
  if (sock >= 0) {
    close_socket(sock);
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving open_socket(%d, %s, %s) --> %d\n", port, ip, ip_to_bind, -1);
#endif
getaddrinfo_error:
  freeaddrinfo(result);
  return -1;
}

static int
test_port(int port, const char *request, char *buffer, int bufsize,
          int64_t test_timeout, char *ip = NULL, char *ip_to_bind = NULL)
{
  int64_t start_time, timeout;
  int sock;
  size_t length = strlen(request);
  int64_t err, idx;

  start_time = milliseconds();

  sock = open_socket(port, ip, ip_to_bind);
  if (sock < 0) {
    goto error;
  }

  timeout = milliseconds() - start_time;
  if (timeout >= test_timeout) {
    cop_log(COP_WARNING, "(test) timeout occurred [%" PRId64 " ms]\n", timeout);
    goto error;
  }
  timeout = test_timeout - timeout;

  err = poll_write(sock, timeout);
  if (err < 0) {
    cop_log(COP_WARNING, "(test) poll write failed [%d '%s']\n", errno, strerror(errno));
    goto error;
  } else if (err == 0) {
    cop_log(COP_WARNING, "(test) write timeout [%" PRId64 " ms]\n", timeout);
    goto error;
  }
  // Write the request to the server.
  while (length > 0) {
    do {
      err = write(sock, request, length);
    } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

    if (err < 0) {
      cop_log(COP_WARNING, "(test) write failed [%d '%s']\n", errno, strerror(errno));
      goto error;
    }

    request += err;
    length -= err;
  }

  idx = 0;
  for (;;) {
    if (idx >= bufsize) {
      cop_log(COP_WARNING, "(test) response is too large [%d]\n", idx);
      goto error;
    }

    timeout = milliseconds() - start_time;
    if (timeout >= test_timeout) {
      cop_log(COP_WARNING, "(test) timeout occurred [%" PRId64 " ms]\n", timeout);
      goto error;
    }
    timeout = test_timeout - timeout;

    err = poll_read(sock, timeout);
    if (err < 0) {
      cop_log(COP_WARNING, "(test) poll read failed [%d '%s']\n", errno, strerror(errno));
      goto error;
    } else if (err == 0) {
      cop_log(COP_WARNING, "(test) read timeout [%" PRId64 " ]\n", timeout);
      goto error;
    }

    do {
      err = read(sock, &buffer[idx], bufsize - idx);
    } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

    if (err < 0) {
      cop_log(COP_WARNING, "(test) read failed [%d '%s']\n", errno, strerror(errno));
      goto error;
    } else if (err == 0) {
      buffer[idx] = '\0';
      close(sock);
      return 0;
    } else {
      idx += err;
    }
  }

error:
  if (sock >= 0) {
    close_socket(sock);
  }
  return -1;
}

static int
read_manager_string(const char *variable, char *value)
{
  char buffer[4096];
  char request[1024];
  char *p, *e;
  int err;

  snprintf(request, sizeof(request), "read %s\n", variable);

  err = test_port(rs_port, request, buffer, 4095, manager_timeout * 1000);
  if (err < 0) {
    return err;
  }

  p = strstr(buffer, variable);
  if (!p) {
    cop_log(COP_WARNING, "(manager test) could not find record " "name in response\n");
    return -1;
  }
  p += strlen(variable);

  p = strstr(p, "Val:");
  if (!p) {
    cop_log(COP_WARNING, "(manager test) could not find record " "value in response\n");
    return -1;
  }
  p += sizeof("Val:") - 1;

  while (*p && (*p != '\'')) {
    p += 1;
  }

  if (*p == '\0') {
    cop_log(COP_WARNING, "(manager test) could not find properly " "delimited value in response\n");
    return -1;
  }
  p += 1;

  e = p;
  while (*e && (*e != '\'')) {
    e += 1;
  }

  if (*e != '\'') {
    cop_log(COP_WARNING, "(manager test) could not find properly " "delimited value in response\n");
    return -1;
  }

  strncpy(value, p, e - p);
  value[e - p] = '\0';

  return 0;
}

static int
read_manager_int(const char *variable, int *value)
{
  char buffer[4096];
  char request[1024];
  char *p;
  int err;

  snprintf(request, sizeof(request), "read %s\n", variable);

  err = test_port(rs_port, request, buffer, 4095, manager_timeout * 1000);
  if (err < 0) {
    return err;
  }

  p = strstr(buffer, variable);
  if (!p) {
    cop_log(COP_WARNING, "(manager test) could not find record " "name in response\n");
    return -1;
  }
  p += strlen(variable);

  p = strstr(p, "Val:");
  if (!p) {
    cop_log(COP_WARNING, "(manager test) could not find record " "value in response\n");
    return -1;
  }
  p += sizeof("Val:") - 1;

  while (*p && (*p != '\'')) {
    p += 1;
  }

  if (*p == '\0') {
    cop_log(COP_WARNING, "(manager test) could not find properly " "delimited value in response\n");
    return -1;
  }
  p += 1;

  *value = 0;
  while (isdigit(*p)) {
    *value = *value * 10 + (*p - '0');
    p += 1;
  }

  if (*p != '\'') {
    cop_log(COP_WARNING, "(manager test) could not find properly " "delimited value in response\n");
    return -1;
  }
  return 0;
}

static int
read_mgmt_cli_int(const char *variable, int *value)
{
  TSInt val;

  if (TSRecordGetInt(variable, &val) != TS_ERR_OKAY) {
    cop_log(COP_WARNING, "(cli test) could not communicate with mgmt cli\n");
    return -1;
  }
  *value = val;
  return 0;
}


static int
test_rs_port()
{
  char buffer[4096];
  int err;

  err = read_manager_string("proxy.config.manager_binary", buffer);
  if (err < 0) {
    return err;
  }

  if (strcmp(buffer, manager_binary) != 0) {
    cop_log(COP_WARNING, "(manager test) bad response value\n");
    return -1;
  }

  return 0;
}


static int
test_mgmt_cli_port()
{
  TSString val;
  int ret = 0;

  if (TSRecordGetString("proxy.config.manager_binary", &val) !=  TS_ERR_OKAY) {
    cop_log(COP_WARNING, "(cli test) unable to retrieve manager_binary\n");
    ret = -1;
  } else {
    if (strcmp(val, manager_binary) != 0) {
      cop_log(COP_WARNING, "(cli test) bad response value, got %s, expected %s\n", val, manager_binary);
      ret = -1;
    }
  }

  if (val)
    TSfree(val);
  return ret;
}


static int
test_http_port(int port, char *request, int timeout, char *ip = NULL, char *ip_to_bind = NULL)
{
  char buffer[4096];
  char *p;
  int err;

  err = test_port(port, request, buffer, 4095, timeout, ip, ip_to_bind);
  if (err < 0) {
    return err;
  }

  p = buffer;

  if (strncmp(p, "HTTP/", 5) != 0) {
    cop_log(COP_WARNING, "(http test) received malformed response\n");
    return -1;
  }

  p += 5;
  while (*p && !isspace(*p)) {
    p += 1;
  }

  while (*p && isspace(*p)) {
    p += 1;
  }

  if (strncmp(p, "200", 3) != 0) {
    char pstatus[4] = { '\0', '\0', '\0', '\0' };
    strncpy(pstatus, p, 3);
    cop_log(COP_WARNING, "(http test) received non-200 status(%s)\n", pstatus);
    return -1;
  }

  p = strstr(p, "\r\n\r\n");
  if (!p) {
    cop_log(COP_WARNING, "(http test) could not find end of header\n");
    return -1;
  }

  p += 4;
  while (*p) {
    if (strncmp(p, "abcdefghijklmnopqrstuvwxyz", 26) != 0) {
      cop_log(COP_WARNING, "(http test) corrupted response data\n");
      return -1;
    }

    p += 26;
    while (*p && (*p != '\n')) {
      p += 1;
    }
    p += 1;
  }

  return 0;
}

static int
test_server_http_port()
{
  char request[1024] = {'\0'};
  static char localhost[] = "127.0.0.1";

  // Generate a request for a the 'synthetic.txt' document the manager
  // servers up on the autoconf port.
  snprintf(request, sizeof(request), "GET http://127.0.0.1:%d/synthetic.txt HTTP/1.0\r\n\r\n", autoconf_port);

  return test_http_port(http_backdoor_port, request, server_timeout * 1000, localhost, localhost);
}

static int
heartbeat_manager()
{
  int err;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering heartbeat_manager()\n");
#endif
  // the CLI, and the rsport if cluster is enabled.
  err = test_mgmt_cli_port();
  if ((0 == err) && (cluster_type != NO_CLUSTER))
    err = test_rs_port();

  if (err < 0) {
    if (err < 0) {
      manager_failures += 1;
      cop_log(COP_WARNING, "manager heartbeat [variable] failed [%d]\n", manager_failures);

      if (manager_failures > 1) {
        manager_failures = 0;
        cop_log(COP_WARNING, "killing manager\n");
        safe_kill(manager_lockfile, manager_binary, true);
      }
#ifdef TRACE_LOG_COP
      cop_log(COP_DEBUG, "Leaving heartbeat_manager() --> %d\n", err);
#endif
      return err;
    }
  }


#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving heartbeat_manager() --> %d\n", err);
#endif
  return err;
}

static int
heartbeat_server()
{
  int err;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering heartbeat_server()\n");
#endif
  err = test_server_http_port();

  if (err < 0) {
    // If the test failed, increment the count of the number of
    // failures. We don't kill the server the first time the test
    // fails because we might just have gotten caught in a race
    // where we decided to do the test because we thought the
    // server was up even though somebody was in the process of
    // bringing it down. The "server_up" function will reset
    // 'server_failures' if it determines the server is down.

    server_failures += 1;
    cop_log(COP_WARNING, "server heartbeat failed [%d]\n", server_failures);

    // If this is the second time that the server test has failed
    // we kill the server.
    if (server_failures > 1) {
      server_failures = 0;
      cop_log(COP_WARNING, "killing server\n");

      // TSqa02622: Change the ALRM signal handler while
      //   trying to kill the process since if a core
      //   is being written, it could take a long time
      //   Set a new alarm so that we can print warnings
      //   if it is taking too long to kill the server
      //
      safe_kill(server_lockfile, server_binary, false);
    }
  } else {
    if (server_failures)
      cop_log(COP_WARNING, "server heartbeat succeeded\n");
    server_failures = 0;
  }

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving heartbeat_server() --> %d\n", err);
#endif
  return err;
}

static int
server_up()
{
  static int old_val = 0;
  int val = -1;
  int err;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering server_up()\n");
#endif
  if (cluster_type != NO_CLUSTER) {
    err = read_manager_int("proxy.node.proxy_running", &val);
    if (err < 0) {
      cop_log(COP_WARNING, "could not contact manager, " "assuming server is down\n");
#ifdef TRACE_LOG_COP
      cop_log(COP_DEBUG, "Leaving server_up() --> 0\n");
#endif
      return 0;
    }
  } else {
    err = read_mgmt_cli_int("proxy.node.proxy_running", &val);
    if (err < 0) {
      cop_log(COP_WARNING, "could not contact manager, " "assuming server is down\n");
#ifdef TRACE_LOG_COP
      cop_log(COP_DEBUG, "Leaving server_up() --> 0\n");
#endif
      return 0;
    }
  }

  if (val != old_val) {
    server_failures = 0;
    server_not_found = 0;
    old_val = val;
  }

  if (val == 1) {
#ifdef TRACE_LOG_COP
    cop_log(COP_DEBUG, "Leaving server_up() --> 1\n");
#endif
    return 1;
  } else {
#ifdef TRACE_LOG_COP
    cop_log(COP_DEBUG, "Leaving server_up() --> 0\n");
#endif
    return 0;
  }
}


//         |  state  |  status  |  action
// --------|---------|----------|---------------
// manager |   up    |    ok    |  nothing
// server  |   up    |    ok    |
// --------|---------|----------|---------------
// manager |   up    |    bad   |  kill manager
// server  |   up    |    ?     |
// --------|---------|----------|---------------
// manager |   up    |    ok    |  kill manager
// server  |   down  |    ?     |
// --------|---------|----------|---------------
// manager |   up    |    ok    |  kill server
// server  |   up    |    bad   |


static void
check_programs()
{
  int err;
  pid_t holding_pid;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering check_programs()\n");
#endif

  // Try to get the manager lock file. If we succeed in doing this,
  // it means there is no manager running.
  Lockfile manager_lf(manager_lockfile);
  err = manager_lf.Open(&holding_pid);
  chown_file_to_user(manager_lockfile, admin_user);
#if defined(linux)
  // if lockfile held, but process doesn't exist, killall and try again
  if (err == 0) {
    if (kill(holding_pid, 0) == -1) {
      cop_log(COP_WARNING, "%s's lockfile is held, but its pid (%d) is missing;"
              " killing all processes named '%s' and retrying\n", manager_binary, holding_pid, manager_binary);
      ink_killall(manager_binary, killsig);
      sleep(1);                 // give signals a chance to be received
      err = manager_lf.Open(&holding_pid);
    }
  }
#endif
  if (err > 0) {
    // 'lockfile_open' returns the file descriptor of the opened
    // lockfile.  We need to close this before spawning the
    // manager so that the manager can grab the lock.
    manager_lf.Close();

#if !defined(MANAGER_FLAP_DETECTION)
    // Make sure we don't have a stray traffic server running.
    cop_log(COP_WARNING, "traffic_manager not running, making sure traffic_server is dead\n");
    safe_kill(server_lockfile, server_binary, false);
    // Spawn the manager.
    cop_log(COP_WARNING, "spawning traffic_manager\n");
    spawn_manager();
#else
    // Make sure we don't have a stray traffic server running.
    if (!manager_flapping) {
      cop_log(COP_WARNING, "traffic_manager not running, making sure traffic_server is dead\n");
      safe_kill(server_lockfile, server_binary, false);
    }
    // Spawn the manager (check for flapping manager too)
    ink_hrtime now = milliseconds();
    if (!manager_flapping) {
      if ((manager_flap_interval_start_time == 0) ||
          (now - manager_flap_interval_start_time > MANAGER_FLAP_INTERVAL_MSEC)
        ) {
        // either:
        // . it's our first time through
        // . we were flapping a while ago, but we would
        //   like to retry now
        // . it's been a while since we last tried to start
        //   traffic_manager
        manager_flap_count = 0;
      }
      if (manager_flap_count >= MANAGER_MAX_FLAP_COUNT) {
        // we've flapped too many times, hold off for a while
        cop_log(COP_WARNING, "unable to start traffic_manager, retrying in %d second(s)\n",
                MANAGER_FLAP_RETRY_MSEC / 1000);
        manager_flapping = true;
        manager_flap_retry_start_time = now;
      } else {
        // try to spawn traffic_manager
        cop_log(COP_WARNING, "spawning traffic_manager\n");
        spawn_manager();
        // track spawn attempt
        if (manager_flap_count == 0) {
          manager_flap_interval_start_time = now;
        }
        manager_flap_count++;
      }
    } else {
      // we were flapping, take some time off and don't call
      // spawn_manager
      if (now - manager_flap_retry_start_time > MANAGER_FLAP_RETRY_MSEC) {
        manager_flapping = false;
        manager_flap_interval_start_time = 0;
      }
    }
#endif
  } else {
    // If there is a manager running we want to heartbeat it to
    // make sure it hasn't wedged. If the manager test succeeds we
    // check to see if the server is up. (That is, it hasn't been
    // brought down via the UI).  If the manager thinks the server
    // is up, we make sure there is actually a server process
    // running. If there is we test it.

    alarm(2 * manager_timeout);
    err = heartbeat_manager();
    alarm(0);

    if (err < 0) {
      return;
    }

    if (server_up() <= 0) {
      return;
    }

    Lockfile server_lf(server_lockfile);
    err = server_lf.Open(&holding_pid);
#if defined(linux)
    // if lockfile held, but process doesn't exist, killall and try again
    if (err == 0) {
      if (kill(holding_pid, 0) == -1) {
        cop_log(COP_WARNING, "%s's lockfile is held, but its pid (%d) is missing;"
                " killing all processes named '%s' and retrying\n", server_binary, holding_pid, server_binary);
        ink_killall(server_binary, killsig);
        sleep(1);               // give signals a chance to be received
        err = server_lf.Open(&holding_pid);
      }
    }
#endif
    if (err > 0) {
      server_lf.Close();

      server_not_found += 1;
      cop_log(COP_WARNING, "cannot find traffic_server [%d]\n", server_not_found);

      if (server_not_found > 1) {
        server_not_found = 0;
        cop_log(COP_WARNING, "killing manager\n");
        safe_kill(manager_lockfile, manager_binary, true);
      }
    } else {
      alarm(2 * server_timeout);
      heartbeat_server();
      alarm(0);
    }
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving check_programs()\n");
#endif
}

static void
check_memory()
{
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering check_memory()\n");
#endif
#if defined(linux)
  if (check_memory_required) {
    FILE *fp;
    char buf[LINE_MAX];
    long long memfree, swapfree, swapsize;
    memfree = swapfree = swapsize = 0;
    if ((fp = fopen("/proc/meminfo", "r"))) {
      while (fgets(buf, sizeof buf, fp)) {
        if (strncmp(buf, "MemFree:", sizeof "MemFree:" - 1) == 0)
          memfree = strtoll(buf + sizeof "MemFree:" - 1, 0, 10);
        else if (strncmp(buf, "SwapFree:", sizeof "SwapFree:" - 1) == 0)
          swapfree = strtoll(buf + sizeof "SwapFree:" - 1, 0, 10);
        else if (strncmp(buf, "SwapTotal:", sizeof "SwapTotal:" - 1) == 0)
          swapsize = strtoll(buf + sizeof "SwapTotal:" - 1, 0, 10);
      }
      fclose(fp);
      // simple heuristic for linux 2.2.x
      //    swapsize swapfree memfree
      // 1:    >0      low     high    (bad)
      // 2:    >0      high    low     (okay)
      // 3:    >0      low     low     (bad; covered by 1)
      // 4:     0       0      high    (okay)
      // 5:     0       0      low     (bad)
      if ((swapsize != 0 && swapfree < check_memory_min_swapfree_kb) ||
          (swapsize == 0 && memfree < check_memory_min_memfree_kb)) {
        cop_log(COP_WARNING, "Low memory available (swap: %dkB, mem: %dkB)\n", (int) swapfree, (int) memfree);
        cop_log(COP_WARNING, "Killing '%s' and '%s'\n", manager_binary, server_binary);
        manager_failures = 0;
        safe_kill(manager_lockfile, manager_binary, true);
        server_failures = 0;
        safe_kill(server_lockfile, server_binary, false);
      }
    } else {
      cop_log(COP_WARNING, "Unable to open /proc/meminfo: %s\n", strerror(errno));
    }
  }
#endif
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving check_memory()\n");
#endif
}

static int
check_no_run()
{
  char path[PATH_MAX * 2];
  struct stat info;
  int err;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering check_no_run()\n");
#endif
  snprintf(path, sizeof(path), "%s/internal/no_cop", config_dir);

  do {
    err = stat(path, &info);
  } while ((err < 0) && (transient_error(errno, TRANSIENT_ERROR_WAIT_MS)));

  if (err < 0) {
#ifdef TRACE_LOG_COP
    cop_log(COP_DEBUG, "Leaving check_no_run() --> 0\n");
#endif
    return 0;
  }

  cop_log(COP_WARNING, "encountered \"%s\" file...exiting\n", path);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving check_no_run() --> -1\n");
#endif
  return -1;
}

// Changed function from taking no argument and returning void
// to taking a void* and returning a void*. The change was made
// so that we can call ink_thread_create() on this function
// in the case of running cop as a win32 service.
static void*
check(void *arg)
{
  bool mgmt_init = false;
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering check()\n");
#endif

  for (;;) {
    // problems with the ownership of this file as root Make sure it is
    // owned by the admin user
    chown_file_to_user(manager_lockfile, admin_user);
    chown_file_to_user(server_lockfile, admin_user);

    alarm(2 * (sleep_time + manager_timeout * 2 + server_timeout));

    if (check_no_run() < 0) {
      break;
    }
    // Log any SIGCLD singals we received
    if (child_pid > 0) {
      if (WIFEXITED(child_status) == 0) {
        // Child terminated abnormally
        cop_log(COP_WARNING,
                "cop received non-normal child status signal [%d %d]\n", child_pid, WEXITSTATUS(child_status));
      } else {
        // normal termination
        cop_log(COP_WARNING, "cop received child status signal [%d %d]\n", child_pid, child_status);
      }
      if (WIFSIGNALED(child_status)) {
        int sig = WTERMSIG(child_status);
        cop_log(COP_WARNING, "child terminated due to signal %d: %s\n", sig, strsignal(sig));
      }

      child_pid = child_status = 0;
    }

    // Re-read the config file information
    read_config();

    // Check to make sure the programs are running
    check_programs();

    // Check to see if we're running out of free memory
    check_memory();

    // Pause to catch our breath. (10 seconds).
    // Use 'millisleep()' because normal 'sleep()' interferes with
    // the SIGALRM signal which we use to heartbeat the cop.
    millisleep(sleep_time * 1000);

    // We do this after the first round of checks, since the first "check" will spawn traffic_manager
    if (!mgmt_init) {
      TSInit(Layout::get()->runtimedir, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));
      mgmt_init = true;
    }
  }

  // Done with the mgmt API.
  TSTerminate();

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving check()\n");
#endif
  return arg;
}


static void
check_lockfile()
{
  int err;
  pid_t holding_pid;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering check_lockfile()\n");
#endif
  Lockfile cop_lf(cop_lockfile);
  err = cop_lf.Get(&holding_pid);
  if (err < 0) {
    cop_log(COP_WARNING, "periodic cop heartbeat couldn't open '%s' (errno %d)\n", cop_lockfile, -err);
    exit(1);
  } else if (err == 0) {
    cop_log(COP_DEBUG, "periodic heartbeat successful, another cop still on duty\n");
    exit(1);
  }

  cop_log(LOG_NOTICE, "--- Cop Starting [Version: %s] ---\n", appVersionInfo.FullVersionInfoStr);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving check_lockfile()\n");
#endif
}

static void
init_signals()
{
  struct sigaction action;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering init_signals()\n");
#endif
  // Handle the SIGCHLD signal. We simply reap all children that
  // die (which should only be spawned traffic_manager's).
  action.sa_handler = sig_child;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  sigaction(SIGCHLD, &action, NULL);

  // Handle a bunch of fatal signals. We simply call abort() when
  // these signals arrive in order to generate a core. There is some
  // difficulty with generating core files when linking with libthread
  // under solaris.
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  action.sa_handler = NULL;
  action.sa_sigaction = sig_fatal;
#else
  action.sa_handler = sig_fatal;
#endif
  sigemptyset(&action.sa_mask);
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_flags = 0;
#endif

  sigaction(SIGQUIT, &action, NULL);
  sigaction(SIGILL, &action, NULL);
  sigaction(SIGFPE, &action, NULL);
  sigaction(SIGBUS, &action, NULL);
  sigaction(SIGSEGV, &action, NULL);
#if !defined(linux)
  sigaction(SIGEMT, &action, NULL);
  sigaction(SIGSYS, &action, NULL);
#endif

  // Handle the SIGALRM signal. We use this signal to make sure the
  // cop never wedges. It gets reset every time through its loop. If
  // the alarm ever expires we treat it as a fatal signal and dump
  // core, secure in the knowledge we'll get restarted.
  set_alarm_death();

  action.sa_handler = sig_ignore;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  sigaction(SIGPIPE, &action, NULL);
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving init_signals()\n");
#endif
}

static void
init_config_dir()
{

  struct stat info;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering init_config_dir()\n");
#endif

  root_dir = Layout::get()->prefix;
  runtime_dir = Layout::get()->runtimedir;
  config_dir = Layout::get()->sysconfdir;

  if (chdir(root_dir) < 0) {
    cop_log(COP_FATAL, "unable to change to root directory \"%s\" [%d '%s']\n", root_dir, errno, strerror(errno));
    cop_log(COP_FATAL," please set correct path in env variable TS_ROOT \n");
    exit(1);
  }

  if (stat(config_dir, &info) < 0) {
    cop_log(COP_FATAL, "unable to locate config directory '%s'\n",config_dir);
    cop_log(COP_FATAL, " please try setting correct root path in env variable TS_ROOT \n");
    exit(1);
  }

  if (stat(runtime_dir, &info) < 0) {
    cop_log(COP_FATAL, "unable to locate local state directory '%s'\n",runtime_dir);
    cop_log(COP_FATAL, " please try setting correct root path in either env variable TS_ROOT \n");
    exit(1);
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving init_config_dir()\n");
#endif
}

static void
init_lockfiles()
{

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering init_lockfiles()\n");
#endif
  Layout::relative_to(cop_lockfile, sizeof(cop_lockfile), Layout::get()->runtimedir, COP_LOCK);
  Layout::relative_to(manager_lockfile, sizeof(manager_lockfile), Layout::get()->runtimedir, MANAGER_LOCK);
  Layout::relative_to(server_lockfile, sizeof(server_lockfile), Layout::get()->runtimedir, SERVER_LOCK);

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving init_lockfiles()\n");
#endif
}

static void
init_syslog()
{
  openlog("traffic_cop", LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);
}

static void
init_config_file()
{
  struct stat info;

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering init_config_file()\n");
#endif
  Layout::relative_to(config_file, sizeof(config_file),
                      config_dir, "records.config.shadow");
  if (stat(config_file, &info) < 0) {
    Layout::relative_to(config_file, sizeof(config_file),
                        config_dir, "records.config");
    if (stat(config_file, &info) < 0) {
      cop_log(COP_FATAL, "unable to locate \"%s/records.config\" or \"%s/records.config.shadow\"\n",
              config_dir, config_dir);
      exit(1);
    }
  }
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving init_config_file()\n");
#endif
}

static void
init()
{
#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Entering init()\n");
#endif

  init_signals();
  init_syslog();

  init_config_dir();

  init_config_file();
  init_lockfiles();
  check_lockfile();

#if defined(linux)
  struct utsname buf;
  if (uname(&buf) >= 0) {
    if (strncmp(buf.release, "2.2.", 4) == 0) {
      cop_log(COP_WARNING, "Linux 2.2.x kernel detected; enabling low memory fault protection");
      check_memory_required = true;
    }
  }
#endif

#ifdef TRACE_LOG_COP
  cop_log(COP_DEBUG, "Leaving init()\n");
#endif
}

int version_flag = 0;

int
main(int argc, char *argv[])
{
  int fd;
  appVersionInfo.setup(PACKAGE_NAME,"traffic_cop", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-stop") == 0) {
      ink_fputln(stdout, "Cool! I think I'll be a STOP cop!");
      killsig = SIGSTOP;
    } else if (strcmp(argv[i], "-V") == 0) {
      ink_fputln(stderr, appVersionInfo.FullVersionInfoStr);
      exit(0);
    }
  }
  // Detach STDIN, STDOUT, and STDERR (basically, "nohup"). /leif
  signal(SIGHUP, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);

  setsid();                     // Important, thanks Vlad. :)
#if defined(freebsd) && !defined(kfreebsd)
  setpgrp(0,0);
#else
  setpgrp();
#endif

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if ((fd = open("/dev/null", O_WRONLY, 0)) >= 0) {
    fcntl(fd, F_DUPFD, STDIN_FILENO);
    fcntl(fd, F_DUPFD, STDOUT_FILENO);
    fcntl(fd, F_DUPFD, STDERR_FILENO);
    close(fd);
  } else {
    ink_fputln(stderr, "Unable to open /dev/null");
    return 0;
  }

  // Initialize and start it up.
  init();
  check(NULL);

  return 0;
}
