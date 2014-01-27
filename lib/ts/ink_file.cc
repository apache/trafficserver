/** @file

  File manipulation routines for libts

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

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

int
ink_fputln(FILE * stream, const char *s)
{
  if (stream && s) {
    int rc = fputs(s, stream);
    if (rc > 0)
      rc += fputc('\n', stream);
    return rc;
  }
  else
    return -EINVAL;
}                               /* End ink_fgets */

/*---------------------------------------------------------------------------*

  int ink_file_fd_readline(int fd, int bufsz, char *buf)

  This routine reads bytes from <fd> into the buffer pointed to by <buf>.
  The reading stops when (a) a LF is read into the buffer, (b) the end of
  file is reached, or (c) <bufsz> - 1 bytes are read.  The <bufsz> parameter
  must be >= 2.

  The data pointed to by <buf> is always NUL terminated, and the LF is
  left in the data.  This routine can be used as a replacement for
  fgets-like functions.  If the <bufsz> is too small to hold a complete line,
  the partial line will be stored, and subsequent reads will read more data.

  This routine returns the number of bytes read, 0 on end of file, or
  a negative errno on error.

 *---------------------------------------------------------------------------*/

int
ink_file_fd_readline(int fd, int bufsz, char *buf)
{
  char c;
  int i = 0;

  if (bufsz < 2)
    return (-EINVAL);           /* bufsz must by >= 2 */

  while (i < bufsz - 1) {       /* leave 1 byte for NUL */
    int n = read(fd, &c, 1);    /* read 1 byte */
    if (n == 0)
      break;                    /* EOF */
    if (n < 0)
      return (n);               /* error */
    buf[i++] = c;               /* store in buffer */
    if (c == '\n')
      break;                    /* stop if stored a LF */
  }

  buf[i] = '\0';                /* NUL terminate buffer */
  return (i);                   /* number of bytes read */
}                               /* End ink_file_fd_readline */

/* Write until NUL */
int
ink_file_fd_writestring(int fd, const char *buf)
{
  int len, i = 0;

  if (buf && (len = strlen(buf)) > 0 && (i = (int) write(fd, buf, (size_t) len) != len))
    i = -1;

  return i;                     /* return chars written */
}                               /* End ink_file_fd_writestring */

int
ink_filepath_merge(char *path, int pathsz, const char *rootpath, const char *addpath, int flags)
{
  size_t rootlen; // is the length of the src rootpath
  size_t maxlen;  // maximum total path length
  size_t keptlen; // is the length of the retained rootpath
  size_t pathlen; // is the length of the result path
  size_t seglen;  // is the end of the current segment
  char curdir[PATH_NAME_MAX];

  /* Treat null as an empty path.
  */
  if (!addpath)
    addpath = "";

  if (addpath[0] == '/') {
    // If addpath is rooted, then rootpath is unused.
    // Ths violates any INK_FILEPATH_SECUREROOTTEST and
    // INK_FILEPATH_NOTABSOLUTE flags specified.
    //
    if (flags & INK_FILEPATH_SECUREROOTTEST)
      return EACCES; // APR_EABOVEROOT;
    if (flags & INK_FILEPATH_NOTABSOLUTE)
      return EISDIR; // APR_EABSOLUTE;

    // If INK_FILEPATH_NOTABOVEROOT wasn't specified,
    // we won't test the root again, it's ignored.
    // Waste no CPU retrieving the working path.
    //
    if (!rootpath && !(flags & INK_FILEPATH_NOTABOVEROOT))
      rootpath = "";
  }
  else {
    // If INK_FILEPATH_NOTABSOLUTE is specified, the caller
    // requires a relative result.  If the rootpath is
    // ommitted, we do not retrieve the working path,
    // if rootpath was supplied as absolute then fail.
    //
    if (flags & INK_FILEPATH_NOTABSOLUTE) {
      if (!rootpath)
        rootpath = "";
      else if (rootpath[0] == '/')
        return EISDIR; //APR_EABSOLUTE;
    }
  }
  if (!rootpath) {
    // Start with the current working path.  This is bass akwards,
    // but required since the compiler (at least vc) doesn't like
    // passing the address of a char const* for a char** arg.
    //
    if (!getcwd(curdir, sizeof(curdir))) {
      return errno;
    }
    rootpath = curdir;
  }
  rootlen = strlen(rootpath);
  maxlen = rootlen + strlen(addpath) + 4; // 4 for slashes at start, after
                                          // root, and at end, plus trailing
                                          // null
  if (maxlen > (size_t)pathsz) {
    return E2BIG; //APR_ENAMETOOLONG;
  }
  if (addpath[0] == '/') {
    // Ignore the given root path, strip off leading
    // '/'s to a single leading '/' from the addpath,
    // and leave addpath at the first non-'/' character.
    //
    keptlen = 0;
    while (addpath[0] == '/')
      ++addpath;
    path[0] = '/';
    pathlen = 1;
  }
  else {
    // If both paths are relative, fail early
    //
    if (rootpath[0] != '/' && (flags & INK_FILEPATH_NOTRELATIVE))
      return EBADF; //APR_ERELATIVE;

    // Base the result path on the rootpath
    //
    keptlen = rootlen;
    memcpy(path, rootpath, rootlen);

    // Always '/' terminate the given root path
    //
    if (keptlen && path[keptlen - 1] != '/') {
      path[keptlen++] = '/';
    }
    pathlen = keptlen;
  }

  while (*addpath) {
    // Parse each segment, find the closing '/'
    //
    const char *next = addpath;
    while (*next && (*next != '/')) {
      ++next;
    }
    seglen = next - addpath;

    if (seglen == 0 || (seglen == 1 && addpath[0] == '.')) {
      // noop segment (/ or ./) so skip it
      //
    }
    else if (seglen == 2 && addpath[0] == '.' && addpath[1] == '.') {
      // backpath (../)
      if (pathlen == 1 && path[0] == '/') {
        // Attempt to move above root.  Always die if the
        // INK_FILEPATH_SECUREROOTTEST flag is specified.
        //
        if (flags & INK_FILEPATH_SECUREROOTTEST) {
          return EACCES; //APR_EABOVEROOT;
        }

        // Otherwise this is simply a noop, above root is root.
        // Flag that rootpath was entirely replaced.
        //
        keptlen = 0;
      }
      else if (pathlen == 0
               || (pathlen == 3
               && !memcmp(path + pathlen - 3, "../", 3))
               || (pathlen  > 3
               && !memcmp(path + pathlen - 4, "/../", 4))) {
        // Path is already backpathed or empty, if the
        // INK_FILEPATH_SECUREROOTTEST.was given die now.
        //
        if (flags & INK_FILEPATH_SECUREROOTTEST) {
          return EACCES; //APR_EABOVEROOT;
        }

        // Otherwise append another backpath, including
        // trailing slash if present.
        //
        memcpy(path + pathlen, "../", *next ? 3 : 2);
        pathlen += *next ? 3 : 2;
      }
      else {
        // otherwise crop the prior segment
        //
        do {
            --pathlen;
        } while (pathlen && path[pathlen - 1] != '/');
      }

      // Now test if we are above where we started and back up
      // the keptlen offset to reflect the added/altered path.
      //
      if (pathlen < keptlen) {
        if (flags & INK_FILEPATH_SECUREROOTTEST) {
          return EACCES; //APR_EABOVEROOT;
        }
        keptlen = pathlen;
      }
    }
    else {
        // An actual segment, append it to the destination path
        //
      if (*next) {
        seglen++;
      }
      memcpy(path + pathlen, addpath, seglen);
      pathlen += seglen;
    }

    // Skip over trailing slash to the next segment
    //
    if (*next) {
      ++next;
    }

    addpath = next;
  }
  path[pathlen] = '\0';
  if (pathlen > 1 && path[pathlen - 1] == '/') {
    // Trim trailing slash unless requested
    size_t es = strlen(addpath);
    if (es == 0 || addpath[es - 1] != '/') {
      --pathlen;
      path[pathlen] = '\0';
    }
  }

  // keptlen will be the rootlen unless the addpath contained
  // backpath elements.  If so, and INK_FILEPATH_NOTABOVEROOT
  // is specified (INK_FILEPATH_SECUREROOTTEST was caught above),
  // compare the original root to assure the result path is
  // still within given root path.
  //
  if ((flags & INK_FILEPATH_NOTABOVEROOT) && keptlen < rootlen) {
    if (strncmp(rootpath, path, rootlen)) {
      return EACCES; //APR_EABOVEROOT;
    }
    if (rootpath[rootlen - 1] != '/'
        && path[rootlen] && path[rootlen] != '/') {
      return EACCES; //APR_EABOVEROOT;
    }
  }

  return 0;
}

int
ink_filepath_make(char *path, int pathsz, const char *rootpath, const char *addpath)
{
  size_t rootlen; // is the length of the src rootpath
  size_t maxlen;  // maximum total path length

  /* Treat null as an empty path.
  */
  if (!addpath)
    addpath = "";

  if (addpath[0] == '/') {
    // If addpath is rooted, then rootpath is unused.
    ink_strlcpy(path, addpath, pathsz);
    return 0;
  }
  if (!rootpath || !*rootpath) {
    // If there's no rootpath return the addpath
    ink_strlcpy(path, addpath, pathsz);
    return 0;
  }
  rootlen = strlen(rootpath);
  maxlen  = strlen(addpath) + 2;
  if (maxlen > (size_t)pathsz) {
    *path = '\0';
    return (int)maxlen;
  }
  ink_strlcpy(path, rootpath, pathsz);
  path += rootlen;
  pathsz -= rootlen;
  if (*(path - 1) != '/') {
    *(path++) = '/';
    --pathsz;
  }
  ink_strlcpy(path, addpath, pathsz);
  return 0;
}

int
ink_file_fd_zerofill(int fd, off_t size)
{
  // Clear the file by truncating it to zero and then to the desired size.
  if (ftruncate(fd, 0) < 0) {
    return errno;
  }

  // ZFS does not implement posix_fallocate() and fails with EINVAL. As a general workaround,
  // just fall back to ftrucate if the preallocation fails.
#if HAVE_POSIX_FALLOCATE
  if (posix_fallocate(fd, 0, size) == 0) {
    return 0;
  }
#endif

  if (ftruncate(fd, size) < 0) {
    return errno;
  }

  return 0;
}

bool
ink_file_is_directory(const char * path)
{
  struct stat sbuf;

  if (stat(path, &sbuf) == -1) {
    return false;
  }

  return S_ISDIR(sbuf.st_mode);
}
