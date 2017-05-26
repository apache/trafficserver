import httplib
import unittest
import sys

sys.path = ['../framework'] + sys.path
import atstf

class ExampleHttpClientTest(unittest.TestCase):
    def test_get(self):
        #
        # Read test case configuration from the same config file used to start ATS and origin processes.
        #

        conf = atstf.parse_config()

        ats1_conf = conf['processes']['ats1']

        hostname = ats1_conf['interfaces']['http']['hostname']
        port = ats1_conf['interfaces']['http']['port']

        origin_conf = conf['processes']['origin1']

        expected_status_code = origin_conf['actions']['GET']['/chunked/down']['status_code']
        chunk_size_bytes = origin_conf['actions']['GET']['/chunked/down']['chunk_size_bytes']
        num_chunks = origin_conf['actions']['GET']['/chunked/down']['num_chunks']
        expected_bytes_received = chunk_size_bytes * num_chunks

        #
        # Send request to ATS and assert that the correct response is received
        #

        conn = httplib.HTTPConnection("%s:%d" % (hostname.encode("utf-8"), port))

        try:
            conn.request('GET', '/chunked/down')
            response = conn.getresponse()

            self.assertEqual(expected_status_code, response.status)

            response_body = response.read()

            self.assertEqual(expected_bytes_received, len(response_body))
        finally:
            conn.close()


