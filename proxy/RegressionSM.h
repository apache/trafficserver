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

#pragma once

#include "I_EventSystem.h"
#include "tscore/Regression.h"

/*
  Regression Test Composition State Machine

  See RegressionSM.cc at the end for an example
*/

struct RegressionSM : public Continuation {
  RegressionTest *t = nullptr; // for use with rprint

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
  int status           = REGRESSION_TEST_INPROGRESS;
  int *pstatus         = nullptr;
  RegressionSM *parent = nullptr;
  int nwaiting         = 0;
  int nchildren        = 0;
  std::vector<RegressionSM *> children;
  intptr_t n             = 0;
  intptr_t ichild        = 0;
  bool parallel          = false;
  bool repeat            = false;
  Action *pending_action = nullptr;

  int regression_sm_start(int event, void *data);
  int regression_sm_waiting(int event, void *data);
  void set_status(int status);
  void child_done(int status);
  void xrun(RegressionSM *parent);

  RegressionSM(RegressionTest *at = nullptr) : t(at) { mutex = new_ProxyMutex(); }

  RegressionSM(const RegressionSM &);
};

RegressionSM *r_sequential(RegressionTest *t, int n, RegressionSM *sm);
RegressionSM *r_sequential(RegressionTest *t, RegressionSM *sm, ...); // terminate list in NULL
RegressionSM *r_parallel(RegressionTest *t, int n, RegressionSM *sm);
RegressionSM *r_parallel(RegressionTest *t, RegressionSM *sm, ...); // terminate list in NULL
