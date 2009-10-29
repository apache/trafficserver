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

   remote_start.cc

   Description:

   
 ****************************************************************************/

#include <unistd.h>

#if (HOST_OS == darwin)
// Fix wierd conflict with Tcl #define for panic
#include <sys/mman.h>
#endif

#include "sio_loop.h"
#include "sio_buffer.h"
#include "raf_cmd.h"
#include "test_utils.h"
#include "test_exec.h"

#include "Tokenizer.h"
#include "Diags.h"

/* Constants */
static const int SIZE_32K = 32768;

static int
create_pipe_for_remote(const char *hostname, int *pipe_arg, int in_pipe)
{

  int r;
  do {
    r = pipe(pipe_arg);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    TE_Error("Pipe creation for %s failed : %s", hostname, strerror(errno));
    return -1;
  }

  do {
    r = fcntl((in_pipe == true) ? pipe_arg[0] : pipe_arg[1], F_SETFL, O_NONBLOCK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    TE_Error("Failed to set non-block on %s pipe", hostname, strerror(errno));
    close(pipe_arg[0]);
    close(pipe_arg[1]);
    return -1;
  }

  return 0;
}

pid_t
start_remote_shell(const UserDirInfo * ud_info, const char *hostname, int *in_pipe, int *out_pipe)
{

  int r = create_pipe_for_remote(hostname, in_pipe, true);
  if (r < 0) {
    return r;
  }

  r = create_pipe_for_remote(hostname, out_pipe, false);
  if (r < 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    return r;
  }

  char *sub_cmd;
  if (strcmp(hostname, "localhost") == 0) {
    //  The command we want is
    //   <shell> -c "bash -s"
    sub_cmd = strdup("bash -s");
  } else {
    //  The command we want is
    //   <shell> -c "ssh <hostname> -q -o "BatchMode yes" -o "StrictHostKeyChecking no" bash -s"
    const char *sub_cmd_format = "ssh -q -o \"BatchMode yes\" -o \"StrictHostKeyChecking no\" %s bash -s";
    sub_cmd = (char *) malloc(strlen(sub_cmd_format) + strlen(hostname) + 1);
    sprintf(sub_cmd, sub_cmd_format, hostname);
  }

  char **argv = build_argv_v(ud_info->shell, "-c", sub_cmd, NULL);
  free(sub_cmd);
  sub_cmd = NULL;

  pid_t new_pid = fork();

  if (new_pid < 0) {
    TE_Error("fork to contact %s failed : %s", hostname, strerror(errno));
    close(in_pipe[0]);
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(out_pipe[1]);
    destroy_argv(argv);
    return -1;
  } else if (new_pid == 0) {
    // Child
    r = dup2(in_pipe[0], 0);
    if (r < 0) {
      Fatal("dup to stdin failed : %s", strerror(errno));
    }

    r = dup2(out_pipe[1], 1);
    if (r < 0) {
      Fatal("dup to stdout failed : %s", strerror(errno));
    }

    r = dup2(out_pipe[1], 2);
    if (r < 0) {
      Fatal("dup to stderr failed : %s", strerror(errno));
    }
    // FIX - use fd limit
    for (int i = 3; i < 1024; i++) {
      close(i);
    }

    r = execv(ud_info->shell, argv);

    if (r < 0) {
      Fatal("exec failed : %s", strerror(errno));
      return r;
    }
  } else {
    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);
    destroy_argv(argv);

    return new_pid;
  }

  return -1;
}

const char *
send_remote_cmd(sio_buffer * in_buf, int fd_in, sio_buffer * out_buf, int fd_out)
{

  int timeout_ms = 60 * 1000;
  const char *rmsg = write_buffer(fd_in, in_buf, &timeout_ms);

  if (rmsg) {
    return rmsg;
  }

  rmsg = read_until(fd_out, out_buf, '\n', &timeout_ms);

  if (rmsg) {
    return rmsg;
  }

  return NULL;
}

const char *
determine_arch(int fd_in, int fd_out, char **arch_out)
{

  sio_buffer in_buf;
  sio_buffer out_buf;

  // First get os type
  const char cmd[] = "uname -s\n";
  in_buf.fill(cmd, sizeof(cmd) - 1);

  const char *rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);
  if (rmsg) {
    return rmsg;
  }

  char *start = out_buf.start();
  char *end = out_buf.memchr('\n');
  *end = '\0';

  // Since determine arch is the first command executed after the
  //  ssh, it's entirely possible that output has garbage in from
  //  error messages in the .login or .profile/.cshrc.  There error
  //  messages tend be of the form "<thing>: <msg>"   Thus we
  //  will ignore any line message that has a colon in it
  while (memchr(start, ':', end - start) != NULL) {
    out_buf.consume((end - start) + 1);

    // First check if we've got another complete line
    end = out_buf.memchr('\n');
    if (end == NULL) {
      // We need to read more
      int timeout_ms = 60 * 1000;
      rmsg = read_until(fd_out, &out_buf, '\n', &timeout_ms);
      if (rmsg) {
        return rmsg;
      }
      end = out_buf.memchr('\n');
    }
    // At this point we've got another line and end is pointing
    //  to the '\n' terminating the line.  So we just need
    //  to tidy up an loop around again
    start = out_buf.start();
    *end = '\0';

  }

  Debug("remote", "OS is %s", out_buf.start());

  if (strcmp(out_buf.start(), "SunOS") == 0) {
    // We need to get the processor type to distinguish between
    //    sparc & x86 solaris
    in_buf.reset();
    out_buf.reset();

    const char pcmd[] = "uname -p\n";
    in_buf.fill(pcmd, sizeof(pcmd) - 1);

    if ((rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out)) != NULL) {
      return rmsg;
    }

    end = out_buf.memchr('\n');
    *end = '\0';

    Debug("remote", "Proc Type is %s", out_buf.start());

    if (strcmp(out_buf.start(), "i386") == 0) {
      *arch_out = strdup("SunOSx86");
    } else {
      *arch_out = strdup("SunOS");
    }
  } else {
    *arch_out = strdup(out_buf.start());
  }

  return NULL;
}

// char* find_proc_manager_binary(const char* remote_arch)
//
//   Caller frees return value
//
char *
find_proc_manager_binary(const UserDirInfo * ud, const char *remote_arch)
{

  char *my_arch = get_arch_str();
  FreeOnDestruct freer(my_arch);

  if (strcasecmp(remote_arch, my_arch) == 0) {
    int r;
    do {
      r = access("proc_manager", R_OK | X_OK);
    } while (r < 0 && errno == EINTR);

    if (r == 0) {
      Debug("remote", "Using local proc_manager");
      return strdup("proc_manager");
    }
  }

  char *pack_name = find_local_package("proc_manager", remote_arch);

  if (pack_name == NULL) {
    return NULL;
  }

  int len = strlen(ud->package_dir) + 1 + strlen(pack_name) + 1;
  char *local_pm_path = (char *) malloc(len);
  sprintf(local_pm_path, "%s/%s", ud->package_dir, pack_name);

  free(pack_name);
  return local_pm_path;
}


const char *
setup_remote_directories(int fd_in, int fd_out, UserDirInfo * ud)
{

  sio_buffer in_buf;
  sio_buffer out_buf;

  const char check_path_format[] = "if [ -d %s ]; then\n" "   echo ok\n" "else\n" "   echo not found\n" "fi\n";

  const char mkdir_format[] = "if mkdir %s\n" "then\n" "   echo ok\n" "fi\n";

  int len = sizeof(check_path_format) + strlen(ud->test_stuff_path) + 1;
  char *cmd = (char *) malloc(len);
  sprintf(cmd, check_path_format, ud->test_stuff_path);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  const char *rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  char *end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to check stuff path: %s", out_buf.start());

  if (strcmp(out_buf.start(), "ok") != 0) {
    return "remote stuff_path dir does not exist";
  }

  in_buf.reset();
  out_buf.reset();

  len = sizeof(check_path_format) + strlen(ud->test_stuff_path_and_dir) + 1;
  cmd = (char *) malloc(len);
  sprintf(cmd, check_path_format, ud->test_stuff_path_and_dir);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to check stuff dir: %s", out_buf.start());

  if (strcmp(out_buf.start(), "ok") != 0) {
    // Need to create the directory
    in_buf.reset();
    out_buf.reset();

    int len = sizeof(mkdir_format) + strlen(ud->test_stuff_path_and_dir) + 1;
    char *cmd = (char *) malloc(len);
    sprintf(cmd, mkdir_format, ud->test_stuff_path_and_dir);

    in_buf.fill(cmd, strlen(cmd));
    free(cmd);
    cmd = NULL;

    rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

    end = out_buf.memchr('\n');
    *end = '\0';
    Debug("remote", "Response to mkdir stuff dir: %s", out_buf.start());

    if (strcmp(out_buf.start(), "ok") != 0) {
      TE_Note("failed to create remote directory : %s", out_buf.start());
      return "remote directory creation failed";
    }
  }

  return NULL;
}


const char *
transfer_proc_manager_binary(int local_file_fd, int fd_in, int *timeout_ms)
{

  sio_buffer file_buf(SIZE_32K);

  bool read_done = false;
  const char *rmsg = NULL;

  while (read_done == false) {

    int eof = 0;
    rmsg = read_to_buffer(local_file_fd, &file_buf, SIZE_32K, &eof, timeout_ms);

    if (rmsg) {
      return rmsg;
    }

    if (eof == 1) {
      read_done = true;
    }

    rmsg = write_buffer(fd_in, &file_buf, timeout_ms);
    if (rmsg) {
      return rmsg;
    }
  }

  return NULL;
}

const char *
check_remote_proc_manager(int fd_in, int fd_out,
                          UserDirInfo * ud, const char *remote_proc_mgr_name, const char *remote_arch, int *ok)
{
  sio_buffer in_buf;
  sio_buffer out_buf;

  *ok = 0;

  const char ls_check_format[] =
    "if [ -x %s ]; then\n" "  a=`ls -l %s`\n" "  echo ${a:-error}\n" "else\n" "  echo error\n" "fi\n";

  int len = sizeof(ls_check_format) + (2 * strlen(remote_proc_mgr_name)) + 1;
  char *cmd = (char *) malloc(len);
  sprintf(cmd, ls_check_format, remote_proc_mgr_name, remote_proc_mgr_name);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  const char *rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  char *end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to ls check cmd: %s", out_buf.start());

  if (strcmp(out_buf.start(), "error") == 0) {
    TE_Warning("error checking proc_manager; replacing : %s", out_buf.start());
    return NULL;
  }

  Tokenizer ws_tok(" \t\r\n");
  int num_tok = ws_tok.Initialize(out_buf.start());

  if (num_tok < 8) {
    TE_Warning("bad ls output on proc_manager check; replacing : %s", out_buf.start());
    return NULL;
  }

  int remote_size = atoi(ws_tok[4]);

  if (remote_size == 0) {
    return NULL;
  }

  char *local_pm = find_proc_manager_binary(ud, remote_arch);

  if (local_pm == NULL) {
    // Since we don't a replacement, the exisiting one
    //   will have be ok
    TE_Warning("no process manager for arch %s found", remote_arch);
    *ok = 1;
    return NULL;
  }

  int r;
  struct stat stat_info;
  do {
    r = stat(local_pm, &stat_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    // Can not get info on local one, leave exisitng
    TE_Warning("stat on for proc_manager % : %s", local_pm, strerror(errno));
    *ok = 1;
  } else {
    // Check to see if sizes match
    //
    //  FIX: should also check to see if local package
    //    is newer than remote
    if (stat_info.st_size == remote_size) {
      *ok = 1;
    }
  }
  free(local_pm);
  local_pm = NULL;

  return NULL;
}


//
// const char* start_remote_proc_manager
//
//
const char *
start_remote_proc_manager(int &fd_in, int &fd_out,
                          UserDirInfo * ud, const char *remote_proc_mgr_name, int remote_proc_mgr_killtm)
{
  sio_buffer in_buf;
  sio_buffer out_buf;

  const char start_format[] = "./%s -r -q -d . -p %d%s%s -k %d\n";

  int len = sizeof(start_format) + strlen(remote_proc_mgr_name) + 1;
  if (ud->log_collator_arg) {
    len += 3 + strlen(ud->log_collator_arg);
  }

  char *cmd = (char *) malloc(len);
  sprintf(cmd, start_format, remote_proc_mgr_name, ud->port,
          (ud->log_collator_arg == NULL) ? "" : " -L ",
          (ud->log_collator_arg == NULL) ? "" : ud->log_collator_arg, remote_proc_mgr_killtm);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  const char *rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  char *end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to start cmd: %s", out_buf.start());

  if (strcmp(out_buf.start(), "liftoff") != 0) {
    TE_Note("failed to start proc_manager : %s", out_buf.start());
    return "remote start failed";
  }
  close(fd_in);
  fd_in = -1;

  return NULL;
}



// const char* put_and_start_proc_manager()
//
//
const char *
put_and_start_proc_manager(int &fd_in, int &fd_out,
                           UserDirInfo * ud,
                           const char *remote_proc_mgr_name, const char *remote_arch, int remote_proc_mgr_killtm)
{

  sio_buffer in_buf;
  sio_buffer out_buf;

  char *local_pm = find_proc_manager_binary(ud, remote_arch);

  if (local_pm == NULL) {
    TE_Error("no process manager for arch %s found", remote_arch);
    return "no process manager for arch";
  }

  int local_file_fd = -1;
  do {
    local_file_fd = open(local_pm, O_RDONLY);
  } while (local_file_fd < 0 && errno == EINTR);

  if (local_file_fd < 0) {
    TE_Error("unable to open %s : %s", local_pm, strerror(errno));
    free(local_pm);
    return "open of proc_manager for transfer failed";
  }

  free(local_pm);
  local_pm = NULL;

  const char put_and_start_format[] = "cat - > %s; chmod 0755 %s; ./%s -r -q -d . -p %d%s%s -k %d\n";

  int len = sizeof(put_and_start_format) + (3 * strlen(remote_proc_mgr_name)) + 32 + 1;
  if (ud->log_collator_arg) {
    len += 3 + strlen(ud->log_collator_arg);
  }

  char *cmd = (char *) malloc(len);
  sprintf(cmd, put_and_start_format, remote_proc_mgr_name,
          remote_proc_mgr_name, remote_proc_mgr_name, ud->port,
          (ud->log_collator_arg == NULL) ? "" : " -L ",
          (ud->log_collator_arg == NULL) ? "" : ud->log_collator_arg, remote_proc_mgr_killtm);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  int timeout_ms = 60 * 1000;
  const char *rmsg = write_buffer(fd_in, &in_buf, &timeout_ms);
  char *end;

  if (rmsg) {
    goto CLEANUP;
  }

  rmsg = transfer_proc_manager_binary(local_file_fd, fd_in, &timeout_ms);
  close(fd_in);
  fd_in = -1;

  if (rmsg) {
    goto CLEANUP;
  }

  rmsg = read_until(fd_out, &out_buf, '\n', &timeout_ms);

  if (rmsg) {
    goto CLEANUP;
  }

  end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to put and start: %s", out_buf.start());

  if (strcmp(out_buf.start(), "liftoff") != 0) {
    TE_Note("failed to push and start proc_manager : %s", out_buf.start());
    return "remote push_and_start failed";
  }

CLEANUP:
  if (local_file_fd >= 0) {
    close(local_file_fd);
  }

  if (rmsg) {
    TE_Error("put_and_start proc_manager failed %s", rmsg);
  }
  return rmsg;
}

const char *
handle_proc_manager(int &fd_in, int &fd_out, UserDirInfo * ud, const char *arch, int kw)
{

  sio_buffer in_buf;
  sio_buffer out_buf;

  const char proc_mgr_base[] = "proc_manager";
  int len = strlen(proc_mgr_base) + 1 + strlen(arch);

  char *proc_manager_name = (char *) malloc(len + 1);
  sprintf(proc_manager_name, "%s-%s", proc_mgr_base, arch);
  FreeOnDestruct pm_name_freer(proc_manager_name);

  const char chdir_format[] = "if cd %s\n" "then\n" "   echo ok\n" "fi\n";

  const char check_file_format[] = "if [ -e %s ]; then\n" "   echo ok\n" "else\n" "   echo not found\n" "fi\n";

  len = sizeof(chdir_format) + strlen(ud->test_stuff_path_and_dir) + 1;
  char *cmd = (char *) malloc(len);

  sprintf(cmd, chdir_format, ud->test_stuff_path_and_dir);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  const char *rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  char *end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to cd: %s", out_buf.start());

  if (strcmp(out_buf.start(), "ok") != 0) {
    TE_Note("failed to change to remote directory : %s", out_buf.start());
    return "chdir to remote stuff_path failed";
  }

  in_buf.reset();
  out_buf.reset();

  len = sizeof(check_file_format) + strlen(proc_manager_name) + 1;
  cmd = (char *) malloc(len);

  sprintf(cmd, check_file_format, proc_manager_name);

  in_buf.fill(cmd, strlen(cmd));
  free(cmd);
  cmd = NULL;

  rmsg = send_remote_cmd(&in_buf, fd_in, &out_buf, fd_out);

  if (rmsg) {
    return rmsg;
  }

  end = out_buf.memchr('\n');
  *end = '\0';
  Debug("remote", "Response to check for %s : %s", proc_manager_name, out_buf.start());

  if (strcmp(out_buf.start(), "ok") == 0) {
    int ok = 0;
    rmsg = check_remote_proc_manager(fd_in, fd_out, ud, proc_manager_name, arch, &ok);

    if (rmsg) {
      return rmsg;
    }

    if (ok) {
      Debug("remote", "proc_manager is up to date on remote");
      rmsg = start_remote_proc_manager(fd_in, fd_out, ud, proc_manager_name, kw);
    } else {
      Debug("remote", "proc_manager is out of date on remote");
      rmsg = put_and_start_proc_manager(fd_in, fd_out, ud, proc_manager_name, arch, kw);
    }

  } else {
    Debug("remote", "no proc_manager on remote");
    rmsg = put_and_start_proc_manager(fd_in, fd_out, ud, proc_manager_name, arch, kw);
  }

  return rmsg;
}

const char *
check_remote_isalive(unsigned int ip, int port, int retries = 20)
{

  RafCmd request;
  RafCmd response;
  sio_buffer read_buffer;

  request(0) = strdup("0");
  request(1) = strdup("isalive");

  const char *rmsg = "Unknown Error";
  for (int i = 0; i < retries; i++) {
    int timeout_ms = 10 * 1000;
    int fd = SIO::make_client(ip, port);
    bool success = false;

    if (fd >= 0) {
      rmsg = send_raf_cmd(fd, &request, &timeout_ms);
      if (rmsg == NULL) {
        rmsg = read_raf_resp(fd, &read_buffer, &response, &timeout_ms);
        if (rmsg == NULL) {
          if (response.length() >= 2 && *(response[1]) == '0') {
            success = true;
          } else {
            rmsg = "bad raf reply";
          }
        }
      }
    } else {
      rmsg = "connect failed";
    }

    close(fd);

    if (success) {
      Debug("remote", "remote passed 'isalive' check");
      ink_debug_assert(rmsg == NULL);
      break;
    } else {
      Debug("remote", "remote failed 'isalive' check : %s", rmsg);
      response.clear();
      read_buffer.reset();
      usleep(500 * 1000);
    }
  }

  return rmsg;
}

int
remote_start(const char *hostname, unsigned int ip, UserDirInfo * ud, int kw)
{
  int in_pipe[2];
  int out_pipe[2];

  pid_t child_pid = start_remote_shell(ud, hostname, in_pipe, out_pipe);

  if (child_pid < 0) {
    return -1;
  }

  int in_fd = in_pipe[1];
  int out_fd = out_pipe[0];
  int return_value = -1;

  char *arch = NULL;
  const char *rmsg = determine_arch(in_fd, out_fd, &arch);

  if (rmsg) {
    free(arch);
    TE_Error("remote start on %s failed : %s", hostname, rmsg);
    goto CLEANUP;
  } else {
    Debug("remote", "Remote architecture for %s is %s", hostname, arch);
  }

  rmsg = setup_remote_directories(in_fd, out_fd, ud);

  if (rmsg) {
    TE_Error("remote directory setup on %s failed : %s", hostname, rmsg);
    goto CLEANUP;
  }

  rmsg = handle_proc_manager(in_fd, out_fd, ud, arch, kw);

  if (rmsg) {
    TE_Error("proc_manager startup on %s failed %s", hostname, rmsg);
    goto CLEANUP;
  }

  rmsg = check_remote_isalive(ip, ud->port);
  if (rmsg) {
    TE_Error("proc_manager on %s failed isalive check : %s", hostname, rmsg);
  } else {
    return_value = 0;
  }

CLEANUP:

  if (in_fd >= 0) {
    int timeout_ms = 10000;
    sio_buffer close_buf;
    close_buf.fill("exit\n", 5);
    write_buffer(in_fd, &close_buf, &timeout_ms);
    close(in_fd);
  }

  if (out_fd >= 0) {
    close(out_fd);
  }

  if (child_pid > 0) {
    int exit_status;
    reap_and_kill_child(child_pid, &exit_status);
    child_pid = -1;
  }

  if (arch) {
    free(arch);
  }

  return return_value;
}
