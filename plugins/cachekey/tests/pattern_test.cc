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

#include "pattern.h"
#include <iostream>
#include <vector>

/**
 * @file pattern_test.cc
 * @brief simple test driver to test and experiment with PCRE patterns.
 *
 * @todo: create unit tests for the classes in pattern.cc.
 * Compile with -DCACHEKEY_UNIT_TEST to use non-ATS logging.
 */
int
main(int argc, char *argv[])
{
  if (argc < 3) {
    std::cerr << "Usage: " << String(argv[0]) << " subject pattern" << std::endl;
    return -1;
  }

  String subject(argv[1]);
  String pattern(argv[2]);

  std::cout << "subject: '" << subject << "'" << std::endl;
  std::cout << "pattern: '" << pattern << "'" << std::endl;

  Pattern p;
  if (p.init(pattern)) {
    std::cout << "--- matching ---" << std::endl;
    bool result = p.match(subject);
    std::cout << "subject:'" << subject << "' " << (char *)(result ? "matched" : "did not match") << " pattern:'" << pattern << "'"
              << std::endl;

    std::cout << "--- capture ---" << std::endl;
    StringVector v;
    result = p.capture(subject, v);
    for (StringVector::iterator it = v.begin(); it != v.end(); ++it) {
      std::cout << "capture: " << *it << std::endl;
    }

    std::cout << "--- replace ---" << std::endl;
    String r;
    result = p.replace(subject, r);
    std::cout << "replacement result:'" << result << "'" << std::endl;

  } else {
    std::cout << "pattern: '" << pattern << "' failed to compile" << std::endl;
  }
}
