# 项目说明

本文档描述 XServerByAI 当前主线代码的目标、节点拓扑、网络分层与已经落地的关键能力。本文档不再使用“初版/骨架阶段”的表述，内容以当前仓库实现为准。

## 当前目标

1. 以单个 `xserver-node` 可执行程序承载 `GM`、`Gate`、`Game` 三类节点。
2. 以 C++ 负责配置、日志、事件循环、ZeroMQ、KCP、宿主边界与节点编排。
3. 以 C# 负责 `Game` 侧实体框架、stub catalog、avatar 创建、mailbox/proxy 路由与客户端推送桥接。
4. 以单个 UTF-8 JSON 文件描述一个服务器组的全部实例配置。

## 当前已落地能力

1. `GM`
   - 负责 `Gate/Game -> GM` 注册、心跳、节点在线聚合、mesh ready 聚合、stub ownership 下发、ready 聚合与 `clusterReady` 下发。
   - 暴露本地控制 HTTP：`GET /healthz`、`GET /status`、`GET /boardcase`、`POST /shutdown`。
   - 会短暂加载 managed `Framework` 程序集，读取 `ServerStub` catalog，并构建 bootstrap ownership 表。
2. `Gate`
   - 向 `GM` 注册并维护心跳。
   - 监听来自全部 `Game` 的 `Inner` 注册与心跳，维护本地 Game 目录。
   - 暴露 `authNetwork` HTTP：`GET /healthz`、`POST /login`。
   - 暴露 `clientNetwork` KCP 入口，并且只在收到 `clusterReady = true` 后开放客户端接入。
   - 当前支持 `clientHello (45010)` 与 `selectAvatar (45013)` 两条客户端主线消息。
   - 当前支持 `Game -> Gate` 三类中继：`Relay.PushToClient (2001)`、`Relay.ForwardMailboxCall (2002)`、`Relay.ForwardProxyCall (2005)`。
3. `Game`
   - 向 `GM` 注册并维护心跳。
   - 在收到 `allNodesOnline = true` 后，对全部 `Gate` 建立注册/心跳闭环，并上报 mesh ready。
   - 宿主 CLR，绑定 `GameNativeInit`、`GameNativeOnMessage`、ownership/catalog 相关导出。
   - 基于 ownership 创建本地 `ServerStubEntity`，聚合 ready 后回报 `GM`。
   - 当前已接通 `Gate -> Game` 的 avatar 创建请求 `2003`，以及 `Game -> Gate` 的创建结果回传 `2004`。

## 拓扑与网络分层

1. 当前默认拓扑是 `1 GM + N Gate + M Game`，其中 `N >= 1`、`M >= 1`。
2. `Inner`
   - 承载 `GM <-> Gate/Game` 的注册、心跳与启动编排。
   - 承载 `Game -> Gate` 的注册、心跳与后续中继流量。
   - 当前实现基于 ZeroMQ over TCP。
3. `Control`
   - 只用于管理 `GM`。
   - 当前实现是 `GM` 本地 HTTP 控制面。
4. `Auth`
   - 只用于 `Gate` 的 HTTP 登录授予。
   - `/login` 返回 KCP 会话预约信息，包括 host、port、conversation 与有效期。
5. `Client`
   - 只用于客户端与 `Gate` 的 KCP 数据面。
   - 当前由顶层 `kcp` 配置块控制算法参数。

## 当前主线业务闭环

1. `GM` 启动并监听 `Inner` 与 `Control`。
2. `Game`、`Gate` 各自向 `GM` 注册并进入心跳。
3. `GM` 确认期望节点全部在线后，通知全部 `Game` 开始 `Game -> Gate` 全连接闭环。
4. `Game` 完成到全部 `Gate` 的注册/心跳后，上报 mesh ready。
5. `GM` 下发 stub ownership。
6. `Game` 初始化本地 stub，并在全部 ready 后上报 `GM`。
7. `GM` 向全部 `Gate` 下发 `clusterReady = true`。
8. 客户端先调用 `Gate /login` 获取 KCP 预约，再通过 `clientHello` 建立受控会话。
9. 客户端发送 `selectAvatar` 后，`Gate` 选择一个就绪 `Game`，发送 `2003` 创建 avatar 请求；`Game` 完成处理后回送 `2004`，`Gate` 绑定路由并给客户端确认。
10. 运行期 managed 逻辑可继续通过 `Relay.ForwardMailboxCall`、`Relay.ForwardProxyCall` 与 `Relay.PushToClient` 驱动跨节点和下行消息。

## managed 与客户端项目

当前仓库中已落地的 managed / client 项目包括：

1. `src/managed/Foundation`
2. `src/managed/Framework`
3. `src/managed/GameLogic`
4. `src/managed/EntityProperties.Generator`
5. `src/managed/Tests/*`
6. `client/XServer.Client.Framework`
7. `client/XServer.Client.GameLogic`
8. `client/XServer.Client.App`
9. `client/Tests/XServer.Client.Tests`

其中：

1. `Framework` 与 `GameLogic` 当前目标框架是 `net10.0`。
2. `EntityProperties.Generator` 当前目标框架是 `netstandard2.0`。
3. 客户端项目不并入 `XServerByAI.Managed.sln`，而是按项目单独构建与测试。

## 当前不做的事情

1. 不引入独立分布式注册中心、跨地域多活或跨机房复制。
2. 不引入第三方 RPC 框架；当前节点间协议仍由项目自行编码。
3. 不把 `Gate <-> Game` 运行期消息抽象成完整通用 RPC 系统；当前主线仍以 avatar bootstrap 和 mailbox/proxy/push 三类运行期流量为主。

## 关联文档

1. 配置与日志见 [CONFIG_LOGGING.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/CONFIG_LOGGING.md)。
2. 启动顺序见 [STARTUP_FLOW.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/STARTUP_FLOW.md)。
3. `Inner` 协议见 [PROCESS_INNER.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/PROCESS_INNER.md)。
4. managed ABI 见 [MANAGED_INTEROP.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/MANAGED_INTEROP.md)。
5. Gate/Game 中继见 [GATE_GAME_ENVELOPE.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/GATE_GAME_ENVELOPE.md)。
