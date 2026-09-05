# BeChat TLV 协议（v0.0.1）

> 本文档定义 BeChat v0.0.1 的线上字节协议：TLV 报文格式、Tag 与状态码、各命令的请求/响应字段以及错误处理规则。
> 功能范围见 `docs/DesignLine.md`，开发蓝图见 `docs/MyBlueprint.md`。

## 1. 基础约定

### 1.1 传输层

| 项   | 约定                                                                                                                  |
| ---- | --------------------------------------------------------------------------------------------------------------------- |
| 协议 | TCP 长连接                                                                                                            |
| 分帧 | 多个 TLV 首尾相接，无分隔符；接收方先读满 4 字节头部，再按 `Length` 读满 Value                                        |
| 加密 | 未来启用 TLSv1.3；启用后字节流格式不变，TLS 只负责加密传输                                                            |
| 版本 | 头部不含协议版本号；版本演进通过新增 Tag 完成，已分配 Tag 语义不得改变；破坏性变更必须通过预留的协商类 Tag 显式协商 |

### 1.2 字节序

所有多字节整数均为**大端序（network byte order）**。

### 1.3 基本类型

| 类型                    | 宽度           | 说明                                |
| ----------------------- | -------------- | ----------------------------------- |
| u16 / u32 / u64         | 2 / 4 / 8 字节 | 无符号整数，大端                    |
| String                  | 变长           | `Length(u16) + Data(UTF-8)`，见下   |
| Message / OnlineUser 等 | 变长           | 固定字段按顺序拼接的复合结构，见 §6 |

```
String {
  Length: 2 bytes  u16 大端，Data 的字节数
  Data:   <Length> bytes  UTF-8，不含结尾 0x00
}
```

- 除特别说明外，字符串不允许为空。
- 文本类 `String` 的 Data 必须是合法 UTF-8；非法编码按 `StatusCode::InvalidParameter` 处理。
- 接收方必须校验：`String.Length <= 当前 Value 剩余字节数`。
- 字段长度约束：用户名 1 ~ 32 字节；密码 6 ~ 64 字节；消息内容见 §6.3。

> 安全提示：TLSv1.3 启用前，用户名和密码按明文字节流传输，当前版本只适合本地开发与测试。

### 1.4 时间戳与序号

- 时间戳：`u64`，Unix 时间戳（UTC，毫秒）。
- 聊天消息序号 `seq`：`u64`，服务端内存中从 1 开始单调递增，重启后重置；`0` 表示「无 / 不限」。

### 1.5 请求标识（request_id）

- `request_id` 为 `u32` 大端，由客户端生成。
- 所有 `Req` 请求 Value 的前 4 字节都是 `request_id`；所有 `Resp` 响应 Value 的前 4 字节原样回传；`Push` 推送不含 `request_id`。
- 服务端不维护未完成 `request_id` 表、不做去重，只原样回传；`request_id` 唯一性由客户端保证。
- 为避免客户端自己无法匹配，同一连接上尚未收到响应的请求应使用不同的 `request_id`；收到响应后可以复用。
- 客户端可以使用任意值（包括 `0`）作为 `request_id`，服务端不会校验它是否为零。
- 仅当请求 Value 不足 4 字节、无法提取 `request_id` 时，响应中该字段填 `0`。
- 注意区分：响应中的 `request_id = 0` 可能是客户端发送的 0，也可能是「不存在」时的填充值；客户端应保证自己的请求始终携带完整 4 字节公共头，避免混淆。

## 2. TLV 报文格式

```
+----------+----------+--------------------+
| Tag      | Length   | Value              |
| u16 大端 | u16 大端 | Length 字节        |
+----------+----------+--------------------+
```

| 字段   | 宽度        | 说明                                                                  |
| ------ | ----------- | --------------------------------------------------------------------- |
| Tag    | 2 字节      | 操作类型，大端 `u16`，取值见 §4                                       |
| Length | 2 字节      | Value 的字节数，大端 `u16`，不含 Tag 和 Length 自身；`0` 表示无 Value |
| Value  | Length 字节 | 布局由 Tag 决定，见 §6                                                |

约束：

- 单个 TLV 的 Value 最大 65535 字节（`Length` 字段上限）；服务端可配置更小的 `max_payload`（默认 65535），超过视为协议错误。
- 本版本所有 `Req` 请求的 Value 至少为 4 字节（`request_id` 公共头）。
- v0.0.1 不引入分片，超过上限的内容直接拒绝。
- 含列表的响应必须在 `max_payload` 内按完整条目截断；`count` 表示实际返回的条目数。

## 3. 连接与交互规则

1. 连接建立后处于**未登录状态**，只允许 `Req::Register` 和 `Req::Login`。
2. `Req::Login` 成功后，服务端把该连接绑定到对应用户名，进入**已登录状态**：
   - 在线状态由 TCP 连接存活决定，断开即视为下线。
   - 同一用户名重复登录时，新连接顶替旧连接，旧连接由服务端直接关闭，其上未完成的请求不再处理。
   - 登录成功后身份固定；本版本不提供登出/切换用户命令，关闭连接即登出。
   - 已登录连接再发 `Req::Register` / `Req::Login` 属于状态错误，返回 `StatusCode::InvalidState`。
3. 允许流水线（pipelining）：客户端可以不等上一个请求的响应，直接发送后续请求。
   - 服务端按接收顺序处理请求；当前实现响应也按请求顺序写回。
   - 客户端必须用 `request_id` 匹配请求与响应，不能依赖「响应顺序 == 发送顺序」这一实现细节。
   - `Req::Login` 之后紧跟的受保护请求，只有登录成功后才按已登录状态处理。
4. 登录成功后服务端可能随时主动推送，推送可能插在任意两个响应之间；客户端必须能在任意时刻接收：`Push::NewMessage`、`Push::UserJoined`。
5. 每个能被正常解析并接受的请求都有一个对应响应，响应 Tag = 请求 Tag `| 0x8000`。
6. 无法按请求正常回复的情况（解析失败、未知 Tag 等）统一使用 `Resp::Error`。
7. 客户端只能发送 `Req` 区间的 Tag（`0x0001 ~ 0x00FF`）。

## 4. Tag 定义

### 4.1 区间约定

| 区间            | 含义            | 说明                                                                                   |
| --------------- | --------------- | -------------------------------------------------------------------------------------- |
| 0x0000          | 保留            | 不使用                                                                                 |
| 0x0001 ~ 0x00FF | `Req` 请求      | 客户端可发送；该区间内的所有请求（含未来新增 Tag）Value 前 4 字节都必须是 `request_id` |
| 0x8000          | `Resp` 通用错误 | `Resp::Error`                                                                          |
| 0x8001 ~ 0x80FF | `Resp` 响应     | 响应 Tag = 请求 Tag ` \| 0x8000`                                                       |
| 0x8100 ~ 0x81FF | `Push` 主动推送 | 服务端主动发送，客户端不得发送                                                         |
| 其余            | 保留            | 预留给未来功能；已分配的 Tag 不得改变语义，扩展通过新增 Tag 完成                       |

### 4.2 Tag 总表

| 数值   | 常量                               | 方向  | 登录前可用 | 说明               |
| ------ | ---------------------------------- | :---: | :--------: | ------------------ |
| 0x0001 | `MessageTag::Req::Register`        |  Req  |     是     | 用户注册           |
| 0x0002 | `MessageTag::Req::Login`           |  Req  |     是     | 用户登录           |
| 0x0003 | `MessageTag::Req::GetHistory`      |  Req  |     否     | 获取历史聊天消息   |
| 0x0004 | `MessageTag::Req::GetOnlineUsers`  |  Req  |     否     | 获取在线用户列表   |
| 0x0005 | `MessageTag::Req::SendMessage`     |  Req  |     否     | 发送聊天消息       |
| 0x8000 | `MessageTag::Resp::Error`          | Resp  |     -      | 通用错误响应       |
| 0x8001 | `MessageTag::Resp::Register`       | Resp  |     -      | 注册响应           |
| 0x8002 | `MessageTag::Resp::Login`          | Resp  |     -      | 登录响应           |
| 0x8003 | `MessageTag::Resp::GetHistory`     | Resp  |     -      | 历史消息响应       |
| 0x8004 | `MessageTag::Resp::GetOnlineUsers` | Resp  |     -      | 在线用户响应       |
| 0x8005 | `MessageTag::Resp::SendMessage`    | Resp  |     -      | 发送消息响应       |
| 0x8101 | `MessageTag::Push::NewMessage`     | Push  |     -      | 新聊天消息推送     |
| 0x8102 | `MessageTag::Push::UserJoined`     | Push  |     -      | 用户加入聊天室推送 |

## 5. 状态码

状态码为 `u16` 大端，出现在各响应的 `status` 字段。

| 数值   | 常量                                | 含义           | 使用场景                                                          |
| ------ | ----------------------------------- | -------------- | ----------------------------------------------------------------- |
| 0x0000 | `StatusCode::Ok`                    | 成功           | 请求处理成功                                                      |
| 0x0001 | `StatusCode::MalformedPacket`       | 协议错误       | 未知/错误方向 Tag、Value 长度不匹配、String 越界、超过 max_payload |
| 0x0002 | `StatusCode::InvalidParameter`      | 参数错误       | 用户名/密码/limit/content 不符合约束、非法 UTF-8                  |
| 0x0003 | `StatusCode::NotLoggedIn`           | 未登录         | 未登录就请求受保护接口                                            |
| 0x0004 | `StatusCode::UsernameAlreadyExists` | 用户名已存在   | 注册时用户名已存在                                                |
| 0x0005 | `StatusCode::InvalidCredentials`    | 凭据错误       | 登录时用户名不存在或密码错误                                      |
| 0x0006 | `StatusCode::InvalidState`          | 状态错误       | 当前连接状态不允许该请求（如登录后再次注册/登录）                 |
| 0xFFFF | `StatusCode::InternalError`         | 服务端内部错误 | 服务端内部异常                                                    |

## 6. 命令详细定义

Value 按字段顺序拼接。通用规则：

- 所有 `Req` 请求 Value 前 4 字节为 `request_id (u32)`。
- 所有 `Resp` 响应 Value 前 4 字节为回传的 `request_id (u32)`，随后紧跟 `status (u16)`；`Resp::Error` 例外，见 §6.8。
- `Push` 推送 Value 不含 `request_id`，也没有 `status`。
- 请求 Value 必须恰好被各字段完整消费；字段不足或存在多余字节都按 `StatusCode::MalformedPacket` 处理。

### 6.1 用户注册

注册成功后不会自动登录，客户端需要再发送 `Req::Login`。

**请求 `Req::Register`（0x0001）**

| 字段       | 类型   | 说明                        |
| ---------- | ------ | --------------------------- |
| request_id | u32    | 客户端生成的请求标识        |
| username   | String | 1 ~ 32 字节 UTF-8，全服唯一 |
| password   | String | 6 ~ 64 字节                 |

**响应 `Resp::Register`（0x8001）**

| 字段       | 类型 | 说明                                                                                    |
| ---------- | ---- | --------------------------------------------------------------------------------------- |
| request_id | u32  | 回传请求中的 `request_id`                                                               |
| status     | u16  | `Ok` 成功；`UsernameAlreadyExists` 重名；`InvalidState` 已登录状态下重复注册；其余见 §7 |

### 6.2 用户登录

登录成功后：当前连接绑定该用户名；先向当前连接返回 `Resp::Login`（`status = Ok`）；再向所有在线连接广播 `Push::UserJoined`（客户端可自行忽略自己）。

**请求 `Req::Login`（0x0002）**

| 字段       | 类型   | 说明                 |
| ---------- | ------ | -------------------- |
| request_id | u32    | 客户端生成的请求标识 |
| username   | String | 已注册用户名         |
| password   | String | 密码                 |

**响应 `Resp::Login`（0x8002）**

| 字段       | 类型 | 说明                                                                                  |
| ---------- | ---- | ------------------------------------------------------------------------------------- |
| request_id | u32  | 回传请求中的 `request_id`                                                             |
| status     | u16  | `Ok` 成功；`InvalidCredentials` 用户名或密码错误；`InvalidState` 已登录状态下重复登录 |

### 6.3 发送聊天消息

发送者由当前连接绑定的用户名决定，Value 中不需要发送者字段。

**请求 `Req::SendMessage`（0x0005）**

| 字段       | 类型   | 说明                              |
| ---------- | ------ | --------------------------------- |
| request_id | u32    | 客户端生成的请求标识              |
| content    | String | 1 ~ 65475 字节 UTF-8（数据部分）  |

> `65475` 是保守上限：按用户名最长 32 字节计算，可保证同一条消息能完整放进发送请求、新消息推送和历史响应。若服务端配置了更小的 `max_payload`，则以 `max_payload` 为实际上限。

**响应 `Resp::SendMessage`（0x8005）**

| 字段       | 类型 | 说明                      |
| ---------- | ---- | ------------------------- |
| request_id | u32  | 回传请求中的 `request_id` |
| status     | u16  | `Ok` 成功；未登录见 §7    |
| seq        | u64  | 服务端分配给该消息的序号  |
| timestamp  | u64  | 服务端写入时间，Unix 毫秒 |

发送成功后：消息写入服务端内存中的聊天室历史；先向发送者返回 `Resp::SendMessage`（`status = Ok`）；再向所有在线连接广播 `Push::NewMessage`（包含发送者自己）。

### 6.4 获取历史聊天消息

**请求 `Req::GetHistory`（0x0003）**

| 字段       | 类型 | 说明                                                              |
| ---------- | ---- | ----------------------------------------------------------------- |
| request_id | u32  | 客户端生成的请求标识                                              |
| before_seq | u64  | `0` 表示从最新一条开始取；非 0 表示只取 `seq < before_seq` 的消息 |
| limit      | u16  | 最多返回条数；`0` 表示服务端默认值（建议 50）；有效范围 1 ~ 200   |

**响应 `Resp::GetHistory`（0x8003）**

| 字段       | 类型           | 说明                      |
| ---------- | -------------- | ------------------------- |
| request_id | u32            | 回传请求中的 `request_id` |
| status     | u16            | `Ok` 成功；`limit` 超范围见 §7 |
| count      | u16            | 实际返回的消息条数        |
| messages   | Message[count] | 按 `seq` 从小到大排列     |

`Message` 结构：

| 字段      | 类型   | 说明         |
| --------- | ------ | ------------ |
| seq       | u64    | 消息序号     |
| sender    | String | 发送者用户名 |
| timestamp | u64    | Unix 毫秒    |
| content   | String | 消息内容     |

翻页方式：先传 `before_seq = 0` 拿最新一页，再用本页第一条消息的 `seq` 作为下一次的 `before_seq` 继续向前翻。

若按 `limit` 返回的消息会让整个响应 Value 超过 `max_payload`，服务端只保留其中 `seq` 较大（较新）的完整消息，直到响应能放入 `max_payload`；响应仍按 `seq` 升序排列。这样客户端用本页第一条消息的 `seq` 作为下一次 `before_seq` 时不会漏掉中间消息。

### 6.5 获取在线用户列表

**请求 `Req::GetOnlineUsers`（0x0004）**

Value 只包含公共头，`Length = 4`：

| 字段       | 类型 | 说明                 |
| ---------- | ---- | -------------------- |
| request_id | u32  | 客户端生成的请求标识 |

**响应 `Resp::GetOnlineUsers`（0x8004）**

| 字段       | 类型              | 说明                      |
| ---------- | ----------------- | ------------------------- |
| request_id | u32               | 回传请求中的 `request_id` |
| status     | u16               | `Ok` 成功；未登录见 §7    |
| count      | u16               | 在线用户数                |
| users      | OnlineUser[count] | 按登录时间从小到大排列    |

`OnlineUser` 结构：

| 字段            | 类型   | 说明                    |
| --------------- | ------ | ----------------------- |
| username        | String | 用户名                  |
| login_timestamp | u64    | 本次登录时间，Unix 毫秒 |

> 在线用户列表当前没有分页参数。若在线用户过多导致响应超过 `max_payload`，服务端只保留登录时间较晚的完整条目并设置实际 `count`；完整列表的翻页能力留待后续 Tag 扩展。

### 6.6 推送：新聊天消息 `Push::NewMessage`（0x8101）

聊天室有新消息时向所有在线连接广播。

| 字段      | 类型   | 说明         |
| --------- | ------ | ------------ |
| seq       | u64    | 消息序号     |
| sender    | String | 发送者用户名 |
| timestamp | u64    | Unix 毫秒    |
| content   | String | 消息内容     |

### 6.7 推送：用户加入聊天室 `Push::UserJoined`（0x8102）

某用户登录成功后向所有在线连接广播。

| 字段      | 类型   | 说明                |
| --------- | ------ | ------------------- |
| username  | String | 新加入的用户名      |
| timestamp | u64    | 登录时间，Unix 毫秒 |

### 6.8 通用错误响应 `Resp::Error`（0x8000）

用于无法按请求格式正常回复的场景，包括未知/错误方向 Tag、Value 字段越界、Length 与字段长度不一致等。

| 字段        | 类型 | 说明                                                          |
| ----------- | ---- | ------------------------------------------------------------- |
| request_id  | u32  | 从请求 Value 前 4 字节提取并回传；提取不到时填 0x00000000     |
| request_tag | u16  | 触发错误的请求 Tag；无法判断时填 0x0000                       |
| status      | u16  | 错误码，当前版本固定为 `StatusCode::MalformedPacket`（0x0001） |

解析规则：

- 对 `0x0001 ~ 0x00FF` 区间内的请求（包括未知 Tag），Value 的前 4 字节按 `request_id` 处理并原样回传，即使后续字段无法解析。
- 请求 Value 不足 4 字节时，`request_id` 填 0x00000000。

## 7. 错误处理规则

服务端先完成 TLV 边界和 Value 字段解析，再按顺序做准入与业务检查：

1. **解析失败**：客户端发送 `Resp` / `Push` 区间 Tag、未知 Tag、Value 无法解析、Value 不足 4 字节、超过服务端上限。
   返回 `Resp::Error`，回传可提取的 `request_id`（提取不到填 0），`status = MalformedPacket`；随后继续读取下一个 TLV。
2. **状态错误**：已登录连接发送 `Req::Register` / `Req::Login`。返回对应响应 Tag，回传 `request_id`，`status = InvalidState`。
3. **未登录**：已知请求但客户端未登录。返回该请求对应的响应 Tag，回传 `request_id`，`status = NotLoggedIn`。
4. **参数错误**：字段语义不合法（如用户名过短、limit 超范围、空消息）。返回对应响应 Tag，回传 `request_id`，`status = InvalidParameter`。
5. **业务错误**：注册重名返回 `status = UsernameAlreadyExists`；登录凭据错误返回 `status = InvalidCredentials`。
6. **网络错误或 EOF**：关闭连接；若该连接已登录，从在线列表移除。

## 8. 示例

### 8.1 注册

用户 `alice` / 密码 `secret`，客户端使用 `request_id = 1`：

```text
C→S:
  00 01                                   # Tag = Req::Register
  00 13                                   # Value 长度 = 19
  00 00 00 01                             # request_id = 1
  00 05 61 6C 69 63 65                    # String "alice"
  00 06 73 65 63 72 65 74                 # String "secret"

S→C:
  80 01                                   # Tag = Resp::Register
  00 06                                   # Value 长度 = 6
  00 00 00 01                             # request_id = 1（原样回传）
  00 00                                   # status = Ok
```

### 8.2 登录并流水线发送消息

```text
C→S: Req::Login       { request_id = 1, username = "alice", password = "secret" }
S→C: Resp::Login      { request_id = 1, status = Ok }
S→C: Push::UserJoined { username = "alice", timestamp = <unix_ms> }      # 广播

# 客户端不等 SendMessage 的响应，直接发送后续请求：
C→S: Req::SendMessage { request_id = 2, content = "hi" }
C→S: Req::GetHistory  { request_id = 3, before_seq = 0, limit = 50 }

S→C: Resp::SendMessage { request_id = 2, status = Ok, seq = 1, timestamp = <unix_ms> }
S→C: Push::NewMessage  { seq = 1, sender = "alice", timestamp = <unix_ms>,
                         content = "hi" }                                # 广播
S→C: Resp::GetHistory  { request_id = 3, status = Ok, count = 1,
                         messages = [ { seq = 1, sender = "alice", ... } ] }
```

### 8.3 翻页拉取历史

```text
C→S: Req::GetHistory { request_id = 4, before_seq = 0, limit = 50 }
S→C: Resp::GetHistory { request_id = 4, status = Ok, count = N,
                        messages = [N 条，seq 递增] }

# 向前翻页：用上一页第一条消息的 seq
C→S: Req::GetHistory { request_id = 5, before_seq = <上一页最小 seq>, limit = 50 }
S→C: Resp::GetHistory { request_id = 5, ... }
```
