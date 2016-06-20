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

#include "ts/ink_platform.h"
#include "ts/ink_lockfile.h"

#define LOCKFILE_BUF_LEN 16 // 16 bytes should be enought for a pid

int
Lockfile::Open(pid_t *holding_pid)
{
  char buf[LOCKFILE_BUF_LEN];
  pid_t val;
  int err;
  *holding_pid = 0;

#define FAIL(x)  \
  {              \
    if (fd > 0)  \
      close(fd); \
    return (x);  \
  }

  struct flock lock;
  char *t;
  int size;

  fd = -1;

  // Try and open the Lockfile. Create it if it does not already
  // exist.
  do {
    fd = open(fname, O_RDWR | O_CREAT, 0644);
  } while ((fd < 0) && (errno == EINTR));

  if (fd < 0)
    return (-errno);

  // Lock it. Note that if we can't get the lock EAGAIN will be the
  // error we receive.
  lock.l_type   = F_WRLCK;
  lock.l_start  = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len    = 0;

  do {
    err = fcntl(fd, F_SETLK, &lock);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
    // We couldn't get the lock. Try and read the process id of the
    // process holding the lock from the lockfile.
    t = buf;

    for (size = 15; size > 0;) {
      do {
        err = read(fd, t, size);
      } while ((err < 0) && (errno == EINTR));

      if (err < 0)
        FAIL(-errno);
      if (err == 0)
        break;

      size -= err;
      t += err;
    }

    *t = '\0';

    // coverity[secure_coding]
    if (sscanf(buf, "%d\n", (int *)&val) != 1) {
      *holding_pid = 0;
    } else {
      *holding_pid = val;
    }
    FAIL(0);
  }
  // If we did get the lock, then set the close on exec flag so that
  // we don't accidently pass the file descriptor to a child process
  // when we do a fork/exec.
  do {
    err = fcntl(fd, F_GETFD, 0);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0)
    FAIL(-errno);

  val = err | FD_CLOEXEC;

  do {
    err = fcntl(fd, F_SETFD, val);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0)
    FAIL(-errno);

  // Return the file descriptor of the opened lockfile. When this file
  // descriptor is closed the lock will be released.

  return (1); // success

#undef FAIL
}

int
Lockfile::Get(pid_t *holding_pid)
{
  char buf[LOCKFILE_BUF_LEN];
  int err;
  *holding_pid = 0;

  fd = -1;

  // Open the Lockfile and get the lock. If we are successful, the
  // return value will be the file descriptor of the opened Lockfile.
  err = Open(holding_pid);
  if (err != 1)
    return err;

  if (fd < 0) {
    return -1;
  }
  // Truncate the Lockfile effectively erasing it.
  do {
    err = ftruncate(fd, 0);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
    close(fd);
    return (-errno);
  }
  // Write our process id to the Lockfile.
  snprintf(buf, sizeof(buf), "%d\n", (int)getpid());

  do {
    err = write(fd, buf, strlen(buf));
  } while ((err < 0) && (errno == EINTR));

  if (err != (int)strlen(buf)) {
    close(fd);
    return (-errno);
  }

  return (1); // success
}

void
Lockfile::Close(void)
{
  if (fd != -1) {
    close(fd);
  }
}

//-------------------------------------------------------------------------
// Lockfile::Kill() and Lockfile::KillAll()
//
// Open the lockfile. If we succeed it means there was no process
// holding the lock. We'll just close the file and release the lock
// in that case. If we don't succeed in getting the lock, the
// process id of the process holding the lock is returned. We
// repeatedly send the KILL signal to that process until doing so
// fails. That is, until kill says that the process id is no longer
// valid (we killed the process), or that we don't have permission
// to send a signal to that process id (the process holding the lock
// is dead and a new process has replaced it).
//
// INKqa11325 (Kevlar: linux machine hosed up if specific threads
// killed): Unfortunately, it's possible on Linux that the main PID of
// the process has been successfully killed (and is waiting to be
// reaped while in a defunct state), while some of the other threads
// of the process just don't want to go away.
//-------------------------------------------------------------------------

static void
lockfile_kill_internal(pid_t init_pid, int init_sig, pid_t pid, const char * /* pname ATS_UNUSED */, int sig)
{
  int err;
  int status;

  if (init_sig > 0) {
    kill(init_pid, init_sig);
    // Wait for children to exit
    do {
      err = waitpid(-1, &status, WNOHANG);
      if (err == -1)
        break;
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  do {
    err = kill(pid, sig);
  } while ((err == 0) || ((err < 0) && (errno == EINTR)));
}

void
Lockfile::Kill(int sig, int initial_sig, const char *pname)
{
  int err;
  int pid;
  pid_t holding_pid;

  err = Open(&holding_pid);
  if (err == 1) // success getting the lock file
  {
    Close();
  } else if (err == 0) // someone else has the lock
  {
    pid = holding_pid;
    if (pid != 0) {
      lockfile_kill_internal(pid, initial_sig, pid, pname, sig);
    }
  }
}

void
Lockfile::KillGroup(int sig, int initial_sig, const char *pname)
{
  int err;
  pid_t pid;
  pid_t holding_pid;
  pid_t self = getpid();

  err = Open(&holding_pid);
  if (err == 1) // success getting the lock file
  {
    Close();
  } else if (err == 0) // someone else has the lock
  {
    do {
      pid = getpgid(holding_pid);
    } while ((pid < 0) && (errno == EINTR));

    if ((pid < 0) || (pid == self)) {
      // Error getting process group,
      // or we are the group's owner.
      // Let's kill just holding_pid
      pid = holding_pid;
    } else if (pid != self) {
      // We managed to get holding_pid's process group
      // and this group is not ours.
      // This way, we kill the process_group:
      pid = -pid;
    }

    if (pid != 0) {
      // In order to get core files from each process, please
      // set your core_pattern appropriately.
      lockfile_kill_internal(holding_pid, initial_sig, pid, pname, sig);
    }
  }
}
