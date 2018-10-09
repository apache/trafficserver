uWServer
========

uWServer is a mock HTTP server that takes predefined set of sessions for serving response to HTTP requests. Each session includes one or more transactions. A transaction is composed of an HTTP request and an HTTP response.
uWServer accepts session data in JSON fromat only.


Command:
----------------

`python3.5 uWServer.py  --data-dir <PATH_TO_SESSION_DIR>`

Options:
-----------

To see the options please run `python3.5 uWServer.py --help`

Session Definitions:
--------------------

Example session:

```
{
  "encoding": "url_encoded",
  "version": "0.2",
  "txns": [
    {
      "response": {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "body": "",
        "timestamp": "1469733493.993"
      },
      "request": {
        "headers": "GET / HTTP/1.1\r\nHost: www.example.test\r\n\r\n",
        "body": "",
        "timestamp": "1469733493.993"
      },
      "uuid": "",
      "timestamp": ""
    }
  ],
  "timestamp": "1234567890.098"
}
```

Each session should be in its own file, and any number of files may be created to define sessions.

The `response` map may include an `options` string, which is a comma-delimited list of options to be enabled. Currently the only option supported is `skipHooks`, which will ignore any hooks created for the matching request/response pair. See **Options**.
