/** @file

  Some utility and support functions for the management module.

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
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "MgmtUtils.h"
#include "MgmtSocket.h"
#include "tscore/Diags.h"

#include "LocalManager.h"

static int use_syslog = 0;

/* mgmt_use_syslog()
 *
 *    Called to indicate that the syslog should be used and
 *       the log has been opened
 */
void
mgmt_use_syslog()
{
  use_syslog = 1;
}

/*
 * mgmt_readline(...)
 *   Simple, inefficient, read line function. Takes a socket to read
 * from, a char * to write into, and a max len to read. The newline
 * is stripped.
 *
 * Returns:  num bytes read
 *           -1  error
 */
int
mgmt_readline(int soc, char *buf, int maxlen)
{
  int n = 0;
  char c;

  for (; n < maxlen; n++) {
    int rc = read_socket(soc, &c, 1);
    if (rc == 1) {
      *buf++ = c;
      if (c == '\n') {
        --buf;
        *buf = '\0';
        if (*(buf - 1) == '\r') {
          --buf;
          *buf = '\0';
        }
        break;
      }
    } else if (rc == 0) {
      if (n == 1) { /* EOF */
        return 0;
      } else {
        break;
      }
    } else { /* Error */
      if (errno == ECONNRESET || errno == EPIPE) {
        return n ? n : 0;
      }

      if (mgmt_transient_error()) {
        mgmt_sleep_msec(1);
        continue;
      }

      return -1;
    }
  }

  return n;
} /* End mgmt_readline */

/*
 * mgmt_writeline(...)
 *   Simple, inefficient, write line function. Takes a soc to write to,
 * a char * containing the data, and the number of bytes to write.
 * It sends nbytes + 1 bytes worth of data, the + 1 being the newline
 * character.
 *
 * Returns:    num bytes not written
 *             -1  error
 */
int
mgmt_writeline(int soc, const char *data, int nbytes)
{
  int nleft, n;
  const char *tmp = data;

  nleft = nbytes;
  while (nleft > 0) {
    int nwritten = write_socket(soc, tmp, nleft);
    if (nwritten <= 0) { /* Error or nothing written */
      return nwritten;
    }
    nleft -= nwritten;
    tmp += nwritten;
  }

  if ((n = write_socket(soc, "\n", 1)) <= 0) { /* Terminating newline */
    if (n < 0) {
      return n;
    } else {
      return (nbytes - nleft);
    }
  }

  return (nleft); /* Paranoia */
} /* End mgmt_writeline */

/*
 * mgmt_read_pipe()
 * - Reads from a pipe
 *
 * Returns: bytes read
 *          0 on EOF
 *          -errno on error
 */

int
mgmt_read_pipe(int fd, char *buf, int bytes_to_read)
{
  char *p        = buf;
  int bytes_read = 0;

  while (bytes_to_read > 0) {
    int err = read_socket(fd, p, bytes_to_read);
    if (err == 0) {
      return err;
    } else if (err < 0) {
      // Turn ECONNRESET into EOF.
      if (errno == ECONNRESET || errno == EPIPE) {
        return bytes_read ? bytes_read : 0;
      }

      if (mgmt_transient_error()) {
        mgmt_sleep_msec(1);
        continue;
      }

      return -errno;
    }

    bytes_to_read -= err;
    bytes_read += err;
    p += err;
  }

  return bytes_read;
}

/*
 * mgmt_write_pipe()
 * - Writes to a pipe
 *
 * Returns: bytes written
 *          0 on EOF
 *          -errno on error
 */

int
mgmt_write_pipe(int fd, char *buf, int bytes_to_write)
{
  char *p           = buf;
  int bytes_written = 0;

  while (bytes_to_write > 0) {
    int err = write_socket(fd, p, bytes_to_write);
    if (err == 0) {
      return err;
    } else if (err < 0) {
      if (mgmt_transient_error()) {
        mgmt_sleep_msec(1);
        continue;
      }

      return -errno;
    }

    bytes_to_write -= err;
    bytes_written += err;
    p += err;
  }

  return bytes_written;
}

void
mgmt_log(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);
  if (diags) {
    NoteV(message_format, ap);
  } else {
    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      syslog(LOG_WARNING, "%s", message);
    } else {
      snprintf(extended_format, sizeof(extended_format), "[E. Mgmt] log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
    }
  }

  va_end(ap);
  return;
} /* End mgmt_log */

void
mgmt_elog(const int lerrno, const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);

  if (diags) {
    ErrorV(message_format, ap);
    if (lerrno != 0) {
      Error("last system error %d: %s", lerrno, strerror(lerrno));
    }
  } else {
    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "ERROR ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      syslog(LOG_ERR, "%s", message);
      if (lerrno != 0) {
        syslog(LOG_ERR, " (last system error %d: %s)", lerrno, strerror(lerrno));
      }
    } else {
      snprintf(extended_format, sizeof(extended_format), "Manager ERROR: %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
      if (lerrno != 0) {
        snprintf(message, sizeof(message), "(last system error %d: %s)", lerrno, strerror(lerrno));
        ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
      }
    }
  }
  va_end(ap);
  return;
} /* End mgmt_elog */

void
mgmt_fatal(const int lerrno, const char *message_format, ...)
{
  va_list ap;

  va_start(ap, message_format);

  if (diags) {
    if (lerrno != 0) {
      Error("last system error %d: %s", lerrno, strerror(lerrno));
    }

    FatalV(message_format, ap);
  } else {
    char extended_format[4096], message[4096];
    snprintf(extended_format, sizeof(extended_format), "FATAL ==> %s", message_format);
    vsprintf(message, extended_format, ap);

    ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);

    if (use_syslog) {
      syslog(LOG_ERR, "%s", message);
    }

    if (lerrno != 0) {
      fprintf(stderr, "[E. Mgmt] last system error %d: %s", lerrno, strerror(lerrno));

      if (use_syslog) {
        syslog(LOG_ERR, " (last system error %d: %s)", lerrno, strerror(lerrno));
      }
    }
  }

  va_end(ap);

  mgmt_cleanup();
  ::exit(1);
} /* End mgmt_fatal */

static inline int
get_interface_mtu(int sock_fd, struct ifreq *ifr)
{
  if (ioctl(sock_fd, SIOCGIFMTU, ifr) < 0) {
    mgmt_log("[getAddrForIntr] Unable to obtain MTU for "
             "interface '%s'",
             ifr->ifr_name);
    return 0;
  } else {
#if defined(solaris) || defined(hpux)
    return ifr->ifr_metric;
#else
    return ifr->ifr_mtu;
#endif
  }
}

bool
mgmt_getAddrForIntr(char *intrName, sockaddr *addr, int *mtu)
{
  bool found = false;

  if (intrName == nullptr) {
    return false;
  }

  int fakeSocket;            // a temporary socket to pass to ioctl
  struct ifconf ifc;         // ifconf information
  char *ifbuf;               // ifconf buffer
  struct ifreq *ifr, *ifend; // pointer to individual interface info
  int lastlen;
  int len;

  // Prevent UMRs
  memset(addr, 0, sizeof(struct in_addr));

  if ((fakeSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    mgmt_fatal(errno, "[getAddrForIntr] Unable to create socket\n");
  }
  // INKqa06739
  // Fetch the list of network interfaces
  // . from Stevens, Unix Network Prog., pg 434-435
  ifbuf   = nullptr;
  lastlen = 0;
  len     = 128 * sizeof(struct ifreq); // initial buffer size guess
  for (;;) {
    ifbuf = static_cast<char *>(ats_malloc(len));
    memset(ifbuf, 0, len); // prevent UMRs
    ifc.ifc_len = len;
    ifc.ifc_buf = ifbuf;
    if (ioctl(fakeSocket, SIOCGIFCONF, &ifc) < 0) {
      if (errno != EINVAL || lastlen != 0) {
        mgmt_fatal(errno, "[getAddrForIntr] Unable to read network interface configuration\n");
      }
    } else {
      if (ifc.ifc_len == lastlen) {
        break;
      }
      lastlen = ifc.ifc_len;
    }
    len *= 2;
    ats_free(ifbuf);
  }

  found = false;
  // Loop through the list of interfaces
  ifend = reinterpret_cast<struct ifreq *>(ifc.ifc_buf + ifc.ifc_len);
  for (ifr = ifc.ifc_req; ifr < ifend;) {
    if (ifr->ifr_addr.sa_family == AF_INET && strcmp(ifr->ifr_name, intrName) == 0) {
      // Get the address of the interface
      if (ioctl(fakeSocket, SIOCGIFADDR, reinterpret_cast<char *>(ifr)) < 0) {
        mgmt_log("[getAddrForIntr] Unable obtain address for network interface %s\n", intrName);
      } else {
        // Only look at the address if it an internet address
        if (ifr->ifr_ifru.ifru_addr.sa_family == AF_INET) {
          ats_ip_copy(addr, &ifr->ifr_ifru.ifru_addr);
          found = true;

          if (mtu) {
            *mtu = get_interface_mtu(fakeSocket, ifr);
          }

          break;
        } else {
          mgmt_log("[getAddrForIntr] Interface %s is not configured for IP.\n", intrName);
        }
      }
    }
#if defined(freebsd) || defined(darwin)
    ifr = (struct ifreq *)((char *)&ifr->ifr_addr + ifr->ifr_addr.sa_len);
#else
    ifr = reinterpret_cast<struct ifreq *>((reinterpret_cast<char *>(ifr)) + sizeof(*ifr));
#endif
  }
  ats_free(ifbuf);
  close(fakeSocket);

  return found;
} /* End mgmt_getAddrForIntr */

/*
 * mgmt_sortipaddrs(...)
 *   Routine to sort and pick smallest ip addr.
 */
struct in_addr *
mgmt_sortipaddrs(int num, struct in_addr **list)
{
  int i = 0;
  unsigned long min;
  struct in_addr *entry, *tmp;

  min   = (list[0])->s_addr;
  entry = list[0];
  while (i < num && (tmp = list[i]) != nullptr) {
    i++;
    if (min > tmp->s_addr) {
      min   = tmp->s_addr;
      entry = tmp;
    }
  }
  return entry;
} /* End mgmt_sortipaddrs */

void
mgmt_sleep_sec(int seconds)
{
  sleep(seconds);
}
void
mgmt_sleep_msec(int msec)
{
  usleep(msec * 1000);
}
