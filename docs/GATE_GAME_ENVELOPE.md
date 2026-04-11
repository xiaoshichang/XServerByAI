# GATE_GAME_ENVELOPE

本文档定义 XServerByAI 当前主线代码中 `Game <-> Gate` 运行期中继消息的结构与语义。当前活跃的中继协议以 `RelayCodec.h` 为准，重点覆盖 mailbox、proxy 与客户端推送三条链路。

## 当前生效范围

1. 当前已实现并在节点代码中实际消费的 relay `msgId` 为：
   - `2001` `Relay.PushToClient`
   - `2002` `Relay.ForwardMailboxCall`
   - `2005` `Relay.ForwardProxyCall`
2. `2000` `kRelayForwardToGameMsgId` 目前只保留在头文件中，主线节点逻辑没有接通，不应视为当前有效运行期协议。
3. `2003` / `2004` 是 Gate 与 Game 之间的 avatar 创建请求/结果消息，但它们不是 `RelayCodec` 结构，不属于本文约束范围。

## 共享编码约定

1. 所有 relay 负载都承载在外层 `PacketHeader + payload` 内，消息边界由 ZeroMQ 保证。
2. 当前三种 relay 消息都要求：
   - 外层 `PacketHeader.flags = 0`
   - 外层 `PacketHeader.seq = 0`
3. 所有整数按网络字节序编码。
4. 所有字符串字段都使用 `uint16 byteLength + UTF-8 bytes` 编码。
5. 业务负载统一使用 `uint32 payloadLength + payload bytes` 编码。
6. `relayFlags` 当前必须为 `0`。
7. 编解码失败、字段非法或目标不可达时，当前实现以本地日志与丢弃处理为主，没有额外定义协议级 relay 错误响应包。

## Relay.ForwardMailboxCall (`msgId = 2002`)

### 用途

1. 供 `Game` 发起 mailbox / stub 调用，并通过 `Gate` 转发到目标 `Game`。
2. 当前常见来源有：
   - managed `forward_stub_call` 回调
   - `GM` 转发到 `OnlineStub` 的管理消息

### 结构

| Field | Type | Description |
| --- | --- | --- |
| `sourceGameNodeId` | `string16` | 发起调用的源 `GameNodeId`，不能为空 |
| `targetGameNodeId` | `string16` | 目标 `GameNodeId`，不能为空 |
| `targetEntityId` | `string16` | 目标实体 GUID；可为空 |
| `targetMailboxName` | `string16` | 目标 mailbox 名；当 `targetEntityId` 为空时必须非空 |
| `mailboxCallMsgId` | `uint32` | mailbox 调用消息号，必须非零 |
| `relayFlags` | `uint32` | 当前必须为 `0` |
| `payloadLength` | `uint32` | 负载长度 |
| `payload` | `bytes` | mailbox 调用负载 |

### 语义约束

1. `sourceGameNodeId` 与 `targetGameNodeId` 必须是非空 `NodeID`。
2. `targetEntityId` 如果存在，必须是 canonical GUID 字符串。
3. `targetMailboxName` 与 `targetEntityId` 允许同时出现；但如果 `targetEntityId` 为空，则 `targetMailboxName` 不能为空。
4. `Gate` 当前只负责：
   - 校验来源连接与 `sourceGameNodeId` 一致
   - 确认目标 `Game` 已注册且可转发
   - 原样把中继包发送到目标 `Game`
5. `Gate` 不参与 mailbox 业务分发，也不重写 payload。

## Relay.ForwardProxyCall (`msgId = 2005`)

### 用途

1. 供 `Game` 发起面向可迁移实体的 proxy 调用。
2. `routeGateNodeId` 指明“当前持有该实体会话目录的 Gate”；由该 Gate 决定目标实体当前应该转发到哪个 `Game`。

### 结构

| Field | Type | Description |
| --- | --- | --- |
| `sourceGameNodeId` | `string16` | 发起调用的源 `GameNodeId`，不能为空 |
| `routeGateNodeId` | `string16` | 路由 Gate 的 `NodeID`，不能为空 |
| `targetEntityId` | `string16` | 目标实体 GUID，必须是 canonical GUID |
| `proxyCallMsgId` | `uint32` | proxy 调用消息号，必须非零 |
| `relayFlags` | `uint32` | 当前必须为 `0` |
| `payloadLength` | `uint32` | 负载长度 |
| `payload` | `bytes` | proxy 调用负载 |

### 语义约束

1. `routeGateNodeId` 必须等于实际接收该包的 `Gate` `NodeID`；否则当前实现会拒绝处理。
2. `Gate` 使用 `targetEntityId` 在本地 `avatarId -> sessionId` 索引中查找活跃会话。
3. 查到会话后，`Gate` 会从对应 `ClientSessionRecord.game_node_id` 解析目标 `Game`，再把 relay 包原样转发给该 `Game`。
4. 如果目标 avatar 会话不存在、已关闭或目标 `Game` 不可达，当前实现只记录日志，不回送协议级失败包。

## Relay.PushToClient (`msgId = 2001`)

### 用途

1. 供 `Game` 通过 `Gate` 向客户端推送单向消息。
2. 当前 managed `push_client_message` 回调最终会编码为该结构。

### 结构

| Field | Type | Description |
| --- | --- | --- |
| `sourceGameNodeId` | `string16` | 发起推送的源 `GameNodeId`，不能为空 |
| `routeGateNodeId` | `string16` | 目标 `Gate` `NodeID`，不能为空 |
| `targetEntityId` | `string16` | 目标 avatar GUID，必须是 canonical GUID |
| `clientMsgId` | `uint32` | 下发给客户端的消息号，必须非零 |
| `relayFlags` | `uint32` | 当前必须为 `0` |
| `payloadLength` | `uint32` | 负载长度 |
| `payload` | `bytes` | 客户端消息体 |

### 语义约束

1. `Gate` 当前通过 `targetEntityId` 定位本地活跃 avatar 会话。
2. 找到会话后，`Gate` 会重新封装客户端侧 `PacketHeader`：
   - `msgId = clientMsgId`
   - `seq = 0`
   - `flags = 0`
3. 当前没有为推送定义协议级 ACK、重试或压缩位语义。

## 当前实现边界

1. relay codec 只定义 `Game <-> Gate` 的负载编码，不定义客户端侧 JSON 或业务对象结构。
2. 运行期错误目前主要由节点本地日志承担，没有为 relay 流量定义单独的对外 `errorCode` 响应协议。
3. `Gate` 当前只维护 avatar 会话路由；没有实现通用的“任意客户端消息统一转发到 Game 再统一回包”的老式 `ForwardToGame` 方案。

## 关联文档

1. 路由与会话模型见 [SESSION_ROUTING.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/SESSION_ROUTING.md)。
2. `msgId` 登记见 [MSG_ID.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/MSG_ID.md)。
3. managed 回调与 runtime 约定见 [MANAGED_INTEROP.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/MANAGED_INTEROP.md)。
