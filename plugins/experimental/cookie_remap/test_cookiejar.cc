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

#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <iostream>
#include <string>
#include <assert.h>
#include "catch.hpp"
#include "cookiejar.h"

using std::cout;
using std::endl;
using std::string;

TEST_CASE("Basic test with ; separated cookies", "[CookieJar]")
{
  std::cerr << "Verify individual cookie crumbs (semicolon separated)" << std::endl;
  CookieJar oreo;
  bool rc = oreo.create("fp=1;fn=2;sp=3;tl=4");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("fp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "1");

  rc = oreo.get_full("fn", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_full("sp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");

  rc = oreo.get_full("tl", val);
  REQUIRE(rc == true);
  REQUIRE(val == "4");

  rc = oreo.get_full("doesnotexist", val);
  REQUIRE(rc == false);
}

TEST_CASE("Basic test with space separated cookies", "[CookieJar]")
{
  std::cerr << "Verify individual cookie crumbs (space separated)" << std::endl;
  CookieJar oreo;
  bool rc = oreo.create("fp=1 fn=2    sp=3          tl=4");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("fp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "1");

  rc = oreo.get_full("fn", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_full("sp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");

  rc = oreo.get_full("tl", val);
  REQUIRE(rc == true);
  REQUIRE(val == "4");

  rc = oreo.get_full("doesnotexist", val);
  REQUIRE(rc == false);
}

TEST_CASE("Basic test with mixed delimiters", "[CookieJar]")
{
  std::cerr << "Verify individual cookie crumbs (mixed delimiters)" << std::endl;
  CookieJar oreo;
  bool rc = oreo.create("fp=1;fn=2 ;  sp=3 ;;     ; tl=4");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("fp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "1");

  rc = oreo.get_full("fn", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_full("sp", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");

  rc = oreo.get_full("tl", val);
  REQUIRE(rc == true);
  REQUIRE(val == "4");

  rc = oreo.get_full("doesnotexist", val);
  REQUIRE(rc == false);
}

TEST_CASE("Test with some empty values", "[CookieJar]")
{
  std::cerr << "Verify empty values" << std::endl;
  CookieJar oreo;
  bool rc = oreo.create("lastname=whatever;firstname=;age=100;salary=;dept=engineering");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("lastname", val);
  REQUIRE(rc == true);
  REQUIRE(val == "whatever");

  rc = oreo.get_full("firstname", val);
  REQUIRE(rc == true);
  REQUIRE(val == "");

  rc = oreo.get_full("age", val);
  REQUIRE(rc == true);
  REQUIRE(val == "100");

  rc = oreo.get_full("salary", val);
  REQUIRE(rc == true);
  REQUIRE(val == "");

  rc = oreo.get_full("dept", val);
  REQUIRE(rc == true);
  REQUIRE(val == "engineering");
}

TEST_CASE("Verify double quotes around values are stripped", "[CookieJar]")
{
  std::cerr << "Verify double quotes around values are stripped" << std::endl;
  CookieJar oreo;
  bool rc = oreo.create("lang=c;vcs=\"git\"");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("vcs", val);
  REQUIRE(rc == true);
  REQUIRE(val == "git");
}

TEST_CASE("Discard invalid cookie names", "[CookieJar]")
{
  std::cerr << "Discard invalid cookie names" << std::endl;

  // [] cannot be used in cookie names
  CookieJar oreo;
  bool rc = oreo.create("t=2;x=3;[invalid]=4;valid=5");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("t", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_full("x", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");

  rc = oreo.get_full("valid", val);
  REQUIRE(rc == true);
  REQUIRE(val == "5");

  rc = oreo.get_full("[invalid]", val);
  REQUIRE(rc == false);
}

TEST_CASE("Handle null values", "[CookieJar]")
{
  std::cerr << "Handle null values" << std::endl;
  CookieJar oreo;

  // perl's value will be ""
  // ancient will not be found because there is no = following it
  // = will not be found because there is no = following it
  // python will be "modern"
  bool rc = oreo.create("perl=  ancient  =;python=modern");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("perl", val);
  REQUIRE(rc == true);
  REQUIRE(val == "");

  rc = oreo.get_full("ancient", val);
  REQUIRE(rc == false);

  rc = oreo.get_full("=", val);
  REQUIRE(rc == false);

  rc = oreo.get_full("python", val);
  REQUIRE(rc == true);
  REQUIRE(val == "modern");
}

TEST_CASE("Verify subcookies are parsed", "[CookieJar]")
{
  std::cerr << "Verify subcookies are parsed" << std::endl;
  CookieJar oreo;

  bool rc = oreo.create("team1=spiderman=1&ironman=2&batman=3;team2=thor=1&wonderwoman=2&antman=3;superhero3=spiderman");
  REQUIRE(rc == true);

  string val;
  rc = oreo.get_full("team1", val);
  REQUIRE(rc == true);
  REQUIRE(val == "spiderman=1&ironman=2&batman=3");

  rc = oreo.get_full("superhero3", val);
  REQUIRE(rc == true);
  REQUIRE(val == "spiderman");

  rc = oreo.get_part("team1", "spiderman", val);
  REQUIRE(rc == true);
  REQUIRE(val == "1");

  rc = oreo.get_part("team1", "ironman", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_part("team1", "batman", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");

  rc = oreo.get_part("team2", "thor", val);
  REQUIRE(rc == true);
  REQUIRE(val == "1");

  rc = oreo.get_part("team2", "wonderwoman", val);
  REQUIRE(rc == true);
  REQUIRE(val == "2");

  rc = oreo.get_part("team2", "antman", val);
  REQUIRE(rc == true);
  REQUIRE(val == "3");
}
