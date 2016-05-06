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

#ifndef _RegressionSM_h
#define _RegressionSM_h

#include "I_EventSystem.h"
#include "ts/Regression.h"
#include "ts/DynArray.h"

/*
  Regression Test Composition State Machine

  See RegressionSM.cc at the end for an example
*/

struct RegressionSM : public Continuation {
  RegressionTest *t; // for use with rprint

  // methods to override
  virtual void run(); // replace with leaf regression
  virtual RegressionSM *
  clone()
  {
    return new RegressionSM(*this);
  } // replace for run_xxx(int n,...);

  // public API
  void done(int status = REGRESSION_TEST_NOT_RUN);
  void run(int *pstatus);
  void run_in(int *pstatus, ink_hrtime t);

  // internal
  int status;
  int *pstatus;
  RegressionSM *parent;
  int nwaiting;
  int nchildren;
  DynArray<RegressionSM *> children;
  intptr_t n, ichild;
  bool par, rep;
  Action *pending_action;

  int regression_sm_start(int event, void *data);
  int regression_sm_waiting(int event, void *data);
  void set_status(int status);
  void child_done(int status);
  void xrun(RegressionSM *parent);

  RegressionSM(RegressionTest *at = NULL)
    : t(at),
      status(REGRESSION_TEST_INPROGRESS),
      pstatus(0),
      parent(0),
      nwaiting(0),
      nchildren(0),
      children(0),
      ichild(0),
      par(false),
      rep(false),
      pending_action(0)
  {
    mutex = new_ProxyMutex();
  }

  RegressionSM(const RegressionSM &);
};

RegressionSM *r_sequential(RegressionTest *t, int n, RegressionSM *sm);
RegressionSM *r_sequential(RegressionTest *t, RegressionSM *sm, ...); // terminate list in NULL
RegressionSM *r_parallel(RegressionTest *t, int n, RegressionSM *sm);
RegressionSM *r_parallel(RegressionTest *t, RegressionSM *sm, ...); // terminate list in NULL

#endif
