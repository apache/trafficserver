/** @file

  This file implements predefined log formats

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

#include "LogPredefined.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogFormat.h"

// predefined formats
static const char squid_format[] =
  "%<cqtq> %<ttms> %<chi> %<crc>/%<pssc> %<psql> %<cqhm> %<cquc> %<caun> %<phr>/%<pqsn> %<psct> %<xid>";

static const char common_format[] = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl>";

static const char extended_format[] =
  "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl> "
  "%<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts>";

static const char extended2_format[] = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl> "
  "%<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts> %<phr> %<cfsc> %<pfsc> %<crc>";

PreDefinedFormatList::PreDefinedFormatList()
{
}

PreDefinedFormatList::~PreDefinedFormatList()
{
  PreDefinedFormatInfo * info;
  while (!this->list.empty()) {
    info = this->list.pop();
    delete info;
  }
}

void
PreDefinedFormatList::init(LogConfig * config)
{
  LogFormat *fmt;

  fmt = NEW(new LogFormat("squid", squid_format));
  config->global_format_list.add(fmt, false);
  Debug("log", "squid format added to the global format list");

  if (config->squid_log_enabled) {
    this->list.enqueue(NEW(new PreDefinedFormatInfo(fmt, config->squid_log_name, config->squid_log_is_ascii, config->squid_log_header)));
  }

  fmt = NEW(new LogFormat("common", common_format));
  config->global_format_list.add(fmt, false);
  Debug("log", "common format added to the global format list");

  if (config->common_log_enabled) {
    this->list.enqueue(NEW(new PreDefinedFormatInfo(fmt, config->common_log_name, config->common_log_is_ascii, config->common_log_header)));
  }

  fmt = NEW(new LogFormat("extended", extended_format));
  config->global_format_list.add(fmt, false);
  Debug("log", "extended format added to the global format list");

  if (config->extended_log_enabled) {
    this->list.enqueue(NEW(new PreDefinedFormatInfo(fmt, config->extended_log_name, config->extended_log_is_ascii, config->extended_log_header)));
  }

  fmt = NEW(new LogFormat("extended2", extended2_format));
  config->global_format_list.add(fmt, false);
  Debug("log", "extended2 format added to the global format list");

  if (config->extended2_log_enabled) {
    this->list.enqueue(NEW(new PreDefinedFormatInfo(fmt, config->extended2_log_name, config->extended2_log_is_ascii, config->extended2_log_header)));
  }

}

