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

#ifndef _CLI_UTILS_H_
#define _CLI_UTILS_H_

/****************************************************************************
 *
 *  CliUtils.h - Utilities to handle command line interface communication
 *
 * 
 ****************************************************************************/
#include "ink_platform.h"
#include "ink_hrtime.h"
#include "ink_port.h"

// Server side functions (blocking I/O)
int cli_read(int fd, char *buf, int maxlen);
int cli_write(int fd, const char *data, int nbytes);
ink_hrtime milliTime(void);

// Client side functions (non-blocking I/O)
int cli_read_timeout(int fd, char *buf, int maxlen, ink_hrtime timeout);
int cli_write_timeout(int fd, const char *data, int nbytes, ink_hrtime timeout);
int GetTSDirectory(char *ts_path);

#endif /* _CLI_UTILS_H_ */
