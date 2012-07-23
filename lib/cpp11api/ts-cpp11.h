/** @file
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

#ifndef TS_CPP11_H_
#define TS_CPP11_H_

#ifndef __GXX_EXPERIMENTAL_CXX0X__
#error The C++ Apache Traffic Server API wrapper requires C++11 support.
#endif

#include <functional>
#include <string>
#include <vector>

namespace ats {
namespace api {
typedef std::vector<std::string> StringVector;

enum class HookType {
  HOOK_PRE_REMAP = 100, HOOK_POST_REMAP, HOOK_READ_RESPONSE_HEADER, HOOK_READ_REQUEST_HEADER, HOOK_SEND_RESPONSE_HEADER
};

enum class NextState {
  HTTP_CONTINUE = 200, HTTP_ERROR, HTTP_DONT_CONTINUE
};

class Transaction;

/*
 * This is the type our global hook callbacks will take
 */
typedef std::function<NextState(Transaction &)> GlobalHookCallback;

/*
 * Create a new hook
 */
void CreateGlobalHook(HookType, GlobalHookCallback);
void CreateTransactionHook(Transaction &, HookType, GlobalHookCallback);

std::string GetPristineRequestUrl(Transaction &);
std::string GetRequestUrl(Transaction &);
std::string GetRequestUrlPath(Transaction &);

}
}

/*
 * Every plugin must have a simple entry point
 */
void PluginRegister(const ats::api::StringVector &);


#endif /* TS_CPP11_H_ */
