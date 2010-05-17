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

  ink_file.c

  File manipulation routines for libinktomi.a.

 ****************************************************************************/

#include "inktomi++.h"

DIR *
ink_opendir(const char *path)
{
  return opendir(path);
}

int
ink_closedir(DIR * d)
{
  return closedir(d);
}

int
ink_access_extension(char *base, char *ext, int amode)
{
  char name[PATH_MAX];

  snprintf(name, sizeof(name) - 1, "%s%s", base, ext);
  name[sizeof(name) - 1] = 0;
  return (access(name, amode));
}                               /* End ink_access_extension */

int
ink_readdir_r(DIR * dirp, struct dirent *entry, struct dirent **pentry)
{
#if (HOST_OS == freebsd)
  // this is safe for threads not accessing the same file
  // but not for interlaced accesses to the same file
  // those make little sense anyway
  // and are not used in Traffic Server
  struct dirent *d = readdir(dirp);
  if (!d) {
    errno = ENOENT;
    return -1;
  }
  memcpy(entry, d, d->d_reclen);
  *pentry = entry;
  return 0;
#else
  return readdir_r(dirp, entry, pentry);
#endif
}

FILE *
ink_fopen_extension(char *base, char *ext, char *mode)
{
  FILE *fp;
  char name[PATH_MAX];

  snprintf(name, sizeof(name) - 1, "%s%s", base, ext);
  name[sizeof(name) - 1] = 0;
  fp = ink_fopen(name, mode);
  return (fp);
}                               /* End ink_open_extension */


FILE *
ink_fopen(char *name, char *mode)
{
  FILE *fp;

  if ((fp = fopen(name, mode)) == NULL) {
    ink_fatal(1, "ink_fopen: can't open file '%s' for mode '%s'", name, mode);
  }
  return (fp);
}                               /* End ink_fopen */


void
ink_fclose(FILE * fp)
{
  int status;

  status = fclose(fp);
  if (status != 0) {
    ink_fatal(1, "ink_fclose: can't close file pointer");
  }
}                               /* End ink_fclose */


void
ink_fseek(FILE * stream, long offset, int ptrname)
{
  int status;

  status = fseek(stream, offset, ptrname);
  if (status != 0) {
    ink_fatal(1, "ink_fseek: can't seek");
  }
}                               /* End ink_fseek */


long
ink_ftell(FILE * stream)
{
  long status;

  status = ftell(stream);
  if (status < 0) {
    ink_fatal(1, "ink_ftell: ftell returns %ld for stream %ld", status, (long) stream);
  }
  return (status);
}                               /* End ink_ftell */


void
ink_rewind(FILE * stream)
{
  rewind(stream);
}                               /* End ink_rewind */


char *
ink_fgets(char *s, int n, FILE * stream)
{
  char *p;

  p = fgets(s, n, stream);
  if (p == NULL) {
    ink_fatal(1, "ink_fgets: fgets returned NULL reading %d bytes", n);
  }
  return (p);
}                               /* End ink_fgets */

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

size_t
ink_fread(void *ptr, size_t size, size_t nitems, FILE * stream)
{
  size_t s;

  s = fread(ptr, size, nitems, stream);
  if (s == 0) {
    ink_fatal(1, "ink_fread: fread(%lu,%u,%u,%lu) returned status %d", (long) ptr, size, nitems, (long) stream, s);
  }
  return (s);
}                               /* End ink_fread */


size_t
ink_fwrite(void *ptr, size_t size, size_t nitems, FILE * stream)
{
  size_t s;

  s = fwrite(ptr, size, nitems, stream);
  if (s == 0) {
    ink_fatal(1, "ink_fwrite: fwrite(%lu,%u,%u,%lu) returned status %d", (long) ptr, size, nitems, (long) stream, s);
  }
  return (s);
}                               /* End ink_fwrite */


int
ink_file_name_mtime(char *path, ink_time_t * tp)
{
  int s;
  struct stat sbuf;

  s = stat(path, &sbuf);
  if (s != 0)
    return (0);
  *tp = sbuf.st_mtime;
  return (1);
}                               /* End ink_file_name_mtime */


int
ink_file_name_size(char *path, off_t * op)
{
  int s;
  struct stat sbuf;

  s = stat(path, &sbuf);
  if (s != 0)
    return (0);
  *op = sbuf.st_size;
  return (1);
}                               /* End ink_file_name_size */


int
ink_file_lock_raw(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
  struct flock lock;

  lock.l_type = type;
  lock.l_start = offset;
  lock.l_whence = whence;
  lock.l_len = len;

  return (fcntl(fd, cmd, &lock));
}                               /* End ink_file_lock_raw */


int
ink_file_region_lock(int fd, int type, off_t start, off_t len)
{
  int s = ink_file_lock_raw(fd, F_SETLKW, type, start, SEEK_SET, len);
  return (s < 0 ? errno : s);
}                               /* End ink_file_region_lock */


int
ink_file_region_trylock(int fd, int type, off_t start, off_t len)
{
  int s = ink_file_lock_raw(fd, F_SETLK, type, start, SEEK_SET, len);
  return (s < 0 ? errno : s);
}                               /* End ink_file_region_lock */


int
ink_file_lock(int fd, int type)
{
  int s = ink_file_lock_raw(fd, F_SETLKW, type, 0, SEEK_SET, 0);
  return (s < 0 ? errno : s);
}                               /* End ink_file_lock */


int
ink_file_trylock(int fd, int type)
{
  int s = ink_file_lock_raw(fd, F_SETLK, type, 0, SEEK_SET, 0);
  return (s < 0 ? errno : s);
}                               /* End ink_file_trylock */



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
ink_filepath_merge(char *path, int pathsz, const char *rootpath,
                   const char *addpath, int flags)
{
  size_t rootlen; // is the length of the src rootpath
  size_t maxlen;  // maximum total path length
  size_t keptlen; // is the length of the retained rootpath
  size_t pathlen; // is the length of the result path
  size_t seglen;  // is the end of the current segment
  char curdir[PATH_MAX];

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
