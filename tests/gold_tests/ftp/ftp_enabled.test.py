
Test.Summary = '''
Test that FTP traffic is accepted by Trafficserver when enabled.
'''

ats = Test.MakeATSProcess("ts")
ats.Disk.records_config.update({
        'proxy.config.ftp_enabled': 1,
        'proxy.config.url_remap.remap_required': 0
    })
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET ftp://127.0.0.1:{0}/ HTTP/1.1\r\nHost: 127.0.0.1:{0}\r\n\r\n".format(server.Variables.Port), "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

server.addResponse("session.json", request_header, response_header)

test = Test.AddTestRun("FTP accepted by Trafficserver")
test.Processes.Default.Command = 'curl --proxy 127.0.0.1:{0} "ftp://127.0.0.1:{1}" --verbose'.format(ats.Variables.port, server.Variables.Port)
test.Processes.Default.ReturnCode=0
test.Processes.Default.Streams.stderr="gold/ftp_enabled.gold"
test.Processes.Default.StartBefore(ats, ready = When.PortOpen(ats.Variables.port))
test.Processes.Default.StartBefore(server, ready = When.PortOpen(server.Variables.Port))

