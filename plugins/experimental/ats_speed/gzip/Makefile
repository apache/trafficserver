TSXS?=tsxs

all:
	$(TSXS) -o gzip.so -v -C *.cc
install: 
	$(TSXS) -v -i -o gzip.so
clean:
	rm -f *.lo *.so

