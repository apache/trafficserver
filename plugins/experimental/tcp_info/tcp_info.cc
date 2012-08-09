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

/* stats.c:  expose traffic server stats over http
 */

#include <stdio.h>
#include <stdlib.h>
#include <ts/ts.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>

struct Config {
  int percentage;
  const char* log_file;
  int log_fd;
  int log_level;
};
static Config config;

static void
load_config() {
  char config_file[PATH_MAX];
  config.percentage = 100;
  config.log_level = 1;
  config.log_file = NULL;
  
  // get the configuration file name
  const char* install_dir = TSInstallDirGet();
  char *root = getenv("ROOT");
  if (root != NULL && strcmp(install_dir, "ROOT") == 0) {
    snprintf(config_file, sizeof(config_file), "%s/%s/%s", install_dir, "conf", "tcp_info.config");
  } else {
    snprintf(config_file, sizeof(config_file), "%s/%s/%s", install_dir, "etc", "tcp_info.config");
  }
  TSDebug("tcp_info", "config file name: %s", config_file);

  // open the config file
  FILE *file = fopen(config_file, "r");
  assert(file != NULL);
  char line[256];

  // read and parse the lines
  while (fgets(line, sizeof(line), file) != NULL) {
    char *pos = strchr(line, '=');
    *pos = '\0';
    char *value = pos + 1;

    // remove the new line
    pos = strchr(value, '\n');
    if (pos != NULL) {
      *pos = '\0';
    }

    if (value != NULL) {
      TSDebug("tcp_info", "config key: %s", line);
      TSDebug("tcp_info", "config value: %s", value);
      if (strcmp(line, "percentage") == 0) {
        config.percentage = atoi(value);
      } else if (strcmp(line, "log_file") == 0) {
        config.log_file = strdup(value);
      } else if (strcmp(line, "log_level") == 0) {
        config.log_level = atoi(value);
      }
    }
  }

  TSDebug("tcp_info", "percentage: %d", config.percentage);
  TSDebug("tcp_info", "log filename: %s", config.log_file);
  TSDebug("tcp_info", "log_level: %d", config.log_level);

  config.log_fd = open(config.log_file, O_APPEND | O_CREAT | O_RDWR, 0666);
  assert(config.log_fd > 0);
}

static void
log_tcp_info(const char* client_ip, const char* server_ip, struct tcp_info &info) {
  char buffer[256];

  // get the current time
  struct timeval now;
  gettimeofday(&now, NULL);

  int bytes = 0;
  if (config.log_level == 2) {
    bytes = snprintf(buffer, sizeof(buffer), "%u %u %s %s %u %u %u %u %u %u %u %u %u %u %u %u\n",
                     (uint32_t)now.tv_sec,
                     (uint32_t)now.tv_usec,
                     client_ip,
                     server_ip,
                     info.tcpi_last_data_sent,
                     info.tcpi_last_data_recv,
                     info.tcpi_snd_cwnd,
                     info.tcpi_snd_ssthresh,
                     info.tcpi_rcv_ssthresh,
                     info.tcpi_rtt,
                     info.tcpi_rttvar,
                     info.tcpi_unacked,
                     info.tcpi_sacked,
                     info.tcpi_lost,
                     info.tcpi_retrans,
                     info.tcpi_fackets
      );
  } else {
    bytes = snprintf(buffer, sizeof(buffer), "%u %s %s %u\n",
                     (uint32_t)now.tv_sec,
                     client_ip,
                     server_ip,
                     info.tcpi_rtt
    );
  }

  ssize_t wrote = write(config.log_fd, buffer, bytes);
  sync();
  assert(wrote == bytes);
  TSDebug("tcp_info", "wrote: %d bytes to file: %s", bytes, config.log_file);
  TSDebug("tcp_info", "logging: %s", buffer);
}


static int
tcp_info_hook(TSCont contp, TSEvent event, void *edata)
{
  TSDebug("tcp_info", "tcp_info_hook called");
  TSHttpSsn ssnp = (TSHttpSsn) edata;

  struct tcp_info tcp_info;
  int tcp_info_len = sizeof(tcp_info);
  int fd;

  if (TSHttpSsnClientFdGet(ssnp, &fd) != TS_SUCCESS)
    goto done;

  // get the tcp info structure
  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, (void *)&tcp_info, (socklen_t *)&tcp_info_len) == 0) {
    // the structure is the correct size
    if (tcp_info_len == sizeof(tcp_info)) {
      // no need to run rand if we are always going log (100%)
      int random = 0;
      if (config.percentage < 100) {
        random = rand() % 100;
        TSDebug("tcp_info", "random: %d, config.percentage: %d", random, config.percentage);
      }

      if (random < config.percentage) {
        TSDebug("tcp_info", "got the tcp_info struture and now logging");

        // get the client address
        const struct sockaddr *client_addr = TSHttpSsnClientAddrGet(ssnp); 
        const struct sockaddr *server_addr = TSHttpSsnIncomingAddrGet(ssnp);
        if (client_addr == NULL || server_addr == NULL)
          goto done;
        struct sockaddr_in *client_in_addr = (struct sockaddr_in *)client_addr;
        struct sockaddr_in *server_in_addr = (struct sockaddr_in *)server_addr;
        char client_str[INET_ADDRSTRLEN];
        char server_str[INET_ADDRSTRLEN];


        // convert ip to string
        inet_ntop(client_addr->sa_family, &(client_in_addr->sin_addr), client_str, INET_ADDRSTRLEN);
        inet_ntop(server_addr->sa_family, &(server_in_addr->sin_addr), server_str, INET_ADDRSTRLEN);

        log_tcp_info(client_str, server_str, tcp_info);
      }
    }
  }

done:
  TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 3) {
      result = 1;
    }
  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = (char*)"stats";
  info.vendor_name = (char*)"Apache Software Foundation";
  info.support_email = (char*)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS)
    TSError("Plugin registration failed. \n");

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  load_config();

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, TSContCreate(tcp_info_hook, NULL));
  TSDebug("tcp_info", "tcp info module registered");
}
