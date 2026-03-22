# KCP_CONFIG

本文档定义 XServerByAI 当前阶段 `Gate` 侧 `Client` 网络所使用的 KCP 配置项、默认值与调优边界。后续 `M4-01` 与 `M4-02` 在封装客户端会话与监听器时，应以本文件为默认基线。

**适用范围**
1. 当前只覆盖 `Gate <-> Client` 链路上的 KCP 参数。
2. 节点与节点之间的网络统一属于 `Inner`，不使用本文件中的参数。
3. ctrl 工具与 `GM` 之间的网络统一属于 `Control`，不使用本文件中的参数。

**配置模型**
1. `kcp` 为集群级公共配置块，位于配置文件顶层。
2. `gate.<NodeID>.clientNetwork.listenEndpoint` 表示某个 Gate 的客户端监听地址。
3. `kcp` 负责算法参数，`clientNetwork.listenEndpoint` 负责监听地址，两者职责分离。

**示例**

```json
{
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
  },
  "gate": {
    "Gate0": {
      "clientNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4000
        }
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
2. `sndwnd = 128`、`rcvwnd = 128`：兼顾状态广播与输入突发。
3. `nodelay = true`、`intervalMs = 10`、`fastResend = 2`：形成当前默认低延迟档位。
4. `noCongestionWindow = false`：公网默认保留拥塞控制。
5. `streamMode = false`：保持消息边界清晰，便于与上层协议配合。

**约束**
1. `mtu` 不得高于实际 UDP 安全负载；如无额外约束，优先保持默认值。
2. `sndwnd` 与 `rcvwnd` 应尽量同步调整，避免单侧放大导致另一侧阻塞。
3. `noCongestionWindow = true` 只建议在受控内网或已验证带宽环境中使用。
4. `streamMode = true` 会改变消息边界语义；启用前必须同步评估客户端协议 framing。
5. KCP 参数不替代应用层心跳、鉴权超时或会话清理规则。

**实现要求**
1. `Gate` 的 `ClientNetwork` 实现必须同时读取 `gate.<NodeID>.clientNetwork.listenEndpoint` 与顶层 `kcp`。
2. 若底层包装暂时不支持某个字段，必须在实现说明中明确缺口，不能静默忽略配置。
3. 默认值变更属于运行行为变更；修改前应同步更新本文与 `docs/CONFIG_LOGGING.md`。
