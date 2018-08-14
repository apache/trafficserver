#include "DBHost.h"

/// internal lookup table

// thread safe table: host_name -> host_rec
static const int DBHost_partitions = 64;
DBHost::TableType DBHost::table(DBHost_partitions);
