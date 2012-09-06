require 'string'
require 'debug'
ts = require 'ts'
ts.hook = require 'ts.hook'

ts.debug('lua', string.format('loaded %s', debug.getinfo(1).source))

ts.hook.register(ts.hook.OS_DNS_HOOK,
    function(event, txn)
        ts.debug('lua', string.format('callback for event=%d', event))
        txn:continue()
    end
)
-- vim: set sw=4 ts=4 et :
