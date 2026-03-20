# CONFIG_LOGGING

本文档定义 XServerByAI 当前阶段的进程配置组织方式、逻辑配置模型与日志输出规范。后续 `M2-01`、`M2-02`、`M3-06`、`M5-17`、`M6-06` 与 `M6-09` 应以本文件作为统一基线。当前 native 日志后端基线为 vendored `spdlog`，但配置键名、等级与文件组织规则不与单一实现强绑定。

**适用范围**
1. 覆盖 `GM`、`Gate`、`Game` 三类进程的启动期静态配置，以及 native / managed 共用的日志等级、文件组织与记录字段。
2. 不定义密钥管理、远程配置中心、环境变量注入或热更新协议；如需引入，应在兼容当前逻辑模型的前提下单独扩展。
3. 架构允许多机多进程部署；当前开发与联调默认使用单个配置文件 + 多次进程启动的单机多进程方式，跨机部署时复用相同配置键名与日志字段。

**统一启动入口**
1. native 统一使用 `xserver-node <configPath> <selector>` 启动。
2. `configPath` 指向包含整个服务器组配置的单个 UTF-8 JSON 文件，例如 `config.json`。
3. `selector` 当前只接受 `gm`、`gate<index>`、`game<index>` 形式，例如 `gm`、`gate0`、`game1`。
4. `selector` 只负责在单文件中选择启动实例，不替代协议层 `NodeID`；例如 `gate0` 对应的稳定逻辑身份仍为 `Gate0`。

**配置载体与目录布局**
1. 正式运行配置固定使用单个 UTF-8 JSON 文件与 `.json` 扩展名，不并行维护 YAML 正式版本。
2. 配置文件名由部署方决定，可使用 `config.json`、`local-dev.json` 等任意稳定命名；运行时以传入的 `configPath` 为准。
3. 推荐布局如下：

```text
configs/
  config.json

logs/
  GM-2026-03-15.log
  Gate0-2026-03-15.log
  Game0-2026-03-15.log
```

4. 当前阶段不支持跨文件 `include`、`shared.json` + 实例文件拼装，避免多来源优先级歧义。
5. `logs/` 为默认运行期输出根目录，不纳入版本控制。

**顶层块与实例选择**

| Block | Required | Applies To | Description |
| --- | --- | --- | --- |
| `serverGroup` | Yes | `GM` / `Gate` / `Game` | 服务器组身份与环境标签，例如 `id`、`environment` |
| `logging` | Yes | `GM` / `Gate` / `Game` | 公共日志默认值 |
| `gm` | Yes | `GM` | `gm` 选择器对应的单例配置块 |
| `gate` | Yes | `Gate` | 以 `gate0`、`gate1` 等选择器为子键的 Gate 实例集合 |
| `game` | Yes | `Game` | 以 `game0`、`game1` 等选择器为子键的 Game 实例集合 |

选择规则：
1. `selector = gm` 时，加载顶层 `gm` 配置块。
2. `selector = gate0` 时，加载 `gate.gate0` 配置块。
3. `selector = game0` 时，加载 `game.game0` 配置块。
4. Gate / Game 实例块都必须包含 `nodeId` 字段，且与选择器的规范 `NodeID` 一致，例如 `gate0 -> Gate0`。
5. Gate / Game 实例块都必须提供 `service.listenEndpoint`；其中 `Game` 的该地址会在注册时映射到 `docs/PROCESS_CONTROL.md` 中的 `serviceEndpoint`，供 Gate 建立 ZeroMQ over TCP 内部连接。
6. `gm` 配置块必须提供 `control.listenEndpoint`，作为 GM 内部控制面的监听地址。

**命名与解析规则**
1. 配置键统一使用 `lowerCamelCase`。
2. 时长字段统一使用 `*Ms` 后缀；容量字段优先使用 `*Bytes`、`*KB`、`*MB`；端点字段统一命名为 `*Endpoint` 并使用 `{ "host": string, "port": uint16 }` 结构。
3. `gm`、`gate0`、`game0` 这类实例选择器使用全小写，且只用于配置选择与启动参数；协议、注册表和路由层继续使用 `GM`、`Gate0`、`Game0` 这类稳定标识。
4. 单次启动只做一次“代码默认值 + 顶层公共块 + 选中实例块”的解析。
5. 顶层 `logging` 作为公共默认块；若选中实例内存在 `logging` 子块，则按键覆盖公共 `logging` 字段。
6. 数组字段整体替换，不做按索引局部 merge。
7. 未知顶层配置块与未知 `logging` 字段默认视为配置错误，避免静默吞掉拼写错误。
8. 已建模对象同样执行未知字段校验，例如 `serverGroup`、`gm.control`、`gate.*.service`、`gate.*.kcp` 与 `game.*.managed`。
9. `game.*.managed.assemblyName` 为可选字段；缺失时默认回退为 `XServer.Managed.GameLogic`。

**最小配置示例**

```json
{
  "serverGroup": {
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
  "gm": {
    "control": {
      "listenEndpoint": {
        "host": "127.0.0.1",
        "port": 5000
      }
    }
  },
  "gate": {
    "gate0": {
      "nodeId": "Gate0",
      "service": {
        "listenEndpoint": {
          "host": "0.0.0.0",
          "port": 7000
        }
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
      }
    }
  },
  "game": {
    "game0": {
      "nodeId": "Game0",
      "service": {
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

**`logging` 配置块**

| Item | Type | Default | Description |
| --- | --- | --- | --- |
| `rootDir` | `string` | `logs` | 日志输出根目录；若为相对路径，则相对于进程当前工作目录解析 |
| `minLevel` | `enum` | `Info` | 最小输出等级 |
| `flushIntervalMs` | `uint32` | `1000` | 普通日志批量刷盘间隔，`Error` / `Fatal` 仍应尽快刷盘 |
| `rotateDaily` | `bool` | `true` | 是否按 UTC 日期切分日志文件 |
| `maxFileSizeMB` | `uint32` | `64` | 单文件达到阈值后在同一天内追加分卷 |
| `maxRetainedFiles` | `uint32` | `10` | 每个实例前缀保留的最大历史文件数 |

约束：
1. `rootDir` 不得为空字符串。
2. `minLevel` 仅允许 `Trace`、`Debug`、`Info`、`Warn`、`Error`、`Fatal`。
3. `flushIntervalMs`、`maxFileSizeMB`、`maxRetainedFiles` 必须为正整数。

**日志等级**

| Level | Order | Description |
| --- | --- | --- |
| `Trace` | 0 | 最细粒度的调试轨迹，默认关闭 |
| `Debug` | 1 | 调试级别的流程与状态变化 |
| `Info` | 2 | 正常运行中的关键生命周期与状态信息 |
| `Warn` | 3 | 不致命但值得关注的异常、降级或兼容处理 |
| `Error` | 4 | 当前操作失败，需要排障或恢复动作 |
| `Fatal` | 5 | 进程即将退出或关键能力不可恢复 |

规则：
1. 过滤规则按等级顺序比较，输出所有 `>= minLevel` 的日志。
2. `Error` 与 `Fatal` 记录必须尽快刷盘。
3. 后续 `M5-17` 将 managed 日志桥接到 native 时，必须复用同一套等级集合。

**日志文件组织与滚动**
1. 日志实例标识 `instanceId` 的取值固定为：`GM` 使用 `GM`，`Gate` / `Game` 使用各自实例块内的 `nodeId`。
2. 默认文件路径格式为 `<rootDir>/<instanceId>-YYYY-MM-DD.log`，日期使用 UTC 日历日。
3. 当同一天的当前日志文件超过 `maxFileSizeMB` 时，按 `<instanceId>-YYYY-MM-DD.1.log`、`<instanceId>-YYYY-MM-DD.2.log` 递增追加分卷。
4. `maxRetainedFiles` 以同一 `instanceId` 前缀为作用域，超出上限时删除最旧文件。
5. 实现应自动创建缺失目录；目录创建失败属于基础设施错误。

**单行记录字段**

| Field | Required | Description |
| --- | --- | --- |
| `timestampUtc` | Yes | UTC 时间戳，格式固定为 `yyyy-MM-ddTHH:mm:ss.fffZ` |
| `level` | Yes | 日志等级 |
| `processType` | Yes | `GM`、`Gate`、`Game` |
| `instanceId` | Yes | `GM` 或 `NodeID` |
| `pid` | Yes | 当前操作系统进程号 |
| `category` | Yes | 稳定的分类名，例如 `net.kcp`、`control.register` |
| `message` | Yes | 人可读消息正文 |
| `errorCode` | Conditional | 若记录对应已登记失败码，则输出数值错误码 |
| `errorName` | Conditional | 若输出 `errorCode`，则同步输出规范英文名 |
| `context` | Optional | 追加的 `key=value` 结构化上下文 |

推荐文本顺序如下：

```text
<timestampUtc> <level> <processType> <instanceId> pid=<pid> cat=<category> msg="<message>" [errorCode=<code> errorName=<name>] [k=v ...]
```

**对后续实现条目的约束**
1. `M2-01` 的日志模块至少需要基于 `spdlog` 实现文件输出、最小等级过滤、单行文本格式、按天滚动与大小补充分卷。
2. `M2-02` 的配置加载应至少支持单文件解析、实例选择、必填块校验与未知字段失败策略。
3. `M3-06` 不定义独立配置下发消息；GM / Gate / Game 继续以进程启动时传入的统一配置文件作为配置事实来源。
4. `M5-17` 的 managed 日志桥接必须保留 `level`、`category`、`message` 与结构化上下文，不得把所有 managed 日志折叠成单一 native 类别。
5. `M6-06` 编写 GM / Gate / Game 配置文件时，应以本文给出的单文件布局、选择器规则、键名和 `logging` 默认值为模板。
