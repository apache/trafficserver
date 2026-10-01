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
import asyncio
import grpc
import os
import sys

import simple_pb2
import simple_pb2_grpc

global_message_counter: int = 0
global_num_completed_connections: int = 0


async def run_grpc_client(hostname: str, proxy_port: int, proxy_cert: bytes) -> int:
    """Run the gRPC client.

    :param hostname: The hostname to which to connect.
    :param proxy_port: The ATS port to which to connect.
    :param proxy_cert: The public TLS certificate to verify ATS against.
    :return: The exit code.
    """
    global global_message_counter
    global global_num_completed_connections
    credentials = grpc.ssl_channel_credentials(root_certificates=proxy_cert)
    channel_options = (('grpc.ssl_target_name_override', hostname),)
    destination_endpoint = f'127.0.0.1:{proxy_port}'
    async with grpc.aio.secure_channel(destination_endpoint, credentials, options=channel_options) as channel:
        print(f'Connecting to: {destination_endpoint}')
        stub = simple_pb2_grpc.TalkerStub(channel)

        print(f'Creating two messages to send for counter: {global_message_counter}')
        message_1 = simple_pb2.SimpleRequest(message=f'Client request message: {global_message_counter}.1')
        message_2 = simple_pb2.SimpleRequest(message=f'Client request message: {global_message_counter}.2')
        my_message_count = global_message_counter
        global_message_counter += 1
        print(f'Sending request: {my_message_count}.1')
        response = await stub.MakeRequest(message_1)
        print(f'Response {my_message_count}.1 received from server: {response.message}')
        print(f'Sending the second request: {my_message_count}.2')
        message = simple_pb2.SimpleRequest(message=f'Client request message: {global_message_counter}.2')
        response = await stub.MakeAnotherRequest(message_2)
        print(f'Response {my_message_count}.2 received from server: {response.message}')
        global_num_completed_connections += 1
    return 0


async def run_grpc_clients(hostname: str, proxy_port: int, proxy_cert: bytes, num_connections: int) -> int:
    """Run the gRPC client.

    :param hostname: The hostname to which to connect.
    :param proxy_port: The ATS port to which to connect.
    :param proxy_cert: The public TLS certificate to verify ATS against.
    :param num_connections: The number of client connections to create.
    :return: The exit code.
    """
    tasks: list[asyncio.Task] = []
    for i in range(num_connections):
        print(f'Creating client {i}')
        tasks.append(run_grpc_client(hostname, proxy_port, proxy_cert))
    await asyncio.gather(*tasks)
    if global_num_completed_connections != num_connections:
        print(f'Expected {num_connections} responses, but got {global_num_completed_connections}')
        return 1
    else:
        print(f'Got the expected {num_connections} responses.')
        return 0


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('hostname', help='The hostname to which to connect.')

    parser.add_argument('proxy_port', metavar='proxy-port', type=int, help='The ATS port to which to connect.')
    parser.add_argument('proxy_cert', metavar='proxy-cert', type=argparse.FileType('rb'), help='The public TLS certificate to use.')
    parser.add_argument('num_connections', metavar='num-connections', type=int, help='The number of connections to create.')
    return parser.parse_args()


def main() -> int:
    """Run the main entry point for the gRPC client.

    :return: The exit code.
    """
    args = parse_args()

    try:
        return asyncio.run(run_grpc_clients(args.hostname, args.proxy_port, args.proxy_cert.read(), args.num_connections))
    except grpc.RpcError as e:
        print(f'RPC failed with code {e.code()}: {e.details()}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
