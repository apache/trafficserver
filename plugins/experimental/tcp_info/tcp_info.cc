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

/* tcp_info.cc:  logs the tcp_info data struture to a file
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
  int sample;
  const char* log_file;
  int log_fd;
  int log_level;
  int hook;
};
static Config config;

static void
load_config() {
  char config_file[PATH_MAX];
  config.sample = 1000;
  config.log_level = 1;
  config.log_file = NULL;
  config.hook = 1;
  
  // get the install directory
  const char* install_dir = TSInstallDirGet();

  // figure out the config file and open it
  snprintf(config_file, sizeof(config_file), "%s/%s/%s", install_dir, "etc", "tcp_info.config");
  FILE *file = fopen(config_file, "r");
  if (file == NULL) {
    snprintf(config_file, sizeof(config_file), "%s/%s/%s", install_dir, "conf", "tcp_info.config");
    file = fopen(config_file, "r");
  }
  TSDebug("tcp_info", "config file name: %s", config_file);
  assert(file != NULL);

  // read and parse the lines
  char line[256];
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
      if (strcmp(line, "sample") == 0) {
        config.sample = atoi(value);
      } else if (strcmp(line, "log_file") == 0) {
        config.log_file = strdup(value);
      } else if (strcmp(line, "log_level") == 0) {
        config.log_level = atoi(value);
      } else if (strcmp(line, "hook") == 0) {
        config.hook = atoi(value);
      }
    }
  }

  TSDebug("tcp_info", "sample: %d", config.sample);
  TSDebug("tcp_info", "log filename: %s", config.log_file);
  TSDebug("tcp_info", "log_level: %d", config.log_level);
  TSDebug("tcp_info", "hook: %d", config.hook);

  config.log_fd = open(config.log_file, O_APPEND | O_CREAT | O_RDWR, 0666);
  assert(config.log_fd > 0);
}

static void
log_tcp_info(const char* event_name, const char* client_ip, const char* server_ip, struct tcp_info &info) {
  char buffer[256];

  // get the current time
  struct timeval now;
  gettimeofday(&now, NULL);

  int bytes = 0;
  if (config.log_level == 2) {
#if !defined(freebsd) || defined(__GLIBC__)
    bytes = snprintf(buffer, sizeof(buffer), "%s %u %u %s %s %u %u %u %u %u %u %u %u %u %u %u %u\n",
                     event_name,
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
#else
        bytes = snprintf(buffer, sizeof(buffer), "%s %u %u %s %s %u %u %u %u %u %u %u %u %u %u %u %u\n",
                     event_name,
                     (uint32_t)now.tv_sec,
                     (uint32_t)now.tv_usec,
                     client_ip,
                     server_ip,
                     info.__tcpi_last_data_sent,
                     info.tcpi_last_data_recv,
                     info.tcpi_snd_cwnd,
                     info.tcpi_snd_ssthresh,
                     info.__tcpi_rcv_ssthresh,
                     info.tcpi_rtt,
                     info.tcpi_rttvar,
                     info.__tcpi_unacked,
                     info.__tcpi_sacked,
                     info.__tcpi_lost,
                     info.__tcpi_retrans,
                     info.__tcpi_fackets
      );
#endif
  } else {
    bytes = snprintf(buffer, sizeof(buffer), "%s %u %s %s %u\n",
                     event_name,
                     (uint32_t)now.tv_sec,
                     client_ip,
                     server_ip,
                     info.tcpi_rtt
    );
  }

  ssize_t wrote = write(config.log_fd, buffer, bytes);
  assert(wrote == bytes);
  TSDebug("tcp_info", "wrote: %d bytes to file: %s", bytes, config.log_file);
  TSDebug("tcp_info", "logging: %s", buffer);
}


static int
tcp_info_hook(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = NULL;
  TSHttpTxn txnp = NULL;

  const char *event_name;
  switch (event) {
  case TS_EVENT_HTTP_SSN_START:
    ssnp = (TSHttpSsn)edata;
    event_name = "ssn_start";
    break;
  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn)edata;
    ssnp = TSHttpTxnSsnGet(txnp);
    event_name = "txn_start";
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    txnp = (TSHttpTxn)edata;
    ssnp = TSHttpTxnSsnGet(txnp);
    event_name = "send_resp_hdr";
    break;
  case TS_EVENT_HTTP_SSN_CLOSE:
    ssnp = (TSHttpSsn)edata;
    event_name = "ssn_close";
  default:
    return 0;
  }

  TSDebug("tcp_info", "tcp_info_hook called, event: %s", event_name);

  struct tcp_info tcp_info;
  int tcp_info_len = sizeof(tcp_info);
  int fd;

  if (TSHttpSsnClientFdGet(ssnp, &fd) != TS_SUCCESS) {
    TSDebug("tcp_info", "error getting the client socket fd");
    goto done;
  }

  // get the tcp info structure
  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, (void *)&tcp_info, (socklen_t *)&tcp_info_len) == 0) {
    // the structure is the correct size
    if (tcp_info_len == sizeof(tcp_info)) {
      // no need to run rand if we are always going log (100%)
      int random = 0;
      if (config.sample < 1000) {
        random = rand() % 1000;
        TSDebug("tcp_info", "random: %d, config.sample: %d", random, config.sample);
      }

      if (random < config.sample) {
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
        
        
        log_tcp_info(event_name, client_str, server_str, tcp_info);
      }
    } else {
      TSDebug("tcp_info", "tcp_info length is the wrong size");
    }
  } else {
    TSDebug("tcp_info", "error calling getsockopt()");
  }

done:
  if (txnp != NULL) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } else if (ssnp != NULL) {
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  }
  return 0;
}

void
TSPluginInit(int, const char *[]) // int argc, const char *argv[]
{
  TSPluginRegistrationInfo info;

  info.plugin_name = (char*)"tcp_info";
  info.vendor_name = (char*)"Apache Software Foundation";
  info.support_email = (char*)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS)
    TSError("Plugin registration failed. \n");

  // load the configuration file
  load_config();

  // add a hook to the state machine
  // TODO: need another hook before the socket is closed, keeping it in for now because it will be easier to change if or when another hook is added to ATS
  if ((config.hook & 1) != 0) {
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, TSContCreate(tcp_info_hook, NULL));
    TSDebug("tcp_info", "added hook to the start of the TCP connection");
  }
  if ((config.hook & 2) != 0) {
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, TSContCreate(tcp_info_hook, NULL));
    TSDebug("tcp_info", "added hook to the close of the transaction");
  }
  if ((config.hook & 4) != 0) {
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, TSContCreate(tcp_info_hook, NULL));
    TSDebug("tcp_info", "added hook to the sending of the headers");
  }
  if ((config.hook & 8) != 0) {
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, TSContCreate(tcp_info_hook, NULL));
    TSDebug("tcp_info", "added hook to the close of the TCP connection");
  }

  TSDebug("tcp_info", "tcp info module registered");
}
