
#CPLUSPLUS=g++
CPLUSPLUS=CC

CPP=@CPP@
CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include
LIBINK_INC=../../../../../ts-libinktomi

default: INKHttpTxnCacheLookupCompleteStatusGet.so \
	INKHttpTxnCacheLookupStatusSet.so INKHttpTxnRedirectRequest.so \
	INKHttpTxnCacheLookupUrlGet.so	INKHttpTxnCacheLookupDo.so

INKHttpTxnCacheLookupCompleteStatusGet.so: \
		INKHttpTxnCacheLookupCompleteStatusGet.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnCacheLookupCompleteStatusGet.so INKHttpTxnCacheLookupCompleteStatusGet.o
	chmod ugo+rx INKHttpTxnCacheLookupCompleteStatusGet.so

INKHttpTxnCacheLookupCompleteStatusGet.o: $(API_INC)/InkAPI.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) \
		-o INKHttpTxnCacheLookupCompleteStatusGet.o \
		-c INKHttpTxnCacheLookupCompleteStatusGet.cc

INKHttpTxnCacheLookupStatusSet.so: INKHttpTxnCacheLookupStatusSet.o 
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnCacheLookupStatusSet.so \
					INKHttpTxnCacheLookupStatusSet.o
	chmod ugo+rx INKHttpTxnCacheLookupStatusSet.so

INKHttpTxnCacheLookupStatusSet.o: $(API_INC)/InkAPI.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) \
		-o INKHttpTxnCacheLookupStatusSet.o \
		-c INKHttpTxnCacheLookupStatusSet.cc

INKHttpTxnRedirectRequest.so: INKHttpTxnRedirectRequest.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnRedirectRequest.so \
					INKHttpTxnRedirectRequest.o
	chmod ugo+rx INKHttpTxnRedirectRequest.so

INKHttpTxnRedirectRequest.o: $(API_INC)/InkAPI.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) \
		-o INKHttpTxnRedirectRequest.o \
		-c INKHttpTxnRedirectRequest.cc

INKHttpTxnCacheLookupUrlGet.so: INKHttpTxnCacheLookupUrlGet.o 
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnCacheLookupUrlGet.so \
					INKHttpTxnCacheLookupUrlGet.o
	chmod ugo+rx INKHttpTxnCacheLookupUrlGet.so

INKHttpTxnCacheLookupUrlGet.o: $(API_INC)/InkAPI.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) \
		-o INKHttpTxnCacheLookupUrlGet.o \
		-c INKHttpTxnCacheLookupUrlGet.cc

INKHttpTxnCacheLookupDo.so:	INKHttpTxnCacheLookupDo.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnCacheLookupDo.so \
					INKHttpTxnCacheLookupDo.o	
	chmod ugo+rx INKHttpTxnCacheLookupDo.so

INKHttpTxnCacheLookupDo.o: $(API_INC)/InkAPI.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) \
		-o INKHttpTxnCacheLookupDo.o \
		-c INKHttpTxnCacheLookupDo.cc


clean:
	rm -f   INKHttpTxnCacheLookupCompleteStatusGet.o \
		INKHttpTxnCacheLookupCompleteStatusGet.so \
		INKHttpTxnCacheLookupStatusSet.o 	 \
		INKHttpTxnCacheLookupStatusSet.so 	\
		INKHttpTxnRedirectRequest.o		\
		INKHttpTxnRedirectRequest.so		\
		INKHttpTxnCacheLookupUrlGet.o		\
		INKHttpTxnCacheLookupUrlGet.so		\
		INKHttpTxnCacheLookupDo.o		\
		INKHttpTxnCacheLookupDo.so

