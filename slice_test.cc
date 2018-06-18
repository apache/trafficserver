/*
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

/*
 * These are misc unit tests for slicer
 */

#include "util.h"

#include <iostream>
#include <sstream>

std::string
testKeyVal()
{
  std::string res;

  std::string const teststring("threads = 7 # comment\n"
                               "#another comment\n"
                               "blocksize =1024\n"
                               "input_watermark_bytes= 524289\n"
                               "output_watermark_bytes=524214\n"
                               " threads=2\n"
                               "#key=val\n");

  std::istringstream istr(teststring);

  std::vector<std::pair<std::string, std::string>> keyvalsgot;

  std::string line;
  while (std::getline(istr, line)) {
    std::pair<std::string, std::string> keyval(keyValFrom(std::move(line)));

    if (!keyval.first.empty() || !keyval.second.empty()) {
      std::cerr << keyval.first << ' ' << keyval.second << std::endl;
      keyvalsgot.push_back(std::move(keyval));
    }
  }

  return res;
}

int
main()
{
  int status(0);

  {
    std::string const kvres(testKeyVal());
    if (!kvres.empty()) {
      status = 1;
    }
  }

  return status;
}
