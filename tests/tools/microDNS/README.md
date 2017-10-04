uDNS
=====

uDNS is a small DNS server that takes in a pre-defined set of domain names and the IPs that each domain name maps to. The mappings should be inputted with a JSON file in a format described below.

uDNS runs on localhost and serves whatever port is specified in the command line arguments. uDNS serves both UDP and TCP connections.

If uDNS does not find the requested domain in the explicitly mapped section of the JSON, uDNS will respond with the IPs given in the `otherwise` section of the JSON. The `otherwise` section is mandatory.


JSON format
------
```json
{
  "mappings": [
    {"domain1": ["ip1", "ip2", "etc"]},
    {"domain2": ["ip3", "ip4", "etc"]},
    {"domain3": ["ip5"]},
  ],

  "otherwise": ["defaultip1", "defaultip2", "etc"]
}
```

An example can be found in `sample_zonefile.json`


Caveat
------
You should not include any two records like this: `host1.example.com` and `example.com`

A DNS request for `host1.example.com` could return the A-record associated with `host1.example.com` or `example.com`, depending on your luck.


Running
------
`python3 uDNS.py ip_addr port zone_file [--rr]`

For a detailed description of flags, see `python3 uDNS.py -h`


Use with Apache Traffic Server
------
1. In `records.config`, add configuration lines: `CONFIG proxy.config.dns.nameservers STRING ip_address:PORT` and `CONFIG proxy.config.dns.round_robin_nameservers INT 0`, where `PORT` is whatever port you want uDNS to serve on.
2. Run uDNS on `Ip_addr`:`PORT`
3. Now all domains mapped in the uDNS JSON config file should be mapped by ATS as well
