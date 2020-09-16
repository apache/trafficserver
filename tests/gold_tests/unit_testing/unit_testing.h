/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <ts/ts.h>

namespace Au_UT
{
extern char const Debug_tag[];

// Delete file whose path is specified in the constructor when the instance is destroyed.
//
class FileDeleter
{
public:
  FileDeleter(std::string_view pathspec);

  ~FileDeleter();

private:
  std::string _pathspec;
};

using InProgress = std::shared_ptr<FileDeleter>;

// Create a statically-allocated object of this class to register a test function.  The function starts but may
// not finish the test.  To indicate that the test has finished, it should destroy the InProgress object that
// it is passed, plus all the copies that it makes of the object.  A test must cause traffic_server to exit
// with a non-zero exit value if it fails.
//
struct Test {
  Test(void (*test_func)(InProgress));
};

} // end namespace Au_UT

using namespace Au_UT;
