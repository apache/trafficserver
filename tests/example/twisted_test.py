import sys

from twisted.trial import unittest
from twisted.internet import reactor
from twisted.internet.defer import Deferred
from twisted.internet.protocol import Protocol
from twisted.web.client import Agent
from twisted.web.http_headers import Headers
from twisted.python import log
from twisted.internet.defer import succeed
from twisted.web.iweb import IBodyProducer
from zope.interface import implements

sys.path = ['../framework'] + sys.path
import atstf

class ExampleTwistedTest(unittest.TestCase):
    def setUp(self):
        log.startLogging(sys.stdout)

    def test_get(self):
        #
        # Read test case configuration from the same config file used to start ATS and origin processes.
        #

        conf = atstf.parse_config()

        method = 'GET'
        hostname = conf['processes']['ats1']['interfaces']['http']['hostname']
        port = conf['processes']['ats1']['interfaces']['http']['port']
        abs_path = '/chunked/down'
        headers = Headers({'User-Agent': ['ExampleTwistedTest']})
        body_producer = None

        agent = Agent(reactor)

        request = agent.request(method,  "http://%s:%d%s" % (hostname.encode("utf-8"), port, abs_path), headers,
                                body_producer)

        done = Deferred()

        response_handler = ResponseHandler(self, done, conf['processes']['origin1']['actions'][method][abs_path])

        request.addCallback(response_handler.handle_response)
        request.addErrback(response_handler.handle_error)

        return done # This is a deferred.  The test case will not complete until this completes

    def test_post(self):
        #
        # Read test case configuration from the same config file used to start ATS and origin processes.
        #

        conf = atstf.parse_config()

        method = 'POST'
        hostname = conf['processes']['ats1']['interfaces']['http']['hostname']
        port = conf['processes']['ats1']['interfaces']['http']['port']
        abs_path = '/nonchunked/up'
        headers = Headers({'User-Agent': ['ExampleTwistedTest'], "Content-Type": ["text/plain"]})
        body_producer = NonChunkedBodyProducer(chr(42) * 1024)

        agent = Agent(reactor)

        request = agent.request(method,  "http://%s:%d%s" % (hostname.encode("utf-8"), port, abs_path), headers,
                                body_producer)

        done = Deferred()

        response_handler = ResponseHandler(self, done, conf['processes']['origin1']['actions'][method][abs_path])

        request.addCallback(response_handler.handle_response)
        request.addErrback(response_handler.handle_error)

        return done # This is a deferred.  The test case will not complete until this completes

class ResponseBodyHandler(Protocol):
    def __init__(self, test_case, done, expected_bytes_received):
        self.__test_case = test_case
        self.__done = done
        self.__expected_bytes_received = expected_bytes_received
        self.__bytes_received = 0

    def dataReceived(self, data):
        self.__bytes_received += len(data)
        return self.__done

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
        try:
            self.__test_case.assertEqual(self.__expected_bytes_received, self.__bytes_received)
            self.__test_case.assertEqual("Response body fully received", reason.getErrorMessage())
            self.__done.callback(None)
        except Exception, e:
            self.__done.errback(e)
            raise e

class ResponseHandler:
    def __init__(self, test_case, done, action_conf):
        self.__test_case = test_case
        self.__done = done
        self.__expected_status_code = action_conf['status_code']
        self.__expected_bytes_received = action_conf['chunk_size_bytes'] * action_conf['num_chunks']

    def handle_response(self, response):
        try:
            self.__test_case.assertEqual(self.__expected_status_code, response.code)
            response.deliverBody(ResponseBodyHandler(self.__test_case, self.__done, self.__expected_bytes_received))
            return self.__done
        except Exception, e:
            self.__done.errback(e)
            raise e

    def handle_error(self, error):
        self.__test_case.fail("Error processing response: %s" % error)

class NonChunkedBodyProducer(object):
    implements(IBodyProducer)

    def __init__(self, body):
        self.body = body
        self.length = len(body)

    def startProducing(self, consumer):
        consumer.write(self.body)
        return succeed(None)

    def pauseProducing(self):
        pass

    def stopProducing(self):
        pass
