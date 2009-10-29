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

#ifndef __IDC_CONTROL_MATCHER_H__
#define __IDC_CONTROL_MATCHER_H__

#include "api/include/InkAPI.h"

class IDCControlResult
{
public:
  IDCControlResult()
  {
    idc_eligible = 1;
  }
  void Print()
  {
    if (idc_eligible)
      printf("idc_eligible\n");
    else
      printf("not idc_eligible\n");
  }
  int idc_eligible;
};

class IDCControlRecord
{
public:
  IDCControlRecord()
  {
    line_info = NULL;
  }
  char *Init(matcher_line * line_info)
  {
    this->line_info = line_info;
    return NULL;
  }
  void UpdateMatch(IDCControlResult * result, RD * rdata)
  {
    result->idc_eligible = 0;
  }
  void Print()
  {
  }
  matcher_line *line_info;
  int line_num;
};

class IDCRequestData:public RequestData
{
public:
  char *get_string()
  {
    return url_string;
  }
  const char *get_host()
  {
    return host_string;
  }
  ip_addr_t get_ip()
  {
    INKAssert(!"should not be used");
    return 0;
  }
  ip_addr_t get_client_ip()
  {
    INKAssert(!"should not be used");
    return 0;
  }
  IDCRequestData(INKMBuffer bufp, INKMLoc offset) {
    //get the url
    int url_length;
    // INKUrlStringGet function allocates a new buffer and 
    // returns a NULL terminated string in that buffer...We have
    // to free it in the destructor
    url_string = INKUrlStringGet(bufp, offset, &url_length);

    //get the host
    int host_length;
    const char *host = INKUrlHostGet(bufp, offset, &host_length);
    host_string = (char *) INKmalloc(host_length + 1);
    strncpy(host_string, host, host_length);
    host_string[host_length] = 0;
  }
  virtual ~ IDCRequestData() {
    if (url_string) {
      // This string is already released by ControlMatcher::Match();                    
      //INKfree(url_string);
    }

    if (host_string) {
      INKfree(host_string);
    }
  }
  char *url_string;
  char *host_string;
};

typedef ControlMatcher<IDCControlRecord, IDCControlResult> IDC_table;

#endif /* __IDC_CONTROL_MATCHER_H__ */
