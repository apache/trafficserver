/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "util.h"

#include "slice.h"

std::pair<std::string, std::string>
keyValFrom(std::string line)
{
  std::pair<std::string, std::string> keyval;

  // strip any comments
  std::string::size_type const cpos(line.find_first_of("#"));
  if (std::string::npos != cpos) {
    if (0 == cpos) {
      return keyval;
    }

    line = line.substr(0, cpos - 1);
    DEBUG_LOG("Stripped comment '%s'", line.c_str());
  }

  if (line.empty()) {
    return keyval;
  }

  std::string::size_type const poskeybeg(line.find_first_not_of(" \t"));
  std::string::size_type const poskeyend(line.find_first_of(" =\t", poskeybeg + 1));

  if (std::string::npos == poskeyend) {
    return keyval;
  }

  std::string::size_type const posequal(line.find_first_of("=", poskeyend));

  std::string::size_type const posvalbeg(line.find_first_not_of(" \t", posequal + 1));

  if (std::string::npos == posvalbeg) {
    return keyval;
  }

  std::string::size_type const posvalend(line.find_first_of(" \t", posvalbeg + 1));

  keyval.first  = line.substr(poskeybeg, poskeyend - poskeybeg);
  keyval.second = line.substr(posvalbeg, posvalend - posvalbeg);

  return keyval;
}
