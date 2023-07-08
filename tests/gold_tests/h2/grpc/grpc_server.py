"""A gRPC server."""

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

import argparse
from concurrent import futures
import grpc
import sys
import time

import simple_pb2
import simple_pb2_grpc


class SimpleServicer(simple_pb2_grpc.SimpleServicer):
    """A gRPC servicer."""

    def SimpleMethod(self, request, context):
        """An example gRPC method."""
        print(f'Request received from client: {request.message}')
        response = simple_pb2.SimpleResponse(message=f"Echo: {request.message}")
        return response


def run_grpc_server(port: int, server_cert: str, server_key: str) -> int:
    """Run the gRPC server.

    :param port: The port on which to listen.
    :param server_cert: The public TLS certificate to use.
    :param server_key: The private TLS key to use.
    :return: The exit code.
    """
    credentials = grpc.ssl_server_credentials([(server_key, server_cert)])
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    simple_pb2_grpc.add_SimpleServicer_to_server(SimpleServicer(), server)
    server_endpoint = f'127.0.0.1:{port}'
    server.add_secure_port(server_endpoint, credentials)
    print(f'Listening on: {server_endpoint}')
    server.start()
    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        print("Keyboard interrupt received, exiting.")
        return 0
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int, help='The port on which to listen.')
    parser.add_argument('server_crt', type=argparse.FileType('rb'), help="The public TLS certificate to use.")
    parser.add_argument('server_key', type=argparse.FileType('rb'), help="The private TLS key to use.")
    return parser.parse_args()


def main() -> int:
    """Run the main entry point for the gRPC server.

    :return: The exit code.
    """
    args = parse_args()
    return run_grpc_server(args.port, args.server_crt.read(), args.server_key.read())


if __name__ == '__main__':
    sys.exit(main())
