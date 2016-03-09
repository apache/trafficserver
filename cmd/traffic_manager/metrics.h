/** @file
 *
 *  Traffic Manager custom metrics.
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

#ifndef METRICS_H_D289E71B_AAC5_4CF3_9954_D54EDED60D1B
#define METRICS_H_D289E71B_AAC5_4CF3_9954_D54EDED60D1B

#if TS_USE_LUAJIT

bool metrics_binding_initialize(BindingInstance &binding);
void metrics_binding_destroy(BindingInstance &binding);
void metrics_binding_evaluate(BindingInstance &binding);

#else /* TS_USE_LUAJIT */

struct BindingInstance {
};

static inline bool
metrics_binding_initialize(BindingInstance &binding)
{
  return false;
}

static inline void
metrics_binding_destroy(BindingInstance &binding)
{
}

static inline void
metrics_binding_evaluate(BindingInstance &binding)
{
}

#endif /* TS_USE_LUAJIT */

#endif /* METRICS_H_D289E71B_AAC5_4CF3_9954_D54EDED60D1B */
