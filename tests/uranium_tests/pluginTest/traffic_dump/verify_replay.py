#!/usr/bin/env python3
"""
Verify that a given JSON replay file fulfills basic expectations.
"""
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
import json
import jsonschema
import sys


expected_sensitive_value = \
    '''0000000 0000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 0000009 000000a 000000b 000000c 000000d ''' \
    '''000000e 000000f 0000010 0000011 0000012 0000013 0000014 0000015 0000016 0000017 0000018 0000019 000001a 000001b ''' \
    '''000001c 000001d 000001e 000001f 0000020 0000021 0000022 0000023 0000024 0000025 0000026 0000027 0000028 0000029 ''' \
    '''000002a 000002b 000002c 000002d 000002e 000002f 0000030 0000031 0000032 0000033 0000034 0000035 0000036 0000037 ''' \
    '''0000038 0000039 000003a 000003b 000003c 000003d 000003e 000003f 0000040 0000041 0000042 0000043 0000044 0000045 ''' \
    '''0000046 0000047 0000048 0000049 000004a 000004b 000004c 000004d 000004e 000004f 0000050 0000051 0000052 0000053 ''' \
    '''0000054 0000055 0000056 0000057 0000058 0000059 000005a 000005b 000005c 000005d 000005e 000005f 0000060 0000061 ''' \
    '''0000062 0000063 0000064 0000065 0000066 0000067 0000068 0000069 000006a 000006b 000006c 000006d 000006e 000006f ''' \
    '''0000070 0000071 0000072 0000073 0000074 0000075 0000076 0000077 0000078 0000079 000007a 000007b 000007c 000007d ''' \
    '''000007e 000007f 0000080 0000081 0000082 0000083 0000084 0000085 0000086 0000087 0000088 0000089 000008a 000008b ''' \
    '''000008c 000008d 000008e 000008f 0000090 0000091 0000092 0000093 0000094 0000095 0000096 0000097 0000098 0000099 ''' \
    '''000009a 000009b 000009c 000009d 000009e 000009f 00000a0 00000a1 00000a2 00000a3 00000a4 00000a5 00000a6 00000a7 ''' \
    '''00000a8 00000a9 00000aa 00000ab 00000ac 00000ad 00000ae 00000af 00000b0 00000b1 00000b2 00000b3 00000b4 00000b5 ''' \
    '''00000b6 00000b7 00000b8 00000b9 00000ba 00000bb 00000bc 00000bd 00000be 00000bf 00000c0 00000c1 00000c2 00000c3 ''' \
    '''00000c4 00000c5 00000c6 00000c7 00000c8 00000c9 00000ca 00000cb 00000cc 00000cd 00000ce 00000cf 00000d0 00000d1 ''' \
    '''00000d2 00000d3 00000d4 00000d5 00000d6 00000d7 00000d8 00000d9 00000da 00000db 00000dc 00000dd 00000de 00000df ''' \
    '''00000e0 00000e1 00000e2 00000e3 00000e4 00000e5 00000e6 00000e7 00000e8 00000e9 00000ea 00000eb 00000ec 00000ed ''' \
    '''00000ee 00000ef 00000f0 00000f1 00000f2 00000f3 00000f4 00000f5 00000f6 00000f7 00000f8 00000f9 00000fa 00000fb ''' \
    '''00000fc 00000fd 00000fe 00000ff'''


def validate_json(schema_json, replay_json):
    """
    Validate the replay file against the provided schema.
    """
    try:
        jsonschema.validate(instance=replay_json, schema=schema_json)
    except jsonschema.ValidationError as e:
        print("The replay file does not validate against the schema: {}".format(e))
        return False
    else:
        return True


def verify_there_was_a_transaction(replay_json):
    """
    Verify that the replay file has a sensible looking transaction.
    """
    try:
        transactions = replay_json['sessions'][0]['transactions']
    except KeyError:
        print("The replay file did not have transactions in it.")
        return False

    if len(transactions) < 1:
        print("There are no transactions in the replay file.")
        return False
    transaction = transactions[0]
    if not ('client-request' in transaction and 'proxy-response' in transaction):
        print("There was not request and response in the transaction of the replay file.")
        return False

    return True


def verify_http_information(replay_json):
    """
    Verify various expected HTTP information.
    """
    for session in replay_json['sessions']:
        top_layer = session['protocol'][0]
        is_http2 = top_layer['name'] == 'http' and top_layer['version'] == '2'
        if is_http2:
            # Every HTTP/2 client request should have a stream-id node.
            for transaction in session['transactions']:
                client_request = transaction['client-request']
                try:
                    client_request['http2']['stream-id']
                except KeyError:
                    print("An HTTP/2 transaction did not have a stream-id node.")
                    return False

    return True


def verify_request_target(replay_json, request_target):
    """
    Verify that the 'url' element of the first transaction contains the request target.
    """
    try:
        url = replay_json['sessions'][0]['transactions'][0]['client-request']['url']
    except KeyError:
        print("The replay file did not have a first transaction with a url element.")
        return False

    if url != request_target:
        print("Mismatched request target. Expected: {}, received: {}".format(request_target, url))
        return False
    return True


def verify_client_request_size(replay_json, client_request_size):
    """
    Verify that the 'url' element of the first transaction contains the request target.
    """
    try:
        size = int(replay_json['sessions'][0]['transactions'][0]['client-request']['content']['size'])
    except KeyError:
        print("The replay file did not have content size element in the first client-request.")
        return False

    if size != client_request_size:
        print("Mismatched client-request request size. Expected: {}, received: {}".format(client_request_size, size))
        return False
    return True


def verify_sensitive_fields_not_dumped(replay_json, sensitive_fields):
    """
    Verify that all of the cookie fields have the expected value.
    """
    message_types = ['client-request', 'proxy-request', 'server-response', 'proxy-response']
    try:
        for session in replay_json['sessions']:
            for transaction in session['transactions']:
                for message_type in transaction:
                    if message_type not in message_types:
                        continue
                    message = transaction[message_type]
                    for field in message['headers']['fields']:
                        field_name = field[0].lower()
                        if field_name in sensitive_fields:
                            field_value = field[1]
                            if field_value not in expected_sensitive_value:
                                print("Found an unexpected cookie: {}: {}".format(field[0], field[1]))
                                return False

    except KeyError:
        print("Could not find headers in the replay file.")
        return False

    return True


def verify_protocols_helper(replay_json, expected_protocols, is_client):
    try:
        if is_client:
            protocol_node = replay_json['sessions'][0]['protocol']
        else:
            protocol_node = replay_json['sessions'][0]['transactions'][0]['proxy-request']['protocol']

        dumped_protocols = ''
        is_first_protocol = True
        for protocol in protocol_node:
            if not is_first_protocol:
                dumped_protocols += ","
            is_first_protocol = False
            dumped_protocols += protocol['name']

        if dumped_protocols == expected_protocols:
            return True
        else:
            host_name = "client" if is_client else "server"
            print(
                'Unexpected protocol {} stack. Expected: "{}", found: "{}".'.format(
                    host_name, expected_protocols, dumped_protocols))
            return False
    except KeyError:
        print("Could not find {} protocol stack node in the replay file.".format(host_name))
        return False


def verify_client_protocols(replay_json, expected_protocols):
    return verify_protocols_helper(replay_json, expected_protocols, is_client=True)


def verify_server_protocols(replay_json, expected_protocols):
    return verify_protocols_helper(replay_json, expected_protocols, is_client=False)


def get_tls_features(replay_json, is_client):
    session = replay_json['sessions'][0]
    if is_client:
        protocol_parent = session['protocol']
    else:
        protocol_parent = session['transactions'][0]['proxy-request']['protocol']

    for protocol in protocol_parent:
        if protocol['name'] == "tls":
            return protocol
    else:
        raise KeyError('The "tls" protocol node was not found.')


def verify_tls_features(expected_tls_features, found_tls_features):
    for expected_tls_feature in expected_tls_features.split(','):
        expected_key, expected_value = expected_tls_feature.split(':')
        try:
            found_value = found_tls_features[expected_key]
        except KeyError:
            print("Could not find tls feature in the replay file: {}".format(expected_key))
            return False
        value_matches = False
        if isinstance(found_value, bool) or (found_value in ('true', 'false')):
            if expected_value.lower() == "false":
                expected_value = False
            elif expected_value.lower() == "true":
                expected_value = True
            else:
                raise ValueError("Cannot convert expected value to a boolean: {}, found: {}".format(expected_value, found_value))
            if found_value == 'true':
                found_value = True
            elif found_value == 'false':
                found_value = False
            value_matches = found_value == bool(expected_value)

        elif isinstance(found_value, str):
            value_matches = found_value == expected_value
        elif isinstance(found_value, int):
            value_matches = found_value == int(expected_value)
        else:
            raise ValueError("Cannot determine type of found value: {}".format(found_value))

        if not value_matches:
            print('Mismatched value for "{}", expected "{}", received "{}"'.format(expected_key, expected_value, found_value))
            return False
    return True


def verify_client_tls_features(replay_json, expected_tls_features):
    try:
        tls_features = get_tls_features(replay_json, is_client=True)
    except KeyError:
        print("Could not find client tls node in the replay file.")
        return False

    if not verify_tls_features(expected_tls_features, tls_features):
        print('Failed to verify client tls features in "{}"'.format(expected_tls_features))
        return False
    return True


def verify_server_tls_features(replay_json, expected_tls_features):
    try:
        tls_features = get_tls_features(replay_json, is_client=False)
    except KeyError:
        print("Could not find server tls node in the replay file.")
        return False

    if not verify_tls_features(expected_tls_features, tls_features):
        print('Failed to verify server tls features in "{}"'.format(expected_tls_features))
        return False
    return True


def verify_client_http_version(replay_json, expected_client_http_version):
    try:
        found_version = replay_json['sessions'][0]['transactions'][0]['client-request']['version']
    except KeyError:
        print("Could not find client-request:version node in the replay file.")
        return False

    if expected_client_http_version != found_version:
        print('Expected client version of "{}", but found "{}"'.format(expected_client_http_version, found_version))
        return False
    return True


def verify_client_request_body_bytes(replay_json, expected_body_bytes):
    """
    Verify that the replay file has the specified client request body bytes in it.
    """
    try:
        received_body_bytes = replay_json['sessions'][0]['transactions'][0]['client-request']['content']['data']
    except KeyError:
        print("The replay file did not have a client-request content data element in the first transaction.")
        return False

    if received_body_bytes != expected_body_bytes:
        print("Expected body bytes of '{0}' but got '{1}'".format(expected_body_bytes, received_body_bytes))
        return False

    # If the client request had that many bytes, verify that the proxy-request
    # does too. This is not guaranteed to always be true, but for our autest it
    # currently holds and this check is worthwhile.
    try:
        proxy_request_body_size = replay_json['sessions'][0]['transactions'][0]['proxy-request']['content']['size']
    except KeyError:
        print("The replay file did not have a proxy-request content size element in the first transaction.")
        return False

    if int(proxy_request_body_size) != len(expected_body_bytes):
        print(
            "Expected the proxy-request content size to be '{0}' but got '{1}'".format(
                len(expected_body_bytes), proxy_request_body_size))
        return False

    return True


def verify_proxy_response_body_bytes(replay_json, expected_body_bytes):
    """
    Verify that the replay file has the specified proxy response body bytes in it.
    """
    try:
        received_body_bytes = replay_json['sessions'][0]['transactions'][0]['proxy-response']['content']['data']
    except KeyError:
        print("The replay file did not have a proxy-response content data element in the first transaction.")
        return False

    if received_body_bytes != expected_body_bytes:
        print("Expected body bytes of '{0}' but got '{1}'".format(expected_body_bytes, received_body_bytes))
        return False

    # If the proxy response had that many bytes, verify that the server-response
    # does too. This is not guaranteed to always be true, but for our autest it
    # currently holds and this check is worthwhile.
    try:
        server_response_body_size = replay_json['sessions'][0]['transactions'][0]['server-response']['content']['size']
    except KeyError:
        print("The replay file did not have a server-response content size element in the first transaction.")
        return False

    if int(server_response_body_size) != len(expected_body_bytes):
        print(
            "Expected the server-response content size to be '{0}' but got '{1}'".format(
                len(expected_body_bytes), server_response_body_size))
        return False

    return True


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("schema_file", type=argparse.FileType('r'), help="The schema in which to validate the replay file.")
    parser.add_argument("replay_file", type=argparse.FileType('r'), help="The replay file to validate.")
    parser.add_argument("--request-target", help="The request target ('url' element) to expect in the replay file.")
    parser.add_argument("--client-request-size", type=int, help="The expected size value in the client-request node.")
    parser.add_argument(
        "--sensitive-fields",
        action="append",
        help="The fields that are considered sensitive and replaced with insensitive values.")
    parser.add_argument("--client-protocols", help="The comma-separated sequence of protocols to expect for the client connection.")
    parser.add_argument("--server-protocols", help="The comma-separated sequence of protocols to expect for the server connection.")
    parser.add_argument("--client-tls-features", help="The TLS values to expect for the client connection.")
    parser.add_argument("--server-tls-features", help="The TLS values to expect for the server connection.")
    parser.add_argument("--client-http-version", help="The client HTTP version to expect")
    parser.add_argument("--request_body", type=str, help="Verify that the client request has the specified body bytes.")
    parser.add_argument("--response_body", type=str, help="Verify that the proxy response has the specified body bytes.")
    return parser.parse_args()


def main():
    args = parse_args()

    schema_json = json.load(args.schema_file)
    replay_json = json.load(args.replay_file)

    if not validate_json(schema_json, replay_json):
        return 1

    # Verifying that there is a transaction in the replay file may seem
    # unnecessary since the replay file validated against the schema. But a JSON
    # file that doesn't have conflicting entry names will pass the schema. For
    # instance, this passes against our replay schema:
    #
    # {"name": "Bob", "languages": ["English", "French"]}
    #
    # Thus we do the following sanity check to make sure that the replay file
    # appears to have some transaction in it.
    if not verify_there_was_a_transaction(replay_json):
        return 1

    if not verify_http_information(replay_json):
        return 1

    if args.request_target and not verify_request_target(replay_json, args.request_target):
        return 1

    if args.client_request_size and not verify_client_request_size(replay_json, args.client_request_size):
        return 1

    if args.sensitive_fields and not verify_sensitive_fields_not_dumped(replay_json, args.sensitive_fields):
        return 1

    if args.client_protocols and not verify_client_protocols(replay_json, args.client_protocols):
        return 1

    if args.server_protocols and not verify_server_protocols(replay_json, args.server_protocols):
        return 1

    if args.client_tls_features and not verify_client_tls_features(replay_json, args.client_tls_features):
        return 1

    if args.server_tls_features and not verify_server_tls_features(replay_json, args.server_tls_features):
        return 1

    if args.client_http_version and not verify_client_http_version(replay_json, args.client_http_version):
        return 1

    if args.request_body and not verify_client_request_body_bytes(replay_json, args.request_body):
        return 1

    if args.response_body and not verify_proxy_response_body_bytes(replay_json, args.response_body):
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
