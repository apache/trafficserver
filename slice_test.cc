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

#include "ContentRange.h"
#include "range.h"

#include <iostream>
#include <sstream>
#include <vector>

std::string
testContentRange()
{
  std::ostringstream oss;

  ContentRange null;
  if (null.isValid())
  {
    oss << "fail: null isValid test" << std::endl;
  }

  ContentRange const exprange(1023, 1048576, 307232768);

  if (! exprange.isValid())
  {
    oss << "Fail: exprange valid" << std::endl;
    oss << exprange.m_begin
      << ' ' << exprange.m_end
      << ' ' << exprange.m_length
      << std::endl;
  }

  std::string const expstr("bytes 1023-1048575/307232768");

  char gotbuf[1024];
  int gotlen = sizeof(gotbuf);

  bool const strstat(exprange.toStringClosed(gotbuf, &gotlen));

  if (! strstat)
  {
    oss << "failure status toStringClosed" << std::endl;
  }
  else
  if ((int)expstr.size() != gotlen)
  {
    oss << "Fail: expected toStringClosed length" << std::endl;
    oss << "got: " << gotlen << " exp: " << expstr.size() << std::endl;
    oss << "Got: " << gotbuf << std::endl;
    oss << "Exp: " << expstr << std::endl;
  }
  else
  if (expstr != gotbuf)
  {
    oss << "Fail: expected toStringClosed value" << std::endl;
    oss << "Got: " << gotbuf << std::endl;
    oss << "Exp: " << expstr << std::endl;
  }

  ContentRange gotrange;
  bool const gotstat(gotrange.fromStringClosed(expstr.c_str()));
  if (! gotstat)
  {
    oss << "fail: gotstat from string" << std::endl;
  }
  else
  if ( gotrange.m_begin != exprange.m_begin
    || gotrange.m_end != exprange.m_end
    || gotrange.m_length != exprange.m_length )
  {
    oss << "fail: value compare gotrange and exprange" << std::endl;
  }

  std::string const teststr("bytes 0-1048575/30723276");
  if (! gotrange.fromStringClosed(teststr.c_str()))
  {
    oss << "fail: parse teststr" << std::endl;
  }

  return oss.str();
}


struct Tests
{
  typedef std::string(* TestFunc)();
  std::vector<std::pair<TestFunc, char const *> > funcs;

  void
  add
    ( TestFunc const & func
    , char const * const fname
    )
  {
    funcs.push_back(std::make_pair(func, fname));
  }

  int
  run
    () const
  {
    int numfailed(0);
    for (std::pair<TestFunc, char const *> const & namefunc : funcs)
    {
      TestFunc const & func = namefunc.first;
      char const * const name = namefunc.second;

      std::cerr << name << " : ";

      std::string const fres(func());
      if (fres.empty())
      {
        std::cerr << "pass" << std::endl;
      }
      else
      {
        std::cerr << "FAIL" << std::endl;
        std::cerr << fres << std::endl;
        ++numfailed;
      }
    }
    return numfailed;
  }

};

int
main()
{
  Tests tests;
  tests.add(testContentRange, "testContentRange");
  return tests.run();
}
