# KCP_CONFIG

本文档定义 XServerByAI 当前阶段 Gate 侧客户端 KCP 会话使用的配置项、默认值与调优边界。后续 `M4-01` 与 `M4-02` 在封装 KCP 会话对象和监听器时，应以本文件作为默认配置基线。

**适用范围**
1. 当前只覆盖 Gate ↔ Client 链路上的 KCP 参数，不覆盖 GM/Gate/Game 的内部 TCP 链路。
2. 当前只定义与 KCP 算法和分段行为直接相关的参数，不把应用层心跳、鉴权超时、断线踢出等策略混入本规范。
3. 未来配置文件格式由后续配置规范条目统一约定；本文件先固定逻辑配置项名称与默认值。

**默认档位目标**
1. 面向基于 `space` 的实时交互场景，优先控制输入回显、状态同步与短消息广播的时延；在房间类型游戏中可将 `space` 理解为房间。
2. 默认档位采用“低延迟、可承受公网抖动、保留基本拥塞控制”的平衡策略。
3. 若未来针对回放、大包同步或弱网极端场景需要差异化档位，应在保持本默认档位兼容的前提下另行扩展。

**逻辑配置块示例**

```yaml
kcp:
  mtu: 1200
  sndwnd: 128
  rcvwnd: 128
  nodelay: true
  intervalMs: 10
  fastResend: 2
  noCongestionWindow: false
  minRtoMs: 30
  deadLinkCount: 20
  streamMode: false
```

上例只是逻辑结构示意，不约束最终配置文件必须使用 YAML；后续 JSON/YAML 载体由配置规范统一决定。

**配置项定义**

| Item | Type | Default | Recommended Range | KCP Mapping | Description |
| --- | --- | --- | --- | --- | --- |
| `mtu` | `uint32` | `1200` | `576-1400` | `ikcp_setmtu` | 单个 KCP 分段使用的最大传输单元；默认取 `1200` 以降低公网 IP 分片风险 |
| `sndwnd` | `uint32` | `128` | `32-512` | `ikcp_wndsize(sndwnd, ...)` | 发送窗口大小；默认允许场景状态短时突发但不过度放大内存占用 |
| `rcvwnd` | `uint32` | `128` | `32-512` | `ikcp_wndsize(..., rcvwnd)` | 接收窗口大小；默认与发送窗口对称，避免基础广播场景下过早阻塞 |
| `nodelay` | `bool` | `true` | `true/false` | `ikcp_nodelay(nodelay, ...)` | 是否启用 KCP 低延迟模式；默认开启以缩短 flush 和确认等待 |
| `intervalMs` | `uint32` | `10` | `10-40` | `ikcp_nodelay(..., interval, ...)` | KCP 内部 update/flush 轮询间隔；默认 `10ms`，兼顾时延与 CPU 开销 |
| `fastResend` | `uint32` | `2` | `0-5` | `ikcp_nodelay(..., resend, ...)` | 快速重传触发阈值；`0` 表示关闭，默认 `2` 用于弱网下尽快补发关键小包 |
| `noCongestionWindow` | `bool` | `false` | `true/false` | `ikcp_nodelay(..., nc)` | 是否关闭拥塞窗口控制；默认保留拥塞控制，避免公网波动时放大突发 |
| `minRtoMs` | `uint32` | `30` | `10-100` | `ikcp->rx_minrto` | 最小重传超时；默认 `30ms` 与低延迟档位配套，避免过于激进的重传抖动 |
| `deadLinkCount` | `uint32` | `20` | `10-60` | `ikcp->dead_link` | 连续视为不可恢复链路的阈值；达到后可由上层将会话视为失效 |
| `streamMode` | `bool` | `false` | `true/false` | `ikcp->stream` | 是否启用流模式；默认关闭，保持消息边界清晰，便于与项目自定义包结构配合 |

**默认值说明**
1. `mtu = 1200`：优先减少公网环境中的 UDP 分片风险，而不是追求单包极限吞吐。
2. `sndwnd = 128`、`rcvwnd = 128`：对场景状态广播和短时输入突发留出一定缓冲，同时避免一开始就把窗口开得过大。
3. `nodelay = true`、`intervalMs = 10`、`fastResend = 2`：形成当前默认的低延迟档位，适合强调输入响应和短消息传播的场景型玩法；在房间类型游戏中可将 `space` 理解为房间。
4. `noCongestionWindow = false`：默认仍保留拥塞窗口，避免在公网抖动或丢包场景下把问题放大成更大的发送突发。
5. `minRtoMs = 30`：与默认低延迟档位匹配，兼顾补发速度和误判概率。
6. `deadLinkCount = 20`：作为链路已明显异常的底线阈值；是否立即断开连接仍由上层会话逻辑决定。
7. `streamMode = false`：项目当前协议和后续 Gate 会话转发都以“消息”为基本单位，不建议在默认档位下改为流模式。

**约束与调优规则**
1. `mtu` 不得高于链路可承受的实际 UDP 安全负载；若部署环境未提供更强约束，优先保持默认 `1200`。
2. `sndwnd` 与 `rcvwnd` 不要求绝对相等，但默认应同步调整；若只放大发送窗口而不放大接收窗口，容易在广播场景下放大阻塞。
3. 当 `nodelay = false` 时，应重新评估 `intervalMs` 与 `minRtoMs`；不能机械沿用低延迟档位的其他参数。
4. `fastResend > 0` 只适用于允许更积极补发的实时场景；若链路质量稳定但 CPU 预算紧张，可考虑回退到 `0`。
5. `noCongestionWindow = true` 只建议在受控内网或经验证的固定带宽环境使用，不应作为公网默认值。
6. `streamMode = true` 会改变消息边界语义；若后续需要启用，必须同步评估客户端 framing 与上层协议兼容性。
7. KCP 参数不替代应用层心跳、鉴权超时或会话清理规则；这些策略应在会话与路由模型文档中单独定义。

**后续实现映射要求**
1. Gate 进程的 KCP 会话封装应保留上述逻辑字段，避免在代码中硬编码散落的魔法数字。
2. 若底层 KCP 包装层暂时不支持某个字段，例如 `streamMode` 或 `deadLinkCount` 的配置注入，应在实现说明中明确缺口，而不是默默忽略配置。
3. 默认值变更属于协议行为和运行特性变更；修改前应同步更新本文件并记录变更原因。
