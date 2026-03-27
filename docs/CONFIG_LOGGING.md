# CONFIG_LOGGING

鏈枃妗ｅ畾涔?XServerByAI 褰撳墠闃舵鐨勯厤缃粍缁囨柟寮忋€佺綉缁滃懡鍚嶈鑼冧笌鏃ュ織杈撳嚭鍩虹嚎銆傚悗缁?`M1-16`銆乣M2-01`銆乣M2-02`銆乣M3-06`銆乣M4-02`銆乣M5-17`銆乣M6-06` 涓?`M6-09` 搴斾互鏈枃浠朵负鍑嗐€?

**缃戠粶鍛藉悕瑙勮寖**
1. `Inner` 琛ㄧず鑺傜偣涓庤妭鐐逛箣闂寸殑鍐呴儴缃戠粶锛屼緥濡?`InnerNetwork`銆乣innerNetwork.listenEndpoint`銆乣innerNetworkEndpoint`銆?
2. `Control` 琛ㄧず ctrl 宸ュ叿涓?`GM` 涔嬮棿鐨勭鐞嗙綉缁溿€傚綋鍓嶉樁娈靛凡钀藉湴 `GM` 鏈湴 HTTP 绠＄悊鎺ュ彛閰嶇疆鍧楋紝缁熶竴浣跨敤 `ControlNetwork`銆乣controlNetwork.listenEndpoint` 绛夊懡鍚嶃€?
3. `Client` 琛ㄧず `Gate` 涓庡鎴风涔嬮棿鐨勭綉缁滐紝渚嬪 `ClientNetwork`銆乣clientNetwork.listenEndpoint`銆?
4. 涓嶅啀浣跨敤鍘嗗彶閬楃暀鏈鍘昏〃杈捐妭鐐归棿閾捐矾鎴栧疄渚嬮€夋嫨銆?

**缁熶竴鍚姩鍏ュ彛**
1. native 缁熶竴浣跨敤 `xserver-node <configPath> <nodeId>` 鍚姩銆?
2. `configPath` 鎸囧悜鍗曚釜 UTF-8 JSON 閰嶇疆鏂囦欢锛屼緥濡?`configs/local-dev.json`銆?
3. `nodeId` 褰撳墠鍙帴鍙?`GM`銆乣Gate<index>`銆乣Game<index>` 褰㈠紡锛屼緥濡?`GM`銆乣Gate0`銆乣Game1`銆?
4. `nodeId` 鍙礋璐ｅ湪鍗曟枃浠朵腑閫夋嫨瀹炰緥锛涚ǔ瀹氶€昏緫韬唤缁熶竴绉颁负 `NodeID`锛屽綋鍓嶄笌鍚姩鍙傛暟鍙栧€间竴鑷淬€?

**椤跺眰閰嶇疆鍧?*

| Block | Required | Applies To | Description |
| --- | --- | --- | --- |
| `env` | Yes | `GM` / `Gate` / `Game` | 鏈嶅姟鍣ㄧ粍韬唤涓庣幆澧冩爣绛?|
| `logging` | Yes | `GM` / `Gate` / `Game` | 闆嗙兢绾ф棩蹇楅厤缃?|
| kcp | Yes | Gate | 闆嗙兢绾у鎴风 KCP 鍙傛暟 |
| managed | Yes | GM / Game | 共享托管程序集标识与 runtime asset 路径 |
| `gm` | Yes | `GM` | `GM` 鍗曚緥閰嶇疆 |
| `gate` | Yes | `Gate` | 浠?`NodeID` 涓洪敭鐨?Gate 瀹炰緥闆嗗悎 |
| `game` | Yes | `Game` | 浠?`NodeID` 涓洪敭鐨?Game 瀹炰緥闆嗗悎 |

**瀹炰緥閫夋嫨瑙勫垯**
1. `nodeId = GM` 鏃讹紝鍔犺浇椤跺眰 `gm` 閰嶇疆鍧椼€?
2. `nodeId = Gate0` 鏃讹紝鍔犺浇 `gate.Gate0` 閰嶇疆鍧椼€?
3. `nodeId = Game0` 鏃讹紝鍔犺浇 `game.Game0` 閰嶇疆鍧椼€?
4. `gate` / `game` 瀛愰敭鐩存帴浣跨敤 `NodeID`锛屼緥濡?`gate.Gate0`銆乣game.Game0`銆?
5. 褰撳墠瀹炵幇鍙澶栨毚闇蹭袱绫绘帴鍙ｏ細鍔犺浇瀹屾暣 `ClusterConfig`锛屼互鍙婂熀浜?`ClusterConfig + nodeId` 閫夋嫨鍏蜂綋 `NodeConfig`銆?

**鑺傜偣閰嶇疆瑕佹眰**
1. `gm.innerNetwork.listenEndpoint` 蹇呭～锛岃〃绀?`GM` 鐨?`InnerNetwork` 鐩戝惉鍦板潃銆?
2. `gm.controlNetwork.listenEndpoint` 蹇呭～锛岃〃绀?`GM` 鐨勬湰鍦?HTTP 绠＄悊鎺ュ彛鐩戝惉鍦板潃銆?
3. `gate.<NodeID>.innerNetwork.listenEndpoint` 蹇呭～锛岃〃绀鸿 Gate 鐨?`InnerNetwork` 鐩戝惉鍦板潃銆?
4. `gate.<NodeID>.clientNetwork.listenEndpoint` 蹇呭～锛岃〃绀鸿 Gate 鐨?`ClientNetwork` 鐩戝惉鍦板潃銆?
5. `game.<NodeID>.innerNetwork.listenEndpoint` 蹇呭～锛岃〃绀鸿 Game 鐨?`InnerNetwork` 鐩戝惉鍦板潃銆?
6. `managed.assemblyName` 默认为 `XServer.Managed.GameLogic`；`managed.assemblyPath` 与 `managed.runtimeConfigPath` 由顶层共享提供。
7. `logging` 涓?`kcp` 宸茬Щ鍔ㄥ埌闆嗙兢灞傦紱涓嶅啀鍦?`NodeConfig` 鍐呴噸澶嶅瓨鏀捐繖浜涘叕鍏卞瓧娈点€?
8. `NodeConfig` 涓嶅啀鏄惧紡淇濆瓨鍘嗗彶鐨勮繘绋嬬被鍨嬪瓧娈典笌鑺傜偣鏍囪瘑瀛楁锛涜妭鐐硅鑹茬敱娲剧敓绫诲瀷浣撶幇锛屽疄渚嬬敱 `ClusterConfig` 涓?`gm` / `gate` / `game` 鐨勯敭鍊间綋鐜般€?

**鍛藉悕涓庤В鏋愮害鏉?*
1. JSON 閿粺涓€浣跨敤 `lowerCamelCase`銆?
2. 鏃堕暱瀛楁缁熶竴浣跨敤 `*Ms` 鍚庣紑銆?
3. 绔偣瀛楁缁熶竴浣跨敤 `*Endpoint` 鍛藉悕锛屽苟閲囩敤 `{ "host": string, "port": uint16 }` 缁撴瀯銆?
4. 椤跺眰鏈煡閰嶇疆鍧椾笌宸插缓妯″璞′腑鐨勬湭鐭ュ瓧娈甸粯璁よ涓洪厤缃敊璇€?
5. 当前实现对 `env`、`logging`、`kcp`、`managed`、`gm.innerNetwork`、`gm.controlNetwork`、`gate.*.innerNetwork`、`gate.*.clientNetwork` 与 `game.*.innerNetwork` 都执行显式字段校验。

**鏈€灏忛厤缃ず渚?*

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

褰撳墠浠撳簱绀轰緥鏂囦欢浣嶄簬 `configs/local-dev.json`銆?

**`logging` 閰嶇疆椤?*

| Item | Type | Default | Description |
| --- | --- | --- | --- |
| `rootDir` | `string` | `logs` | 鏃ュ織杈撳嚭鏍圭洰褰?|
| `minLevel` | `enum` | `Info` | 鏈€灏忚緭鍑虹瓑绾?|
| `flushIntervalMs` | `uint32` | `1000` | 甯歌鍒风洏闂撮殧 |
| `rotateDaily` | `bool` | `true` | 鏄惁鎸?UTC 鏃ュ巻鏃ュ垏鍒嗘棩蹇?|
| `maxFileSizeMB` | `uint32` | `64` | 鍗曟枃浠跺ぇ灏忎笂闄?|
| `maxRetainedFiles` | `uint32` | `10` | 姣忎釜瀹炰緥鍓嶇紑淇濈暀鐨勬渶澶у巻鍙叉枃浠舵暟 |

**鏃ュ織绛夌骇**
1. 缁熶竴绛夌骇闆嗗悎涓?`Trace`銆乣Debug`銆乣Info`銆乣Warn`銆乣Error`銆乣Fatal`銆?
2. 杩囨护瑙勫垯涓鸿緭鍑烘墍鏈?`>= minLevel` 鐨勬棩蹇椼€?
3. `Error` 涓?`Fatal` 蹇呴』灏藉揩鍒风洏銆?

**鏃ュ織鏂囦欢缁勭粐**
1. 瀹炰緥鏍囪瘑 `instanceId` 鍥哄畾浣跨敤 `GM` 鎴栧搴?`NodeID`銆?
2. 榛樿鏂囦欢璺緞鏍煎紡涓?`<rootDir>/<instanceId>-YYYY-MM-DD.log`銆?
3. 鍚屼竴澶╄秴鍑?`maxFileSizeMB` 鍚庯紝鎸?`<instanceId>-YYYY-MM-DD.1.log`銆乣.2.log` 閫掑鍒嗗嵎銆?

**鍗曡鏃ュ織瀛楁**

| Field | Required | Description |
| --- | --- | --- |
| `timestampUtc` | Yes | UTC 鏃堕棿鎴?|
| `level` | Yes | 鏃ュ織绛夌骇 |
| `processType` | Yes | `GM`銆乣Gate`銆乣Game` |
| `instanceId` | Yes | `GM` 鎴?`NodeID` |
| `pid` | Yes | 褰撳墠杩涚▼鍙?|
| `category` | Yes | 绋冲畾鍒嗙被鍚嶏紝渚嬪 `inner.register`銆乣client.kcp` |
| `message` | Yes | 浜虹被鍙娑堟伅 |
| `errorCode` | Conditional | 鑻ュ搴旂ǔ瀹氶敊璇爜鍒欒緭鍑?|
| `errorName` | Conditional | 涓?`errorCode` 瀵瑰簲鐨勮鑼冭嫳鏂囧悕 |
| `context` | Optional | 缁撴瀯鍖栦笂涓嬫枃 |

**瀹炵幇绾︽潫**
1. `M2-02` 鐨勯厤缃姞杞藉繀椤昏嚦灏戞敮鎸?`ClusterConfig` 鍔犺浇銆乣NodeConfig` 閫夋嫨銆佸繀濉潡鏍￠獙涓庢湭鐭ュ瓧娈靛け璐ャ€?
2. `M4-02` 鐨?Gate 瀹㈡埛绔洃鍚繀椤讳娇鐢?`gate.<NodeID>.clientNetwork.listenEndpoint` 涓庨《灞?`kcp`銆?
3. `M3-07` 鐨?`GM` 鏈湴 HTTP 绠＄悊鎺ュ彛浣跨敤 `gm.controlNetwork.listenEndpoint`锛涘悗缁嫢鎵╁睍澶栭儴 ctrl 宸ュ叿锛屼篃蹇呴』缁х画娌跨敤 `ControlNetwork`銆乣controlNetwork.listenEndpoint` 绛夊懡鍚嶃€?

**M3-17 补充：顶层 managed**
1. 顶层新增 `managed` 配置块，当前由 `GM` 与全部 `Game` 共用；`game.<NodeID>` 不再内嵌 `managed`。
2. `managed` 当前包含 `assemblyName`、`assemblyPath` 与 `runtimeConfigPath` 三个字段；`assemblyPath` 与 `runtimeConfigPath` 支持相对配置文件所在目录解析。
3. `GM` 通过顶层 `managed` 加载托管程序集并读取 ServerStub catalog；`Game` 仍复用同一块配置初始化托管运行时。
4. 配置校验会对顶层 `managed` 做显式字段检查，未知字段与缺失必需路径都会按配置错误处理。
