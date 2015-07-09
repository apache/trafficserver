#include "HuffmanCodec.h"
#include <stdlib.h>
#include <iostream>

using namespace std;

void
test()
{
  char *dst_start = (char *)malloc(1024 * 2);
  char string[1024];
  for (int i = 0; i < 1024; i++) {
    long num = lrand48();
    string[i] = (char)num;
  }
  const uint8_t *src = (const uint8_t *)string;
  uint32_t src_len = sizeof(string);

  int bytes = huffman_decode(dst_start, src, src_len);

  cout << "bytes: " << bytes << endl;
  for (int i = 0; i < bytes; i++) {
    cout << i << " " << (int)dst_start[i] << " " << dst_start[i] << endl;
  }
}

int
main()
{
  hpack_huffman_init();

  for (int i = 0; i < 100; i++) {
    test();
  }

  hpack_huffman_fin();
  return 0;
}
