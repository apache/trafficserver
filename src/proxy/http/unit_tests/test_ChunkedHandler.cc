/** @file

  Catch-based unit tests for ChunkedHandler::read_size().

  These tests drive the real chunk size line parser through a small subclass
  fixture and a backing IOBuffer, covering hex size parsing, chunk extensions,
  quoted-string extension values per RFC 9110 Section 5.6.4, quoted-pair
  escapes, and strict versus non-strict line termination.

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
