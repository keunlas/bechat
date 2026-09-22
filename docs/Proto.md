# 协议格式

协议固定为 TLV 报文，由三个字段 [tag, length, value] 组成。

Tag 字段固定为 16 bit 即 2 bytes。
Length 字段固定为 16 bit 即 2 bytes。
Value 字段的长度是可变的，由 Length 字段指定，故最大长度为 65536 bytes。





