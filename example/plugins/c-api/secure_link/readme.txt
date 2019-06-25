The secure_link plugin allows Traffic Server to protect resources by verifying
checksum value passed in the request and computed for the request.
Checksum is the md5 digest of concatenation of several params:
  [secret] + [Client IP Address] + [HTTP Query Path] + [expire]
The plugin can be used in the plugins chain in remap.config and it expects two @pparams:
1. secret - the word, which is known only to the application that generated link
   and Traffic Server.
2. policy - if set to 'strict' and checksums not match or expire value
   lower than current time the client will receive 403 Forbidden response.
   Used for debugging.

For example request
  http://foo.example.com/d41d8cd98f00b204e9800998ecf8427e/52b9ed11/path/to/secret/document.pdf
may be remapped to
  http://bar.example.com/path/to/secret/document.pdf?st=d41d8cd98f00b204e9800998ecf8427e&ex=52b9ed11
and then passed to secure_link plugin, which compare 'st' and 'ex' GET params
with computed md5 checksum and current time respectively.
If check passed the plugin removes 'st' and 'ex' GET params and passes down
the processing chain;
