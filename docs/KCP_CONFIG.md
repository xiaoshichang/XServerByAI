# KCP_CONFIG

本文档定义 XServerByAI 当前阶段 Gate 侧客户端 KCP 会话使用的配置项、默认值与调优边界。后续 `M4-01` 与 `M4-02` 在封装 KCP 会话对象和监听器时，应以本文件作为默认配置基线。

**适用范围**
1. 当前只覆盖 Gate ↔ Client 链路上的 KCP 参数，不覆盖 GM/Gate/Game 的 ZeroMQ over TCP 内部链路。
2. 当前只定义与 KCP 算法和分段行为直接相关的参数，不把应用层心跳、鉴权超时、断线踢出等策略混入本规范。
3. 配置载体、统一启动入口与公共键名约定复用 `docs/CONFIG_LOGGING.md`；本文件只固定 `gate.<selector>.kcp` 子块的字段名与默认值。

**逻辑配置块示例**

```json
{
  "gate": {
    "gate0": {
      "nodeId": "Gate0",
      "kcp": {
        "mtu": 1200,
        "sndwnd": 128,
        "rcvwnd": 128,
        "nodelay": true,
        "intervalMs": 10,
        "fastResend": 2,
        "noCongestionWindow": false,
        "minRtoMs": 30,
        "deadLinkCount": 20,
        "streamMode": false
      }
    }
  }
}
```

**配置项定义**

| Item | Type | Default | Recommended Range | KCP Mapping | Description |
| --- | --- | --- | --- | --- | --- |
| `mtu` | `uint32` | `1200` | `576-1400` | `ikcp_setmtu` | 单个 KCP 分段使用的最大传输单元 |
| `sndwnd` | `uint32` | `128` | `32-512` | `ikcp_wndsize(sndwnd, ...)` | 发送窗口大小 |
| `rcvwnd` | `uint32` | `128` | `32-512` | `ikcp_wndsize(..., rcvwnd)` | 接收窗口大小 |
| `nodelay` | `bool` | `true` | `true/false` | `ikcp_nodelay(nodelay, ...)` | 是否启用低延迟模式 |
| `intervalMs` | `uint32` | `10` | `10-40` | `ikcp_nodelay(..., interval, ...)` | KCP 内部 update/flush 轮询间隔 |
| `fastResend` | `uint32` | `2` | `0-5` | `ikcp_nodelay(..., resend, ...)` | 快速重传触发阈值 |
| `noCongestionWindow` | `bool` | `false` | `true/false` | `ikcp_nodelay(..., nc)` | 是否关闭拥塞窗口控制 |
| `minRtoMs` | `uint32` | `30` | `10-100` | `ikcp->rx_minrto` | 最小重传超时 |
| `deadLinkCount` | `uint32` | `20` | `10-60` | `ikcp->dead_link` | 视为不可恢复链路的阈值 |
| `streamMode` | `bool` | `false` | `true/false` | `ikcp->stream` | 是否启用流模式 |

**默认值说明**
1. `mtu = 1200`：优先减少公网 IP 分片风险。
2. `sndwnd = 128`、`rcvwnd = 128`：兼顾场景状态广播与输入突发。
3. `nodelay = true`、`intervalMs = 10`、`fastResend = 2`：形成当前默认的低延迟档位。
4. `noCongestionWindow = false`：公网默认保留拥塞控制。
5. `streamMode = false`：保持消息边界清晰，便于与上层协议协同。

**约束与调优规则**
1. `mtu` 不得高于链路可承受的实际 UDP 安全负载；若部署环境未提供更强约束，优先保持默认 `1200`。
2. `sndwnd` 与 `rcvwnd` 默认应同步调整，避免仅放大发送侧而导致接收阻塞。
3. `noCongestionWindow = true` 只建议在受控内网或经过验证的固定带宽环境使用。
4. `streamMode = true` 会改变消息边界语义；若后续启用，必须同步评估客户端 framing 与上层协议兼容性。
5. KCP 参数不替代应用层心跳、鉴权超时或会话清理规则；这些策略应在会话与路由模型文档中单独定义。

**后续实现映射要求**
1. Gate 进程的 KCP 会话封装应保留上述逻辑字段，避免在代码中硬编码散落的魔法数字。
2. 若底层 KCP 包装层暂时不支持某个字段，例如 `streamMode` 或 `deadLinkCount` 的配置注入，应在实现说明中明确缺口，而不是默默忽略配置。
3. 默认值变更属于协议行为与运行特性变更；修改前应同步更新本文并记录原因。