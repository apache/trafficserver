#! /usr/bin/env node

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
