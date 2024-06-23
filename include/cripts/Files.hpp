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
#pragma once

#include <filesystem>
#include <fstream>

#include "cripts/Lulu.hpp"

namespace File
{

class Path : public std::filesystem::path
{
  using super_type = std::filesystem::path;
  using self_type  = Path;

public:
  using super_type::super_type;

  self_type &rebase();
};

using Type = std::filesystem::file_type;

std::filesystem::file_status Status(const Path &path);

namespace Line
{
  class Reader
  {
    using self_type = Reader;

  public:
    Reader()                          = delete;
    Reader(const self_type &)         = delete;
    void operator=(const self_type &) = delete;

    explicit Reader(const std::string &path) : _path(path), _stream{path} {}
    explicit Reader(const Cript::string_view path) : _path(path), _stream{_path} {}

    operator Cript::string() { return line(); }

    Cript::string
    line()
    {
      Cript::string line;

      if (_stream) {
        std::getline(_stream, line);
      }

      return line;
    }

  private:
    File::Path    _path;
    std::ifstream _stream;
  }; // namespace Reader

} // namespace Line

} // namespace File
