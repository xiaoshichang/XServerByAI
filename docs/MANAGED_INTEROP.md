# MANAGED_INTEROP

本文档定义 XServerByAI 当前主线代码中 native `Game` / `GM` 与 managed `XServer.Managed.Framework` 之间的互操作 ABI、导出入口、回调表与运行时约束。本文档只保留当前最新生效状态。

## 适用范围

1. `Game` 进程会宿主 CLR，加载 `XServer.Managed.Framework`，绑定并调用 `GameNative*` 导出。
2. `GM` 进程也会加载同一 managed 程序集，但当前只使用 catalog 导出读取 `ServerStub` 目录，不调用 `GameNativeInit`，也不驱动 managed 业务循环。
3. 本文档只描述 native <-> managed ABI 与当前实现约束，不重复定义业务消息、路由、实体或全局错误码语义；这些内容分别复用 `docs/MSG_ID.md`、`docs/SESSION_ROUTING.md`、`docs/DISTRIBUTED_ENTITY.md` 和 `docs/ERROR_CODE.md`。

## 当前生效版本

| 名称 | 值 | 当前代码位置 |
| --- | --- | --- |
| `XS_MANAGED_ABI_VERSION` | `9` | `src/native/host/ManagedInterop.h` |
| `ManagedAbi.Version` | `9` | `src/managed/Framework/Interop/ManagedAbi.cs` |
| 导出类型名 | `XServer.Managed.Framework.Interop.GameNativeExports, XServer.Managed.Framework` | native / managed 两侧一致 |

当前 native 侧还通过静态断言锁定关键布局，说明现行实现按 x64 ABI 在编译期校验以下尺寸：

- `ManagedNativeCallbacks = 72` bytes
- `ManagedInitArgs = 120` bytes
- `ManagedServerStubCatalogEntry = 272` bytes
- `ManagedServerStubOwnershipEntry = 404` bytes
- `ManagedServerStubReadyEntry = 276` bytes

## 装载与程序集约定

1. native 通过 `nethost` 解析 `hostfxr`，再获取 `load_assembly_and_get_function_pointer`，不使用私有 CLR 启动路径。
2. 当前目标 runtime config 为 `XServer.Managed.Framework.runtimeconfig.json`，根程序集为 `XServer.Managed.Framework.dll`。
3. native 在绑定任何其他导出前，必须先解析并调用 `GameNativeGetAbiVersion`；若返回值不等于 `9`，应立即按 `Interop.AbiVersionMismatch` 处理。
4. `ManagedRuntimeHost::BindExports()` 当前会一次性绑定以下导出：
   - `GameNativeGetAbiVersion`
   - `GameNativeInit`
   - `GameNativeOnMessage`
   - `GameNativeOnTick`
   - `GameNativeOnNativeTimer`
   - `GameNativeApplyServerStubOwnership`
   - `GameNativeResetServerStubOwnership`
   - `GameNativeGetReadyServerStubCount`
   - `GameNativeGetReadyServerStubEntry`
   - `GameNativeGetServerStubCatalogCount`
   - `GameNativeGetServerStubCatalogEntry`

## ABI 基础约定

1. 所有导出与回调统一使用 `cdecl` 调用约定。
2. 所有共享结构必须使用顺序布局，只允许 blittable 字段；禁止跨边界直接传递 `string`、`object`、托管数组或托管引用。
3. 所有字符串统一使用 UTF-8 `pointer + byteLength` 或固定大小 UTF-8 缓冲区表达，不使用 NUL 结尾作为协议语义。
4. `bool` 跨 ABI 统一使用 `byte` / `uint8`：`0` 表示 false，`1` 表示 true。
5. 输入指针只在当前调用期间借用；managed 若需延后使用，必须在调用期间自行复制。
6. managed 导出不得让异常穿过 ABI 边界；当前实现统一在 managed 侧捕获异常并返回本地状态码。
7. `struct_size` 字段用于前向兼容校验；调用方至少要填入当前已知结构尺寸。
8. 当前固定缓冲区上限为：
   - `NodeId = 128` UTF-8 bytes
   - `ServerStub.EntityType = 128` UTF-8 bytes
   - `ServerStub.EntityId = 128` UTF-8 bytes

## 共享结构

### `ManagedNativeCallbacks`

`GameNativeInit` 会接收一张 native 回调表；managed 保存后用于主动回调 native。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 当前回调表结构大小 |
| `reserved0` | `uint32` | 保留，当前填 `0` |
| `context` | `pointer` | native 回调上下文，当前为 `GameNode*` |
| `on_server_stub_ready` | function pointer | managed `ServerStub` 就绪后主动通知 native |
| `on_log` | function pointer | managed 日志桥接回 native logger |
| `create_once_timer` | function pointer | 向 native 事件循环注册一次性定时器 |
| `cancel_timer` | function pointer | 取消前述一次性定时器 |
| `forward_stub_call` | function pointer | managed 发起 `Mailbox/Stub` 路由转发 |
| `forward_proxy_call` | function pointer | managed 发起服务端 `Proxy` 路由转发 |
| `push_client_message` | function pointer | managed 经由 Gate 向客户端推送消息 |

### `ManagedInitArgs`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `abi_version` | `uint32` | 当前必须为 `9` |
| `process_type` | `uint16` | 当前调用方进程类型；现阶段只有 `Game` 真正调用 `GameNativeInit` |
| `reserved0` | `uint16` | 保留 |
| `node_id_utf8` | `pointer` | 当前节点 `NodeID` 的 UTF-8 缓冲区 |
| `node_id_length` | `uint32` | `node_id_utf8` 长度 |
| `config_path_utf8` | `pointer` | 配置文件路径 UTF-8 缓冲区 |
| `config_path_length` | `uint32` | `config_path_utf8` 长度 |
| `native_callbacks` | `ManagedNativeCallbacks` | native 回调表 |

### `ManagedMessageView`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `msg_id` | `uint32` | 业务消息号 |
| `seq` | `uint32` | 当前未作为 managed runtime 核心语义使用 |
| `flags` | `uint32` | 当前未作为 managed runtime 核心语义使用 |
| `session_id` | `uint64` | 会话标识；当前多数 managed 入站消息不依赖此字段 |
| `player_id` | `uint64` | 玩家标识；当前多数 managed 入站消息不依赖此字段 |
| `payload` | `pointer` | 消息体缓冲区，可为 `null` |
| `payload_length` | `uint32` | 消息体长度 |
| `reserved0` | `uint32` | 保留 |

### `ManagedServerStubCatalogEntry`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `entity_type_length` | `uint32` | `entity_type_utf8` 有效长度 |
| `entity_type_utf8` | `uint8[128]` | `ServerStub` 类型名 |
| `entity_id_length` | `uint32` | `entity_id_utf8` 有效长度 |
| `entity_id_utf8` | `uint8[128]` | 当前 catalog 阶段的实体 ID 占位值 |
| `reserved0` | `uint32` | 保留 |

当前实现中，catalog 的 `entity_id` 固定返回占位值 `unknown`。真实运行时实体 ID 来自 stub 实例化后产生的 managed 实体。

### `ManagedServerStubOwnershipEntry`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `entity_type_length` | `uint32` | `entity_type_utf8` 有效长度 |
| `entity_type_utf8` | `uint8[128]` | stub 类型名 |
| `entity_id_length` | `uint32` | `entity_id_utf8` 有效长度 |
| `entity_id_utf8` | `uint8[128]` | 当前分配的实体 ID |
| `owner_game_node_id_length` | `uint32` | `owner_game_node_id_utf8` 有效长度 |
| `owner_game_node_id_utf8` | `uint8[128]` | owner `GameNodeId` |
| `entry_flags` | `uint32` | 分配附带标志；当前 ABI 仅透传，不单独定义位语义 |

### `ManagedServerStubOwnershipSync`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `status_flags` | `uint32` | 额外状态标记；当前 ABI 仅透传 |
| `assignment_epoch` | `uint64` | 当前 ownership 版本号 |
| `server_now_unix_ms` | `uint64` | server 当前时间戳 |
| `assignment_count` | `uint32` | assignment 数量 |
| `reserved0` | `uint32` | 保留 |
| `assignments` | `pointer` | 指向 `ManagedServerStubOwnershipEntry[]` |

### `ManagedServerStubReadyEntry`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `struct_size` | `uint32` | 结构大小 |
| `entity_type_length` | `uint32` | `entity_type_utf8` 有效长度 |
| `entity_type_utf8` | `uint8[128]` | 就绪 stub 类型名 |
| `entity_id_length` | `uint32` | `entity_id_utf8` 有效长度 |
| `entity_id_utf8` | `uint8[128]` | 就绪 stub 实体 ID |
| `ready` | `uint8` | 当前实现中应为 `1` |
| `reserved0` | `uint8[3]` | 保留 |
| `entry_flags` | `uint32` | 就绪附带标志；当前实现默认写 `0` |

## 当前导出入口

### `GameNativeGetAbiVersion`

- 返回当前 ABI 版本号，现为 `9`。
- native 在绑定其他导出前必须先验证此值。

### `GameNativeInit`

- 当前只在 `GameNode::InitializeManagedRuntime()` 中调用。
- 初始化 managed runtime 状态，保存 native 回调表，并配置：
  - `NativeLoggerBridge`
  - `ManagedNativeServerEntityMessageTransport`
  - `ManagedNativeTimerScheduler`
  - `GameNodeRuntimeState`
- 成功返回 `0`；参数不合法或初始化失败返回负值本地状态码。

### `GameNativeOnMessage`

当前实现不是“任意业务消息总入口”，而是只处理以下几类已落地消息：

1. `2003`：创建 `AvatarEntity`。
2. `RelayProxyCallCodec.ForwardProxyCallMsgId (2005)`：把转发后的服务端代理调用投递给目标实体。
3. `RelayMailboxCallCodec.ForwardMailboxCallMsgId (2002)`：把转发后的 mailbox 调用投递给当前 `GameNodeId` 下的目标实体 / stub。

其余消息当前直接返回 `0` 并忽略。

### `GameNativeOnTick`

- 保留导出，当前实现直接返回 `0`。
- 现阶段 managed 运行时不依赖该导出驱动主逻辑，更多依赖显式消息与 native timer bridge。

### `GameNativeOnNativeTimer`

- 由 native 侧一次性定时器触发后回调。
- 传入 `timerId`，由 managed `ManagedNativeTimerScheduler` 查找并执行本地回调。
- 成功返回 `0`，否则返回负值本地状态码。

### `GameNativeApplyServerStubOwnership`

- native 在收到 `Inner.ServerStubOwnershipSync (1202)` 后调用。
- managed 侧会把 ownership 快照转成 `ServerStubOwnershipSnapshot`，并在当前节点创建本地拥有的 stub 实例。
- 成功返回 `0`。
- 若 `GameNodeRuntimeState.ApplyOwnership()` 失败，当前实现返回 `1000 + GameNodeRuntimeStateErrorCode`。

### `GameNativeResetServerStubOwnership`

- 清空 managed 侧 ownership / 本地 owned stub 状态。
- 当前实现成功返回 `0`。

### `GameNativeGetReadyServerStubCount` / `GameNativeGetReadyServerStubEntry`

- 提供 query-style ready 快照读取。
- 当前仍保留，主要用于测试、诊断与兼容；实际启动链路中的 authoritative ready 上报已是 callback-driven。

### `GameNativeGetServerStubCatalogCount` / `GameNativeGetServerStubCatalogEntry`

- 供 `GM` 读取 managed server stub 目录。
- `GM` 当前只依赖这两个导出做 stub 发现，不进入 `GameNativeInit`。

## 当前回调语义

### `on_server_stub_ready`

1. managed `ServerStub` 进入 ready 状态时立即回调 native。
2. native 会把回调投递回 `Game` 事件循环线程，再更新本地 `ready_entries`。
3. 当本地 owned stubs 全部 ready 后，`Game` 才向 `GM` 发送 `Inner.GameServiceReadyReport (1203)`。

### `on_log`

1. managed 通过 `NativeLoggerBridge` 把日志级别、category、message 以 UTF-8 形式回传 native。
2. 当前桥接是 best-effort：若未提供回调或编码失败，managed 逻辑继续执行，不把日志桥接失败当作业务失败。

### `create_once_timer` / `cancel_timer`

1. managed 只能通过 ABI 请求 native 事件循环创建一次性定时器。
2. 创建成功时返回 `timerId > 0`；失败时返回负值 `NativeTimerErrorCode`。
3. 定时器触发后 native 反向调用 `GameNativeOnNativeTimer(timerId)`。

### `forward_stub_call`

1. 供 managed runtime 发送 mailbox / stub 路由调用。
2. `target_game_node_id` 可为空；为空时 native 会按当前 ownership 表查找目标 stub 的 owner。
3. native 当前把该调用编码为 `RelayForwardMailboxCall`，经某个已就绪 Gate 转发。

### `forward_proxy_call`

1. 供 managed runtime 发送服务端 `Proxy` 调用。
2. native 当前把该调用编码为 `RelayForwardProxyCall`，经指定 `route_gate_node_id` 转发。

### `push_client_message`

1. 供 managed runtime 通过 Gate 向客户端推送消息。
2. native 当前把该调用编码为 `RelayPushToClient`，经指定 `route_gate_node_id` 转发。

## 当前运行时流程

### `GM` 目录发现流程

1. `GM` 加载 managed runtime host。
2. 绑定所有导出。
3. 仅读取 `GameNativeGetServerStubCatalogCount/Entry`。
4. 生成 server stub 状态表，并把 `entity_id` 初始占位为 `unknown`。

### `Game` 启动流程

1. `GameNode::InitializeManagedRuntime()` 加载 CLR 并绑定全部导出。
2. native 构造 `ManagedNativeCallbacks` 与 `ManagedInitArgs`，调用 `GameNativeInit`。
3. `GM` 下发 `Inner.ServerStubOwnershipSync (1202)`。
4. native 把 ownership 同步转换为 `ManagedServerStubOwnershipSync`，调用 `GameNativeApplyServerStubOwnership`。
5. managed 创建本节点拥有的 stub，并在就绪后通过 `on_server_stub_ready` 回调 native。
6. native 聚合所有本地 ready 项，全部就绪后发送 `Inner.GameServiceReadyReport (1203)` 给 `GM`。

### `Game` 运行期消息与路由

1. Gate -> Game 的创建角色请求当前通过 `GameNativeOnMessage(msgId=2003)` 驱动 managed 创建 `AvatarEntity`。
2. Game / Gate / GM 之间的 relay 消息会在 native 解包后，以 `ManagedMessageView` 投递到 managed 的 `GameNativeOnMessage`。
3. managed 内部实体若需要跨节点 mailbox 调用、服务端 proxy 调用或客户端推送，会反向使用 `forward_stub_call`、`forward_proxy_call`、`push_client_message`。

## 错误处理与兼容策略

1. CLR 加载、`hostfxr` 初始化、导出绑定与 ABI 版本校验失败，统一落到 `docs/ERROR_CODE.md` 中 `4000-4014` 的 host / interop 错误码。
2. `GameNative*` 导出当前返回的是“局部状态码”，不是全局注册错误码：
   - `0` 表示成功
   - 负值通常表示参数非法、运行时未初始化、索引越界或内部操作失败
   - mailbox / proxy / stub transport 回调使用其各自的枚举返回码
3. 如需改变导出名、调用约定、结构布局或字段语义，必须提升 ABI 版本，不得静默覆写 `v9` 语义。
