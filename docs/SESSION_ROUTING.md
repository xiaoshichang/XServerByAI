# SESSION_ROUTING

���ĵ����� XServerByAI ��ǰ�׶� Gate ��ͻ��˻Ự��Gate �Ĵ���·�ɰ��Լ� GM �ṩ�� Game �ڵ�·��Ŀ¼������ģ�͡����� `M4-04`��`M4-10`��`M4-11`��`M4-12` �� `M5-08` ��ʵ�ֻỰ�������������̶���·�ɡ����ظ�֪������ʵ��·��ʱ��Ӧ�Ա��ļ���Ϊ�ֶ�������״̬Լ���Ļ��ߣ�ʵ���� ownership��Ǩ�������� `Mailbox` / `Proxy` ����� `docs/DISTRIBUTED_ENTITY.md`��

**���÷�Χ**
1. ��ǰ���� Gate ����ά���Ŀͻ��˻Ự��¼���Ự�� Game �İ󶨹�ϵ���Լ��� Gate ѡ��Ŀ�� Game ��Ŀ¼��Ŀ��
2. ��ǰģ�ͷ����� `KCP session -> Gate logical session -> transport route target -> C# entity route hint` ��һ��·����ֱ�ӳ��ؼ�ȨЭ��ϸ�ڻ򳡾�/ʵ��ҵ��״̬������ʵ����ࡢǨ��������·���յ������� `docs/DISTRIBUTED_ENTITY.md` ͳһ���塣
3. ��ǰ�׶β������ Gate �Ĺ���Ự��������ỰǨ�ơ������� Game �䶯̬����Ǩ�ƣ���Щ�����������룬Ӧ�ڼ��ݱ�ģ�͵�ǰ������չ��
4. ��ǰĬ��ÿ����������̶� `1` �� GM������ͬ���� `N` �� Gate �ڵ��� `M` �� Game �ڵ㣬���� `N >= 1`��`M >= 1`��Gate �� Game ����ȫ�������ˣ�Game?Game �� Gate?Gate ��ֱ�����ӡ�

**����Լ��**
1. ���ģ���е��ֶα����л����ڲ�Э����Ϣ�壬Ӧ�����ڲ�Э��ͳһԼ����ʹ�������ֽ��򣨴�ˣ����룻`string`��`string[]`��`Endpoint` �� `LoadSnapshot` �ı�������� `docs/PROCESS_INNER.md`��
2. `sessionId` ʹ�� `uint64` ���壬`0` Ϊ��Чֵ��ͬһ�� Gate �������������ڲ��ø����ѷ���� `sessionId`��
3. `playerId` ʹ�� `uint64` ���壬`0` ��ʾ����δ����ҡ��򡰸ý׶�δ֪����������Ϸ���ұ�ų�ͻ��
4. `gameNodeId` �� `gateNodeId` ���� `Inner.ProcessRegister.nodeId`���ֱ��ʾ Game/Gate �ڵ���ȶ��߼���ݣ���ǰ�׶β���Ϊ·�ɹ�ϵ��������ע����Լ�ֶΡ�
5. ����ʱ����ֶξ�ʹ�� `unixTimeMsUtc` ���壻δ֪����δ������ʱ���ͳһ�� `0`��
6. �Ự�����ڵ�ǰ�׶���Ϊ������һ���»Ự������ `sessionId` ���ñ������ת�Ƹ��µ��������ӡ�
7. `NodeID` ʹ�����ִ�Сд�ĸ�ʽ `<ProcessType><index>`������ `Gate0`��`Gate1`��`Game0`��`Game1`��ͬһ���������ڲ��ó����ظ� `NodeID`��

**ö�٣�SessionState**

| ֵ | ���� | ˵�� |
| --- | --- | --- |
| `1` | `Created` | Gate �Ѵ����߼��Ự������δ��ɼ�Ȩ��ҵ��� |
| `2` | `Authenticating` | ����ִ�м�Ȩ���˺�У������װ�ص�ǰ������ |
| `3` | `Active` | �Ự���շ�ҵ����Ϣ�������������·�� |
| `4` | `Closing` | �ѽ����������̣����ٽ����µ�ҵ������ |
| `5` | `Closed` | �Ự�ѹرգ���������ϻ��ӳٻ���������Ϣ |

**ö�٣�RouteState**

| ֵ | ���� | ˵�� |
| --- | --- | --- |
| `1` | `Unassigned` | ��δ����Ŀ�� Game |
| `2` | `Selecting` | Gate ���ڸ��ݵ�ǰĿ¼��ѡĿ�� Game |
| `3` | `Bound` | �ѹ̶��󶨵����� Game������ҵ������Ӧ��������ͬһĿ�� |
| `4` | `RouteLost` | ��ǰ�󶨵� Game �Ѳ����á�����ʧЧ�������״̬ʧЧ |
| `5` | `Released` | �Ự�ѽ�����·�ɹ�ϵ���ͷ� |

**ö�٣�GameRouteState**

| ֵ | ���� | ˵�� |
| --- | --- | --- |
| `1` | `Available` | �ɽ����µĻỰ�� |
| `2` | `Draining` | ���ٽ����»Ự�����������лỰ��������ֱ������ |
| `3` | `Unavailable` | ���ɱ�ѡ��ΪĿ��·�ɣ����а��ڼ�⵽��Ӧ��ΪʧЧ |

**����ṹ��RouteTarget**

| �ֶ� | ���� | ˵�� |
| --- | --- | --- |
| `gameNodeId` | `string` | Ŀ�� Game �ڵ���ȶ��߼���ݣ�ȡֵ����ע����е� `nodeId` |
| `innerNetworkEndpoint` | `Endpoint` | Gate ����Ŀ�� Game ʹ�õķ������ |
| `routeEpoch` | `uint64` | Gate ����·��Ŀ¼�汾��ÿ��Ŀ¼�������� |

`RouteTarget` ����ǰ�Ự���󶨵��ĸ� Game �ڵ㡱����С�����л����塣`gameNodeId` �����ȶ���ݣ�`routeEpoch` ��������ð󶨻�����һ�汾��Ŀ¼�ó���

**����ṹ��SessionRecord**

| �ֶ� | ���� | ˵�� |
| --- | --- | --- |
| `sessionId` | `uint64` | Gate ������߼��Ự��ʶ������������Ψһ�ҷ��� |
| `gateNodeId` | `string` | ��ǰ���ظûỰ�� Gate �ڵ��ȶ��߼���� |
| `sessionState` | `uint16` | �Ự��������״̬��ȡֵ�� `SessionState` |
| `playerId` | `uint64` | ��ǰ����ұ�ʶ��δ��ʱ�� `0` |
| `routeState` | `uint16` | ·��״̬��ȡֵ�� `RouteState` |
| `routeTarget` | `RouteTarget` | ��ǰ�󶨵�Ŀ�� Game��δ��ʱ��Ϊȫ�ֶ���Ч |
| `connectedAtUnixMs` | `uint64` | �߼��Ự����ʱ�� |
| `authenticatedAtUnixMs` | `uint64` | ��ɼ�Ȩ�����װ�ص�ʱ�䣻δ���ʱΪ `0` |
| `lastActiveUnixMs` | `uint64` | ���һ���շ��ͻ���ҵ����Ϣ��ʱ�� |
| `closedAtUnixMs` | `uint64` | �Ự�ر�ʱ�䣻�Դ��ʱΪ `0` |
| `closeReasonCode` | `int32` | �Ự�ر�ԭ��δ֪��δ�ر�ʱΪ `0` |

`SessionRecord` �� Gate �ڲ���Ȩ���Ựģ�͡��κλỰ�������·���жϡ���Ұ󶨻����ʵ��·����չ����Ӧ�Ըü�¼Ϊ��һ��ʵ��Դ��������ɢ�������Ӷ��󡢶�ʱ����ҵ����еĸ����ֶΡ�

���� `routeTarget` ������ Gate �Ự��ǰ���еĴ����Ŀ�� Game������������ҵ��ʵ������� owner������ `PlayerEntity` �����Ǩ��ʵ�壬Gate ������ `sessionId` / `playerId` ����֮�϶���ά�� `Proxy` ������Ϣ������ `SpaceEntity`��`ServerStubEntity` ���಻��Ǩ��ʵ�壬��Ӧ���� `Mailbox` �ľ�̬Ѱַ���崦���

**����ṹ��GameDirectoryEntry**

| �ֶ� | ���� | ˵�� |
| --- | --- | --- |
| `gameNodeId` | `string` | Game �ڵ��ȶ��߼���ݣ�ȡֵ����ע����е� `nodeId` |
| `routeState` | `uint16` | Gate �ӽ��µĿ�·��״̬��ȡֵ�� `GameRouteState` |
| `innerNetworkEndpoint` | `Endpoint` | Gate ��� Game �����ڲ�����ʹ�õ�Ŀ���ַ |
| `capabilityTags` | `string[]` | ����ע����Ϣ��������ǩ�����ں������������˺�ѡĿ�� |
| `load` | `LoadSnapshot` | ���һ������ GM �ĸ��ؿ��� |
| `routeEpoch` | `uint64` | ����Ŀ����� Gate ����Ŀ¼�汾 |
| `updatedAtUnixMs` | `uint64` | Gate ���һ��Ӧ�ø���Ŀ���µ�ʱ�� |

`GameDirectoryEntry` ���� GM ά����ע�����������Ϣ���� Gate Ϊ�»Ự��·��ѡ��ʱ�ĺ�ѡĿ¼��ÿ��Ŀ¼��Ŀ����Ӧͬһ����������һ����ֱ���� Game �ڵ㡣���Ƕ��������ڵ�����ʱ���棬��Ӧ�������ȶ�ҵ��ʵ��־û���

**Gate �ر�����**
1. `sessionId -> SessionRecord` �� Gate ����������Ȩ���洢���κλỰ���Ҷ�Ӧ�����иñ��
2. `playerId -> sessionId[]` �ǰ���һز�Ự�Ĵμ�����������ģ�Ͳ�������һ�Զ࣬����ѵ��˵�¼����Ӳ����������ṹ��
3. `gameNodeId -> sessionId[]` �ǰ�Ŀ�� Game �ڵ�ز�Ự�ķ������������� Game �ڵ����ߡ�����ʧЧ����������ʱ���ٶ�λ��Ӱ��Ự��
4. `gameNodeId -> GameDirectoryEntry` �� Gate �ı���·��Ŀ¼�����ں�ѡ Game �ڵ��ѯ��汾�ȶԡ�
5. ���� `M5-08` ��ʵ��·��ʵ�֣�Gate �����ڱ��ļ�ģ��֮������ `playerId -> proxy owner` ���ද̬���������ڽ�����Ǩ��ʵ�嵱ǰ���� Game����������������� `SessionRecord` �� `GameDirectoryEntry` �Ļ������塣

**·�ɰ���ʧЧ����**
1. Gate �ڽ��ܿͻ������Ӳ������߼��Ự��Ӧ��д�� `SessionRecord`����ʼ״̬Ϊ `sessionState = Created`��`routeState = Unassigned`��`playerId = 0`��
2. ���Ự�����Ȩ���˺ż������װ�ؽ׶�ʱ��Ӧ�л��� `sessionState = Authenticating`����Ȩʧ�ܿ�ֱ�ӽ���ر����̣������ȷ���·�ɡ�
3. ͬһ���������� Gate �� Game ����ȫ�������ˡ�ÿ�� Gate ��ӦΪ·��Ŀ¼�е�ÿ�� Game �ڵ�ά��ֱ���ڲ����ӣ�Game �ڵ�֮�䡢Gate �ڵ�֮�䲻ֱ��ͨ�š�
4. ֻ�� `GameRouteState = Available` �ҵ�ǰ Gate �ѽ���ֱ���ڲ����ӵ�Ŀ¼��Ŀ�ɱ������µĻỰ�󶨡�Gate ѡ��Ŀ���Ӧ�������� `RouteTarget` �뵱ǰ `routeEpoch` д�� `SessionRecord`������ٽ��� `routeState = Bound`��
5. ��ǰ�׶�Ĭ�ϲ��á�ͬһ�Ự���������ڹ̶��󶨵�һ����� Game �ڵ㡱�Ĵ���ģ�͡�һ�� `routeState = Bound`��ͬһ `sessionId` ���þ�Ĭ�л����µ�Ŀ�� Game �ڵ㣻������Ϊ�»Ự�����µ� `sessionId` ����ѡ·����Ǩ��ʵ��� owner �仯��Ӧ����дΪ `SessionRecord` �ľ�Ĭ����Ǩ�ƣ���Ӧͨ�� `Proxy` �����㵥�������
6. `gameNodeId` ����ȶ��߼���ݡ�����ͬһ `NodeID` ��Ӧ�Ľ��̷����������·�ؽ���Gate ҲӦͨ�� GM �·���Ŀ¼״̬�������ڲ�����״̬����ȷ�ϸ���Ŀ�Ƿ���Ȼ���ã�����������������Լ�š�
7. �� GM ��ĳ�� Game �ڵ���Ϊ `Draining` ʱ��Gate Ӧֹͣ��������»Ự����������� `Bound` �Ự����ʹ�ã��� GM ���Ϊ `Unavailable`���� Gate ��⵽��� Game �ڵ���ڲ����ӡ�Ŀ¼״̬������ȨУ��ʧЧʱ��Ӧ����ػỰת�� `RouteLost`��
8. `RouteLost` ֻ��ԭ��Ŀ���Ѿ����ܼ����нӵ�ǰ�Ự�����������Զ�Ǩ�ƻ��Զ��������塣���������� Gate �Ự�Ĵ����ʧЧ����ֱ�ӵȼ��ڿ�Ǩ��ʵ����� owner �л��������Ƿ�Ͽ��ͻ��ˡ��Ƿ�ȴ��ָ����Ƿ�����ʽ������Ӧ�ɺ�����̱���Ŀ�������塣
9. �Ự�ر�ʱ��Ӧ�Ȱ� `sessionState` ��Ϊ `Closing`��ִ�з��������������Ҫ֪ͨ����תΪ `Closed`/`Released`������˳����뱣֤�����������ҵ� `playerId` �� `gameNodeId` ����ӳ�䡣

**�� Gate?Game ��װ�Ĺ�ϵ**
1. `RelayEnvelopeHeader.sessionId` �� `RelayEnvelopeHeader.playerId` �ֱ��� `SessionRecord.sessionId` �� `SessionRecord.playerId` ���壻Game �������з����µĻỰ��ʶ��
2. Gate?Game �м̷�װ��ֱ��Я�������� `RouteTarget`��Ŀ�� Game �ڵ��� Gate ��Ŀ��ڵ�֮���ֱ���ڲ�������������� Gate ��ת��ǰ��������֤ `SessionRecord.routeTarget` ��Ȼ��Ч��
3. Game �ڴ��� `Relay.ForwardToGame` ʱ��Ӧȷ��Ŀ��Ự�����ҵ�ǰ�Ự����Ȩ���ڱ��ڵ㡣��Ự�����ڣ�Ӧ���� `3101 Relay.SessionNotFound`����·�ɻ�󶨹�ϵ�뵱ǰ Game �ڵ㲻ƥ�䣬Ӧ���� `3102 Relay.RouteOwnershipMismatch`��
4. ����������Ự�رա���Ұ󶨡�·��ʧЧ���ڲ�֪ͨ��Ϣ������Ϣ���ֶ�Ӧֱ�Ӹ��ñ��ļ������ `sessionId`��`playerId`��`gameNodeId` �� `routeEpoch` ���壬�����ٴ���һ��ƽ��������

**�Ժ�����Ŀ��Լ��**
1. `M4-04` �� Gate �Ự�����ʵ��Ӧ�� `sessionId -> SessionRecord` ΪȨ��ģ�ͣ���Ӧ�����Ӷ����϶���ά�����Զ����ƽ��״̬��
2. `M4-05` Ӧʵ�֡�ÿ�� Gate ��ͬ��ȫ�� Game �ڵ㡱��ֱ������ģ�ͣ������Ǿ��� Gate?Gate �� Game?Game ������ת����
3. `M4-10` �Ķ��������� Game ֪ͨӦ�������� `gameNodeId -> sessionId[]` ����������ȷ������ Game �ڵ�ʧЧʱ����������λ��Ӱ��Ự��
4. `M4-11` �Ĺ̶��󶨲���ֻӦ���塰���ѡ���ѡ Game �ڵ㡱������Ӧ�ı� `RouteTarget` �� `RouteState` �Ļ������塣
5. `M4-12` �ĸ��ظ�֪������Զ�ȡ `GameDirectoryEntry.load`����������û����ģ����չ������°��Ѱ󶨻Ự�͵�Ǩ�Ƶ����� Game �ڵ㡣
6. `M5-08` ��ʵ��·��Ӧ������ `sessionId -> playerId` ������·֮�ϣ�����չ�� `PlayerEntity Proxy -> SpaceEntity Mailbox / ServerStubEntity Mailbox`�������ƹ��Ựģ��ֱ�Ӱ�ҵ��ʵ��������Ӿ����ʵ���յ�ķ��ࡢ���� ownership��Ǩ��������ִ��ģ��Լ���� `docs/DISTRIBUTED_ENTITY.md`��

