# 目前逻辑的一些改造草稿

[ok]
Session 需要提供一个 Send 接口，
用来在 write_strand_ 上面去把 TlvMessagePtr 发送出去。
on_read_completed 只负责把接受到的消息传输给 ServerContext。

[todo]
Dispathcer 不作为单独的类而是作为函数提供，
应该放入 ServerContext 中。
并且提供额外的 RequestCodec 类去解析请求，
解析好的请求交给 Dispathcer 函数进行解析，
该函数会将请求分配给 ServerContext 中持有的 ChatService 对象。

[todo]
TlvCodec 及其在 ServerContext 中被使用的部分需要重新规划。

[todo]
TlvProto 需要重新审核。



