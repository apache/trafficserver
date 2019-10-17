/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

// To make compile faster
// https://github.com/philsquared/Catch/blob/master/docs/slow-compiles.md
// #define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "tscore/I_Layout.h"
#include "tscore/Diags.h"

#include "I_EventSystem.h"
#include "RecordsConfig.h"

#include "QUICConfig.h"
#include "HuffmanCodec.h"
#include "QPACK.h"
#include "HTTP.h"

#define TEST_THREADS 1

char qifdir[256]  = "./qifs/qifs";
char encdir[256]  = "./qifs/encoded";
char decdir[256]  = "./qifs/decoded";
int tablesize     = 4096;
int streams       = 100;
int ackmode       = 0;
char appname[256] = "ats";
char pattern[256] = "";

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor

  virtual void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    BaseLogFile *base_log_file = new BaseLogFile("stderr");
    diags                      = new Diags(testRunInfo.name.c_str(), "" /* tags */, "" /* actions */, base_log_file);
    diags->activate_taglist("qpack", DiagsTagType_Debug);
    diags->config.enabled[DiagsTagType_Debug] = true;
    diags->show_location                      = SHOW_LOCATION_DEBUG;

    Layout::create();
    RecProcessInit(RECM_STAND_ALONE);
    LibRecordsConfigInit();

    QUICConfig::startup();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(TEST_THREADS);

    Thread *main_thread = new EThread;
    main_thread->set_specific();

    url_init();
    mime_init();
    http_init();
    hpack_huffman_init();
  }
};
CATCH_REGISTER_LISTENER(EventProcessorListener);

int
main(int argc, char *argv[])
{
  Catch::Session session;
  using namespace Catch::clara;
  auto cli =
    session.cli() | Opt(qifdir, "qifdir")["--q-qif-dir"]("path for a directory that contains QIF files (default:qifs/qifs") |
    Opt(encdir, "encdir")["--q-encoded-dir"]("path for a directory that encoded files will be stored (default:qifs/encoded)") |
    Opt(decdir, "decdir")["--q-decoded-dir"]("path for a directory that decoded files will be stored (default:qifs/decoded)") |
    Opt(tablesize, "size")["--q-dynamic-table-size"]("dynamic table size for encoding: 0-65535 (default:4096)") |
    Opt(streams, "n")["--q-max-blocked-streams"]("max blocked streams for encoding: 0-65535 (default:100)") |
    Opt(ackmode, "mode")["--q-ack-mode"]("acknowledgement modes for encoding: none(default:0) or immediate(1)") |
    Opt(pattern, "pattern")["--q-pattern"]("filename pattern: file name pattern for decoding (default:)") |
    Opt(appname, "app")["--q-app"]("app name: app name (default:ats)");

  session.cli(cli);

  int returnCode = session.applyCommandLine(argc, argv);
  if (returnCode != 0) {
    return returnCode;
  }

  return session.run();
}
