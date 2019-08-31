test: http https http2

http:
	sudo /usr/local/bin/trafficserver stop
	sudo rm -f /usr/local/var/log/trafficserver/squid.blog
	sudo /usr/local/bin/trafficserver start
	curl `cat urls.http | xargs`
	dstat 10 >& dstat.log &
	sudo perf record &
	sudo perf stat >& perf-stat.log &
	h2load --h1 -t 10 -D 60 -n 1000000 -c 1000 `cat urls.http | xargs` | tail -9 > h2load.log
	killall python3
	sudo killall -s SIGINT perf
	sudo killall -s SIGINT perf
	sleep 5
	sudo perf report -sdso,symbol --stdio  -w10,20,40,20 | head -40 | tail -37 > perf-report.log
	cat h2load.log dstat.log perf-stat.log perf-report.log > http_benchmark.log

https:
	sudo /usr/local/bin/trafficserver stop
	sudo rm -f /usr/local/var/log/trafficserver/squid.blog
	sudo /usr/local/bin/trafficserver start
	curl `cat urls.http | xargs`
	dstat 10 >& dstat.log &
	sudo perf record &
	sudo perf stat >& perf-stat.log &
	h2load --h1 -t 10 -D 60 -n 1000000 -c 1000 `cat urls.https | xargs` | tail -9 > h2load.log
	killall python3
	sudo killall -s SIGINT perf
	sudo killall -s SIGINT perf
	sleep 5
	sudo perf report -sdso,symbol --stdio  -w10,20,40,20 | head -40 | tail -37 > perf-report.log
	cat h2load.log dstat.log perf-stat.log perf-report.log > https_benchmark.log

http2:
	sudo /usr/local/bin/trafficserver stop
	sudo rm -f /usr/local/var/log/trafficserver/squid.blog
	sudo /usr/local/bin/trafficserver start
	curl `cat urls.http | xargs`
	dstat 10 >& dstat.log &
	sudo perf record &
	sudo perf stat >& perf-stat.log &
	h2load -t 10 -D 60 -n 1000000 -c 1000 `cat urls.https | xargs` | tail -9 > h2load.log
	killall python3
	sudo killall -s SIGINT perf
	sudo killall -s SIGINT perf
	sleep 5
	sudo perf report -sdso,symbol --stdio  -w10,20,40,20 | head -40 | tail -37 > perf-report.log
	cat h2load.log dstat.log perf-stat.log perf-report.log > http2_benchmark.log
