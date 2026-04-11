# CONFIG_LOGGING

本文档定义 XServerByAI 当前主线代码使用的集群配置模型、节点命名约定与日志输出基线。本文档只保留当前有效配置，不再记录历史版本迁移过程。

## 适用范围

1. 适用于 `xserver-node <configPath> <nodeId>` 读取的单个 UTF-8 JSON 集群配置文件。
2. 适用于 native `GM`、`Gate`、`Game` 三类节点的配置选择逻辑。
3. 适用于 `src/native/core/Config.*` 与 `src/native/core/Logging.*` 当前实现。

## 启动入口与 NodeID

1. native 统一使用 `xserver-node <configPath> <nodeId>` 启动。
2. `configPath` 必须指向单个 UTF-8 JSON 配置文件，例如 `configs/local-dev.json`。
3. `nodeId` 当前支持 `GM`、`Gate<index>`、`Game<index>`，例如 `GM`、`Gate0`、`Game1`。
4. `Gate` 与 `Game` 的稳定逻辑身份统一称为 `NodeID`；当前命令行参数里的 `nodeId` 与配置键名保持一致。

## 顶层配置模型

当前配置文件固定包含以下顶层块：

| Key | Required | Scope | Description |
| --- | --- | --- | --- |
| `env` | Yes | cluster | 服务器组标识与环境标签 |
| `logging` | Yes | cluster | 日志根目录、等级与滚动策略 |
| `kcp` | Yes | cluster | `Gate -> Client` KCP 参数 |
| `managed` | Yes | cluster | CLR 宿主装载 `Framework` 的路径与搜索目录 |
| `gm` | Yes | cluster | `GM` 的 `Inner` / `Control` 监听地址 |
| `gate` | Yes | cluster | 全部 `Gate` 实例配置，键名直接使用 `NodeID` |
| `game` | Yes | cluster | 全部 `Game` 实例配置，键名直接使用 `NodeID` |

当前实现中：

1. `logging`、`kcp` 与 `managed` 是集群级共享配置。
2. `GM` 使用 `gm`。
3. `Gate` 从 `gate.<NodeID>` 读取实例级配置。
4. `Game` 从 `game.<NodeID>` 读取实例级配置，并额外继承顶层 `managed`。

## JSON 命名与校验规则

1. 所有 JSON 键统一使用 `lowerCamelCase`。
2. 时长字段统一使用 `*Ms` 后缀。
3. 监听地址统一使用：

```json
{
  "listenEndpoint": {
    "host": "127.0.0.1",
    "port": 5000
  }
}
```

4. 当前配置解析是严格模式：缺失必填字段会失败，未知字段也会失败。
5. `port` 必须大于 `0`。
6. `host` 不允许为空字符串。
7. 路径字段按当前配置文件所在目录解析相对路径。

## 当前示例配置

当前仓库样例文件是 [configs/local-dev.json](/C:/Users/xiao/Documents/GitHub/XServerByAI/configs/local-dev.json)，其结构如下：

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
  "managed": {
    "assemblyName": "XServer.Managed.Framework",
    "assemblyPath": "../src/managed/Framework/bin/Debug/net10.0/XServer.Managed.Framework.dll",
    "runtimeConfigPath": "../src/managed/Framework/bin/Debug/net10.0/XServer.Managed.Framework.runtimeconfig.json",
    "searchAssemblyPaths": [
      "../src/managed/Framework/bin/Debug/net10.0/XServer.Managed.Framework.dll",
      "../src/managed/GameLogic/bin/Debug/net10.0/XServer.Managed.GameLogic.dll"
    ]
  },
  "gm": {
    "innerNetwork": {
      "listenEndpoint": {
        "host": "127.0.0.1",
        "port": 5000
      }
    },
    "controlNetwork": {
      "listenEndpoint": {
        "host": "127.0.0.1",
        "port": 5100
      }
    }
  },
  "gate": {
    "Gate0": {
      "innerNetwork": {
        "listenEndpoint": {
          "host": "127.0.0.1",
          "port": 7000
        }
      },
      "authNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4100
        }
      },
      "clientNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4000
        }
      }
    },
    "Gate1": {
      "innerNetwork": {
        "listenEndpoint": {
          "host": "127.0.0.1",
          "port": 7001
        }
      },
      "authNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4101
        }
      },
      "clientNetwork": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 4001
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
      }
    },
    "Game1": {
      "innerNetwork": {
        "listenEndpoint": {
          "host": "127.0.0.1",
          "port": 7101
        }
      }
    }
  }
}
```

## 各配置块说明

### `env`

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | `string` | Yes | 当前服务器组标识，例如 `local-dev` |
| `environment` | `string` | Yes | 环境标签，例如 `dev` / `staging` / `prod` |

### `logging`

| Field | Type | Required | Default | Description |
| --- | --- | --- | --- | --- |
| `rootDir` | `string` | Yes | `logs` | 日志根目录 |
| `minLevel` | `enum` | Yes | `Info` | 最低输出等级 |
| `flushIntervalMs` | `uint32` | Yes | `1000` | 周期性 flush 间隔，必须大于 `0` |
| `rotateDaily` | `bool` | Yes | `true` | 是否按 UTC 日期滚动 |
| `maxFileSizeMB` | `uint32` | Yes | `64` | 单文件大小上限，必须大于 `0` |
| `maxRetainedFiles` | `uint32` | Yes | `10` | 允许保留的历史日志文件数量，必须大于 `0` |

`minLevel` 当前支持：

1. `Trace`
2. `Debug`
3. `Info`
4. `Warn`
5. `Error`
6. `Fatal`

### `kcp`

字段定义与默认值见 [KCP_CONFIG.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/KCP_CONFIG.md)。当前这些参数只作用于 `Gate` 的 `clientNetwork`。

### `managed`

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `assemblyName` | `string` | Yes | CLR 宿主加载的根程序集名，当前默认 `XServer.Managed.Framework` |
| `assemblyPath` | `path` | Yes | 根程序集 `.dll` 路径 |
| `runtimeConfigPath` | `path` | Yes | `.runtimeconfig.json` 路径 |
| `searchAssemblyPaths` | `path[]` | No | 额外可扫描程序集路径，`GM` 用它发现 `ServerStub` catalog |

当前实现中：

1. `GM` 会加载 `managed`，但只读取 stub catalog，不调用 `GameNativeInit`。
2. 所有 `Game` 节点都会继承同一份 `managed` 配置。

### `gm`

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `innerNetwork.listenEndpoint` | `Endpoint` | Yes | `GM` 的内部监听地址 |
| `controlNetwork.listenEndpoint` | `Endpoint` | Yes | `GM` 管理 HTTP 监听地址 |

当前 `GM` 的 `controlNetwork` 已落地以下 HTTP 路由：

1. `GET /healthz`
2. `GET /status`
3. `GET /boardcase`
4. `POST /shutdown`

### `gate`

每个 `gate.<NodeID>` 都必须包含：

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `innerNetwork.listenEndpoint` | `Endpoint` | Yes | `Gate` 的内部监听地址，供 `Game -> Gate` 注册与转发 |
| `authNetwork.listenEndpoint` | `Endpoint` | Yes | `Gate` 的 HTTP 登录入口 |
| `clientNetwork.listenEndpoint` | `Endpoint` | Yes | `Gate` 的 KCP 客户端入口 |

当前 `authNetwork` 已落地以下 HTTP 路由：

1. `GET /healthz`
2. `POST /login`

### `game`

每个 `game.<NodeID>` 当前只要求：

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `innerNetwork.listenEndpoint` | `Endpoint` | Yes | `Game` 的内部监听地址 |

`Game` 的 managed 配置不再写在 `game.<NodeID>` 下，而是统一继承顶层 `managed`。

## 日志输出约定

1. `LoggerOptions.instance_id` 当前固定使用 `GM` 或实际 `NodeID`，例如 `Gate0`、`Game1`。
2. 默认文件路径格式为 `<rootDir>/<instanceId>-YYYY-MM-DD.log`。
3. 同一天内超过 `maxFileSizeMB` 后，会继续写入 `<instanceId>-YYYY-MM-DD.1.log`、`.2.log` 等分卷。
4. `rotateDaily = false` 时，当前实例持续写同一日期基准文件，直到达到大小滚动条件。
5. `Error` 与 `Fatal` 会立即 flush；其余等级按 `flushIntervalMs` 周期 flush。
6. 每行日志至少包含 UTC 时间、等级、进程类型、实例标识、category、message；若提供 `errorCode` / `errorName` / 上下文字段，也会一并输出。

## 关联文档

1. 项目总览见 [PROJECT.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/PROJECT.md)。
2. KCP 参数见 [KCP_CONFIG.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/KCP_CONFIG.md)。
3. managed ABI 与宿主约定见 [MANAGED_INTEROP.md](/C:/Users/xiao/Documents/GitHub/XServerByAI/docs/MANAGED_INTEROP.md)。
