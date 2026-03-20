# PROCESS_CONTROL

本文档定义 XServerByAI 当前阶段 GM 与 Gate/Game 之间“进程注册”与“心跳”控制面消息的结构、字段语义与默认时序。当前默认控制面传输承载在 ZeroMQ over TCP 链路上，消息体与状态约定以本文为基线。

**适用范围**
1. 当前仅覆盖 `Control.ProcessRegister`（`msgId = 1000`）与 `Control.ProcessHeartbeat`（`msgId = 1100`）两类消息。
2. 请求方向为 `Gate/Game -> GM`，响应方向为 `GM -> Gate/Game`，响应复用同一 `msgId` 并设置 `PacketHeader.flags.Response`。
3. 失败响应通过 `PacketHeader.flags.Error` 与 `docs/ERROR_CODE.md` 中登记的控制面错误码表达。
4. 当前默认每个服务器组固定 `1` 个 GM，管理该组内 `N` 个 Gate 节点与 `M` 个 Game 节点，其中 `N >= 1`、`M >= 1`。

**共享编码约定**
1. 消息体中的整数统一使用网络字节序（大端）编码。
2. `string` 使用 `uint16 byteLength + UTF-8 bytes` 编码，单个字符串建议不超过 `1024` 字节。
3. `string[]` 使用 `uint16 count + repeated string` 编码，单个数组建议不超过 `32` 项。
4. `bool` 使用 `uint8` 表示，`0` 为 `false`，`1` 为 `true`。
5. 所有时间戳字段统一使用 `unixTimeMsUtc` 语义。
6. `PacketHeader.seq` 应使用非零值关联请求与响应；响应必须回显请求的 `seq`。
7. 当前未定义的保留字段或保留位必须为 `0`。

**枚举：ProcessType**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Gate` | 客户端入口与路由转发进程 |
| `2` | `Game` | 业务逻辑与状态承载进程 |

当前阶段只允许 `Gate` 与 `Game` 向 GM 发送注册与心跳消息；其他取值返回 `3000 Control.ProcessTypeInvalid`。

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

**时序与校验规则**
1. ZeroMQ over TCP 控制链路可用后，发送方必须先发送一次 `Control.ProcessRegister`；注册成功前不得发送心跳。
2. `nodeId` 表示稳定逻辑身份；GM 应拒绝同一时刻重复的活动 `nodeId` 注册，并返回 `3001 Control.NodeIdConflict`。
3. 当前阶段一次成功注册会把 `nodeId` 绑定到当前控制链路；后续心跳默认依赖该链路定位活动节点，不再额外发放独立租约字段。
4. 默认心跳间隔为 `5000ms`，默认超时阈值为 `15000ms`；GM 可以在响应中覆盖，但必须保证 `heartbeatIntervalMs < heartbeatTimeoutMs`。
5. 心跳来自未知节点或未完成注册的控制链路时返回 `3003 Control.NodeNotRegistered`；心跳来自已被替换、失效或不再拥有该 `nodeId` 的控制链路时返回 `3004 Control.ControlChannelInvalid`。
6. 心跳请求若 `PacketHeader.flags` 非请求语义、`seq = 0`、payload 截断或 `statusFlags` 非 `0`，GM 可返回 `3005 Control.HeartbeatRequestInvalid`；该场景默认不要求完整重注册。
7. `serviceEndpoint.port = 0`、`serviceEndpoint.host` 为空或 `processType` 不合法时，GM 应拒绝注册，并返回 `3000` 或 `3002`。
8. `load` 中无法提供的数据一律填 `0`，禁止使用负值或未初始化内存表达“未知”。
9. 当前阶段不额外定义显式注销消息；优雅下线可以通过连接关闭表达，后续如需补充单独的注销消息，再在控制面号段中登记新 `msgId`。

