# 目前逻辑的一些待改造项

## 设计 RequestCodec 及其相关逻辑

### 目标

RequestCodec 解析的返回值需要以一种方便且高效的形式统一返回：

- **方便**：一个入口、一次错误判断、一次 `std::visit` 分发，业务侧拿到强类型参数，不再手动偏移解析。
- **高效**：解析路径零堆分配、零拷贝。

### 统一返回值

采用 C++23 的 `std::expected` + `std::variant`（见 `bechat/proto/request_codec.h`）：

```cpp
struct RegisterParams      { std::string_view username; std::string_view password; };
struct LoginParams         { std::string_view username; std::string_view password; };
struct GetHistoryParams    { uint64_t before_seq; uint16_t limit; };
struct GetOnlineUsersParams {};                                  // 无参数字段
struct SendMessageParams   { std::string_view content; };

using RequestParams = std::variant<RegisterParams, LoginParams,
                                   GetHistoryParams, GetOnlineUsersParams,
                                   SendMessageParams>;

using DecodeOutcome = std::expected<RequestParams, StatusCode::Type>;

class RequestCodec {
 public:
  static DecodeOutcome Decode(uint16_t tag, std::string_view payload);
};
```

设计要点：

- 成功/失败通道分离：`if (!outcome)` 处理错误，`*outcome` 直接 visit 业务分支。
- 公共头 `request_id` 不进 variant，由 `RequestMessage::request_id()` 提供，避免每个参数结构体重复携带。
- 原草稿 `DecodedRequest` 废弃删除。

### Decode 结构解析（`bechat/proto/request_codec.cpp`）

- 基于现有 `ValueReader` 的 `ReadStringView` / `ReadInt` 零拷贝读取 `payload`（已排除 request_id 的剩余 value）。
- Register / Login → username + password；GetHistory → before_seq + limit；SendMessage → content；GetOnlineUsers → 无字段。
- 读完必须 `reader.Done()`：字段不足或存在多余字节 → `unexpected(MalformedPacket)`（对应 TlvProto §6「请求 Value 必须恰好被各字段完整消费」）。
- 未知 tag 防御性返回 `MalformedPacket`。
- 只做结构解析，**不做语义校验**（用户名长度、limit 范围、非空、UTF-8 等），保证 TlvProto §7 中「状态检查先于参数校验」的检查顺序，语义校验留待后续接入。

### 字符串生命周期

参数字段用 `std::string_view` 借用 `RequestMessage` 的 value buffer。安全性依据：`HandleRequest` 的 lambda 按值捕获 `request` shared_ptr，Decode 与 visit 分发全程同步完成于该作用域内。需在 `Decode` 注释中以 `@attention` 标明「仅在 HandleRequest 同步处理期间有效」。

### HandleRequest 消费流程（`bechat/core/server_context.cpp`）

1. 保留现有前置检查：非法 tag 或 request_id 不存在 → `Resp::Error(MalformedPacket)`。
2. `auto outcome = RequestCodec::Decode(request->tag(), request->payload());`
   - 失败 → `Resp::Error(MalformedPacket)`。
   - 成功 → `std::visit(overloaded{...}, *outcome)` 分发到 5 个业务分支（暂为占位，保持当前测试响应行为）。
3. 文件内定义 `overloaded` 辅助模板（含推导指引）。

### 待办清单

- [ ] 重写 `bechat/proto/request_codec.h`：参数结构体 + `RequestParams` + `DecodeOutcome` + `Decode` 声明
- [ ] 新建 `bechat/proto/request_codec.cpp`：按 tag 的结构解析
- [ ] 改造 `bechat/core/server_context.cpp` 的 `HandleRequest`：Decode + visit 分发
- [ ] `bechat/CMakeLists.txt` 注册 `proto/request_codec.cpp`
- [ ] 编译验证（`-Wall -Werror`），按 `test/TestCMD.md` 用 `nc` 手工验证合法与畸形请求

### 不在本阶段范围

语义参数校验（InvalidParameter）、登录状态检查、业务逻辑实现、`RequestResult`（to_self_response / to_broadcast_push）的接线，留待后续改造项。
