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

#pragma once

#include "Http3FrameGenerator.h"
#include "Http3Frame.h"

class Http3Transaction;
class VIO;

class Http3DataFramer : public Http3FrameGenerator
{
public:
  Http3DataFramer(Http3Transaction *transaction, VIO *source);

  // Http3FrameGenerator
  Http3FrameUPtr generate_frame() override;
  bool is_done() const override;

private:
  Http3Transaction *_transaction = nullptr;
  VIO *_source_vio               = nullptr;
};
