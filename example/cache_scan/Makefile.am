CC=cc
LD=ld
CFLAGS= -g -D_REENTRANT=1 -m32
LDFLAGS= -g -shared -m32

TS_BASE=../../../..
API_INC= -I. -I$(TS_BASE)/proxy/api/include
TARGET=cache_scan

default: $(TARGET).so

$(TARGET).o: $(TARGET).cc
	$(CXX) $(CFLAGS) $(API_INC) -o $(TARGET).o -c $(TARGET).cc

$(TARGET).so: $(TARGET).o
	$(CXX) $(LDFLAGS) -o $(TARGET).so $(TARGET).o

clean:
	rm -f $(TARGET).so $(TARGET).o
