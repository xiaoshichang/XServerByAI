# CONFIG_LOGGING

本文档定义 XServerByAI 当前阶段的配置组织方式、网络命名规范与日志输出基线。后续 `M1-16`、`M2-01`、`M2-02`、`M3-06`、`M4-02`、`M5-17`、`M6-06` 与 `M6-09` 应以本文件为准。

**网络命名规范**
1. `Inner` 表示节点与节点之间的内部网络，例如 `InnerNetwork`、`innerNetwork.listenEndpoint`、`innerNetworkEndpoint`。
2. `Control` 表示 ctrl 工具与 `GM` 之间的管理网络。当前阶段尚未落地独立 `ControlNetwork` 配置块，但后续新增字段、函数和文档都必须沿用该命名。
3. `Client` 表示 `Gate` 与客户端之间的网络，例如 `ClientNetwork`、`clientNetwork.listenEndpoint`。
4. 不再使用历史遗留术语去表达节点间链路或实例选择。

**统一启动入口**
1. native 统一使用 `xserver-node <configPath> <nodeId>` 启动。
2. `configPath` 指向单个 UTF-8 JSON 配置文件，例如 `configs/local-dev.json`。
3. `nodeId` 当前只接受 `GM`、`Gate<index>`、`Game<index>` 形式，例如 `GM`、`Gate0`、`Game1`。
4. `nodeId` 只负责在单文件中选择实例；稳定逻辑身份统一称为 `NodeID`，当前与启动参数取值一致。

**顶层配置块**

| Block | Required | Applies To | Description |
| --- | --- | --- | --- |
| `env` | Yes | `GM` / `Gate` / `Game` | 服务器组身份与环境标签 |
| `logging` | Yes | `GM` / `Gate` / `Game` | 集群级日志配置 |
| `kcp` | Yes | `Gate` | 集群级客户端 KCP 参数 |
| `gm` | Yes | `GM` | `GM` 单例配置 |
| `gate` | Yes | `Gate` | 以 `NodeID` 为键的 Gate 实例集合 |
| `game` | Yes | `Game` | 以 `NodeID` 为键的 Game 实例集合 |

**实例选择规则**
1. `nodeId = GM` 时，加载顶层 `gm` 配置块。
2. `nodeId = Gate0` 时，加载 `gate.Gate0` 配置块。
3. `nodeId = Game0` 时，加载 `game.Game0` 配置块。
4. `gate` / `game` 子键直接使用 `NodeID`，例如 `gate.Gate0`、`game.Game0`。
5. 当前实现只对外暴露两类接口：加载完整 `ClusterConfig`，以及基于 `ClusterConfig + nodeId` 选择具体 `NodeConfig`。

**节点配置要求**
1. `gm.innerNetwork.listenEndpoint` 必填，表示 `GM` 的 `InnerNetwork` 监听地址。
2. `gate.<NodeID>.innerNetwork.listenEndpoint` 必填，表示该 Gate 的 `InnerNetwork` 监听地址。
3. `gate.<NodeID>.clientNetwork.listenEndpoint` 必填，表示该 Gate 的 `ClientNetwork` 监听地址。
4. `game.<NodeID>.innerNetwork.listenEndpoint` 必填，表示该 Game 的 `InnerNetwork` 监听地址。
5. `game.<NodeID>.managed.assemblyName` 可选，缺失时回退到 `XServer.Managed.GameLogic`。
6. `logging` 与 `kcp` 已移动到集群层；不再在 `NodeConfig` 内重复存放这些公共字段。
7. `NodeConfig` 不再显式保存历史的进程类型字段与节点标识字段；节点角色由派生类型体现，实例由 `ClusterConfig` 中 `gm` / `gate` / `game` 的键值体现。

**命名与解析约束**
1. JSON 键统一使用 `lowerCamelCase`。
2. 时长字段统一使用 `*Ms` 后缀。
3. 端点字段统一使用 `*Endpoint` 命名，并采用 `{ "host": string, "port": uint16 }` 结构。
4. 顶层未知配置块与已建模对象中的未知字段默认视为配置错误。
5. 当前实现对 `env`、`logging`、`kcp`、`gm.innerNetwork`、`gate.*.innerNetwork`、`gate.*.clientNetwork`、`game.*.innerNetwork` 与 `game.*.managed` 都执行显式字段校验。

**最小配置示例**

```json
{
  "env": {
    "id": "local-dev",
    "environment": "dev"
  },
  "logging": {
    "rootDir": "logs",
    "minLevel": "Info",
    "flushIntervalMs": 1000,
    "rotateDaily": true,
    "maxFileSizeMB": 64,
    "maxRetainedFiles": 10
  },
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
  "gm": {
    "innerNetwork": {
      "listenEndpoint": {
        "host": "127.0.0.1",
        "port": 5000
      }
    }
  },
  "gate": {
    "Gate0": {
      "innerNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 7000
        }
      },
      "clientNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4000
        }
      }
    }
  },
  "game": {
    "Game0": {
      "innerNetwork": {
        "listenEndpoint": {
          "host": "127.0.0.1",
          "port": 7100
        }
      },
      "managed": {
        "assemblyName": "XServer.Managed.GameLogic"
      }
    }
  }
}
```

当前仓库示例文件位于 `configs/local-dev.json`。

**`logging` 配置项**

| Item | Type | Default | Description |
| --- | --- | --- | --- |
| `rootDir` | `string` | `logs` | 日志输出根目录 |
| `minLevel` | `enum` | `Info` | 最小输出等级 |
| `flushIntervalMs` | `uint32` | `1000` | 常规刷盘间隔 |
| `rotateDaily` | `bool` | `true` | 是否按 UTC 日历日切分日志 |
| `maxFileSizeMB` | `uint32` | `64` | 单文件大小上限 |
| `maxRetainedFiles` | `uint32` | `10` | 每个实例前缀保留的最大历史文件数 |

**日志等级**
1. 统一等级集合为 `Trace`、`Debug`、`Info`、`Warn`、`Error`、`Fatal`。
2. 过滤规则为输出所有 `>= minLevel` 的日志。
3. `Error` 与 `Fatal` 必须尽快刷盘。

**日志文件组织**
1. 实例标识 `instanceId` 固定使用 `GM` 或对应 `NodeID`。
2. 默认文件路径格式为 `<rootDir>/<instanceId>-YYYY-MM-DD.log`。
3. 同一天超出 `maxFileSizeMB` 后，按 `<instanceId>-YYYY-MM-DD.1.log`、`.2.log` 递增分卷。

**单行日志字段**

| Field | Required | Description |
| --- | --- | --- |
| `timestampUtc` | Yes | UTC 时间戳 |
| `level` | Yes | 日志等级 |
| `processType` | Yes | `GM`、`Gate`、`Game` |
| `instanceId` | Yes | `GM` 或 `NodeID` |
| `pid` | Yes | 当前进程号 |
| `category` | Yes | 稳定分类名，例如 `inner.register`、`client.kcp` |
| `message` | Yes | 人类可读消息 |
| `errorCode` | Conditional | 若对应稳定错误码则输出 |
| `errorName` | Conditional | 与 `errorCode` 对应的规范英文名 |
| `context` | Optional | 结构化上下文 |

**实现约束**
1. `M2-02` 的配置加载必须至少支持 `ClusterConfig` 加载、`NodeConfig` 选择、必填块校验与未知字段失败。
2. `M4-02` 的 Gate 客户端监听必须使用 `gate.<NodeID>.clientNetwork.listenEndpoint` 与顶层 `kcp`。
3. 后续若新增 ctrl 工具与 `GM` 的链路配置，应使用 `ControlNetwork`、`controlNetwork.listenEndpoint` 等命名。
