# BeChat 协议规范

## 1. 传输与连接模型

- 业务命令基于 TCP，**每条 TCP 连接只承载一条命令**：客户端连接 → 发送一个请求 → 服务器返回一个响应 → 关闭连接（类似 HTTP 短连接）；
- 心跳基于 UDP（客户端 → 服务器单向），与 TCP 完全分离；
- 所有多字节整数使用**大端序（network byte order）**；
- 字符串字段使用 UTF-8，定长字段左对齐、剩余字节填 `0x00`；
- 未来若引入长连接模式，通过预留的协商原语（0x00F0–0x00FF）升级当前连接，协议头部保持不变。

## 2. TCP 固定头部

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
┌───────────────────────────────┬───────────────┐
│            Type (u16)         │ Length (u16)  │
└───────────────────────────────┴───────────────┘
```

| 字段 | 宽度 | 说明 |
|------|------|------|
| Type | 2 字节 | 操作类型，见第 4 节 |
| Length | 2 字节 | Value 部分的字节数，上限 65535；服务器可用 `max_payload` 进一步收紧 |

Value 为空时 Length 为 0，头部之后没有载荷。

> Length 保持 2 字节（u16）为既定决策，单包 Value 上限 64 KiB。若未来需要更大载荷，通过分片或扩展头演进，不改变现有头部。

## 3. 通用约定

### 3.1 请求 / 响应

- 客户端到服务器：请求；
- 服务器对每个请求**必须返回一条响应**，响应 Type = 请求 Type `| 0x8000`；
- 所有响应 Value 的第一个字段是 `status (u16)`；
- 响应写完后服务器关闭连接；客户端在响应前多发的数据被忽略；
- 当前版本没有服务端主动推送；Push 类型（0x0101/0x0102）预留给未来长连接模式。

### 3.2 请求上下文

每个请求 Value 的前 4 字节是 `request_id (u32)`，由客户端生成（建议递增或随机），响应原样带回。服务器不保存、不解释 request_id，客户端用它匹配请求与响应、做超时与去重。

### 3.3 令牌（Token）

受保护原语的 Value 中带有 `token (u8[128])`：

- 由服务器在 Login 成功时签发，格式为 `base64url(payload) || '.' || base64url(HMAC-SHA256(payload))`；
- payload 至少包含：用户名（64 字节）、签发时间、过期时间；
- 服务器验证时不查表，只校验签名与过期时间；
- 令牌由客户端保存；token 过期后重新走一次 Login 获取新 token 即可。

## 4. TCP 操作清单

| Type | 名称 | 方向 | 需要令牌 | 说明 |
|------|------|------|:--------:|------|
| 0x0001 | Register | C→S | 否 | 注册新用户 |
| 0x0002 | Login | C→S | 否 | 登录，签发 token 并登记在线 |
| 0x0003 | Logout | C→S | 是 | 主动下线 |
| 0x0004 | OnlineQuery | C→S | 是 | 查询一组用户名是否在线（纯心跳判定） |
| 0x0010 | SendPrivate | C→S | 是 | 私聊：接收方在线则入其待收队列 |
| 0x0011 | SendGroup | C→S | 是 | 群聊：写历史并返回 seq |
| 0x0012 | CreateGroup | C→S | 是 | 创建群（创建者为 owner） |
| 0x0013 | JoinGroup | C→S | 是 | 加入群 |
| 0x0014 | LeaveGroup | C→S | 是 | 离开群 |
| 0x0015 | PullGroupHistory | C→S | 是 | 按 seq 增量拉取群聊历史 |
| 0x0020 | Poll | C→S | 是 | 拉取并清空私聊待收队列 |
| 0x8001 | RegisterResp | S→C | - | 响应 |
| 0x8002 | LoginResp | S→C | - | 响应 |
| 0x8003 | LogoutResp | S→C | - | 响应 |
| 0x8004 | OnlineQueryResp | S→C | - | 响应 |
| 0x8010 | SendPrivateResp | S→C | - | 响应 |
| 0x8011 | SendGroupResp | S→C | - | 响应 |
| 0x8012 | CreateGroupResp | S→C | - | 响应 |
| 0x8013 | JoinGroupResp | S→C | - | 响应 |
| 0x8014 | LeaveGroupResp | S→C | - | 响应 |
| 0x8015 | PullGroupHistoryResp | S→C | - | 响应 |
| 0x8020 | PollResp | S→C | - | 响应 |
| 0x0101 | PrivatePush（预留） | S→C | - | 未来长连接模式使用 |
| 0x0102 | GroupPush（预留） | S→C | - | 未来长连接模式使用 |
| 0x00F0–0x00FF | 连接协商（预留） | C→S | - | 未来长连接模式协商用 |

## 5. TCP Value 结构

以下布局中的定长字符串均为 UTF-8 零填充；`content` 等变长字段占满剩余字节。

### 5.1 Register（0x0001）

```
request_id   u32
username     u8[64]
password     u8[64]      # 建议客户端传加盐哈希，服务器再哈希后存储
```

### 5.2 RegisterResp（0x8001）

```
request_id   u32
status       u16
```

### 5.3 Login（0x0002）

```
request_id   u32
username     u8[64]
password     u8[64]
```

### 5.4 LoginResp（0x8002）

```
request_id            u32
status                u16
token                 u8[128]   # 仅成功时有效，失败时为全零
heartbeat_interval    u32       # 秒，服务器建议的心跳间隔（默认 10）
heartbeat_timeout     u32       # 秒，服务器判定离线阈值（默认 30）
server_time           u64       # unix 毫秒，供客户端校准
```

### 5.5 Logout（0x0003）与 LogoutResp（0x8003）

请求：

```
request_id   u32
token        u8[128]
```

响应：

```
request_id   u32
status       u16
```

### 5.6 OnlineQuery（0x0004）

```
request_id   u32
token        u8[128]
query_count  u16
usernames    u8[64] * query_count
```

### 5.7 OnlineQueryResp（0x8004）

```
request_id   u32
status       u16
query_count  u16
results      per-user:
  username      u8[64]
  online        u8      # 0x00 离线 / 0x01 在线（心跳有效）
```

### 5.8 SendPrivate（0x0010）

```
request_id   u32
token        u8[128]
recipient    u8[64]
content      bytes      # 剩余全部字节
```

### 5.9 SendPrivateResp（0x8010）

```
request_id   u32
status       u16        # 0x0000 已入接收方待收队列 / 0x0005 用户不存在 / 0x000B 离线
recipient    u8[64]
```

### 5.10 SendGroup（0x0011）

```
request_id   u32
token        u8[128]
group        u8[64]
content      bytes
```

### 5.11 SendGroupResp（0x8011）

```
request_id   u32
status       u16
group        u8[64]
seq          u64        # 本次消息在群历史中的序号
```

### 5.12 CreateGroup（0x0012）

```
request_id   u32
token        u8[128]
group        u8[64]
```

### 5.13 CreateGroupResp（0x8012）

```
request_id   u32
status       u16
group        u8[64]
```

创建成功后创建者自动成为成员（owner）。

### 5.14 JoinGroup（0x0013）与 JoinGroupResp（0x8013）

请求：

```
request_id   u32
token        u8[128]
group        u8[64]
```

响应：

```
request_id   u32
status       u16
group        u8[64]
```

### 5.15 LeaveGroup（0x0014）与 LeaveGroupResp（0x8014）

请求与响应结构同 JoinGroup。离开后不再接收该群消息；群本身保留（含历史）。

### 5.16 PullGroupHistory（0x0015）

```
request_id   u32
token        u8[128]
group        u8[64]
after_seq    u64        # 只拉取 seq > after_seq 的消息；0 表示拉取最新的若干条
max_count    u16        # 本次最多拉取条数（默认 100）
```

### 5.17 PullGroupHistoryResp（0x8015）

```
request_id   u32
status       u16
group        u8[64]
count        u16
messages     per-message:
  seq          u64
  sender       u8[64]
  timestamp    u64      # unix 毫秒
  content_len  u16
  content      bytes (content_len)
```

服务器按剩余报文空间截断条数；客户端若收到 `count == max_count`，可继续用最后一条的 `seq` 翻页。

### 5.18 Poll（0x0020）

```
request_id   u32
token        u8[128]
```

### 5.19 PollResp（0x8020）

```
request_id   u32
status       u16
private_count  u16
messages     per-message:
  sender       u8[64]
  timestamp    u64      # unix 毫秒
  content_len  u16
  content      bytes (content_len)
```

服务器返回并**清空**该用户私聊待收队列。客户端轮询频率自行决定（如在线时每 1–2 秒一次，或由用户操作触发）。

### 5.20 预留类型

- 0x0101 PrivatePush、0x0102 GroupPush：长连接模式的推送类型，当前版本不发送；
- 0x00F0–0x00FF：长连接协商原语（如 StreamMode），未来启用。

## 6. UDP 心跳协议

### 6.1 包格式（固定长度，客户端 → 服务器）

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 ...
┌───────────────┬───────────────┬───────────────┬─────────────┐
│ magic (u16)   │ version (u8)  │ 保留 (u8=0)   │ token[128]  │
├───────────────┴───────────────┴───────────────┴─────────────┤
│ token[128]（续）                                             │
├─────────────────────────────────────────────────────────────┤
│ client_ts (u64, unix 毫秒)                                  │
└─────────────────────────────────────────────────────────────┘
```

| 字段 | 宽度 | 说明 |
|------|------|------|
| magic | 2 字节 | 固定 `0xBEC8`，用于过滤无关 UDP 流量 |
| version | 1 字节 | 协议版本，当前 `0x01` |
| 保留 | 1 字节 | 置 0 |
| token | 128 字节 | Login 签发的令牌 |
| client_ts | 8 字节 | 客户端本地 unix 毫秒，仅供服务器日志/调试，不参与判定 |

总长 140 字节。服务器**不回复**心跳包，客户端单向发送。

### 6.2 判定规则

- 客户端登录成功后立即开始发送，之后每 **10 秒**发送一次（以 LoginResp 返回的 `heartbeat_interval` 为准）；
- 服务器收到合法心跳（magic/version 正确、token 签名有效、未过期、该用户已登录）后，更新 `last_heartbeat`；
- 服务器定时器每 **5 秒**扫描在线表，`last_heartbeat` 距今超过 **30 秒**（`heartbeat_timeout`）的用户判定为断开：移出在线表，**丢弃其私聊待收队列**；
- 非法心跳包（错误 magic、伪造/过期 token、用户未登录）直接丢弃并记录 debug 日志；
- TCP 连接的建立与断开不影响在线状态。

### 6.3 时序示例

```
客户端                          服务器
  │── TCP Login ──────────────▶│  校验通过，登记在线
  │◀── LoginResp(token, 10, 30)│
  │── TCP 连接关闭 ───────────▶│  不影响在线状态
  │── UDP heartbeat (t=0) ───▶│  last_heartbeat = t0
  │── UDP heartbeat (t=10s) ─▶│  last_heartbeat 更新
  │── UDP heartbeat (t=20s) ─▶│  更新
  │     ... 客户端停止发送 ... │
  │                           │  t=50s 扫描：距上次心跳 30s
  │                           │  ⇒ 判定断开，移除在线条目并丢弃待收队列
```

### 6.4 端口

- 默认与 TCP 使用**同一个端口号**（TCP 与 UDP 协议栈独立，可同时绑定 35565）；
- 若部署环境不允许同端口，可通过配置为 UDP 指定独立端口。

## 7. 状态码

### 7.1 通用

| 状态码 | 含义 |
|--------|------|
| 0x0000 | 成功 |
| 0x0001 | 未认证（令牌缺失或无效） |
| 0x0002 | 协议错误（Length 超限、字段非法、未知 Type） |
| 0x0003 | 用户名已存在（Register） |
| 0x0004 | 用户名或密码错误（Login） |
| 0x0005 | 用户不存在（私聊收件人不存在） |
| 0x0006 | 群不存在（SendGroup/JoinGroup/PullGroupHistory） |
| 0x0007 | 参数错误（字段长度、数量不匹配） |
| 0x0008 | 已在群中（重复 Join） |
| 0x0009 | 不在群中（未加入就 SendGroup/LeaveGroup） |
| 0x000A | 群名已存在（CreateGroup） |
| 0x000B | 接收方离线（SendPrivate） |
| 0xFFFF | 服务器内部错误 |

## 8. 错误处理规则

1. 头部 Length 超过 `max_payload`、字段长度越界、未知 Type：返回 `0x0002` 协议错误后**关闭连接**；
2. 受保护原语令牌无效：返回 `0x0001`，连接保持到响应写完；
3. 业务错误（重复注册、群不存在等）：返回对应状态码；
4. 每条 TCP 连接只处理一条命令，响应写完后服务器关闭连接；多余数据被忽略；
5. 任何网络错误 / EOF：记录日志并关闭连接，不影响在线状态；
6. 心跳超时：移出在线表并丢弃私聊待收队列，不发送任何网络通知（没有可用的 TCP 通道）。

## 9. 迁移说明

- 头部（Type u16 + Length u16）与当前实现一致，`docs/test_cmd.md` 中的手工测试命令保持有效；
- 当前 echo 行为将被 Dispatcher 取代，且 Session 改为“读一条 → 响应 → 关闭”，不再循环读取；
- 客户端调用方式改变：每个命令新建 TCP 连接，发送请求，收响应，关闭；
- 新增 UDP 心跳通道：服务器需同时绑定 UDP socket，客户端在登录后启动心跳发送；
- 新增私聊待收队列与 Poll 原语，取代长连接推送。
