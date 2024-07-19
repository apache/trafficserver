/** @file

   fuzzing proxy/http3frame

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
*/

#include "proxy/http3/Http3Frame.h"
#include "proxy/http3/Http3Config.h"
#include "proxy/http3/Http3FrameDispatcher.h"
#include "proxy/http3/Http3SettingsHandler.h"

#include "records/RecordsConfig.h"
#include "tscore/Layout.h"

#define kMinInputLength 8
#define kMaxInputLength 1024

#define TEST_THREADS 1

bool
DoInitialization()
{
  Layout::create();
  RecProcessInit();
  LibRecordsConfigInit();

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(TEST_THREADS);
  ts::Http3Config::startup();

  return true;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 1;
  }

  static bool Initialized = DoInitialization();

  MIOBuffer *input1 = new_MIOBuffer(BUFFER_SIZE_INDEX_128);
  input1->write(input_data, size_data);
  IOBufferReader *input_reader1 = input1->alloc_reader();
   
  Http3FrameFactory frame_factory;
  frame_factory.fast_create(*input_reader1);

  free_MIOBuffer(input1);

  return 0;
}
