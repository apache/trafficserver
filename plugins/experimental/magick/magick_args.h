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

#pragma once

#include <string>
#include <vector>

using CharVector        = std::vector<char>;
using CharPointerVector = std::vector<char *>;

/** Split a decoded `magick` query parameter into an argv vector.
 *
 * Tokens are separated by spaces, with double quotes grouping. The separators are
 * overwritten with NUL in place, so the returned pointers alias @a v and are only
 * valid while @a v lives and is not reallocated. Any trailing NUL bytes the caller
 * appended are ignored rather than turned into empty tokens.
 */
CharPointerVector QueryParameterToArguments(CharVector &v);

/** Defense-in-depth allowlist for the argv handed to ImageMagick.
 *
 * @param arguments argv as produced by QueryParameterToArguments().
 * @param reason if non-null, set to a human-readable rejection reason on failure.
 * @return true when every token is vetted and the command shape is exactly
 *         `convert mpr:b <operators...> mpr:a`.
 */
bool ValidateMagickArguments(const CharPointerVector &arguments, std::string *reason = nullptr);
