#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HttpCompat.h"
#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern int cmd_disable_pfreelist;

namespace
{
constexpr size_t MAX_INPUT_LENGTH = 16 * 1024;

void
exercise_lists(const std::string &value, const std::string &candidate, uint8_t flags, int output_length)
{
  StrList               comma_list((flags & 1) != 0);
  StrList               semicolon_list((flags & 2) != 0);
  StrList               token_list((flags & 4) != 0);
  std::array<char, 256> output{};
  std::array<char, 256> type{};
  std::array<char, 256> subtype{};
  int                   matching_length  = 0;
  int                   matching_index   = 0;
  int                   value_length     = static_cast<int>(value.size());
  int                   candidate_length = static_cast<int>(candidate.size());
  char                  separator        = (flags & 8) != 0 ? ';' : ',';
  bool                  ignore_wildcards = (flags & 16) != 0;

  HttpCompat::parse_comma_list(&comma_list, value.c_str(), value_length);
  HttpCompat::parse_semicolon_list(&semicolon_list, value.c_str(), value_length);
  HttpCompat::parse_tok_list(&token_list, (flags & 32) != 0, value.c_str(), value_length, separator);
  HttpCompat::lookup_param_in_strlist(&semicolon_list, "q", output.data(), output_length);
  HttpCompat::lookup_param_in_strlist(&semicolon_list, "charset", output.data(), output_length);
  HttpCompat::lookup_param_in_semicolon_string(value.c_str(), value_length, candidate.c_str(), output.data(), output_length);
  HttpCompat::find_Q_param_in_strlist(&semicolon_list);
  HttpCompat::match_accept_language(candidate.c_str(), candidate_length, &comma_list, &matching_length, &matching_index,
                                    ignore_wildcards);
  HttpCompat::match_accept_charset(candidate.c_str(), candidate_length, &comma_list, &matching_index, ignore_wildcards);
  HttpCompat::parse_mime_type(value.c_str(), type.data(), subtype.data(), output_length, output_length);
  HttpCompat::parse_mime_type_with_len(value.c_str(), value_length, type.data(), subtype.data(), output_length, output_length);
}

MIMEField *
add_field(HTTPHdr &header, std::string_view value)
{
  MIMEField *field = header.field_create("Accept-Language");

  field->value_set(header.m_heap, header.m_mime, value);
  header.field_attach(field);
  return field;
}

void
exercise_vary(const std::string &first, const std::string &second, uint8_t flags)
{
  HTTPHdr        first_header;
  HTTPHdr        second_header;
  ts::PostScript cleanup([&]() {
    second_header.destroy();
    first_header.destroy();
  });

  first_header.create(HTTPType::REQUEST);
  second_header.create(HTTPType::REQUEST);
  MIMEField *first_field  = (flags & 64) != 0 ? nullptr : add_field(first_header, first);
  MIMEField *second_field = (flags & 128) != 0 ? nullptr : add_field(second_header, second);

  HttpCompat::do_vary_header_values_match(first_field, second_field);
  if (first_field && second_field) {
    HttpCompat::do_vary_header_values_match(first_field, first_field);
    add_field(first_header, second);
    add_field(second_header, first);
    HttpCompat::do_vary_header_values_match(first_field, second_field);
  }
}
} // namespace

extern "C" int
LLVMFuzzerInitialize(int *, char ***)
{
  cmd_disable_pfreelist = true;
  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));
  http_init();
  return 0;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size < 3 || size > MAX_INPUT_LENGTH) {
    return 0;
  }

  uint8_t     flags         = data[0];
  int         output_length = static_cast<int>(data[1]) + 1;
  size_t      split         = static_cast<size_t>(data[2]) * (size - 3) / 255;
  const char *payload       = reinterpret_cast<const char *>(data + 3);
  std::string first(payload, split);
  std::string second(payload + split, size - 3 - split);

  exercise_lists(first, second, flags, output_length);
  exercise_lists(second, first, flags, output_length);
  exercise_vary(first, second, flags);
  return 0;
}
