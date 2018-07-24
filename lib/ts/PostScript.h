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
#include <utility>

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
// Note that there is a bug when an actual parameter to the constructor is a pointer lvalue.  It only works properly
// with pointer rvalues.  So instead of a variable name, say p, the actual parameter must be p+0, &*p, or some such
// conversion to an rvalue.  See the unit tests (test_PostScript.cc) for an example.
// See also:
// https://stackoverflow.com/questions/51483598/in-c17-why-is-pointer-type-deduction-apparently-inconsistent-for-class-templa
//
template <typename Callable, typename... Args> class PostScript
{
public:
  PostScript(Callable f, Args &&... args) : _f(f), _argsTuple(std::forward<Args>(args)...) {}

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
