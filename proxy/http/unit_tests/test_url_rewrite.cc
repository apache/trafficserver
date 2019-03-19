/** @file

  Catch-based tests for URL rewriting.

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

#include "catch.hpp"

#include "remap/UrlRewrite.h"
#include "remap/RemapConfig.h"

TEST_CASE("URL Rewrite", "[url-rewrite]")
{
  SECTION("001-text")
  {
    auto rewriter = new UrlRewrite;
    std::cout << "001 Text" << std::endl;
    remap_parse_config("unit_tests/remap_examples/remap_001.config", rewriter);
    rewriter->Print();
    delete rewriter;
  }
  SECTION("001-yaml")
  {
    auto rewriter = new UrlRewrite;
    std::cout << "001 YAML" << std::endl;
    remap_parse_config("unit_tests/remap_examples/remap_001.yaml", rewriter);
    rewriter->Print();
    delete rewriter;
  }
  SECTION("002-yaml")
  {
    auto rewriter = new UrlRewrite;
    std::cout << "002 YAML" << std::endl;
    remap_parse_config("unit_tests/remap_examples/remap_002.yaml", rewriter);
    rewriter->Print();
    delete rewriter;
  }
  SECTION("003-yaml")
  {
    auto rewriter = new UrlRewrite;
    std::cout << "003 YAML" << std::endl;
    remap_parse_config("unit_tests/remap_examples/remap_003.yaml", rewriter);
    rewriter->Print();
    delete rewriter;
  }
}
