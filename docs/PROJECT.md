# 项目说明（初版）

本文档描述当前多进程游戏服务器框架的目标、整体架构、关键技术边界与长期约定。当前阶段仍以“工程骨架 + 规范固化”为主，不包含完整业务玩法实现。

**目标**
1. 在允许多机多进程部署的架构前提下，实现 `GM` / `Gate` / `Game` 三类进程协作的最小可运行架构；开发期默认以单机多进程方式联调与测试。
2. 采用 C++ 作为底层运行时与系统能力实现，C# 作为业务逻辑实现。
3. 通过 `nethost` 托管 CLR，实现 C++ → C# 的高性能互操作。
4. 客户端连接采用 KCP；节点间通信默认使用基于 ZeroMQ over TCP 的传输，应用层继续复用统一的二进制消息体协议。

**非目标**
1. 当前阶段不引入独立分布式注册中心、跨机房复制或跨地域多活部署能力。
2. 不引入第三方 RPC 框架；节点间二进制协议仍由项目自定义实现。当前仅接入基础设施级依赖 `spdlog`、`zeromq/libzmq`、standalone `asio` 与 header-only `nlohmann/json`（不携带其他 Boost 组件），其中 `nlohmann/json` 仅用于 JSON 序列化/反序列化与配置载体读写。
3. 不在本阶段定义完整的业务玩法、场景迁移或多活写入模型。

**运行环境**
1. 兼容 Windows 与 Linux。
2. C++ 构建使用 CMake。
3. C# 构建使用 `dotnet`（.NET 运行时由 `nethost` 加载）。
4. 基础第三方依赖以 vendored 源码形式统一放在 `3rd/`，当前基线为 `spdlog`、`zeromq/libzmq`、standalone `asio` 与 header-only `nlohmann/json`。

**进程角色**
1. `GM` 进程  
每个服务器组固定 `1` 个，负责该组的控制面，包括启动/关闭流程、注册表、健康检查、配置分发与路由目录维护。
2. `Gate` 进程  
每个服务器组部署 `N` 个（`N >= 1`），负责客户端连接维护、KCP 会话管理、鉴权与路由转发，不承载业务逻辑。
3. `Game` 进程  
每个服务器组部署 `M` 个（`M >= 1`），负责核心游戏逻辑与实体状态承载。

**统一 Node 入口**
1. native 统一使用单个可执行文件 `xserver-node` 作为 `GM` / `Gate` / `Game` 的启动入口。
2. 启动命令形式固定为 `xserver-node <configPath> <selector>`，例如 `xserver-node config.json gm`、`xserver-node config.json gate0`、`xserver-node config.json game0`。
3. `selector` 仅用于从单个配置文件中选择实例；协议层与注册表中继续使用稳定逻辑身份 `GM`、`Gate0`、`Game0`。

**NodeID 约定**
1. Gate 与 Game 的稳定逻辑身份统一称为 `NodeID`。
2. `NodeID` 使用区分大小写的 `<ProcessType><index>` 格式，例如 `Gate0`、`Gate1`、`Game0`、`Game1`。
3. 同一服务器组内 `NodeID` 必须唯一；进程重启后应复用原 `NodeID`。

**服务器组拓扑（开发期默认单机多进程）**
1. 单个服务器组固定为 `1 GM + N Gate + M Game`，其中 `N >= 1`、`M >= 1`。
2. GM 与所属服务器组内每个 Gate/Game 通过基于 ZeroMQ over TCP 的内部控制链路通信。
3. 同一服务器组内 Gate 与 Game 通过基于 ZeroMQ over TCP 的内部链路形成全连接拓扑。
4. Game 与 Game 之间不直接连接。
5. Gate 与 Gate 之间不直接连接。
6. 客户端通过 UDP + KCP 与 Gate 通信。
7. 同一服务器组内的节点可以部署在同一台机器，也可以拆分到多台机器；协议、NodeID 与路由模型不依赖单机假设。

**启动与注册流程（建议）**
1. GM 启动，监听控制端口并加载配置。
2. Game 启动后向 GM 注册，初始化托管层与本地 `ServerStubEntity`；只有当本进程要求的 `ServerStubEntity` 全部 ready 后，才向 GM 上报本地服务就绪。
3. Gate 启动后向 GM 注册，并拉取 Game 路由目录或订阅变更；在收到 GM 的集群就绪放行指令前，必须保持客户端连接入口关闭。
4. GM 聚合各 Game 上报的 `ServerStubEntity ready` 状态；只有当服务器组内要求的 `ServerStubEntity` 全部 ready 后，才向 Gate 下发开放客户端入口的控制消息。
5. 客户端连接 Gate，完成鉴权后建立会话，Gate 将会话绑定到目标 Game 节点。
6. 注册、心跳、服务就绪与入口开放控制消息的结构、字段含义与默认时序约定见 `docs/PROCESS_CONTROL.md`。

**进程间二进制协议**
1. 节点间业务消息体统一使用网络字节序（大端）编码的固定包头与二进制 body。
2. 包头结构定义见 `docs/PACKET_HEADER.md`，固定长度为 `20` 字节。
3. `msgId` 采用集中分段管理；控制面、Gate↔Game 中继、客户端接入与实体业务消息使用独立号段。
4. 节点间传输默认由 ZeroMQ 提供消息边界；如后续在流式 TCP 适配中复用本协议，则采用“长度前缀 + 包头 + 包体”的 framing。
5. 包头常量见 `docs/PACKET_HEADER.md`，`msgId` 分段与命名细则见 `docs/MSG_ID.md`，错误码范围与编码规则见 `docs/ERROR_CODE.md`。

**Gate ↔ Game 通信模型**
1. 同一服务器组内，每个 Gate 与每个 Game 节点维持直接的 ZeroMQ over TCP 内部链路，构成全连接拓扑。
2. Gate↔Gate 与 Game↔Game 不直接通信；所有客户端业务转发都经由 Gate ↔ Game 链路完成。
3. 请求类型消息需要 `seq`，响应消息回显同一 `seq`。
4. 支持双向推送，例如 `Game -> Gate -> Client`。
5. Gate↔Game 中继封装字段、响应语义与客户端包元数据约定见 `docs/GATE_GAME_ENVELOPE.md`。

**KCP 连接模型（客户端）**
1. 客户端与 Gate 通过 UDP 建立 KCP 会话。
2. KCP 会话与客户端账号/角色绑定，Gate 维护会话生命周期。
3. Gate 侧 KCP 配置项、默认值与调优边界见 `docs/KCP_CONFIG.md`。
4. KCP 算法参数与应用层心跳、断线清理和路由策略分离定义；KCP 负责传输层重传与拥塞相关行为，会话与路由模型见 `docs/SESSION_ROUTING.md`。

**会话与路由**
1. Gate 维护 `sessionId -> SessionRecord` 主表，以及按 `playerId`、`gameNodeId` 反查的索引；字段语义见 `docs/SESSION_ROUTING.md`。
2. 当前默认采用“Gate 会话固定绑定入站 Game 节点”的传输路由模型；若目标 Game 失效，会话进入 `RouteLost`，不做静默迁移。
3. GM 维护 Game 节点注册表、租约与负载信息，供 Gate 生成本地 `GameDirectoryEntry` 路由目录。

**分布式实体架构**
1. C# 业务层采用 `ServerEntity` / `ServerStubEntity` 两级模型，完整语义见 `docs/DISTRIBUTED_ENTITY.md`。
2. Gate 只负责客户端连接、会话与转发，不持有业务实体状态；GM 只负责控制面与路由目录；业务实体统一由 Game 承载。
3. `PlayerEntity` 可迁移，`SpaceEntity` 不可迁移；`ServerStubEntity` 的承载 Game 在启动时确定，运行期不迁移。
4. `ServerStubEntity ready` 是集群级启动门闩：Game 负责本地 ready 判定并上报 GM，GM 聚合后再通知 Gate 开放客户端入口；Gate 不得自行推断集群是否已 ready。
5. 实体间 RPC 默认分为两种寻址方式：静态 `Mailbox` 用于不可迁移实体，动态 `Proxy` 用于可迁移实体。
6. 当前默认业务链路为 `session -> PlayerEntity Proxy -> SpaceEntity Mailbox / other server entity / ServerStubEntity Mailbox`。
7. 当前阶段不定义 active-active 多写，也不定义跨 Game 直接业务通信。

**心跳与健康检查（建议默认值）**
1. Gate/Game 对 GM 心跳间隔建议为 `5s`，超时建议为 `15s`。
2. GM 在超时后标记实例不可用，并通知 Gate 更新路由目录。
3. 所有心跳与状态变更均通过内部协议完成，避免额外外部依赖。

**C++ ↔ C#（nethost）互操作**
1. 当前阶段仅 `Game` 进程承载 CLR，根程序集固定为 `XServer.Managed.GameLogic`，ABI 约定见 `docs/MANAGED_INTEROP.md`。
2. C++ 通过 `nethost` / `hostfxr` / `load_assembly_and_get_function_pointer` 加载 .NET 运行时并解析托管导出入口。
3. C# 导出类型固定为 `XServer.Managed.GameLogic.Interop.GameNativeExports`，入口名固定为 `GameNativeGetAbiVersion`、`GameNativeInit`、`GameNativeOnMessage`、`GameNativeOnTick`。
4. 所有导出入口统一使用 `cdecl` + `UnmanagedCallersOnly`，并在首次业务调用前做 ABI 版本校验。
5. 互操作数据结构必须保持 blittable；输入缓冲区只在调用期间借用，托管异常不得跨边界传播到 native 侧。

**线程与调度模型（建议）**
1. 每个进程采用以 standalone `asio::io_context` 为核心的主线程事件循环，网络 IO 与定时器以异步方式驱动。
2. 定时器与超时能力统一基于 standalone `asio::steady_timer`，项目侧只补充业务所需的封装、生命周期与取消语义。
3. 可选轻量工作线程池应优先基于 standalone `asio` executor / `io_context::run()` 组织，必要时使用 `strand` 保证串行化，而不是额外自研调度器。
4. Game 内部逻辑采用固定 Tick（例如 `20ms` 或 `50ms`），Tick 频率可配置。
**配置与日志（建议）**
1. 进程配置统一使用单个 UTF-8 JSON 载体；同一服务器组可以共享一份逻辑配置，开发期默认将 `GM` / `Gate` / `Game` 实例都放在一个配置文件中，实例选择规则见 `docs/CONFIG_LOGGING.md`。
2. Gate / Game 的启动选择器使用 `gate0`、`game0` 这类小写形式，而稳定 `NodeID` 仍为 `Gate0`、`Game0`；例如 KCP 子块字段复用 `gate.<selector>.kcp` 逻辑键名。
3. `Game` 实例块必须显式提供内部服务地址，例如 `game.game0.service.listenEndpoint`，用于注册到 GM 并供 Gate 通过 ZeroMQ over TCP 建立节点间连接。
4. native 日志模块当前基线为 `spdlog`；配置键名、等级与文件组织规则仍统一以 `docs/CONFIG_LOGGING.md` 为准。
5. native 侧 JSON 序列化/反序列化与配置文件读写当前基线为 vendored header-only `nlohmann/json`；仅用于单文件 JSON 配置与后续结构化数据读写，不替代节点间二进制协议。
6. 日志默认输出到 `logs/` 根目录下的平铺文件，例如 `GM-2026-03-15.log`、`Gate0-2026-03-15.log`；统一等级、字段、滚动与错误码呈现规则见 `docs/CONFIG_LOGGING.md`。

**目录结构**
1. `src/native/` C++ 核心、统一 `xserver-node` 入口与三类进程实现。
2. `src/managed/` C# 业务逻辑与公共接口。
3. `configs/` 运行配置。
4. `logs/` 运行期日志输出目录（默认不纳入版本控制）。
5. `3rd/` vendored 第三方依赖目录；每个依赖单独建目录，包含源码与必要的项目侧构建包装。
6. `cmake/` CMake 公共脚本与工具链配置。
7. `docs/` 文档与规范，包含协议说明。
8. 目录、命名与命名空间细则统一见 `docs/CONVENTIONS.md`。

**后续建议里程碑**
1. 先实现 GM + Game 的最小握手与心跳。
2. 再接入 Gate 与 KCP 会话管理。
3. 最后完善路由策略、负载汇总与稳定性优化。
