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

#ifndef __I_NETCONFIG_H__
#define __I_NETCONFIG_H__

/*
 * Temporary file specifying all configurable parameters for the 
 * net sub system.
 * For the default values look at NetConfig.cc
 */

extern int net_config_max_poll_delay;
extern int net_config_fds_throttle;
extern int net_config_listen_backlog;

// SSL config
extern int net_config_ssl_mode;
extern int net_config_sslAccelerator;
extern int net_config_ssl_accept_port_number;
extern int net_config_clientCertLevel;
extern char *net_config_atallaAccelLibPath;
extern char *net_config_ncipherAccelLibPath;
extern char *net_config_cswiftAccelLibPath;
extern char *net_config_serverCertFilename;
extern char *net_config_serverCertRelativePath;
extern char *net_config_multicert_config_file;
extern char *net_config_ssl_server_private_key_filename;
extern char *net_config_ssl_server_private_key_path;
extern char *net_config_CACertFilename;
extern char *net_config_CACertRelativePath;
extern int net_config_clientVerify;
extern char *net_config_ssl_client_cert_filename;
extern char *net_config_ssl_client_cert_path;
extern char *net_config_ssl_client_private_key_filename;
extern char *net_config_ssl_client_private_key_path;
extern char *net_config_clientCACertFilename;
extern char *net_config_clientCACertRelativePath;
#endif
