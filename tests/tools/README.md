
These tools are meant to become it own repository in the future. They are here at the moment to help accelerate progress at getting everything working.

Note these Tools require python 3.4 or better.

# Traffic-Replay

Replay client to replay session logs.

Usage:
python3.5 traffic-replay/ -type <nossl|ssl|h2|random> -log_dir /path/to/log -v

Session Log format (in JSON):

 {"version": "0.1",
  "txns": [
        {"request": {"headers": "POST ……\r\n\r\n", "timestamp": "..", "body": ".."},
        "response": {"headers": "HTTP/1.1..\r\n\r\n", "timestamp": "..", "body": ".."},
        "uuid": "1"},
        {"request": {"headers": "POST ..….\r\n\r\n", "timestamp": "..", "body": ".."},
        "response": {"headers": "HTTP/1.1..\r\nr\n", "timestamp": "..", "body": ".."},
        "uuid": "2"}
  ],
  "timestamp": "....",
  "encoding": "...."}
  Configuration: The configuration required to run traffic-replay can be specified in traffic-replay/Config.py

# TCP Client

A command line interface that sends and receives bytes over TCP, to aid in repeatable testing.

Run `python3.5 tcp_client.py -h` to see example usage.
