To test this plugin, add WebSocket.so to plugin.config, start
Traffic Server, then in a browser JavaScript console enter the
following:

  ws = new WebSocket('ws://some.host:8080/');
  ws.onmessage = function(e) { console.log(e.data); };
  ws.send('hello');

The host name 'some.host' must resolve to the server where Traffic
Server is running. You should get a response from the plugin.

It appears to be necessary that the host name be a valid DNS name on
the server where Traffic Server is running. If not, the WebSocket
connection will fail with an error of 502 "Cannot find server."
