'''
Test redirection behavior to invalid addresses
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

from enum import Enum
import re
import os
import socket
Test.Summary = '''
Test redirection behavior to invalid addresses
'''

Test.ContinueOnFail = False

Test.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, 'tcp_client.py'))

dns = Test.MakeDNServer('dns')
# This record is used in each test case to get the initial redirect response from the origin that we will handle.
dnsRecords = {'iwillredirect.test': ['127.0.0.1']}

host = socket.gethostname()
ipv4addrs = set()
try:
    ipv4addrs = set([ip for
                     (family, _, _, _, (ip, *_)) in
                     socket.getaddrinfo(host, port=None) if
                     socket.AF_INET == family])
except socket.gaierror:
    pass

ipv6addrs = set()
try:
    ipv6addrs = set(["[{0}]".format(ip.split('%')[0]) for
                     (family, _, _, _, (ip, *_)) in
                     socket.getaddrinfo(host, port=None) if
                     socket.AF_INET6 == family and 'fe80' != ip[0:4]])  # Skip link-local addresses.
except socket.gaierror:
    pass

origin = Test.MakeOriginServer('origin', ip='0.0.0.0')
ArbitraryTimestamp = '12345678'

# This is for cases when the content is actually fetched from the invalid address.
request_header = {
    'headers': ('GET / HTTP/1.1\r\n'
                'Host: *\r\n\r\n'),
    'timestamp': ArbitraryTimestamp,
    'body': ''}
response_header = {
    'headers': ('HTTP/1.1 204 No Content\r\n'
                'Connection: close\r\n\r\n'),
    'timestamp': ArbitraryTimestamp,
    'body': ''}
origin.addResponse('sessionfile.log', request_header, response_header)

# Map scenarios to trafficserver processes.
trafficservers = {}

data_dirname = 'generated_test_data'
data_path = os.path.join(Test.TestDirectory, data_dirname)
os.makedirs(data_path, exist_ok=True)


def normalizeForAutest(value):
    '''
    autest uses "test run" names to build file and directory names, so we must transform them in case there are incompatible or
    annoying characters.
    This means we can also use them in URLs.
    '''
    if not value:
        return None
    return re.sub(r'[^a-z0-9-]', '_', value, flags=re.I)


def makeTestCase(redirectTarget, expectedAction, scenario):
    '''
    Helper method that creates a "meta-test" from which autest generates a test case.

    :param redirectTarget: The target address of a redirect from origin to be handled.
    :param scenario: Defines the ACL to configure and the addresses to test.
    '''

    config = ','.join(':'.join(t) for t in sorted((addr.name.lower(), action.name.lower()) for (addr, action) in scenario.items()))

    normRedirectTarget = normalizeForAutest(redirectTarget)
    normConfig = normalizeForAutest(config)
    tr = Test.AddTestRun('With_Config_{0}_Redirect_to_{1}'.format(normConfig, normRedirectTarget))

    if trafficservers:
        tr.StillRunningAfter = origin
        tr.StillRunningAfter = dns
    else:
        tr.Processes.Default.StartBefore(origin)
        tr.Processes.Default.StartBefore(dns)

    if config not in trafficservers:
        trafficservers[config] = Test.MakeATSProcess('ts_{0}'.format(normConfig))
        trafficservers[config].Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|dns|redirect',
            'proxy.config.http.number_of_redirections': 1,
            'proxy.config.http.cache.http': 0,
            'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
            'proxy.config.dns.resolv_conf': 'NULL',
            'proxy.config.url_remap.remap_required': 0,
            'proxy.config.http.redirect.actions': config,
            'proxy.config.http.connect_attempts_timeout': 5,
            'proxy.config.http.connect_attempts_max_retries': 0,
        })
        tr.Processes.Default.StartBefore(trafficservers[config])
    else:
        tr.StillRunningAfter = trafficservers[config]

    testDomain = 'testdomain{0}.test'.format(normRedirectTarget)
    # The micro DNS server can't tell us whether it has a record of the domain already, so we use a dictionary to avoid duplicates.
    # We remove any surrounding brackets that are common to IPv6 addresses.
    if redirectTarget:
        dnsRecords[testDomain] = [redirectTarget.strip('[]')]

    # A GET request parameterized on the config and on the target.
    request_header = {
        'headers': ('GET /redirect?config={0}&target={1} HTTP/1.1\r\n'
                    'Host: *\r\n\r\n').
        format(normConfig, normRedirectTarget),
        'timestamp': ArbitraryTimestamp,
        'body': ''}
    # Returns a redirect to the test domain for the given target & the port number for the TS of the given config.
    response_header = {
        'headers': ('HTTP/1.1 307 Temporary Redirect\r\n'
                    'Location: http://{0}:{1}/\r\n'
                    'Connection: close\r\n\r\n').
        format(testDomain, origin.Variables.Port),
        'timestamp': ArbitraryTimestamp,
        'body': ''}
    origin.addResponse('sessionfile.log', request_header, response_header)

    # Generate the request data file.
    with open(os.path.join(data_path, tr.Name), 'w') as f:
        f.write(('GET /redirect?config={0}&target={1} HTTP/1.1\r\n'
                 'Host: iwillredirect.test:{2}\r\n\r\n').
                format(normConfig, normRedirectTarget, origin.Variables.Port))
    # Set the command with the appropriate URL.
    tr.Processes.Default.Command = "bash -o pipefail -c 'python3 tcp_client.py 127.0.0.1 {0} {1} | head -n 1'".\
        format(trafficservers[config].Variables.port, os.path.join(data_dirname, tr.Name))
    tr.Processes.Default.ReturnCode = 0
    # Generate and set the 'gold file' to check stdout
    goldFilePath = os.path.join(data_path, '{0}.gold'.format(tr.Name))
    with open(goldFilePath, 'w') as f:
        f.write(expectedAction.value['expectedStatusLine'])
    tr.Processes.Default.Streams.stdout = goldFilePath


class AddressE(Enum):
    '''
    Classes of addresses are mapped to example addresses.
    '''
    Private = ('10.0.0.1', '[fc00::1]')
    Loopback = (['127.1.2.3'])  # [::1] is ommitted here because it is likely overwritten by Self, and there are no others in IPv6.
    Multicast = ('224.1.2.3', '[ff42::]')
    Linklocal = ('169.254.0.1', '[fe80::]')
    Routable = ('72.30.35.10', '[2001:4998:58:1836::10]')  # Do not Follow redirects to these in an automated test.
    Self = ipv4addrs | ipv6addrs  # Addresses of this host.
    Default = None  # All addresses apply, nothing in particular to test.


class ActionE(Enum):
    # Title case because 'return' is a Python keyword.
    Return = {'config': 'return', 'expectedStatusLine': 'HTTP/1.1 307 Temporary Redirect\r\n'}
    Reject = {'config': 'reject', 'expectedStatusLine': 'HTTP/1.1 403 Forbidden\r\n'}
    Follow = {'config': 'follow', 'expectedStatusLine': 'HTTP/1.1 204 No Content\r\n'}

    # Added to test failure modes.
    Break = {'expectedStatusLine': 'HTTP/1.1 502 Cannot find server.\r\n'}


scenarios = [
    {
        # Follow to loopback, but alternately reject/return others.
        AddressE.Private: ActionE.Reject,
        AddressE.Loopback: ActionE.Follow,
        AddressE.Multicast: ActionE.Reject,
        AddressE.Linklocal: ActionE.Return,
        AddressE.Routable: ActionE.Reject,
        AddressE.Self: ActionE.Return,
        AddressE.Default: ActionE.Reject,
    },

    {
        # Follow to loopback, but alternately reject/return others, flipped from the previous scenario.
        AddressE.Private: ActionE.Return,
        AddressE.Loopback: ActionE.Follow,
        AddressE.Multicast: ActionE.Return,
        AddressE.Linklocal: ActionE.Reject,
        AddressE.Routable: ActionE.Return,
        AddressE.Self: ActionE.Reject,
        AddressE.Default: ActionE.Return,
    },

    {
        # Return loopback, but reject everything else.
        AddressE.Loopback: ActionE.Return,
        AddressE.Default: ActionE.Reject,
    },

    {
        # Reject loopback, but return everything else.
        AddressE.Loopback: ActionE.Reject,
        AddressE.Default: ActionE.Return,
    },

    {
        # Return everything.
        AddressE.Default: ActionE.Return,
    },
]

for scenario in scenarios:
    for addressClass in AddressE:
        if not addressClass.value:
            # Default has no particular addresses to test.
            continue
        for address in addressClass.value:
            expectedAction = scenario[addressClass] if addressClass in scenario else scenario[AddressE.Default]
            makeTestCase(redirectTarget=address, expectedAction=expectedAction, scenario=scenario)

    # Test redirects to names that cannot be resolved.
    makeTestCase(redirectTarget=None, expectedAction=ActionE.Break, scenario=scenario)

dns.addRecords(records=dnsRecords)

# Make sure this runs only after local files have been created.
Test.Setup.Copy(data_path)
