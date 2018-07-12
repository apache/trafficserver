/** @file

   Generic "guard" class templates.  The destructor calls a function object with arbitrary parameters.  This utility is
   available in both the core and plugins.

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

#include <tuple>

namespace ts
{
// The destructor of this class calls the function object passed to its constructor, with the given arguments.
// For example:
//   ts::PostScript g(TSHandleMLocRelease, bufp, parent, hdr);
//
// The release() member will prevent the call to the function upon destruction.
//
// Helpful in avoiding errors due to exception throws or error function return points, like the one that caused
// Heartbleed.
//
template <typename Callable, typename... Args> class PostScript
{
public:
  PostScript(Callable f, Args &&... args) : _f(f), _argsTuple(args...) {}

  ~PostScript()
  {
    if (_armed) {
      std::apply(_f, _argsTuple);
    }
  }

  void
  release()
  {
    _armed = false;
  }

  // No copying or moving.
  PostScript(const PostScript &) = delete;
  PostScript &operator=(const PostScript &) = delete;

private:
  bool _armed = true;
  Callable _f;
  std::tuple<Args...> _argsTuple;
};

} // end namespace ts
