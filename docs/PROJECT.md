# 项目说明（初版）

本文档描述当前多进程游戏服务器框架的目标、整体架构、关键技术细节与约定。当前阶段为“框架骨架设计”，不包含具体业务逻辑实现。

**目标**
1. 在单机多进程模式下，实现 GM / Gate / Game 三类进程协作的最小可运行架构。
2. 采用 C++ 作为底层运行时与系统能力实现，C# 作为业务逻辑实现。
3. 通过 `nethost` 托管 CLR，实现 C++ ↔ C# 的高性能互操作。
4. 客户端连接采用 KCP 协议，进程间通信使用自研二进制协议。

**非目标**
1. 当前阶段不引入分布式注册中心与跨机房部署。
2. 不引入第三方 RPC 框架或序列化框架（可在后续评估替换）。

**运行环境**
1. 兼容 Windows 与 Linux。
2. C++ 构建使用 CMake。
3. C# 构建使用 `dotnet`（.NET 运行时由 `nethost` 加载）。

**进程角色**
1. `GM` 进程  
负责集群控制面，提供启动/关闭流程、注册表、健康检查、配置分发、负载汇总。
2. `Gate` 进程  
负责客户端连接维护、KCP 会话管理、鉴权与路由转发，不承载业务逻辑。
3. `Game` 进程  
负责核心游戏逻辑与状态管理，按需求可多实例横向扩展。

**单机多进程拓扑**
1. GM 与 Gate/Game 通过内部 TCP 通信。
2. Gate 与 Game 通过内部 TCP 长连接通信。
3. 客户端通过 UDP + KCP 与 Gate 通信。

**启动与注册流程（建议）**
1. GM 启动，监听控制端口，加载配置。
2. Game 启动后向 GM 注册，持续心跳上报，携带能力与负载。
3. Gate 启动后向 GM 注册，拉取可用 Game 列表或订阅变更。
4. 客户端连接 Gate，完成鉴权后建立会话，Gate 将会话绑定到目标 Game。
5. 注册与心跳消息结构、字段含义与默认时序约定见 `docs/PROCESS_CONTROL.md`。

**进程间二进制协议**
1. 统一包头，网络字节序（大端）传输，消息体为自研二进制结构。
2. 包头建议格式（固定长度）：
```
struct PacketHeader {
  uint32_t magic;   // 0x47535052 "GSPR"
  uint16_t ver;     // 协议版本，初始为 1
  uint16_t flags;   // bit0: request/response, bit1: compressed, bit2: error
  uint32_t length;  // body length
  uint32_t msgId;   // 消息编号
  uint32_t seq;     // 请求序号
}
```
3. `msgId` 采用集中分段管理；控制面、Gate↔Game 中转、客户端接入与业务实体消息使用独立号段，响应默认复用原请求 `msgId` 并通过 `flags` 标识。
4. 采用“长度前缀 + 包头 + 包体”的 framing，避免粘包/拆包问题。
5. 包头与常量细则见 `docs/PACKET_HEADER.md`，`msgId` 分段与命名细则见 `docs/MSG_ID.md`，错误码范围与编码规则见 `docs/ERROR_CODE.md`；后续扩展字段如 `crc`、`traceId`、`routeId` 等需在兼容性约束下另行定义。

**Gate ↔ Game 通信模型**
1. Gate 维护到多个 Game 的连接池。
2. 请求类型消息需要 `seq`，响应消息带回相同 `seq`。
3. 支持双向推送（例如 Game → Gate → Client）。
4. Gate↔Game 中继封装字段、响应语义与客户端包元数据约定见 `docs/GATE_GAME_ENVELOPE.md`。

**KCP 连接模型（客户端）**
1. 客户端与 Gate 通过 UDP 进行 KCP 会话建立。
2. KCP 会话与客户端账号/角色绑定，Gate 维护会话生命周期。
3. Gate 侧 KCP 配置项、默认值与调优边界见 `docs/KCP_CONFIG.md`。
4. KCP 算法参数与应用层心跳/断线策略分离定义；KCP 负责传输层重传与拥塞相关行为，业务超时策略由后续会话条目另行约定。

**会话与路由**
1. Gate 维护 `sessionId → playerId → gameInstance` 的路由关系。
2. 路由策略可先采用“固定绑定”策略，后续可扩展为一致性哈希或分区表。
3. GM 维护 Game 实例的注册表与负载信息，供 Gate 选择路由。

**心跳与健康检查（建议默认值）**
1. Gate/Game 对 GM 心跳间隔建议 5 秒，超时建议 15 秒。
2. GM 在超时后标记实例不可用，并通知 Gate 更新路由表。
3. 所有心跳与状态变更均通过内部协议完成，避免外部依赖。

**C++ ↔ C#（nethost）互操作**
1. C++ 通过 `nethost`/`hostfxr` 加载 .NET 运行时与指定程序集。
2. C# 提供导出入口（推荐使用 `UnmanagedCallersOnly`），C++ 获取函数指针调用。
3. C++ 调用 C# 的入口函数例如 `Init`、`OnMessage`、`OnTick`，C# 仅处理业务状态。
4. 互操作数据结构尽量使用 blittable 类型，避免复杂对象跨边界。

**线程与调度模型（建议）**
1. 每个进程采用主线程事件循环，网络 IO 与定时器以异步驱动。
2. 可选一个轻量工作线程池处理耗时逻辑，保持 IO 线程低延迟。
3. Game 内部逻辑采用固定 Tick（例如 20ms 或 50ms），Tick 频率可配置。

**配置与日志（建议）**
1. 配置文件建议使用 `configs/*.json` 或 `configs/*.yaml`。
2. 日志按进程分文件输出，建议支持滚动与等级过滤。

**目录结构**
1. `src/native/` C++ 核心与三类进程入口。
2. `src/managed/` C# 业务逻辑与公共接口。
3. `configs/` 运行配置。
4. `cmake/` CMake 公共脚本与工具链配置。
5. `docs/` 文档与规范，包含协议说明。
6. 目录、命名与命名空间细则统一见 `docs/CONVENTIONS.md`。

**后续建议里程碑**
1. 先实现 GM + Game 的最小握手与心跳。
2. 再接入 Gate 与 KCP 会话管理。
3. 最后完善路由策略、负载汇总与稳定性优化。
