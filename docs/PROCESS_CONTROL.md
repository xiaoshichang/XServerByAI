# PROCESS_CONTROL

本文档定义 XServerByAI 当前阶段 GM 与 Gate/Game 之间“进程注册”与“心跳”控制面消息的结构、字段语义与默认时序。后续控制面实现应以此文档作为消息体与状态约定的基线。

**适用范围**
1. 当前仅覆盖 `Control.ProcessRegister`（`msgId = 1000`）与 `Control.ProcessHeartbeat`（`msgId = 1100`）两类消息。
2. 请求方向为 `Gate/Game -> GM`，响应方向为 `GM -> Gate/Game`，响应复用同一 `msgId` 并设置 `PacketHeader.flags.Response`。
3. 失败响应通过 `PacketHeader.flags.Error` 与 `docs/ERROR_CODE.md` 中登记的控制面错误码表达。
4. 当前默认每个服务器组固定 `1` 个 GM，管理该组内 `N` 个 Gate 节点与 `M` 个 Game 节点，其中 `N >= 1`、`M >= 1`。

**共享编码约定**
1. 消息体中的整数沿用内部协议统一约定，使用网络字节序（大端）编码。
2. `string` 使用 `uint16 byteLength + UTF-8 bytes` 编码，单个字符串最大建议不超过 `1024` 字节。
3. `string[]` 使用 `uint16 count + repeated string` 编码，单个数组最大建议不超过 `32` 项。
4. `bool` 使用 `uint8` 表示，`0` 为 `false`，`1` 为 `true`。
5. 所有时间戳字段均使用 `unixTimeMsUtc` 语义。
6. `PacketHeader.seq` 应使用非零值关联请求与响应；响应必须回显请求的 `seq`。
7. 当前未定义的保留字段或保留位必须为 `0`，接收方应忽略未来可扩展字段之外的未定义语义。

**枚举：ProcessType**

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `1` | `Gate` | 客户端入口与路由转发进程 |
| `2` | `Game` | 业务逻辑与状态承载进程 |

当前阶段只有 `Gate` 与 `Game` 允许向 GM 发送注册与心跳消息；其他值返回 `3000 Control.ProcessTypeInvalid`。

**共享概念：NodeID**
1. `nodeId` 表示 Gate/Game 在所属服务器组内的稳定逻辑身份。
2. `nodeId` 使用区分大小写的格式 `<ProcessType><index>`，例如 `Gate0`、`Gate1`、`Game0`、`Game1`。
3. 同一服务器组内 `nodeId` 必须唯一；进程重启后应保留原 `nodeId`。
4. `GM` 不参与 `nodeId` 编号体系；每个服务器组固定 `1` 个 GM，负责管理该组内全部 Gate/Game 节点。

**共享结构：Endpoint**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `host` | `string` | 对外发布的监听地址，建议使用 IP 字面量或在内部网络可解析的主机名 |
| `port` | `uint16` | 对外发布的监听端口，`0` 视为无效 |

`serviceEndpoint` 用于向 GM 声明该进程后续要被其他组件消费的服务入口。对 `Game` 来说，它表示 Gate 需要连接的内部 TCP 服务地址；对 `Gate` 来说，它表示当前 Gate 发布给集群的服务入口，具体用途可在后续条目中细化。

**共享结构：LoadSnapshot**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `connectionCount` | `uint32` | 当前网络连接数或内部长连接数；未知时填 `0` |
| `sessionCount` | `uint32` | 当前会话数；对不适用的进程填 `0` |
| `entityCount` | `uint32` | 当前实体数；对不适用的进程填 `0` |
| `spaceCount` | `uint32` | 当前场景数；对不适用的进程填 `0` |
| `loadScore` | `uint32` | 归一化负载分值，范围建议 `0-10000`，未知时填 `0` |

该结构作为 M3-14 之前的最小负载快照，占位时允许全 `0` 上报。

**Control.ProcessRegister（`msgId = 1000`）**
1. 发送时机：`Gate`/`Game` 与 GM 建立内部 TCP 连接后，应先发送注册请求，再发送任何其他控制面请求。
2. 成功语义：GM 接受该连接成为一个新的活动注册，并返回当前注册租约、心跳参数与服务器时间。
3. 失败语义：GM 拒绝当前注册，调用方可根据返回的 `errorCode` 与 `retryAfterMs` 决定是否重试。

注册请求体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `processType` | `uint16` | 进程类型，取值见 `ProcessType` |
| `processFlags` | `uint16` | 当前保留，发送方必须置 `0` |
| `nodeId` | `string` | 进程在所属服务器组内的稳定逻辑标识（`NodeID`），例如 `Gate0`、`Game1` |
| `pid` | `uint32` | 本地操作系统进程号，用于诊断与管理展示 |
| `startedAtUnixMs` | `uint64` | 该进程本次启动时间，用于区分同一 `NodeID` 的重启 |
| `serviceEndpoint` | `Endpoint` | 该进程对外发布的服务入口，不能为空 |
| `buildVersion` | `string` | 可读构建版本或 Git 描述字符串，用于兼容性排查 |
| `capabilityTags` | `string[]` | 能力标签列表，例如 `space-basic`、`chat-disabled`；没有时传空数组 |
| `load` | `LoadSnapshot` | 初始负载快照；未知时允许全 `0` |

注册成功响应体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `registrationId` | `uint64` | 由 GM 分配的注册租约标识；后续心跳必须回传该值 |
| `heartbeatIntervalMs` | `uint32` | 推荐心跳发送间隔，默认 `5000` |
| `heartbeatTimeoutMs` | `uint32` | GM 判定超时的阈值，默认 `15000` |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳，用于调试和对齐时钟偏差 |

注册失败响应体（`Response + Error`）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，取值见 `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；`0` 表示不提供建议 |

**Control.ProcessHeartbeat（`msgId = 1100`）**
1. 发送前提：只有在收到注册成功响应后，发送方才可发送心跳。
2. 成功语义：GM 确认该注册仍然有效，并可在响应中调整后续心跳参数。
3. 失败语义：GM 认为当前注册不存在或已失效，发送方需要根据错误码判断是否重新注册。

心跳请求体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `registrationId` | `uint64` | 注册成功响应返回的租约标识 |
| `sentAtUnixMs` | `uint64` | 发送方发送该心跳时的时间戳 |
| `statusFlags` | `uint32` | 当前保留，发送方必须置 `0` |
| `load` | `LoadSnapshot` | 最新负载快照；未知时允许全 `0` |

心跳成功响应体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `registrationId` | `uint64` | 回显当前有效租约标识 |
| `heartbeatIntervalMs` | `uint32` | GM 推荐的下一轮心跳间隔 |
| `heartbeatTimeoutMs` | `uint32` | 当前租约使用的超时阈值 |
| `serverNowUnixMs` | `uint64` | GM 当前时间戳 |

心跳失败响应体（`Response + Error`）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `errorCode` | `int32` | 失败原因，常见值为 `3003` 或 `3004` |
| `retryAfterMs` | `uint32` | 建议重试等待时间；要求立即重注册时可为 `0` |
| `requireFullRegister` | `bool` | `1` 表示发送方必须重新走完整注册流程 |

**时序与校验规则**
1. 连接建立后，发送方必须先发送一次 `Control.ProcessRegister`；注册成功前不得发送心跳。
2. `nodeId` 表示稳定逻辑身份，GM 应拒绝同一时刻重复的活动 `nodeId` 注册，并返回 `3001 Control.NodeIdConflict`。
3. `registrationId` 只对一次成功注册及其所属 TCP 连接生命周期有效；连接重建或 GM 要求重注册时，必须重新申请新的 `registrationId`。
4. 默认心跳间隔为 `5000ms`，默认超时阈值为 `15000ms`；GM 可以在响应中覆盖，但必须保证 `heartbeatIntervalMs < heartbeatTimeoutMs`。
5. 心跳引用未知租约时返回 `3003 Control.RegistrationNotFound`；引用已过期租约时返回 `3004 Control.RegistrationExpired`。
6. `serviceEndpoint.port = 0`、`serviceEndpoint.host` 为空或 `processType` 不合法时，GM 应拒绝注册，并返回 `3000` 或 `3002`。
7. `load` 中无法提供的数据一律填 `0`，禁止使用负值或未初始化内存表达“未知”。
8. 当前阶段不额外定义显式注销消息；优雅下线可以通过连接关闭表达，后续如需补充单独的注销消息，再在控制面号段中登记新 `msgId`。
