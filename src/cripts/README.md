# 1. Cripts

Cripts, C-Scripts, is a set of wrappers and include files, for making simple ATS
plugins easy to generate, modify or create from scratch. A key design here is that
the Code is the Configuration, i.e. the intent really is to have a custom Cript file
for every remap rule in the running system.

ToDo: This document is not 100% updated with all features, but is at least a starting point.

- [1. Cripts](#1-cripts)
- [2. Building Cripts](#2-building-cripts)
  - [2.1. Building a Cript](#21-building-a-cript)
  - [2.2. Lint: Validating a Cript](#22-lint-validating-a-cript)
  - [2.3. Clang-tidy](#23-clang-tidy)
- [3. Writing Cripts](#3-writing-cripts)
  - [3.1. Data types](#31-data-types)
  - [3.2. Hooks](#32-hooks)
  - [3.3. Processing and modifying headers](#33-processing-and-modifying-headers)
  - [3.4. Processing and modifying URLs](#34-processing-and-modifying-urls)
    - [3.4.1. Special case: Cache Key URL](#341-special-case-cache-key-url)
  - [3.5. Accessing and modifying connections](#35-accessing-and-modifying-connections)
  - [3.6. Overridable configurations](#36-overridable-configurations)
  - [3.7. Pattern and identity matching](#37-pattern-and-identity-matching)
  - [3.8. Cryptography functions](#38-cryptography-functions)
  - [3.9. Various other utilities](#39-various-other-utilities)
  - [3.10. Transaction contexts](#310-transaction-contexts)
    - [3.10.1. Instance data (parameters)](#3101-instance-data-parameters)
    - [3.10.2. Transaction data](#3102-transaction-data)
- [4. Plugins Cript can mimic or replace](#4-plugins-cript-can-mimic-or-replace)
  - [4.1. conf\_remap](#41-conf_remap)
  - [4.2. cachekey](#42-cachekey)
  - [4.3. header\_rewrite](#43-header_rewrite)
  - [4.4. regex\_remap](#44-regex_remap)
  - [4.5. geoip\_acl and maxmind\_acl](#45-geoip_acl-and-maxmind_acl)
  - [4.6. tcpinfo](#46-tcpinfo)

# 2. Building Cripts

Cripts needs the `{fmt}` and `PCRE2` libraries and include files. We currently only
build cripts when explicitly enabled, and only using `cmake``.

## 2.1. Building a Cript

At the moment, building Cripts needs to be done manually, using cmake and package
config tools. Once built, it can be loaded as any regular remap plugin:

```
map https://example.com https://origin.example.com @plugin=cript_test.so
```

## 2.2. Lint: Validating a Cript

TBD: I have the beginning of this tool, will land it later.

## 2.3. Clang-tidy

A custom clang-tidy configuration is provided with this patch, and I've run all
code through clang-tidy with this configuration.

# 3. Writing Cripts

Cripts follow the same basic model of how ATS splits up transaction processing
into what we call "hooks". A hook is essentially a callback mechanism, where
custom code can be injected into the ATS core via plugins.

The Cript language itself is essentially C++17, except it imposes some serious,
but important, limitations on what can and can not be used. Albeit we call this
a scripting language, it's truly compiled into regular, reloadable ATS plugins.

To start off with, we'll show a very basic Cript, to get an idea of what to
expect:

```
// The primary include file, this has to always be included
#include <Cript/Preamble.hpp>

do_send_response()
{
  borrow req  = Client::Request::get();
  borrow resp = Client::Response::get();

  if (req["X-Miles"] != "") {
      resp["X-Miles"] = req["X-Miles"]; // Echo back the request header
  }
}

do_remap()
{
  borrow req = Client::Request::get();

  req["@receipt"] = "PropertyX";
}

#include <Cript/Epilogue.hpp>
```

Don't worry about the exact details here, we will discuss them all further
down this documentation. There are however two critical pieces here to remember:

* All Cript's must have the Preamble and Epilogue include's as above
* You can have your own C/C++ function definitions if you like, but the predefined
  callbacks (such as `do_remap()`) have fixed names

## 3.1. Data types

Cript will make a best-effort to hide data types from the users as much as possible.
As such, it's highly recommended to use the "auto" style when declaring variables.
Cript being C/C++, it's impossible to hide everything, and to optimize integration
with ATS, we have a few quirks.

For example, there are two types of strings which can be returned, regular `strings`
and something called `string_view`. The latter are immutable representations of
strings that are owned and managed by ATS, and you can and should only use these
when reading values out of say a request or request header.

These are the typical types that are used by Cripts:

| Type        | Description                                   |
| ----------- | --------------------------------------------- |
| string      | This is the common std::string type from C++  |
| string_view | An immutable, ATS owned string representation |
| integer     | A signed integer value, 64-bit long           |
| float       | A signed, floating point value                |
| boolean     | A `true` or `false` flag                      |

## 3.2. Hooks

To simplify usage, Cript predefines a set of function names, each corresponding
to an ATS hook. These names are set in stone, and will be automatically added
to the ATS core if provided in the Cript. Those hooks not used will not be
called, obviously.

The current version of Cript supports the following callbacks / hooks, all of which are optional:

| Callback name      | Hook equivalent                | Description                                        |
| ------------------ | ------------------------------ | -------------------------------------------------- |
| do_remap()         | none                           | This is the main entry point for all remap plugins |
| do_post_remap()    | TS_HTTP_POST_REMAP_HOOK        | Called right after the remap rule is done          |
| do_send_request()  | TS_HTTP_SEND_REQUEST_HDR_HOOK  | Called before making an origin request             |
| do_read_response() | TS_HTTP_READ_RESPONSE_HDR_HOOK | Called when the origin has a response              |
| do_send_response() | TS_HTTP_SEND_RESPONSE_HDR_HOOK | Called before sending a response to client         |
| do_txn_close()     | TS_HTTP_TXN_CLOSE_HOOK         | Called just before the transaction is done         |

Note that not all of these callbacks are triggered for all requests. For example,
upon a cache hit in ATS, `do_send_request()` and `do_read_response()` are not called.
In addition, there are two plugin specific callbacks, which are used when loading
the plugins and instantiating remap rules:

| Callback name      | API equivalent        | Description                                    |
| ------------------ | --------------------- | ---------------------------------------------- |
| do_init            | TSRemapInit()         | Called once when the Cript is loaded           |
| do_create_instance | TSRemapNewInstance    | Called for every remap rule using the Cript    |
| do_delete_instance | TSRemapDeleteInstance | Called to cleanup any instance data from above |

## 3.3. Processing and modifying headers

A big part of all Cripts is to read and modify various headers. ATS as it works,
has four distinct headers, which translates into the following four environments:

| Header name      | Description                                        |
| ---------------- | -------------------------------------------------- |
| Client::Request  | The clients request header                         |
| Client::Response | The response header that is sent to the client     |
| Server::Request  | The request header being sent to the origin server |
| Server::Response | The response header being received from origin     |

Accessing these headers is easy:
```
    borrow client_req  = Client::Request::get();
    borrow client_resp = Client::Response::get();
    borrow server_req  = Server::Request::get();
    borrow server_resp = Server::Response::get();
```

Note that not all of these are available in every callback; For example, the client
response header is not available to read or write until we are in the hook for
`do_send_response()`.

The response headers has a couple of additional features specifically for
responses:
```
borrow client_resp = Client::Response::get();

if (resp.status = 220) {
    resp.status = 200;
}

// TBD more stuff here ?
```

Similarly, the request headers has a few unique traits as well:

```
borrow client_req = Client::Request::get();

if (client_req.method == "GET") {
    ...
}

// TBD more stuff here ?
```

## 3.4. Processing and modifying URLs

Similarly to headers, URLs are important in Cripts, in all its various forms.
Currently, Cript supports the following URLs:

| URL name         | Description                                                    |
| ---------------- | -------------------------------------------------------------- |
| Client::Pristine | The pristine client request URL, which is immutable            |
| Client::URL      | The clients request URL, which can be modified                 |
| Cache::URL       | This is a special URL, used internally of ATS as the cache key |

Getting these URLs follows a similar getter pattern to the headers:
```
borrow pristine = Pristine::URL::get();
borrow client   = Client::URL::get();
borrow cache    = Cache::URL::get();
```

Within a URL object, you can access all its various components via names following
standard URL naming.

| Identifier | Description                             |
| ---------- | --------------------------------------- |
| host       | The URL host                            |
| port       | The URL port number                     |
| path       | The URL path                            |
| query      | The URL query parameters (all of them!) |

Using this is easy (of course):
```
auto url = Client::URL;

if (url.host == "www.example.com") {
    ...
}
```

The `path` components does support indexes, e.g. the first URL path
component ("directory") can be read or written with `path[0]`.

The query parameter has a set of additional features, which are particularly
useful for modifying the cache key URLs. This includes sorting the list of
query parameters, or adding and removing a query parameter by name. This is
best explained with a real example:

```
borrow client_req = Client::Request::get();
borrow c_url      = Cache::URL::get();

c_url.query.sort(); // Sorts all query parameter by name first
c_url.query["foo"].erase(); // Removes the foo query parameter, if it exists
c_url.query["bar"] = "fie"; // Adds, or modifies, the bar query param to be "fie"

if (client_req["X-Miles"]) {
    c_url.path += "key_add=";
    c_url.path += client_req["X-Miles"]; // Adds this header to the cache key
}
```

With this in mind, a URL query path of

```
?foo=fum&z=xxx&d=yyy
```

gets normalized and modified to

```
?bar=fie&d=yyy&z=xxx
```

In addition, the cache key URL path is appended with an additional string
extracted from the request headers.

### 3.4.1. Special case: Cache Key URL

## 3.5. Accessing and modifying connections

There are essentially two possible connections being involved with a
transaction: The client connections, and the origin server connection. The
latter will not exist on a cache miss, and should therefore only be used
in callbacks involving origin transactions (*cache misses*).

Accessing the client connections is easily done with

```
borrow conn  = Client::Connection::get();
```

TODO: More stuff here, explaining the details

## 3.6. Overridable configurations
In ATS, some (not all) configurations can be overriden per transaction, or
remap rule. Cripts supports all such configurations, in a way that retains
the naming from `records.config`! Examples:

```
proxy.config.http.cache.http.set(0);

if (proxy.config.http.cache.generation.get() > 0) {
    // Do something
}
```

All overridable configurations are documented in the [records.config](https://docs.trafficserver.apache.org/en/latest/admin-guide/files/records.config.en.html)
documentation. Look for a tag `Overridable` for each setting.

## 3.7. Pattern and identity matching

Several pattern matching features exists in Cripts today:

| Name               | Description                                                                     |
| ------------------ | ------------------------------------------------------------------------------- |
| Matcher::Range::IP | Match an IP (from e.g. a `Client::Connection`) against a range of IPs           |
| Matcher::PCRE      | Create a Perl compatible regular expression, to match arbitrary strings against |

They all follow the same basic concept:

1. Setup a matcher (ideally `static` for performance)
2. Get the string or identity to match from the transaction
3. Call the `contains()` (or `match()`, they are synonyms) function

Examples:

```
static Matcher::Range::IP allow({"192.168.201.0/24", "17.0.0.0/8"});
borrow conn = Client::Connection::get();

if (allow.contains(conn.ip())) {
  CDebug("Client IP allowed: {}", conn.ip().string(24, 64));
  ...
}
```

Again, it's important to use the `static` keyword here, which helps the Cripts compiler
to optimize the creation of the Matcher.

```
static Matcher::PCRE pcre("^/([^/]+)/(.*)$");
auto url = Client::URL::get();
auto res = pcre.match(url.path);

if (res) {
  borrow resp = Client::Response::get()

  resp["X-PCRE-Test"] = format("{} and {}", res[1], res[2]);
}
```

## 3.8. Cryptography functions

## 3.9. Various other utilities

## 3.10. Transaction contexts
### 3.10.1. Instance data (parameters)

### 3.10.2. Transaction data

# 4. Plugins Cript can mimic or replace

This is a list of existing ATS plugins that properly written and configure Cript
scripts could replace. This will repeat some of the sections
and mentions above, but helps identifying what can be used and when.

## 4.1. conf_remap

This plugin can set overridable configuration per remap. Cripts supports that via
the global `proxy.` object. E.g.

```
proxy.config.http.cache.http.set(1);

if (proxy.config.http.cache.http.get() {
  //...
}
```

## 4.2. cachekey

A Cript is free to modify the `Cache::URL`` as needed, in either `do_remap()` or
`do_post_remap()`. We recommend the latter to be used for cache-key manipulation.

```
do_post_remap()
{
  borrow ckey = Cache::URL::get();

  ckey.query.sort();
  ckey.path += "entropy";
}
```

## 4.3. header_rewrite

Almost all features from `header_rewrite` are available, and more flexible, in Cript.
If there's anything missing, please let us know.

## 4.4. regex_remap

Cript supports PCRE2, as well as modifying the request URI as you like. For example:

```
do_post_remap()
{
  static Matcher::PCRE hosts({"(.*(\\.ogre.\\.com"});
  borrow url = Client::URL::get();
  auto res  = hosts.match(url.host);

  if (res) {
    url.path.insert(0, res[1]); // Prepend the path with the hostname
  }
}
```

## 4.5. geoip_acl and maxmind_acl

Lookups into the geo-location database is done via the connection object

```
do_remap()
{
  borrow conn = Client::Connection::get();

  if (conn.geo.ASN() == 123) {
    Error::Status::set(403);
  }
}
```

## 4.6. tcpinfo

The existing connection TcpInfo features can do everything this plugin can do,
and more. Feature parity is done with

```
do_send_response()
{
    borrow resp = Client::Response::get();
    borrow conn = Client::Connection::get();

    resp["@TCPInfo"] = conn.tcpinfo.log(); // Can also use format() for more flexibility
  }
}
```
