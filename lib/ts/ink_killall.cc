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

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#include "libts.h"

#if defined(linux)

#include "ink_killall.h"
#include "ink_resource.h"

#define PROC_BASE "/proc"

#define INITIAL_PIDVSIZE 32

int
ink_killall(const char *pname, int sig)
{

  int err;
  pid_t *pidv;
  int pidvcnt;

  if (ink_killall_get_pidv_xmalloc(pname, &pidv, &pidvcnt) < 0) {
    return -1;
  }

  if (pidvcnt == 0) {
    ats_free(pidv);
    return 0;
  }

  err = ink_killall_kill_pidv(pidv, pidvcnt, sig);
  ats_free(pidv);

  return err;

}

int
ink_killall_get_pidv_xmalloc(const char *pname, pid_t ** pidv, int *pidvcnt)
{

  DIR *dir;
  FILE *fp;
  struct dirent *de;
  pid_t pid, self;
  char buf[LINE_MAX], *p, *comm;
  int pidvsize = INITIAL_PIDVSIZE;

  if (!pname || !pidv || !pidvcnt)
    goto l_error;

  self = getpid();
  if (!(dir = opendir(PROC_BASE)))
    goto l_error;

  *pidvcnt = 0;
  *pidv = (pid_t *)ats_malloc(pidvsize * sizeof(pid_t));

  while ((de = readdir(dir))) {
    if (!(pid = (pid_t) atoi(de->d_name)) || pid == self)
      continue;
    snprintf(buf, sizeof(buf), PROC_BASE "/%d/stat", pid);
    if ((fp = fopen(buf, "r"))) {
      if (fgets(buf, sizeof buf, fp) == 0)
        goto l_close;
      if ((p = strchr(buf, '('))) {
        comm = p + 1;
        if ((p = strchr(comm, ')')))
          *p = '\0';
        else
          goto l_close;
        if (strcmp(comm, pname) == 0) {
          if (*pidvcnt >= pidvsize) {
            pid_t *pidv_realloc;
            pidvsize *= 2;
            if (!(pidv_realloc = (pid_t *)ats_realloc(*pidv, pidvsize * sizeof(pid_t)))) {
              ats_free(*pidv);
              goto l_error;
            } else {
              *pidv = pidv_realloc;
            }
          }
          (*pidv)[*pidvcnt] = pid;
          (*pidvcnt)++;
        }
      }
    l_close:
      fclose(fp);
    }
  }
  closedir(dir);

  if (*pidvcnt == 0) {
    ats_free(*pidv);
    *pidv = 0;
  }

  return 0;

l_error:
  *pidv = NULL;
  *pidvcnt = 0;
  return -1;
}

int
ink_killall_kill_pidv(pid_t * pidv, int pidvcnt, int sig)
{

  int err = 0;

  if (!pidv || (pidvcnt <= 0))
    return -1;

  while (pidvcnt > 0) {
    pidvcnt--;
    if (kill(pidv[pidvcnt], sig) < 0)
      err = -1;
  }

  return err;

}

#endif  // linux check
