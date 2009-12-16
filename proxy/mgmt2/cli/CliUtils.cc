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

/***************************************/
/****************************************************************************
 *
 *  CliUtils.cc - Utilities to handle command line interface communication
 *
 * 
 ****************************************************************************/

#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
#include "inktomi++.h"
#include "CliUtils.h"
#include "MgmtSocket.h"

// These functions are stolen from MgmtUtils.cc  I hate copying them since
//   it creates a maintence headache but newline termination simply will
//   not work for the command line interface since some things need to
//   be able to span multiple lines

/*
 * cli_read(...)
 *   Simple, inefficient, read line function. Takes a fd to read from
 * an unsigned char * to write into, and a max len to read.  Reads until
 * the max length is encounted or a NULL character is found
 *
 * Returns:  num bytes read
 *           -1  error
 */
int
cli_read(int fd, char *buf, int maxlen)
{
  int n, rc;
  char c;

  for (n = 1; n < maxlen; n++) {
    if ((rc = ink_read_socket(fd, &c, 1)) == 1) {
      *buf++ = c;
      if (c == '\0') {
        --buf;
        *buf = '\0';
        break;
      }
    } else if (rc == 0) {
      if (n == 1)               /* EOF */
        return 0;
      else
        break;
    } else                      /* Error */
      return -1;
  }                             /* end for */

  return n;
}                               /* End cli_read() */


/* 
 * cli_write(...)
 *   Simple, inefficient, write line function. Takes a fd to write to, an
 * unsigned char * containing the data, and the number of bytes to write. 
 * It sends nbytes + 1 bytes worth of data, the + 1 being a NULL character
 *
 * Returns:    num bytes not written
 *             -1  error
 */
int
cli_write(int fd, const char *data, int nbytes)
{
  int nleft, nwritten, n;
  const char *tmp = data;

  nleft = nbytes;
  while (nleft > 0) {
    nwritten = ink_write_socket(fd, tmp, nleft);
    if (nwritten <= 0)          /* Error or nothing written */
      return nwritten;

    nleft -= nwritten;
    tmp += nwritten;
  }

  if ((n = ink_write_socket(fd, "\0", 1)) <= 0) {       /* Terminating newline */
    if (n < 0)
      return n;
    else
      return (nbytes - nleft);
  }

  return (nleft);               /* Paranoia */
}                               /* End cli_write */

//
//  NOTE: the above functions are now only used in the server-side
//    of the CLI.  The client is now using non-blocking descriptors
//    and employing timeouts.  The client function are below.
//
//

// ink_hrtime milliTime() 
//
// Returns the result of gettimeofday converted to
// one 64bit int
//
ink_hrtime
milliTime()
{
  struct timeval curTime;

  ink_gethrtimeofday(&curTime, NULL);
  // Make liberal use of casting to ink_hrtime to ensure the
  //  compiler does not truncate our result
  return ((ink_hrtime) curTime.tv_sec * 1000) + ((ink_hrtime) curTime.tv_usec / 1000);
}

/*
 * cli_read_timeout(...)
 * Reads from the passed in file descriptor.  Reads until
 * the max length is encounted or a NULL character is found or
 * the timeout (in milliseconds) elaspes.  If the timeout elapses, -1
 * returned even if data has been read from the socket.  Negative time out
 * will cause the function not to return until buffer space has
 * been exhausted or NULL character is read
 *
 * fd must have the non-blocking flag set
 *
 * Returns:  num bytes read
 *           -1  error
 */
int
cli_read_timeout(int fd, char *buf, int maxlen, ink_hrtime timeout)
{
  int sys_r;
  char *readCur = buf;
  int bytesRead = 0;
  ink_hrtime end_time = milliTime() + timeout;
  ink_hrtime time_left;
  struct timeval *select_timeout;
  struct timeval timeval_left;

  fd_set selectFDs;

  while (bytesRead < maxlen) {

    if (timeout > 0) {
      time_left = end_time - milliTime();
      if (time_left < 0) {
        time_left = 0;
      }
      timeval_left = ink_hrtime_to_timeval(time_left);
      select_timeout = &timeval_left;
    } else {
      time_left = -1;
      select_timeout = NULL;
    }

    FD_ZERO(&selectFDs);
    FD_SET(fd, &selectFDs);
    sys_r = mgmt_select(FD_SETSIZE, &selectFDs, NULL, NULL, select_timeout);

    if (sys_r < 0) {
      fprintf(stderr, "Select failed : %s\n", strerror(errno));
      return -1;
    } else if (sys_r == 0) {
      fprintf(stderr, "Read from traffic_manager timed out\n");
      return -1;
    }

    ink_assert(bytesRead < maxlen);
    sys_r = ink_read_socket(fd, readCur, maxlen - bytesRead);
    if (sys_r < 0) {
      fprintf(stderr, "Read from traffic_manager failed : %s\n", strerror(errno));
      return -1;
    } else if (sys_r == 0) {
      // No more data.  The connection was closed 
      return bytesRead;
    } else {
      // More data was read
      bytesRead += sys_r;
      readCur += sys_r;

      // Check to see if we are done.  Either we will have
      //   reached that NULL terminator or we will have filled
      //   up our buffer
      if (*(readCur - 1) == '\0' || bytesRead == maxlen) {
        return bytesRead;
      }
    }
  }

  ink_assert(0);                // Should not get here
  return -1;
}                               /* End cli_read_timeout */

/* 
 * cli_write_timeout(...)
 * Takes a fd to write to, an unsigned char * containing the data,
 * and the number of bytes to write and timeout in 
 * milli-seconds.  Negative time out cause the function not to
 * return until all data is written
 * It sends nbytes + 1 bytes worth of data, the + 1 being a NULL character
 *
 * fd must have the non-blocking flag set
 *
 * Returns:    num bytes not written
 *             -1  error
 */
int
cli_write_timeout(int fd, const char *data, int nbytes, ink_hrtime timeout)
{
  int sys_r;
  const char *requestCur;
  int requestLen;
  int bytesToSend;
  const char *nullBuf = "\0";
  fd_set selectFDs;
  struct timeval *select_timeout;
  struct timeval timeval_left;

  // Timeout vars
  ink_hrtime end_time = milliTime() + timeout;
  ink_hrtime time_left;

  // We need to make two passes. One to write the data and one
  //   to write the extra NULL byte
  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      requestCur = data;
      requestLen = nbytes;
      bytesToSend = nbytes;
    } else {
      requestCur = nullBuf;
      requestLen = 1;
      bytesToSend = 1;
    }

    while (bytesToSend > 0) {

      // Wait until we can write something
      if (timeout > 0) {
        time_left = end_time - milliTime();
        if (time_left < 0) {
          time_left = 0;
        }
        timeval_left = ink_hrtime_to_timeval(time_left);
        select_timeout = &timeval_left;
      } else {
        time_left = -1;
        select_timeout = NULL;
      }

      FD_ZERO(&selectFDs);
      FD_SET(fd, &selectFDs);
      sys_r = mgmt_select(FD_SETSIZE, NULL, &selectFDs, NULL, select_timeout);

      if (sys_r < 0) {
        fprintf(stderr, "Select failed : %s\n", strerror(errno));
        return -1;
      } else if (sys_r == 0) {
        fprintf(stderr, "Write to traffic_manager timed out\n");
        return -1;
      }

      sys_r = ink_write_socket(fd, requestCur, bytesToSend);
      if (sys_r < 0) {
        fprintf(stderr, "Write to traffic_manager failed, connection probably closed : %s\n", strerror(errno));
        return -1;
      } else {
        requestCur += sys_r;
        bytesToSend -= sys_r;
      }
    }
  }

  return nbytes + 1;
}                               /* End cli_write_timeout */

/*
 This function is copied from cli2/CliMgmtUtils.cc. Gets the install directory from
/etc/traffic_server
*/
int
GetTSDirectory(char *ts_path)
{
  FILE *fp;
  const char *env_path;
  struct stat s;
  int err;

  if ((env_path = getenv("TS_ROOT"))) {
    ink_strncpy(ts_path, env_path, PATH_NAME_MAX);
  } else {
    if ((fp = fopen("/etc/traffic_server", "r")) != NULL) {
      if (fgets(ts_path, PATH_NAME_MAX, fp) == NULL) {
        fclose(fp);
        printf("\nInvalid contents in /etc/traffic_server\n");
        printf(" Please set correct path in env variable TS_ROOT \n");
        return -1;
      }
      // strip newline if it exists
      int len = strlen(ts_path);
      if (ts_path[len - 1] == '\n') {
        ts_path[len - 1] = '\0';
      }
      // strip trailing "/" if it exists
      len = strlen(ts_path);
      if (ts_path[len - 1] == '/') {
        ts_path[len - 1] = '\0';
      }
      
      fclose(fp);
    } else {
      ink_strncpy(ts_path, PREFIX, PATH_NAME_MAX);
    }
  }

  if ((err = stat(ts_path, &s)) < 0) {
    printf("unable to stat() TS PATH '%s': %d %d, %s\n", 
              ts_path, err, errno, strerror(errno));
    printf(" Please set correct path in env variable TS_ROOT \n");
    return -1;
  }

  return 0;
}
