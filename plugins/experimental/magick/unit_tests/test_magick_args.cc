/**
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

#include "magick_args.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
/** Tokenize and validate a command exactly as the plugin does, so the tests exercise
 * the real path from decoded query parameter to verdict.
 */
bool
accepts(const std::string &command, std::string *reason = nullptr)
{
  CharVector buffer(command.begin(), command.end());

  buffer.push_back('\0');

  const CharPointerVector arguments = QueryParameterToArguments(buffer);

  return ValidateMagickArguments(arguments, reason);
}
} // namespace

TEST_CASE("ValidateMagickArguments accepts allowlisted commands")
{
  // Without at least one happy-path case, a validator that rejected everything
  // would ship green.
  SECTION("no operators")
  {
    REQUIRE(accepts("convert mpr:b mpr:a"));
  }
  SECTION("zero-argument operators")
  {
    REQUIRE(accepts("convert mpr:b -flip mpr:a"));
    REQUIRE(accepts("convert mpr:b -auto-orient -strip -trim mpr:a"));
  }
  SECTION("one-argument operators")
  {
    REQUIRE(accepts("convert mpr:b -resize 128x128 mpr:a"));
    REQUIRE(accepts("convert mpr:b -crop 100x100+10+20 mpr:a"));
    REQUIRE(accepts("convert mpr:b -quality 90 mpr:a"));
    REQUIRE(accepts("convert mpr:b -colorspace sRGB mpr:a"));
    REQUIRE(accepts("convert mpr:b -filter Lanczos -resize 50% mpr:a"));
    REQUIRE(accepts("convert mpr:b -background none mpr:a"));
    REQUIRE(accepts("convert mpr:b -background #ff0000 mpr:a"));
  }
  SECTION("a geometry may lead with a minus")
  {
    // A blanket "values may not start with -" test would break this.
    REQUIRE(accepts("convert mpr:b -rotate -90 mpr:a"));
  }
  SECTION("mixed")
  {
    REQUIRE(accepts("convert mpr:b -auto-orient -resize 800x600 -quality 85 -strip mpr:a"));
  }
}

TEST_CASE("ValidateMagickArguments enforces the command shape")
{
  SECTION("too few arguments")
  {
    REQUIRE_FALSE(accepts(""));
    REQUIRE_FALSE(accepts("convert"));
    REQUIRE_FALSE(accepts("convert mpr:a"));
  }
  SECTION("argv[0] must be convert")
  {
    REQUIRE_FALSE(accepts("mogrify mpr:b -flip mpr:a"));
    REQUIRE_FALSE(accepts("identify mpr:b mpr:a"));
  }
  SECTION("the mpr:b input register is mandatory")
  {
    // Without an input image convert fails, and mpr:a still holds whatever a
    // previous transform left in the process-global MPR registry.
    REQUIRE_FALSE(accepts("convert -flip mpr:a"));
    REQUIRE_FALSE(accepts("convert input.jpg -flip mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:c -flip mpr:a"));
  }
  SECTION("mpr:a must be last")
  {
    REQUIRE_FALSE(accepts("convert mpr:b mpr:a -flip"));
    REQUIRE_FALSE(accepts("convert mpr:b -flip output.png"));
    REQUIRE_FALSE(accepts("convert mpr:b -flip mpr:a extra"));
  }
  SECTION("a value may not swallow the trailing mpr:a")
  {
    REQUIRE_FALSE(accepts("convert mpr:b -resize mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -flip -quality mpr:a"));
  }
}

TEST_CASE("ValidateMagickArguments rejects the dangerous argv surface")
{
  SECTION("-write")
  {
    REQUIRE_FALSE(accepts("convert mpr:b -write /tmp/owned.png mpr:a"));
  }
  SECTION("@file")
  {
    REQUIRE_FALSE(accepts("convert mpr:b @/etc/passwd mpr:a"));
  }
  SECTION("coders")
  {
    REQUIRE_FALSE(accepts("convert mpr:b msl:/tmp/x.msl mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b url:http://example.com/x mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b ephemeral:/etc/passwd mpr:a"));
  }
  SECTION("non-allowlisted operators")
  {
    REQUIRE_FALSE(accepts("convert mpr:b -draw \"text 0,0 hi\" mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -annotate 0 hi mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -process x mpr:a"));
  }
  SECTION("+ spellings are rejected")
  {
    // ImageMagick's option table gives +resize an arity of 1 while
    // ConvertImageCommand consumes zero arguments for it, so accepting + forms
    // would desynchronize the position model from convert's actual parsing.
    REQUIRE_FALSE(accepts("convert mpr:b +resize 128x128 mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b +flip mpr:a"));
  }
  SECTION("operators dropped from the allowlist")
  {
    // -minify is not an ImageMagick CLI option in either version, so it reaches
    // UnrecognizedOption and fails the conversion.
    REQUIRE_FALSE(accepts("convert mpr:b -minify mpr:a"));
    // -format buys nothing (the output coder is fixed by the mpr:a filename) and
    // its value reaches InterpretImageProperties, which handles @path and %[fx:].
    REQUIRE_FALSE(accepts("convert mpr:b -format png mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -format \"%[fx:1]\" mpr:a"));
  }
}

TEST_CASE("ValidateMagickArguments rejects MagickCommandGenesis pre-scanned tokens")
{
  // MagickCommandGenesis walks argv[1..argc-2] with no concept of option arity
  // before ConvertImageCommand ever runs, so a value position is reachable as an
  // option. These two commands satisfy the shape and arity rules exactly and must
  // still be rejected on the value.
  SECTION("regression: -distribute-cache carried in a value position")
  {
    REQUIRE_FALSE(accepts("convert mpr:b -format -distribute-cache mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -colorspace -distribute-cache mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -quality -distribute-cache mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -background -distribute-cache mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -resize -distribute-cache mpr:a"));
  }
  SECTION("regression: -bench carried in a value position")
  {
    // -bench with a non-numeric value yields zero iterations, so Genesis returns
    // its initial MagickTrue without ever running the command, defeating both the
    // return-value check and the mandatory mpr:b input.
    REQUIRE_FALSE(accepts("convert mpr:b -format -bench mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -filter -bench mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -rotate -bench mpr:a"));
  }
  SECTION("every Genesis-scanned token, in both spellings")
  {
    for (const std::string token : {"-bench", "-concurrent", "-debug", "-distribute-cache", "-duration", "-regard-warnings"}) {
      REQUIRE_FALSE(accepts("convert mpr:b -colorspace " + token + " mpr:a"));
      REQUIRE_FALSE(accepts("convert mpr:b -colorspace +" + token.substr(1) + " mpr:a"));
      REQUIRE_FALSE(accepts("convert mpr:b " + token + " mpr:a"));
    }
  }
}

TEST_CASE("ValidateMagickArguments type-checks operator values")
{
  SECTION("integers")
  {
    REQUIRE(accepts("convert mpr:b -quality 90 mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -quality 90x90 mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -quality high mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -depth @/etc/passwd mpr:a"));
  }
  SECTION("geometries reject letters, so no option name fits")
  {
    REQUIRE(accepts("convert mpr:b -resize 100x100! mpr:a"));
    REQUIRE(accepts("convert mpr:b -resize 100x100^ mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -resize abc mpr:a"));
    // A geometry must contain a digit, so punctuation alone is not a value.
    REQUIRE_FALSE(accepts("convert mpr:b -resize -- mpr:a"));
    // @ is valid area geometry in ImageMagick but is excluded so a value can never
    // become an @file reference.
    REQUIRE_FALSE(accepts("convert mpr:b -resize @1000000 mpr:a"));
  }
  SECTION("enum tokens may not lead with a dash")
  {
    REQUIRE(accepts("convert mpr:b -gravity NorthWest mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -gravity -NorthWest mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -filter @file mpr:a"));
  }
  SECTION("colors")
  {
    REQUIRE(accepts("convert mpr:b -background rgb(255,0,0) mpr:a"));
    REQUIRE(accepts("convert mpr:b -background gray50 mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -background @/etc/passwd mpr:a"));
    REQUIRE_FALSE(accepts("convert mpr:b -background -red mpr:a"));
  }
  SECTION("overlong values")
  {
    REQUIRE_FALSE(accepts("convert mpr:b -colorspace " + std::string(65, 'a') + " mpr:a"));
  }
}

TEST_CASE("ValidateMagickArguments reports a reason")
{
  std::string reason;

  REQUIRE_FALSE(accepts("convert mpr:b -write /tmp/x mpr:a", &reason));
  REQUIRE(reason.find("-write") != std::string::npos);

  reason.clear();
  REQUIRE_FALSE(accepts("convert mpr:b -quality bad mpr:a", &reason));
  REQUIRE(reason.find("-quality") != std::string::npos);

  reason.clear();
  REQUIRE(accepts("convert mpr:b -flip mpr:a", &reason));
  REQUIRE(reason.empty());
}
