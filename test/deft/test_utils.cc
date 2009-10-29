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

   test_utils.cc

   Description:

   
 ****************************************************************************/

#include <strings.h>
#include <errno.h>
#include <sys/utsname.h>

#include "Diags.h"
#include "ink_bool.h"
#include "rafencode.h"

#include "test_utils.h"
#include "sio_buffer.h"
#include "sio_loop.h"
#include "raf_cmd.h"

// char** build_argv(const char* arg0, const char* rest)
//    
//   Uses rafdecode to handle quoting on arg 'rest'
//
char **
build_argv(const char *arg0, const char *rest)
{

  int argc = 1;

  const char *cur = rest;

  char *array_default = NULL;
  DynArray < char *>args(&array_default);

  if (rest) {
    const char *rest_end = rest + strlen(rest);
    while (cur < rest_end) {
      const char *lastp;

      int n = raf_decodelen(cur, rest_end - cur, &lastp);

      if (n > 0) {
        const char *lastp2;
        char *new_arg = (char *) malloc(n + 1);
        raf_decode(cur, rest_end - cur, new_arg, n, &lastp2);
        new_arg[n] = '\0';
        ink_debug_assert(lastp == lastp2);
        args(argc - 1) = new_arg;
        argc++;
      }
      cur = lastp;
    }
  }

  char **argv = (char **) malloc(sizeof(char *) * (argc + 1));

  argv[0] = strdup(arg0);

  if (rest) {
    for (int i = 1; i < argc; i++) {
      argv[i] = args[i - 1];
    }
  }

  argv[argc] = NULL;

  return argv;
}

char **
build_argv_v(const char *arg0, ...)
{

  int argc = 1;

  va_list vlist;
  va_start(vlist, arg0);

  const char *argX = va_arg(vlist, const char *);

  while (argX != NULL) {
    argc++;
    argX = va_arg(vlist, const char *);
  }
  va_end(vlist);

  char **argv = (char **) malloc(sizeof(char *) * (argc + 1));

  argv[0] = strdup(arg0);

  va_start(vlist, arg0);

  for (int i = 1; i < argc; i++) {
    argX = va_arg(vlist, const char *);
    argv[i] = strdup(argX);
  }
  va_end(vlist);

  argv[argc] = NULL;

  return argv;
}

// char** append_argv(char** argv1, const char** argv2)
//
//   Creates and returns new argv that contains both
//      args from argv1 and argv2.  Argv1 strings are
//      references and argv2's string are copied.
//   argv1 is freed
//
//   Caller frees return value
//
char **
append_argv(char **argv1, char **argv2)
{

  int size1 = 0;
  int size2 = 0;
  char **tmp = argv1;

  while (*tmp != NULL) {
    size1++;
    tmp++;
  }

  tmp = argv2;

  while (*tmp != NULL) {
    size2++;
    tmp++;
  }

  char **new_argv = (char **) malloc((size1 + size2 + 1) * sizeof(char *));

  int i = 0;
  for (i = 0; i < size1; i++) {
    new_argv[i] = argv1[i];
  }

  for (i = 0; i < size2; i++) {
    new_argv[size1 + i] = strdup(argv2[i]);
  }

  new_argv[size1 + size2] = NULL;

  free(argv1);

  return new_argv;
}


void
destroy_argv(char **argv)
{
  if (argv) {
    char **tmp = argv;
    while (*tmp) {
      free(*tmp);
      tmp++;
    }
    free(argv);
  }
}


static const char *
backup_to_next_dot(const char *start, const char *end)
{

  while (--end >= start) {
    if (*end == '.') {
      return end;
    }
  }

  return NULL;
}

int
check_package_file_extension(const char *file_name, const char **ext_ptr)
{

  // We only accept .tgz & .tar.gz
  bool found = false;
  const char *r_ptr = NULL;

  int len = strlen(file_name);

  const char *ext = backup_to_next_dot(file_name, file_name + len);

  if (ext) {
    if (strcmp(ext, ".tgz") == 0) {
      r_ptr = ext;
      found = true;
    } else if (strcmp(ext, ".gz") == 0) {
      const char *tar_ext = backup_to_next_dot(file_name, ext);

      if (tar_ext && (ext - tar_ext) == 4) {
        if (memcmp(tar_ext, ".tar", 4) == 0) {
          r_ptr = tar_ext;
          found = true;
        }
      }
    }
  }

  if (ext_ptr) {
    *ext_ptr = r_ptr;
  }

  if (found == true && r_ptr > file_name) {
    return 0;
  } else {
    return 1;
  }
}

const char *
write_buffer(int fd, sio_buffer * buf, int *timeout_ms)
{

  int bytes_sent = 0;

  while (buf->read_avail() > 0) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    ink_hrtime poll_start = ink_get_based_hrtime_internal();

    int r;
    do {
      r = poll(&pfd, 1, *timeout_ms);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));

    ink_hrtime poll_end = ink_get_based_hrtime_internal();

    *timeout_ms -= ink_hrtime_to_msec(poll_end - poll_start);

    if (*timeout_ms < 0) {
      *timeout_ms = 0;
    }

    if (r < 0) {
      Error("write_buffer: poll failed: %s", strerror(errno));
      return "poll failed";
    } else if (r == 0) {
      return "write timeout";
    } else {
      do {
        r = write(fd, buf->start(), buf->read_avail());
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        if (errno == EAGAIN) {
          // Try again
          continue;
        } else {
          Error("write_buffer: write failed: %s", strerror(errno));
          return "write failed";
        }
      } else {
        bytes_sent += r;
        buf->consume(r);
      }
    }
  }

  Debug("net", "successfully sent %d bytes", bytes_sent);

  return NULL;
}

const char *
read_until(int fd, sio_buffer * read_buffer, char end_chr, int *timeout_ms)
{

  int bytes_read = 0;

  const char *resp_end;
  while ((resp_end = read_buffer->memchr(end_chr)) == NULL) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    ink_hrtime poll_start = ink_get_based_hrtime_internal();

    int r;
    do {
      r = poll(&pfd, 1, *timeout_ms);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));

    ink_hrtime poll_end = ink_get_based_hrtime_internal();

    *timeout_ms -= ink_hrtime_to_msec(poll_end - poll_start);

    if (*timeout_ms < 0) {
      *timeout_ms = 0;
    }

    if (r < 0) {
      Error("read_until: poll failed: %s", strerror(errno));
      return "poll failed";
    } else if (r == 0) {
      return "read timeout";
    } else {
      int avail = read_buffer->expand_to(2048);
      do {
        r = read(fd, read_buffer->end(), avail);
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        if (errno == EAGAIN) {
          // Try again
          continue;
        } else {
          Error("read_until: read failed: %s", strerror(errno));
          return "read failed";
        }
      } else if (r == 0) {
        Error("read_until: read eof");
        return "read eof";
      } else {
        bytes_read += r;
        read_buffer->fill(r);
      }
    }
  }

  Debug("net", "successfully read %d bytes", bytes_read);

  return NULL;
}

const char *
read_to_buffer(int fd, sio_buffer * read_buffer, int nbytes, int *eof, int *timeout_ms)
{

  int bytes_read = 0;
  *eof = 0;

  while (bytes_read < nbytes) {

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    ink_hrtime poll_start = ink_get_based_hrtime_internal();

    int r;
    do {
      r = poll(&pfd, 1, *timeout_ms);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));

    ink_hrtime poll_end = ink_get_based_hrtime_internal();

    *timeout_ms -= ink_hrtime_to_msec(poll_end - poll_start);

    if (*timeout_ms < 0) {
      *timeout_ms = 0;
    }

    if (r < 0) {
      Error("read_to_buffer: poll failed: %s", strerror(errno));
      return "poll failed";
    } else if (r == 0) {
      return "read timeout";
    } else {
      int avail = read_buffer->expand_to(nbytes - bytes_read);
      do {
        r = read(fd, read_buffer->end(), avail);
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        if (errno == EAGAIN) {
          // Try again
          continue;
        } else {
          Error("read_until: read failed: %s", strerror(errno));
          return "read failed";
        }
      } else if (r == 0) {
        *eof = 1;
        return NULL;
      } else {
        bytes_read += r;
        read_buffer->fill(r);
      }
    }
  }

  Debug("net", "successfully read %d bytes", bytes_read);

  return NULL;
}

const char *
send_raf_cmd(int fd, RafCmd * request, int *timeout_ms)
{

  sio_buffer request_buffer;
  request->build_message(&request_buffer);

  if (is_debug_tag_set("raf")) {
    request_buffer.expand_to(1);
    *(request_buffer.end()) = '\0';
  }
  Debug("raf", "sending raf request: %s", request_buffer.start());

  return write_buffer(fd, &request_buffer, timeout_ms);
}

const char *
read_raf_resp(int fd, sio_buffer * read_buffer, RafCmd * response, int *timeout_ms)
{

  const char *rmsg = read_until(fd, read_buffer, '\n', timeout_ms);

  if (rmsg) {
    return rmsg;
  }

  char *cmd_start = read_buffer->start();
  char *cmd_end = read_buffer->memchr('\n');
  int cmd_len = cmd_end - cmd_start;
  *cmd_end = '\0';
  Debug("raf", "read raf response: %s", read_buffer->start());

  response->clear();
  response->process_cmd(cmd_start, cmd_len);

  read_buffer->consume(cmd_len + 1);

  return NULL;
}


const char *
create_or_verify_dir(const char *dir, int *error_code)
{

  struct stat dir_info;

  int r;
  do {
    r = stat(dir, &dir_info);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (errno == ENOENT) {
      do {
        r = mkdir(dir, 0755);
      } while (r < 0 && errno == EINTR);

      if (r < 0) {
        *error_code = errno;
        return "Unable to create directory";
      } else {
        Debug("dir", "Created directory %s", dir);
      }
    } else {
      *error_code = errno;
      return "Can not access directory";
    }
  } else {

    if (!(dir_info.st_mode & S_IFDIR)) {
      *error_code = 0;
      return "is not a directory";
    }
  }

  do {
    r = access(dir, R_OK | W_OK | X_OK | F_OK);
  } while (r < 0 && errno == EINTR);

  if (r < 0) {
    if (errno == EACCES) {
      *error_code = errno;
      return "insufficinet permissions on directory";
    } else {
      return "access permissions on stuff directory";
    }
  }

  *error_code = 0;
  return NULL;
}


// char* get_arch_str()
//   caller frees return value
//
char *
get_arch_str()
{

  struct utsname uname_info;

  uname(&uname_info);
  if (strcmp(uname_info.sysname, "SunOS") == 0 && strcmp(uname_info.machine, "i86pc") == 0) {
    return strdup("SunOSx86");
  } else {
    return strdup(uname_info.sysname);
  }
}
