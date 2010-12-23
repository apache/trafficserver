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

#include "I_NetConfig.h"

#define NULL 0

int net_config_poll_timeout = DEFAULT_POLL_TIMEOUT;
int net_config_fds_throttle = 8000;
int net_config_listen_backlog = 1024;


// SSL config
// # proxy.config.ssl.enabled should be:
// #   0 - none
// #   1 - SSL enabled
int net_config_ssl_mode = 1;

// #   proxy.config.ssl.accelerator.type should be:
// #   0 - none (software algorithms)
// #   1 - NCipher Nfast accelerator card
// #   2 - Rainbow Crypto Swift accelerator card
// #   3 - Compaq Atalla accelerator card
int net_config_sslAccelerator = 0;

//    # the following accelerator library paths only need
//    # to be changed if the default path was not used
//    # while installing the accelerator card.
const char *net_config_atallaAccelLibPath = "/opt/atalla/lib";
const char *net_config_ncipherAccelLibPath = "/opt/nfast/toolkits/hwcrhk";
const char *net_config_cswiftAccelLibPath = "/usr/lib";


int net_config_ssl_accept_port_number = 8443;

//    # Client certification level should be:
//    # 0 no client certificates
//    # 1 client certificates optional
//    # 2 client certificates required
int net_config_clientCertLevel = 0;

//    # Server cert filename is the name of the cert file
//    # for a single cert system and the default cert name
//    # for a multiple cert system.
const char *net_config_serverCertFilename = "server.pem";

//    # This is the path that will be used for both single and
//    # multi cert systems.
const char *net_config_serverCertRelativePath = "etc/trafficserver";

//    # Fill in private key file and path only if the server's
//    # private key is not contained in the server certificate file.
//    # For multiple cert systems, if any private key is not contained
//    # in the cert file, you must fill in the private key path.
char *net_config_ssl_server_private_key_filename = NULL;
char *net_config_ssl_server_private_key_path = NULL;

//    # The CA file name and path are the
//    # certificate authority certificate that
//    # client certificates will be verified against.
char *net_config_CACertFilename = NULL;
char *net_config_CACertRelativePath = NULL;


//    ################################
//    # client related configuration #
//    ################################

int net_config_clientVerify = 0;
char *net_config_ssl_client_cert_filename = NULL;
const char *net_config_ssl_client_cert_path = "etc/trafficserver";

//    # Fill in private key file and path only if the client's
//    # private key is not contained in the client certificate file.
char *net_config_ssl_client_private_key_filename = NULL;
char *net_config_ssl_client_private_key_path = NULL;

//    # The CA file name and path are the
//    # certificate authority certificate that
//    # server certificates will be verified against.
char *net_config_clientCACertFilename = NULL;
char *net_config_clientCACertRelativePath = NULL;


const char *net_config_multicert_config_file = "ssl_multicert.config";
