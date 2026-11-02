# Cripts unit tests

`test_cripts` runs the cripts objects against the real plugin API and real header heaps, without starting a server.
Nothing is stubbed: every TS API call a test makes goes to the same implementation `traffic_server` uses.

## Layout

The two targets play the parts of `traffic_server` and a cript:

|               | Process          | Loads with `dlopen()`  | Which links |
|---------------|------------------|------------------------|-------------|
| In production | `traffic_server` | a compiled cript       | libcripts   |
| In the tests  | `test_cripts`    | `test_cripts_cases.so` | libcripts   |

Inside the `test_cripts` process, in order:

```
test_cripts                 executable: main.cc, plus the core and tsapi_objects (the real TS API), symbols exported
  main()
    1. set up the core: pfreelist off, Diags, http_init(), api_init()
    2. dlopen("test_cripts_cases.so") ------------------+
    3. Catch::Session().run() runs the cases            |
                    ^                                   v
                    |               test_cripts_cases.so    module: the test_*.cc files
                    |                 - links libcripts; its static initializers run now, after step 1
                    +---------------- - each TEST_CASE registers with libCatch2 as the module loads
                                      - its TS API calls, and libcripts', bind to symbols in test_cripts
```

Both targets link the same shared libCatch2, so the cases the module registers in step 2 are the ones step 3 runs.

### Why the cases are a dlopen()ed module

libcripts' static initializers read state that the core sets up before it loads any plugin:
- The global `proxy` object looks up the overridable configurations with `TSHttpTxnConfigFind()`. That reads a table
  the core builds in its own static initializers. A shared library's initializers run before the executable's, so
  linking libcripts into the test executable would run the lookup against an empty table and `TSFatal` before
  `main()`.
- The `cripts::Method` constants copy the API's well-known method strings, which `api_init()` sets. Loaded any
  earlier, every constant would be empty.

A real cript is `dlopen()`ed after the core is up, and the tests do the same.

### Why the module takes only the core's headers

The module must not link the core libraries. If it did, it would get private copies of the core's globals, for
example the header token tables that `http_init()` sets up in the executable. It would then run against copies that
were never initialized.

### Why libCatch2 must be shared

The cases register in the module, and `main()` runs them. That only works with one Catch2 registry for both, so
libCatch2 has to be a shared library. `lib/CMakeLists.txt` builds it that way, and `src/cripts/CMakeLists.txt` stops
the configure if that ever changes, because a static Catch2 would silently run no tests.

### Setup in `main()`

`main()` sets up the core, as `traffic_server` does before it loads plugins, and only then loads the cases:
- There are no event threads, so `cmd_disable_pfreelist` keeps the allocators off the thread local free lists.
- The main thread is registered as an `EThread`, so `this_thread()` gives the cripts Context and continuation
  allocations a thread, as on a net thread.
- `Diags` writes to stderr, so `TSWarning()`, `TSError()` and `TSFatal()` from the code under test print their
  messages instead of crashing.
- `http_init()` sets up the well-known header strings and the parser tables.
- `api_init()` sets the API's well-known strings, such as `TS_HTTP_METHOD_GET`.

## The test transaction (`CriptsTestTxn.h`)

The cripts header and URL objects reach the request through calls such as `TSHttpTxnClientReqGet(txnp, ...)`. These
only check that `txnp` is a live `HttpSM` and return one of its `t_state.hdr_info` headers. `cripts_test::Txn`
provides exactly that, without running the state machine:
- an `HttpSM` marked alive, with the HTTP configuration attached;
- a client request parsed by the real parser, and optionally a server response (`set_server_response()`);
- a `cripts::Context` for that transaction.

A test case declares `context` the same way a cript has it, so the cripts macros work unchanged:

```cpp
TEST_CASE("Header: add a field", "[cripts][Headers]")
{
  cripts_test::Txn txn{"GET /a HTTP/1.1\r\nHost: example.com\r\n\r\n"};
  auto             context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-New"] = "value";
  CHECK(txn.client_request_field("X-New") == "value");
}
```

When checking writes, check what the core holds rather than what cripts reads back:
- `client_request_field()`, `client_request_field_count()` and `client_url()` read the core's `HTTPHdr` and `URL`
  directly, so a change that only lives in a cripts cache still fails the test.
- `sm()` gives access to the rest of the transaction state.
- `cripts_test::str()` turns a cripts component into a `std::string`. Catch2 needs this to print values, because the
  components only convert through a non-const `GetSV()`.

To test what happens across hooks, call `context->reset()`. It is what the Epilogue does on entering each hook. Code
that depends on the current hook needs `context->state.hook` set first. For example, response status writes go to
the header only in the response hooks.

## Adding tests

- Put new cases in a `test_<Area>.cc` file. Add the file to the `test_cripts_cases` sources in
  `src/cripts/CMakeLists.txt`.
- Each Catch2 case is registered as its own ctest (`add_catch2_test`) and runs in its own process, so a case that
  aborts does not take the others with it. Cases must not depend on each other.
- New cases only change the module. The executable is relinked when the module changes (`LINK_DEPENDS`), which is what
  makes ctest discover them.

Run the tests with:

```
ctest --test-dir <build dir> -R test_cripts
```

To run a single case:

```
<build dir>/src/cripts/test_cripts "Header: add a field"
```

## Limits

There is no event loop, no network connection and no session: `ssnp` is null, and there is no server VC. Hooks never
fire, so the Epilogue's hook dispatch is not exercised. Behaviour that needs any of these belongs in an AuTest under
`tests/gold_tests/cripts/` instead.
