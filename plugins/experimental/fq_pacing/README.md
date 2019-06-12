fq_pacing Plugin
====

# Overview
This plugin rate limits individual TCP connections on a per-remap basis.

The pacing is accomplished by using `setsockopt(SO_MAX_PACING_RATE)` and the
Fair Queuing (see `man 8 tc-fq`) qdisc.

## Supported Platforms
* Linux
 * RedHat/Centos 7.2+
 * Debian 8x
 * Any Linux kernel >= 3.18

# Configuration Instructions (Linux)
1. Before activating this plugin, you must enable the fair queuing qdisc by setting `net.core.default_qdisc = fq` in /etc/sysctl.conf` and rebooting.
2. Confirm the qdisc is active by running `sysctl net.core.default_qdisc`
3. Specify the plugin name and rate (bytes per second) on a remap line in `remap.config` to activate:
```
map http://reverse-fqdn/ http://origin/ @plugin=fq_pacing.so @pparam=--rate=100000
```
