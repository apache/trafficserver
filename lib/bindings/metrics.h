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

#ifndef METRICS_H_FED1F5EA_9EDE_48E6_B05A_5DCAFD8DC319
#define METRICS_H_FED1F5EA_9EDE_48E6_B05A_5DCAFD8DC319

// Create a new metrics binding userdata object.
int lua_metrics_new(const char *prefix, lua_State *L);

// Register metrics binding type metatable.
void lua_metrics_register(lua_State *L);

// Install new metrics objects into the global namespace. This function
// iterates over all the registered metrics and installs a metrics
// object at the global name given by the metric's prefix. For example,
// if the metric is named "proxy.my.great.counter", it would install
// a metrics object at the global name "proxy.my.great".
int lua_metrics_install(lua_State *L);

#endif /* METRICS_H_FED1F5EA_9EDE_48E6_B05A_5DCAFD8DC319 */
