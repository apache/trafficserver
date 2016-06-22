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

/****************************************************************************

  Show.h


 ****************************************************************************/

#ifndef _Show_h_
#define _Show_h_

#include "StatPages.h"

#define STREQ_PREFIX(_x, _s) (!strncasecmp(_x, _s, sizeof(_s) - 1))

struct ShowCont;
typedef int (ShowCont::*ShowContEventHandler)(int event, Event *data);
struct ShowCont : public Continuation {
private:
  char *buf, *start, *ebuf;

public:
  Action action;
  char *sarg;

  int
  show(const char *s, ...)
  {
    va_list aap, va_scratch;
    ptrdiff_t avail = ebuf - buf;
    ptrdiff_t needed;

    va_start(aap, s);
    va_copy(va_scratch, aap);
    needed = vsnprintf(buf, avail, s, va_scratch);
    va_end(va_scratch);

    if (needed >= avail) {
      ptrdiff_t bufsz = ebuf - start;
      ptrdiff_t used  = buf - start;

      Debug("cache_inspector", "needed %d bytes, reallocating to %d bytes", (int)needed, (int)bufsz + (int)needed);

      bufsz += ROUNDUP(needed, ats_pagesize());
      start = (char *)ats_realloc(start, bufsz);
      ebuf  = start + bufsz;
      buf   = start + used;
      avail = ebuf - buf;

      needed = vsnprintf(buf, avail, s, aap);
      va_end(aap);

      if (needed >= avail) {
        Debug("cache_inspector", "needed %d bytes, but had only %d", (int)needed, (int)avail + (int)needed);
        return EVENT_DONE;
      }
    }

    buf += needed;
    return EVENT_CONT;
  }

#define CHECK_SHOW(_x)  \
  if (_x == EVENT_DONE) \
    return complete_error(event, e);

  int
  finishConn(int event, Event *e)
  {
    if (!action.cancelled) {
      StatPageData data(start, buf - start);
      action.continuation->handleEvent(STAT_PAGE_SUCCESS, &data);
      start = 0;
    } else {
      ats_free(start);
      start = NULL;
    }
    return done(VIO::CLOSE, event, e);
  }

  int
  complete(int event, Event *e)
  {
    CHECK_SHOW(show("</BODY>\n</HTML>\n"));
    return finishConn(event, e);
  }

  int
  completeJson(int event, Event *e)
  {
    return finishConn(event, e);
  }

  int
  complete_error(int event, Event *e)
  {
    ats_free(start);
    start = NULL;
    if (!action.cancelled)
      action.continuation->handleEvent(STAT_PAGE_FAILURE, NULL);
    return done(VIO::ABORT, event, e);
  }

  int
  begin(const char *name)
  {
    return show("<HTML>\n<HEAD><TITLE>%s</TITLE>\n"
                "<BODY BGCOLOR=\"#ffffff\" FGCOLOR=\"#00ff00\">\n"
                "<H1>%s</H1>\n",
                name, name);
  }

  int
  showError(int event, Event *e)
  {
    return complete_error(event, e);
  }

  virtual int
  done(int /* e ATS_UNUSED */, int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    delete this;
    return EVENT_DONE;
  }

  ShowCont(Continuation *c, HTTPHdr * /* h ATS_UNUSED */) : Continuation(NULL), sarg(0)
  {
    size_t sz = ats_pagesize();

    mutex  = c->mutex;
    action = c;
    buf    = (char *)ats_malloc(sz);
    start  = buf;
    ebuf   = buf + sz;
  }

  ~ShowCont()
  {
    ats_free(sarg);
    ats_free(start);
  }
};

#endif
