# 项目说明（初版）

本文档描述 XServerByAI 当前阶段的目标、节点拓扑、网络分层与关键技术边界。当前阶段以“统一节点入口 + 配置基线 + 协议骨架 + 运行时骨架”为主，不展开完整业务玩法实现。

**目标**
1. 以单个 `xserver-node` 可执行程序承载 `GM`、`Gate`、`Game` 三类节点。
2. 以 C++ 负责底层运行时、网络与宿主边界，以 C# 负责 `Game` 侧业务逻辑。
3. 以单个 UTF-8 JSON 配置文件描述一个服务器组的集群配置与节点实例配置。
4. 为 `Inner` 集群通信、`Client` 接入以及 `Control` 管理能力提供统一命名和稳定骨架。

**非目标**
1. 当前阶段不引入独立分布式注册中心、跨机房复制或跨地域多活部署能力。
2. 不引入第三方 RPC 框架；节点间二进制协议仍由项目自定义实现。当前 native 基础设施依赖为 `spdlog`、`zeromq/libzmq`、standalone `asio` 与 header-only `nlohmann/json`（不携带其他 Boost 组件）；此外为 C++↔C# 宿主链路 vendored 官方 .NET native hosting artifacts（`nethost` / `hostfxr` 头文件与平台 `nethost` 链接工件），其中 `nlohmann/json` 仅用于 JSON 序列化/反序列化与配置载体读写。
3. 不在本阶段定义完整的业务玩法、场景迁移或多活写入模型。

**平台与依赖**
1. 目标平台为 Windows 与 Linux。
2. C++ 构建使用 CMake。
3. C# 构建使用 `dotnet`（.NET 运行时由 `nethost` 加载）。
4. 基础第三方依赖以 vendored 源码或官方 host pack 工件形式统一放在 `3rd/`，当前基线为 `spdlog`、`zeromq/libzmq`、standalone `asio`、header-only `nlohmann/json` 与 .NET 官方 `dotnet_host`（`nethost` / `hostfxr` headers + platform `nethost` link artifacts）。

**节点角色**
1. `GM`：集群协调节点，负责注册、心跳、“所有节点已上线”通知、Game↔Gate mesh ready 聚合、ownership 分配、ready 聚合与 `clusterReady` 编排。
2. `Gate`：客户端接入节点，负责 `Client` 网络、会话管理、维护已注册 `Game` 目录以及 Gate/Game 中继。
3. `Game`：业务承载节点，负责托管逻辑、实体状态、对全部 `Gate` 的注册/心跳、mesh ready 上报、按 ownership 初始化 Stub 以及本地 ready 上报。

**网络分层命名**
1. `Inner`：节点与节点之间的网络，当前主要承载 `GM <-> Gate/Game` 与 `Game -> Gate` 的注册、心跳及启动编排消息。
2. `Control`：ctrl 工具与 `GM` 之间的管理网络。当前阶段已落地 `GM` 本地 HTTP 管理接口，并统一沿用 `ControlNetwork`、`controlNetwork.listenEndpoint` 命名。
3. `Client`：`Gate` 与客户端之间的网络，对应 `ClientNetwork`、`clientNetwork.listenEndpoint` 与集群级 `kcp`。

**NodeID 约定**
1. Gate 与 Game 的稳定逻辑身份统一称为 `NodeID`。
2. `NodeID` 使用 `<ProcessType><index>` 形式，例如 `Gate0`、`Gate1`、`Game0`、`Game1`。
3. 启动参数中的 `nodeId` 只用于实例选择；当前与 `NodeID` 取值保持一致。

**拓扑**
1. 当前默认拓扑为 `1 GM + N Gate + M Game`，其中 `N >= 1`、`M >= 1`。
2. `GM` 与每个 `Gate` / `Game` 通过 `Inner` 网络直接通信。
3. `Gate` 与 `Game` 之间通过 `Inner` 网络形成“`Game` 主动注册到全部 `Gate`”的中继平面。
4. 客户端只与 `Gate` 的 `Client` 网络通信。
5. `Gate` 与 `Gate`、`Game` 与 `Game` 当前不直接形成业务通道。

**统一启动入口**
1. native 统一使用 `xserver-node <configPath> <nodeId>` 启动。
2. `configPath` 指向单个 UTF-8 JSON 配置文件。
3. `nodeId` 当前支持 `GM`、`Gate<index>`、`Game<index>`。

**开发期管理脚本**
1. `M3-17` 当前提供 Python 主入口 `tools/cluster_ctl.py`，用于执行 `start` / `kill` 两类本地开发管理命令。
2. Windows 下提供薄包装脚本 `tools/start_local_cluster.bat` 与 `tools/kill_local_cluster.bat`，默认面向 `configs/local-dev.json`。
3. `start` 命令按 `GM -> Game[*] -> Gate[*]` 顺序拉起节点，并通过 `GM` 的本地 HTTP `GET /healthz` / `GET /status` 做最小启动探活。
4. `kill` 命令当前只覆盖 Windows，按“配置路径 + `NodeID` + `xserver-node.exe`”精确匹配并强制终止本机进程，不接入优雅退出流程。

**配置模型**
1. 顶层配置块固定为 `env`、`logging`、`kcp`、`managed`、`gm`、`gate`、`game`。
2. `logging`、`kcp` 与 `managed` 是集群级公共配置。
3. `gm.innerNetwork.listenEndpoint` 表示 `GM` 的内部监听地址。
4. `gm.controlNetwork.listenEndpoint` 表示 `GM` 的本地 HTTP 管理接口监听地址。
5. `gate.<NodeID>` 同时包含 `innerNetwork.listenEndpoint` 与 `clientNetwork.listenEndpoint`。
6. `managed` 包含 `assemblyName`、`assemblyPath`、`runtimeConfigPath` 与 `searchAssemblyPaths`，由 `GM` 与全部 `Game` 共用；`game.<NodeID>` 只包含实例级 `innerNetwork.listenEndpoint`。
7. 当前仓库样例配置位于 `configs/local-dev.json`。

**协议与运行时**
1. 节点间消息统一使用二进制包头与消息体编码，包头定义见 `docs/PACKET_HEADER.md`。
2. `msgId` 规范见 `docs/MSG_ID.md`，错误码规范见 `docs/ERROR_CODE.md`。
3. 启动期与集群期 `Inner` 消息规范见 `docs/PROCESS_INNER.md`。
4. `Gate` 的客户端路由语义见 `docs/SESSION_ROUTING.md` 与 `docs/GATE_GAME_ENVELOPE.md`。

**托管互操作**
1. 当前阶段 `Game` 进程承载 CLR 业务入口；`GM` 会在启动编排阶段短暂加载 `Framework` 程序集，并结合 `managed.searchAssemblyPaths` 读取 ServerStub catalog，但不会运行 `GameNativeInit` / `GameNativeOnMessage` / `GameNativeOnTick` 业务循环。
2. 默认托管程序集名为 `XServer.Managed.Framework`。
3. `src/managed/Foundation` 承载通用契约与互操作共享类型，`src/managed/Framework` 承载 `ServerEntity` / `ServerStubEntity` 等分布式实体框架公共抽象，`src/managed/GameLogic` 承载业务逻辑并依赖 `Framework`。
4. ABI 与导出函数约定见 `docs/MANAGED_INTEROP.md`。

**关联文档**
1. 配置与日志：`docs/CONFIG_LOGGING.md`
2. KCP：`docs/KCP_CONFIG.md`
3. 启动顺序：`docs/STARTUP_FLOW.md`
4. 分布式实体：`docs/DISTRIBUTED_ENTITY.md`
