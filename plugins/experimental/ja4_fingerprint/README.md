# ATS (Apache Traffic Server) JA4 Fingerprint Plugin

## General Information

The JA4 algorithm designed by John Althouse is the successor to JA3. A JA4 fingerprint has three sections, delimited by underscores, called a, b, and c. The a section contains basic, un-hashed information about the client hello, such as the version and most preferred ALPN. The b section is a hash of the ciphers, and the c section is a hash of the extensions. The algorithm is licensed under the BSD 3-Clause license.

The technical specification of the algorithm is available [here](https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md).

### Main Differences from JA3 Algorithm

* The ciphers and extensions are sorted before hashing.
* Information about the SNI and ALPN is included.
* The fingerprint can indicate that QUIC is in use.

## Changes from JA3 Plugin

* The behavior is as if the ja3\_fingerprint option `--modify-incoming` were specified. No other mode is supported.
* The raw (un-hashed) cipher and extension lists are not logged.
* There is no way to turn the log off.
* There is no remap variant of the plugin.

These changes were made to simplify the plugin as much as possible. The missing features are useful and may be implemented in the future.

## Logging and Debugging

To get debug information in the traffic log, enable the debug tag `ja4_fingerprint`.
