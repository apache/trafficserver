#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

#
# Sample input
# Client 1  Requests                                         = 320         count        sum
# Client 1  Cumulative rate                                  = 16.00       op/sec       sum
# Client 1  Cumulative Byte Throughput                       = 157185.73   Bps          sum
# Client 1  Bytes requested per req                          = 9755.87     byte         ave Requests
# Client 1  Average Round-trip                               = 8161.02     msec         ave Requests
# Client 1  Maximum Round-trip                               = 14509       msec         max
# Client 1  Minimum Round-trip                               = 7253        msec         min
# Client 1  Transactions with Round-trip above 1000          =  320        count        sum


BEGIN {
  metric_count = 0;
  ok = 1;
}
/^Client.*=/{

  machine_id = $2;
  if (machine_id >= number_of_machines) {
    printf "Aborting: client id (%d) >= number_of_machines (%d): '%s'\n", machine_id, number_of_machines, $0;
    ok = 0;
    exit(-1);
  }
# build up metric string

  metric = $3;
  for (i = 4; i <= NF; i++) {
    if ($i == "=") break;
    metric = metric " " $i;
  }
  if (i >= NF) {
    printf "Aborting: Couldn't find '=' sign in line '%s'\n", $0;
    ok = 0;
    exit(-1);
  }

  i++
  value = $i;

# find metric in current list and add if not already there

  for (j = 0; j < metric_count; j++) {
    if (metric == metrics[j]) break;
  }
  metric_index = j;
  if (j == metric_count) {  # new metric
    metric_index = metric_count++;
    metrics[metric_index] = metric;

# get unit, op, op_arg fields
    i++;
    unit = $i;
    i++;
    op = $i;
    i++;

# check combiner 
    if ((op != "min") && (op != "max") && (op != "sum") && (op != "ave")) {
      printf "Aborting: unknown combiner '%s' in line '%s'\n", op, $0;
      ok = 0;
      exit(-1);
    }

# get optional argument for combiner
    op_arg_str = $i;
    while (++i <= NF) {
      op_arg_str = op_arg_str " " $i;    
    }

# convert op_arg_str to metric index (make sure op_arg is a known metric)
    op_arg = -1;
    if (op_arg_str != "") {
      for (k = 0; k < metric_count; k++) {
	 if (metrics[k] == op_arg_str) op_arg = k;
      }
      if (op_arg < 0) {
	printf "Aborting: Unknown combiner argument '%s' in line '%s'\n", op_arg_str, $0;
	ok = 0;
	exit(-1);
      }
    }

# store metric, units, combiner for this metric
    units[metric_index] = unit;
    ops[metric_index] = op;
    op_args[metric_index] = op_arg;
  }

# compute index for storing fields

  mapped_index = metric_index * number_of_machines + machine_id;
  if (flags[mapped_index] == 1) {
    printf "Aborting: duplicate record '%s'\n", $0;
    ok = 0;
    exit -1;
  }
#
  values[mapped_index] = value;
  flags[mapped_index] = 1;

}
END {
  nclients = split(client_names, clients, ",");
  if (ok) {
    if (excel == 1) {
      printf "%s,", "Metric";
    } else {
      printf "%30s    ", "Metric";
    }
    for (i = 0; i < number_of_machines; i++) {
#      printf "Client %1d", i;
       printf "%8s", substr(clients[i+1], 1, 8);
      if (excel == 1) {
	printf ",";
      } else {
	printf " ";
      }
    }
    if (excel == 1) {
      printf "%s,%s,%s(%s)\n", "combined", "units", "combiner", "weight";
    } else {
      printf "| %9s | %8s %10s(%s)\n", "combined", "units", "combiner", "weight";
      printf "_\n";
    }
    for (j = 0; j < metric_count; j++) {
      if (excel == 1) {
        printf "%s,", metrics[j];
      } else {
        printf "%30s = ", metrics[j];
      }
      found = 0;
      for (i = 0; i < number_of_machines; i++) {
	mapped_index = j * number_of_machines + i;
	value = values[mapped_index];
	if (flags[mapped_index] == 1) {
	  if (excel == 1) {
  	    printf "%.0lf,", value;
          } else {
  	    printf "%9.0lf", value;
          }
	  if (ops[j] == "ave") {
	    mapped_index = op_args[j] * number_of_machines + i;
	    weight = values[mapped_index];
	  } else {
	    weight = 1;
	  }
	  if (found != 1) {
	    combined = value * weight;
	    found = 1;
	    total = weight;
	  } else {
	    if (ops[j] == "sum") {
	      combined += value;
	    } else if (ops[j] == "max") {
	      if (value > combined) combined = value;
	    } else if (ops[j] == "min") {
	      if (value < combined) combined = value;
	    } else if (ops[j] == "ave") {
	      combined += value * weight;
	      total += weight;
	    }
	  }
	} else {
	  if (excel == 1) {
	    printf "%s,", "****";
	  } else {
	    printf "%9s", "****";
	  }
	}
      }
      if (ops[j] == "ave") {
	if (total == 0) {
  	  combined = 0;
	} else {
  	  combined /= total;
	}
      }
      if (excel == 1) {
#	printf ",";
      } else {
        printf " | ";
      }

      if (excel == 1) {
        printf "%.0lf,%s,%s", combined, units[j], ops[j];
      } else {
        printf "%9.0lf | %8s %10s", combined, units[j], ops[j];
      }
      if (op_args[j] >= 0) {
	printf "(%s)", metrics[op_args[j]];
      }
      printf "\n";
    }
  }
}
