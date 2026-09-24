# ReConnect（刷新令牌重连）设计与实施计划

> 状态：设计已确认，待实现
> 关联文档：`docs/Proto.md`、`docs/DesignLine.md`
> 关联代码：`bechat/core/server_context.cpp`、`bechat/service/user_registry.cpp/h`

---

## 0. 背景与目标

当前实现中，刷新令牌是一条**完全无状态**的 JWT：

- `jti` 直接等于 username（见 `get_refresh_token_now`），没有任何唯一性；
- 服务端不保存任何签发记录，只要签名和过期时间合法，历史令牌永远有效，无法吊销；
- `UserRegisty::Refresh()` 与 `handle_refresh()` 都要求 `session->IsAuthorized()`，
  所以一个**新建立的连接**即使持有合法刷新令牌，也只会拿到 `NOT_LOGIN`，无法恢复登录态。

本计划引入 `RECONNECT`：客户端只要持有合法刷新令牌，就能在**新连接**上无密码恢复已登录状态。
为了让"让旧刷新令牌失效"成为可能，刷新令牌必须从无状态变为**有服务端记录**的凭据。

### 0.1 已确认的三条设计决策

| # | 决策 | 直接影响 |
| - | ---- | -------- |
| 1 | 刷新令牌状态**持久化**，磁盘格式改为 **JSON Lines** | 重启服务器后重连依然有效；需要做旧格式迁移 |
| 2 | `RECONNECT` **不轮换**刷新令牌 | 实现简单、重试安全；刷新令牌是 30 天**硬过期**，到期必须重新输密码 |
| 3 | **多设备** | 令牌按 `(username, device_id)` 维度管理，各设备互不影响 |

### 0.2 非目标（本期明确不做）

- 刷新令牌轮换（rotation）与"复用即吊销"检测；
- 新登录时主动踢掉/通知其它设备的在线连接（需要连接注册表，另行设计）；
- 业务消息的 access token 校验（`UserRegisty::Verify()` 目前没有任何 tag 调用它）；
- 强制 TLS（属于部署与安全加固，见第 8 章风险）。

---

## 1. 核心概念与不变量

### 1.1 概念

- **RTE（Refresh Token Entry）**：服务端保存的一条刷新令牌记录，主键是 `jti`；
- **device_id**：由客户端生成并持久化的设备标识，**不是安全凭据**，只用于区分设备；
- **身份来源**：只有"验签通过的刷新令牌"里的 claim 才能作为重连时的身份来源。

### 1.2 必须始终成立的不变量

1. `jti` 全局唯一（随机生成，绝不复用）；
2. 同一 `(username, device_id)` 在服务端**最多存在 1 条 RTE**；
3. **查表是唯一的吊销手段**：只有服务端存在对应 RTE 的刷新令牌才算有效；
4. 磁盘文件是内存状态的**完整快照**，任何一次状态改动（登录、清理过期、淘汰设备）之后都要落盘；
5. claim（`sub`/`uname`/`did`）只有在**签名校验通过之后**才可信。

---

## 2. 流程总览

### 2.1 登录（LOGIN，0x0002，已存在，需改造）

1. 校验用户名 / 密码（沿用现有 argon2 逻辑）；
2. 删除该用户下所有**已过期**的 RTE；
3. 删除该用户下 `device_id` 相同的旧 RTE（**同设备重新登录 → 旧刷新令牌立即失效**）；
4. 若设备数已达上限 `kMaxDevicesPerUser`，按 `created_at` 淘汰最旧的一条；
5. 生成随机 `jti`，插入新 RTE，落盘；
6. `session->SetAuthorized(username)`；
7. 签发 access token 与 refresh token 返回（顺序见 Step 6 的注意点）。

### 2.2 重连（RECONNECT，0x0006，新增）

1. 解码 + 验签刷新令牌（HS256、refresh secret、`iss`、`aud`、`use=refresh`、未过期）；
2. 从**验签通过**的令牌中读取 `sub`（username）与 `jti`；
3. 查表：用户存在、RTE 存在、RTE 未过期；
4. **不改动 RTE**（不轮换、不延长 30 天过期时间）；
5. `session->SetAuthorized(username)`；
6. 签发**绑定新 Session id** 的 access token 返回。

### 2.3 续签（REFRESH，0x0005，已登录连接）

与现状相同，但**同样必须查 jti 白名单**（否则旧令牌仍能续签 access token）；
不轮换刷新令牌，失败时不再把 Session 置为未授权（见 Step 8）。

### 2.4 服务器重启

启动时从 JSON Lines 文件恢复 `user_records_`，刷新令牌继续有效——这正是决策 1 的目的。

---

## 3. 数据模型与磁盘格式

### 3.1 内存结构（`bechat/service/user_registry.h`）

```cpp
static constexpr size_t kMaxDeviceIdSize{64};
static constexpr size_t kMaxDevicesPerUser{8};   // 单用户最多保留的设备数

struct RefreshTokenEntry {
  std::string jti;         // 32 位十六进制（16 字节随机）
  std::string device_id;   // 空串表示客户端未上报 device_id
  int64_t created_at{0};   // unix 秒
  int64_t expires_at{0};   // unix 秒 = created_at + 30 天
};

struct UserRecord {
  std::string username;
  std::string password_hash;
  std::unordered_map<std::string /* jti */, RefreshTokenEntry> refresh_tokens{};
};
```

选择 `unordered_map<jti, RTE>` 而不是 `vector`：查表/删除都是 O(1)；
代价是遍历顺序不稳定，落盘时按 `created_at` 排序解决（见 3.2）。

### 3.2 磁盘格式（JSON Lines）

文件路径沿用：`$HOME/.config/bechat/user_records.db`（`Config::CfgPath()`）。
一行一个用户，每行是一个完整的 JSON 对象：

```json
{"v":1,"username":"alice","password_hash":"$argon2id$v=19$...","refresh_tokens":[{"jti":"9f2c...","device_id":"dev-a","created_at":1758700000,"expires_at":1761292000}]}
```

格式规则：

- `v`：**行格式版本**，当前为 `1`；读取时缺失按 `1` 处理，未知字段忽略；
- `refresh_tokens`：数组，元素按 `created_at` 升序输出；
- 顶层用户按 `username` 升序输出；
- 时间统一用 **unix 秒（int64）**，不用 ISO 字符串 + 时区；
- 单行解析失败：`ERROR` 日志 + 跳过该行，**不中断启动**；
- nlohmann::json 的 object 默认按 key 有序（`std::map`），所以同样内容序列化结果稳定，方便 diff。

### 3.3 原子写

因为要修改已有用户的记录，旧的"追加一行"方式不再适用，改为**全量重写 + 原子替换**：

1. 写入 `user_records.db.tmp`（**必须同目录**，保证 rename 不跨设备）；
2. `flush()` 并检查流状态；失败则记 `CRITICAL` 日志后返回（内存状态保留，服务继续运行）；
3. `std::filesystem::rename(tmp, target)`（POSIX 上是原子替换）；
4. 若 rename 失败（例如 Windows 不允许覆盖已存在文件）：
   先 `std::filesystem::remove(target)` 再 rename；仍失败记 `CRITICAL`。

> 当前是单机单聊天室的规模，全量重写足够；用户量上来之后再考虑
> append-only + 定期 compaction。

### 3.4 旧格式迁移

旧格式：`username hash`（空格分隔，一行一个用户）。

迁移规则：

1. 判断方式：去掉行首尾空白后，**首字符是 `{` → JSONL**，否则按旧格式解析；
2. 出现任意旧格式行时，加载完成后：
   - 把旧文件重命名为 `user_records.db.bak`；
   - 用新格式重新写一份（此步会清空所有 RTE，因为旧记录没有令牌信息）；
3. 迁移在 `UserRegisty` 构造函数中完成。`bechat.cpp` 里 `ServerContexts`
   先于 `Server` / `SslServer` 构造，此时还没有连接进来，不存在并发问题；
4. **迁移后所有旧刷新令牌失效**，老客户端需要重新登录一次——这是预期行为，
   建议写进 release note。

### 3.5 过期清理与设备上限

- **过期清理**：惰性清理。加载时、每次 `Login` / `Reconnect` / `Refresh` 进入临界区时，
  先删除 `expires_at <= now` 的 RTE；有删除则标记"需要落盘"。
- **设备上限**：`kMaxDevicesPerUser = 8`（建议值，后续可移到配置文件）。
  登录时如果**新的 device_id** 会导致超限，按 `created_at` 淘汰最旧的一条并 `INFO` 记录；
  被淘汰设备下次重连会拿到 `REVOKED_REFRESH_TOKEN`。

---

## 4. 令牌设计

### 4.1 刷新令牌（有变化）

保留：`type=JWT`、`iss=BeChat`、`sub=username`、`aud=bechat`、`iat`、`nbf`、`exp`、
`use=refresh`、`uname=username`、`ver=CFG_VERSION`、HS256 + `CFG_JWT_SECRET_REFRESH`、30 天。

变化：

- **`jti`：由 username 改为随机 16 字节（hex 32 字符）**，这是"能吊销"的前提；
- 新增 `did` claim = device_id，便于排查问题；
  **服务端查表只信 `jti`**，`did` 仅用于一致性校验与日志；
- 因为不轮换，`exp` 就是硬过期时间，客户端 30 天后必须重新登录。

随机数用 libsodium（项目已链接，`Global::Init()` 里已调用 `sodium_init()`）：

```cpp
static std::string random_jti() {
  unsigned char buf[16];
  randombytes_buf(buf, sizeof(buf));
  // 转成 32 字符 hex 字符串返回
}
```

### 4.2 访问令牌（不变）

10 分钟有效期，`jti = session id`，**绑定当前连接**。
注意：`RECONNECT` 成功后必须返回**新连接**的 access token，旧连接的 access token 对新连接无效。

### 4.3 校验顺序（非常重要）

```
decode → verify(签名 / iss / aud / use / exp) → 读取 claim 得到 username、jti → 查表 → SetAuthorized
```

永远不要"先读 claim 再验签"。username 只能来自验签通过后的 `sub` / `uname`。

---

## 5. 协议变更

### 5.1 新增 tag

```cpp
#define BECHAT_TAG_RECONNECT 0x0006U   // Session & Account 段，紧接 BECHAT_TAG_REFRESH
```

### 5.2 LOGIN 请求新增可选字段 `device_id`

| 字段 | 类型 | 必需 | 说明 |
| ---- | ---- | ---- | ---- |
| device_id | 字符串 | 否（缺省 `""`） | 客户端生成并持久化；同一设备重复登录会让该设备的旧刷新令牌失效 |

校验（放业务层 `UserRegisty::Login`）：长度 ≤ 64，字符集 `[A-Za-z0-9_-]`；
不合法返回 `BECHAT_STATUS_INVALID_PARAMS`。

> 缺省为空串时也能工作：同一用户所有"未上报设备"的登录共享 `device_id=""`，
> 表现等价于单设备——旧客户端重复登录同样会让上一个令牌失效。

### 5.3 RECONNECT（Tag: 0x0006）

请求报文：

| 字段 | 类型 | 必需 | 说明 |
| ---- | ---- | ---- | ---- |
| request_id | uint32 | 否 | 同现有约定 |
| refresh_token | 字符串 | 是 | 刷新令牌 |

响应报文：

| 字段 | 类型 | 必需 | 说明 |
| ---- | ---- | ---- | ---- |
| request_id | uint32 | 是 | |
| status_code | uint32 | 是 | |
| access_token | 字符串 | 否 | 成功时返回，绑定本连接的 Session id |
| username | 字符串 | 否 | 成功时返回；方便只保存了令牌的客户端确认身份 |

补充语义：

- 在**已登录**的 Session 上调用 `RECONNECT` 返回 `BECHAT_STATUS_ALREADY_LOGIN`
  （这种情况客户端应该用 `REFRESH`）；
- 同一个 `device_id` 允许多个连接同时存在（允许同设备多开），互相不影响。

### 5.4 状态码

新增：

```cpp
#define BECHAT_STATUS_REVOKED_REFRESH_TOKEN 0x010AU
```

语义：令牌本身合法（验签通过、未过期），但服务端已**不再认可**它
（同设备重新登录、被设备上限淘汰、记录丢失；将来的 LOGOUT 也会复用它）。
客户端收到它应当清除本地令牌并要求重新输密码。

保留并区分：

- `BECHAT_STATUS_EXPIRED_REFRESH_TOKEN 0x0108`：签名有效但已过期；
- `BECHAT_STATUS_INVALID_REFRESH_TOKEN 0x0109`：格式错误、签名错误、密钥/版本不匹配、claim 不一致。

判定表：

| 情况 | 状态码 |
| ---- | ------ |
| 令牌过期 | 0x0108 EXPIRED_REFRESH_TOKEN |
| 令牌被篡改 / 签名错 / 格式错 / ver 不匹配 / claim 不一致 | 0x0109 INVALID_REFRESH_TOKEN |
| 验签通过但服务端没有该 jti / 用户不存在 / 被淘汰 / 登出 | 0x010A REVOKED_REFRESH_TOKEN |
| 已登录连接调用 RECONNECT | 0x0104 ALREADY_LOGIN |
| device_id 超长 / 非法字符 | 0x0004 INVALID_PARAMS |

---

## 6. 实施步骤

> 建议严格按顺序实施，每一步结束后都能编译 + 跑通已有功能。
> 每步的"验收"是**做完该步立刻**可以检查的事情。

### Step 0：准备

1. 开新分支（例如 `feat/reconnect`），保证 `main` 上的既有功能可回退；
2. 先跑一次基线构建与手工冒烟，确认改动前就是可用的：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bin/bechat
```

3. 备份现有数据文件，避免迁移实验污染真实数据：

```bash
cp -a "$HOME/.config/bechat/user_records.db" \
      "$HOME/.config/bechat/user_records.db.manual-backup"
```

4. 记住两个路径：
   - 配置文件：`$HOME/.config/bechat/`（`Config::CfgPath()`）
   - 用户数据：`$HOME/.config/bechat/user_records.db`
5. 提醒：迁移完成后**所有旧刷新令牌都会失效**，需要重新登录一次。

### Step 1：协议常量

**文件**：`bechat/proto/message_tag.h`、`bechat/proto/status_code.h`

**改动**：

1. `message_tag.h` 增加 `BECHAT_TAG_RECONNECT 0x0006U`，注释写明"用刷新令牌在新连接上恢复登录"；
2. `status_code.h` 增加 `BECHAT_STATUS_REVOKED_REFRESH_TOKEN 0x010AU`，
   注释写明"令牌合法但已被服务端吊销，需重新登录"。

**验收**：编译通过；此步没有任何行为变化。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

---

### Step 2：请求参数与解析

**文件**：`bechat/proto/request_params.h`、`bechat/proto/request_factory.cpp`

**改动**：

1. `LoginParams` 增加 `std::string device_id`，构造函数增加第 4 个参数并给默认值
   （`std::string device_id = {}`），避免大范围改动调用点；
2. 新增 `ReconnectParams`：

```cpp
struct ReconnectParams {
  uint32_t request_id;
  std::string refresh_token;

  ReconnectParams(uint32_t req_id, std::string refresh_tok)
      : request_id(req_id), refresh_token(std::move(refresh_tok)) {}
};
```

3. `RequestParams` 的 `std::variant` 增加 `ReconnectParams`；
4. `RequestFactory::Parse`：
   - `BECHAT_TAG_LOGIN` 分支：`device_id` 可选，缺失或类型不对时取空串；
   - 新增 `BECHAT_TAG_RECONNECT` 分支：`val.at("refresh_token")`（必需）；
   - 解析层只做类型校验；长度/字符集校验放到 `UserRegisty::Login`（业务层），返回 `INVALID_PARAMS`。

参考写法（LOGIN 分支）：

```cpp
std::string device_id{};
if (val.contains("device_id") && val.at("device_id").is_string()) {
  device_id = val.at("device_id").get<std::string>();
}
LoginParams params(request_id, val.at("username"), val.at("password"),
                   std::move(device_id));
```

**验收**：

- 缺 `refresh_token` 的 RECONNECT 请求收到 `MALFORMED_PAYLOAD`；
- 不带 `device_id` 的 LOGIN 仍能正常解析（行为与现在一致）；
- 未知 tag 仍是 `UNSUPPORTED_TAG`。

---

### Step 3：令牌签发函数重构（顺带修一个现有 bug）

**文件**：`bechat/service/user_registry.cpp`

**先看一个现有 bug**：现在 `get_access_token_now(session)` 的 `sub`/`uname` 取的是
`session.Username()`，但 `Login()` 里是**先签发令牌、后 `SetAuthorized()`**，
所以签发出来的令牌 `sub`/`uname` 是空字符串——`Verify()`（要求 subject/uname 等于
`session->Username()`）永远不可能通过。改造时必须一并修掉。

**改动**：把两个静态函数改成不依赖 Session 当前状态的显式参数版本：

```cpp
static std::string make_access_token(const std::string& username,
                                     uint64_t session_id,
                                     std::chrono::system_clock::time_point now);

static std::string make_refresh_token(const std::string& username,
                                      const std::string& jti,
                                      const std::string& device_id,
                                      std::chrono::system_clock::time_point now);

static std::string random_jti();   // 见 4.1
static int64_t UnixSecondsNow();   // system_clock → unix 秒
```

- `make_access_token` 的 `jti` = `std::to_string(session_id)`，其余 claim 不变；
- `make_refresh_token` 的 `jti` = 参数 `jti`，新增 `did` claim；
- 两者都保留 `ver = CFG_VERSION` claim（Step 8 会真正校验它）。
- `user_registry.cpp` 需要新增的 include：`<chrono>`、`<optional>`、`<cstdint>`、`<cctype>`、
  `<nlohmann/json.hpp>`、`<sodium.h>`（libsodium 已在 `user_registry.h` 中间接引入，确认一下即可）。

**验收**：编译通过；此时行为与现在一致（因为 `Login` 还没改）。

---

### Step 4：内存数据结构与接口

**文件**：`bechat/service/user_registry.h`

**改动**：

1. 增加 `RefreshTokenEntry`（见 3.1）并扩展 `UserRecord`；
2. 增加常量 `kMaxDeviceIdSize`、`kMaxDevicesPerUser`；
3. `Login` 签名增加 `const std::string& device_id`（放在 `password` 之后）：

```cpp
uint32_t Login(const std::string& username, const std::string& password,
               const std::string& device_id,
               std::shared_ptr<SessionHandle> session,
               std::pair<std::string, std::string>* tokens = nullptr);
```

4. 新增 `Reconnect`：

```cpp
/**
 * @brief 用刷新令牌在新连接上恢复登录
 *
 * @param session 未登录的 Session
 * @param refresh_token 客户端持有的刷新令牌
 * @param [out] username 恢复出的用户名（可为空指针）
 * @param [out] access_token 新的访问令牌
 * @return 状态码
 */
uint32_t Reconnect(std::shared_ptr<SessionHandle> session,
                   const std::string& refresh_token, std::string* username,
                   std::string* access_token);
```

5. 增加私有 helper，并统一命名约定：**后缀 `_locked` 表示"调用方必须已持有 `user_records_mtx_`"**：

```cpp
UserRecord* find_user_locked(const std::string& username);
bool erase_expired_locked(UserRecord& rec, int64_t now);                  // 是否有删除
bool erase_device_locked(UserRecord& rec, const std::string& device_id);  // 是否有删除
void evict_oldest_locked(UserRecord& rec);
void record_write_file_locked();                                          // 全量原子写
```

6. 建议增加可注入路径的构造函数，方便写单元测试（现有代码把路径硬编码成 `Config::CfgPath()`）：

```cpp
explicit UserRegisty(
    std::string file_path = Config::CfgPath() + "/user_records.db");
```

**验收**：编译通过。此时 `server_context.cpp` 调用 `Login` 的地方会因为少一个参数而报错，
这是预期的，Step 9 会修；若希望每一步都能编译，可以先给 `device_id` 一个默认值
`= std::string{}`，Step 9 完成后再去掉。

---

### Step 5：持久化：序列化 / 反序列化 / 迁移

**文件**：`bechat/service/user_registry.cpp`

**改动**：

1. `serialize_user(const UserRecord&) -> nlohmann::json`：
   - `v = 1`、`username`、`password_hash`；
   - `refresh_tokens` 数组按 `created_at` 升序；
2. `record_write_file_locked()`：按 3.3 的原子写流程；用户按 `username` 升序输出；
3. `parse_user_line(const std::string& line) -> std::optional<UserRecord>`：
   - 去掉行尾 `\r`、跳过空行；
   - 首字符 `{` → JSONL 分支：`json::parse`，字段用 `value("key", default)` 容错读取；
   - 否则 → 旧格式分支：`istringstream iss(line); iss >> name >> hash;`
     构造 `UserRecord`（`refresh_tokens` 为空），并把 `legacy_seen` 置为 `true`；
   - 解析失败返回 `std::nullopt`，调用方记 `ERROR` 并跳过；
4. `record_read_file(const std::string& path)`（构造函数中调用，早于任何连接）：
   - 文件不存在直接返回；
   - 逐行解析并 `try_emplace`（同名保留先出现的记录，与现有行为一致）；
   - 若 `legacy_seen`：先把旧文件重命名为 `path + ".bak"`，再调用
     `record_write_file_locked()` 写出新格式（构造函数是单线程，无需加锁）；
5. 删除旧的 `record_append_file`（已被全量重写取代）。

反序列化时要做的防御：

- `refresh_tokens` 缺失或不是数组 → 视为空数组；
- 数组元素缺 `jti` → 跳过该元素并 `WARN`；
- `created_at` / `expires_at` 缺失 → 分别回退为 0 与 0（下一轮惰性清理会删除它）；
- 加载阶段可顺手丢掉 `expires_at <= now` 的元素（等同 3.5 的惰性清理）。

**验收**（可先用最小测试程序，或直接跑服务端）：

- 新装环境：登录后文件出现，内容为单行合法 JSON，包含 `jti` / `device_id` / `created_at` / `expires_at`；
- 造一个旧格式文件（`alice <hash>`）启动：用户仍能登录，文件被改写成 JSONL，
  同目录出现 `user_records.db.bak`；
- 重启服务器后内存中的 `refresh_tokens` 与文件一致（可临时加日志打印条数验证）。

---

### Step 6：`Login` 改造

**文件**：`bechat/service/user_registry.cpp`

**device_id 校验**（业务层，放在文件内静态 helper）：

```cpp
static bool IsValidDeviceId(const std::string& device_id) {
  for (unsigned char c : device_id) {
    const bool ok = std::isalnum(c) || c == '_' || c == '-';
    if (!ok) return false;
  }
  return device_id.size() <= UserRegisty::kMaxDeviceIdSize;   // 空串合法
}
```

**伪代码**：

```cpp
uint32_t UserRegisty::Login(const std::string& username,
                            const std::string& password,
                            const std::string& device_id,
                            std::shared_ptr<SessionHandle> session,
                            std::pair<std::string, std::string>* tokens) {
  if (!IsValidDeviceId(device_id)) return BECHAT_STATUS_INVALID_PARAMS;

  std::string refresh_token;
  {
    std::lock_guard guard(user_records_mtx_);

    auto* rec = find_user_locked(username);
    if (!rec) return BECHAT_STATUS_LOGIN_FAIL;
    if (crypto_pwhash_str_verify(rec->password_hash.c_str(), password.c_str(),
                                 password.length()))
      return BECHAT_STATUS_LOGIN_FAIL;

    const int64_t now = UnixSecondsNow();
    erase_expired_locked(*rec, now);

    // 同设备重新登录：旧令牌立即失效
    erase_device_locked(*rec, device_id);

    // 新 device_id 才会触发淘汰
    if (rec->refresh_tokens.size() >= kMaxDevicesPerUser) {
      evict_oldest_locked(*rec);
    }

    RefreshTokenEntry entry;
    entry.jti = random_jti();
    entry.device_id = device_id;
    entry.created_at = now;
    entry.expires_at = now + 30LL * 24 * 3600;
    rec->refresh_tokens.emplace(entry.jti, entry);

    refresh_token = make_refresh_token(username, entry.jti, device_id, now);
    record_write_file_locked();   // 状态已改动，必须落盘
  }   // ← 这里释放 registry 锁

  // 顺序很重要：先授权，再签发 access token
  session->SetAuthorized(username);
  if (tokens) {
    tokens->first = std::move(refresh_token);
    tokens->second =
        make_access_token(username, session->Id(), UnixSecondsNow());
  }
  return BECHAT_STATUS_SUCCESS;
}
```

**注意点**：

1. **不要持锁调用 `session->*`**，锁范围尽量小（规则：registry 锁是最内层锁）；
2. 先 `SetAuthorized` 再签发 access token（顺带修掉 Step 3 提到的 bug）；
3. 插入新令牌本身就代表状态已变，必须落盘，不要只依赖 `dirty` 判断；
4. `device_id` 为空串是合法输入；
5. 密码校验失败时**不要**改动任何 RTE（避免攻击者用错误密码把别人踢下线）。

**验收**：见第 7 章用例 1–3、6–7。

---

### Step 7：`Reconnect` 实现

**文件**：`bechat/service/user_registry.cpp`

**伪代码**：

首先抽一个"解码 + 验签"helper，`Reconnect` 与 `Refresh` 共用（`decoded_jwt` 没有默认构造函数，
所以用 `std::optional` 承载返回值）：

```cpp
static std::optional<jwt::decoded_jwt<jwt::traits::kazuho_picojson>>
decode_verify_refresh_token(const std::string& token, uint32_t* status_code) {
  try {
    auto decoded = jwt::decode(token);   // 格式错误会抛异常

    std::error_code ec;
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{CFG_JWT_SECRET_REFRESH})
        .leeway(30)
        .with_issuer("BeChat")
        .with_audience("bechat")
        .with_claim("use", jwt::claim(std::string{"refresh"}))
        .with_claim("ver", jwt::claim(std::string{CFG_VERSION}))
        .verify(decoded, ec);   // 注意：不要 with_subject / with_id

    if (ec) {
      *status_code =
          static_cast<jwt::error::token_verification_error>(ec.value()) ==
                  jwt::error::token_verification_error::token_expired
              ? BECHAT_STATUS_EXPIRED_REFRESH_TOKEN
              : BECHAT_STATUS_INVALID_REFRESH_TOKEN;
      return std::nullopt;
    }
    return decoded;
  } catch (const std::exception& e) {
    // 客户端输入问题，不能上报 INTERNAL_ERROR
    WARN("invalid refresh token: {}", e.what());
    *status_code = BECHAT_STATUS_INVALID_REFRESH_TOKEN;
    return std::nullopt;
  }
}
```

然后 `Reconnect` 本身：

```cpp
uint32_t UserRegisty::Reconnect(std::shared_ptr<SessionHandle> session,
                                const std::string& refresh_token,
                                std::string* username,
                                std::string* access_token) {
  uint32_t verify_status = BECHAT_STATUS_SUCCESS;
  auto decoded = decode_verify_refresh_token(refresh_token, &verify_status);
  if (!decoded) return verify_status;

  const std::string uname = decoded->get_subject();
  const std::string jti = decoded->get_id();
  if (uname.empty() || jti.empty()) return BECHAT_STATUS_INVALID_REFRESH_TOKEN;

  if (decoded->has_payload_claim("uname") &&
      decoded->get_payload_claim("uname").as_string() != uname) {
    return BECHAT_STATUS_INVALID_REFRESH_TOKEN;
  }

  const int64_t now = UnixSecondsNow();
  std::string device_id;
  {
    std::lock_guard guard(user_records_mtx_);

    auto* rec = find_user_locked(uname);
    if (!rec) return BECHAT_STATUS_REVOKED_REFRESH_TOKEN;

    if (erase_expired_locked(*rec, now)) record_write_file_locked();

    auto it = rec->refresh_tokens.find(jti);
    if (it == rec->refresh_tokens.end())
      return BECHAT_STATUS_REVOKED_REFRESH_TOKEN;
    if (it->second.expires_at <= now)
      return BECHAT_STATUS_EXPIRED_REFRESH_TOKEN;

    // did 一致性校验（可选但推荐）
    if (decoded->has_payload_claim("did")) {
      const std::string did = decoded->get_payload_claim("did").as_string();
      if (did != it->second.device_id)
        return BECHAT_STATUS_INVALID_REFRESH_TOKEN;
    }
    device_id = it->second.device_id;

    // 注意：不改动条目 —— 不轮换、不更新 expires_at
  }

  session->SetAuthorized(uname);   // 锁外
  if (username) *username = uname;
  if (access_token) *access_token = make_access_token(uname, session->Id(), now);
  INFO("Reconnect user {} device {} session {}", uname, device_id, session->Id());
  return BECHAT_STATUS_SUCCESS;
}
```

**注意点**：

1. `verify()` 里**不能**再用 `with_subject(session->Username())` / `with_id(...)`，
   新连接还没有用户名；改成"验签通过后读 claim"；
2. 用户不存在、条目不存在都返回 `REVOKED_REFRESH_TOKEN`（对客户端而言动作一样：重新登录）；
3. 日志：成功 `INFO`/`DEBUG`，吊销 `WARN`（带 username、jti 前 8 位、device_id），
   **不要打印完整令牌**；
4. 查表通过到 `SetAuthorized` 之间存在极小竞态窗口（另一线程同设备登录会吊销该条目），
   属于可接受竞态，见第 8 章。
5. `decode_verify_refresh_token` 建议作为文件内静态函数（anonymous namespace），
   Step 8 的 `Refresh` 直接复用它，避免两处校验逻辑漂移。

**验收**：见第 7 章用例 4、5、8、9、14。

---

### Step 8：`Refresh` / `Verify` 适配

**文件**：`bechat/service/user_registry.cpp`

**改动**：

1. `Refresh`（已登录连接的续签）：
   - 复用 Step 7 的 `decode_verify_refresh_token`，再走"读 sub / jti → 查表"的流程，
     并要求 sub 与 `session->Username()` 一致；
   - 命中白名单才签发新 access token；
   - **去掉失败时的 `session->SetUnauthorized()`**：一次写错的令牌不该打掉整个会话，
     由客户端根据状态码决定是重连还是重新登录；
   - 去掉 `with_id(session->Username())`（jti 已经不是 username）。
2. `Verify`：
   - 增加 `.with_claim("ver", jwt::claim(std::string{CFG_VERSION}))`；
   - `jwt::decode` 抛异常时返回 `BECHAT_STATUS_INVALID_ACCESS_TOKEN`，而不是 `INTERNAL_ERROR`；
3. 三个函数遵守同一原则：**客户端输入问题绝不上报 `INTERNAL_ERROR`**。

**验收**：

- 用升级前的旧刷新令牌调用 REFRESH → `REVOKED_REFRESH_TOKEN`；
- 用错误 secret 签发的令牌 → `INVALID_REFRESH_TOKEN`；
- 用已过期令牌 → `EXPIRED_REFRESH_TOKEN`。

---

### Step 9：`ServerContexts` 接入

**文件**：`bechat/core/server_context.h`、`bechat/core/server_context.cpp`

**改动**：

1. 头文件在 `handle_refresh` 之后声明：

```cpp
void handle_reconnect(std::shared_ptr<SessionHandle>, ReconnectParams);
```

2. `OnSessionMessage` 的 `std::visit` 增加分支：

```cpp
[&](ReconnectParams p) { handle_reconnect(session, p); },
```

3. `handle_login` 把 `p.device_id` 传给 `user_registry_.Login(...)`；
4. 新增 `handle_reconnect`，结构照抄 `handle_refresh`，但判断逻辑相反：

```cpp
std::string username{};
std::string access_token{};
uint32_t status_code =
    session->IsAuthorized()
        ? BECHAT_STATUS_ALREADY_LOGIN
        : user_registry_.Reconnect(session, p.refresh_token, &username,
                                   &access_token);

nlohmann::json jvalue = nlohmann::json::object();
jvalue["request_id"] = p.request_id;
jvalue["status_code"] = status_code;
if (status_code == BECHAT_STATUS_SUCCESS) {
  jvalue["access_token"] = std::move(access_token);
  jvalue["username"] = std::move(username);
}

auto resp = ResponseFactory::MakeResponse(BECHAT_TAG_RECONNECT, jvalue);
session->Send(std::move(resp));
```

5. 异常路径沿用现有风格：`ERROR` 日志 +
   `MakeError(BECHAT_TAG_RECONNECT, p.request_id, BECHAT_STATUS_INTERNAL_ERROR)`。

**验收**：编译通过，进入 Step 10 做全流程手工验证。

---

### Step 10：编译与手工联调

**构建**：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

产物在 `build/bin/`：`bechat`（服务端）、`tlv_sender`（手工测试工具）。

**启动服务端**：

```bash
./build/bin/bechat
```

配置文件与数据文件都在 `$HOME/.config/bechat/`（首次启动自动生成配置）。
测试前确认 `jwt.secret.access` / `jwt.secret.refresh` 已改成非默认值。

**用 `tlv_sender` 手工发请求**（默认连 `localhost:35565`）：

```bash
./build/bin/tlv_sender
```

输入格式为 `<tag> <length> <value>`，`length` 是 **value 的字节数**。
工具按行读取，所以 JSON 必须写成**一行且不带多余空格**。下面这个小函数可以生成输入行：

```bash
send() {
  local tag=$1
  shift
  local json="$*"
  printf '%s %s %s\n' "$tag" "$(printf '%s' "$json" | wc -c)" "$json"
}

send 0x0001 '{"request_id":1,"username":"alice","password":"secret"}'
send 0x0002 '{"request_id":2,"username":"alice","password":"secret","device_id":"dev-a"}'
send 0x0006 '{"request_id":3,"refresh_token":"<上一步返回的 refresh_token>"}'
```

把 `send` 打印出来的行粘贴给 `tlv_sender`，它会把响应以十六进制回显；
把 value 部分的十六进制还原成 JSON 就能看到 `status_code` 与 `access_token`。

**完成标准**：第 7 章全部用例通过。

---

### Step 11：文档同步

**文件**：`docs/Proto.md`

**改动**：

1. "用户登录"请求报文表增加 `device_id` 行（可选，说明语义与格式限制）；
2. 新增"用户重连"章节：`Tag: 0x0006`，请求 / 响应字段表，说明
   "成功后可继续使用本连接"、"不轮换刷新令牌"、"30 天硬过期"；
3. "用户刷新访问令牌"章节补充：**必须**在已登录连接上调用，未登录连接请用重连；
4. 新增状态码说明：`0x0108`（过期）/ `0x0109`（非法）/ `0x010A`（已被吊销）；
5. 全局补充一句：刷新令牌是**多设备**的，不同 `device_id` 互不影响。

---

### Step 12：清理与回归

**文件**：`bechat/service/user_registry.cpp`、可选 `docs/DesignLine.md`

**改动**：

1. 删除 `user_registry.cpp` 中的 `// [TODO] restore refresh_token`；
2. 用项目自带的 `.clang-format` 格式化改动过的文件：

```bash
clang-format -i bechat/service/user_registry.cpp \
  bechat/service/user_registry.h bechat/core/server_context.cpp \
  bechat/core/server_context.h bechat/proto/request_factory.cpp \
  bechat/proto/request_params.h
```

3. 项目开启 `-Wall -Werror`，确认零警告；
4. 回归已有功能：注册、登录、重复登录返回 `ALREADY_LOGIN`、未知 tag 报错；
5. 可选：在 `docs/DesignLine.md` 的"基础架构"里补一句"用户数据与刷新令牌持久化"。

---

## 7. 测试用例矩阵

### 7.1 手工 / 联调用例

| # | 步骤 | 期望结果 |
| - | ---- | -------- |
| 1 | 注册 alice | `SUCCESS`，文件出现 alice 行 |
| 2 | 用 `device_id=dev-a` 登录 | `SUCCESS`，返回 access + refresh |
| 3 | 同一连接再次登录 | `ALREADY_LOGIN` |
| 4 | 新连接用 RT_A 调 RECONNECT | `SUCCESS`，返回新 access_token 与 username |
| 5 | 在步骤 4 的连接上再发 LOGIN | `ALREADY_LOGIN`（证明会话已被授权） |
| 6 | 用 `device_id=dev-b` 登录；再用 RT_A 重连 | 全部 `SUCCESS`（多设备互不影响） |
| 7 | 用 `device_id=dev-a` 重新登录，再用旧 RT_A 重连 | `REVOKED_REFRESH_TOKEN`；新 RT_A2 可用 |
| 8 | 篡改令牌任意一字符后重连 | `INVALID_REFRESH_TOKEN` |
| 9 | 临时把刷新令牌有效期改成 5 秒，等过期后重连（测完改回） | `EXPIRED_REFRESH_TOKEN` |
| 10 | 重启服务端后用 RT 重连 | `SUCCESS`（持久化生效） |
| 11 | 用旧格式 `username hash` 文件启动 | 可登录；文件变 JSONL；出现 `.bak` |
| 12 | 两个连接同时用同一 RT 重连 | 都 `SUCCESS`（不轮换，重复使用合法） |
| 13 | 依次登录 dev-1 … dev-9 | dev-1 被淘汰，用其 RT 重连返回 `REVOKED_REFRESH_TOKEN` |
| 14 | 使用升级前签发的旧令牌（jti=username） | `REVOKED_REFRESH_TOKEN` |
| 15 | 已登录连接用 RT 调 REFRESH | `SUCCESS`，返回新 access_token |
| 16 | 已登录连接用被吊销的 RT 调 REFRESH | `REVOKED_REFRESH_TOKEN`，且会话保持可用 |
| 17 | 空 device_id 登录两次 | 第二次使第一次的 RT 失效（等价单设备） |

### 7.2 自动化测试建议

`test/CMakeLists.txt` 已提供 `add_ctest_executable` 宏，建议新增 `test/test_user_registry.cpp`，
用可注入路径的构造函数指向 `std::filesystem::temp_directory_path()` 下的临时文件，覆盖：

1. `Login` → 落盘 → 重新构造 `UserRegisty` → `Reconnect` 成功（持久化往返）；
2. 同 device 重新登录 → 旧 jti 重连失败；
3. 两个不同 device → 两条 RTE，互不影响；
4. 超过 `kMaxDevicesPerUser` → 最旧条目被淘汰；
5. 旧格式文件迁移（写入 `alice <hash>` 后构造，检查 `.bak` 与新文件内容）；
6. 序列化 → 反序列化 → 序列化，结果一致（幂等）。

> 注意：`UserRegisty` 的默认路径依赖 `Config::CfgPath()`，而它读 `$HOME`。
> 测试里要么注入路径，要么把 `HOME` 指向临时目录，并确保 `Global::Init()` 已被调用
> （libsodium 初始化，`randombytes_buf` 依赖它）。

---

## 8. 已知取舍与风险

1. **不轮换 = 被盗令牌 30 天内一直可用**。只有"同设备重新登录 / 登出 / 设备淘汰 / 记录丢失"
   能让它失效。刷新令牌实质上等同于"30 天有效期的密码"，
   因此**强烈建议在客户端上使用 TLS 端口**（`SslServer` 已存在，端口 `server.ssl_port`）。
2. **设备上限淘汰是静默的**（只写 `INFO` 日志），被淘汰的设备下次重连才知道。
3. **同设备多连接共享同一令牌**：重新登录或登出会让该设备所有连接的重连能力一起失效
   （已建立的连接本身不受影响，access token 绑的是 session）。
4. **全量重写文件**：复杂度 O(用户总数)，当前规模可接受；
   用户量上千后要改成 append-only + compaction。
5. **查表与授权之间的竞态**：查表通过后、`SetAuthorized` 前，另一个线程可能刚好吊销该条目，
   于是"刚验证通过的令牌"仍会被授权。极小概率且影响有限；
   若要彻底消除，需要在锁内完成 `SetAuthorized`（会引入跨锁调用，不推荐）。
6. **持久化失败不回滚内存状态**：只记 `CRITICAL` 日志。重启后可能丢失最近的吊销记录，
   表现为"本该失效的令牌又能用了"。若不能接受，需要改成"写失败即拒绝服务/拒绝该次登录"。
7. **access token 目前没有被真正使用**：`UserRegisty::Verify()` 尚无调用方，
   `RECONNECT` 只是把令牌发给客户端；将来要把校验接入业务 tag 时需要另立计划。

---

## 9. 后续可选工作（按优先级）

1. **LOGOUT（0x0003）**：删除该 `device_id` 的 RTE，让"登出"真正具备服务端吊销能力；
2. **令牌轮换 + grace 窗口 + 复用检测**：缩短泄露窗口（注意丢包与多连接的误判问题）；
3. **业务 tag 接入 access token 校验**（启用 `Verify`，统一未授权请求的处理）；
4. **把令牌有效期、设备上限移到配置文件**（`auth.refresh_expire_days`、`auth.max_devices`）；
5. **连接注册表**：新登录时可主动通知/断开旧连接；
6. **磁盘写入优化**：append-only 变更日志 + 周期性全量 compaction。

---

## 10. 附：本计划顺带修复的现有问题

1. `Login()` 先签发令牌、后 `SetAuthorized()`，导致令牌 `sub`/`uname` 为空
   → `Verify()` 永远不可能通过（Step 3 / Step 6 修复）；
2. `Refresh()` 失败即 `SetUnauthorized()`，一次错误输入就打掉整个会话（Step 8 修复）；
3. `Verify()` / `Refresh()` 把 `jwt::decode` 异常一律上报 `INTERNAL_ERROR`，
   客户端输入错误却记成服务端故障（Step 8 修复）；
4. `ver` claim 写了但从未校验，跨版本升级时旧令牌仍然可用（Step 8 补上）；
5. `handle_refresh()` 要求 `IsAuthorized()`，导致新连接无法用刷新令牌恢复登录
   （Step 9 由 `RECONNECT` 解决）；
6. access token 的 `jti = session id`，而 session id 在每次进程启动后从 1 重新分配，
   老的 access token 理论上可能与新会话撞号（Step 8 的 `ver` 校验可缓解；
   彻底解决需要把"服务启动随机数"写进 access token，另行设计）。
