API_HDR = ../include/InkAPI.h

default: \
	add-header/add-header.so \
	append-transform/append-transform.so \
	basic-auth/basic-auth.so \
	blacklist-1/blacklist-1.so \
	bnull-transform/bnull-transform.so \
	hello/hello.so \
	null-transform/null-transform.so \
	output-header/output-header.so \
	protocol/protocol.so \
	redirect-1/redirect-1.so \
	server-transform/server-transform.so \
	session-1/session-1.so \
	thread-pool/psi.so

add-header/add-header.so: add-header/add-header.c $(API_HDR)
	(cd add-header; $(MAKE))

append-transform/append-transform.so: append-transform/append-transform.c $(API_HDR)
	(cd append-transform; $(MAKE))

basic-auth/basic-auth.so: basic-auth/basic-auth.c $(API_HDR)
	(cd basic-auth; $(MAKE))

blacklist-1/blacklist-1.so: blacklist-1/blacklist-1.c $(API_HDR)
	(cd blacklist-1; $(MAKE))

bnull-transform/bnull-transform.so: bnull-transform/bnull-transform.c $(API_HDR)
	(cd bnull-transform; $(MAKE))

hello/hello.so: hello/hello.c $(API_HDR)
	(cd hello; $(MAKE))

null-transform/null-transform.so: null-transform/null-transform.c $(API_HDR)
	(cd null-transform; $(MAKE))

output-header/output-header.so: output-header/output-header.c $(API_HDR)
	(cd output-header; $(MAKE))

protocol/protocol.so: protocol/Protocol.c protocol/TxnSM.c $(API_HDR)
	(cd protocol; $(MAKE))

redirect-1/redirect-1.so: redirect-1/redirect-1.c $(API_HDR)
	(cd redirect-1; $(MAKE))

server-transform/server-transform.so: server-transform/server-transform.c $(API_HDR)
	(cd server-transform; $(MAKE))

session-1/session-1.so: session-1/session-1.c $(API_HDR)
	(cd session-1; $(MAKE))

thread-pool/psi.so: thread-pool/psi.c thread-pool/thread.c thread-pool/thread.h $(API_HDR)
	(cd thread-pool; $(MAKE))

clean:
	rm -f */*.so */*.o
