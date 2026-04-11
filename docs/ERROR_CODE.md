# ERROR_CODE

本文档定义 XServerByAI 当前主线代码的错误码规则与当前已登记错误码。内容以当前仓库实现为准。

## 基础规则

1. `errorCode` 使用 `int32` 语义表示。
2. `0` 固定表示成功，非 `0` 表示失败。
3. 对外错误码统一使用非负整数；不要跨 ABI 传递负错误码作为稳定协议语义。
4. 同一个错误码一旦登记，不得复用到新语义。
5. `msgId` 负责表达“哪类消息”，`errorCode` 负责表达“为什么失败”，两者不能互相替代。

## 号段约定

| Range | Category | Description |
| --- | --- | --- |
| `1-999` | 通用公共 | 参数、状态、超时等公共错误 |
| `1000-1999` | 协议 / 序列化 | 包头、版本、flags、编解码错误 |
| `2000-2999` | 网络 / 会话 | KCP / TCP / 会话链路问题 |
| `3000-3999` | Inner / 路由 | 注册、心跳、路由与内部转发 |
| `4000-4999` | 宿主 / 互操作 | `nethost`、CLR、ABI 与导出绑定 |
| `5000-5999` | 基础设施 | 配置、日志、持久化、资源 |
| `10000+` | 业务域 | Avatar / Space / Stub 等上层业务错误 |

## 当前已登记 Inner 错误码

| errorCode | CanonicalName | Status | Owner | Description |
| --- | --- | --- | --- | --- |
| `3000` | `Inner.ProcessTypeInvalid` | `Active` | `GM` | 注册请求中的 `processType` 非法 |
| `3001` | `Inner.NodeIdConflict` | `Active` | `GM` | `nodeId` 已被活动连接占用 |
| `3002` | `Inner.InnerNetworkEndpointInvalid` | `Active` | `GM` | 注册请求里的 `innerNetworkEndpoint` 非法 |
| `3003` | `Inner.NodeNotRegistered` | `Active` | `GM` | 当前链路尚未完成注册 |
| `3004` | `Inner.ChannelInvalid` | `Active` | `GM` | 该连接或路由通道已无效 |
| `3005` | `Inner.RequestInvalid` | `Active` | `GM` | inner 请求包头或消息体不合法 |

## 当前已登记互操作错误码

| errorCode | CanonicalName | Status | Owner | Description |
| --- | --- | --- | --- | --- |
| `4000` | `Interop.RuntimeAlreadyLoaded` | `Active` | `host` | CLR 宿主已加载，禁止重复初始化 |
| `4001` | `Interop.RuntimeConfigPathEmpty` | `Active` | `host` | 未提供 `runtimeconfig.json` 路径 |
| `4002` | `Interop.RuntimeConfigNotFound` | `Active` | `host` | `runtimeconfig.json` 不存在 |
| `4003` | `Interop.AssemblyPathEmpty` | `Active` | `host` | 未提供程序集路径 |
| `4004` | `Interop.AssemblyNotFound` | `Active` | `host` | 根程序集文件不存在 |
| `4005` | `Interop.HostfxrPathResolveFailed` | `Active` | `host` | `nethost` 无法解析 `hostfxr` 路径 |
| `4006` | `Interop.HostfxrLibraryLoadFailed` | `Active` | `host` | `hostfxr` 动态库加载失败 |
| `4007` | `Interop.HostfxrExportLoadFailed` | `Active` | `host` | `hostfxr` 必需导出缺失 |
| `4008` | `Interop.RuntimeInitializeFailed` | `Active` | `host` | 宿主上下文初始化失败 |
| `4009` | `Interop.RuntimeDelegateLoadFailed` | `Active` | `host` | `load_assembly_and_get_function_pointer` 获取失败 |
| `4010` | `Interop.RuntimeContextCloseFailed` | `Active` | `host` | 宿主上下文关闭失败 |
| `4011` | `Interop.RuntimeNotLoaded` | `Active` | `host` | CLR 宿主尚未加载 |
| `4012` | `Interop.EntryPointResolveFailed` | `Active` | `host` | 导出入口解析失败 |
| `4013` | `Interop.AbiVersionMismatch` | `Active` | `host` | managed ABI 版本与 native 不匹配 |
| `4014` | `Interop.EntryPointNotBound` | `Active` | `host` | 导出入口尚未完成绑定 |

## 当前说明

1. `3100-3199` 仍然保留给 relay / 路由扩展错误使用。
2. 当前主线 `Relay.ForwardMailboxCall`、`Relay.ForwardProxyCall` 与 `Relay.PushToClient` 的异常主要通过本地日志处理，尚未登记对外稳定的 relay `errorCode`。
3. 如果未来把 relay 失败升级为对外协议语义，应在 `3100-3199` 号段重新登记，而不是复用旧文档中的过期描述。

## 命名约定

1. 规范英文名使用 `PascalCase` 片段并以 `.` 分隔。
2. 推荐格式：
   - `<Area>.<Reason>`
   - `<Area>.<Subject>.<Reason>`
3. 当前常用前缀包括：
   - `Inner`
   - `Interop`
   - `Protocol`
   - `Common`
