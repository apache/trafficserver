/**
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

#define DEFINE_JSONRPC_PROTO_FUNCTION(fn) ts::Rv<YAML::Node> fn(std::string_view const &id, const YAML::Node &params)

template <typename Iter, std::size_t N>
std::array<std::string, N>
chunk_impl(Iter from, Iter to)
{
  const std::size_t size = std::distance(from, to);
  if (size <= N) {
    return {std::string{from, to}};
  }
  std::size_t index{0};
  std::array<std::string, N> ret;
  const std::size_t each_part = size / N;
  const std::size_t remainder = size % N;

  for (auto it = from; it != to;) {
    if (std::size_t rem = std::distance(it, to); rem == (each_part + remainder)) {
      ret[index++] = std::string{it, it + rem};
      break;
    }
    ret[index++] = std::string{it, it + each_part};
    std::advance(it, each_part);
  }

  return ret;
}

template <std::size_t N>
auto
chunk(std::string_view v)
{
  return chunk_impl<std::string_view::const_iterator, N>(v.begin(), v.end());
}