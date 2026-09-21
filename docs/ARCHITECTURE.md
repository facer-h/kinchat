# 架构与设计说明

本文档描述「即时通信网络聊天室（KinChat）」服务端的整体架构与关键设计。

## 1. 架构定位

- **纯内存、零数据库**：在线用户、聊天室成员、消息均在内存中维护，重启即清空；无账号体系，身份为一次性昵称。
- **单进程、单 `io_context`**：所有异步 I/O 统一由一个 `boost::asio::io_context` 调度。
- **长连接**：TCP 持久连接，客户端通过心跳保活维护在线状态。

## 2. 模块划分

```
ChatService  ── 服务调度层：接受连接、维护在线 / 群聊集合、消息路由、会话清理
    │
    └── Session  ── 连接会话层：单个对端的异步读写、写队列、广播
```

### 2.1 `Session`（连接会话层）

每个客户端连接对应一个 `Session`，继承 `std::enable_shared_from_this` 以安全地参与异步回调。

| 成员 | 职责 |
|---|---|
| `tcp::socket socket_` | 持有的客户端套接字 |
| `asio::streambuf buffer_` | 异步读缓冲 |
| `std::deque<std::shared_ptr<std::string>> writeQueue_` | 写队列，串行化异步发送 |
| `std::string name_` | 会话昵称 |
| `bool running_` | 运行状态 |

| 方法 | 职责 |
|---|---|
| `doRead()` | 异步读取对端数据 |
| `doWrite()` | 将写队列中的数据异步发送出去 |
| `BroadCast(msg)` | 向群聊中的所有客户端广播消息 |

回调类型：

- `handleMsg`：`void(const sessionPtr&, const std::string&)`，业务消息（路由到 `ChatService::handleMessage`）。
- `closeSession`：`void(const sessionPtr&)`，连接关闭回调。

### 2.2 `ChatService`（服务调度层）

| 成员 | 职责 |
|---|---|
| `asio::io_context io_context_` | 异步任务总调度 |
| `tcp::acceptor acceptor_` | 监听并接受新连接 |
| `tcp::socket socket_` | 新连接套接字 |
| `std::set<User> clients_` | 在线用户集合 |
| `std::set<Conservation> Conservations` | 群聊（聊天室）集合 |
| `bool isRunning_` | 服务运行状态 |

| 方法 | 职责 |
|---|---|
| `doAccept()` | 异步等待并接受新的客户端连接 |
| `handleMessage(session, msg)` | 处理客户端发来的包（消息路由） |
| `closeSession(session)` | 关闭并移除指定会话 |

### 2.3 数据结构

```cpp
struct User {
    std::string name;                        // 名字
    int userId_;                             // 用户 id
    boost::shared_ptr<Session> clients_;     // 指向客户端的共享指针
};

struct Conservation {                        // 群聊
    int userID_;                             // 群聊标识
    std::set<User> clients_;                 // 群聊中的用户
};
```

## 3. 核心流程

### 3.1 连接建立与进入

```
客户端 ──TCP 连接──▶ acceptor.doAccept()
                          │
                    创建 Session ──▶ doRead() 等待昵称
                          │
                    校验昵称：非空 / 格式合法 / 未被占用
                          │
                    加入 clients_ ──▶ 广播"xx 进入聊天室" ──▶ 启动心跳
```

### 3.2 消息路由

客户端发来消息后，`Session::doRead` 解析出完整包，交给 `handleMessage` 按消息类型分发：

| 消息类型 | 处理逻辑 |
|---|---|
| `join` | 进入聊天室（昵称认证） |
| `leave` | 离开聊天室 / 主动登出 |
| `heartbeat` | 刷新最后心跳时间 |
| `private_msg` | 校验目标在线 → 实时推送；目标已离开则拒绝并回错误 |
| `group_msg` | 遍历群聊 / 全体在线成员逐一转发 |
| `system_msg` | 系统广播 |

### 3.3 写队列与异步写

发送端不直接写 socket，而是把消息压入 `writeQueue_`，由 `doWrite` 逐个串行异步发送，避免多消息并发写导致的交织与竞态。

### 3.4 心跳与在线状态

```
客户端 ──每 30s──▶ heartbeat ──▶ 刷新"最后心跳时间"
服务器扫描（每 10s）── 超过 90s（3 个周期）未心跳 ──┐
                                                  ▼
                             判定已离开 ──▶ closeSession() ──▶ 移出 clients_
```

参数均可配置（心跳间隔、超时阈值、扫描周期）。

## 4. 设计要点与取舍

- **只与在线用户聊天**：私聊目标必须在线，服务器强制校验，不设离线消息（避免引入持久化）。
- **会话级联系人**：联系人随会话建立、随离开清空；对方离开后仅标记状态、不删除记录（本会话内）。
- **写队列串行化**：以 `std::deque<std::shared_ptr<std::string>>` 串行发送，简化并发控制。
- **预留扩展**：存储层与账号体系均预留抽象，未来可平滑接入数据库、账号体系与 TLS。

## 5. 消息协议（建议）

长连接采用 TCP 或 WebSocket，消息为 JSON 结构化协议：

```jsonc
{ "type": "join",         "nickname": "..." }   // 进入
{ "type": "leave" }                              // 离开
{ "type": "heartbeat" }                          // 心跳
{ "type": "private_msg",  "to": "...", "content": "..." }  // 私聊
{ "type": "group_msg",    "content": "..." }     // 群聊
{ "type": "system_msg",   "content": "..." }     // 系统广播
```

服务器对每种消息类型做合法性校验，未认证连接、目标离线、格式非法等情况拒绝处理并返回错误响应。