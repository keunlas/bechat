# 目前逻辑的一些待改造项

## 报文接收超时机制

Session 类中的 read_tag read_length read_value 要添加超时机制，
当发生超时时，Session 将会直接断开连接。

## 设计 RequestCodec 及其相关逻辑

目前还需要设计 RequestCodec 解析的返回值以怎样的形式返回，
重点是如何方便且高效的统一返回值。






