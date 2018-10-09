# coding=utf-8

#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import datetime
import sys
import time
import threading
import traceback
import socketserver
import argparse
import codecs
import json
from dnslib import *

sys.path.append(
    os.path.normpath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            '..'
        )
    )
)

import lib.IPConstants as IPConstants

TTL = 60 * 5  # completely arbitrary TTL value
round_robin = False
default_records = list()
records = dict()

class DomainName(str):
    def __getattr__(self, item):
        return DomainName(item + '.' + self)


class BaseRequestHandler(socketserver.BaseRequestHandler):

    def get_data(self):
        raise NotImplementedError

    def send_data(self, data):
        raise NotImplementedError

    def handle(self):
        now = datetime.datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S.%f')
        print("\n\n%s request %s (%s %s):" % (self.__class__.__name__[:3], now, self.client_address[0],
                                              self.client_address[1]))
        try:
            data = self.get_data()
            self.send_data(dns_response(data))
        except Exception:
            traceback.print_exc(file=sys.stderr)


class TCPRequestHandler(BaseRequestHandler):

    def get_data(self):
        data = self.request.recv(8192).strip()
        sz = int(codecs.encode(data[:2], 'hex'), 16)
        if sz < len(data) - 2:
            raise Exception("Wrong size of TCP packet")
        elif sz > len(data) - 2:
            raise Exception("Too big TCP packet")
        return data[2:]

    def send_data(self, data):
        sz = codecs.decode(hex(len(data))[2:].zfill(4), 'hex')
        return self.request.sendall(sz + data)


class UDPRequestHandler(BaseRequestHandler):

    def get_data(self):
        return self.request[0].strip()

    def send_data(self, data):
        return self.request[1].sendto(data, self.client_address)


def build_domain_mappings(path):
    with open(path) as f:
        zone_file = json.load(f)

    for domain in zone_file['mappings']:
        for d in iter(domain.keys()):
            # this loop only runs once, kind of a hack to access the only key in the dict
            domain_name = DomainName(d)
            print("Domain name:", domain_name)
            # we can test using python's built-in ipaddress module, but this should suffice
            records[domain_name] = [A(x) if ":" not in x else AAAA(x) for x in domain[domain_name]]
            print(records[domain_name])

    if 'otherwise' in zone_file:
        default_records.extend([A(d) if ":" not in d else AAAA(d) for d in zone_file['otherwise']])


def add_authoritative_records(reply, domain):
    # ns1 and ns1 are hardcoded in, change if necessary
    reply.add_auth(RR(rname=domain, rtype=QTYPE.NS, rclass=1, ttl=TTL, rdata=NS(domain.ns1)))
    reply.add_auth(RR(rname=domain, rtype=QTYPE.NS, rclass=1, ttl=TTL, rdata=NS(domain.ns2)))


def dns_response(data):
    ''' dns_response takes in the raw bytes from the socket and does all the logic behind what
        RRs get returned as the response '''
    global default_records, records, TTL, round_robin

    request = DNSRecord.parse(data)
    print(request)

    reply = DNSRecord(DNSHeader(id=request.header.id, qr=1, aa=1, ra=1), q=request.q)
    qname = request.q.qname
    qn = str(qname)
    qtype = request.q.qtype
    qt = QTYPE[qtype]
    found_specific = False

    # first look for a specific mapping
    for domain, rrs in records.items():
        if domain == qn or qn.endswith('.' + domain):
            # we are the authoritative name server for this domain and all subdomains
            for rdata in rrs:
                # only include requested record types (ie. A, MX, etc)
                rqt = rdata.__class__.__name__
                if qt in ['*', rqt]:
                    found_specific = True
                    reply.add_answer(RR(rname=qname, rtype=getattr(QTYPE, str(rqt)), rclass=1, ttl=TTL, rdata=rdata))

            # rotate the A entries if round robin is on
            if round_robin:
                a_records = [x for x in rrs if type(x) == A]
                records[domain] = a_records[1:] + a_records[:1]  # rotate list
            break

    # else if a specific mapping is not found, return default A-records
    if not found_specific:
        for a in default_records:
            found_specific = True
            reply.add_answer(RR(rname=qname, rtype=QTYPE.A, rclass=1, ttl=TTL, rdata=a))

        if round_robin:
            default_records = default_records[1:] + default_records[:1]

    if not found_specific:
        reply.header.set_rcode(3)

    print("---- Reply: ----\n", reply)
    return reply.pack()


if __name__ == '__main__':
    # handle cmd line args
    parser = argparse.ArgumentParser()
    parser.add_argument("ip", type=str, help="Interface")
    parser.add_argument("port", type=int, help="port uDNS should listen on")
    parser.add_argument("zone_file", help="path to zone file")
    parser.add_argument("--rr", action='store_true',
                        help='round robin load balances if multiple IP addresses are present for 1 domain')
    args = parser.parse_args()

    if IPConstants.isIPv6(args.ip):
        #  *UDPServer derives from TCPServer, so setting one will affect the other
        socketserver.TCPServer.address_family = socket.AF_INET6

    # exit(1)

    if args.rr:
        round_robin = True
    build_domain_mappings(args.zone_file)

    ipaddr = IPConstants.getIP(args.ip)

    servers = [
        socketserver.ThreadingUDPServer((ipaddr, args.port), UDPRequestHandler),
        socketserver.ThreadingTCPServer((ipaddr, args.port), TCPRequestHandler),
    ]

    print("Starting DNS on address {0} port {1}...".format(ipaddr, args.port))
    for s in servers:
        thread = threading.Thread(target=s.serve_forever)  # that thread will start one more thread for each request
        thread.daemon = True  # exit the server thread when the main thread terminates
        thread.start()

    try:
        while 1:
            time.sleep(1)
            sys.stderr.flush()
            sys.stdout.flush()

    except KeyboardInterrupt:
        print("Got SigINT")
        # pass
    finally:
        for s in servers:
            s.shutdown()
