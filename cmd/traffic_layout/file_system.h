/** @file

  A brief file description

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

#pragma once

#include <string>

// binary executable mode (for symlink)
#define BIN_MODE 33261

// some system does not have OPEN_MAX defined
// size can be changed accordingly
#define OPEN_MAX_FILE 256

// append slash & remove slash of path for convinient use
void append_slash(std::string &path);

void remove_slash(std::string &path);

// some checks for directory exist or is it a directory
bool exists(std::string const &dir);

bool is_directory(std::string const &directory);

// for file system
bool create_directory(const std::string &dir);

bool remove_directory(const std::string &dir);

// remove everything inside this directory
bool remove_inside_directory(const std::string &dir);

bool copy_directory(const std::string &src, const std::string &dst);
