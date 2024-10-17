# pthread trylock stats

bcc tool to observe the pthread trylock

# Usage

```
sudo ./trylock-stats.py -p `pgrep TS_MAIN` > trylock.log
```

# Requirements

This tool depends on [bcc](https://github.com/iovisor/bcc).

On RHEL, below packages are required.

- bcc
- bcc-tools
- python3-bcc
