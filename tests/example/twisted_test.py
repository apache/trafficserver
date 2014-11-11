import sys
import unittest

from twisted.internet import reactor
from twisted.internet.defer import Deferred
from twisted.internet.protocol import Protocol
from twisted.web.client import Agent
from twisted.web.http_headers import Headers

sys.path = ['../framework'] + sys.path
import atstf

class ExampleTwistedTest(unittest.TestCase):
    def test_ats1(self):
        #
        # Read test case configuration from the same config file used to start ATS and origin processes.
        #

        conf = atstf.parse_config()

        ats1_conf = conf['processes']['ats1']

        hostname = ats1_conf['interfaces']['http']['hostname']
        port = ats1_conf['interfaces']['http']['port']

        origin_conf = conf['processes']['origin1']

        expected_status_code = origin_conf['actions']['GET']['/foo/bar']['status_code']
        chunk_size_bytes = origin_conf['actions']['GET']['/foo/bar']['chunk_size_bytes']
        num_chunks = origin_conf['actions']['GET']['/foo/bar']['num_chunks']
        expected_bytes_received = chunk_size_bytes * num_chunks

        #
        # Create a twisted HTTP client that will interact with ATS and save all details of the interaction.   We'll
        # later assert that the saved details are correct.   Ideally we'd just throw assertions into the client itself,
        # but sadly assertions are implemented with exceptions and twisted eats any exception raised by our callbacks.
        #
        # See https://twistedmatrix.com/documents/current/web/howto/client.html for details on writing HTTP clients
        #

        agent = Agent(reactor)

        request = agent.request('GET',  "http://%s:%d/foo/bar" % (hostname.encode("utf-8"), port),
                                Headers({'User-Agent': ['ExampleTwistedTest']}), None)

        response_handler = ResponseHandler(reactor)

        request.addCallback(response_handler.handle_response)
        request.addCallback(response_handler.handle_completion)
        request.addErrback(response_handler.handle_error)

        reactor.run()

        #
        # Now assert
        #

        self.assertEqual(expected_status_code, response_handler.get_status_code())
        self.assertEqual(expected_bytes_received, response_handler.get_body_handler().get_bytes_received())
        self.assertEqual("Response body fully received", \
                         response_handler.get_body_handler().get_reason().getErrorMessage())

class ResponseBodyHandler(Protocol):
    def __init__(self, deferred):
        self.__deferred = deferred
        self.__bytes_received = 0
        self.__reason = None

    def dataReceived(self, data):
        self.__bytes_received += len(data)

    def connectionLost(self, reason):
        #
        # This function name is highly misleading.  According to the web client docs:
        #
        # When the body has been completely delivered, the protocol's connectionLost method is called. It is important
        # to inspect the Failure passed to connectionLost . If the response body has been completely received, the
        # failure will wrap a twisted.web.client.ResponseDone exception. This indicates that it is known that all data
        # has been received. It is also possible for the failure to wrap a twisted.web.http.PotentialDataLoss exception:
        # this indicates that the server framed the response such that there is no way to know when the entire response
        # body has been received. Only HTTP/1.0 servers should behave this way. Finally, it is possible for the
        # exception to be of another type, indicating guaranteed data loss for some reason (a lost connection, a memory
        # error, etc).
        #
        self.__reason = reason
        self.__deferred.callback(None)

    def get_bytes_received(self):
        return self.__bytes_received

    def get_reason(self):
        return self.__reason

class ResponseHandler:
    def __init__(self, reactor):
        self.__reactor = reactor
        self.__deferred = Deferred()
        self.__body_handler = ResponseBodyHandler(self.__deferred)
        self.__error = None
        self.__status_code = None

    def handle_response(self, response):
        self.__status_code = response.code
        response.deliverBody(self.__body_handler)
        return self.__deferred

    def handle_completion(self):
        self.__reactor.stop()

    def handle_error(self, error):
        self.__error = error
        self.__reactor.stop()

    def get_body_handler(self):
        return self.__body_handler

    def get_error(self):
        return self.__error

    def get_status_code(self):
        return self.__status_code
