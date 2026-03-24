# PROCESS_INNER

本文档定义 XServerByAI 当前阶段启动编排相关 `Inner` 消息的结构、字段语义与时序约束。当前启动控制面分成两段：
1. `Gate/Game -> GM`：完成节点注册、心跳、ownership 下发与 ready 聚合。
2. `Game -> Gate`：完成 `Game` 到每个 `Gate` 的注册与心跳闭环，为后续 Gate→Game 转发建立可用目标目录。

所有消息默认承载在 ZeroMQ over TCP 的 `Inner` 链路上；消息边界、包头与 flags 规则以 `docs/PACKET_HEADER.md` 为准。

**适用范围**
1. 当前文档覆盖 `Inner.NodeRegister`（`1000`）、`Inner.NodeHeartbeat`（`1100`）、`Inner.ClusterReadyNotify`（`1201`）、`Inner.ServerStubOwnershipSync`（`1202`）与 `Inner.GameServiceReadyReport`（`1203`）。
2. `Inner.NodeRegister` / `Inner.NodeHeartbeat` 是共享消息：既用于 `Gate/Game -> GM`，也用于 `Game -> Gate` 的启动期闭环。

**通用编码约定**
1. 所有整数字段统一使用网络字节序。
2. `string` 使用 `uint16 byteLength + UTF-8 bytes` 编码；单字段建议不超过 `1024` 字节。
3. `string[]` 使用 `uint16 count + repeated string` 编码；单数组建议不超过 `32` 项。
4. 结构体数组使用 `uint16 count + repeated <entry>` 编码；单次消息建议不超过 `256` 项。
5. `bool` 使用 `uint8` 表示：`0 = false`，`1 = true`。
6. 所有时间戳字段统一使用 `unixTimeMsUtc` 语义。
7. 当前未定义的保留字段、保留位与 `statusFlags` 必须为 `0`。

**枚举：ProcessType**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Gate` | 客户端接入、会话与转发节点 |
| `2` | `Game` | 业务逻辑、实体与本地 ready 宿主节点 |

`GM` 是当前 `Inner` 编排面的协调者，但不通过 `ProcessType` 出现在注册请求体中。

**结构：Endpoint**

| Field | Type | Description |
| --- | --- | --- |
| `host` | `string` | 对外发布的主机地址，可为 IP 或可解析主机名 |
| `port` | `uint16` | 对外发布的监听端口，`0` 为非法 |

**结构：LoadSnapshot**

| Field | Type | Description |
| --- | --- | --- |
| `connectionCount` | `uint32` | 当前内部链路连接数；未知时为 `0` |
| `sessionCount` | `uint32` | 当前会话数量；未知时为 `0` |
| `entityCount` | `uint32` | 当前实体数量；未知时为 `0` |
| `spaceCount` | `uint32` | 当前场景数量；未知时为 `0` |
| `loadScore` | `uint32` | 统一负载分值，建议范围 `0-10000`；未知时为 `0` |

**结构：ServerStubOwnershipEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub 实体类型，例如 `MatchService`、`ChatService` |
| `entityKey` | `string` | 同类型内稳定的 Stub 实例标识 |
| `ownerGameNodeId` | `string` | 当前负责承载该 Stub 的 `Game` `NodeID` |
| `entryFlags` | `uint32` | 保留，当前必须为 `0` |

`(entityType, entityKey)` 共同唯一标识一个 `ServerStubEntity` 实例。

**结构：ServerStubReadyEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub 实体类型 |
| `entityKey` | `string` | 同类型内稳定的 Stub 实例标识 |
| `ready` | `bool` | 当前 Stub 是否已 ready |
| `entryFlags` | `uint32` | 保留，当前必须为 `0` |

**Inner.NodeRegister（`msgId = 1000`）**
1. 用途：建立一条新的启动控制面注册会话，可用于 `Gate/Game -> GM`，也可用于 `Game -> Gate`。
2. 发送前提：发起方必须已经建立到底层 `Inner` 对端的可用链路。
3. 成功语义：接收方接受这条注册会话，并返回后续心跳参数。
4. 失败语义：接收方拒绝当前注册，发起方依据 `errorCode` / `retryAfterMs` 决定是否重试。

注册请求体：

| Field | Type | Description |
| --- | --- | --- |
| `processType` | `uint16` | 发起方类型，取值见 `ProcessType` |
| `processFlags` | `uint16` | 保留，当前必须为 `0` |
| `nodeId` | `string` | 发起方稳定 `NodeID` |
| `pid` | `uint32` | 发起方进程号，仅用于诊断与日志 |
| `startedAtUnixMs` | `uint64` | 发起方进程启动时间 |
| `innerNetworkEndpoint` | `Endpoint` | 发起方对外发布的 `Inner` 地址 |
| `buildVersion` | `string` | 可读构建版本字符串 |
| `capabilityTags` | `string[]` | 能力标签列表；没有时为空 |
| `load` | `LoadSnapshot` | 当前负载快照；未知时全 `0` |

注册成功响应体：

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | 建议心跳周期，当前默认 `5000` |
| `heartbeatTimeoutMs` | `uint32` | 会话超时阈值，当前默认 `15000` |
| `serverNowUnixMs` | `uint64` | 接收方当前时间，用于时钟对齐与诊断 |

注册失败响应体（`Response + Error`）：

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，取值见 `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；`0` 表示未提供 |

**Inner.NodeHeartbeat（`msgId = 1100`）**
1. 用途：维护已建立的注册会话，并附带轻量负载刷新。
2. 发送前提：只有在同一链路上收到对应注册成功响应后，才允许开始发送心跳。
3. 成功语义：接收方确认当前注册仍然有效，并可刷新心跳参数。
4. 失败语义：接收方认为当前会话已失效或不再匹配，发起方应按 `requireFullRegister` 决定是否完整重注册。

心跳请求体：

| Field | Type | Description |
| --- | --- | --- |
| `sentAtUnixMs` | `uint64` | 发送方发送该心跳时的本地时间 |
| `statusFlags` | `uint32` | 保留，当前必须为 `0` |
| `load` | `LoadSnapshot` | 最新负载快照；未知时全 `0` |

心跳成功响应体：

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | 接收方建议的后续心跳周期 |
| `heartbeatTimeoutMs` | `uint32` | 当前会话使用的超时阈值 |
| `serverNowUnixMs` | `uint64` | 接收方当前时间 |

心跳失败响应体（`Response + Error`）：

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因 |
| `retryAfterMs` | `uint32` | 建议重试等待时间；`0` 表示未提供 |
| `requireFullRegister` | `bool` | `true` 表示发送方必须丢弃旧会话并重新走完整注册 |

**Inner.ClusterReadyNotify（`msgId = 1201`）**
1. 发送时机：`GM` 在集群 ready 聚合结果发生变化时，向全部 `Gate` 下发最新结论；`Game` 可按需旁路消费，但当前关键接收方是 `Gate`。
2. 关键语义：`clusterReady = true` 表示 `GM` 已确认当前服务器组满足对外服务前提；`Gate` 只有收到这一结论后，才允许开放 `ClientNetwork`。
3. 幂等语义：接收方只保留最新 `readyEpoch` 的结果；旧轮次通知必须忽略。

通知体：

| Field | Type | Description |
| --- | --- | --- |
| `readyEpoch` | `uint64` | 集群 ready 结论版本号 |
| `clusterReady` | `bool` | 集群是否 ready |
| `statusFlags` | `uint32` | 保留，当前必须为 `0` |
| `serverNowUnixMs` | `uint64` | `GM` 当前时间 |

**Inner.ServerStubOwnershipSync（`msgId = 1202`）**
1. 发送时机：`GM` 在期望 `Game` 与 `Gate` 节点都已注册完成后，向全部 `Game` 下发当前 `ServerStubEntity` ownership 快照。
2. 关键语义：接收方只接受最新 `assignmentEpoch` 的全量快照；旧轮次结果必须丢弃。
3. 使用方式：`Game` 收到后只初始化分配给自己的 Stub，并以同一 `assignmentEpoch` 驱动后续 ready 上报。

通知体：

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | 当前 ownership 版本号 |
| `statusFlags` | `uint32` | 保留，当前必须为 `0` |
| `assignments` | `ServerStubOwnershipEntry[]` | 当前全量 ownership 快照 |
| `serverNowUnixMs` | `uint64` | `GM` 当前时间 |

**Inner.GameServiceReadyReport（`msgId = 1203`）**
1. 发送时机：`Game` 只有在 assigned `ServerStubEntity` 已 ready 且已经完成对全部目标 `Gate` 的注册/心跳闭环后，才允许向 `GM` 上报本地 ready 结果。
2. 关键语义：`localReady = true` 表示该 `Game` 在当前 `assignmentEpoch` 下已经满足对外服务前提。
3. 聚合语义：`GM` 必须按 `nodeId + assignmentEpoch` 聚合；旧轮次 ready 上报不得混入新一轮 ownership 结论。

上报体：

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | 当前 ready 所基于的 ownership 版本 |
| `localReady` | `bool` | 当前 `Game` 是否已经本地 ready |
| `statusFlags` | `uint32` | 保留，当前必须为 `0` |
| `entries` | `ServerStubReadyEntry[]` | 当前 `Game` 负责 Stub 的 ready 明细 |
| `reportedAtUnixMs` | `uint64` | 发送方形成该 ready 结论的时间 |

**时序与一致性约束**
1. `GM` 必须先进入 `InnerNetwork` 监听状态，再允许其他节点接入。
2. `Game` 与 `Gate` 都必须先完成 `Gate/Game -> GM` 的注册与心跳闭环，`GM` 才能进入 ownership 决策阶段。
3. `GM` 只有在期望的 `Game` 与 `Gate` 节点都注册完成后，才能下发 `Inner.ServerStubOwnershipSync`。
4. `Game` 不得在收到 ownership 分配前自行决定本地承载哪些 `ServerStubEntity`。
5. `Game` 在向 `GM` 报告 `localReady = true` 前，必须先完成对全部目标 `Gate` 的注册与心跳闭环。
6. `Gate` 可以在收到 `clusterReady = true` 之前维护来自 `Game` 的内部注册表，但不得提前开放客户端入口。
7. `Gate` 对客户端入口的开放只取决于最新的 `Inner.ClusterReadyNotify`；本地配置齐全、局部链路可用或单个 `Game` ready 都不足以替代这一结论。
8. 当前默认心跳参数为 `heartbeatIntervalMs = 5000`、`heartbeatTimeoutMs = 15000`；响应方可以覆盖，但必须满足 `heartbeatIntervalMs < heartbeatTimeoutMs`。
9. `statusFlags`、`ServerStubOwnershipEntry.entryFlags` 与 `ServerStubReadyEntry.entryFlags` 当前都必须为 `0`。
10. 若未来需要额外的目录快照或增量同步协议，应以新的扩展条目重新登记，而不是复用当前已登记的启动编排消息。

