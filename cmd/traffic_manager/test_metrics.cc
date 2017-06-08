/*

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

#include "ts/Regression.h"
#include "ts/TestBox.h"
#include "ts/I_Layout.h"
#include "LocalManager.h"
#include "RecordsConfig.h"
#include "P_RecLocal.h"
#include "metrics.h"

LocalManager *lmgmt = nullptr;

// Check that we can load and delete metrics.
REGRESSION_TEST(LoadMetrics)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);

  box = REGRESSION_TEST_PASSED;

  BindingInstance binding;
  box.check(metrics_binding_initialize(binding), "initialize metrics");
  metrics_binding_destroy(binding);
}

// Check that we can set a value.
REGRESSION_TEST(EvalMetrics)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);

  box = REGRESSION_TEST_PASSED;

  const char *config = R"(
integer 'proxy.node.test.value' [[
  return 5
]]
  )";

  BindingInstance binding;

  box.check(metrics_binding_initialize(binding), "initialize metrics");
  box.check(binding.eval(config), "load metrics config");

  metrics_binding_evaluate(binding);

  RecInt value = 0;
  box.check(RecGetRecordInt("proxy.node.test.value", &value) == REC_ERR_OKAY, "read value (5) from proxy.node.test.value");
  box.check(value == 5, "proxy.node.test.value was %" PRId64 ", wanted 5", value);

  metrics_binding_destroy(binding);
}

int
main(int argc, const char **argv)
{
  Layout::create();
  RecLocalInit();
  LibRecordsConfigInit();
  return RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);
}
