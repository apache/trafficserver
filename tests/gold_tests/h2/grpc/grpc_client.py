"""A gRPC client."""

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
import grpc
import os
import sys

import simple_pb2
import simple_pb2_grpc


def run_grpc_client(hostname: str, proxy_port: int, proxy_cert: bytes) -> int:
    """Run the gRPC client.

    :param hostname: The hostname to which to connect.
    :param proxy_port: The ATS port to which to connect.
    :param proxy_cert: The public TLS certificate to verify ATS against.
    :return: The exit code.
    """
    credentials = grpc.ssl_channel_credentials(root_certificates=proxy_cert)
    channel_options = (('grpc.ssl_target_name_override', hostname),)
    destination_endpoint = f'127.0.0.1:{proxy_port}'
    channel = grpc.secure_channel(destination_endpoint, credentials, options=channel_options)
    print(f'Connecting to: {destination_endpoint}')
    stub = simple_pb2_grpc.SimpleStub(channel)

    message = simple_pb2.SimpleRequest(message="Client request message")
    response = stub.SimpleMethod(message)
    print(f'Response received from server: {response.message}')
    return 0


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('hostname', help='The hostname to which to connect.')

    parser.add_argument('proxy_port', type=int, help='The ATS port to which to connect.')
    parser.add_argument('proxy_cert', type=argparse.FileType('rb'), help='The public TLS certificate to use.')
    return parser.parse_args()


def main() -> int:
    """Run the main entry point for the gRPC client.

    :return: The exit code.
    """
    args = parse_args()

    try:
        return run_grpc_client(args.hostname, args.proxy_port, args.proxy_cert.read())
    except grpc.RpcError as e:
        print(f'RPC failed with code {e.code()}: {e.details()}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
