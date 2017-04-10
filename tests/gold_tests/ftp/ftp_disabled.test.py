
Test.Summary = '''
Test that FTP traffic is rejected by Trafficserver when not enabled.
'''

ats = Test.MakeATSProcess("ts")
ats.Disk.records_config.update({
        'proxy.config.ftp_enabled': 0,
        'proxy.config.url_remap.remap_required': 0
    })

test = Test.AddTestRun("FTP rejected by Trafficserver")
test.Processes.Default.Command = 'curl --proxy 127.0.0.1:{0} "ftp://127.0.0.1" --verbose'.format(ats.Variables.port)
test.Processes.Default.ReturnCode=0
test.Processes.Default.Streams.stderr="gold/ftp_disabled.gold"
test.Processes.Default.StartBefore(ats, ready = When.PortOpen(ats.Variables.port))

