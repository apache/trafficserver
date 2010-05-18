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

#include "ink_defs.h"
#include "ink_file.h"
#include "I_Layout.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>

#define CHANGE_ALL 	0
#define CHANGE_TIME 	1
#define CHANGE_DATE 	2
#define CHANGE_TIMEZONE 3
#define CHANGE_NTP 	4


int
main(int argc, char *argv[])
{
#if (HOST_OS == linux)
  struct tm *mPtr;
  struct timeval v;
  FILE *fp, *tmp;
  // TODO: Use defines instead hard coding 1024
  //
  char zonepath[1024], no_cop_path[1024], buffer[1024], command[1024];
  char stop_traffic_server[PATH_MAX_LEN + 1], start_traffic_server[1024];
  char *hour = 0, *minute = 0, *second = 0, *month = 0, *day = 0, *year = 0, *timezone = 0, *ntpservers = 0;
  int reset_all = 0, reset_time = 0, reset_date = 0, reset_timezone = 0, reset_ntp = 0;
  int restart;

  // INKqa12795 (connection to the TM lost after date/time change.)
  int fd;
  int fd_max = USHRT_MAX;       // just in case getrlimit() fails
  struct rlimit rl;
  char *env_path;

  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    fd_max = rl.rlim_max;
  }
  for (fd = 3; fd < fd_max; fd++) {
    close(fd);
  }
  buffer[0] = 0;
  // Before accessing file system initialize Layout engine
  Layout::create();

  Layout::relative_to(stop_traffic_server, sizeof(stop_traffic_server) - 1,
                      Layout::get()->bindir, "stop_traffic_server");
  Layout::relative_to(start_traffic_server, sizeof(start_traffic_server) - 1,
                      Layout::get()->bindir, "start_traffic_server");
  Layout::relative_to(no_cop_path, sizeof(no_cop_path) - 1,
                      Layout::get()->bindir, "internal/no_cop");
  // XXX: Why strncpy here?
  strncpy(zonepath, "/usr/share/zoneinfo/", sizeof(zonepath) - 1);

//  while(access(no_cop_path, F_OK) == -1);
//argv[1] is the control flag for restart TS or not.
  restart = atoi(argv[1]);

  if (restart) {
    NOWARN_UNUSED_RETURN(system(stop_traffic_server));
  }

  switch (atoi(argv[2])) {
  case CHANGE_ALL:
    reset_all = 1;
    hour = argv[3];
    minute = argv[4];
    second = argv[5];
    month = argv[6];
    day = argv[7];
    year = argv[8];
    timezone = argv[9];
    ntpservers = argv[10];
    break;
  case CHANGE_TIME:
    reset_time = 1;
    hour = argv[3];
    minute = argv[4];
    second = argv[5];
    break;
  case CHANGE_DATE:
    reset_date = 1;
    month = argv[3];
    day = argv[4];
    year = argv[5];
    break;
  case CHANGE_TIMEZONE:
    reset_timezone = 1;
    timezone = argv[3];
    break;
  case CHANGE_NTP:
    reset_ntp = 1;
    ntpservers = argv[3];
    break;
  }

  if (reset_timezone || reset_all) {
    fp = fopen("/etc/sysconfig/clock", "r");
    tmp = fopen("/tmp/clock.tmp", "w");
    if (fp != NULL) {
      int zone_find = 0;
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "ZONE") != NULL) {
          fprintf(tmp, "ZONE=\"%s\"\n", timezone);
          zone_find = 1;
        } else {
          fputs(buffer, tmp);
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
      }
      fclose(fp);
      if (zone_find == 0) {
        fprintf(tmp, "ZONE=\"%s\"\n", timezone);
      }
    } else {
      fprintf(tmp, "ZONE=\"%s\"\n", timezone);
    }
    fclose(tmp);
    NOWARN_UNUSED_RETURN(system("/bin/mv /tmp/clock.tmp /etc/sysconfig/clock"));
    strncat(zonepath, timezone, (sizeof(zonepath) - 1 - strlen(zonepath)));
    strncpy(command, "/bin/cp -f ", sizeof(command) - 1);
    strncat(command, zonepath, sizeof(command) - 1 - strlen(command));
    strncat(command, " /etc/localtime", sizeof(command) - 1 - strlen(command));
    NOWARN_UNUSED_RETURN(system(command));
  }

  memset(&v, 0, sizeof(struct timeval));
  gettimeofday(&v, NULL);
  mPtr = localtime(&(v.tv_sec));

  if (reset_time || reset_all) {
    mPtr->tm_sec = atoi(second);
    mPtr->tm_min = atoi(minute);
    mPtr->tm_hour = atoi(hour);
  }
  if (reset_date || reset_all) {
    mPtr->tm_mon = atoi(month) - 1;
    mPtr->tm_mday = atoi(day);
    mPtr->tm_year = atoi(year) - 1900;
  }

  if (reset_time || reset_date || reset_all) {
    if ((v.tv_sec = mktime(mPtr)) > 0) {
      settimeofday(&v, NULL);
    }
    NOWARN_UNUSED_RETURN(system("/sbin/hwclock --systohc --utc"));

//Change the UTC option to be "true" because we have set the hwclock as UTC
    fp = fopen("/etc/sysconfig/clock", "r");
    tmp = fopen("/tmp/clock.tmp", "w");
    if (fp != NULL) {
      int utc_find = 0;
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "UTC") != NULL) {
          fprintf(tmp, "UTC=true\n");
          utc_find = 1;
        } else {
          fputs(buffer, tmp);
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
      }
      fclose(fp);
      if (utc_find == 0) {
        fprintf(tmp, "UTC=true\n");
      }
    } else {
      fprintf(tmp, "UTC=true\n");
    }
    fclose(tmp);
    NOWARN_UNUSED_RETURN(system("/bin/mv /tmp/clock.tmp /etc/sysconfig/clock"));

  }

  if (reset_ntp || reset_all) {
    int server_no = 0;
    char *server[3], *pos;

    //Bug49374, the ntp utils need the SIGALRM be unblocked, unblock all signal for safe.
    sigset_t newmask, oldmask;
    sigfillset(&newmask);
    sigprocmask(SIG_UNBLOCK, &newmask, &oldmask);

    NOWARN_UNUSED_RETURN(system("/sbin/service ntpd stop"));
    snprintf(command, sizeof(command) - 1, "/usr/sbin/ntpdate -s -b -p 8 %s", ntpservers);
    NOWARN_UNUSED_RETURN(system(command));
    server[server_no] = pos = ntpservers;
    while ((pos = strchr(pos, ' ')) != NULL) {
      *pos = '\0';
      pos++;
      server_no++;
      server[server_no] = pos;
    }

    remove("/etc/ntp/step-tickers");
    fp = fopen("/etc/ntp/step-tickers", "w");
    for (int i = 0; i <= server_no; i++) {
      fprintf(fp, "%s\n", server[i]);
    }
    fclose(fp);

    fp = fopen("/etc/ntp.conf", "r");
    tmp = fopen("/tmp/ntpconf.tmp", "w");
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    while (!feof(fp)) {
      if (strncmp(buffer, "server", 6) != 0) {
        fputs(buffer, tmp);
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    }

    for (int i = 0; i <= server_no; i++) {
      fprintf(tmp, "server %s\n", server[i]);
    }
    fclose(fp);
    fclose(tmp);
    NOWARN_UNUSED_RETURN(system("/bin/mv /tmp/ntpconf.tmp /etc/ntp.conf"));

    NOWARN_UNUSED_RETURN(system("/sbin/chkconfig --level 2345 ntpd on"));
    NOWARN_UNUSED_RETURN(system("/sbin/service ntpd start"));
  }

  if (restart) {
//INKqa12511 restart crond because it is sensetive on the time also.
    NOWARN_UNUSED_RETURN(system("/sbin/service crond restart"));
    NOWARN_UNUSED_RETURN(system(start_traffic_server));
  }
#endif
  exit(0);
}
