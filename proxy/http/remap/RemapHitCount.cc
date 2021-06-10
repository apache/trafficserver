/** @file

  Stats to track remap rule matches

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

#include "RemapHitCount.h"
#include "UrlRewrite.h"

extern UrlRewrite *rewrite_table;

struct ShowRemapCount : public ShowCont {
  ShowRemapCount(Continuation *c, HTTPHdr *h) : ShowCont(c, h) { SET_HANDLER(&ShowRemapCount::showHandler); }
  int
  showHandler(int event, Event *e)
  {
    auto table = rewrite_table->acquire();
    CHECK_SHOW(show(rewrite_table->PrintRemapHits().c_str()));
    table->release();
    return completeJson(event, e);
  }
};

Action *
register_ShowRemapHitCount(Continuation *c, HTTPHdr *h)
{
  ShowRemapCount *s = new ShowRemapCount(c, h);
  this_ethread()->schedule_imm(s);
  return &s->action;
}
