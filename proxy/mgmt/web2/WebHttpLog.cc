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

/************* ***************************
 *
 *  WebHttpLog.cc
 *
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "TextBuffer.h"
#include "Main.h"
#include "WebCompatibility.h"
#include "WebHttpLog.h"

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

WebHandle WebHttpLogHandle = WEB_HANDLE_INVALID;

//-------------------------------------------------------------------------
// WebHttpLogInit
//-------------------------------------------------------------------------

void
WebHttpLogInit()
{
  struct stat s;
  int err;
  char *log_dir;
  char log_file[PATH_NAME_MAX+1];

  if ((err = stat(system_log_dir, &s)) < 0) {
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.log.logfile_dir", &log_dir)
	       == REC_ERR_OKAY);
    if ((err = stat(log_dir, &s)) < 0) {
      // Try 'system_root_dir/var/log/trafficserver' directory
      snprintf(system_log_dir, sizeof(system_log_dir), "%s%s%s%s%s%s%s",
               system_root_dir, DIR_SEP,"var",DIR_SEP,"log",DIR_SEP,"trafficserver");
      if ((err = stat(system_log_dir, &s)) < 0) {
        mgmt_elog("unable to stat() log dir'%s': %d %d, %s\n",
                system_log_dir, err, errno, strerror(errno));
        mgmt_elog("please set 'proxy.config.log.logfile_dir'\n");
        //_exit(1);
      }
    } else {
      ink_strncpy(system_log_dir,log_dir,sizeof(system_log_dir));
    }
  }

  snprintf(log_file, sizeof(log_file), "%s%s%s", system_log_dir, DIR_SEP, "lm.log");
  if (WebHttpLogHandle == WEB_HANDLE_INVALID) {
    WebHttpLogHandle = WebFileOpenW(log_file);
  }

}

//-------------------------------------------------------------------------
// WebHttpLogTransaction
//-------------------------------------------------------------------------

int
WebHttpLogTransaction(WebHttpContext * whc)
{

  const char space[] = " ";
  const char dash[] = "-";
  const char quote[] = "\"";

  textBuffer log_output(512);
  char *hostname;
  char *user;
  const char *date;
  const char *request;
  HttpStatus_t status;
  int conLen;
  int bytes_written;

  char lenBuf[20];

  if (wGlobals.logResolve == true) {
    hostname = WebGetHostname_Xmalloc(whc->client_info);
    log_output.copyFrom(hostname, strlen(hostname));
    xfree(hostname);
  } else {
    hostname = inet_ntoa(whc->client_info->sin_addr);
    log_output.copyFrom(hostname, strlen(hostname));
  }

  log_output.copyFrom(space, 1);
  log_output.copyFrom(dash, 1);
  log_output.copyFrom(space, 1);

  user = whc->current_user.user;
  if (*user != '\0') {
    log_output.copyFrom(user, strlen(user));
  } else {
    log_output.copyFrom(dash, 1);
  }

  log_output.copyFrom(space, 1);

  // get the logging info
  whc->response_hdr->getLogInfo(&date, &status, &conLen);
  whc->request->getLogInfo(&request);

  log_output.copyFrom(date, strlen(date));
  log_output.copyFrom(space, 1);
  log_output.copyFrom(quote, 1);
  if (request != NULL) {
    log_output.copyFrom(request, strlen(request));
  }
  log_output.copyFrom(quote, 1);
  log_output.copyFrom(space, 1);
  log_output.copyFrom(httpStatCode[status], strlen(httpStatCode[status]));
  log_output.copyFrom(space, 1);

  snprintf(lenBuf, sizeof(lenBuf), "%d", conLen);
  log_output.copyFrom(lenBuf, strlen(lenBuf));
  log_output.copyFrom("\n", 1);

  WebFileWrite(WebHttpLogHandle, log_output.bufPtr(), log_output.spaceUsed(), &bytes_written);

  return WEB_HTTP_ERR_OKAY;

}
