# balancer
#### 具体用法: 
     https://docs.trafficserver.apache.org/en/latest/reference/plugins/balancer.en.html 
#### 项目地址: 
     https://github.com/xieyugui/balancer.git

在roundrobin 模式下新增 backup、weight、max_fails、fail_timeout ,以及add path和开启https回源的功能, 
  当添加--open参数时, backup、weight、max_fails、fail_timeout等参数才会生效，不然只有默认轮询功能：

#### backup=number
    marks the server as a backup server. It will be passed requests when the primary servers are 
    unavailable.by default, 0    --(0/1)

#### weight=number
    sets the weight of the server, by default, 1.

#### max_fails=number
    sets the number of unsuccessful attempts to communicate with the server that should happen 
    in the duration set by the fail_timeout parameter to consider the server unavailable for 
    a duration also set by the fail_timeout parameter. By default, the number of unsuccessful
    attempts is set to 3. The zero value disables the accounting of attempts. What is considered
    an unsuccessful attempt is defined by the proxy_next_upstream, fastcgi_next_upstream, 
    uwsgi_next_upstream, scgi_next_upstream, and memcached_next_upstream directives.

#### fail_timeout=time
    sets the time during which the specified number of unsuccessful attempts to communicate 
    with the server should happen to consider the server unavailable; and the period of time 
    the server will be considered unavailable. By default, the parameter is set to 10 seconds.
    
    
### For example:
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin @pparam=--https @pparam=--open @pparam=one.bar.com:80,0,1,3,10 @pparam=two.bar.com,0,1,3,10
 
### Add path:
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin,0/ @pparam=--open @pparam=one.bar.com:80,0,1,3,10 @pparam=two.bar.com,0,1,3,10 <br />
    if client request http://cdnxxx.com/1.jpg then ATS will back to the source server request http://cdnxxx.com/0/1.jpg
