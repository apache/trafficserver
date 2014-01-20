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

/**************************************************************************

  ink_process.cc

  Generic process interface.
**************************************************************************/
#include "ink_process.h"
#include "ink_resource.h"
#include "ink_memory.h"

#include <errno.h>
#include <spawn.h>
#include <map>
#include <sys/wait.h>

#if defined(darwin)
# include <crt_externs.h>
#endif

typedef std::map<FILE *, pid_t> FilePidMap;
typedef std::map<FILE *, pid_t>::iterator FilePidMapIter;

static FilePidMap ink_popen_file_to_pid;

static void
convert_string_to_argv(const char *cmdLine, char ***argv)
{
  char *str1, *token;
  char *saveptr;
  int argc = 0;
  char * cmdLineCopy = ats_strdup(cmdLine);
  char **newArgv;
  newArgv = static_cast<char **>(ats_malloc(sizeof(char *)));
  newArgv[0] = NULL;

  for ( str1 = cmdLineCopy; ; str1 = NULL) {
    token = strtok_r(str1, " \t", &saveptr);
    if (token == NULL)
      break;
    newArgv[argc++] = ats_strdup(token);
    newArgv = static_cast<char **>(ats_realloc(newArgv,(argc+1) * sizeof(char *)));
    newArgv[argc] = NULL;
  }
  ats_free(cmdLineCopy);

  *argv = newArgv;
}

FILE *
ink_popen(const char *progname, const char *mode)
{
  char **argv;

  convert_string_to_argv(progname,&argv);
  
  // check to see if we can access the exec
  if (access(argv[0], X_OK) == -1) {
    /* chdir() in child wouldn't have worked */
    int save = errno; // Error() could change errno
    errno = save;
    return NULL;
  }

  // validate mode
  if (*mode != 'r' && *mode != 'w') {
    errno = EINVAL;
    return NULL;
  }
  // create read[0] & write[1] ends of pipe
  int fds[2];
  if (-1 == pipe(fds)) {
    return NULL;
  }

  pid_t pid;
  FILE *fp;

  // Redirect new standard input (fd 0) or output(fd 1) 
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  if (*mode == 'r') { // map stdout from child process to readable fd 
    posix_spawn_file_actions_adddup2(&file_actions, fds[1], STDOUT_FILENO);
    fp = fdopen(fds[0], "r");
  } else { // map stdin to child process to a writable fd
    posix_spawn_file_actions_adddup2(&file_actions, fds[0], STDIN_FILENO);
    fp = fdopen(fds[1], "w");
  }
  posix_spawn_file_actions_addclose(&file_actions, fds[0]);
  posix_spawn_file_actions_addclose(&file_actions, fds[1]);

  int status;

  char **env = NULL;
#if defined(linux)
  env = environ;
#elif defined(darwin)
  env = *_NSGetEnviron();
#endif

  // spawn the new process
  status = posix_spawn(&pid, argv[0], &file_actions, NULL, argv, env);
  posix_spawn_file_actions_destroy(&file_actions);
  // close unused fds
  if (*mode == 'r') {
    close(fds[1]); // close unused write end 
  } else {
    close(fds[0]); // close unused read end 
  }
  // check status of posix_spawn
  if (status == 0) {
    ink_popen_file_to_pid[fp] = pid;
  } else { // failed to spawn, clean up
    fclose(fp);
    fp = NULL;
    errno = status;
  }

  // free up argv
  for(int i=0;argv[i];i++) {
    ats_free(argv[i]);
  }
  ats_free(argv);

  return fp;
}

int
ink_pclose(FILE *f)
{
  int iReturn = -1;
  // make sure the FILE * is valid for us
  FilePidMapIter it = ink_popen_file_to_pid.find(f);
  if (it == ink_popen_file_to_pid.end()) {
    errno = ECHILD;
    return -1;
  }

  pid_t pid = it->second;
  int status;

  // wait for process to exit
  if (waitpid(pid, &status, 0) != -1) {
    iReturn = fclose(f);
  } else { // some sort of error occured errno has reason
    iReturn = -1;
  }

  // removing our mapping
  ink_popen_file_to_pid.erase(it);
  return iReturn;
}
