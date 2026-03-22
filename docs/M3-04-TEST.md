# M3-04 ���Լ�¼

## ��Χ

- ��֧��`M3-04`
- �����ύ��
  - `d086807`��`[developer] M3-04 implement GM register request handling and response`
- ������ݣ�`docs/M3-04.md`
- ��֤Ŀ�꣺ȷ�� GM �������ѽӹ� `Inner.ProcessRegister`���ܹ������¼�ѭ�������ע��������롢ע���д�롢����ӳ������Ӧ���ͣ������� `docs/PROCESS_INNER.md` / `docs/ERROR_CODE.md` Լ����Э����������塣

## ���Խ��

- �������ͨ����`M2-12`��`M3-03` ����ɣ�����ǰ `M3-04` ״̬Ϊ `������`
- Native ȫ����֤��ִ�в�ͨ����
  - `cmake -S . -B build -DXS_BUILD_TESTS=ON`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`
  - �����`15/15` ����ͨ��
- ���ο����ύδ���� `XServerByAI.Managed.sln`��`src/managed/` ������ .NET �����ļ������δִ�� managed build
- �Զ������Ը���ͨ����
  - `xs_node_gm_register_tests` �˵�����֤ `Game` ע��ɹ���д��ע����`Gate` / `Game` ��ͬһ GM �����˿�������ע��ɹ����ɹ���Ӧ�� `msgId` / `flags` / `seq` ���ԡ��ظ� `nodeId` ���� `3001`���Ƿ� `processType` ���� `3000`���Ƿ� `innerNetworkEndpoint` ���� `3002`�������ֶηǷ����� `3005`���Լ�������������Ⱦע����Ҳ��������Ӧ
  - `xs_node_gm_node_tests` ����ͨ����˵�� `InnerNetwork` �ļ����������ڡ�·����Ϣ�������¼�ѭ��������û�б�����Ŀ�ع��ƻ�
  - `xs_net_register_codec_tests` ������ native ��������ͨ����˵������Ŀ��ע����Ӧ����������·���Ľ���û���ƻ����б���������ģ��
- ���������һ���Լ��ͨ����
  - `InnerNetwork` �ѱ�¶�ȶ��ġ��յ�·����Ϣ���ص��롰�� routingId �ذ����������Ա���ֻ���� ROUTER ��������
  - `GmNode` ���ӹ� `msgId = 1000` �� `Response` / `Error` ��־δ��λ��`seq != 0` ��ע�������޷��γ��ȶ�Э�������ĵ��𻵰���¼��־����
  - ע��ɹ�ʱ��� `routingId`��`lastHeartbeatAtUnixMs`��`innerNetworkReady = false` д�� `ProcessRegistry`��������Ĭ�� `5000ms / 15000ms` ���������� `serverNowUnixMs`
  - ע��ʧ��ʱ���������ĵ�һ�£�`3000 Inner.ProcessTypeInvalid`��`3001 Inner.NodeIdConflict`��`3002 Inner.InnerNetworkEndpointInvalid`��`3005 Inner.RequestInvalid`

## ���½���

- `M3-04` ���ֲ���ͨ����
- `docs/DEVELOPMENT_PLAN.md` �ѽ� `M3-04` �� `������` ����Ϊ `�����`��
- ���� `M3-05`��`M3-06`��`M3-12` ����ֱ�Ӹ��ñ���Ŀ�γɵ�ע���д������������Ӧ�����������·��Ӧ������

