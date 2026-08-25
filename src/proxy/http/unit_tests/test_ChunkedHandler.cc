/** @file

  Catch-based unit tests for ChunkedHandler::read_size() and read_trailer().

  These tests drive the real chunk size line and trailer parsers through a small
  subclass fixture and a backing IOBuffer, covering hex size parsing, chunk
  extensions, quoted-string extension values per RFC 9110 Section 5.6.4,
  quoted-pair escapes, the trailer terminating line per RFC 9112 Section 7.1, and
  strict versus non-strict line termination.

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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>

#include "proxy/http/HttpTunnel.h"
#include "iocore/eventsystem/IOBuffer.h"

// Subclass fixture that drives the otherwise private read_size() parser and lets
// a test seed the parsing state. ChunkedHandler names this class as a friend, so
// it must live at global scope to match that declaration (not in an anonymous
// namespace).
class TestableChunkedHandler : public ChunkedHandler
{
public:
  // Drive the private size parser. Returns the number of input bytes consumed.
  int64_t
  parse()
  {
    return read_size();
  }

  // Drive the private trailer parser. Returns the number of input bytes consumed.
  int64_t
  parse_trailer()
  {
    return read_trailer();
  }

  void
  reset(IOBufferReader *reader, bool strict)
  {
    chunked_reader       = reader;
    state                = ChunkedState::READ_SIZE;
    strict_chunk_parsing = strict;
    running_sum          = 0;
    num_digits           = 0;
    num_cr               = 0;
    prev_is_cr           = false;
    in_quoted_string     = false;
    in_escape            = false;
  }

  // Seed the handler at the start of the trailer section, which is where
  // read_size() leaves it after the final zero-size chunk (state
  // READ_TRAILER_BLANK). drop_chunked_trailers stays false so read_trailer()
  // does not need a chunked_buffer.
  void
  reset_for_trailer(IOBufferReader *reader, bool strict)
  {
    chunked_reader        = reader;
    state                 = ChunkedState::READ_TRAILER_BLANK;
    strict_chunk_parsing  = strict;
    drop_chunked_trailers = false;
  }
};

namespace
{

using State = ChunkedHandler::ChunkedState;

// Result of parsing a chunk size line: the terminal state, the parsed size, the
// number of input bytes read_size() consumed, and how many bytes were left
// unconsumed in the reader. The consumed/remaining counts are what distinguish a
// parser that keeps a quoted-string value intact (consumes the whole line) from
// one that stops at the first embedded CRLF.
struct ParseResult {
  State   state;
  int64_t size;
  int64_t bytes_left; // cur_chunk_bytes_left: body bytes the dechunker will read for this chunk.
  int64_t consumed;
  int64_t remaining;
};

// Feed a complete chunk size line into read_size() in a single buffer.
ParseResult
parse_chunk_size_line(const char *input, bool strict = true)
{
  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = buffer->alloc_reader();
  buffer->write(input, std::strlen(input));

  TestableChunkedHandler handler;
  handler.reset(reader, strict);
  int64_t consumed = handler.parse();

  ParseResult result{handler.state, handler.cur_chunk_size, handler.cur_chunk_bytes_left, consumed, reader->read_avail()};
  free_MIOBuffer(buffer);
  return result;
}

// Result of parsing a trailer section: the terminal state, the number of input
// bytes read_trailer() consumed, and how many bytes were left unconsumed. A
// parser that ends the trailers at a bare LF consumes only up to that LF and
// leaves any following bytes (a smuggled request) behind; one that requires CRLF
// rejects (READ_ERROR) instead.
struct TrailerResult {
  State   state;
  int64_t consumed;
  int64_t remaining;
};

// Feed a trailer section into read_trailer() in a single buffer. The input begins
// where read_size() hands off after the final zero-size chunk, so it does not
// include the leading "0\r\n".
TrailerResult
parse_chunk_trailer(const char *input, bool strict = true)
{
  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = buffer->alloc_reader();
  buffer->write(input, std::strlen(input));

  TestableChunkedHandler handler;
  handler.reset_for_trailer(reader, strict);
  int64_t consumed = handler.parse_trailer();

  TrailerResult result{handler.state, consumed, reader->read_avail()};
  free_MIOBuffer(buffer);
  return result;
}

// A single row of the size-line matrix: a label, the raw size-line bytes, the
// parsing mode, the expected terminal state, and the expected chunk size
// (negative means do not check the size, used for the malformed rows).
struct SizeCase {
  const char *name;
  const char *input;
  bool        strict;
  State       expect_state;
  int64_t     expect_size;
};

// A single row of the trailer matrix: a label, the trailer-section bytes (which
// begin after read_size() has consumed the final "0\r\n"), the parsing mode, and
// the expected terminal state.
struct TrailerCase {
  const char *name;
  const char *input;
  bool        strict;
  State       expect_state;
};

} // namespace

TEST_CASE("ChunkedHandler parses chunk sizes", "[chunked]")
{
  SECTION("single hex digit")
  {
    auto r = parse_chunk_size_line("a\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 10);
  }

  SECTION("multiple hex digits")
  {
    auto r = parse_chunk_size_line("ff\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 255);
  }

  SECTION("final chunk of size zero")
  {
    auto r = parse_chunk_size_line("0\r\n");
    CHECK(r.state == State::READ_TRAILER_BLANK);
    CHECK(r.size == 0);
  }
}

TEST_CASE("ChunkedHandler parses chunk extensions", "[chunked]")
{
  SECTION("token extension value")
  {
    auto r = parse_chunk_size_line("a;ext=value\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 10);
  }

  SECTION("quoted-string extension value")
  {
    auto r = parse_chunk_size_line("a;ext=\"hello world\"\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 10);
  }

  SECTION("multiple extensions")
  {
    auto r = parse_chunk_size_line("a;ext1=val1;ext2=\"val2\"\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 10);
  }

  SECTION("whitespace before the semicolon (BWS) still parses the extension")
  {
    auto r = parse_chunk_size_line("a ;ext=value\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 10);
  }
}

// RFC 9110 Section 5.6.4: a quoted-string cannot contain a bare CR or LF (neither
// qdtext nor quoted-pair permits them). A chunk extension whose quoted value
// embeds CR/LF is therefore malformed, and the parser must reject it (READ_ERROR)
// rather than forward a request a downstream parser could frame differently.
TEST_CASE("ChunkedHandler rejects CR or LF inside a quoted extension value", "[chunked]")
{
  SECTION("a single embedded CRLF is rejected")
  {
    CHECK(parse_chunk_size_line("1;a=\"\r\nfoo\"\r\n").state == State::READ_ERROR);
  }

  SECTION("several embedded CRLFs are rejected")
  {
    CHECK(parse_chunk_size_line("1;ext=\"line1\r\nline2\r\nline3\"\r\n").state == State::READ_ERROR);
  }

  SECTION("a bare LF inside the quoted value is rejected in both modes")
  {
    CHECK(parse_chunk_size_line("1;a=\"x\ny\"\r\n", true).state == State::READ_ERROR);
    CHECK(parse_chunk_size_line("1;a=\"x\ny\"\r\n", false).state == State::READ_ERROR);
  }

  SECTION("an embedded CRLF is rejected even when whitespace (BWS) precedes the semicolon")
  {
    CHECK(parse_chunk_size_line("1 ;a=\"\r\nfoo\"\r\n").state == State::READ_ERROR);
  }

  SECTION("a quoted-pair cannot escape a CR")
  {
    CHECK(parse_chunk_size_line("1;a=\"\\\rx\"\r\n").state == State::READ_ERROR);
  }
}

// A quoted-string value with no CR/LF is well formed and parses to a normal chunk.
TEST_CASE("ChunkedHandler accepts a valid quoted extension value", "[chunked]")
{
  SECTION("an escaped DQUOTE does not close the quoted string")
  {
    auto r = parse_chunk_size_line("1;ext=\"value\\\"more\"\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 1);
    CHECK(r.bytes_left == 1); // the dechunker reads exactly the 1 declared body byte
    CHECK(r.remaining == 0);
  }

  SECTION("an escaped backslash is consumed as a quoted-pair")
  {
    auto r = parse_chunk_size_line("1;ext=\"path\\\\file\"\r\n");
    CHECK(r.state == State::READ_CHUNK);
    CHECK(r.size == 1);
    CHECK(r.remaining == 0);
  }
}

// A chunk extension can be split across socket reads. read_size() must suspend
// in READ_EXTENSION when the reader empties mid-extension and resume cleanly on
// the next call. (process_chunked_content() also routes READ_EXTENSION back to
// read_size(); without that, a split extension would crash the dispatcher.)
TEST_CASE("ChunkedHandler resumes a chunk extension split across reads", "[chunked]")
{
  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = buffer->alloc_reader();

  TestableChunkedHandler handler;
  handler.reset(reader, true);

  // First read ends in the middle of a quoted-string extension value.
  buffer->write("5;ext=\"ab", 9);
  handler.parse();
  CHECK(handler.state == State::READ_EXTENSION);
  CHECK(reader->read_avail() == 0);

  // The rest of the line arrives in a later read and completes the chunk.
  buffer->write("cd\"\r\nhello\r\n", 12);
  handler.parse();
  CHECK(handler.state == State::READ_CHUNK);
  CHECK(handler.cur_chunk_size == 5);

  free_MIOBuffer(buffer);
}

TEST_CASE("ChunkedHandler rejects malformed size lines under strict parsing", "[chunked]")
{
  SECTION("non hex, non delimiter character after the size")
  {
    CHECK(parse_chunk_size_line("ax\r\n", true).state == State::READ_ERROR);
  }

  SECTION("extension with no preceding size digits")
  {
    CHECK(parse_chunk_size_line(";\r\n", true).state == State::READ_ERROR);
  }

  SECTION("a second CR before the LF is a protocol error")
  {
    CHECK(parse_chunk_size_line("a\r\r\n", true).state == State::READ_ERROR);
  }

  SECTION("a second CR after an extension is a protocol error")
  {
    CHECK(parse_chunk_size_line("a;ext=value\r\r\n", true).state == State::READ_ERROR);
  }
}

TEST_CASE("ChunkedHandler honors strict versus non-strict line termination", "[chunked]")
{
  SECTION("a bare LF after an extension is rejected in strict mode")
  {
    // A chunk size line ending in a bare LF after an extension, with body bytes
    // following, must be rejected rather than accepted with the bytes as data.
    auto r = parse_chunk_size_line("7;x\nabcwxyz\r\n", true);
    CHECK(r.state == State::READ_ERROR);
  }

  SECTION("a bare LF after an extension is accepted in non-strict mode")
  {
    auto lenient = parse_chunk_size_line("a;ext=value\n", false);
    CHECK(lenient.state == State::READ_CHUNK);
    CHECK(lenient.size == 10);
  }

  SECTION("a proper CRLF terminator works in both modes")
  {
    CHECK(parse_chunk_size_line("a\r\n", true).state == State::READ_CHUNK);
    CHECK(parse_chunk_size_line("a\r\n", false).state == State::READ_CHUNK);
  }
}

// RFC 9112 Section 7.1: the trailer section ends with an empty line, "CRLF". A
// bare LF blank line is not a valid terminator. read_trailer() must enforce CRLF
// under strict parsing, the same as the chunk size line does: a bare LF blank
// line ends the trailers on a lenient parser but a strict peer keeps reading, so
// any bytes after the bare LF can be framed by the two parsers as different
// requests (a request boundary desync). Under strict parsing the bare LF must be
// a protocol error so the ambiguous bytes are never forwarded.
TEST_CASE("ChunkedHandler honors strict versus non-strict trailer termination", "[chunked]")
{
  SECTION("a bare LF blank line does not terminate the trailers in strict mode")
  {
    // Bytes after the bare LF look like a smuggled request. The strict parser
    // must reject rather than end the trailers here and forward the trailing GET.
    auto r = parse_chunk_trailer("\nGET /smuggled HTTP/1.1\r\n\r\n", true);
    CHECK(r.state == State::READ_ERROR);
  }

  SECTION("a bare LF blank line still terminates the trailers in non-strict mode")
  {
    auto r = parse_chunk_trailer("\n", false);
    CHECK(r.state == State::READ_DONE);
  }

  SECTION("a CRLF blank line terminates the trailers in both modes")
  {
    CHECK(parse_chunk_trailer("\r\n", true).state == State::READ_DONE);
    CHECK(parse_chunk_trailer("\r\n", false).state == State::READ_DONE);
  }

  SECTION("a bare LF after a trailer field does not terminate the trailers in strict mode")
  {
    // The trailer field line ends, returning to a blank line, and the following
    // bare LF must not be accepted as the terminator under strict parsing.
    auto r = parse_chunk_trailer("X-Trailer: v\r\n\nGET /smuggled HTTP/1.1\r\n\r\n", true);
    CHECK(r.state == State::READ_ERROR);
  }

  SECTION("a trailer field followed by a CRLF blank line terminates in strict mode")
  {
    auto r = parse_chunk_trailer("X-Trailer: v\r\n\r\n", true);
    CHECK(r.state == State::READ_DONE);
  }
}

// Broad regression matrix for the chunk size line parser. Each row is one input
// and the terminal state the parser must reach, exercised across strict and
// non-strict modes. New variations are added by appending a row, so the matrix
// grows without new boilerplate.
TEST_CASE("ChunkedHandler size-line parsing matrix", "[chunked]")
{
  auto c = GENERATE(values<SizeCase>({
    // label                              input                       strict expected state          size
    {"size zero hands off to trailers",           "0\r\n",                     true,  State::READ_TRAILER_BLANK, 0   },
    {"single hex digit",                          "1\r\n",                     true,  State::READ_CHUNK,         1   },
    {"lowercase hex",                             "a\r\n",                     true,  State::READ_CHUNK,         10  },
    {"uppercase hex",                             "A\r\n",                     true,  State::READ_CHUNK,         10  },
    {"multiple hex digits",                       "ff\r\n",                    true,  State::READ_CHUNK,         255 },
    {"four hex digits",                           "1000\r\n",                  true,  State::READ_CHUNK,         4096},
    {"leading zeros",                             "00a\r\n",                   true,  State::READ_CHUNK,         10  },
    {"token extension",                           "a;ext=value\r\n",           true,  State::READ_CHUNK,         10  },
    {"extension token, no value",                 "a;ext\r\n",                 true,  State::READ_CHUNK,         10  },
    {"quoted-string extension",                   "a;ext=\"hello world\"\r\n", true,  State::READ_CHUNK,         10  },
    {"multiple extensions",                       "a;e1=v1;e2=\"v2\"\r\n",     true,  State::READ_CHUNK,         10  },
    {"BWS before the semicolon",                  "a ;ext=value\r\n",          true,  State::READ_CHUNK,         10  },
    {"escaped DQUOTE in quoted value",            "1;e=\"a\\\"b\"\r\n",        true,  State::READ_CHUNK,         1   },
    {"escaped backslash in quoted value",         "1;e=\"a\\\\b\"\r\n",        true,  State::READ_CHUNK,         1   },
    // Malformed: an embedded CR/LF in a quoted-string extension value.
    {"embedded CRLF in quoted value, strict",     "1;a=\"\r\nfoo\"\r\n",       true,  State::READ_ERROR,         -1  },
    {"embedded CRLF in quoted value, lenient",    "1;a=\"\r\nfoo\"\r\n",       false, State::READ_ERROR,         -1  },
    {"embedded bare LF in quoted value, strict",  "1;a=\"x\ny\"\r\n",          true,  State::READ_ERROR,         -1  },
    {"embedded bare LF in quoted value, lenient", "1;a=\"x\ny\"\r\n",          false, State::READ_ERROR,         -1  },
    {"quoted-pair cannot escape a CR",            "1;a=\"\\\rx\"\r\n",         true,  State::READ_ERROR,         -1  },
    {"embedded CRLF in quoted value after BWS",   "1 ;a=\"\r\nx\"\r\n",        true,  State::READ_ERROR,         -1  },
    // Malformed: bad size syntax.
    {"non-hex character after the size",          "ax\r\n",                    true,  State::READ_ERROR,         -1  },
    {"non-hex first character",                   "g\r\n",                     true,  State::READ_ERROR,         -1  },
    {"extension with no size digits",             ";\r\n",                     true,  State::READ_ERROR,         -1  },
    {"a second CR before the LF",                 "a\r\r\n",                   true,  State::READ_ERROR,         -1  },
    {"a second CR after an extension",            "a;ext=value\r\r\n",         true,  State::READ_ERROR,         -1  },
    {"control character after the size",          "a\x01\r\n",                 true,  State::READ_ERROR,         -1  },
    // Line termination: bare LF gated on strict parsing.
    {"bare LF after a plain size, strict",        "a\nbody",                   true,  State::READ_ERROR,         -1  },
    {"bare LF after extension, strict",           "7;x\nabcwxyz\r\n",          true,  State::READ_ERROR,         -1  },
    {"bare LF after extension, lenient",          "a;ext=value\n",             false, State::READ_CHUNK,         10  },
    {"CRLF terminator, strict",                   "a\r\n",                     true,  State::READ_CHUNK,         10  },
    {"CRLF terminator, lenient",                  "a\r\n",                     false, State::READ_CHUNK,         10  },
  }));

  DYNAMIC_SECTION(c.name << (c.strict ? " [strict]" : " [lenient]"))
  {
    auto r = parse_chunk_size_line(c.input, c.strict);
    CHECK(r.state == c.expect_state);
    if (c.expect_size >= 0) {
      CHECK(r.size == c.expect_size);
    }
  }
}

// Broad regression matrix for the trailer parser. The input begins where
// read_size() hands off after the final zero-size chunk, so it does not include
// the leading "0\r\n". The terminating empty line must be a full CRLF under
// strict parsing; a bare LF blank line terminates only in non-strict mode.
TEST_CASE("ChunkedHandler trailer parsing matrix", "[chunked]")
{
  auto c = GENERATE(values<TrailerCase>({
    // label                                         input                                     strict expected state
    {"CRLF terminator, strict",                       "\r\n",                                true,  State::READ_DONE },
    {"CRLF terminator, lenient",                      "\r\n",                                false, State::READ_DONE },
    {"bare LF terminator, strict",                    "\n",                                  true,  State::READ_ERROR},
    {"bare LF terminator, lenient",                   "\n",                                  false, State::READ_DONE },
    {"bare LF then smuggled request, strict",         "\nGET /x HTTP/1.1\r\n\r\n",           true,  State::READ_ERROR},
    {"trailer field then CRLF terminator, strict",    "X-T: v\r\n\r\n",                      true,  State::READ_DONE },
    {"trailer field then CRLF terminator, lenient",   "X-T: v\r\n\r\n",                      false, State::READ_DONE },
    {"trailer field then bare LF terminator, strict", "X-T: v\r\n\nGET /x HTTP/1.1\r\n\r\n", true,  State::READ_ERROR},
    {"two trailer fields then CRLF, strict",          "X-T: v\r\nY-T: w\r\n\r\n",            true,  State::READ_DONE },
    {"two trailer fields then CRLF, lenient",         "X-T: v\r\nY-T: w\r\n\r\n",            false, State::READ_DONE },
    // Only the terminating empty line is gated. A bare LF ending a non-blank
    // trailer field line is still tolerated under strict parsing (it does not end
    // the message, so it creates no request boundary the peers can disagree on).
    {"bare LF ends a field line, tolerated, strict",  "X-T: v\nY-T: w\r\n\r\n",              true,  State::READ_DONE },
  }));

  DYNAMIC_SECTION(c.name << (c.strict ? " [strict]" : " [lenient]"))
  {
    auto r = parse_chunk_trailer(c.input, c.strict);
    CHECK(r.state == c.expect_state);
  }
}

// A trailer can be split across socket reads. read_trailer() must suspend when
// the reader empties mid-trailer and resume cleanly, still rejecting a bare-LF
// terminator that only arrives in a later read under strict parsing.
TEST_CASE("ChunkedHandler resumes a trailer split across reads", "[chunked]")
{
  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = buffer->alloc_reader();

  TestableChunkedHandler handler;
  handler.reset_for_trailer(reader, true);

  // First read ends after a complete trailer field line, parking at a blank line.
  buffer->write("X-Trailer: v\r\n", 14);
  handler.parse_trailer();
  CHECK(handler.state == State::READ_TRAILER_BLANK);
  CHECK(reader->read_avail() == 0);

  // The bare-LF terminator arrives in a later read and must still be rejected.
  buffer->write("\n", 1);
  handler.parse_trailer();
  CHECK(handler.state == State::READ_ERROR);

  free_MIOBuffer(buffer);
}
