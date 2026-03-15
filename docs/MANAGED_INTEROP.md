# MANAGED_INTEROP

本文档定义 XServerByAI 当前阶段 native `Game` 宿主与 managed `GameLogic` 程序集之间的互操作入口签名、ABI 版本与调用约定。后续 `M5-01`、`M5-02`、`M6-01` 与 `M6-02` 在实现 `nethost` 加载、函数指针绑定、blittable 封送与 `Game` 进程调用 managed 入口时，应以本文件作为统一基线。

**适用范围**
1. 当前阶段仅 `Game` 进程宿主 CLR；`GM` 与 `Gate` 不直接调用 managed 业务程序集，除非后续条目显式扩展。
2. 当前只定义 native → managed 的入口 ABI，不定义 managed → native 回调表；后续如需日志桥接、主动回传或持久化回调，应在兼容本 ABI 的前提下扩展。
3. 当前 contract 覆盖程序集身份、导出类型名、函数签名、结构布局、字符串编码、错误返回与调用时序。
4. 业务消息编号、会话/实体路由与错误码责任域分别复用 `docs/MSG_ID.md`、`docs/SESSION_ROUTING.md`、`docs/DISTRIBUTED_ENTITY.md` 与 `docs/ERROR_CODE.md`，本文件不重复定义业务语义。

**装载模型**
1. native 侧通过 `nethost` 定位 `hostfxr`，再通过 `hostfxr` 获取 `load_assembly_and_get_function_pointer`，不得依赖平台私有的 CLR 启动捷径。
2. 根 runtime config 固定为 `XServer.Managed.GameLogic.runtimeconfig.json`；根程序集固定为 `XServer.Managed.GameLogic.dll`。
3. `XServer.Managed.Foundation` 作为 `GameLogic` 的项目引用依赖被一并加载，但不作为 native 直接解析导出入口的目标程序集。
4. 导出类型名固定为 `XServer.Managed.GameLogic.Interop.GameNativeExports, XServer.Managed.GameLogic`。
5. native 在解析任何业务入口前，必须先解析 `GameNativeGetAbiVersion` 并验证 ABI 版本号；若不匹配，应按互操作错误处理并终止托管业务初始化。

**ABI 总约定**
1. 所有导出入口统一使用 `cdecl` 调用约定；native 侧函数指针 typedef 与 C# `UnmanagedCallersOnly` 的 `CallConvCdecl` 必须严格一致。
2. 所有导出方法都必须是 `static`、非泛型、不可重载、无托管闭包依赖；导出类应保持无实例状态假设。
3. 当前 ABI 版本常量固定为 `1`，native 与 managed 两侧都应以同一数值校验兼容性。
4. 互操作结构只允许使用 blittable 字段：固定宽度整数、指针、`nint`/`nuint`、无引用的顺序布局结构；禁止在 ABI 结构中直接放 `string`、`object`、数组、`bool` 或托管引用。
5. `bool` 语义若必须跨边界传递，统一使用 `uint8` / `byte` 表示，`0` 为假，`1` 为真。
6. 所有字符串统一使用 UTF-8 的 `pointer + byteLength` 形式传递；不要求 NUL 结尾。
7. 输入指针与缓冲区只在当前调用期间借用。managed 侧如果要延后使用，必须在调用期间自行复制；不得把 native 传入的裸指针缓存到后续 Tick 或异步任务中。
8. managed 导出方法不得让异常跨越 ABI 边界。任何未预期异常都必须在 managed 侧捕获并转换为 `docs/ERROR_CODE.md` 中约定的互操作或业务错误码。
9. 当前 ABI 不定义“由 managed 分配内存并返回给 native”的所有权转移模式；后续如需引入，必须通过新 ABI 版本或新的独立 contract 扩展。

**ABI 版本**

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `XS_MANAGED_ABI_VERSION` | `1` | 首版 `Game` 宿主 ↔ managed 业务入口 ABI |

**共享结构：ManagedInitArgs**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `structSize` | `uint32` | 当前结构体字节长度；用于前向兼容校验 |
| `abiVersion` | `uint32` | 调用方声明的 ABI 版本；当前必须为 `1` |
| `processType` | `uint16` | 调用方进程类型；当前固定为 `Game = 2`，语义复用 `docs/PROCESS_CONTROL.md` |
| `reserved0` | `uint16` | 保留，发送方必须置 `0` |
| `nodeIdUtf8` | `pointer` | 当前 `Game` 节点 `NodeID` 的 UTF-8 缓冲区首地址 |
| `nodeIdLength` | `uint32` | `nodeIdUtf8` 的字节长度 |
| `configPathUtf8` | `pointer` | 当前 `Game` 配置文件路径的 UTF-8 缓冲区首地址；由统一入口 `xserver-node <configPath> <selector>` 传入 |
| `configPathLength` | `uint32` | `configPathUtf8` 的字节长度；必须大于 `0` |

`ManagedInitArgs` 的目标是给 managed 入口提供“当前是哪个 `Game` 节点、使用哪个 ABI、配置文件位置在哪里”这组最小上下文。当前不把日志回调、分配器、网络句柄或线程句柄混入初始化结构；`configPathUtf8` 指向的单文件配置布局与键名规范复用 `docs/CONFIG_LOGGING.md`，并且在当前统一入口模型下属于必填输入。

**共享结构：ManagedMessageView**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `structSize` | `uint32` | 当前结构体字节长度；用于前向兼容校验 |
| `msgId` | `uint32` | 当前消息编号，语义复用 `docs/MSG_ID.md` |
| `seq` | `uint32` | 当前消息序号；若消息类型不使用则为 `0` |
| `flags` | `uint32` | 当前消息附带标志位；具体含义由 native 侧复用内部协议语义 |
| `sessionId` | `uint64` | 关联会话标识；若当前消息不面向客户端上下文则为 `0` |
| `playerId` | `uint64` | 关联玩家标识；未知或不适用时为 `0` |
| `payload` | `pointer` | 消息体二进制缓冲区首地址；允许为 `null` |
| `payloadLength` | `uint32` | `payload` 的字节长度；无包体时为 `0` |
| `reserved0` | `uint32` | 保留，发送方必须置 `0` |

`ManagedMessageView` 只表达“这次调 managed 入口时附带的最小消息视图”。它不重复携带包头、连接对象或复杂路由对象；更高层的会话、实体和业务语义由 managed 逻辑根据 `msgId`、`sessionId`、`playerId` 与 payload 自行解释。

**导出入口签名**

native 侧应以如下等价 C 函数签名理解 managed 导出：

```cpp
constexpr uint32_t XS_MANAGED_ABI_VERSION = 1;

struct ManagedInitArgs {
  uint32_t structSize;
  uint32_t abiVersion;
  uint16_t processType;
  uint16_t reserved0;
  const uint8_t* nodeIdUtf8;
  uint32_t nodeIdLength;
  const uint8_t* configPathUtf8;
  uint32_t configPathLength;
};

struct ManagedMessageView {
  uint32_t structSize;
  uint32_t msgId;
  uint32_t seq;
  uint32_t flags;
  uint64_t sessionId;
  uint64_t playerId;
  const uint8_t* payload;
  uint32_t payloadLength;
  uint32_t reserved0;
};

using ManagedGetAbiVersionFn = uint32_t (*)();
using ManagedInitFn = int32_t (*)(const ManagedInitArgs* args);
using ManagedOnMessageFn = int32_t (*)(const ManagedMessageView* message);
using ManagedOnTickFn = int32_t (*)(uint64_t nowUnixMsUtc, uint32_t deltaMs);
```

managed 侧等价导出形式应如下：

```csharp
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace XServer.Managed.GameLogic.Interop;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ManagedInitArgs
{
    public uint StructSize;
    public uint AbiVersion;
    public ushort ProcessType;
    public ushort Reserved0;
    public byte* NodeIdUtf8;
    public uint NodeIdLength;
    public byte* ConfigPathUtf8;
    public uint ConfigPathLength;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ManagedMessageView
{
    public uint StructSize;
    public uint MsgId;
    public uint Seq;
    public uint Flags;
    public ulong SessionId;
    public ulong PlayerId;
    public byte* Payload;
    public uint PayloadLength;
    public uint Reserved0;
}

public static unsafe class GameNativeExports
{
    [UnmanagedCallersOnly(EntryPoint = "GameNativeGetAbiVersion", CallConvs = [typeof(CallConvCdecl)])]
    public static uint GameNativeGetAbiVersion() => 1;

    [UnmanagedCallersOnly(EntryPoint = "GameNativeInit", CallConvs = [typeof(CallConvCdecl)])]
    public static int GameNativeInit(ManagedInitArgs* args) => 0;

    [UnmanagedCallersOnly(EntryPoint = "GameNativeOnMessage", CallConvs = [typeof(CallConvCdecl)])]
    public static int GameNativeOnMessage(ManagedMessageView* message) => 0;

    [UnmanagedCallersOnly(EntryPoint = "GameNativeOnTick", CallConvs = [typeof(CallConvCdecl)])]
    public static int GameNativeOnTick(ulong nowUnixMsUtc, uint deltaMs) => 0;
}
```

**入口语义**
1. `GameNativeGetAbiVersion`  
   native 在解析其他导出前首先调用它；返回值必须等于 `XS_MANAGED_ABI_VERSION`。
2. `GameNativeInit`  
   在 CLR 启动且 ABI 校验通过后调用一次。成功返回 `0`，失败返回已登记的非零错误码。未成功初始化前，native 不得调用 `GameNativeOnMessage` 或 `GameNativeOnTick`。
3. `GameNativeOnMessage`  
   用于把 native 已解码的业务消息视图送入 managed。`payload` 指向消息体，不包含 transport 包头。返回 `0` 表示 managed 已接受并处理，非零表示处理失败。
4. `GameNativeOnTick`  
   用于驱动 managed 逻辑 Tick。`nowUnixMsUtc` 为当前绝对时间，`deltaMs` 为上次 Tick 到本次 Tick 的增量毫秒。返回 `0` 表示成功。
5. 当前 ABI `v1` 不定义独立的 `Shutdown` 导出。进程退出或 CLR 卸载前的清理仍由 native 宿主生命周期与 managed 运行时默认行为处理；若后续需要显式清理入口，应通过新的 ABI 版本补充。

**调用时序与线程约束**
1. 推荐时序固定为：`load runtime -> resolve GameNativeGetAbiVersion -> verify abi -> call GameNativeInit -> repeated GameNativeOnMessage / GameNativeOnTick`。
2. 当前默认由 native 宿主保证对同一个 managed `Game` 宿主实例的调用非重入、串行执行。后续若引入并发调用模型，必须先扩展 contract。
3. `GameNativeOnTick` 与 `GameNativeOnMessage` 不应并发进入同一 managed 宿主实例；否则容易破坏业务状态一致性。
4. native 侧对 `ManagedInitArgs` 与 `ManagedMessageView` 的 `structSize`、空指针与长度组合负责做最小校验；managed 侧仍应防御非法输入并返回稳定错误码。

**错误处理与兼容策略**
1. 所有导出入口除 `GameNativeGetAbiVersion` 外都以 `int32` 返回状态：`0` 表示成功，非零表示失败。
2. ABI 不匹配、入口缺失、`hostfxr` / `nethost` 初始化失败等问题应落入 `docs/ERROR_CODE.md` 的 `4000-4099` 托管运行时号段。
3. managed 业务拒绝、业务校验失败或路由相关失败应继续落入其对应的业务号段或路由号段，不得一律折叠成互操作错误。
4. 若后续需要变更导出函数名、调用约定、结构字段含义或返回值语义，应通过新的 ABI 版本推进，而不是静默覆盖 `v1`。

**对后续条目的约束**
1. `M5-01` 的 `nethost` 初始化应以本文件定义的装载顺序和运行时配置文件为准。
2. `M5-02` 的函数指针绑定应只绑定本文件列出的首版导出入口，不应临时拼装额外私有入口名。
3. `M6-01` 的 blittable 封送实现应直接复用 `ManagedInitArgs` 与 `ManagedMessageView` 的字段顺序和基础类型语义。
4. `M6-02` 的 `Game` 进程托管调用只应调用本文件定义的 `GameNativeInit`、`GameNativeOnMessage`、`GameNativeOnTick`，并在此之前完成 ABI 校验。
