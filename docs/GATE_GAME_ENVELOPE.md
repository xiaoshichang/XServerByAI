# GATE_GAME_ENVELOPE

本文档定义 XServerByAI 当前阶段 Gate 与 Game 之间的中继封装格式。该格式用于承载“客户端请求转发到 Game”“Game 将业务响应回传 Gate”“Game 经由 Gate 主动推送客户端消息”三类链路语义。

**适用范围**
1. 当前仅覆盖 `Relay.ForwardToGame`（`msgId = 2000`）与 `Relay.PushToClient`（`msgId = 2001`）两类内部消息。
2. `Relay.ForwardToGame` 的请求方向为 `Gate -> Game`；成功或失败响应方向为 `Game -> Gate`，响应复用同一 `msgId` 并设置 `PacketHeader.flags.Response`。
3. `Relay.PushToClient` 的方向为 `Game -> Gate`，当前为单向消息，不定义显式协议级确认。
4. `PacketHeader.flags.Error` 在本链路中只表达“中继层失败”，不直接等价于客户端业务层失败。
5. 会话与路由模型的字段语义、状态机与失效规则见 `docs/SESSION_ROUTING.md`；本文件只约定中继封装如何保留并传递最小会话标识字段。

**共享编码约定**
1. 所有内部整数沿用内部协议统一约定，使用网络字节序（大端）编码。
2. 中继封装传递的是“客户端消息体语义”，不是客户端原始 `PacketHeader` 字节；Gate 与 Game 都应使用结构化字段重建客户端头部语义。
3. `clientMsgId` 必须指向客户端可见的消息语义，当前不得使用内部保留号段 `1-3999`，推荐取值范围为 `4000+`。
4. `clientFlags` 只允许使用 `docs/PACKET_HEADER.md` 中定义的低 3 位；出现其他位或不合法组合时，应视为 `3103 Relay.ClientFlagsInvalid`。
5. 外层 `PacketHeader.flags.Compressed` 表示 Gate↔Game 内部链路压缩；`clientFlags.Compressed` 表示 Gate 重新发包给客户端时的目标压缩语义，两者互不替代。
6. `payload` 使用 `uint32 payloadLength + payload bytes` 编码；`payloadLength = 0` 表示空消息体。
7. 除明确定义的字段外，所有保留字段均必须置 `0`。

**共享结构：RelayEnvelopeHeader**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `sessionId` | `uint64` | 客户端会话标识；当前必须为非零值 |
| `playerId` | `uint64` | 已绑定玩家标识；未登录或未知时填 `0` |
| `clientMsgId` | `uint32` | 客户端侧最终语义消息编号 |
| `clientSeq` | `uint32` | 客户端消息关联序号；无关联时可为 `0` |
| `clientFlags` | `uint16` | 客户端侧包头标志位语义，取值受 `PACKET_HEADER` 约束 |
| `relayFlags` | `uint16` | 当前保留，必须置 `0` |

`RelayEnvelopeHeader` 是中继链路与客户端消息语义之间的最小桥接头。Gate 与 Game 不应在内部链路中重新引入客户端原始头部的 `magic`、`version`、`length` 或内部控制 `msgId`。

**Relay.ForwardToGame（`msgId = 2000`）**
1. 用途：Gate 将客户端请求或经 Gate 归一化后的业务请求转发给已经选定的 Game。
2. 请求时外层 `PacketHeader.seq` 必须为非零值；成功或失败响应必须回显同一 `seq`。
3. 请求包中的 `clientFlags` 当前不得设置 `Response` 或 `Error`；若原始客户端包存在 `Compressed` 语义，可由 Gate 决定是否在归一化后保留到 `clientFlags`。

请求体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `relay` | `RelayEnvelopeHeader` | 会话、玩家与客户端包元数据 |
| `payloadLength` | `uint32` | 客户端语义消息体长度 |
| `payload` | `bytes` | 客户端语义消息体 |

成功响应体（`Response`）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `relay` | `RelayEnvelopeHeader` | 回显会话、玩家与客户端包元数据 |
| `payloadLength` | `uint32` | 返回给客户端的业务消息体长度 |
| `payload` | `bytes` | 返回给客户端的业务消息体 |

成功响应约束：
1. `relay.clientMsgId` 必须为 Gate 需要发送给客户端的目标消息编号。
2. `relay.clientFlags` 必须设置 `Response` 位；如该客户端业务响应携带业务错误语义，可同时设置 `Error` 位。
3. `relay.clientSeq` 通常沿用客户端请求的关联序号；如客户端协议后续允许重映射，必须由专门文档另行说明。

失败响应体（`Response + Error`）：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `sessionId` | `uint64` | 发生失败的目标会话 |
| `playerId` | `uint64` | 当前绑定玩家标识；未知时为 `0` |
| `clientMsgId` | `uint32` | 原始请求的客户端语义消息编号 |
| `clientSeq` | `uint32` | 原始请求的客户端关联序号 |
| `errorCode` | `int32` | 中继失败原因，取值见 `docs/ERROR_CODE.md` |

失败响应约束：
1. 外层 `PacketHeader.flags.Error` 只表示中继链路失败，例如目标会话不存在、路由不匹配、客户端消息编号非法。
2. 该失败响应不直接等价为客户端业务失败包；Gate 可以根据 `errorCode` 做日志、断连、丢弃或后续转换策略。
3. 常见错误码为 `3100 Relay.ClientMessageIdInvalid`、`3101 Relay.SessionNotFound`、`3102 Relay.RouteOwnershipMismatch`、`3103 Relay.ClientFlagsInvalid`。

**Relay.PushToClient（`msgId = 2001`）**
1. 用途：Game 主动要求 Gate 向客户端发送单向推送消息。
2. 外层 `PacketHeader.seq` 当前应为 `0`；若后续引入推送确认或重试，再扩展新的协议字段或消息类型。
3. 该消息不定义协议级响应；Gate 的本地投递失败由日志、metrics 或未来的诊断消息处理。

消息体：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `relay` | `RelayEnvelopeHeader` | 目标会话、玩家与客户端包元数据 |
| `payloadLength` | `uint32` | 推送消息体长度 |
| `payload` | `bytes` | 推送消息体 |

推送约束：
1. `relay.clientFlags` 不得设置 `Response` 位；若推送本身携带客户端错误语义，可按客户端协议需要设置 `Error` 位。
2. `relay.clientSeq` 默认应为 `0`；只有客户端协议明确要求推送也携带关联序号时，才允许使用非零值。
3. `relay.clientMsgId` 应指向客户端可见的推送消息编号，不能复用内部控制或中继 `msgId`。

**字段与校验规则**
1. `sessionId = 0` 一律视为非法中继包。
2. `playerId = 0` 仅表示“当前无玩家绑定”或“该阶段未知”，不得与合法玩家编号冲突。
3. `clientMsgId = 0` 或落在内部保留号段时，应返回或记录 `3100 Relay.ClientMessageIdInvalid`。
4. `clientFlags` 中的未定义位、请求包带 `Response/Error`、推送包带 `Response` 等情况，应返回或记录 `3103 Relay.ClientFlagsInvalid`。
5. Game 在处理 `Relay.ForwardToGame` 时，如果目标会话不存在，应返回 `3101 Relay.SessionNotFound`；如果会话存在但不属于当前 Game，应返回 `3102 Relay.RouteOwnershipMismatch`。
6. Gate 在重新封装客户端下行包时，应使用 `relay.clientMsgId`、`relay.clientSeq` 与 `relay.clientFlags` 生成客户端侧 `PacketHeader`，而不是复用外层 Gate↔Game 的 `PacketHeader`。
7. 当前不在中继封装中定义显式 `routeId`、`traceId`、`authContext` 等扩展字段；如后续需要，应在保持兼容性的前提下新增版本或扩展结构。
