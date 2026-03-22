# PROJECT

本文档描述 XServerByAI 当前阶段的目标、节点拓扑、网络分层与关键技术边界。当前阶段以“统一节点入口 + 配置基线 + 协议骨架 + 运行时骨架”为主，不展开完整业务玩法实现。

**目标**
1. 以单个 `xserver-node` 可执行程序承载 `GM`、`Gate`、`Game` 三类节点。
2. 以 C++ 负责底层运行时、网络与宿主边界，以 C# 负责 `Game` 侧业务逻辑。
3. 以单个 UTF-8 JSON 配置文件描述一个服务器组的集群配置与节点实例配置。
4. 为 `Inner` 集群通信、`Client` 接入以及 `Control` 管理能力提供统一命名和稳定骨架。

**非目标**
1. 当前阶段不实现分布式注册中心、自动部署或热更新协议。
2. 当前阶段不提供完整的 RPC 框架；内部消息以项目自定义协议为主。
3. 当前阶段只提供 `GM` 本地 HTTP 管理接口，不提供完整的外部 ctrl 工具链、鉴权体系或更复杂的运维平台能力。

**平台与依赖**
1. 目标平台为 Windows 与 Linux。
2. C++ 构建使用 CMake。
3. C# 构建使用 `dotnet`，托管宿主通过 `nethost` / `hostfxr` 接入。
4. 当前主要 vendored 依赖位于 `3rd/`，包括 `spdlog`、`zeromq/libzmq`、standalone `asio` 与 header-only `nlohmann/json`。

**节点角色**
1. `GM`：集群协调节点，负责注册、心跳、ready 聚合、ownership 分配与路由目录维护。
2. `Gate`：客户端接入节点，负责 `Client` 网络、会话管理与 Gate/Game 中继。
3. `Game`：业务承载节点，负责托管逻辑、实体状态与本地 ready 上报。

**网络分层命名**
1. `Inner`：节点与节点之间的网络，当前主要承载 `GM <-> Gate/Game` 的注册、心跳和路由消息。
2. `Control`：ctrl 工具与 `GM` 之间的管理网络。当前阶段已落地 `GM` 本地 HTTP 管理接口，并统一沿用 `ControlNetwork`、`controlNetwork.listenEndpoint` 命名。
3. `Client`：`Gate` 与客户端之间的网络，对应 `ClientNetwork`、`clientNetwork.listenEndpoint` 与集群级 `kcp`。

**NodeID 约定**
1. Gate 与 Game 的稳定逻辑身份统一称为 `NodeID`。
2. `NodeID` 使用 `<ProcessType><index>` 形式，例如 `Gate0`、`Gate1`、`Game0`、`Game1`。
3. 启动参数中的 `nodeId` 只用于实例选择；当前与 `NodeID` 取值保持一致。

**拓扑**
1. 当前默认拓扑为 `1 GM + N Gate + M Game`，其中 `N >= 1`、`M >= 1`。
2. `GM` 与每个 `Gate` / `Game` 通过 `Inner` 网络直接通信。
3. `Gate` 与 `Game` 之间通过 `Inner` 网络形成全互连中继平面。
4. 客户端只与 `Gate` 的 `Client` 网络通信。
5. `Gate` 与 `Gate`、`Game` 与 `Game` 当前不直接形成业务通道。

**统一启动入口**
1. native 统一使用 `xserver-node <configPath> <nodeId>` 启动。
2. `configPath` 指向单个 UTF-8 JSON 配置文件。
3. `nodeId` 当前支持 `GM`、`Gate<index>`、`Game<index>`。

**配置模型**
1. 顶层配置块固定为 `env`、`logging`、`kcp`、`gm`、`gate`、`game`。
2. `logging` 与 `kcp` 是集群级公共配置。
3. `gm.innerNetwork.listenEndpoint` 表示 `GM` 的内部监听地址。
4. `gm.controlNetwork.listenEndpoint` 表示 `GM` 的本地 HTTP 管理接口监听地址。
5. `gate.<NodeID>` 同时包含 `innerNetwork.listenEndpoint` 与 `clientNetwork.listenEndpoint`。
6. `game.<NodeID>` 包含 `innerNetwork.listenEndpoint` 与可选 `managed.assemblyName`。
6. 当前仓库样例配置位于 `configs/local-dev.json`。

**协议与运行时**
1. 节点间消息统一使用二进制包头与消息体编码，包头定义见 `docs/PACKET_HEADER.md`。
2. `msgId` 规范见 `docs/MSG_ID.md`，错误码规范见 `docs/ERROR_CODE.md`。
3. 启动期与集群期 `Inner` 消息规范见 `docs/PROCESS_INNER.md`。
4. `Gate` 的客户端路由语义见 `docs/SESSION_ROUTING.md` 与 `docs/GATE_GAME_ENVELOPE.md`。

**托管互操作**
1. 当前阶段只有 `Game` 进程承载 CLR。
2. 默认托管程序集名为 `XServer.Managed.GameLogic`。
3. ABI 与导出函数约定见 `docs/MANAGED_INTEROP.md`。

**关联文档**
1. 配置与日志：`docs/CONFIG_LOGGING.md`
2. KCP：`docs/KCP_CONFIG.md`
3. 启动顺序：`docs/STARTUP_FLOW.md`
4. 分布式实体：`docs/DISTRIBUTED_ENTITY.md`
