#! /usr/bin/env node

/** @file

  Implements Simple HTTP test server for the stale_while_revalidate plugin

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

var http = require('http');
var url  = require('url');

http.createServer(function (request, response) {
  console.log(Date());
  setTimeout( function (req, res) {
    console.log(req.headers);
    console.log(url.parse(req.url));
    res.writeHead(200, {'Content-Type': 'text/plain', 'Cache-Control': 'max-age=5, stale-while-revalidate=55'});
    //res.writeHead(500, {'Content-Type': 'text/plain', 'Cache-Control': 'max-age=5, stale-if-error=555'});
    res.end(Date() + '\n');
    console.log(Date() + '\n');
  }, 50, request, response);
}).listen(8081, '127.0.0.1');

console.log('Proxy running at http://127.0.0.1:8080/');
console.log('Server running at http://127.0.0.1:8081/');
