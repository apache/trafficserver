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
 *  ink_syslog.cc
 *
 *
 ****************************************************************************/
#include "tscore/ink_platform.h"

struct syslog_fac {
  char *long_str;
  char *short_str;
  int fac_int;
};

static const syslog_fac convert_table[] = {
  {(char *)"LOG_KERN", (char *)"KERN", LOG_KERN},       {(char *)"LOG_USER", (char *)"USER", LOG_USER},
  {(char *)"LOG_MAIL", (char *)"MAIL", LOG_MAIL},       {(char *)"LOG_DAEMON", (char *)"DAEMON", LOG_DAEMON},
  {(char *)"LOG_AUTH", (char *)"AUTH", LOG_AUTH},       {(char *)"LOG_LPR", (char *)"LPR", LOG_LPR},
  {(char *)"LOG_NEWS", (char *)"NEWS", LOG_NEWS},       {(char *)"LOG_UUCP", (char *)"UUCP", LOG_UUCP},
  {(char *)"LOG_CRON", (char *)"CRON", LOG_CRON},       {(char *)"LOG_LOCAL0", (char *)"LOCAL0", LOG_LOCAL0},
  {(char *)"LOG_LOCAL1", (char *)"LOCAL1", LOG_LOCAL1}, {(char *)"LOG_LOCAL2", (char *)"LOCAL2", LOG_LOCAL2},
  {(char *)"LOG_LOCAL3", (char *)"LOCAL3", LOG_LOCAL3}, {(char *)"LOG_LOCAL4", (char *)"LOCAL4", LOG_LOCAL4},
  {(char *)"LOG_LOCAL5", (char *)"LOCAL5", LOG_LOCAL5}, {(char *)"LOG_LOCAL6", (char *)"LOCAL6", LOG_LOCAL6},
  {(char *)"LOG_LOCAL7", (char *)"LOCAL7", LOG_LOCAL7}, {(char *)"INVALID_LOG_FAC", (char *)"INVALID", -1}};
static const int convert_table_size = sizeof(convert_table) / sizeof(syslog_fac) - 1;

// int facility_string_to_int(const char* str)
//   Converts a string for a syslog to an int for that
//    facility, suitable for passing to openlog()
//
//   If the string can not be converted, returns
//    a negative value
//
int
facility_string_to_int(const char *str)
{
  if (str == nullptr) {
    return -1;
  }
  // Loop Through to see if the string has a valid conversion
  for (int i = 0; i < convert_table_size; i++) {
    if (strcasecmp(convert_table[i].long_str, str) == 0 || strcasecmp(convert_table[i].short_str, str) == 0) {
      return convert_table[i].fac_int;
    }
  }
  return -1;
}
