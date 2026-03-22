# PROCESS_INNER

���ĵ����� XServerByAI ��ǰ�׶� GM �� Gate/Game ֮�䡰����ע�ᡱ����������ServerStubEntity ownership ���䡱��Game ��������ϱ�����Game ·��Ŀ¼��ѯ / ͬ�����롰��Ⱥ����֪ͨ����������Ϣ�Ľṹ���ֶ�������Ĭ��ʱ�򡣵�ǰĬ�Ͽ����洫������� ZeroMQ over TCP ��·�ϣ���Ϣ����״̬Լ���Ա���Ϊ���ߡ�

**���÷�Χ**
1. ��ǰ���� `Inner.ProcessRegister`��`msgId = 1000`����`Inner.ProcessHeartbeat`��`msgId = 1100`����`Inner.ClusterReadyNotify`��`msgId = 1201`����`Inner.ServerStubOwnershipSync`��`msgId = 1202`����`Inner.GameServiceReadyReport`��`msgId = 1203`����`Inner.GameDirectoryQuery`��`msgId = 1204`���� `Inner.GameDirectoryNotify`��`msgId = 1205`��������Ϣ��
2. ����-��Ӧ����Ϣ����ע�ᡢ������ `Inner.GameDirectoryQuery`����Ӧ����Ϊ `GM -> Gate/Game`��������ԭʼ `msgId` �� `PacketHeader.flags.Response`��
3. ������Ϣ���� `Inner.ClusterReadyNotify`��`Inner.ServerStubOwnershipSync`��`Inner.GameServiceReadyReport` �� `Inner.GameDirectoryNotify`��
4. ��ǰ�׶β���������������·���Ϣ�����нڵ�ӽ������ʱ�����ͳһ�����ļ���ȡ������Ⱥ�����Լ�����ʵ�����á�
5. ʧ����Ӧͨ�� `PacketHeader.flags.Error` �� `docs/ERROR_CODE.md` �еǼǵĿ�����������
6. ��ǰĬ��ÿ����������̶� `1` �� GM����������� `N` �� Gate �ڵ��� `M` �� Game �ڵ㣬���� `N >= 1`��`M >= 1`��

**�������Լ��**
1. ��Ϣ���е�����ͳһʹ�������ֽ��򣨴�ˣ����롣
2. `string` ʹ�� `uint16 byteLength + UTF-8 bytes` ���룬�����ַ������鲻���� `1024` �ֽڡ�
3. `string[]` ʹ�� `uint16 count + repeated string` ���룬�������齨�鲻���� `32` �
4. �ṹ������ʹ�� `uint16 count + repeated <entry>` ���룬���Ǿ�����Ϣ����˵�������򵥸����齨�鲻���� `256` �
5. `bool` ʹ�� `uint8` ��ʾ��`0` Ϊ `false`��`1` Ϊ `true`��
6. ����ʱ����ֶ�ͳһʹ�� `unixTimeMsUtc` ���塣
7. `PacketHeader.seq` Ӧʹ�÷���ֵ������������Ӧ����Ӧ������������ `seq`��
8. ��ǰδ����ı����ֶλ���λ����Ϊ `0`��

**ö�٣�ProcessType**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Gate` | �ͻ��������·��ת������ |
| `2` | `Game` | ҵ���߼���״̬���ؽ��� |

��ǰ�׶�ֻ���� `Gate` �� `Game` �� GM ����ע�ᡢ������Ŀ¼��ѯ�� ready �ϱ���Ϣ������ȡֵ���� `3000 Inner.ProcessTypeInvalid`��

**ö�٣�GameRouteState**

| Value | Name | Description |
| --- | --- | --- |
| `1` | `Available` | �ɽ����µĻỰ�� |
| `2` | `Draining` | ���ٽ����»Ự����������лỰ�������� |
| `3` | `Unavailable` | ���ɱ�ѡ��ΪĿ��·�� |

��ö���� `docs/SESSION_ROUTING.md` �е� `GameRouteState` ���屣��һ�¡�

**������NodeID**
1. `nodeId` ��ʾ Gate/Game ��������������ڵ��ȶ��߼���ݡ�
2. `nodeId` ʹ�����ִ�Сд�� `<ProcessType><index>` ��ʽ������ `Gate0`��`Gate1`��`Game0`��`Game1`��
3. ͬһ���������� `nodeId` ����Ψһ�����������Ӧ����ԭ `nodeId`��
4. `GM` ������ `nodeId` �����ϵ��ÿ����������̶� `1` �� GM��
5. ��ǰ�׶� GM �ԡ��ȶ� `nodeId` + ��ǰ�������·��ʶ��һ�����߽ڵ㣬���ٶ�����䵥���Ļ��Լ�š�

**����ṹ��Endpoint**

| Field | Type | Description |
| --- | --- | --- |
| `host` | `string` | ���ⷢ���ļ�����ַ������ʹ�� IP �������������ɽ��������� |
| `port` | `uint16` | ���ⷢ���ļ����˿ڣ�`0` ��Ϊ��Ч |

`innerNetworkEndpoint` ������ GM �����ý��̺���Ҫ������������ѵķ�����ڡ��� `Game` ��˵������ʾ Gate ��Ҫ���ӵ� ZeroMQ over TCP �����ַ���� `Gate` ��˵������ʾ��ǰ Gate ��������Ⱥ�ķ�����ڣ�������;���ں�����Ŀ��ϸ����

**����ṹ��LoadSnapshot**

| Field | Type | Description |
| --- | --- | --- |
| `connectionCount` | `uint32` | ��ǰ�������������ڲ�����������δ֪ʱ�� `0` |
| `sessionCount` | `uint32` | ��ǰ�Ự����������ʱ�� `0` |
| `entityCount` | `uint32` | ��ǰʵ������������ʱ�� `0` |
| `spaceCount` | `uint32` | ��ǰ��������������ʱ�� `0` |
| `loadScore` | `uint32` | ��һ�����ط�ֵ�����鷶Χ `0-10000`��δ֪ʱ�� `0` |

�ýṹ��Ϊ `M3-14` ֮ǰ����С���ؿ��գ�ռλ�׶�����ȫ `0` �ϱ���

**����ṹ��ServerStubOwnershipEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub ʵ�����ͣ����� `MatchService`��`ChatService` |
| `entityKey` | `string` | ͬһ�����µ��ȶ�ʵ����ʶ |
| `ownerGameNodeId` | `string` | ��ǰ��������ظ� Stub �� `Game` �ڵ� |
| `entryFlags` | `uint32` | ��ǰ��������ͷ������� `0` |

`(entityType, entityKey)` ��ͬ���һ�� `ServerStubEntity` ʵ�����ȶ���ݡ�

**����ṹ��ServerStubReadyEntry**

| Field | Type | Description |
| --- | --- | --- |
| `entityType` | `string` | Stub ʵ������ |
| `entityKey` | `string` | ͬһ�����µ��ȶ�ʵ����ʶ |
| `ready` | `bool` | �� Stub ��ǰ�Ƿ��Ѿ� ready |
| `entryFlags` | `uint32` | ��ǰ��������ͷ������� `0` |

**����ṹ��GameDirectorySnapshotEntry**

| Field | Type | Description |
| --- | --- | --- |
| `gameNodeId` | `string` | `Game` �ڵ��ȶ��߼���� |
| `routeState` | `uint16` | ·��״̬��ȡֵ�� `GameRouteState` |
| `innerNetworkEndpoint` | `Endpoint` | Gate ��� `Game` �����ڲ�����ʹ�õ�Ŀ���ַ |
| `capabilityTags` | `string[]` | ����ע����Ϣ��������ǩ |
| `load` | `LoadSnapshot` | ���һ������ GM �ĸ��ؿ��� |
| `updatedAtUnixMs` | `uint64` | ��Ŀ¼��Ŀ���һ�α� GM ˢ�µ�ʱ�� |

�� Gate ���ýṹ���Ϊ���� `GameDirectoryEntry` ʱ��Ӧ�������Ϣ��� `routeEpoch` д��ÿ��������Ŀ�� `routeEpoch` �ֶΡ�

**Inner.ProcessRegister��`msgId = 1000`��**
1. ����ʱ����`Gate`/`Game` �� GM �� ZeroMQ over TCP ������·���ú�Ӧ�ȷ���ע�������ٷ����κ���������������
2. �ɹ����壺GM ���ܸýڵ��Ϊһ���µĻע�ᣬ�����ص�ǰ��������������������ʱ�䡣
3. ʧ�����壺GM �ܾ���ǰע�ᣬ���÷��ɸ��� `errorCode` �� `retryAfterMs` �����Ƿ����ԡ�

ע�������壺

| Field | Type | Description |
| --- | --- | --- |
| `processType` | `uint16` | �������ͣ�ȡֵ�� `ProcessType` |
| `processFlags` | `uint16` | ��ǰ��������ͷ������� `0` |
| `nodeId` | `string` | ������������������ڵ��ȶ��߼���ʶ |
| `pid` | `uint32` | ���ز���ϵͳ���̺ţ�������������չʾ |
| `startedAtUnixMs` | `uint64` | �������ʱ�䣬���������ڵ�����ʶ�� |
| `innerNetworkEndpoint` | `Endpoint` | �ý��̶��ⷢ���ķ�����ڣ�����Ϊ�� |
| `buildVersion` | `string` | �ɶ������汾�� Git �����ַ��� |
| `capabilityTags` | `string[]` | ������ǩ�б��û��ʱ�������� |
| `load` | `LoadSnapshot` | ��ʼ���ؿ��գ�δ֪ʱ����ȫ `0` |

ע��ɹ���Ӧ�壺

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | �Ƽ��������ͼ���Ĭ�� `5000` |
| `heartbeatTimeoutMs` | `uint32` | GM �ж���ʱ����ֵ��Ĭ�� `15000` |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��������ڵ�����ʱ�Ӷ��� |

ע��ʧ����Ӧ�壨`Response + Error`����

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | ʧ��ԭ��ȡֵ�� `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | �������Եȴ�ʱ�䣻`0` ��ʾ���ṩ���� |

**Inner.ProcessHeartbeat��`msgId = 1100`��**
1. ����ǰ�᣺ֻ�����յ�ע��ɹ���Ӧ�󣬷��ͷ��ſɷ���������
2. �ɹ����壺GM ȷ�ϵ�ǰ�ڵ������п�����·�ϵĻע����Ȼ��Ч����������Ӧ�е�����������������
3. ʧ�����壺GM ��Ϊ��ǰ������·���ٶ�Ӧ��ڵ㣬���ͷ���Ҫ���ݴ������ж��Ƿ�����ע�ᡣ

���������壺

| Field | Type | Description |
| --- | --- | --- |
| `sentAtUnixMs` | `uint64` | ���ͷ����͸�����ʱ��ʱ��� |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `load` | `LoadSnapshot` | ���¸��ؿ��գ�δ֪ʱ����ȫ `0` |

�����ɹ���Ӧ�壺

| Field | Type | Description |
| --- | --- | --- |
| `heartbeatIntervalMs` | `uint32` | GM �Ƽ�����һ��������� |
| `heartbeatTimeoutMs` | `uint32` | ��ǰ��·ʹ�õĳ�ʱ��ֵ |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��� |

����ʧ����Ӧ�壨`Response + Error`����

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | ʧ��ԭ�򣬳���ֵΪ `3003`��`3004`�������ʽ�Ƿ�ʱ�ɷ��� `3005` |
| `retryAfterMs` | `uint32` | �������Եȴ�ʱ�䣻Ҫ��������ע��ʱ��Ϊ `0` |
| `requireFullRegister` | `bool` | `1` ��ʾ���ͷ���������������ע�����̣��������ʽ�Ƿ�ʱ��Ϊ `0` |

**Inner.ClusterReadyNotify��`msgId = 1201`��**
1. ����ʱ����GM �ڼ�Ⱥ ready �ۺϽ�������仯ʱ�� Gate �·���������Ҫ��Ҳ������ Game �㲥ͬһ״̬�����������롣
2. �������壺`clusterReady = true` ��ʾ GM ��ǰȷ�ϸ÷��������Ѿ�����������ǰ�᣻�� Gate ��˵����ֱ��Ӱ��ͻ�������Ƿ�����򿪡�
3. �ݵ����壺���շ�ֻ��Ҫ�������� `readyEpoch` �Ľ������֪ͨ�ظ�����ʱӦ���ԡ�

��Ⱥ����֪ͨ�壺

| Field | Type | Description |
| --- | --- | --- |
| `readyEpoch` | `uint64` | ��Ⱥ����״̬�汾�ţ�GM ÿ�������½���ʱ��������� |
| `clusterReady` | `bool` | ��Ⱥ�Ƿ��� ready |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��� |

**Inner.ServerStubOwnershipSync��`msgId = 1202`��**
1. ����ʱ����GM ������������ `Game` �ڵ�ע����ɲ���ɱ��� ownership �������� `Game` �·���ǰ `ServerStubEntity` ownership ȫ�����ա�
2. �������壺����Ϣ��ﵱǰ�������������� `ServerStubEntity -> OwnerGameNodeId` ������ӳ�䣬���������� patch��
3. �ݵ����壺���շ�ֻ�������� `assignmentEpoch` �Ľ�����ɿ����ظ�����ʱӦ���ԡ�

ownership ͬ���壺

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | ownership ���հ汾�ţ�GM ÿ�����¼������� |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `assignments` | `ServerStubOwnershipEntry[]` | ��ǰ����������ȫ�� `ServerStubEntity` �� ownership ���� |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��� |

**Inner.GameServiceReadyReport��`msgId = 1203`��**
1. ����ʱ����`Game` �ڱ��� assigned `ServerStubEntity` ��ʼ����ɺ��ͣ��籾�� ready ���ۻ�ĳ�� assigned Stub �� ready ״̬�����仯��Ҳ������ͬһ `assignmentEpoch` ���ظ��ϱ���
2. �������壺`localReady = true` ��ʾ�� `Game` �ڵ�ǰ `assignmentEpoch` �±�������ص� Stub ��ȫ�� ready��
3. �ݵ����壺GM Ӧ�� `nodeId + assignmentEpoch` �ۺϣ��� `assignmentEpoch` ���ϱ����ø��ǽ��µ� ownership ���ۡ�

���ط��� ready �ϱ��壺

| Field | Type | Description |
| --- | --- | --- |
| `assignmentEpoch` | `uint64` | �� ready ���������ڵ� ownership ���հ汾 |
| `localReady` | `bool` | ���� assigned `ServerStubEntity` �Ƿ���ȫ�� ready |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `entries` | `ServerStubReadyEntry[]` | ��ǰ `Game` ��������ص� Stub ready ���� |
| `reportedAtUnixMs` | `uint64` | ���ͷ����ɸý��۵�ʱ�� |

**Inner.GameDirectoryQuery��`msgId = 1204`��**
1. ����ʱ����`Gate` ��ע���������ջ��������ͣ����ڻ�ȡ��ǰ `Game` ·��Ŀ¼ȫ�����գ�����ͬʱ��������仯���ġ�
2. �ɹ����壺GM ���ص�ǰ `routeEpoch` ��ȫ��Ŀ¼���գ���� `subscriptionAccepted = true`������Ŀ¼�仯���� `Inner.GameDirectoryNotify (1205)` ���͡�
3. ʧ�����壺GM �ܾ�����Ŀ¼��ѯ��������`Gate` �ɸ��ݴ������� `retryAfterMs` �����Ƿ����ԡ�

Ŀ¼��ѯ�����壺

| Field | Type | Description |
| --- | --- | --- |
| `knownRouteEpoch` | `uint64` | `Gate` ��ǰ��Ӧ�õ�Ŀ¼�汾���״β�ѯʱ�� `0` |
| `subscribeUpdates` | `bool` | `1` ��ʾϣ��������������Ŀ¼�仯���� |
| `queryFlags` | `uint32` | ��ǰ��������ͷ������� `0` |

Ŀ¼��ѯ�ɹ���Ӧ�壺

| Field | Type | Description |
| --- | --- | --- |
| `routeEpoch` | `uint64` | GM ��ǰĿ¼�汾�� |
| `subscriptionAccepted` | `bool` | GM �Ƿ���ܸ� `Gate` �ĺ���Ŀ¼�仯���� |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `entries` | `GameDirectorySnapshotEntry[]` | ��ǰ `Game` ·��Ŀ¼ȫ������ |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��� |

Ŀ¼��ѯʧ����Ӧ�壨`Response + Error`����

| Field | Type | Description |
| --- | --- | --- |
| `errorCode` | `int32` | ʧ��ԭ��ȡֵ�� `docs/ERROR_CODE.md` |
| `retryAfterMs` | `uint32` | �������Եȴ�ʱ�䣻`0` ��ʾ���ṩ���� |

**Inner.GameDirectoryNotify��`msgId = 1205`��**
1. ����ʱ����GM ��ĳ���Ѷ��� `Gate` ��Ҫ���� `Game` ·��Ŀ¼�� `routeEpoch` �����仯ʱ���͡�
2. �������壺����Ϣ���͵����µ�ȫ��Ŀ¼���գ����������� patch��
3. �ݵ����壺`Gate` ֻ�������� `routeEpoch` �Ľ�����ɿ����ظ�����ʱӦ���ԡ�

Ŀ¼�仯֪ͨ�壺

| Field | Type | Description |
| --- | --- | --- |
| `routeEpoch` | `uint64` | �µ�Ŀ¼�汾�� |
| `statusFlags` | `uint32` | ��ǰ��������ͷ������� `0` |
| `entries` | `GameDirectorySnapshotEntry[]` | �µ�ȫ�� `Game` ·��Ŀ¼���� |
| `serverNowUnixMs` | `uint64` | GM ��ǰʱ��� |

**ʱ����У�����**
1. ZeroMQ over TCP ������·���ú󣬷��ͷ������ȷ���һ�� `Inner.ProcessRegister`��ע��ɹ�ǰ���÷���������Ŀ¼��ѯ�򱾵� ready �ϱ���
2. `nodeId` ��ʾ�ȶ��߼���ݣ�GM Ӧ�ܾ�ͬһʱ���ظ��Ļ `nodeId` ע�ᣬ������ `3001 Inner.NodeIdConflict`��
3. ��ǰ�׶�һ�γɹ�ע���� `nodeId` �󶨵���ǰ������·������������Ŀ¼��ѯ�� ready �ϱ�Ĭ����������·��λ��ڵ㣬���ٶ��ⷢ�Ŷ�����Լ�ֶΡ�
4. Ĭ���������Ϊ `5000ms`��Ĭ�ϳ�ʱ��ֵΪ `15000ms`��GM ��������Ӧ�и��ǣ������뱣֤ `heartbeatIntervalMs < heartbeatTimeoutMs`��
5. ��������δ֪�ڵ��δ���ע��Ŀ�����·ʱ���� `3003 Inner.NodeNotRegistered`�����������ѱ��滻��ʧЧ����ӵ�и� `nodeId` �Ŀ�����·ʱ���� `3004 Inner.ChannelInvalid`��
6. `innerNetworkEndpoint.port = 0`��`innerNetworkEndpoint.host` Ϊ�ջ� `processType` ���Ϸ�ʱ��GM Ӧ�ܾ�ע�ᣬ������ `3000` �� `3002`��
7. `load` ���޷��ṩ������һ���� `0`����ֹʹ�ø�ֵ��δ��ʼ���ڴ��δ֪����
8. `Inner.ClusterReadyNotify.statusFlags`��`Inner.ServerStubOwnershipSync.statusFlags`��`Inner.GameServiceReadyReport.statusFlags`��`Inner.GameDirectoryQuery.queryFlags`��`Inner.GameDirectoryQuery response.statusFlags` �� `Inner.GameDirectoryNotify.statusFlags` ��ǰ������Ϊ `0`��
9. `ServerStubOwnershipEntry.entryFlags` �� `ServerStubReadyEntry.entryFlags` ��ǰ����Ϊ `0`��`ownerGameNodeId` ����ָ��ǰ����������һ���Ϸ��� `Game` `nodeId`��
10. `GameDirectorySnapshotEntry.routeState` �����ǺϷ��� `GameRouteState` ö��ֵ��Gate �ڱ�������Ŀ¼��Ŀʱ��Ӧ����� `routeEpoch` д��ÿ��������Ŀ�� `routeEpoch` �ֶΡ�
11. `Game` ������Ծɵ� `assignmentEpoch` ownership ���գ�GM Ҳ������Ծɵ� `assignmentEpoch` ready �ϱ���`Gate` ������Ծɵ� `routeEpoch` Ŀ¼���ա�
12. ��ǰ�׶β����ⶨ����ʽע����Ϣ���������߿���ͨ�����ӹرձ��������貹�䵥����ע����Ϣ�����ڿ�����Ŷ��еǼ��� `msgId`��

