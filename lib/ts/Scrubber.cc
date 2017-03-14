/** @file

  Implementation for Scrubber.h

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

#include "Scrubber.h"

Scrubber::Scrubber(const char *config)
{
  bool state_expecting_regex = true;
  const ts::StringView delimiters("->;,", ts::StringView::literal);
  ts::StringView regex;
  ts::StringView replacement;
  ts::StringView text(config);

  this->config = ats_strdup(config);

  // Loop over config string while extracting tokens
  while (text) {
    ts::StringView token = (text.ltrim(&isspace)).extractPrefix(&isspace);
    if (token) {
      // If we hit a delimiter, we change states and skip the token
      if (!ts::StringView(token).ltrim(delimiters)) {
        state_expecting_regex = !state_expecting_regex;
        continue;
      }

      // Store our current token
      if (state_expecting_regex) {
        regex = token;
      } else {
        replacement = token;
      }

      // If we have a pair of (regex, replacement) tokens, we can go ahead and store those values into Diags
      if (regex && replacement) {
        scrub_add(regex, replacement);
        regex.clear();
        replacement.clear();
      }
    }
  }

  // This means that there was some kind of config or parse error.
  // This doesn't catch *all* errors, and is just a heuristic so we can't actually print any meaningful message.
  if ((regex && !replacement) || (!regex && replacement)) {
    // Error("Error in scrubbing config parsing. Not enabling scrubbing.");
  }
}

Scrubber::~Scrubber()
{
  if (config) {
    ats_free(config);
  }

  for (auto s : scrubs) {
    delete s;
  }
}

bool
Scrubber::scrub_add(const ts::StringView pattern, const ts::StringView replacement)
{
  pcre *re;
  const char *error;
  int erroroffset;
  Scrub *s;

  // Temp storage on stack is probably OK. It's unlikely someone will have long enough strings to blow the stack
  char _pattern[pattern.size() + 1];
  sprintf(_pattern, "%.*s", static_cast<int>(pattern.size()), pattern.ptr());

  // compile the regular expression
  re = pcre_compile(_pattern, 0, &error, &erroroffset, NULL);
  if (!re) {
    // Error("Unable to compile PCRE scrubbing pattern");
    // Error("Scrubbing pattern failed at offset %d: %s.", erroroffset, error);
    return false;
  }

  // add the scrub pattern to our list
  s              = new Scrub;
  s->pattern     = pattern;
  s->replacement = replacement;
  s->compiled_re = re;
  scrubs.push_back(s);

  return true;
}

void
Scrubber::scrub_buffer(char *buffer) const
{
  // apply every Scrub in the vector, in order
  for (auto s : scrubs) {
    scrub_buffer(buffer, s);
  }
}

void
Scrubber::scrub_buffer(char *buffer, Scrub *scrub) const
{
  char *buffer_ptr;
  int num_matched;
  int match_len;
  int replacement_len;
  int buffer_len = strlen(buffer);

  // execute regex
  num_matched = pcre_exec(scrub->compiled_re, nullptr, buffer, buffer_len, 0, 0, scrub->ovector, scrub->OVECCOUNT);
  if (num_matched < 0) {
    switch (num_matched) {
    case PCRE_ERROR_NOMATCH:
      return;
    default:
      // Error("PCRE matching error %d\n", num_matched);
      break;
    }
  }

  /*
   * When scrubbing the buffer in place, there are 2 scenarios we need to consider:
   *
   *   1) The replacement text length is shorter or equal to the text we want to scrub away
   *   2) The replacement text is longer than the text we want to scrub away
   *
   * In case 1, we simply "slide" everything left a bit. Our final buffer should
   * look like this (where XXXX is the replacment text):
   *
   *                                new_end  orig_end
   *                                    V      V
   *   -----------------------------------------
   *   |ORIGINAL TEXT|XXXX|ORIGINAL TEXT|      |
   *   -----------------------------------------
   *
   * In case 2, since the final buffer would be longer than the original allocated buffer,
   * we need to truncate everything that would have run over the original end of the buffer.
   * The final buffer should look like this:
   *
   *                                         new_end
   *                                        orig_end
   *                                           V
   *   -----------------------------------------
   *   |ORIGINAL TEXT|XXXXXXXXXXXXXXXXXXX|ORIGI|NAL TEXT
   *   -----------------------------------------
   *
   */

  buffer_ptr      = buffer;
  match_len       = scrub->ovector[1] - scrub->ovector[0];
  replacement_len = scrub->replacement.size();

  if (replacement_len <= match_len) { // case 1
    // overwrite the matched text with the replacement text
    buffer_ptr += scrub->ovector[0];
    memcpy(buffer_ptr, scrub->replacement.ptr(), replacement_len);
    buffer_ptr += replacement_len;

    // slide everything after the matched text left to fill the gap including the '\0'
    memmove(buffer_ptr, buffer + scrub->ovector[1], buffer_len - scrub->ovector[1] + 1);

    // the last char should ALWAYS be a '\0' otherwise we did our math wrong
    ink_assert(buffer[strlen(buffer)] == '\0');
  } else { // case 2
    // first slide all the text after the matched text right and truncate as necessary
    int n_slide = buffer_len - scrub->ovector[0] - replacement_len;
    if (n_slide < 0) {
      // replacement string is too long, we need to shorten it
      replacement_len -= -n_slide;
    } else {
      buffer_ptr += scrub->ovector[0] + replacement_len;
      memmove(buffer_ptr, buffer + scrub->ovector[1], n_slide);
    }

    // next we put the replacement string into the gap we just created
    buffer_ptr = buffer + scrub->ovector[0];
    memcpy(buffer_ptr, scrub->replacement.ptr(), replacement_len);

    // finally, null terminate, since truncation will have removed the original '\0'
    buffer[buffer_len] = '\0';
  }
}
