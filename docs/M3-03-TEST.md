# M3-03 ���Լ�¼

## ��Χ

- ��֧��`M3-03`
- �����ύ��
  - `7677ae0`��`developer: add gm process registry`
- ������ݣ�`docs/M3-03.md`
- ��֤Ŀ�꣺ȷ�� `ProcessRegistry` ����Ϊ���� GM ע���ģ����أ��߱��ȶ�����Ŀ�ṹ���� `nodeId` / `routingId` ��ѯ���Ƴ�������/���ظ��¡�`innerNetworkReady` ���£��Լ�ȷ���ԵĻ���յ���������

## ���Խ��

- �������ͨ����`M1-09` ����ɣ�����ǰ `M3-03` ״̬Ϊ `������`
- Native ȫ����֤��ִ�в�ͨ����
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - �����`14/14` ����ͨ��
- ���ο����ύδ���� `XServerByAI.Managed.sln`��`src/managed/` ������ .NET �����ļ������δִ�� managed build
- �Զ������Ը���ͨ����
  - `xs_node_process_registry_tests` ��֤ Gate/Game ע��ɹ���`nodeId` / `routingId` ��ͻ������������·������ѯ���Ƴ����������ա������븺�ظ��¡�`innerNetworkReady` ���¡������������ձ����
  - `xs_net_register_codec_tests` �� `xs_net_heartbeat_codec_tests` ����ͨ����˵�����ζԿ����湲��ṹ���������δ����ع�
- ���������һ���Լ��ͨ����
  - `ProcessRegistryEntry` �Ѱ��� `processType`��`nodeId`��`pid`��`startedAtUnixMs`��`innerNetworkEndpoint`��`buildVersion`��`capabilityTags`��`load`��`routingId`��`lastHeartbeatAtUnixMs`��`innerNetworkReady`
  - `ProcessRegistry` ʹ�� `nodeId` ��Ϊ��������ʹ�� `routingId` ��Ϊ�������������� `processType`��`nodeId`��`innerNetworkEndpoint.host`��`innerNetworkEndpoint.port` ִ����С����У��
  - `Snapshot()` ͨ���������������ȶ��� `nodeId` �����������Ϻ���·��Ŀ¼�� ready �ۺϵĸ���Ԥ��
- ����Ŀֻ�������ݽṹ��ӿڲ�������û��ֱ�ӽ��� ZeroMQ ��Ϣ�շ�����һ���� `docs/M3-03.md` �ķ�ΧԼ��һ�£����δ����ִ�п�ִ�г��� smoke ����

## ���½���

- `M3-03` ���ֲ���ͨ����
- `docs/DEVELOPMENT_PLAN.md` �ѽ� `M3-03` �� `������` ����Ϊ `�����`��
- ���� `M3-04`��`M3-05` ����ֱ�ӽ�ע����������������ӳ�䵽��ע���ӿڣ���������д�������塣
