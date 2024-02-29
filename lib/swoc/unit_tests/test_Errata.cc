// SPDX-License-Identifier: Apache-2.0
/** @file

    Errata unit tests.
*/

#include <memory>
#include <errno.h>
#include "swoc/Errata.h"
#include "swoc/bwf_std.h"
#include "swoc/bwf_ex.h"
#include "swoc/swoc_file.h"
#include "swoc/Lexicon.h"
#include "catch.hpp"

using swoc::Errata;
using swoc::Rv;
using swoc::TextView;
using Severity = swoc::Errata::Severity;
using namespace std::literals;
using namespace swoc::literals;

static constexpr swoc::Errata::Severity ERRATA_DBG{0};
static constexpr swoc::Errata::Severity ERRATA_DIAG{1};
static constexpr swoc::Errata::Severity ERRATA_INFO{2};
static constexpr swoc::Errata::Severity ERRATA_WARN{3};
static constexpr swoc::Errata::Severity ERRATA_ERROR{4};

std::array<swoc::TextView, 5> Severity_Names{
  {"Debug", "Diag", "Info", "Warn", "Error"}
};

enum class ECode { ALPHA = 1, BRAVO, CHARLIE };

struct e_category : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

e_category e_cat;

const char *
e_category::name() const noexcept {
  return "libswoc";
}

std::string
e_category::message(int ev) const {
  static swoc::Lexicon<ECode> lexicon{
    {{ECode::ALPHA, "Alpha"}, {ECode::BRAVO, "Bravo"}, {ECode::CHARLIE, "Charlie"}},
    "Code out of range"
  };

  return std::string(lexicon[ECode(ev)]);
}

inline std::error_code
ecode(ECode c) {
  return {int(c), e_cat};
}

std::string ErrataSinkText;

// Call from unit test main before starting tests.
void
test_Errata_init() {
  swoc::Errata::DEFAULT_SEVERITY = ERRATA_ERROR;
  swoc::Errata::FAILURE_SEVERITY = ERRATA_WARN;
  swoc::Errata::SEVERITY_NAMES   = swoc::MemSpan<swoc::TextView const>(Severity_Names.data(), Severity_Names.size());

  swoc::Errata::register_sink([](swoc::Errata const &errata) -> void { bwprint(ErrataSinkText, "{}", errata); });
}

Errata
Noteworthy(std::string_view text) {
  return Errata{ERRATA_INFO, text};
}

Errata
cycle(Errata &erratum) {
  return std::move(erratum.note("Note well, young one!"));
}

TEST_CASE("Errata copy", "[libswoc][Errata]") {
  auto notes = Noteworthy("Evil Dave Rulz.");
  REQUIRE(notes.length() == 1);
  REQUIRE(notes.begin()->text() == "Evil Dave Rulz.");

  notes = cycle(notes);
  REQUIRE(notes.length() == 2);

  Errata erratum;
  REQUIRE(erratum.length() == 0);
  erratum.note("Diagnostics");
  REQUIRE(erratum.length() == 1);
  erratum.note("Information");
  REQUIRE(erratum.length() == 2);

  // Test internal allocation boundaries.
  notes.clear();
  std::string_view text{"0123456789012345678901234567890123456789"};
  for (int i = 0; i < 50; ++i) {
    notes.note(text);
  }
  REQUIRE(notes.length() == 50);
  REQUIRE(notes.begin()->text() == text);
  bool match_p = true;
  for (auto &&note : notes) {
    if (note.text() != text) {
      match_p = false;
      break;
    }
  }
  REQUIRE(match_p);
};

TEST_CASE("Rv", "[libswoc][Errata]") {
  Rv<int> zret;
  struct Thing {
    char const *s = "thing";
  };
  using ThingHandle = std::unique_ptr<Thing>;

  zret = 17;
  zret = Errata(std::error_code(EINVAL, std::generic_category()), ERRATA_ERROR, "This is an error");

  {
    auto &[result, erratum] = zret;

    REQUIRE(erratum.length() == 1);
    REQUIRE(erratum.severity() == ERRATA_ERROR);
    REQUIRE_FALSE(erratum.is_ok());

    REQUIRE(result == 17);
    zret = 38;
    REQUIRE(result == 38); // reference binding, update.
  }

  {
    auto &&[result, erratum] = zret;

    REQUIRE(erratum.length() == 1);
    REQUIRE(erratum.severity() == ERRATA_ERROR);

    REQUIRE(result == 38);
    zret = 56;
    REQUIRE(result == 56); // reference binding, update.
  }

  auto test = [](Severity expected_severity, Rv<int> const &rvc) {
    auto const &[cv_result, cv_erratum] = rvc;
    REQUIRE(cv_erratum.length() == 1);
    REQUIRE(cv_erratum.severity() == expected_severity);
    REQUIRE(cv_result == 56);
  };

  {
    auto const &[result, erratum] = zret;
    REQUIRE(result == 56);

    test(ERRATA_ERROR, zret); // invoke it.
  }

  zret.clear();
  REQUIRE(zret.result() == 56);

  {
    auto const &[result, erratum] = zret;
    REQUIRE(result == 56);
    REQUIRE(erratum.length() == 0);
  }

  zret.note("Diagnostics");
  REQUIRE(zret.errata().length() == 1);
  zret.note("Information");
  REQUIRE(zret.errata().length() == 2);
  zret.note("Warning");
  REQUIRE(zret.errata().length() == 3);
  zret.note("Error");
  REQUIRE(zret.errata().length() == 4);
  REQUIRE(zret.result() == 56);

  test(ERRATA_DIAG, Rv<int>{56, Errata(ERRATA_DIAG, "Test rvalue diag")});
  test(ERRATA_INFO, Rv<int>{56, Errata(ERRATA_INFO, "Test rvalue info")});
  test(ERRATA_WARN, Rv<int>{56, Errata(ERRATA_WARN, "Test rvalue warn")});
  test(ERRATA_ERROR, Rv<int>{56, Errata(ERRATA_ERROR, "Test rvalue error")});

  // Test the note overload that takes an Errata.
  zret.clear();
  REQUIRE(zret.result() == 56);
  REQUIRE(zret.errata().length() == 0);
  zret = Errata{ERRATA_INFO, "Information"};
  REQUIRE(ERRATA_INFO == zret.errata().severity());
  REQUIRE(zret.errata().length() == 1);

  Errata e1{ERRATA_DBG, "Debug"};
  zret.note(e1);
  REQUIRE(zret.errata().length() == 2);
  REQUIRE(ERRATA_INFO == zret.errata().severity());

  Errata e2{ERRATA_DBG, "Debug"};
  zret.note(std::move(e2));
  REQUIRE(zret.errata().length() == 3);
  REQUIRE(e2.length() == 0);

  // Now try it on a non-copyable object.
  ThingHandle handle{new Thing};
  Rv<ThingHandle> thing_rv;

  handle->s = "other"; // mark it.
  thing_rv  = std::move(handle);
  thing_rv  = Errata(ERRATA_WARN, "This is a warning");

  auto &&[tr1, te1]{thing_rv};
  REQUIRE(te1.length() == 1);
  REQUIRE(te1.severity() == ERRATA_WARN);
  REQUIRE_FALSE(te1.is_ok());

  ThingHandle other{std::move(tr1)};
  REQUIRE(tr1.get() == nullptr);
  REQUIRE(thing_rv.result().get() == nullptr);
  REQUIRE(other->s == "other"sv);

  auto maker = []() -> Rv<ThingHandle> {
    ThingHandle handle = std::make_unique<Thing>();
    handle->s          = "made";
    return {std::move(handle)};
  };

  auto &&[tr2, te2]{maker()};
  REQUIRE(tr2->s == "made"sv);
};

// DOC -> NoteInfo
template <typename... Args>
Errata &
NoteInfo(Errata &errata, std::string_view fmt, Args... args) {
  return errata.note_v(ERRATA_INFO, fmt, std::forward_as_tuple(args...));
}
// DOC -< NoteInfo

TEST_CASE("Errata example", "[libswoc][Errata]") {
  swoc::LocalBufferWriter<2048> w;
  std::error_code ec;
  swoc::file::path path("does-not-exist.txt");
  auto content = swoc::file::load(path, ec);
  REQUIRE(false == !ec); // it is expected the load will fail.
  Errata errata{ec, ERRATA_ERROR, R"(Failed to open file "{}")", path};
  w.print("{}", errata);
  REQUIRE(w.size() > 0);
  REQUIRE(w.view().starts_with("Error: [enoent") == true);
  REQUIRE(w.view().find("enoent") != swoc::TextView::npos);
}

TEST_CASE("Errata API", "[libswoc][Errata]") {
  // Check that if an int is expected from a function, it can be changed to
  // @c Rv<int> without change at the call site.
  int size = -7;
  auto f   = [&]() -> Rv<int> {
    if (size > 0)
      return size;
    return {-1, Errata(ERRATA_ERROR, "No size, doofus!")};
  };

  int r1 = f();
  REQUIRE(r1 == -1);
  size   = 10;
  int r2 = f();
  REQUIRE(r2 == 10);
}

TEST_CASE("Errata sink", "[libswoc][Errata]") {
  auto &s = ErrataSinkText;
  {
    Errata errata{ERRATA_ERROR, "Nominal failure"};
    NoteInfo(errata, "Some");
    errata.note(ERRATA_DIAG, "error code {}", std::error_code(EPERM, std::system_category()));
  }
  // Destruction should write this out to the string.
  REQUIRE(s.size() > 0);
  REQUIRE(std::string::npos != s.find("Error: Nominal"));
  REQUIRE(std::string::npos != s.find("Info: Some"));
  REQUIRE(std::string::npos != s.find("Diag: error"));

  {
    Errata errata{ERRATA_ERROR, "Nominal failure"};
    NoteInfo(errata, "Some");
    errata.note(ERRATA_DIAG, "error code {}", std::error_code(EPERM, std::system_category()));
    errata.sink();

    REQUIRE(s.size() > 0);
    REQUIRE(std::string::npos != s.find("Error: Nominal"));
    REQUIRE(std::string::npos != s.find("Info: Some"));
    REQUIRE(std::string::npos != s.find("Diag: error"));

    s.clear();
  }

  REQUIRE(s.empty() == true);
  {
    Errata errata{ERRATA_ERROR, "Nominal failure"};
    NoteInfo(errata, "Some");
    errata.note(ERRATA_DIAG, "error code {}", std::error_code(EPERM, std::system_category()));
    errata.clear(); // cleared - no logging
    REQUIRE(errata.is_ok() == true);
  }
  REQUIRE(s.empty() == true);
}

TEST_CASE("Errata local severity", "[libswoc][Errata]") {
  std::string s;
  {
    Errata errata{ERRATA_ERROR, "Nominal failure"};
    NoteInfo(errata, "Some");
    errata.note(ERRATA_DIAG, "error code {}", std::error_code(EPERM, std::system_category()));
    swoc::bwprint(s, "{}", errata);
    REQUIRE(s.size() > 0);
    REQUIRE(std::string::npos != s.find("Error: Nominal"));
    REQUIRE(std::string::npos != s.find("Info: Some"));
    REQUIRE(std::string::npos != s.find("Diag: error"));
  }
  Errata::FILTER_SEVERITY = ERRATA_INFO; // diag is lesser serverity, shouldn't show up.
  {
    Errata errata{ERRATA_ERROR, "Nominal failure"};
    NoteInfo(errata, "Some");
    errata.note(ERRATA_DIAG, "error code {}", std::error_code(EPERM, std::system_category()));
    swoc::bwprint(s, "{}", errata);
    REQUIRE(s.size() > 0);
    REQUIRE(std::string::npos != s.find("Error: Nominal"));
    REQUIRE(std::string::npos != s.find("Info: Some"));
    REQUIRE(std::string::npos == s.find("Diag: error"));
  }

  Errata base{ERRATA_INFO, "Something happened"};
  base.note(Errata{ERRATA_WARN}.note(ERRATA_INFO, "Thing one").note(ERRATA_INFO, "Thing Two"));
  REQUIRE(base.length() == 3);
  REQUIRE(base.severity() == ERRATA_WARN);
}

TEST_CASE("Errata glue", "[libswoc][Errata]") {
  std::string s;
  Errata errata;

  errata.note(ERRATA_ERROR, "First");
  errata.note(ERRATA_WARN, "Second");
  errata.note(ERRATA_INFO, "Third");
  errata.assign_severity_glue_text(":\n");
  bwprint(s, "{}", errata);
  REQUIRE("Error:\nError: First\nWarn: Second\nInfo: Third\n" == s);
  errata.assign_annotation_glue_text("\n"); // check for no trailing newline
  bwprint(s, "{}", errata);
  REQUIRE("Error:\nError: First\nWarn: Second\nInfo: Third" == s);
  errata.assign_annotation_glue_text("\n", true); // check for trailing newline
  bwprint(s, "{}", errata);
  REQUIRE("Error:\nError: First\nWarn: Second\nInfo: Third\n" == s);

  errata.assign_annotation_glue_text(", ");
  bwprint(s, "{}", errata);
  REQUIRE("Error:\nError: First, Warn: Second, Info: Third" == s);

  errata.clear();
  errata.note("First");
  errata.note("Second");
  errata.note("Third");
  errata.assign(ERRATA_ERROR);
  errata.assign_severity_glue_text(" -> ");
  errata.assign_annotation_glue_text(", ");
  bwprint(s, "{}", errata);
  REQUIRE("Error -> First, Second, Third" == s);
}

template <typename... Args>
Errata
errata_errno(int err, Errata::Severity s, swoc::TextView fmt, Args &&...args) {
  return Errata(std::error_code(err, std::system_category()), s, "{} - {}",
                swoc::bwf::SubText(fmt, std::forward_as_tuple<Args...>(args...)), swoc::bwf::Errno(err));
}

template <typename... Args>
Errata
errata_errno(Errata::Severity s, swoc::TextView fmt, Args &&...args) {
  return errata_errno(errno, s, fmt, std::forward<Args>(args)...);
}

TEST_CASE("Errata Wrapper", "[libswoc][errata]") {
  TextView tv1 = "itchi";
  TextView tv2 = "ni";

  SECTION("no args") {
    errno       = EPERM;
    auto errata = errata_errno(ERRATA_ERROR, "no args");
    REQUIRE(errata.front().text().starts_with("no args - EPERM"));
  }

  SECTION("one arg, explcit") {
    auto errata = errata_errno(EPERM, ERRATA_ERROR, "no args");
    REQUIRE(errata.front().text().starts_with("no args - EPERM"));
  }

  SECTION("args, explcit") {
    auto errata = errata_errno(EBADF, ERRATA_ERROR, "{} {}", tv1, tv2);
    REQUIRE(errata.front().text().starts_with("itchi ni - EBADF"));
  }

  SECTION("args") {
    errno       = EINVAL;
    auto errata = errata_errno(ERRATA_ERROR, "{} {}", tv2, tv1);
    REQUIRE(errata.front().text().starts_with("ni itchi - EINVAL"));
  }
}

TEST_CASE("Errata Autotext", "[libswoc][errata]") {
  Errata a{ERRATA_WARN, Errata::AUTO};
  REQUIRE(a.front().text() == "Warn");
  Errata b{ecode(ECode::BRAVO), Errata::AUTO};
  REQUIRE(b.front().text() == "Bravo [2]");
  Errata c{ecode(ECode::ALPHA), ERRATA_ERROR, Errata::AUTO};
  REQUIRE(c.front().text() == "Error: Alpha [1]");

  Errata d{ERRATA_ERROR};
  REQUIRE_FALSE(d.is_ok());
  Errata e{ERRATA_INFO};
  REQUIRE(e.is_ok());
  Errata f{ecode(ECode::BRAVO)};
  REQUIRE_FALSE(f.is_ok());
  // Change properties but need to restore them for other tests.
  swoc::meta::let g1(Errata::DEFAULT_SEVERITY, ERRATA_WARN);
  swoc::meta::let g2(Errata::FAILURE_SEVERITY, ERRATA_ERROR);
  REQUIRE(f.is_ok());
}
