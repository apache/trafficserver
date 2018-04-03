uWServer
========

uWServer is a mock HTTP server that takes predefined set of sessions for serving response to HTTP requests. Each session includes one or more transactions. A transaction is composed of an HTTP request and an HTTP response. 
uWServer accepts session data in JSON fromat only.

Example session :
```
 {"version": "0.1",  
  "txns": [  
        {"request": {"headers": "GET /path1\r\n Host: example.com \r\n\r\n", "timestamp": "1522783378", "body": "Apache Traffic Server"},     
        "response": {"headers": "HTTP/1.1\r\n Server: microserver\r\nContent-Length:100 \r\n\r\n", "timestamp": "1522783378", "body": ""},  
         "uuid": "1"},   
        {"request": {"headers": "GET /path2\r\n\r\n", "timestamp": "1522783378", "body": "Apache Traffic Server"},   
        "response": {"headers": "HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", "timestamp": "1522783378", "body": "Apache Traffic Server"},   
        "uuid": "2"}  
  ],   
  "timestamp": "1522783378",   
  "encoding": ""}  
```
Command:
----------------

`python3.5 uWServer.py  --data-dir <PATH_TO_SESSION_DIR>`

Options:
-----------

To see the options please run `python3.5 uWServer.py -h`


