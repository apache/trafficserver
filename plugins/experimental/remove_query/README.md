# remove_query
####主要功能：
    修改cachekey 将url 的参数去掉，使用该插件需要注意要放在所有remap插件第一个，优先执行
####具体用法: 
    @plugin=remove_query.so
####项目地址: 
    https://github.com/xieyugui/remove_query.git
####For example:
    map http://foo.com http://foo.com  @plugin=remove_query.so @plugin=balancer.so
 
####客户端请求URL：
    http://foo.com/1.jpg?12313 == http://foo.com/1.jpg
    这两种URL ATS 会当做同一份资源缓存
####回源还会带上参数:
     request http://foo.com/1.jpg?12313   back http://foo.com/1.jpg?12313
     request http://foo.com/1.jpg   back http://foo.com/1.jpg
####应用场景：
     当源站带有防盗链规则的时候
