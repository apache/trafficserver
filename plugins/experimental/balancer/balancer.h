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

#ifndef BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273
#define BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273

#include <ts/ts.h>
#include <ts/remap.h>
#include <string>

// Return the length of a string literal.
template <int N>
unsigned
lengthof(const char (&)[N])
{
  return N - 1;
}

struct BalancerTarget {
  std::string name;
  unsigned port;
};

struct BalancerInstance {
  virtual ~BalancerInstance() {}
  virtual void push_target(const BalancerTarget &) = 0;
  virtual const BalancerTarget &balance(TSHttpTxn, TSRemapRequestInfo *) = 0;
};

BalancerInstance *MakeHashBalancer(const char *);
BalancerInstance *MakeRoundRobinBalancer(const char *);

#endif /* BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273 */
