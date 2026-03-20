# PROCESS_CONTROL

本文档定义 XServerByAI 当前阶段 GM 与 Gate/Game 之间“进程注册”“心跳”“ServerStubEntity ownership 分配”“Game 服务就绪上报”“Game 路由目录查询 / 同步”与“集群就绪通知”控制面消息的结构、字段语义与默认时序。当前默认控制面传输承载在 ZeroMQ over TCP 链路上，消息体与状态约定以本文为基线。

**适用范围**
1. 当前覆盖 `Control.ProcessRegister`（`msgId = 1000`）、`Control.ProcessHeartbeat`（`msgId = 1100`）、`Control.ClusterReadyNotify`（`msgId = 1201`）、`Control.ServerStubOwnershipSync`（`msgId = 1202`）、`Control.GameServiceReadyReport`（`msgId = 1203`）、`Control.GameDirectoryQuery`（`msgId = 1204`）与 `Control.GameDirectoryNotify`（`msgId = 1205`）七类消息。
2. 请求-响应类消息包括注册、心跳与 `Control.GameDirectoryQuery`；响应方向为 `GM -> Gate/Game`，并复用原始 `msgId` 与 `PacketHeader.flags.Response`。
3. 单向消息包括 `Control.ClusterReadyNotify`、`Control.ServerStubOwnershipSync`、`Control.GameServiceReadyReport` 与 `Control.GameDirectoryNotify`。
4. 当前阶段不定义独立的配置下发消息；所有节点从进程启动时传入的统一配置文件读取整个集群配置以及自身实例配置。
5. 失败响应通过 `PacketHeader.flags.Error` 与 `docs/ERROR_CODE.md` 中登记的控制面错误码表达。
6. 当前默认每个服务器组固定 `1` 个 GM，管理该组内 `N` 个 Gate 节点与 `M` 个 Game 节点，其中 `N >= 1`、`M >= 1`。

**共享编码约定**
1. 消息体中的整数统一使用网络字节序（大端）编码。
2. `string` 使用 `uint16 byteLength + UTF-8 bytes` 编码，单个字符串建议不超过 `1024` 字节。
3. `string[]` 使用 `uint16 count + repeated string` 编码，单个数组建议不超过 `32` 项。
4. 结构体数组使用 `uint16 count + repeated <entry>` 编码，除非具体消息单独说明，否则单个数组建议不超过 `256` 项。
5. `bool` 使用 `uint8` 表示，`0` 为 `false`，`1` 为 `true`。
6. 所有时间戳字段统一使用 `unixTimeMsUtc` 语义。
7. `PacketHeader.seq` 应使用非零值关联请求与响应；响应必须回显请求的 `seq`。
8. 当前未定义的保留字段或保留位必须为 `0`。

**枚举：ProcessType**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Gate` | 客户端入口与路由转发进程 |
| `2` | `Game` | 业务逻辑与状态承载进程 |

当前阶段只允许 `Gate` 与 `Game` 向 GM 发送注册、心跳、目录查询或 ready 上报消息；其他取值返回 `3000 Control.ProcessTypeInvalid`。

**枚举：GameRouteState**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Available` | 可接受新的会话绑定 |
| `2` | `Draining` | 不再接收新会话，但允许既有会话继续运行 |
| `3` | `Unavailable` | 不可被选择为目标路由 |

该枚举与 `docs/SESSION_ROUTING.md` 中的 `GameRouteState` 语义保持一致。

**共享概念：NodeID**
1. `nodeId` 表示 Gate/Game 在所属服务器组内的稳定逻辑身份。
2. `nodeId` 使用区分大小写的 `<ProcessType><index>` 格式，例如 `Gate0`、`Gate1`、`Game0`、`Game1`。
3. 同一服务器组内 `nodeId` 必须唯一；进程重启后应保留原 `nodeId`。
4. `GM` 不参与 `nodeId` 编号体系；每个服务器组固定 `1` 个 GM。
5. 当前阶段 GM 以“稳定 `nodeId` + 当前活动控制链路”识别一个在线节点，不再额外分配单独的活动租约号。

**共享结构：Endpoint**

| Field | Type | Description |
| --- | --- | --- |
| `host` | `string` | 对外发布的监听地址，建议使用 IP 字面量或内网可解析主机名 |
| `port` | `uint16` | 对外发布的监听端口，`0` 视为无效 |

`serviceEndpoint` 用于向 GM 声明该进程后续要被其他组件消费的服务入口。对 `Game` 来说，它表示 Gate 需要连接的 ZeroMQ over TCP 服务地址；对 `Gate` 来说，它表示当前 Gate 发布给集群的服务入口，具体用途可在后续条目中细化。

**共享结构：LoadSnapshot**

| Field | Type | Description |
| --- | --- | --- |
| `connectionCount` | `uint32` | 当前网络连接数或内部长连接数；未知时填 `0` |
| `sessionCount` | `uint32` | 当前会话数；不适用时填 `0` |
| `entityCount` | `uint32` | 当前实体数；不适用时填 `0` |
| `spaceCount` | `uint32` | 当前场景数；不适用时填 `0` |
| `loadScore` | `uint32` | 归一化负载分值，建议范围 `0-10000`，未知时填 `0` |

该结构作为 `M3-14` 之前的最小负载快照，占位阶段允许全 `0` 上报。

**共享结构：ServerStubOwnershipEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub 实体类型，例如 `MatchService`、`ChatService` |
| `entityKey` | `string` | 同一类型下的稳定实例标识 |
| `ownerGameNodeId` | `string` | 当前被分配承载该 Stub 的 `Game` 节点 |
| `entryFlags` | `uint32` | 当前保留，发送方必须置 `0` |

`(entityType, entityKey)` 共同表达一个 `ServerStubEntity` 实例的稳定身份。

**共享结构：ServerStubReadyEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub 实体类型 |
| `entityKey` | `string` | 同一类型下的稳定实例标识 |
| `ready` | `bool` | 该 Stub 当前是否已经 ready |
| `entryFlags` | `uint32` | 当前保留，发送方必须置 `0` |

**共享结构：GameDirectorySnapshotEntry**

| Field | Type | Description |
| --- | --- | --- |
| `gameNodeId` | `string` | `Game` 节点稳定逻辑身份 |
| `routeState` | `uint16` | 路由状态，取值见 `GameRouteState` |
| `serviceEndpoint` | `Endpoint` | Gate 与该 `Game` 建立内部连接使用的目标地址 |
| `capabilityTags` | `string[]` | 来自注册消息的能力标签 |
| `load` | `LoadSnapshot` | 最近一次来自 GM 的负载快照 |
| `updatedAtUnixMs` | `uint64` | 该目录条目最近一次被 GM 刷新的时间 |

当 Gate 将该结构落地为本地 `GameDirectoryEntry` 时，应把外层消息里的 `routeEpoch` 写入每个本地条目的 `routeEpoch` 字段。

**Control.ProcessRegister（`msgId = 1000`）**
1. 发送时机：`Gate`/`Game` 与 GM 的 ZeroMQ over TCP 控制链路可用后，应先发送注册请求，再发送任何其他控制面请求。
2. 成功语义：GM 接受该节点成为一个新的活动注册，并返回当前建议的心跳参数与服务器时间。
3. 失败语义：GM 拒绝当前注册，调用方可根据 `errorCode` 与 `retryAfterMs` 决定是否重试。

注册请求体：

| Field | Type | Description |
| --- | --- | --- |
| `processType` | `uint16` | 进程类型，取值见 `ProcessType` |
| `processFlags` | `uint16` | 当前保留，发送方必须置 `0` |
| `nodeId` | `string` | 进程在所属服务器组内的稳定逻辑标识 |
| `pid` | `uint32` | 本地操作系统进程号，用于诊断与管理展示 |
| `startedAtUnixMs` | `uint64` | 本次启动时间，用于诊断与节点重启识别 |
| `serviceEndpoint` | `Endpoint` | 该进程对外发布的服务入口，不能为空 |
| `buildVersion` | `string` | 可读构建版本或 Git 描述字符串 |
| `capabilityTags` | `string[]` | 能力标签列表；没有时传空数组 |
| `load` | `LoadSnapshot` | 初始负载快照；未知时允许全 `0` |

注册成功响应体：

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | 推荐心跳发送间隔，默认 `5000` |
| `heartbeatTimeoutMs` | `uint32` | GM 判定超时的阈值，默认 `15000` |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳，用于调试与时钟对齐 |

注册失败响应体（`Response + Error`）：

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，取值见 `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；`0` 表示不提供建议 |

**Control.ProcessHeartbeat（`msgId = 1100`）**
1. 发送前提：只有在收到注册成功响应后，发送方才可发送心跳。
2. 成功语义：GM 确认当前节点在现有控制链路上的活动注册仍然有效，并可在响应中调整后续心跳参数。
3. 失败语义：GM 认为当前控制链路不再对应活动节点，发送方需要根据错误码判断是否重新注册。

心跳请求体：

| Field | Type | Description |
| --- | --- | --- |
| `sentAtUnixMs` | `uint64` | 发送方发送该心跳时的时间戳 |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `load` | `LoadSnapshot` | 最新负载快照；未知时允许全 `0` |

心跳成功响应体：

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | GM 推荐的下一轮心跳间隔 |
| `heartbeatTimeoutMs` | `uint32` | 当前链路使用的超时阈值 |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

心跳失败响应体（`Response + Error`）：

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，常见值为 `3003`、`3004`；请求格式非法时可返回 `3005` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；要求立即重注册时可为 `0` |
| `requireFullRegister` | `bool` | `1` 表示发送方必须重新走完整注册流程；仅请求格式非法时可为 `0` |

**Control.ClusterReadyNotify（`msgId = 1201`）**
1. 发送时机：GM 在集群 ready 聚合结果发生变化时向 Gate 下发；如有需要，也可以向 Game 广播同一状态用于诊断与对齐。
2. 核心语义：`clusterReady = true` 表示 GM 当前确认该服务器组已经满足对外服务前提；对 Gate 来说，这直接影响客户端入口是否允许打开。
3. 幂等语义：接收方只需要保留最新 `readyEpoch` 的结果；旧通知重复到达时应忽略。

集群就绪通知体：

| Field | Type | Description |
| --- | --- | --- |
| `readyEpoch` | `uint64` | 集群就绪状态版本号；GM 每次生成新结论时递增或更新 |
| `clusterReady` | `bool` | 集群是否已 ready |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

**Control.ServerStubOwnershipSync（`msgId = 1202`）**
1. 发送时机：GM 在所有期望的 `Game` 节点注册完成并完成本轮 ownership 计算后，向各 `Game` 下发当前 `ServerStubEntity` ownership 全量快照。
2. 核心语义：该消息表达当前服务器组内所有 `ServerStubEntity -> OwnerGameNodeId` 的完整映射，而不是增量 patch。
3. 幂等语义：接收方只保留最新 `assignmentEpoch` 的结果；旧快照重复到达时应忽略。

ownership 同步体：

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | ownership 快照版本号；GM 每次重新计算后更新 |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `assignments` | `ServerStubOwnershipEntry[]` | 当前服务器组内全部 `ServerStubEntity` 的 ownership 快照 |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

**Control.GameServiceReadyReport（`msgId = 1203`）**
1. 发送时机：`Game` 在本地 assigned `ServerStubEntity` 初始化完成后发送；如本地 ready 结论或某个 assigned Stub 的 ready 状态发生变化，也可以在同一 `assignmentEpoch` 下重复上报。
2. 核心语义：`localReady = true` 表示该 `Game` 在当前 `assignmentEpoch` 下被分配承载的 Stub 已全部 ready。
3. 幂等语义：GM 应按 `nodeId + assignmentEpoch` 聚合；旧 `assignmentEpoch` 的上报不得覆盖较新的 ownership 结论。

本地服务 ready 上报体：

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | 该 ready 结论所基于的 ownership 快照版本 |
| `localReady` | `bool` | 本地 assigned `ServerStubEntity` 是否已全部 ready |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `entries` | `ServerStubReadyEntry[]` | 当前 `Game` 被分配承载的 Stub ready 详情 |
| `reportedAtUnixMs` | `uint64` | 发送方生成该结论的时间 |

**Control.GameDirectoryQuery（`msgId = 1204`）**
1. 发送时机：`Gate` 在注册与心跳闭环建立后发送，用于获取当前 `Game` 路由目录全量快照，并可同时请求后续变化订阅。
2. 成功语义：GM 返回当前 `routeEpoch` 与全量目录快照；如果 `subscriptionAccepted = true`，后续目录变化改用 `Control.GameDirectoryNotify (1205)` 推送。
3. 失败语义：GM 拒绝本次目录查询或订阅请求，`Gate` 可根据错误码与 `retryAfterMs` 决定是否重试。

目录查询请求体：

| Field | Type | Description |
| --- | --- | --- |
| `knownRouteEpoch` | `uint64` | `Gate` 当前已应用的目录版本；首次查询时填 `0` |
| `subscribeUpdates` | `bool` | `1` 表示希望后续持续接收目录变化推送 |
| `queryFlags` | `uint32` | 当前保留，发送方必须置 `0` |

目录查询成功响应体：

| Field | Type | Description |
| --- | --- | --- |
| `routeEpoch` | `uint64` | GM 当前目录版本号 |
| `subscriptionAccepted` | `bool` | GM 是否接受该 `Gate` 的后续目录变化订阅 |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `entries` | `GameDirectorySnapshotEntry[]` | 当前 `Game` 路由目录全量快照 |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

目录查询失败响应体（`Response + Error`）：

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，取值见 `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；`0` 表示不提供建议 |

**Control.GameDirectoryNotify（`msgId = 1205`）**
1. 发送时机：GM 在某个已订阅 `Gate` 需要更新 `Game` 路由目录且 `routeEpoch` 发生变化时发送。
2. 核心语义：该消息发送的是新的全量目录快照，而不是增量 patch。
3. 幂等语义：`Gate` 只保留最新 `routeEpoch` 的结果；旧快照重复到达时应忽略。

目录变化通知体：

| Field | Type | Description |
| --- | --- | --- |
| `routeEpoch` | `uint64` | 新的目录版本号 |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `entries` | `GameDirectorySnapshotEntry[]` | 新的全量 `Game` 路由目录快照 |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

**时序与校验规则**
1. ZeroMQ over TCP 控制链路可用后，发送方必须先发送一次 `Control.ProcessRegister`；注册成功前不得发送心跳、目录查询或本地 ready 上报。
2. `nodeId` 表示稳定逻辑身份；GM 应拒绝同一时刻重复的活动 `nodeId` 注册，并返回 `3001 Control.NodeIdConflict`。
3. 当前阶段一次成功注册会把 `nodeId` 绑定到当前控制链路；后续心跳、目录查询与 ready 上报默认依赖该链路定位活动节点，不再额外发放独立租约字段。
4. 默认心跳间隔为 `5000ms`，默认超时阈值为 `15000ms`；GM 可以在响应中覆盖，但必须保证 `heartbeatIntervalMs < heartbeatTimeoutMs`。
5. 心跳来自未知节点或未完成注册的控制链路时返回 `3003 Control.NodeNotRegistered`；心跳来自已被替换、失效或不再拥有该 `nodeId` 的控制链路时返回 `3004 Control.ControlChannelInvalid`。
6. `serviceEndpoint.port = 0`、`serviceEndpoint.host` 为空或 `processType` 不合法时，GM 应拒绝注册，并返回 `3000` 或 `3002`。
7. `load` 中无法提供的数据一律填 `0`，禁止使用负值或未初始化内存表达“未知”。
8. `Control.ClusterReadyNotify.statusFlags`、`Control.ServerStubOwnershipSync.statusFlags`、`Control.GameServiceReadyReport.statusFlags`、`Control.GameDirectoryQuery.queryFlags`、`Control.GameDirectoryQuery response.statusFlags` 与 `Control.GameDirectoryNotify.statusFlags` 当前都必须为 `0`。
9. `ServerStubOwnershipEntry.entryFlags` 与 `ServerStubReadyEntry.entryFlags` 当前必须为 `0`；`ownerGameNodeId` 必须指向当前服务器组内一个合法的 `Game` `nodeId`。
10. `GameDirectorySnapshotEntry.routeState` 必须是合法的 `GameRouteState` 枚举值；Gate 在本地落盘目录条目时，应把外层 `routeEpoch` 写入每个本地条目的 `routeEpoch` 字段。
11. `Game` 必须忽略旧的 `assignmentEpoch` ownership 快照；GM 也必须忽略旧的 `assignmentEpoch` ready 上报。`Gate` 必须忽略旧的 `routeEpoch` 目录快照。
12. 当前阶段不额外定义显式注销消息；优雅下线可以通过连接关闭表达，后续如需补充单独的注销消息，再在控制面号段中登记新 `msgId`。
