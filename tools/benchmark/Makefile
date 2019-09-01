test:
	$(MAKE) http
	$(MAKE) https
	$(MAKE) http2

# Step 1 - setup the test
setup:
	sudo /usr/local/bin/trafficserver stop
	sudo rm -f /usr/local/var/log/trafficserver/squid.blog*
	sudo /usr/local/bin/trafficserver start
	rm -f *.log perf.data*

# Step 2 - prime the cache
prime_http:
	curl `cat urls.http | xargs`
	curl `cat urls.http | xargs`

prime_https:
	curl -k `cat urls.https | xargs`
	curl -k `cat urls.https | xargs`

# Step 3 - start logging performance data
log_start:
	dstat 10 >& dstat.log &
	sudo perf record &
	sudo perf stat >& perf-stat.log &

# Step 4 - run the benchmark
bench_http:
	h2load --h1 -t 10 -D 60 -n 1000000 -c 1000 `cat urls.http | xargs` | tail -9 > h2load.log

bench_https:
	h2load --h1 -t 10 -D 60 -n 1000000 -c 1000 `cat urls.https | xargs` | tail -9 > h2load.log

bench_http2:
	h2load -t 10 -D 60 -n 1000000 -c 1000 `cat urls.https | xargs` | tail -9 > h2load.log

# Step 5 - stop loggging performance data
log_stop:
	killall python3
	sudo killall -s SIGINT perf
	sudo killall -s SIGINT perf
	sleep 5
	sudo perf report -sdso,symbol --stdio  -w10,20,50,20 | head -40 | tail -37 > perf-report.log

# Step 6 - make a report
report:
	echo '**http2load**' > report
	cat h2load.log >> report
	echo -e "\n**dstat**" >> report
	cat dstat.log >> report
	echo '**perf stat**' >> report
	cat perf-stat.log >> report
	echo '**perf report**' >> report
	cat perf-report.log >> report

# Define the tests and generate the report
http: setup prime_http log_start bench_http log_stop report
	mv report http_benchmark.report

https: setup prime_https log_start bench_https log_stop report
	mv report https_benchmark.report

http2: setup prime_https log_start bench_http2 log_stop report
	mv report http2_benchmark.report

clean:
	rm -f *.log perf.data* *report
