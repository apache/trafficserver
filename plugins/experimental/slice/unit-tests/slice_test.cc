/** @file
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
#include "Range.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

std::string
testContentRange()
{
  std::ostringstream oss;

  ContentRange null;
  if (null.isValid()) {
    oss << "fail: null isValid test" << std::endl;
  }

  ContentRange const exprange(1023, 1048576, 307232768);

  if (!exprange.isValid()) {
    oss << "Fail: exprange valid" << std::endl;
    oss << exprange.m_beg << ' ' << exprange.m_end << ' ' << exprange.m_length << std::endl;
  }

  std::string const expstr("bytes 1023-1048575/307232768");

  char gotbuf[1024];
  int gotlen = sizeof(gotbuf);

  bool const strstat(exprange.toStringClosed(gotbuf, &gotlen));

  if (!strstat) {
    oss << "failure status toStringClosed" << std::endl;
  } else if ((int)expstr.size() != gotlen) {
    oss << "Fail: expected toStringClosed length" << std::endl;
    oss << "got: " << gotlen << " exp: " << expstr.size() << std::endl;
    oss << "Got: " << gotbuf << std::endl;
    oss << "Exp: " << expstr << std::endl;
  } else if (expstr != gotbuf) {
    oss << "Fail: expected toStringClosed value" << std::endl;
    oss << "Got: " << gotbuf << std::endl;
    oss << "Exp: " << expstr << std::endl;
  }

  ContentRange gotrange;
  bool const gotstat(gotrange.fromStringClosed(expstr.c_str()));
  if (!gotstat) {
    oss << "fail: gotstat from string" << std::endl;
  } else if (gotrange.m_beg != exprange.m_beg || gotrange.m_end != exprange.m_end || gotrange.m_length != exprange.m_length) {
    oss << "fail: value compare gotrange and exprange" << std::endl;
  }

  std::string const teststr("bytes 0-1048575/30723276");
  if (!gotrange.fromStringClosed(teststr.c_str())) {
    oss << "fail: parse teststr" << std::endl;
  }

  return oss.str();
}

std::string
testParseRange()
{
  std::ostringstream oss;

  std::vector<std::string> const teststrings = {
    "bytes=0-1023",       "bytes=1-1024", "bytes=11-11",
    "bytes=1-",           // 2nd byte to end
    "Range: bytes=-13",   // final 13 bytes
    "bytes=3-17",         // ,23-29" // open
    "bytes=3 -17 ",       //,18-29" // adjacent
    "bytes=3- 17",        //, 11-29" // overlapping
    "bytes=3 - 11",       //,13-17 , 23-29" // unsorted triplet
    "bytes=3-11 ",        //,13-17, 23-29" // unsorted triplet
    "bytes=0-0",          //,-1" // first and last bytes
    "bytes=-20",          // last 20 bytes of file
    "bytes=-60-50",       // invalid fully negative
    "bytes=17-13",        // degenerate
    "bytes 0-1023/146515" // this should be rejected (Content-range)
  };                      // invalid

  std::vector<Range> const exps = {Range{0, 1023 + 1}, Range{1, 1024 + 1}, Range{11, 11 + 1}, Range{1, Range::maxval},
                                   Range{-1, -1},      Range{3, 17 + 1},   Range{3, 17 + 1},  Range{3, 17 + 1},
                                   Range{3, 11 + 1},   Range{3, 11 + 1},   Range{0, 1},       Range{-20, 0},
                                   Range{-1, -1},      Range{-1, -1},      Range{-1, -1}};

  std::vector<bool> const expsres = {true, true, true, true, false, true, true, true, true, true, true, true, false, false, false};

  assert(exps.size() == teststrings.size());

  std::vector<Range> gots;
  gots.reserve(exps.size());
  std::vector<bool> gotsres;

  for (std::string const &str : teststrings) {
    Range rng;
    gotsres.push_back(rng.fromStringClosed(str.c_str()));
    gots.push_back(rng);
  }

  assert(gots.size() == exps.size());

  for (size_t index(0); index < gots.size(); ++index) {
    if (exps[index] != gots[index] || expsres[index] != gotsres[index]) {
      oss << "Eror parsing index: " << index << std::endl;
      oss << "test: '" << teststrings[index] << "'" << std::endl;
      oss << "exp: " << exps[index].m_beg << ' ' << exps[index].m_end << std::endl;
      oss << "expsres: " << (int)expsres[index] << std::endl;
      oss << "got: " << gots[index].m_beg << ' ' << gots[index].m_end << std::endl;
      oss << "gotsres: " << (int)gotsres[index] << std::endl;
    }
  }

  return oss.str();
}

struct Tests {
  typedef std::string (*TestFunc)();
  std::vector<std::pair<TestFunc, char const *>> funcs;

  void
  add(TestFunc const &func, char const *const fname)
  {
    funcs.push_back(std::make_pair(func, fname));
  }

  int
  run() const
  {
    int numfailed(0);
    for (std::pair<TestFunc, char const *> const &namefunc : funcs) {
      TestFunc const &func   = namefunc.first;
      char const *const name = namefunc.second;

      std::cerr << name << " : ";

      std::string const fres(func());
      if (fres.empty()) {
        std::cerr << "pass" << std::endl;
      } else {
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
  tests.add(testParseRange, "testParseRange");
  return tests.run();
}
