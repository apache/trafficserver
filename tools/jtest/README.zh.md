#              jtest与ATS压力测试                    #

作为一个高性能的proxy代理服务器，Apache Traffic Server是很难用常用工具进行细致的性能压测的，本文尝试对性能压测进行定义并介绍如何在这种高性能、高并发、大规模的系统中，进行破坏级别的压力测试。

##                   压力测试的定义                            ##

很多情况下，大家都希望在服务器上线前、业务上线前，对业务的支撑能力做一个测试，希望知道自己的改动是不是在进步，是不是能够比较平稳的抗住预期的流量压力，等等，总结下来压力测试的主要用途有：

1. 确定新版本的改进不会引起性能问题
2. 找出业务的单机qps数据，并定义好安全的水位线
3. 使用性能数据作为硬件采购以及预算的参数
4. 更好的理解业务的波动对线上系统的压力

日常开发中，最最有意义的是，找出新代码是否在性能上有回退；找出新的性能改进到底提高了多少。

根据压力的来源，压力测试又会被分为：

1. 实验室仿真压力测试

   实验室仿真，在http proxy服务器测试场景下，客户端和服务器端的数据和请求，都是由工具生成的。
2. 业务copy仿真压力测试

   显然，如果希望服务器程序真正的能够在线上跑，简单的实验室测试与业务需求肯定是差异非常大，因此就有了为什么不把业务流量复制到系统中来的想法。这也就是所谓的流量复制的压测方式。


##                   jtest工具初步介绍                        ##

jtest是一个专门用于proxy/cache系统的实验室性能压力测试工具，具有极高的性能。能够同时担当后台服务器和客户端。ATS系统专用的性能压测工具。

商业的压力测试工具，一般也是一个所谓的盒子，自带客户端、服务器端，能够自己生成模拟流量、copy客户提交流量等，很像目前jtest的模式

最早的jtest被设计为可以分布式的集群上运行，用上层脚本系统起停，来压测一个ATS集群系统，早先的系统并没有考虑像现在这样的多核CPU普遍性，设计为单进程工具，我们只在后续高级用法中介绍单机如何跑多个进程等。

###                      jtest简单说明                                ###

### 基本测试
jtest作为专门针对ATS的测试，已经就ATS的最简单配置下，做了很多简化的默认参数，以便于用户快速的上手，我们以最简单的本机jtest压测本机的默认配置ATS为例子，介绍最基本的jtest用法：

1. 设置ATS的remap规则：

    在默认的空remap.config中添加一条规则

    > map http://localhost:9080/ http://127.0.0.1:9080/

2. 运行jtest：
    > jtest

  这个命令默认的参数，即相当于 "jtest -s 9080 -S localhost -p 8080 -P localhost -c 100 -z 0.4"，将会起100个连接使用127.0.0.1的9080端口作为jtest源服务器（jtest监听），对本机（localhost）的8080端口上跑的ATS进行测试，并控制整体命中率在40%。

输出结果：

    con  new    ops 1byte   lat   bytes/per     svrs  new  ops    total   time  err
    100  468 2329.6    39    39 36323315/363233   617  617  617  46131904 136980.9    0
    100  471 2361.5    39    40 35993941/359939   619  619  619  45466393 136981.9    0
    100  465 2327.0    40    41 35385495/353854   607  607  607  45095273 136982.9    0

其中：

* con: 并发连接数。并发连接数，单进程单cpu处理能力取决于CPU与测试场景，请酌情设置，推荐小于9999
* new: 每秒新建连接数。这个参数取决于并发连接数量与长连接效率。
* ops: 每秒请求数。也作qps，是比较体现服务器性能的关键指标。
* 1byte：首字节平均响应时间。这个是体现整体转发效率的关键指标。
* lat: 完成请求整体响应时间（收到最后一个字节）。cache系统性能关键指标。
* bytes/per：每秒字节流量/每秒每连接流量
* svrs：服务器端请求数
* new：服务器端新建连接数
* ops：服务器端每秒请求数
* total：服务器端总请求的字节数
* time：测试时间（秒）
* err：出错数量（连接数）。稳定性测试中，这个数据可以作为一个关键指标。

### jtest命令详解
jtest有非常多的参数，这些参数的组合又会产生很多特殊的效果，我们将从完整的使用说明开始，详细说明jtest测试的命令参数：


    localhost:tools zym$ ./jtest/jtest -h
    JTest Version 1.94  - Dec  9 2013 17:11:24 (zym@zymMBPr.local)
    Usage: /Users/zym/git/traffic.git/tools/jtest/.libs/jtest [--SWITCH [ARG]]

#### 参数格式：
参数格式是 短参数_长参数_类型_默认值_参数说明，多数参数能做到自我说明并且比较详细，更多说明参见下面的说明以及后续高级用法示例。

      switch__________________type__default___description

#### 测试机器IP与端口设置：
这几个设置是最最常用的几个参数

      -p, --proxy_port        int   8080      Proxy Port
      -P, --proxy_host        str   localhost Proxy Host
      -s, --server_port       int   0         Server Port (0:auto select)
      -S, --server_host       str   (null)    Server Host (null:localhost)

* -p -P是用来指定要测试的ATS服务器地址、端口信息
* -s -S是用来指定要测试的ATS服务器，用来作为源的jtest监听域名（IP）和端口信息。

#### 服务器压力控制：
服务器的压力仿真，主要是主动生成的随机流量控制

      -r, --server_speed      int   0         Server Bytes Per Second (0:unlimit)
      -w, --server_delay      int   0         Server Initial Delay (msec)
      -c, --clients           int   100       Clients
      -R, --client_speed      int   0         Client Bytes Per Second (0:unlimit)
      -b, --sbuffersize       int   4096      Server Buffer Size
      -B, --cbuffersize       int   2048      Client Buffer Size
      -a, --average_over      int   5         Seconds to Average Over
      -z, --hitrate           dbl   0.400     Hit Rate
      -Z, --hotset            int   1000      Hotset Size
      -i, --interval          int   1         Reporting Interval (seconds)
      -k, --keepalive         int   4         Keep-Alive Length
      -K, --keepalive_cons    int   4         # Keep-Alive Connections (0:unlimit)
      -L, --docsize           int   -1        Document Size (-1:varied)
      -j, --skeepalive        int   4         Server Keep-Alive (0:unlimit)

* -r -w -R，控制客户端、服务器端的速度，多数压测的情况下，不会做特殊限制，在需要仿真大并发、大延迟等情况下，可以做控制。
* -b -B，模拟客户端和服务器端的buffer大小设置，buffer的大小可以极大的影响IO的能力，也会影响内存的占用。
* -z -Z，这是用来控制命中率和热点数据。命中率是由热点数据的命中，加miss的请求。热点数据的多少，也会影响服务器的内存使用。
* -k -K -j，控制客户端和服务器的长连接。
* -i，用来控制jtest结果统计汇报间隔时间。
* -L，用来控制jtest生成的随机url的返回body大小，默认-1表示完全随机，没有限制。

#### 控制输入输出的配置：

      -x, --show_urls         on    false     Show URLs before they are accessed
      -X, --show_headers      on    false     Show Headers
      -f, --ftp               on    false     FTP Requests
      - , --ftp_mdtm_err_rate dbl   0.000     FTP MDTM 550 Error Rate
      - , --ftp_mdtm_rate     int   0         FTP MDTM Update Rate (sec, 0:never)

* -x -X，用来debug，显示url以及所有header头，是个排查利器。
* ftp相关的是用来压测ftp的，不过ATS对ftp的支持已经删除啦。

#### 测试的流程处理：
jtest测试，是可以进行复杂的处理，比如对一个网站进行深度抓取测试，对反向、正向、透明模式测试

      -l, --fullpage          on    false     Full Page (Images)
      -F, --follow            on    false     Follow Links
      -J, --same_host         on    false     Only follow URLs on same host
      -t, --test_time         int   0         run for N seconds (0:unlimited)
      -u, --urls              str   (null)    URLs from File
      -U, --urlsdump          str   (null)    URLs to File
      -H, --hostrequest       int   0         Host Request(1=yes,2=transparent)
      -C, --check_content     on    false     Check returned content
      - , --nocheck_length    on    false     Don't check returned length
      -m, --obey_redirects    off   true      Obey Redirects
      -M, --embed URL         off   true      Embed URL in synth docs

* -l -F -J，用来对html文件进行解析，并提前其中的所有图片元素等进行深度抓取的控制。
* -t，控制测试运行时间，默认一直跑
* -u -U，给jtest指定url，纪录jtest跑的url（如过存在解析html的方式，则纪录的可能会多于指定的）
* -H，控制服务器测试模式，是否带host头，决定了服务器是跑在反向代理、正向代理、透明代理模式
* -C --nocheck_length，是否检查返回的内容、长度
* -m，是否跳转
* -M，控制是否把uri放到返回结果的Body开头，这个一般用来做数据校验使用


#### 请求的分散度与热点：
hash是jtest、ats里无处不在的，如何让hash互相影响，甚至测试hash碰撞等情况？？

      -q, --url_hash_entries  int   1000000   URL Hash Table Size (-1:use file size)
      -Q, --url_hash_filename str   (null)    URL Hash Table Filename

-q -Q，hash控制
#### 服务器的控制：
服务器的使用类型控制，可以让jtest跑在不同的模式下

      -y, --only_clients      on    false     Only Clients
      -Y, --only_server       on    false     Only Server
      -A, --bandwidth_test    int   0         Bandwidth Test
      -T, --drop_after_CL     on    false     Drop after Content-Length

* -y -Y，可以将jtest单独跑为服务器和客户端分离的服务。
* -A，-T，可以做更快的流量压测工具。

#### 其他控制信息机制：

      -V, --version           on    false     Version
      -v, --verbose           on    false     Verbose Flag
      -E, --verbose_errors    off   true      Verbose Errors Flag

* -v -E，可以用于debug错误等

#### 请求的随机机制：
本类参数，主要控制服务器和请求的随机程度、复杂度，构建一个复杂的测试用例，将会对服务器的稳定性测试起到很好的效果。

      -D, --drand             int   0         Random Number Seed
      -I, --ims_rate          dbl   0.500     IMS Not-Changed Rate
      -g, --client_abort_rate dbl   0.000     Client Abort Rate
      -G, --server_abort_rate dbl   0.000     Server Abort Rate
      -n, --extra_headers     int   0         Number of Extra Headers
      -N, --alternates        int   0         Number of Alternates
      -e, --client_rate       int   0         Clients Per Sec
      -o, --abort_retry_speed int   0         Abort/Retry Speed
      - , --abort_retry_bytes int   0         Abort/Retry Threshold (bytes)
      - , --abort_retry_secs  int   5         Abort/Retry Threshold (secs)
      -W, --reload_rate       dbl   0.000     Reload Rate

* -D，用于生成url的随机数，如果有多个jtest并发运行，可以对这个随机的seed进行区分以控制cache的多小等
* -I，请求的内容中，带的IMS比例
* -g -G，客户端和服务器的Abort比例
* -n -N，控制客户端发送的header数量，服务器的内容的副本数量
* -e，每秒的客户端数量
* -o --abort_retry_bytes --abort_retry_secs，控制重试的速度
* -W 控制内容的重复度？？

#### 服务的仿真程度：

      -O, --compd_port        int   0         Compd port
      -1, --compd_suite       on    false     Compd Suite
      -2, --vary_user_agent   int   0         Vary on User-Agent (use w/ alternates)
      -3, --content_type      int   0         Server Content-Type (1 html, 2 jpeg)
      -4, --request_extension int   0         Request Extn (1".html" 2".jpeg" 3"/")
      -5, --no_cache          int   0         Send Server no-cache
      -7, --zipf_bucket       int   1         Bucket size (of 1M buckets) for Zipf
      -8, --zipf              dbl   0.000     Use a Zipf distribution with this alpha (say 1.2)
      -9, --evo_rate          dbl   0.000     Evolving Hotset Rate (evolutions/hour)

* -0 -1，compress服务
* -2，控制是否对不同的UA启用多副本
* -3，服务器返回的内容的类型
* -4，请求的内容类型
* -5，是否发送给服务器no-cache控制
* -7 -8，zipf服务
* -9，热点的偏移调整

#### 其他信息：

      -d, --debug             on    false     Debug Flag
      -h, --help                              Help


=============================================================================
#### 上面参数中一些经常用的参数

* -c, --clients           int   100       Clients

  跟ab的-c参数类似，默认jtest会启用100并发

* -k, --keepalive         int   4         Keep-Alive Length
* -K, --keepalive_cons    int   4         # Keep-Alive Connections (0:unlimit)

  跟ab的-k参数功能类似，控制长连接的数量以及长连接的效率

* -z, --hitrate           dbl   0.400     Hit Rate

  命中率40%，在反向代理里太低了

* -u, --urls              str   (null)    URLs from File

  提供自己的urls，可以像http_load一样使用jtest

* -y, --only_clients      on    false     Only Clients
* -Y, --only_server       on    false     Only Server

  如果你需要独立运行jtest客户端和服务器端，以提高性能和吞吐量等

## jtest进阶用法
本段将会介绍如何更好的使用jtest压榨ATS性能，避免瓶颈问题
### 独立测试机器
进阶用法里，我们先尝试用用2个机器，一个跑jtest，一个跑ATS，做压力测试：

1. 定义机器角色：

   我们使用 'ts.cn' 作为压测的URL的域名，192.168.0.1作为我们的ATS服务器，192.168.0.2作为我们的测试端，跑jtest。

2. 设置ATS的map规则：

   在我们的例子里，我们应该设置如下规则：

   `map http://ts.cn:9080/ http://192.168.0.2:9080/`
3. 在192.168.0.2运行jtest命令：

   `jtest -S ts.cn -P 192.168.0.1`

在这个例子里，我们设置了服务器端的域名为`ts.cn`，同时给了`-P`参数指定了要测试的服务器是192.168.0.1，这样我们的jtest将会使用192.168.0.1:8080作为代理服务器，使用ts.cn作为要压测的域名来进行压力测试。

### 一个机器多个jtest
由于jtest是单进程模式，进程压测一个当前多大16个core以上的ATS系统，肯定是压不动的，如何才能更好的进行压测呢？接上面的例子，我们可以在测试端多跑几个jtest进程，我们暂跑6个：

1. 定义机器角色：

   我们使用 'ts.cn' 作为压测的URL的域名，192.168.0.1作为我们的ATS服务器，192.168.0.2作为我们的测试端，跑jtest。

2. 设置ATS的map规则：

   在我们的例子里，我们应该设置如下规则：

        map http://ts.cn:9080/ http://192.168.0.2:9080/
        map http://ts.cn:9081/ http://192.168.0.2:9081/
        map http://ts.cn:9082/ http://192.168.0.2:9082/
        map http://ts.cn:9083/ http://192.168.0.2:9083/
        map http://ts.cn:9084/ http://192.168.0.2:9084/
        map http://ts.cn:9085/ http://192.168.0.2:9085/


3. 在192.168.0.2运行对应的6个jtest命令：

        jtest -S ts.cn -s 9080 -P 192.168.0.1 &
        jtest -S ts.cn -s 9081 -P 192.168.0.1 &
        jtest -S ts.cn -s 9082 -P 192.168.0.1 &
        jtest -S ts.cn -s 9083 -P 192.168.0.1 &
        jtest -S ts.cn -s 9084 -P 192.168.0.1 &
        jtest -S ts.cn -s 9085 -P 192.168.0.1 &

这样就可以啦，问题是一个窗口里会持续打印很多很多结果信息，不太容易分辨问题。如何更优雅的跑多个机器多个jtest能？我们下面进行详细介绍

## jtest集群用法
在服务器集群中，一对一的压测是很见的，通常我们为减少jtest性能瓶颈，会采用多个机器，多个jtest一起跑的情况，我们将需要引入其他控制机制、数据统计机制才好。

### screen的并行jtest管理
首先，我们必须有一个并发测试的机制，单机多进程、多机并行，并且可以统计各个进程返回的结果。当初ATS有一个测试框架，可以执行多个机器的并行测试，并能够汇总多个jtest的返回结果，现今我们虽然没有这个工具，但是我们在服务器端的统计工具tsar能够帮我们补足类似的统计数据，测试客户端方面我们这里采用简单的模式，一个screen脚本：

screen的`-c`参数，可以很方便的启动一个screen脚本，在这个脚本里，可以启动多个screen窗口，我们采用这个机制来并发的启动多个jtest命令，由于screen同时有后台驻留的功能，可以确保我们在需要的时候回来看看各个jtest的测试结果，下面是一个screen的脚本：

    screen jtest -P 192.168.0.1 -S ts.cn -s 192.168.0.2 -z 1.0 -D 9080 -k 2 -c 30 -Z 1000 -q 10000 -L 50000
    screen jtest -P 192.168.0.1 -S ts.cn -s 192.168.0.2 -z 1.0 -D 9081 -k 2 -c 30 -Z 1000 -q 10000
    screen top
    detach

我们采用screen的`-X quit`命令，来停止jtest压测。同时，为了表示各个目标的机器，我们可以给各个screen打上标识(-R参数)，以方便区分一个机器上的多个screen。

### N:N的测试机器
为了测试，我们假定有10台测试客户端机器192.168.0.{10..19}，10台测试目标服务器192.168.0.{20..29}（组成cluster或孤立服务器），我们可以在所有的测试客户端机器上对任何目标服务器进行压测，下面是我用来生成测试的一些小脚本：

修改测试方法jtest参数

    # c为服务器ip 10..19
    # s为客户端ip 20..29
    # i为每个机器最多对一个服务器起10个进程压测 0..9
    # 我们的jtest监听端口为 $c$s$i，如10200这样的形式

    for c in {10..19}
    do
      for s in {20..29}
      do
        for i in {0..9}
          do echo map http://ts.cn:$c$s$i/ http://192.168.0.$c:$c$s$i/
        done
      done
    done

这会生成一个很长的map规则，你需要把这个map规则添加到所有的ATS服务器的remap.config配置里：

    map http://ts.cn:10200/ http://192.168.0.10:10200/
    map http://ts.cn:10201/ http://192.168.0.10:10201/
    map http://ts.cn:10202/ http://192.168.0.10:10202/
    .
    .
    .
    map http://ts.cn:19297/ http://192.168.0.19:19297/
    map http://ts.cn:19298/ http://192.168.0.19:19298/
    map http://ts.cn:19299/ http://192.168.0.19:19299/

然后我们就可以用来做一些测试的screen脚本，下面的脚本会让我们在每个测试客户端机器上，生成一些针对每个测试目标服务器启动jtest的screen命令脚本：

    for c in {10..19};do for s in {20..29};do echo "
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}0 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000 -L 5000
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}1 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000 -L 10000
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}2 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000 -L 10000
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}3 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000 -L 20000
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}4 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000 -L 50000
    screen jtest -P 192.168.0.${s} -S ts.cn -s ${c}${s}5 -z 1.0 -D ${c}${s} -k 2 -c 30 -Z 1000 -q 10000
    detach
    " | ssh root@192.168.0.$c tee jtest.screen.$s;done;done
这里生产的每个脚本会对每个测试服务器，启动多个jtest测试，我们这里定义了文件大小(-L)分别为5KB、10KB、10KB、20KB、50KB、随机大小的6个jtest进程，每个进程并发(-c)30，命中率(-z)100%，并通过随机参数(-D)把hash散开。

然后，测试脚本的启动将会非常简单：

    for c in {10..19}
    do
      for s in {20..29}
      do
        ssh -t 192.168.0.$c screen -R jtest$s -c jtest.screen.$s
      done
    done
这些screen进程被命名为jtest{20..29}，你可以用screen -R jtest20来进入后台运行的screen，并用标准的CTRL+a n键盘指令在各个jtest脚本切换以查看测试情况。

停止测试的命令也很简单：

    for c in {10..19}
    do
      for s in {20..29}
      do
        ssh -t 192.168.0.$c screen -Rx jtest$s -X quit
      done
    done
以上命令都可以根据需要调整c和s的数量，例如可以实现多个客户端机器压测一个ATS服务器的效果等。

## 如何更好的仿真出线上的复杂情况
通过上面的screen脚本的定义，我们可以随时修改其中的重要设置，如-c -z等等，甚至增加一些客户端仿真的随机因素等等。

#### 如何指定内存占用
ATS的内存是一个历来敏感的话题，内存泄漏也是最最难排查的问题之一，如何在jtest测试中针对性的对内存缓存进行控制就是一个比较切实的需求。

默认jtest的-L参数是-1，大小全随机，不限制返回body大小的情况下，1000个hot object仅占用24MB左右内存缓存，平均文件大小差不多24KB，从这里我们就明白了，如何调整参数以让ATS占用更多内存：

1. 用-Z调整hot objects数量，这是热点数据，是可缓存数据，是有机会进内存的数据；
2. 用-L调整返回的文档的Body大小，hotset * docsize 就是我们需要的内存缓存大小；

这样我们就能够构建一个良好的内存占用模型，通过ATS的dump mem参数 *proxy.config.dump_mem_info_frequency* 可以让内存缓存占用也dump出来如：

	-----------------------------------------------------------------------------------------
	     Allocated      |        In-Use      | Type Size  |   Free List Name
	--------------------|--------------------|------------|----------------------------------
	                  0 |                  0 |    2097152 | memory/ioBufAllocator[14]
	         3355443200 |         3215982592 |    1048576 | memory/ioBufAllocator[13]
	           67108864 |           55574528 |     524288 | memory/ioBufAllocator[12]
	                  0 |                  0 |     262144 | memory/ioBufAllocator[11]
	                  0 |                  0 |     131072 | memory/ioBufAllocator[10]
	                  0 |                  0 |      65536 | memory/ioBufAllocator[9]

如上所示中结果中，ram在records.config中的相关配置为：

	CONFIG proxy.config.cache.ram_cache.size INT 3221225470
	CONFIG proxy.config.cache.ram_cache_cutoff INT 41943040

remap.config配置为：

	map http://tsdirect.cn:9080/ http://127.0.0.1:9080/

开启mem dump，我仅使用两个命令先后测试：

	jtest -P localhost -S tsdirect.cn -L 950000 -z 1 -Z 3000
	jtest -P localhost -S tsdirect.cn -L 480000 -z 1 -Z 6000
经过第一个jtest命令执行后，内存读入接近3G数据，使用的是1048576（1M）大小的ioBufAllocator分配的，而执行第二个jtest，除了填满后续空闲的600M内存，其他近3G数据再也没有机会进入内存了，无论第二个jtest跑多久都是这样。

这个结果显示我们当前master版本在内存缓存管理方面的热点替换方面的问题仍然没有改进。

## 如何使用jtest来跑stress测试
TBD

## 如何分析jtest爆出的一些ATS问题
TBD

## jtest的待改进问题以及后续计划
* 支持https??
* 支持spdy??
* 单进程太弱？？
* 集群压测上层调度工具？？？
* 循环（随机）压测urls提供的列表，类似http_load一样





