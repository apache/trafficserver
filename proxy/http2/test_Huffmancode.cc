#include "HuffmanCodec.h"
#include <stdlib.h>
#include <iostream>

using namespace std;

void
test()
{
  const int size = 1024;
  char *dst_start = (char *)malloc(size * 2);
  char string[size];
  for (int i = 0; i < size; i++) {
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

  free(dst_start);
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
