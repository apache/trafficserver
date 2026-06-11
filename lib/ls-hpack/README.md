# LiteSpeed HPACK in ATS

Originally, ATS included a home-grown HPACK encoder and decoder for HTTP/2
header compression, featuring a custom Huffman coding implementation. As HTTP/2
adoption grew and later with HTTP/3 evolving to QPACK, the need for
high-performance header decompression became critical. Our original Huffman
decoder was simple but performed bit-by-bit processing, which proved to be a
performance bottleneck under heavy traffic.

To address this, we replaced the ATS home-grown version with the open-source,
MIT-licensed LiteSpeed HPACK library. LiteSpeed's implementation trades static
memory (a fixed-size, compile-time-allocated lookup table of 64K entries) for
drastically reduced computation. In `perf top` testing with production traffic,
the CPU usage of the `huffman_decode` function dropped from about 1% to
effectively unmeasurable levels, delivering a significant performance
improvement.

This integration maintains full HPACK compliance for HTTP/2 and seamlessly
supports header compression performance for HTTP/3 (QPACK), leveraging the same
Huffman decoding acceleration.

# LiteSpeed Version

The current implementation pulled into ATS is based upon what is currently the
latest version,
[v2.3.5](https://github.com/litespeedtech/ls-hpack/releases/tag/v2.3.5).

# ATS Modifications

The code is kept as close to upstream as practical, with one deliberate
behavioral divergence that any future re-sync must preserve:

- `lshpack_dec_huff_decode` (the fast decoder) rejects Huffman padding of 8 or
  more bits, as required by RFC 7541 section 5.2. Upstream's fast decoder
  accepts such padding when it follows the final symbol near the end of the
  input; the 4-bit FSM decoder (`lshpack_dec_huff_decode_full`) has always
  rejected it. See the commented tail check in `lshpack.cc` and the
  `decode_overlong_padding` test in
  `src/proxy/hdrs/unit_tests/test_Huffmancode.cc`, which fails if the check is
  dropped.
