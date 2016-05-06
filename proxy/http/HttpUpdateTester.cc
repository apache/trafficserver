/** @file

        Tests http scheduled functionality by requesting URLs out of a file

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

#include "HttpUpdateSM.h"
#include "HttpDebugNames.h"
#include "ts/Diags.h"
#include "ts/ink_platform.h"

#define MAX_ACTIVE_REQUESTS 5
#define MAX_TOTAL_REQUESTS 100

class UpTest : public Continuation
{
public:
  UpTest(FILE *f, ProxyMutex *amutex);
  int main_handler(int event, void *data);

private:
  void make_requests();
  int active_req;
#ifdef GO_AWAY
  int total_req;
  FILE *file;
#endif
};

UpTest::UpTest(FILE * /* f ATS_UNUSED */, ProxyMutex *amutex)
  : Continuation(amutex),
    active_req(0)
#ifdef GO_AWAY
    ,
    total_req(0),
    file(f)
#endif
{
  SET_HANDLER(&UpTest::main_handler);
}

void
UpTest::make_requests()
{
  ink_release_assert(0);
// HDR FIX ME
#ifdef GO_AWAY
  while (active_req < MAX_ACTIVE_REQUESTS && file != NULL && total_req < MAX_TOTAL_REQUESTS) {
    char url_buf[2048];
    char req_buf[4096];

    if (!fgets(url_buf, 2047, file)) {
      Note("[Http Update Tester] url file exhausted");
      fclose(file);
      file = NULL;
      return;
    }
    url_buf[strlen(url_buf) - 1] = '\0';

    Debug("http_sch", "Firing off request for %s", url_buf);

    sprintf(req_buf, "GET %s HTTP/1.0\r\nCache-Control: max-age=0\r\n\r\n", url_buf);
    const char *req = req_buf;

    HTTPHdr test_req;
    HTTPParser http_parser;
    http_parser_init(&http_parser);
    test_req.create();

    test_req.parse_req(&http_parser, &req, req + strlen(req), false);
    http_parser_clear(&http_parser);

    HttpUpdateSM *current_reader = HttpUpdateSM::allocate();
    current_reader->init();
    Action *a = current_reader->start_scheduled_update(this, test_req);
    (void)a;

    active_req++;
    total_req++;
  }
#endif
}

int
UpTest::main_handler(int event, void * /* data ATS_UNUSED */)
{
  Debug("http_sch", "Received Event %s", HttpDebugNames::get_event_name(event));

  if (event != EVENT_NONE && event != VC_EVENT_IMMEDIATE) {
    active_req--;
  }

  make_requests();

  return EVENT_DONE;
}

void
init_http_update_test()
{
  FILE *f = fopen("urls", "r");

  if (f == NULL) {
    Warning("[Http Update Tester] could not open URL file");
    return;
  }

  UpTest *u = new UpTest(f, new_ProxyMutex());
  fclose(f); // UpTest doesn't take ownership of f.
  eventProcessor.schedule_imm(u);
}
