#!/usr/bin/env python3
'''
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
'''

from collections import defaultdict
from copy import deepcopy
from queue import Queue
from threading import Thread
import argparse
import json
import logging
import os
import sys

description = '''
Post process replay files produced by traffic_dump and clean it of any
incomplete transactions or sessions which got partially written because Traffic
Server was interrupted mid-connection. This also merges sessions to the same
client and, by default, formats the output files with human readable spacing.
'''

# Base replay file template with basic elements
TEMPLATE = json.loads('{"meta": {"version":"1.0"},"sessions":[]}')


class PostProcessError(Exception):
    ''' Base class for post processing errors.
    '''

    def __init__(self, message=None):
        self.message = message

    def __str__(self, *args):
        if self.message:
            return self.message
        else:
            return 'PostProcessError raised'


class VerifyError(PostProcessError):
    ''' Base class for node node verification errors.
    '''
    pass


class VerifyRequestError(VerifyError):
    ''' There was a problem verifying a request node.
    '''
    pass


class VerifyResponseError(VerifyError):
    ''' There was a problem verifying a response node.
    '''
    pass


class VerifySessionError(VerifyError):
    ''' There was a problem verifying a session node.
    '''
    pass


def verify_request(request):
    """ Function to verify request with method, url, and headers
    Args:
        request (json object)

    Raises:
        VerifyRequestError if there is a problem with the request.
    """
    if not request:
        raise VerifyRequestError('No request found.')
    if "method" not in request or not request["method"]:
        raise VerifyRequestError("Request did not have a method.")
    if "url" not in request or not request["url"]:
        raise VerifyRequestError("Request did not have a url.")
    if "headers" not in request or not request["headers"]:
        raise VerifyRequestError("Request did not have headers.")


def verify_response(response):
    """ Function to verify response with status
    Args:
        response (json object)

    Raises:
        VerifyResponseError if there is a problem with the response.
    """
    if not response:
        raise VerifyResponseError("No response found.")
    if "status" not in response or not response["status"]:
        raise VerifyResponseError("Response did not have a status.")


def verify_transaction(transaction, fabricate_proxy_requests=False):
    """ Function to verify that a transaction looks complete.

    Args:
        transaction (json object)
        fabricate_proxy_requests (bool) Whether the post-processor should
          fabricate proxy requests if they don't exist because the proxy served
          the response locally.

    Raises:
        VerifySessionError if there is no transaction.
        VerifyRequestError if there is a problem with a request.
        VerifyResponseError if there is a problem with a response.
    """
    if not transaction:
        raise VerifySessionError('No transaction found in the session.')

    if "client-request" not in transaction:
        raise VerifyRequestError('client-request not found in transaction')
    else:
        verify_request(transaction["client-request"])

    if "proxy-request" not in transaction and fabricate_proxy_requests:
        if "proxy-response" not in transaction:
            raise VerifyRequestError('proxy-response not found in transaction with a client-request')
        transaction["proxy-request"] = transaction["client-request"]
        if "server-response" not in transaction:
            transaction["server-response"] = transaction["proxy-response"]

    # proxy-response nodes can be empty.
    if "proxy-response" not in transaction:
        raise VerifyResponseError('proxy-response not found in transaction')

    if "proxy-request" in transaction or "server-response" in transaction:
        # proxy-request nodes can be empty, so no need to verify_response.
        if "proxy-request" not in transaction:
            raise VerifyRequestError('proxy-request not found in transaction')

        if "server-response" not in transaction:
            raise VerifyResponseError('server-response not found in transaction')
        else:
            verify_response(transaction["server-response"])


def verify_session(session, fabricate_proxy_requests=False):
    """ Function to verify that a session looks complete.

        A valid session contains a valid list of transactions.

    Args:
        transaction (json object)
        fabricate_proxy_requests (bool) Whether the post-processor should
          fabricate proxy requests if they don't exist because the proxy served
          the response locally.

    Raises:
        VerifyError if there is a problem with the session.
    """
    if not session:
        raise VerifySessionError('Session not found.')
    if "transactions" not in session or not session["transactions"]:
        raise VerifySessionError('No transactions found in session.')
    for transaction in session["transactions"]:
        verify_transaction(transaction, fabricate_proxy_requests)


def write_sessions(sessions, filename, indent):
    """ Write the JSON sessions to the given filename.

    Args:
        sessions The parsed JSON sessions to dump into filename.
        filename (string) The path to the file to write the parsed JSON file to.
        indent (int) The number of spaces per line to write to the file. A
          value of None causes the whole JSON file to be written as a single line.
    """
    new_json = deepcopy(TEMPLATE)
    new_json["sessions"] = deepcopy(sessions)
    with open(filename, "w") as f:
        json.dump(new_json, f, ensure_ascii=False, indent=indent)
        logging.debug(f"{filename} has {len(sessions)} sessions")


class ParseJSONError(PostProcessError):
    ''' There was an error opening or parsing the replay file.
    '''
    pass


def parse_json(replay_file):
    """ Open and parse the replay_file.

    Args:
        replay_file (string) The file with JSON content to parse.

    Return:
        The json package parsed JSON file or None if there was a problem
        parsing the file.
    """
    try:
        fd = open(replay_file, 'r')
    except Exception as e:
        logging.exception("Failed to open %s.", replay_file)
        raise ParseJSONError(e)

    try:
        parsed_json = json.load(fd)
    except Exception as e:
        message = e.msg.split(':')[0]
        logging.error("Failed to load %s as a JSON object: %s", replay_file, e)
        raise ParseJSONError(message)

    return parsed_json


def readAndCombine(replay_dir, num_sessions_per_file, indent, fabricate_proxy_requests, out_dir):
    """ Read raw dump files, filter out incomplete sessions, and merge
    them into output files.

    Args:
        replay_dir (string) Full path to dumps
        num_sessions_per_file (int) number of sessions in each output file
        indent (int) The number of spaces per line in the output replay files.
        fabricate_proxy_requests (bool) Whether the post-processor should
          fabricate proxy requests if they don't exist because the proxy served
          the response locally.
        out_dir (string) Output directory for post-processed json files.
    """
    session_count = 0
    batch_count = 0
    transaction_count = 0
    error_count = defaultdict(int)

    base_name = os.path.basename(replay_dir)

    sessions = []
    for f in os.listdir(replay_dir):
        replay_file = os.path.join(replay_dir, f)
        if not os.path.isfile(replay_file):
            continue

        try:
            parsed_json = parse_json(replay_file)
        except ParseJSONError as e:
            error_count[e.message] += 1
            continue

        for session in parsed_json["sessions"]:
            try:
                verify_session(session, fabricate_proxy_requests)
            except VerifyError as e:
                connection_time = session['connection-time']
                if not connection_time:
                    connection_time = session['start-time']
                if connection_time:
                    logging.debug("Omitting session in %s with connection-time: %d: %s", replay_file, session['connection-time'], e)
                else:
                    logging.debug("Omitting a session in %s, could not find a connection time: %s", replay_file, e)
                continue
            sessions.append(session)
            session_count += 1
            transaction_count += len(session["transactions"])
        if len(sessions) >= num_sessions_per_file:
            write_sessions(sessions, f"{out_dir}/{base_name}_{batch_count}.json", indent)
            sessions = []
            batch_count += 1
    if sessions:
        write_sessions(sessions, f"{out_dir}/{base_name}_{batch_count}.json", indent)

    return session_count, transaction_count, error_count


def post_process(in_dir, subdir_q, out_dir, num_sessions_per_file, single_line, fabricate_proxy_requests, cnt_q):
    """ Function used to set up individual threads.

    Each thread loops over the subdir_q, pulls a directory from there, and
    process the replay files in that directory. The threads finish when the
    subdir queue is empty, meaning each subdir has been processed.

    Args:
        in_dir (string) Path to parent of the subdirectories in subdir_q.
        subdir_q (Queue) Queue of subdir to read from.
        out_dir (string) The directory into which the post processed replay files
          are placed.
        num_sessions_per_file (int) traffic_dump will emit a separate file per
          session. This mechanism merges sessions within a single subdir.
          num_sessions_per_file is the limit to the number of sessions merged
          into a single replay file.
        single_line (bool) Whether to emit replay files as a single line. If
          false, the file is spaced out in a human readable fashion.
        fabricate_proxy_requests (bool) Whether the post-processor should
          fabricate proxy requests if they don't exist because the proxy served
          the response locally.
        cnt_q (Queue) Session, transaction, error count queue populated by each
          thread.
    """
    while not subdir_q.empty():
        subdir = subdir_q.get()
        subdir_path = os.path.join(in_dir, subdir)
        indent = 2
        if single_line:
            indent = None
        cnt = readAndCombine(subdir_path, num_sessions_per_file, indent, fabricate_proxy_requests, out_dir)
        cnt_q.put(cnt)


def configure_logging(use_debug=False):
    ''' Configure the logging mechanism.

    Args:
        use_debug (bool) Whether to configure debug-level logging.
    '''
    log_format = '%(levelname)s: %(message)s'
    if use_debug:
        logging.basicConfig(format=log_format, level=logging.DEBUG)
    else:
        logging.basicConfig(format=log_format, level=logging.INFO)


def parse_args():
    ''' Parse the command line arguments.
    '''
    parser = argparse.ArgumentParser(description=description)

    parser.add_argument(
        "in_dir",
        type=str,
        help='''The input directory of traffic_dump replay
                        files.  The expectation is that this will contain
                        sub-directories that themselves contain replay files.
                        This is written to accommodate the directory populated
                        by traffic_dump via the --logdir option.''')
    parser.add_argument("out_dir", type=str, help="The output directory of post processed replay files.")
    parser.add_argument(
        "-n",
        "--num_sessions",
        type=int,
        default=10,
        help='''The maximum number of sessions merged into
                        single replay output files. The default is 10.''')
    parser.add_argument(
        "--no-human-readable",
        action="store_true",
        help='''By default, post processor will generate replay
                        files that are spaced out in a human readable format.
                        This turns off that behavior and leaves the files as
                        single-line entries.''')
    parser.add_argument(
        "--no-fabricate-proxy-requests",
        action="store_true",
        help='''By default, post processor will fabricate proxy
                        requests and server responses for transactions served
                        out of the proxy. Presumably in replay conditions,
                        these fabricated requests and responses will not hurt
                        anything because the Proxy Verifier server will not
                        notice if the proxy replies locally in replay
                        conditions. However, if it doesn't reply locally, then
                        the server will not know how to reply to these
                        requests. Using this option turns off this fabrication
                        behavior.''')
    parser.add_argument("-j", "--num_threads", type=int, default=32, help='''The maximum number of threads to use.''')
    parser.add_argument("-d", "--debug", action="store_true", help="Enable debug level logging.")
    return parser.parse_args()


def main():
    args = parse_args()
    configure_logging(use_debug=args.debug)
    logging.debug("Original options: %s", " ".join(sys.argv))

    if not os.path.exists(args.out_dir):
        os.mkdir(args.out_dir)

    # generate thread arguments
    subdir_q = Queue()
    cnt_q = Queue()
    for subdir in os.listdir(args.in_dir):
        if os.path.isdir(os.path.join(args.in_dir, subdir)):
            subdir_q.put(subdir)

    threads = []
    nthreads = min(max(subdir_q.qsize(), 1), args.num_threads)

    # Start up the threads.
    for _ in range(nthreads):
        t = Thread(
            target=post_process,
            args=(
                args.in_dir, subdir_q, args.out_dir, args.num_sessions, args.no_human_readable,
                not args.no_fabricate_proxy_requests, cnt_q))
        t.start()
        threads.append(t)

    # Wait for them to finish.
    for t in threads:
        t.join()

    # Retrieve the counts
    session_count = 0
    transaction_count = 0
    errors = defaultdict(int)
    for count_tuple in list(cnt_q.queue):
        session_count += count_tuple[0]
        transaction_count += count_tuple[1]
        for e in count_tuple[2]:
            errors[e] += count_tuple[2][e]
    summary = f"Total {session_count} sessions and {transaction_count} transactions."
    logging.info(summary)
    if errors:
        logging.info("Total errors:")
        for e in errors:
            logging.info(f"{e}: {errors[e]}")
    else:
        logging.info("Total errors: 0")

    with open(f"{args.out_dir}/summary.txt", "w", encoding="ascii") as f:
        f.write(f"{summary}\n")


if __name__ == "__main__":
    sys.exit(main())
