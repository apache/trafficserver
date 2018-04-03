uWServer
========

uWServer is a mock HTTP server that takes predefined set of sessions for serving response to HTTP requests. Each session includes one or more transactions. A transaction is composed of an HTTP request and an HTTP response. 
Session format is in JSON.

Example session

 {"version": "0.1", 
  "txns": [
        {"request": {"headers": "POST ……\r\n\r\n", "timestamp": "..", "body": ".."}, 
        "response": {"headers": "HTTP/1.1..\r\n\r\n", "timestamp": "..", "body": ".."},
         "uuid": "1"}, 
        {"request": {"headers": "POST ..….\r\n\r\n", "timestamp": "..", "body": ".."}, 
        "response": {"headers": "HTTP/1.1..\r\nr\n", "timestamp": "..", "body": ".."}, 
        "uuid": "2"}
  ], 
  "timestamp": "....", 
  "encoding": "...."}

Command:
----------------

python3.5 uWServer.py  --data-dir <PATH_TO_SESSION_DIR>
Options:
---------
 [-h] : Help message
 [--data-dir DATA_DIR] : directory containing the pre defined sessions. This is a required option
 [--ip_address IP_ADDRESS] : Interface on which to run uWServer. The default is 127.0.0.1
 [--port PORT] : port. The default is 5005
 [--delay DELAY] : Delay to be added before every response. Default is 0
 [-V] : version
 [--mode MODE] : uWServer operates in either of the two modes: 1) Replay and 2) Test. uWServer uses the uniques identifier passed as 'CONTENT-MD5' header field to locate a response in replay more. 
                  On the other hand, uWServer uses the url path for lookup in test mode. 
 [--ssl SSL] : Configure port as a secure socket layer port
 [--key KEY] : key to be used for ssl port. A key is available in the ssl directory.
 [--cert CERT]: cert to be used for ssl port. A self signed certificate is available in the ssl directory.
 [--clientverify CLIENTVERIFY] : Configure uWServer to ask for certificate from the client.
 [--load LOAD]: load the observer script

