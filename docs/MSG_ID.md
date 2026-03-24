# MSG_ID

本文档定义 XServerByAI 当前阶段 `msgId` 的分段方式、规范命名方式与登记规则。所有内部协议、Gate↔Client 协议与后续 C# 实体消息在分配编号前都应先更新本文件。

**基础规则**
1. `msgId` 使用 `uint32_t` 表示，`0` 为保留值，表示“无效 / 未初始化消息”。
2. `msgId` 只表达“消息语义”；响应沿用请求原始 `msgId`，并通过 `PacketHeader.flags.Response` 表示响应语义。
3. 错误语义通过 `PacketHeader.flags.Error` 表示，不单独创建成对的 `ErrorResponse` `msgId`；具体错误码范围与编码规则见 `docs/ERROR_CODE.md`。
4. 同一个消息一旦对外分配编号，不得复用；废弃时仅标记为 `Deprecated` 并保留历史记录。
5. 生产协议只能使用稳定号段；临时联调或测试消息必须进入测试号段，联调结束后要么删除，要么重新走正式分配。

**顶层号段**

| 范围 | 类别 | 用途 |
| --- | --- | --- |
| `1-999` | 协议保留 | 协议层公共语义、基础兼容控制、通用保留位配套消息 |
| `1000-1999` | Inner 网络 | GM ↔ Gate/Game 的注册、心跳、Stub ownership、ready 上报与路由编排消息 |
| `2000-3999` | 内部中转 | Gate ↔ Game 封装、会话事件、内部 RPC 与转发消息 |
| `4000-9999` | 客户端接入 | Gate ↔ Client 的鉴权、会话、基础服务与通用推送 |
| `10000-39999` | 预留 | 预留 |
| `40000-44999` | 运维 / 诊断 | 后台管理、可观测性、诊断与维护消息 |
| `45000-49999` | 测试 / 联调 | 临时验证、回归探针、实验性协议 |
| `50000+` | 预留 | 未来跨服务器组扩展、大版本迁移或新增协议域 |

**当前预留子段**

| 范围 | 预留对象 | 说明 | 关联条目 |
| --- | --- | --- | --- |
| `1000-1099` | 进程注册 / 注销 | GM Inner 网络的进程生命周期消息 | `M1-09`, `M3-04` |
| `1100-1199` | 心跳 / 健康检查 | 心跳、超时探测、状态上报 | `M1-09`, `M3-05` |
| `1200-1299` | 启动编排 / 路由下发 | GM 下发集群就绪状态、Stub ownership、Game 服务 ready 聚合结果关联消息、Game 节点路由目录与路由变更 | `M3-06`, `M3-12`, `M3-14`, `M3-15` |
| `2000-2099` | Gate↔Game 封装 | 请求转发、响应回传、内部 RPC 信封 | `M1-10`, `M4-06`, `M4-07` |
| `2100-2199` | 会话事件 | 绑定、关闭、路由丢失、踢出、重连等会话与路由状态变化 | `M1-12`, `M4-10`, `M4-16`, `M5-08` |
| `4000-4199` | 客户端会话基础 | 鉴权、客户端心跳、基础 Push | `M4-03`, `M4-15` |
| `10000-39999` | 预留 | 预留 | 后续补充 |

以上表格只表示“子段预留”，不表示具体消息已经完成分配。后续条目在定义具体消息结构时，应在所属子段内补充精确 `msgId`。

其中会话与路由相关消息在落具体结构时，应复用 `docs/SESSION_ROUTING.md` 中的 `sessionId`、`playerId`、`gameNodeId` 与 `routeEpoch` 语义，避免为同一条路由关系创造多套字段命名。

`Mailbox` 与 `Proxy` 只影响实体的寻址与转发路径，不直接决定上层业务 `msgId` 的分配方式：两者都应先尝试本地命中，远程时统一经 Gate 中转；`Mailbox` 为 Gate 提供静态目标 `GameNodeId`，`Proxy` 为 Gate 提供路由表所在 Gate 信息并由其解析当前 owner。若后续需要为 `Proxy` 定位、Gate 二次寻址或转发附加元数据分配消息，应落入 `2000-3999` 内部中转号段，而不是在当前层提前定义具体业务消息。

**已登记Inner 网络消息**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `1000` | `Inner.ProcessRegister` | `Gate/Game -> GM` | `GM` | `Active` | 进程注册请求；成功或失败响应都复用同一 `msgId` |
| `1100` | `Inner.ProcessHeartbeat` | `Gate/Game -> GM` | `GM` | `Active` | 注册后的周期心跳与轻量负载上报；响应复用同一 `msgId` |
| `1201` | `Inner.ClusterReadyNotify` | `GM -> Gate/Game` | `GM` | `Active` | GM 下发集群 ready 结论；单向通知，无响应 `msgId` |
| `1202` | `Inner.ServerStubOwnershipSync` | `GM -> Game` | `GM` | `Active` | GM 下发 `ServerStubEntity -> OwnerGameNodeId` 的全量 ownership 快照 |
| `1203` | `Inner.GameServiceReadyReport` | `Game -> GM` | `game` | `Active` | `Game` 上报当前 `assignmentEpoch` 下的本地 assigned `ServerStubEntity` ready 聚合结果 |
| `1204` | `Inner.GameDirectoryQuery` | `Gate -> GM` | `gate` | `Active` | `Gate` 查询当前 `Game` 路由目录，并可请求建立后续变化订阅；响应复用同一 `msgId` |
| `1205` | `Inner.GameDirectoryNotify` | `GM -> Gate` | `GM` | `Active` | GM 向已订阅 `Gate` 推送新的全量 `Game` 路由目录快照 |

**已登记 Relay 消息**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `2000` | `Relay.ForwardToGame` | `Gate -> Game` | `gate` | `Active` | Gate 将客户端请求中继到已绑定 Game；响应复用同一 `msgId` |
| `2001` | `Relay.PushToClient` | `Game -> Gate` | `game` | `Active` | Game 发起的单向下行推送，由 Gate 重新封装后发送给客户端 |

**命名规范**
1. 每个消息都应维护一个规范英文名，使用 `PascalCase` 片段并以 `.` 分隔，格式为 `<Area>.<Action>` 或 `<Area>.<Subject>.<Action>`。
2. `<Area>` 必须与所属号段的责任域一致，例如 `Inner.ProcessRegister`、`Relay.ForwardToGame`、`Inner.ClusterReadyNotify`。
3. 请求 / 查询 / 命令名称不追加 `Request` 或 `Response` 后缀；响应使用相同 `msgId`，只通过 `Response` 标志位区分。
4. 单向事件统一使用 `Notify` 后缀；面向客户端的主动下行消息统一使用 `Push` 后缀；仅在明确扇出语义时使用 `Broadcast`。
5. 避免使用 `Handle`、`Do`、`Process` 这类宽泛动词，优先使用 `Register`、`Heartbeat`、`Forward`、`Join`、`Leave`、`Sync` 等领域动词。
6. 缩写只允许使用已在项目中固定的名词，例如 `GM`、`Gate`、`Game`、`KCP`；其余名称优先使用完整单词。
7. 文档中的规范英文名应直接映射为代码常量名，去掉 `.` 后保持 `PascalCase`，例如 `Inner.ProcessRegister` → `InnerProcessRegister`。

**登记要求**
1. 任何新增正式 `msgId` 的功能条目，都必须在本文件追加登记记录后才能合入。
2. 单条登记至少包含：数值编号、规范英文名、方向、Owner、状态、简要说明。
3. 如果消息体结构发生不兼容变化，应分配新的 `msgId` 并保留旧编号，不能静默改写旧语义。
4. 日志、metrics 与抓包说明应尽量同时输出数值编号与规范英文名，降低跨语言排障成本。

**登记模板**

| msgId | CanonicalName | Direction | Owner | Status | Description |
| --- | --- | --- | --- | --- | --- |
| `<value>` | `<Area>.<Action>` | `<caller -> callee>` | `<owner>` | `<status>` | `<summary>` |


