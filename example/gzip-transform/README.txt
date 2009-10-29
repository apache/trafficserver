Gzip / Gunzip plugins
---------------------

These plugins work only in conjunction and should not be used
separately to compress or uncompress data.
For instance a TS child runs gunzip and parent runs gzip.
Gzip compresses on one side and gunzip decompresses on the other.

Note that gzip plugin produces a compressed file slightly different
from what the gzip utility does (the header is different). 
Thus, file compressed by gzip utility can not be uncompressed by gunzip. 
 
