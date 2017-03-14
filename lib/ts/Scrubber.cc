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

char *
Scrubber::scrub_buffer(const char *buffer) const
{
  // apply every Scrub in the vector, in order
  char *scrubbed = strdup(buffer);
  for (auto s : scrubs) {
    char *new_scrubbed = scrub_buffer(scrubbed, s);
    ats_free(scrubbed);
    scrubbed = new_scrubbed;
  }
  return scrubbed;
}

char *
Scrubber::scrub_buffer(const char *buffer, Scrub *scrub) const
{
  int num_matched;
  char *scrubbed;
  char *scrubbed_idx;

  // execute regex
  num_matched = pcre_exec(scrub->compiled_re, nullptr, buffer, strlen(buffer), 0, 0, scrub->ovector, scrub->OVECCOUNT);
  if (num_matched < 0) {
    switch (num_matched) {
    case PCRE_ERROR_NOMATCH:
      return strdup(buffer);
    default:
      // Error("PCRE matching error %d\n", num_matched);
      break;
    }
  }

  // guaranteed to be big enough
  scrubbed     = static_cast<char *>(ats_malloc(strlen(buffer) + scrub->replacement.size() + 1));
  scrubbed_idx = scrubbed;

  // copy over all the stuff before the captured substing
  memcpy(scrubbed_idx, buffer, scrub->ovector[0]);
  scrubbed_idx += scrub->ovector[0];

  // copy over the scrubbed stuff
  int replacement_len = scrub->replacement.size();
  memcpy(scrubbed_idx, scrub->replacement.ptr(), replacement_len);
  scrubbed_idx += replacement_len;

  // copy over everything after the scrubbed stuff
  int trailing_len = strlen(buffer + scrub->ovector[1]);
  memcpy(scrubbed_idx, buffer + scrub->ovector[1], trailing_len);
  scrubbed_idx += trailing_len;

  // nul terminate
  *scrubbed_idx = '\0';

  return scrubbed;
}
