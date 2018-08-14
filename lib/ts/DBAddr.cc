#include "DBAddr.h"

// thread safe map: sockaddr -> Extendible
static const int DBAddr_partitions = 64;
DBAddr::TableType DBAddr::map(DBAddr_partitions);

// thread safe map: sockaddrport -> Extendible
static const int DBAddrPort_partitions = 64;
DBAddrPort::TableType DBAddrPort::map(DBAddrPort_partitions);
