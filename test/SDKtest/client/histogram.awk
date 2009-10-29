/^= X/{
  machine_id = $3;
  bucket_no = $4;
  count = $10;

  mapped_index = bucket_no * number_of_machines + machine_id;

  total_count += count;

  x_bucket_count[mapped_index] = count;
  x_bucket_sum[bucket_no] += count;
  if (x_bucket_sum[bucket_no] > max_bucket_sum) {
    max_bucket_sum = x_bucket_sum[bucket_no];
  }
  min = $6;
  max = $8;
  if (min == "_") min = 0;
  if (max == "_") max = 0;
  if (x_bucket_min[bucket_no] == 0) {
    x_bucket_min[bucket_no] = min;
  }
  if (x_bucket_max[bucket_no] == 0) {
    x_bucket_max[bucket_no] = max;
  }

  if (bucket_no > x_max_bucket) {
    x_max_bucket = bucket_no;
  }
}
END{
  nclients = split(client_names, clients, ",");
  if (x_max_bucket > 0) {
    if (excel == 1) {
      printf("bucket min,bucket max,");
      for (j=0; j < number_of_machines; j++) {
        printf "client%d,", j;
      }
      printf "sum\n";
    } else {
      printf "   bucket-range     ";
      for (j=0; j < number_of_machines; j++) {
        printf "client%2d ", j;
      }
      printf "|   sum of       inktogram\n";

      printf "     min : max      ";
      for (j=0; j < number_of_machines; j++) {
         printf "%8s ", substr(clients[j+1], 1, 8);
      }
      printf "|   clients    (stacked bar graph of client counts)\n";


      printf "___________________________________________________________________________";
      for (j=0; j < number_of_machines; j++) {
        printf "_________", j;
      }
      printf("\n");
    }
    running_sum = 0;
    found50 = -1;
    found90 = -1;
    for (i=0; i <= x_max_bucket; i++) {
      if (excel == 1) {
        if (x_bucket_min[i] == x_bucket_max[i]) {
          printf "%s,%.2lf,", "", x_bucket_max[i];
        } else if (x_bucket_max[i] == 0) {
          printf "%.2lf,%s,", x_bucket_min[i], "";
        } else {
          printf "%.2lf,%.2lf,", x_bucket_min[i], x_bucket_max[i];
        }
      } else {
        if (x_bucket_min[i] == x_bucket_max[i]) {
          printf "[%7s : %7.2lf] ", "", x_bucket_max[i];
        } else if (x_bucket_max[i] == 0) {
          printf "[%7.2lf : %7s] ", x_bucket_min[i], "";
        } else {
          printf "[%7.2lf : %7.2lf] ", x_bucket_min[i], x_bucket_max[i];
        }
      }
      sum = 0;
      for (j=0; j < number_of_machines; j++) {
         mapped_index = i * number_of_machines + j;
         count = x_bucket_count[mapped_index];
         if (excel == 1) {
           printf "%d", count;
         } else {
           printf "%8d", count;
         }
         if (excel == 1) {
           printf ",";
         } else {
           printf " ";
         }
         sum += count;
      }
      running_sum += sum;
      if ((found50 < 0) && (running_sum >= 0.50 *  total_count)) {
        found50 = i;
      }
      if ((found90 < 0) && (running_sum >= 0.90 *  total_count)) {
        found90 = i;
      }
      if (excel == 1) {
        printf "%d", sum;
      } else { 
        printf "| %8d  ", sum;
      }
      if (excel == 0) {
        screen = 40;
        for (j = 0; j < number_of_machines; j++) {
          mapped_index = i * number_of_machines + j;
          count = x_bucket_count[mapped_index];
          if (max_bucket_sum == 0) {
            bar = 0;
          } else {
            bar = (screen * count) / max_bucket_sum;
          }
          for (k = 0; k < bar; k++) {
            printf "%d", j;
          }
        }
      }
      printf "\n";
    }
    printf("Total number of data points = %d\n", total_count);
    if (x_bucket_max[found50] < x_bucket_min[found50]) {
      printf("Median value between:          %7.2lf and infinity\n", x_bucket_min[found50]);
    } else {
      printf("Median value between:          %7.2lf and %7.2lf\n", x_bucket_min[found50], x_bucket_max[found50]);
    }
    if (x_bucket_max[found90] < x_bucket_min[found90]) {
      printf("90th percentile value between: %7.2lf and infinity\n", x_bucket_min[found90]);
    } else {
      printf("90th percentile value between: %7.2lf and %7.2lf\n", x_bucket_min[found90], x_bucket_max[found90]);
    }
    printf("\n\n");
  }
}
