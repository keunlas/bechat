# BeChat TLV 协议（v0.0.1）

> 本文档描述 BeChat v0.0.1 的目标协议，与 `docs/DesignLine.md` 中的功能清单对应。
> 当前代码仍处于 TLV 读写验证阶段，`Session` 尚未按本文档分发业务命令。

## 1. 总体约定

### 1.1 传输

- 使用 TCP 长连接；一个连接上可以按顺序收发多个 TLV。
- 多个 TLV 直接首尾相接，不额外加分隔符。接收方先读满 4 字节头部，再按 `Length` 读满 Value。
- 未来启用 TLSv1.3 后，字节流格式不变，TLS 只负责加密传输。
- 当前头部不含协议版本号；版本演进通过新增 Tag 完成，已分配 Tag 的语义不得改变。破坏性变更必须通过预留的协商类 Tag 显式协商。

### 1.2 字节序

- 所有多字节整数都使用**大端序（network byte order）**。
- 读取时使用 `be16toh`（当前代码已如此），写入时使用 `htobe16`；协议字段按网络字节序处理。

### 1.3 字符串

协议中需要字符串时使用 `String`：

```
String {
  Length: 2 bytes (u16, 大端, 表示后面字节数)
  Data:   <Length> bytes (UTF-8, 不含结尾 0x00)
}
```

- 除特别说明外，字符串不允许为空。
- 文本类 `String` 的 Data 必须是合法 UTF-8；非法编码按 `INVALID_PARAMETER` 处理。
- 接收方必须校验：`String.Length <= 当前 Value 剩余字节数`。
- 用户名长度：1 ~ 32 字节；密码长度：6 ~ 64 字节；内容长度见各命令。

> 安全提示：TLSv1.3 启用前，用户名和密码按明文字节流传输，当前版本只适合本地开发与测试。

### 1.4 请求标识（request_id）

- `request_id` 为 `u32`，大端序，由客户端生成。
- 所有 C→S 请求 Value 的前 4 字节都是 `request_id`。
- 所有 S→C 响应 Value 的前 4 字节原样回传 `request_id`；服务端推送不包含 `request_id`。
- 同一连接上，所有“未收到响应”的请求必须使用不同的 `request_id`；收到响应后可以复用。
- `0x00000000` 保留给“无法解析出 request_id”的错误响应，客户端不得使用。

### 1.5 时间与序号

- 时间戳为 `u64`，表示 Unix 时间戳（UTC，毫秒）。
- 聊天消息序号 `seq` 为 `u64`，服务端内存中从 1 开始单调递增，服务器重启后重置；0 表示“无/不限”。

## 2. TLV 报文格式

```
 0                   1
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
+-------------------+-------------------+
|        Tag        |      Length       |
|     (2 bytes)     |     (2 bytes)     |
+-------------------+-------------------+
|                                       |
|               Value                   |
|          (<Length> bytes)             |
|                                       |
+---------------------------------------+
```

| 字段 | 宽度 | 说明 |
|------|------|------|
| Tag | 2 字节 | 操作类型，大端 `u16` |
| Length | 2 字节 | Value 的字节数，大端 `u16`，不含 Tag 和 Length 自身 |
| Value | Length 字节 | 由 Tag 决定布局；`Length == 0` 时无 Value |

- 单个 TLV 的 Value 最大 65535 字节。
- 本版本所有 C→S 请求的 Value 至少为 4 字节（`request_id` 公共头）。
- 服务端实现可配置更小的 `max_payload`（默认 65535）；超过时视为协议错误。
- 当前 v0.0.1 不引入分片，超过上限的内容直接拒绝。
- 响应中包含列表时，服务端必须在 `max_payload` 内按完整条目截断；`count` 表示实际返回的条目数。

## 3. 交互模型

1. 连接建立后处于**未登录状态**，只允许 `C2S_REGISTER` 和 `C2S_LOGIN`。
2. `C2S_LOGIN` 成功后，服务端把该 TCP 连接绑定到对应用户名，进入**已登录状态**。
   - 在线状态由 TCP 连接存活决定；连接断开即视为下线。
   - 同一用户名重复登录时，新连接顶替旧连接，旧连接由服务端直接关闭；旧连接上未完成的请求不再处理。
   - 登录成功后当前连接身份固定；本版本不提供登出/切换用户命令，关闭连接即登出。
   - 已登录连接再次发送 `C2S_REGISTER` / `C2S_LOGIN` 属于状态错误，返回 `0x0008 INVALID_STATE`。
3. 允许流水线（pipelining）：客户端可以不等上一个请求的响应，直接发送后续请求。
   - 服务端按接收顺序处理请求；当前版本响应也按请求顺序写回。
   - 客户端必须用 `request_id` 匹配请求与响应，不能依赖“响应顺序 == 发送顺序”这一实现细节。
   - 同一连接上未完成请求的 `request_id` 必须唯一；重复时后到的请求会被拒绝。
   - 服务端可以限制单个连接未完成请求数量（`max_in_flight`，建议默认 64）；超过限制时返回 `S2C_ERROR`（`0x0007`）并继续读取后续 TLV。
   - 流水线中的请求按接收顺序处理，因此 `C2S_LOGIN` 之后紧跟的受保护请求，只有在登录成功后才按已登录状态处理。
4. 登录成功后，服务端可能随时主动推送，并且推送可能插在任意两个响应之间；客户端必须能在任意时刻接收：
   - `S2C_NEW_MESSAGE_PUSH`
   - `S2C_USER_JOINED_PUSH`
5. 每个能被正常解析并接受的请求都有一个对应响应；响应 Tag = 请求 Tag `| 0x8000`。解析失败、未知 Tag、重复 `request_id`、超过 `max_in_flight` 等无法按请求正常回复的情况，统一使用 `S2C_ERROR`。
6. 客户端只能发送 `0x0001 ~ 0x00FF` 区间的请求 Tag。

## 4. Tag 总表

| Tag | 名称 | 方向 | 登录前可用 | 说明 |
|-----|------|------|:----------:|------|
| 0x0000 | 保留 | - | - | 不使用 |
| 0x0001 | `C2S_REGISTER` | C→S | 是 | 用户注册 |
| 0x0002 | `C2S_LOGIN` | C→S | 是 | 用户登录 |
| 0x0003 | `C2S_GET_HISTORY` | C→S | 否 | 获取历史聊天消息 |
| 0x0004 | `C2S_GET_ONLINE_USERS` | C→S | 否 | 获取在线用户列表 |
| 0x0005 | `C2S_SEND_MESSAGE` | C→S | 否 | 发送聊天消息 |
| 0x8000 | `S2C_ERROR` | S→C | - | 通用错误响应 |
| 0x8001 | `S2C_REGISTER_RESP` | S→C | - | 注册响应 |
| 0x8002 | `S2C_LOGIN_RESP` | S→C | - | 登录响应 |
| 0x8003 | `S2C_GET_HISTORY_RESP` | S→C | - | 历史消息响应 |
| 0x8004 | `S2C_GET_ONLINE_USERS_RESP` | S→C | - | 在线用户响应 |
| 0x8005 | `S2C_SEND_MESSAGE_RESP` | S→C | - | 发送消息响应 |
| 0x8101 | `S2C_NEW_MESSAGE_PUSH` | S→C | - | 新聊天消息推送 |
| 0x8102 | `S2C_USER_JOINED_PUSH` | S→C | - | 用户加入聊天室推送 |

Tag 区间约定：

- `0x0001 ~ 0x00FF`：客户端请求；该区间内的所有请求（包括未来新增 Tag）都必须以 `request_id` 作为 Value 前 4 字节。
- `0x8000`：通用错误响应。
- `0x8001 ~ 0x80FF`：服务端响应，响应 Tag = 请求 Tag `| 0x8000`。
- `0x8100 ~ 0x81FF`：服务端主动推送。
- 其余区间预留给未来功能；已分配的 Tag 不得改变语义，扩展通过新增 Tag 完成。

> `C2S_SEND_MESSAGE` 是相对最初功能清单补充的请求接口：没有它，聊天室内就不会产生“新消息”，推送也没有数据来源。`DesignLine.md` 的基础功能清单已包含该项。

## 5. 具体协议

每个 Value 按字段顺序拼接。

- 所有 C→S 请求 Value 前 4 字节为 `request_id (u32)`。
- 所有 S→C 响应 Value 前 4 字节为 `request_id (u32)`，服务端原样回传；普通响应随后紧跟 `status`（`S2C_ERROR` 除外，见 5.8）。
- 推送 Value 不包含 `request_id`。
- 请求 Value 必须恰好被各字段完整消费；字段不足或存在多余字节都按 `MALFORMED_PACKET` 处理。

### 5.1 用户注册

#### 请求 `C2S_REGISTER`（0x0001）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 客户端生成的请求标识 |
| username | String | 1 ~ 32 字节 UTF-8，全服唯一 |
| password | String | 6 ~ 64 字节 |

#### 响应 `S2C_REGISTER_RESP`（0x8001）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 回传请求中的 `request_id` |
| status | u16 | 0x0000 成功；其他见状态码 |

注册成功后不会自动登录，客户端需要再发送 `C2S_LOGIN`。

### 5.2 用户登录

#### 请求 `C2S_LOGIN`（0x0002）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 客户端生成的请求标识 |
| username | String | 已注册用户名 |
| password | String | 密码 |

#### 响应 `S2C_LOGIN_RESP`（0x8002）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 回传请求中的 `request_id` |
| status | u16 | 0x0000 成功；0x0005 用户名或密码错误；0x0008 已登录状态下重复登录 |

登录成功后：

1. 当前连接绑定该用户名；
2. 服务端先给当前连接返回 `S2C_LOGIN_RESP(OK)`；
3. 再向所有在线连接广播 `S2C_USER_JOINED_PUSH`（包含新登录用户，客户端可自行忽略自己）。

### 5.3 发送聊天消息

#### 请求 `C2S_SEND_MESSAGE`（0x0005）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 客户端生成的请求标识 |
| content | String | 1 ~ 65475 字节 UTF-8（数据部分） |

发送者由当前连接绑定的用户名决定，Value 中不需要发送者字段。

> `65475` 是保守上限：按用户名最长 32 字节计算，可保证同一条消息能完整放进发送请求、新消息推送和历史响应。若服务端配置了更小的 `max_payload`，则以 `max_payload` 为实际上限。

#### 响应 `S2C_SEND_MESSAGE_RESP`（0x8005）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 回传请求中的 `request_id` |
| status | u16 | 0x0000 成功；其他见状态码 |
| seq | u64 | 服务端分配给该消息的序号 |
| timestamp | u64 | 服务端写入时间，Unix 毫秒 |

发送成功后：

1. 消息写入服务端内存中的聊天室历史；
2. 服务端先给发送者返回 `S2C_SEND_MESSAGE_RESP(OK)`；
3. 再向所有在线连接广播 `S2C_NEW_MESSAGE_PUSH`（包含发送者自己）。

### 5.4 获取历史聊天消息

#### 请求 `C2S_GET_HISTORY`（0x0003）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 客户端生成的请求标识 |
| before_seq | u64 | 0 表示从最新一条开始取；非 0 表示只取 `seq < before_seq` 的消息 |
| limit | u16 | 最多返回条数；0 表示服务端默认值（建议 50），有效范围 1 ~ 200 |

#### 响应 `S2C_GET_HISTORY_RESP`（0x8003）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 回传请求中的 `request_id` |
| status | u16 | 0x0000 成功；其他见状态码 |
| count | u16 | 实际返回的消息条数 |
| messages | Message[count] | 按 `seq` 从小到大排列 |

`Message` 结构：

| 字段 | 类型 | 说明 |
|------|------|------|
| seq | u64 | 消息序号 |
| sender | String | 发送者用户名 |
| timestamp | u64 | Unix 毫秒 |
| content | String | 消息内容 |

翻页方式：先传 `before_seq = 0` 拿最新一页，再用本页第一条消息的 `seq` 作为下一次的 `before_seq` 继续向前翻。

若按 `limit` 返回的消息会让整个响应 Value 超过 `max_payload`，服务端只保留其中 `seq` 较大（较新）的完整消息，直到响应能放入 `max_payload`；响应仍按 `seq` 升序排列。这样客户端用本页第一条消息的 `seq` 作为下一次 `before_seq` 时不会漏掉中间消息。

### 5.5 获取在线用户列表

#### 请求 `C2S_GET_ONLINE_USERS`（0x0004）

Value 只包含公共头，`Length = 4`：

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 客户端生成的请求标识 |

#### 响应 `S2C_GET_ONLINE_USERS_RESP`（0x8004）

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 回传请求中的 `request_id` |
| status | u16 | 0x0000 成功；其他见状态码 |
| count | u16 | 在线用户数 |
| users | OnlineUser[count] | 按登录时间从小到大排列 |

`OnlineUser` 结构：

| 字段 | 类型 | 说明 |
|------|------|------|
| username | String | 用户名 |
| login_timestamp | u64 | 本次登录时间，Unix 毫秒 |

> 在线用户列表当前没有分页参数。若在线用户过多导致响应超过 `max_payload`，服务端只保留登录时间较晚的完整条目并设置实际 `count`；完整列表的翻页能力留待后续 Tag 扩展。

### 5.6 推送用户新消息

`S2C_NEW_MESSAGE_PUSH`（0x8101）由服务端在聊天室有新消息时向所有在线连接广播。

| 字段 | 类型 | 说明 |
|------|------|------|
| seq | u64 | 消息序号 |
| sender | String | 发送者用户名 |
| timestamp | u64 | Unix 毫秒 |
| content | String | 消息内容 |

### 5.7 推送用户加入聊天室信息

`S2C_USER_JOINED_PUSH`（0x8102）由服务端在某用户登录成功后向所有在线连接广播。

| 字段 | 类型 | 说明 |
|------|------|------|
| username | String | 新加入的用户名 |
| timestamp | u64 | 登录时间，Unix 毫秒 |

### 5.8 通用错误响应

`S2C_ERROR`（0x8000）用于无法按请求格式正常回复的场景，包括未知/错误方向 Tag、Value 字段越界、Length 与字段长度不一致、重复 `request_id`、超过 `max_in_flight` 等。

| 字段 | 类型 | 说明 |
|------|------|------|
| request_id | u32 | 从请求 Value 前 4 字节提取并回传；提取不到时填 0x00000000 |
| request_tag | u16 | 触发错误的请求 Tag；无法判断时填 0x0000 |
| status | u16 | 错误码，通常为 0x0002；重复 `request_id` 时为 0x0006，超过并发限制时为 0x0007 |

解析规则：

- 对 `0x0001 ~ 0x00FF` 区间内的请求（包括未知 Tag），Value 的前 4 字节按 `request_id` 处理并原样回传，即使后续字段无法解析。
- 请求 Value 不足 4 字节时，`request_id` 填 0x00000000。

## 6. 状态码

| 状态码 | 含义 | 使用场景 |
|--------|------|----------|
| 0x0000 | OK | 成功 |
| 0x0001 | NOT_LOGGED_IN | 未登录就请求受保护接口 |
| 0x0002 | MALFORMED_PACKET | 未知/错误方向 Tag、Value 长度不匹配、String 越界、超过 max_payload |
| 0x0003 | INVALID_PARAMETER | 用户名/密码/limit/content 不符合约束、非法 UTF-8、`request_id` 为 0 |
| 0x0004 | USERNAME_ALREADY_EXISTS | 注册时用户名已存在 |
| 0x0005 | INVALID_CREDENTIALS | 登录时用户名不存在或密码错误 |
| 0x0006 | DUPLICATE_REQUEST_ID | 同一连接上存在重复的未完成 `request_id` |
| 0x0007 | TOO_MANY_IN_FLIGHT | 未完成请求数超过服务端限制 |
| 0x0008 | INVALID_STATE | 当前连接状态不允许该请求（如登录后再次注册/登录） |
| 0xFFFF | INTERNAL_ERROR | 服务端内部错误 |

## 7. 错误处理规则

服务器先完成 TLV 边界和 Value 字段解析，再按下表顺序做准入与业务检查：

1. **解析失败**：客户端发送 S→C 方向 Tag、未知 Tag、Value 无法解析、`Length < 4`、`Length` 超过服务端上限。返回 `S2C_ERROR`，回传可提取的 `request_id`，`status = 0x0002`，随后继续读取下一个 TLV；`request_id` 提取不到时填 0。
2. **超过并发限制**：未完成请求数超过 `max_in_flight`。返回 `S2C_ERROR`，回传 `request_id`，`status = 0x0007`。
3. **重复 request_id**：同一连接上已经存在相同的未完成 `request_id`。返回 `S2C_ERROR`，回传 `request_id`，`status = 0x0006`。
4. **状态错误**：已登录连接发送 `C2S_REGISTER` / `C2S_LOGIN`。返回对应响应 Tag，回传 `request_id`，`status = 0x0008`。
5. **未登录**：已知请求但客户端未登录。返回该请求对应的响应 Tag，回传 `request_id`，`status = 0x0001`。
6. **参数错误**：字段语义不合法（如用户名过短、`request_id` 为 0、limit 超范围、空消息）。返回对应响应 Tag，回传 `request_id`，`status = 0x0003`。
7. **业务错误**：注册重名返回 `status = 0x0004`；登录凭据错误返回 `status = 0x0005`。
8. **网络错误或 EOF**：关闭连接；若该连接已登录，从在线列表移除。

## 8. 示例

### 8.1 注册

用户 `alice` / 密码 `secret`，客户端使用 `request_id = 1`：

```text
C→S:
  00 01                                   # Tag = C2S_REGISTER
  00 13                                   # Value 长度 = 19
  00 00 00 01                             # request_id = 1
  00 05 61 6C 69 63 65                    # String "alice"
  00 06 73 65 63 72 65 74                 # String "secret"

S→C:
  80 01                                   # Tag = S2C_REGISTER_RESP
  00 06                                   # Value 长度 = 6
  00 00 00 01                             # request_id = 1（原样回传）
  00 00                                   # status = OK
```

### 8.2 登录并流水线发送消息

```text
C→S: C2S_LOGIN { request_id = 1, username = "alice", password = "secret" }
S→C: S2C_LOGIN_RESP { request_id = 1, status = OK }
S→C: S2C_USER_JOINED_PUSH { username = "alice", timestamp = <unix_ms> }   # 广播

# 客户端不等 SEND_MESSAGE 的响应，直接发送后续请求：
C→S: C2S_SEND_MESSAGE { request_id = 2, content = "hi" }
C→S: C2S_GET_HISTORY { request_id = 3, before_seq = 0, limit = 50 }

S→C: S2C_SEND_MESSAGE_RESP { request_id = 2, status = OK, seq = 1,
                             timestamp = <unix_ms> }
S→C: S2C_NEW_MESSAGE_PUSH { seq = 1, sender = "alice", timestamp = <unix_ms>,
                            content = "hi" }                               # 广播
S→C: S2C_GET_HISTORY_RESP { request_id = 3, status = OK, count = 1,
                            messages = [ { seq = 1, sender = "alice", ... } ] }
```

### 8.3 翻页拉取历史

```text
C→S: C2S_GET_HISTORY { request_id = 4, before_seq = 0, limit = 50 }
S→C: S2C_GET_HISTORY_RESP { request_id = 4, status = OK, count = N,
                            messages = [N 条，seq 递增] }

# 向前翻页：用上一页第一条消息的 seq
C→S: C2S_GET_HISTORY { request_id = 5, before_seq = <上一页最小 seq>, limit = 50 }
S→C: S2C_GET_HISTORY_RESP { request_id = 5, ... }
```

## 9. 与当前代码的差距

- `Session::read_*` 已按大端序读取 Tag / Length / Value，可作为编解码基础。
- 当前读取循环还没有解析 `request_id`，也没有请求分发逻辑。
- `Session::do_write()` 目前只回写 `Value`，没有写 Tag 和 Length；实现本协议时需要改为回写完整 TLV。
- 支持流水线还需要为每个连接维护“未完成 `request_id` 集合 / 表”，并为响应回传对应 `request_id`。
- 当前还没有 Dispatcher、用户表、在线表、历史消息存储，这些是下一阶段实现内容。
