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

# include "tsconfig/TsValue.h"
# include <cstdio>
# include <iostream>

using ts::config::Configuration;
using ts::config::Value;

inline std::ostream& operator << ( std::ostream& s, ts::ConstBuffer const& b ) {
  if (b._ptr) { s.write(b._ptr, b._size);
  } else { s << b._size;
}
  return s;
}

int main(int /* argc ATS_UNUSED */, char **/* argv ATS_UNUSED */) {
  printf("Testing TsConfig\n");
  ts::Rv<Configuration> cv = Configuration::loadFromPath("test-1.tsconfig");
  if (cv.isOK()) {
    Value v = cv.result().find("thing-1.name");
    if (v) {
      std::cout << "thing-1.name = " << v.getText() << std::endl;
    } else {
      std::cout << "Failed to find 'name' in 'thing-1'" << std::endl;
    }
  } else {
    std::cout << "Load failed" << std::endl
              << cv.errata()
      ;
  }
}
