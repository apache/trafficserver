#!/bin/env python3
'''
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.


import string
import http.client
import cgi
import time
import sys
import json
import os
import threading
from ipaddress import ip_address
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn, ForkingMixIn, BaseServer
from http import HTTPStatus
import argparse
import ssl
import socket

test_mode_enabled = True
__version__="1.0.Beta"

# hack to deal with sessionvalidation until we fix up the 
# packaing logic
sys.path.append(
    os.path.normpath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            '..'
            )
        )
    )

import sessionvalidation.sessionvalidation as sv


SERVER_PORT = 5005 # default port
HTTP_VERSION = 'HTTP/1.1'
G_replay_dict = {}

count = 0
class ThreadingServer(ThreadingMixIn, HTTPServer):
    '''This class forces the creation of a new thread on each connection'''
    pass

class ForkingServer(ForkingMixIn, HTTPServer):
    '''This class forces the creation of a new process on each connection'''
    pass

class SSLServer(ThreadingMixIn, HTTPServer):
    	def __init__(self, server_address, HandlerClass, options):
            BaseServer.__init__(self, server_address, HandlerClass)
            pwd = os.path.dirname(os.path.realpath(__file__))
            keys = os.path.join(pwd,options.key)
            certs = os.path.join(pwd,options.cert)
            self.options = options

            self.daemon_threads = True
            self.protocol_version = 'HTTP/1.1'
            if options.clientverify:
            	self.socket = ssl.wrap_socket(socket.socket(self.address_family, self.socket_type),
                    keyfile=keys, certfile=certs, server_side=True, cert_reqs=ssl.CERT_REQUIRED, ca_certs='/etc/ssl/certs/ca-certificates.crt')
            else:
                self.socket = ssl.wrap_socket(socket.socket(self.address_family, self.socket_type),
                    keyfile=keys, certfile=certs, server_side=True)

            self.server_bind()
            self.server_activate()

# Warning: if you can't tell already, it's pretty hacky
#
# The standard library HTTP server doesn't exactly provide all the functionality we need from the API it exposes,
# so we have to go in and override various methods that probably weren't intended to be overridden
#
# See the source code (https://hg.python.org/cpython/file/3.5/Lib/http/server.py) if you want to see where all these
# variables are coming from
class MyHandler(BaseHTTPRequestHandler):
    def getTestName(self,requestline):
        key=None
        keys=requestline.split(" ")
        #print(keys)
        if keys:
            rkey=keys[1]
        key=rkey.split("/",1)[1]
        if key+"/" in G_replay_dict:
            key = key+"/"
        elif len(key) > 1 and key[:-1] in G_replay_dict:
            key = key[:-1]
        return key

    def parseRequestline(self,requestline):
        testName=None        
        return testName

    def testMode(self,requestline):
        print(requestline)
        key=self.parseRequestline(requestline)
        
        self.send_response(200)
        self.send_header('Connection', 'close')
        self.end_headers()

        
    def get_response_code(self, header):
        # this could totally go wrong
        return int(header.split(' ')[1])

    def generator(self):
        yield 'microserver'
        yield 'yahoo'
    def send_response(self, code, message=None):
        ''' Override `send_response()`'s tacking on of server and date header lines. '''
        #self.log_request(code)
        self.send_response_only(code, message)

    def createDummyBodywithLength(self,numberOfbytes):
        if numberOfbytes==0:
            return None
        body= 'a'
        while numberOfbytes!=1:
            body += 'b'
            numberOfbytes -= 1
        return body

    def writeChunkedData(self):
        for chunk in self.generator():
            response_string=bytes('%X\r\n%s\r\n'%(len(chunk),chunk),'UTF-8')
            self.wfile.write(response_string)
        response_string=bytes('0\r\n\r\n','UTF-8')
        self.wfile.write(response_string)

    def readChunks(self):
        raw_data=b''
        raw_size = self.rfile.readline(65537)        
        size = str(raw_size, 'UTF-8').rstrip('\r\n')
        #print("==========================================>",size)
        size = int(size,16)
        while size>0:
            #print("reading bytes",raw_size)
            chunk = self.rfile.read(size+2) # 2 for reading /r/n
            #print("cuhnk: ",chunk)
            raw_data += chunk
            raw_size = self.rfile.readline(65537)            
            size = str(raw_size, 'UTF-8').rstrip('\r\n')
            size = int(size,16)
        #print("full chunk",raw_data)
        chunk = self.rfile.readline(65537) # read the extra blank newline \r\n after the last chunk

    def send_header(self, keyword, value):
        """Send a MIME header to the headers buffer."""
        if self.request_version != 'HTTP/0.9':
            if not hasattr(self, '_headers_buffer'):
                self._headers_buffer = []
            self._headers_buffer.append(
                ("%s: %s\r\n" % (keyword, value)).encode('UTF-8', 'strict')) #original code used latin-1.. seriously?

        if keyword.lower() == 'connection':
            if value.lower() == 'close':
                self.close_connection = True
            elif value.lower() == 'keep-alive':
                self.close_connection = False
    def parse_request(self):
        """Parse a request (internal).

        The request should be stored in self.raw_requestline; the results
        are in self.command, self.path, self.request_version and
        self.headers.

        Return True for success, False for failure; on failure, an
        error is sent back.

        """
        
        global count, test_mode_enabled
        
        self.command = None  # set in case of error on the first line
        self.request_version = version = self.default_request_version
        self.close_connection = True
        requestline = str(self.raw_requestline, 'UTF-8')
        #print("request",requestline)
        requestline = requestline.rstrip('\r\n')
        self.requestline = requestline        
        
        # Examine the headers and look for a Connection directive.        
        try:
            self.headers = http.client.parse_headers(self.rfile,
                                                     _class=self.MessageClass)
            # read message body
            if self.headers.get('Content-Length') != None:
                bodysize = int(self.headers.get('Content-Length'))
                #print("length of the body is",bodysize)
                message = self.rfile.read(bodysize)
                #print("message body",message)
            if self.headers.get('Transfer-Encoding',"") == 'chunked':
                #print(self.headers)
                self.readChunks()
        except http.client.LineTooLong:
            self.send_error(
                HTTPStatus.BAD_REQUEST,
                "Line too long")
            return False
        except http.client.HTTPException as err:
            self.send_error(
                HTTPStatus.REQUEST_HEADER_FIELDS_TOO_LARGE,
                "Too many headers",
                str(err)
            )
            return False
        
        
        words = requestline.split()
        if len(words) == 3:
            command, path, version = words
            if version[:5] != 'HTTP/':
                self.send_error(
                    HTTPStatus.BAD_REQUEST,
                    "Bad request version (%r)" % version)
                return False
            try:
                base_version_number = version.split('/', 1)[1]
                version_number = base_version_number.split(".")
                # RFC 2145 section 3.1 says there can be only one "." and
                #   - major and minor numbers MUST be treated as
                #      separate integers;
                #   - HTTP/2.4 is a lower version than HTTP/2.13, which in
                #      turn is lower than HTTP/12.3;
                #   - Leading zeros MUST be ignored by recipients.
                if len(version_number) != 2:
                    raise ValueError
                version_number = int(version_number[0]), int(version_number[1])
            except (ValueError, IndexError):
                self.send_error(
                    HTTPStatus.BAD_REQUEST,
                    "Bad request version (%r)" % version)
                return False
            if version_number >= (1, 1) and self.protocol_version >= "HTTP/1.1":
                self.close_connection = False
            if version_number >= (2, 0):
                self.send_error(
                    HTTPStatus.HTTP_VERSION_NOT_SUPPORTED,
                    "Invalid HTTP Version (%s)" % base_version_number)
                return False
        elif len(words) == 2:
            command, path = words
            self.close_connection = True
            if command != 'GET':
                self.send_error(
                    HTTPStatus.BAD_REQUEST,
                    "Bad HTTP/0.9 request type (%r)" % command)
                return False
        elif not words:
            count += 1
            print("bla bla on 157 {0} => {1}".format(count,self.close_connection))
            return False
        else:
            self.send_error(
                HTTPStatus.BAD_REQUEST,
                "Bad request syntax (%r)" % requestline)
            return False
        self.command, self.path, self.request_version = command, path, version

        conntype = self.headers.get('Connection', "")
        if conntype.lower() == 'close':
            self.close_connection = True
        elif (conntype.lower() == 'keep-alive' and
              self.protocol_version >= "HTTP/1.1"):
            self.close_connection = False
         
        # Examine the headers and look for an Expect directive
        expect = self.headers.get('Expect', "")
        if (expect.lower() == "100-continue" and
                self.protocol_version >= "HTTP/1.1" and
                self.request_version >= "HTTP/1.1"):
            print("blabla on 185",self.close_connection)
            if not self.handle_expect_100():
                return False
        return True

    def do_GET(self):
        global G_replay_dict, test_mode_enabled
        if test_mode_enabled:
            request_hash = self.getTestName(self.requestline)
        else:
            request_hash, __ = cgi.parse_header(self.headers.get('Content-MD5'))
        #print("key:",request_hash)
        try:
            response_string=None
            chunkedResponse= False
            if request_hash not in G_replay_dict:
                self.send_response(404)
                self.send_header('Server','blablabla')
                self.send_header('Connection', 'close')
                self.end_headers()

            else:
                resp = G_replay_dict[request_hash]
                headers = resp.getHeaders().split('\r\n')

                # set status codes
                status_code = self.get_response_code(headers[0])
                self.send_response(status_code)

                # set headers
                for header in headers[1:]: # skip first one b/c it's response code
                    if header == '':
                        continue
                    elif 'Content-Length' in header:
                        if 'Access-Control' in header: # skipping Access-Control-Allow-Credentials, Access-Control-Allow-Origin, Content-Length
                            header_parts = header.split(':', 1)
                            header_field = str(header_parts[0].strip())
                            header_field_val = str(header_parts[1].strip())
                            self.send_header(header_field, header_field_val)
                            continue
                        lengthSTR = header.split(':')[1]
                        length = lengthSTR.strip(' ')
                        if test_mode_enabled: # the length of the body is given priority in test mode rather than the value in Content-Length. But in replay mode Content-Length gets the priority
                            if not (resp and resp.getBody()): # Don't attach content-length yet if body is present in the response specified by tester
                                self.send_header('Content-Length', str(length))
                        else:
                            self.send_header('Content-Length', str(length))
                        response_string = self.createDummyBodywithLength(int(length))
                        continue
                    if 'Transfer-Encoding' in header:
                        self.send_header('Transfer-Encoding','Chunked')
                        response_string='%X\r\n%s\r\n'%(len('ats'),'ats')
                        chunkedResponse= True                    
                        continue
            
                    header_parts = header.split(':', 1)
                    header_field = str(header_parts[0].strip())
                    header_field_val = str(header_parts[1].strip())
                    #print("{0} === >{1}".format(header_field, header_field_val))
                    self.send_header(header_field, header_field_val)
                #End for
                if test_mode_enabled:
                    if resp and resp.getBody():
                        length = len(bytes(resp.getBody(),'UTF-8'))
                        response_string=resp.getBody()
                        self.send_header('Content-Length', str(length))
                self.end_headers()
                
                
                if (chunkedResponse):
                    self.writeChunkedData()
                elif response_string!=None and response_string!='':
                    self.wfile.write(bytes(response_string, 'UTF-8'))
            return
        except:
            e=sys.exc_info()
            print("Error",e,self.headers)
            self.send_response(400)
            self.send_header('Connection', 'close')
            self.end_headers()
       

        
    def do_HEAD(self):
        global G_replay_dict, test_mode_enabled
        if test_mode_enabled:
            request_hash = self.getTestName(self.requestline)
        else:
            request_hash, __ = cgi.parse_header(self.headers.get('Content-MD5'))
        
        if request_hash not in G_replay_dict:
            self.send_response(404)
            self.send_header('Connection', 'close')
            self.end_headers()

        else:
            resp = G_replay_dict[request_hash]
            headers = resp.getHeaders().split('\r\n')

            # set status codes
            status_code = self.get_response_code(headers[0])
            self.send_response(status_code)

            # set headers
            for header in headers[1:]: # skip first one b/c it's response code
                if header == '':
                    continue
                elif 'Content-Length' in header:
                    self.send_header('Content-Length', '0')
                    continue
        
                header_parts = header.split(':', 1)
                header_field = str(header_parts[0].strip())
                header_field_val = str(header_parts[1].strip())
                #print("{0} === >{1}".format(header_field, header_field_val))
                self.send_header(header_field, header_field_val)

            self.end_headers()

    def do_POST(self):
        response_string=None
        chunkedResponse= False
        global G_replay_dict, test_mode_enabled
        if test_mode_enabled:
            request_hash = self.getTestName(self.requestline)
        else:
            request_hash, __ = cgi.parse_header(self.headers.get('Content-MD5'))
        try:
            if self.headers.get('Content-MD5') == None:
                print("Content-MD5 not found")
                self.send_response(404)
                self.send_header('Connection', 'close')
                self.end_headers()
                return

            if request_hash not in G_replay_dict:
                self.send_response(404)
                self.send_header('Connection', 'close')
                self.end_headers()
                resp = None
            else:
                resp = G_replay_dict[request_hash]
                resp_headers = resp.getHeaders().split('\r\n')
                # set status codes
                status_code = self.get_response_code(resp_headers[0])
                #print("response code",status_code)
                self.send_response(status_code)
                #print("reposen is ",resp_headers)
                # set headers
                for header in resp_headers[1:]: # skip first one b/c it's response code
                    
                    if header == '':
                        continue
                    elif 'Content-Length' in header:
                        if 'Access-Control' in header: # skipping Access-Control-Allow-Credentials, Access-Control-Allow-Origin, Content-Length
                            header_parts = header.split(':', 1)
                            header_field = str(header_parts[0].strip())
                            header_field_val = str(header_parts[1].strip())
                            self.send_header(header_field, header_field_val)
                            continue
                        
                        lengthSTR = header.split(':')[1]
                        length = lengthSTR.strip(' ')
                        if test_mode_enabled: # the length of the body is given priority in test mode rather than the value in Content-Length. But in replay mode Content-Length gets the priority
                            if not (resp and resp.getBody()): # Don't attach content-length yet if body is present in the response specified by tester
                                self.send_header('Content-Length', str(length))
                        else:
                            self.send_header('Content-Length', str(length))
                        response_string = self.createDummyBodywithLength(int(length))
                        continue
                    if 'Transfer-Encoding' in header:
                        self.send_header('Transfer-Encoding','Chunked')
                        response_string='%X\r\n%s\r\n'%(len('microserver'),'microserver')
                        chunkedResponse= True                    
                        continue
                    
                    header_parts = header.split(':', 1)
                    header_field = str(header_parts[0].strip())
                    header_field_val = str(header_parts[1].strip())
                    #print("{0} === >{1}".format(header_field, header_field_val))
                    self.send_header(header_field, header_field_val)
                # End for loop
                if test_mode_enabled:
                    if resp and resp.getBody():
                        length = len(bytes(resp.getBody(),'UTF-8'))
                        response_string=resp.getBody()
                        self.send_header('Content-Length', str(length))    
                self.end_headers()
            
            if (chunkedResponse):
                self.writeChunkedData()
            elif response_string!=None and response_string!='':
                self.wfile.write(bytes(response_string, 'UTF-8'))
            return
        except:
            e=sys.exc_info()
            print("Error",e,self.headers)
            self.send_response(400)
            self.send_header('Connection', 'close')
            self.end_headers()

def populate_global_replay_dictionary(sessions):
    ''' Populates the global dictionary of {uuid (string): reponse (Response object)} '''
    global G_replay_dict
    for session in sessions:
        for txn in session.getTransactionIter():
            G_replay_dict[txn._uuid] = txn.getResponse()
    
    print("size",len(G_replay_dict))
    
#tests will add responses to the dictionary where key is the testname
def addResponseHeader(key,response_header):
    G_replay_dict[key] = response_header
    
def _path(exists, arg ):
    path = os.path.abspath(arg)
    if not os.path.exists(path) and exists:
        msg = '"{0}" is not a valid path'.format(path)
        raise argparse.ArgumentTypeError(msg)
    return path

def _bool(arg):
        
        opt_true_values = set(['y', 'yes', 'true', 't', '1', 'on' , 'all'])
        opt_false_values = set(['n', 'no', 'false', 'f', '0', 'off', 'none'])

        tmp = arg.lower()
        if tmp in opt_true_values:
            return True
        elif tmp in opt_false_values:
            return False
        else:
            msg = 'Invalid value Boolean value : "{0}"\n Valid options are {0}'.format(arg,
                    opt_true_values | opt_false_values)
            raise argparse.ArgumentTypeError(msg)


def main():
    global test_mode_enabled
    parser = argparse.ArgumentParser()

    parser.add_argument("--data-dir","-d",
                        type=lambda x: _path(True,x),
                        required=True,
                        help="Directory with data file"
                        )

    parser.add_argument("--public","-P", 
                        type=_bool, 
                        default=False,                        
                        help="Bind server to public IP 0.0.0.0 vs private IP of 127.0.0.1"
                        )

    parser.add_argument("--port","-p",
                        type=int,
                        default=SERVER_PORT,                        
                        help="Port to use")

    parser.add_argument("--timeout","-t", 
                        type=float,
                        default=None,                        
                        help="socket time out in seconds")                        

    parser.add_argument('-V','--version', action='version', version='%(prog)s {0}'.format(__version__))

    parser.add_argument("--mode","-m",
                        type=str,
                        default="test",                        
                        help="Mode of operation")
    parser.add_argument("--connection","-c",
                        type=str,
                        default="nonSSL",                        
                        help="use SSL")
    parser.add_argument("--key","-k",
                        type=str,
                        default="ssl/server.pem",                        
                        help="key for ssl connnection")
    parser.add_argument("--cert","-cert",
                        type=str,
                        default="ssl/server.crt",                        
                        help="certificate")
    parser.add_argument("--clientverify","-cverify",
                        type=bool,
                        default=False,
                        help="verify client cert")

    args=parser.parse_args()
    options = args
    # set up global dictionary of {uuid (string): response (Response object)}
    s = sv.SessionValidator(args.data_dir)
    populate_global_replay_dictionary(s.getSessionIter())
    print("Dropped {0} sessions for being malformed".format(len(s.getBadSessionList())))
    
    # start server
    try:
        server_port = args.port
        socket_timeout = args.timeout
        test_mode_enabled = args.mode=="test"
        
        MyHandler.protocol_version = HTTP_VERSION        
        if options.connection == 'ssl':
            server = SSLServer(('',options.port), MyHandler, options)
        else:
            server = ThreadingServer(('', server_port), MyHandler)
        server.timeout = 5
        print("started server")
        server_thread = threading.Thread(target=server.serve_forever())
        server_thread.daemon=True
        server_thread.start()

        #s_serverThread.daemon = True
        #s_serverThread.start()
        #threads.append(s_serverThread)

        
        #server.timeout = socket_timeout or 5
        #print("=== started httpserver ===")
        #server_thread = threading.Thread(target=server.serve_forever())
        #server_thread.daemon=True
        #server_thread.start()
        #threads.append(server_thread)
        #server.serve_forever()
        
        #for t in threads:
        #    t.start()

        #for t in threads:
        #    t.join()
    except KeyboardInterrupt:
        print("\n=== ^C received, shutting down httpserver ===")
        server.socket.close()
        #s_server.socket.close()
        sys.exit(0)


if __name__ == '__main__':
    main()
