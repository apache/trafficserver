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

#include <algorithm>
#include <string_view>

CharPointerVector
QueryParameterToArguments(CharVector &v)
{
  CharPointerVector result;
  result.reserve(32);

  // The caller NUL-terminates the decoded buffer so the last token is a valid C
  // string; the terminator is not part of the command text.
  std::size_t size = v.size();

  while (size > 0 && '\0' == v[size - 1]) {
    --size;
  }

  std::size_t i = 0, j = 0;
  bool        quote = false;

  for (; i < size; ++i) {
    char &c = v[i];
    if ('"' == c) {
      if (i > j) {
        result.push_back(&v[j]);
      }
      c     = '\0';
      j     = i + 1;
      quote = !quote;
    } else if (!quote && ' ' == c) {
      if (i > j) {
        result.push_back(&v[j]);
      }
      c = '\0';
      j = i + 1;
    }
  }
  if (i > j) {
    result.push_back(&v[j]);
  }
  return result;
}

namespace
{
/** What a given operator's value is allowed to look like.
 *
 * Value positions used to be skipped without inspection, on the theory that
 * ConvertImageCommand consumes a declared parameter verbatim and never treats it
 * as a filename or coder. That is true of ConvertImageCommand, but it is not true
 * of MagickCommandGenesis(), which pre-scans the whole of argv[1..argc-2] with no
 * concept of option arity before the command ever runs. So a value position is
 * reachable as an option, and every value has to be typed.
 */
enum class ValueKind {
  Geometry, ///< 128x128, 50%, +10+20, -90, 100x100!
  Integer,  ///< 90, 8
  Token,    ///< an enum name such as sRGB, Lanczos, NorthWest
  Color,    ///< red, #ff0000, rgb(1,2,3), gray50
};

struct OneArgOption {
  std::string_view name;
  ValueKind        kind;
};

/** Operators that consume no value.
 *
 * Only the `-` spelling is listed. The `+` spellings are deliberately rejected:
 * ImageMagick's option table gives, for example, `+resize` an arity of 1 while
 * ConvertImageCommand consumes zero arguments for it, so allowing `+` forms would
 * desynchronize this validator's position model from convert's actual parsing.
 *
 * The arity in these two tables must match what ImageMagick's CommandOptions table
 * declares, and nothing in this tree checks that external invariant. The mistake
 * directions are not symmetric: listing a genuinely one-argument option here rejects
 * (safe), but listing a genuinely zero-argument option in oneArgOptions below makes
 * this code skip a token that ImageMagick will then treat as an operator or filename
 * (fails open). When in doubt, put an option here or leave it out entirely.
 */
constexpr std::string_view zeroArgOptions[] = {
  "-auto-orient", "-flip", "-flop", "-magnify", "-strip", "-transpose", "-transverse", "-trim",
};

constexpr OneArgOption oneArgOptions[] = {
  {"-background",    ValueKind::Color   },
  {"-blur",          ValueKind::Geometry},
  {"-colorspace",    ValueKind::Token   },
  {"-crop",          ValueKind::Geometry},
  {"-density",       ValueKind::Geometry},
  {"-depth",         ValueKind::Integer },
  {"-extent",        ValueKind::Geometry},
  {"-filter",        ValueKind::Token   },
  {"-gaussian-blur", ValueKind::Geometry},
  {"-gravity",       ValueKind::Token   },
  {"-interpolate",   ValueKind::Token   },
  {"-quality",       ValueKind::Integer },
  {"-resize",        ValueKind::Geometry},
  {"-rotate",        ValueKind::Geometry},
  {"-sample",        ValueKind::Geometry},
  {"-scale",         ValueKind::Geometry},
  {"-sharpen",       ValueKind::Geometry},
  {"-thumbnail",     ValueKind::Geometry},
};

/** Tokens MagickCommandGenesis() acts on while pre-scanning argv, before the
 * command callback runs. See MagickWand/magick-cli.c (ImageMagick 7) and
 * wand/mogrify.c (ImageMagick 6). The 6 loop compares option+1, so both the `-`
 * and `+` spellings match there.
 *
 * The value typing below already rejects all of these, because none of them fits
 * any ValueKind. This list is a redundant, explicit backstop so that widening a
 * ValueKind later cannot silently reopen the hole.
 */
constexpr std::string_view genesisScannedOptions[] = {
  "-bench",    "+bench",    "-concurrent",      "+concurrent",      "-debug", "+debug", "-distribute-cache", "+distribute-cache",
  "-duration", "+duration", "-regard-warnings", "+regard-warnings",
};

/// Values longer than this are rejected outright; no legitimate operator value is.
constexpr std::size_t maxValueLength = 64;

bool
inSet(std::string_view allowed, const char c)
{
  return allowed.find(c) != std::string_view::npos;
}

bool
isDigit(const char c)
{
  return '0' <= c && c <= '9';
}

bool
isAlnum(const char c)
{
  return isDigit(c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

/** A geometry may lead with `-` (as in `-rotate -90`), so a bare "no leading dash"
 * test is not usable here. Instead the character set excludes every letter, which
 * is what keeps an option name out of a geometry position, and `@` is excluded so a
 * value can never become an @file reference even though `@` is valid area geometry.
 */
bool
isGeometry(const std::string_view value)
{
  constexpr std::string_view punctuation = "xX%+-!<>^.,";
  bool                       digit       = false;

  for (const char c : value) {
    if (isDigit(c)) {
      digit = true;
    } else if (!inSet(punctuation, c)) {
      return false;
    }
  }

  return digit;
}

bool
isInteger(const std::string_view value)
{
  return !value.empty() && std::all_of(value.begin(), value.end(), isDigit);
}

bool
isToken(const std::string_view value)
{
  if (value.empty() || !isAlnum(value.front())) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](const char c) { return isAlnum(c) || '-' == c || '_' == c; });
}

bool
isColor(const std::string_view value)
{
  constexpr std::string_view punctuation = "#%(),.";

  if (value.empty() || (!isAlnum(value.front()) && '#' != value.front())) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [&](const char c) { return isAlnum(c) || inSet(punctuation, c); });
}

bool
isValidValue(const ValueKind kind, const std::string_view value)
{
  if (value.empty() || value.size() > maxValueLength) {
    return false;
  }

  if (std::find(std::begin(genesisScannedOptions), std::end(genesisScannedOptions), value) != std::end(genesisScannedOptions)) {
    return false;
  }

  switch (kind) {
  case ValueKind::Geometry:
    return isGeometry(value);
  case ValueKind::Integer:
    return isInteger(value);
  case ValueKind::Token:
    return isToken(value);
  case ValueKind::Color:
    return isColor(value);
  }

  return false;
}

} // namespace

/** Even a signed request must not reach ImageMagick's dangerous argv surface
 * (-write, @file, and the msl:/url:/ephemeral: coders), so only vetted convert
 * operators are allowed and the only "file" tokens permitted are the plugin's own
 * mpr:a/mpr:b registers.
 *
 * The shape is pinned to exactly `convert mpr:b <operators...> mpr:a`. mpr:b is
 * mandatory, not merely position constrained: without it the command has no input
 * image, convert fails, and mpr:a still holds whatever a previous transform left in
 * the process-global MPR registry.
 *
 * Both an operator and its value are checked. Checking the operator alone is not
 * sufficient, because MagickCommandGenesis() pre-scans argv without regard for
 * arity and so can pick up a value token as an option.
 */
bool
ValidateMagickArguments(const CharPointerVector &arguments, std::string *reason)
{
  const auto reject = [reason](std::string &&message) {
    if (nullptr != reason) {
      *reason = std::move(message);
    }
    return false;
  };

  if (arguments.size() < 3) {
    return reject("expected `convert mpr:b <operators> mpr:a`");
  }

  const std::size_t last = arguments.size() - 1;

  for (const char *const argument : arguments) {
    if (nullptr == argument) {
      return reject("null argument");
    }
  }

  // ImageMagick treats argv[0] as the command name. The input register must be
  // read first and the output register written last.
  if (std::string_view(arguments[0]) != "convert" || std::string_view(arguments[1]) != "mpr:b" ||
      std::string_view(arguments[last]) != "mpr:a") {
    return reject("expected `convert mpr:b <operators> mpr:a`");
  }

  for (std::size_t i = 2; i < last; ++i) {
    const std::string_view token(arguments[i]);

    if (std::find(std::begin(zeroArgOptions), std::end(zeroArgOptions), token) != std::end(zeroArgOptions)) {
      continue;
    }

    const auto option =
      std::find_if(std::begin(oneArgOptions), std::end(oneArgOptions), [token](const OneArgOption &o) { return o.name == token; });

    if (option == std::end(oneArgOptions)) {
      return reject("disallowed operator: " + std::string(token));
    }

    // Consume the operator's value by position, matching convert's own argv
    // handling. The value may not swallow the trailing mpr:a.
    if (++i >= last) {
      return reject("missing value for operator: " + std::string(token));
    }

    if (const std::string_view value(arguments[i]); !isValidValue(option->kind, value)) {
      return reject("disallowed value for " + std::string(token) + ": " + std::string(value));
    }
  }

  return true;
}
