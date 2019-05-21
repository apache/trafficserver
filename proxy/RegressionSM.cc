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

#include "P_EventSystem.h"
#include "RegressionSM.h"

#define REGRESSION_SM_RETRY (100 * HRTIME_MSECOND)

void
RegressionSM::set_status(int astatus)
{
  ink_assert(astatus != REGRESSION_TEST_INPROGRESS);
  // INPROGRESS < NOT_RUN < PASSED < FAILED
  if (status != REGRESSION_TEST_FAILED) {
    if (status == REGRESSION_TEST_PASSED) {
      if (astatus != REGRESSION_TEST_NOT_RUN) {
        status = astatus;
      }
    } else {
      // INPROGRESS or NOT_RUN
      status = astatus;
    }
  } // else FAILED is FAILED
}

void
RegressionSM::done(int astatus)
{
  if (pending_action) {
    pending_action->cancel();
    pending_action = nullptr;
  }
  set_status(astatus);
  if (pstatus) {
    *pstatus = status;
  }
  if (parent) {
    parent->child_done(status);
  }
}

void
RegressionSM::run(int *apstatus)
{
  pstatus = apstatus;
  run();
}

void
RegressionSM::xrun(RegressionSM *aparent)
{
  parent = aparent;
  parent->nwaiting++;
  run();
}

void
RegressionSM::run_in(int *apstatus, ink_hrtime t)
{
  pstatus = apstatus;
  SET_HANDLER(&RegressionSM::regression_sm_start);
  eventProcessor.schedule_in(this, t);
}

void
RegressionSM::child_done(int astatus)
{
  SCOPED_MUTEX_LOCK(l, mutex, this_ethread());
  ink_assert(nwaiting > 0);
  --nwaiting;
  set_status(astatus);
}

int
RegressionSM::regression_sm_waiting(int /* event ATS_UNUSED */, void *data)
{
  if (!nwaiting) {
    done(REGRESSION_TEST_NOT_RUN);
    delete this;
    return EVENT_DONE;
  }
  if (parallel || nwaiting > 1) {
    (static_cast<Event *>(data))->schedule_in(REGRESSION_SM_RETRY);
    return EVENT_CONT;
  }
  run();
  return EVENT_DONE;
}

int
RegressionSM::regression_sm_start(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  run();
  return EVENT_CONT;
}

RegressionSM *
r_sequential(RegressionTest *t, RegressionSM *sm, ...)
{
  RegressionSM *new_sm = new RegressionSM(t);
  va_list ap;
  va_start(ap, sm);
  new_sm->parallel = false;
  new_sm->repeat   = false;
  new_sm->ichild   = 0;
  new_sm->nwaiting = 0;
  while (sm) {
    new_sm->children.push_back(sm);
    sm = va_arg(ap, RegressionSM *);
  }
  new_sm->n = new_sm->children.size();
  va_end(ap);
  return new_sm;
}

RegressionSM *
r_sequential(RegressionTest *t, int an, RegressionSM *sm)
{
  RegressionSM *new_sm = new RegressionSM(t);
  new_sm->parallel     = false;
  new_sm->repeat       = true;
  new_sm->ichild       = 0;
  new_sm->children.push_back(sm);
  new_sm->nwaiting = 0;
  new_sm->n        = an;
  return new_sm;
}

RegressionSM *
r_parallel(RegressionTest *t, RegressionSM *sm, ...)
{
  RegressionSM *new_sm = new RegressionSM(t);
  va_list ap;
  va_start(ap, sm);
  new_sm->parallel = true;
  new_sm->repeat   = false;
  new_sm->ichild   = 0;
  new_sm->nwaiting = 0;
  while (sm) {
    new_sm->children.push_back(sm);
    sm = va_arg(ap, RegressionSM *);
  }
  new_sm->n = new_sm->children.size();
  va_end(ap);
  return new_sm;
}

RegressionSM *
r_parallel(RegressionTest *t, int an, RegressionSM *sm)
{
  RegressionSM *new_sm = new RegressionSM(t);
  new_sm->parallel     = true;
  new_sm->repeat       = true;
  new_sm->ichild       = 0;
  new_sm->children.push_back(sm);
  new_sm->nwaiting = 0;
  new_sm->n        = an;
  return new_sm;
}

void
RegressionSM::run()
{
  // TODO: Why introduce another scope here?
  {
    MUTEX_TRY_LOCK(l, mutex, this_ethread());
    if (!l.is_locked() || nwaiting > 1) {
      goto Lretry;
    }
    RegressionSM *x = nullptr;
    while (ichild < n) {
      if (!repeat) {
        x = children[ichild];
      } else {
        if (ichild != n - 1) {
          x = children[static_cast<intptr_t>(0)]->clone();
        } else {
          x = children[static_cast<intptr_t>(0)];
        }
      }
      if (!ichild) {
        nwaiting++;
      }
      x->xrun(this);
      ichild++;
      if (!parallel && nwaiting > 1) {
        goto Lretry;
      }
    }
  }
  nwaiting--;
  if (!nwaiting) {
    done(REGRESSION_TEST_NOT_RUN);
    delete this;
    return;
  }
Lretry:
  SET_HANDLER(&RegressionSM::regression_sm_waiting);
  pending_action = eventProcessor.schedule_in(this, REGRESSION_SM_RETRY);
}

RegressionSM::RegressionSM(const RegressionSM &ao) : Continuation(ao)
{
  RegressionSM &o = *const_cast<RegressionSM *>(&ao);

  t        = o.t;
  status   = o.status;
  pstatus  = o.pstatus;
  parent   = &o;
  nwaiting = o.nwaiting;

  for (auto &i : o.children) {
    children.push_back(i->clone());
  }

  n              = o.n;
  ichild         = o.ichild;
  parallel       = o.parallel;
  repeat         = o.repeat;
  pending_action = o.pending_action;

  ink_assert(status == REGRESSION_TEST_INPROGRESS);
  ink_assert(nwaiting == 0);
  ink_assert(ichild == 0);

  mutex = new_ProxyMutex();
}

struct ReRegressionSM : public RegressionSM {
  void
  run() override
  {
    if (time(nullptr) < 1) { // example test
      rprintf(t, "impossible");
      done(REGRESSION_TEST_FAILED);
    } else {
      done(REGRESSION_TEST_PASSED);
    }
  }
  ReRegressionSM(RegressionTest *at) : RegressionSM(at) {}
  RegressionSM *
  clone() override
  {
    return new ReRegressionSM(*this);
  }
  ReRegressionSM(const ReRegressionSM &o) : RegressionSM(o) { t = o.t; }
};

REGRESSION_TEST(RegressionSM)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  RegressionSM *top_sm = r_sequential(
    t, r_parallel(t, new ReRegressionSM(t), new ReRegressionSM(t), nullptr),
    r_sequential(t, new ReRegressionSM(t), new ReRegressionSM(t), nullptr), r_parallel(t, 3, new ReRegressionSM(t)),
    r_sequential(t, 3, new ReRegressionSM(t)),
    r_parallel(t, r_sequential(t, 2, new ReRegressionSM(t)), r_parallel(t, 2, new ReRegressionSM(t)), nullptr), nullptr);
  top_sm->run(pstatus);
}
