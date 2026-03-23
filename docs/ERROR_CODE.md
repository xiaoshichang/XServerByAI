# ERROR_CODE

本文档定义 XServerByAI 当前阶段错误码的取值范围、编码约定与登记规则。所有 native 运行时错误、控制面错误、客户端接入错误与后续 C# 业务错误，在分配正式错误码前都应先更新本文件。

**基础规则**
1. `errorCode` 统一使用 `int32` 语义表示，`0` 固定表示成功，任何非 `0` 值都表示失败。
2. 错误码对外保持非负整数约定；禁止使用负值表达模块私有状态，避免 C++/C# 边界与序列化实现出现符号分歧。
3. 同一个错误码一旦对外分配，不得复用到新语义；废弃时仅标记为 `Deprecated` 并保留历史记录。
4. 错误码只表达稳定的失败类别，不承载动态文本；可读文本、堆栈或调试信息属于日志或可选错误详情，不属于错误码本身。
5. 通用错误优先复用已有公共号段；只有在确实需要区分责任域时才新增域内专属错误码。

**API 返回约定**
1. 项目自有的公开可失败 API 应优先直接返回错误码类型；`0` / `None` 表示成功，非 `0` 表示失败。
2. 当 API 还需要返回业务数据、句柄、视图、长度或新对象标识时，统一通过输出参数或结果结构承载，不再使用 `bool + error_message`、`值或负错误码` 这类双返回通道。
3. `bool` 返回值只应用于纯谓词、状态查询或模块内部辅助函数；一旦调用方需要区分失败原因，就应改为错误码返回。
4. 对外错误码保持可映射到 `int32` 的非负数值语义；模块内使用枚举包装时，也必须满足“成功为 `0`，失败为非 `0`”。
5. 若接口同时提供错误消息文本，该文本只用于诊断展示，不替代稳定错误码，也不改变错误码含义。
6. `TimerManager` 创建接口当前仍是历史兼容例外；后续若要迁移到统一 error-code 风格，必须先消除 `负错误码` 语义后再调整对外签名。

**编码与传输规则**
1. 协议消息通过 `PacketHeader.flags.Error` 表达“当前消息携带错误语义”；不要通过派生 `msgId` 或单独错误消息名表达同一失败。
2. 统一错误返回中的 `errorCode` 字段应使用与其他协议整数一致的网络字节序（大端）编码。
3. 成功响应必须清除 `Error` 标志位；若消息体中仍包含 `errorCode` 字段，其值必须为 `0`。
4. 失败响应或失败通知必须设置 `Error` 标志位，并携带一个已登记的非零 `errorCode`。
5. 是否附带错误详情、上下文字段或可读描述由后续具体消息结构决定，但这些附加字段不得改变 `errorCode` 的稳定语义。
6. `msgId` 负责标识消息类型，`errorCode` 负责标识失败原因；两者职责不能互相替代。

**顶层号段**

| 范围 | 类别 | 用途 |
| --- | --- | --- |
| `1-999` | 通用公共 | 参数校验、状态冲突、超时、权限、限流等可跨模块复用的公共失败 |
| `1000-1999` | 协议 / 序列化 | 包头校验、版本不兼容、flags 非法、编解码与 framing 错误 |
| `2000-2999` | 网络 / 会话 | TCP、KCP、连接状态、会话生命周期与客户端链路问题 |
| `3000-3999` | 控制面 / 路由 | GM 注册、心跳、集群就绪状态下发、路由与内部 RPC 相关失败 |
| `4000-4999` | 运行时 / 互操作 | `nethost`、CLR 加载、函数绑定、宿主生命周期与跨语言互操作失败 |
| `5000-5999` | 基础设施 | 配置、日志、持久化、资源、依赖服务与其他基础设施失败 |
| `10000-19999` | Player 业务 | 玩家实体与玩家域业务失败 |
| `20000-29999` | Space 业务 | 场景实体与场景域业务失败 |
| `30000-34999` | Stub / 全局服务 | 匹配、聊天、排行榜等 `ServerStubEntity` 业务失败 |
| `35000-39999` | 共享业务 | 跨实体复用的业务公共失败 |
| `40000-44999` | 运维 / 诊断 | GM 管理、运维工具、观测与诊断相关失败 |
| `45000-49999` | 测试 / 联调 | 临时验证、回归探针、实验性错误码 |
| `50000+` | 预留 | 后续跨服务器组扩展、大版本迁移或新增系统域 |

**当前预留子段**

| 范围 | 预留对象 | 说明 | 关联条目 |
| --- | --- | --- | --- |
| `1-99` | 参数与输入校验 | 缺字段、非法参数、越界、格式错误 | `M5-16` |
| `100-199` | 状态与冲突 | 状态不匹配、重复操作、资源忙、版本冲突 | `M5-16` |
| `200-299` | 超时与限流 | 超时、取消、频率限制、重试耗尽 | `M4-13` |
| `1000-1099` | 包头 / framing | 魔数错误、版本错误、flags 非法、长度非法 | `M1-06`, `M2-09` |
| `1100-1199` | 序列化 / 分发 | 编解码失败、消息体损坏、未知 `msgId` | `M1-07`, `M2-10` |
| `2000-2099` | 连接与链路 | 连接断开、握手失败、KCP/TCP 链路不可用 | `M2-07`, `M2-08`, `M4-01` |
| `2100-2199` | 会话与鉴权 | 会话不存在、玩家未绑定、路由未建立、会话已失效、鉴权失败与客户端超时 | `M1-12`, `M4-03`, `M4-04`, `M4-15` |
| `3000-3099` | 注册与心跳 | 进程未注册、重复注册、心跳超时、节点不可用 | `M1-09`, `M3-04`, `M3-05` |
| `3100-3199` | 路由与转发 | 无路由、路由失效、路由所有权不匹配、内部 RPC 超时、转发失败 | `M1-10`, `M1-12`, `M4-06`, `M4-13` |
| `4000-4099` | 托管运行时 | `nethost` 初始化、CLR 加载、ABI 版本校验或导出入口绑定失败 | `M1-15`, `M5-01`, `M5-02` |
| `5000-5099` | 配置与日志 | 配置缺失、配置非法、日志输出初始化失败 | `M1-16`, `M2-01`, `M2-02` |
| `5100-5199` | 持久化与资源 | 存储不可用、保存失败、资源不存在、依赖未就绪 | `M5-12`, `M5-13` |
| `10000-10999` | Player 实体核心 | 玩家不存在、状态非法、属性同步失败 | `M5-07`, `M5-09`, `M5-16` |
| `20000-20999` | Space 实体核心 | 场景不存在、场景已满、状态不允许操作 | `M5-07`, `M5-09`, `M5-16` |
| `30000-30499` | MatchService | 排队失败、队列冲突、匹配上下文非法 | `M5-14`, `M5-16` |
| `30500-30999` | ChatService | 频道不存在、禁言、消息投递失败 | `M5-15`, `M5-16` |

以上表格只表示“子段预留”，不表示具体错误码已经完成分配。后续条目在定义错误返回机制或业务失败场景时，应在所属子段内补充精确错误码。

其中会话与路由相关失败在分配具体错误码时，应复用 `docs/SESSION_ROUTING.md` 对 `sessionId`、`routeState`、`gameNodeId` 与 `routeEpoch` 的语义约束，避免把“节点目录版本变化或链路失效”误编码为普通参数错误。

其中 `10000-34999` 业务错误码的责任域划分应与 `docs/DISTRIBUTED_ENTITY.md` 保持一致：`Player` / `Space` 等状态型失败属于 `ServerEntity` 领域，匹配、聊天等 `Stub / 全局服务` 失败属于 `ServerStubEntity` 领域，避免用基础设施错误码承载实体业务语义。

`Mailbox` / `Proxy` 的寻址结果也应服从这一分层：实体内部业务拒绝、状态冲突或领域校验失败进入对应业务号段；而 `Proxy` 无法解析当前 owner、Gate 二次寻址失败或路由元数据失效，则应优先归入 `3000-3999` 的控制面 / 路由错误，而不是误登记为玩家或场景业务错误。

**已登记控制面错误码**

| errorCode | CanonicalName | Domain | Status | Owner | Description |
| --- | --- | --- | --- | --- | --- |
| `3000` | `Control.ProcessTypeInvalid` | `control` | `Active` | `gm` | `processType` 非法或当前阶段不支持该进程类型 |
| `3001` | `Control.NodeIdConflict` | `control` | `Active` | `gm` | `nodeId` 已被活动注册占用，当前连接不能重复注册 |
| `3002` | `Control.ServiceEndpointInvalid` | `control` | `Active` | `gm` | 注册消息缺少可发布服务地址，或端口配置非法 |
| `3003` | `Control.NodeNotRegistered` | `control` | `Active` | `gm` | 心跳或其他控制请求没有命中活动节点，当前连接需要重新注册 |
| `3004` | `Control.ControlChannelInvalid` | `control` | `Active` | `gm` | 当前控制链路已失效或不再拥有该 `nodeId`，发送方必须重新注册 |
| `3005` | `Control.HeartbeatRequestInvalid` | `control` | `Active` | `gm` | 心跳请求包头或消息体不符合控制面约束，发送方应修正请求后重试 |

**已登记 Relay 错误码**

| errorCode | CanonicalName | Domain | Status | Owner | Description |
| --- | --- | --- | --- | --- | --- |
| `3100` | `Relay.ClientMessageIdInvalid` | `relay` | `Active` | `game` | 中继封装中的 `clientMsgId` 为 `0` 或落在内部保留号段，不能作为客户端消息编号 |
| `3101` | `Relay.SessionNotFound` | `relay` | `Active` | `game` | 目标 `sessionId` 在当前接收方上下文中不存在 |
| `3102` | `Relay.RouteOwnershipMismatch` | `relay` | `Active` | `game` | `sessionId` / `playerId` 与当前 Game 持有的路由或绑定关系不一致 |
| `3103` | `Relay.ClientFlagsInvalid` | `relay` | `Active` | `game` | `clientFlags` 包含非法组合或未定义标志位 |

**已登记互操作错误码**

| errorCode | CanonicalName | Domain | Status | Owner | Description |
| --- | --- | --- | --- | --- | --- |
| `4000` | `Interop.RuntimeAlreadyLoaded` | `interop` | `Active` | `host` | 当前进程内的 CLR 宿主已经完成初始化，禁止在未卸载宿主状态前重复执行 `M5-01` 初始化 |
| `4001` | `Interop.RuntimeConfigPathEmpty` | `interop` | `Active` | `host` | 调用方没有提供 `runtimeconfig.json` 路径 |
| `4002` | `Interop.RuntimeConfigNotFound` | `interop` | `Active` | `host` | 指定的 `runtimeconfig.json` 文件不存在或不可作为宿主初始化输入 |
| `4003` | `Interop.AssemblyPathEmpty` | `interop` | `Active` | `host` | 调用方没有提供根程序集 `.dll` 路径 |
| `4004` | `Interop.AssemblyNotFound` | `interop` | `Active` | `host` | 指定的根程序集 `.dll` 文件不存在 |
| `4005` | `Interop.HostfxrPathResolveFailed` | `interop` | `Active` | `host` | `nethost` 无法解析 `hostfxr` 动态库路径 |
| `4006` | `Interop.HostfxrLibraryLoadFailed` | `interop` | `Active` | `host` | 已解析的 `hostfxr` 动态库无法装载到当前 native 进程 |
| `4007` | `Interop.HostfxrExportLoadFailed` | `interop` | `Active` | `host` | `hostfxr_initialize_for_runtime_config`、`hostfxr_get_runtime_delegate` 或 `hostfxr_close` 等必需导出缺失 |
| `4008` | `Interop.RuntimeInitializeFailed` | `interop` | `Active` | `host` | `hostfxr_initialize_for_runtime_config` 未能基于目标 `runtimeconfig.json` 初始化 CLR 宿主上下文 |
| `4009` | `Interop.RuntimeDelegateLoadFailed` | `interop` | `Active` | `host` | `hostfxr_get_runtime_delegate` 未能返回 `load_assembly_and_get_function_pointer` 委托 |
| `4010` | `Interop.RuntimeContextCloseFailed` | `interop` | `Active` | `host` | `hostfxr_close` 未能正常关闭 `M5-01` 初始化阶段的临时宿主上下文 |
**命名规范**
1. 每个错误码都应维护一个规范英文名，使用 `PascalCase` 片段并以 `.` 分隔，格式为 `<Area>.<Reason>` 或 `<Area>.<Subject>.<Reason>`。
2. 公共错误优先使用 `Common` 作为域名，例如 `Common.InvalidArgument`、`Common.Timeout`、`Common.RateLimited`。
3. 模块专属错误应使用与责任域一致的前缀，例如 `Protocol.BadMagic`、`Relay.RouteNotFound`、`Interop.RuntimeLoadFailed`、`Player.NotFound`、`Space.Full`。
4. 规范英文名直接映射为代码常量名，去掉 `.` 后保持 `PascalCase`，例如 `Protocol.BadMagic` → `ProtocolBadMagic`。
5. 避免使用 `UnknownError`、`Failed` 这类信息量过低的宽泛命名；优先使用能够直接定位根因或责任域的名词短语。

**登记要求**
1. 任何新增正式错误码的功能条目，都必须先在本文件追加登记记录后才能合入。
2. 单条登记至少包含：数值编号、规范英文名、适用域、状态、Owner、简要说明。
3. 如果实现发生不兼容语义变更，应分配新的错误码并保留旧编号，不能静默修改既有含义。
4. C++ 与 C# 侧都应以同一数值作为最终对外语义；映射层只能做别名兼容，不能为同一失败制造双编号。
5. 日志、metrics、抓包与管理工具应尽量同时输出数值错误码与规范英文名，降低跨语言排障成本。

**登记模板**

| errorCode | CanonicalName | Domain | Status | Owner | Description |
| --- | --- | --- | --- | --- | --- |
| `1` | `Common.InvalidArgument` | `common` | `Reserved` | `core` | 非法参数，占位示例 |
