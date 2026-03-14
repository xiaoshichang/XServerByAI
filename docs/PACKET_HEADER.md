# PACKET_HEADER

本文档定义 XServerByAI 当前内部二进制协议的固定包头结构与基础常量。该约定适用于 GM、Gate、Game 之间的内部 TCP 通信，并为后续 Gate↔Client 协议兼容性验证提供基础参照。

**基础约定**
1. 包头固定长度为 `20` 字节，不包含包体长度。
2. 线协议使用网络字节序（大端）编码包头与包体中的基础整数。
3. TCP framing 采用“长度前缀 + 包头 + 包体”的方式；其中 `PacketHeader.length` 仅表示包体长度，不重复包含包头。
4. 未定义的 `flags` 保留位必须为 `0`，接收端应将未知保留位视为协议错误。

**包头结构**
```c
struct PacketHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t length;
  uint32_t msgId;
  uint32_t seq;
};
```

**字段说明**
1. `magic`：协议魔数，固定为 `0x47535052`，ASCII 含义为 `GSPR`。
2. `version`：协议版本，当前固定为 `1`。
3. `flags`：协议标志位，当前定义 3 个低位标志。
4. `length`：包体长度，单位为字节，不含头部。
5. `msgId`：消息编号；编号分段与命名规范见 `docs/MSG_ID.md`，响应默认复用请求 `msgId` 并通过 `Response` 标志位区分。
6. `seq`：请求关联序号；请求与响应共用同一 `seq`，单向推送或无需关联的消息可置为 `0`。

**标志位**
1. `0x0001`：`Response`，置位表示当前消息是对某个请求的响应；未置位表示请求或单向消息。
2. `0x0002`：`Compressed`，置位表示包体经过压缩。
3. `0x0004`：`Error`，置位表示当前消息携带错误语义；错误码范围与编码规则见 `docs/ERROR_CODE.md`。
4. `0x0007`：当前已定义标志位掩码。

**当前常量**
1. `PacketMagic = 0x47535052`
2. `PacketVersion = 1`
3. `PacketHeaderSize = 20`
4. `PacketDefinedFlagMask = 0x0007`
5. `PacketSeqNone = 0`

**兼容性说明**
1. 后续如需增加新标志位，必须保持现有字段顺序与固定头长度不变，或通过版本号升级显式区分。
2. 后续如需增加 `crc`、`traceId`、`routeId` 等字段，应在扩展头或新版本包头中定义，不直接修改当前 `PacketHeader` 的 20 字节布局。
