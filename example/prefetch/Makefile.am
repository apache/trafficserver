CC=@CC@
LD=@ld@
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: prefetch-plugin-eg1.so

prefetch-plugin-eg1.o: prefetch-plugin-eg1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o prefetch-plugin-eg1.o -c prefetch-plugin-eg1.c

prefetch-plugin-eg1.so: prefetch-plugin-eg1.o
	$(CC) $(LDFLAGS) -o prefetch-plugin-eg1.so prefetch-plugin-eg1.o

clean:
	rm -f prefetch-plugin-eg1.so prefetch-plugin-eg1.o
