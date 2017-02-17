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

#include <string>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/ink_atomic.h>
#include <ts/ink_inet.h>
#include <ts/experimental.h>

#define PLUGIN_NAME "balancer"

// Return the length of a string literal.
template <int N>
unsigned
lengthof(const char(&)[N])
{
  return N - 1;
}


struct BalancerTarget {
  uint id;
  std::string name;
  uint port;

  //add by daemon.xie
  uint weight;  //配置的权重
  int effective_weight;
  int current_weight; //当前权重，ats 会在运行过程中调整次权重

  uint max_fails; //最大失败次数

  time_t fail_timeout; //失败后，不再使用的时间
  uint down; //指定某个后端是否挂了
  uint backup;  //是否为备份线路

  uint fails; //已尝试失败次数
  uint timeout_fails;//当停用fail_timeout后，仍然是失败时+1,最大次数不能超过100
  time_t accessed; //检测失败时间，用于计算超时
  time_t checked;

  BalancerTarget():id(0),name(""),port(0),weight(1),effective_weight(1),current_weight(0),max_fails(10),
		  fail_timeout(30),down(0),backup(0),fails(0),timeout_fails(1),accessed(0),checked(0){
  }

  ~BalancerTarget() {
	  name = "";
  }
};

//用于存储target 状态，以备源站返回code 的做健康负载处理，new  free
struct BalancerTargetStatus {
	uint target_id;
	uint target_down;
	uint is_down_check;
	int object_status;
};

#endif /* BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273 */
