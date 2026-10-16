# Illustrator Table Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. ���ƻ��Ƽ��ڵ�ǰ�Ի�ֱ��ʵʩ���д��û����Ĳ�ѡ��ִ�з�ʽ��

**Goal:** Ϊ Windows x64 Illustrator 28.5 �������������뽻����֤��ԭ����������������ͨ������ GitHub �ֿ�� Actions �������ɰ�װ����

**Architecture:** C++ ������ĸ��𼸺κͽṹ��Illustrator SDK ���为�𹤾��¼�����ͨ�����顢ԭ�����ֿ��ĵ��ֵ䡣������������ֻ��ʾѡ�����϶�Ԥ�����ɹ��ύ�ı༭ͳһ���� Illustrator ����ջ��

**Tech Stack:** C++17��Adobe Illustrator 2024 Windows C++ SDK��CMake��GitHub Actions `windows-2022`���������汾������Դ������ȡ�õ� 2024 SDK �� Windows ��������Ҫ��Ϊ׼��

**Spec:** `docs/superpowers/specs/2026-10-16-illustrator-table-tool-design.md`

## Global Constraints

- Windows 64 λ Adobe Illustrator 2024���汾 28.5��
- �װ����� 1�C100 �С�1�C50 �У��ܸ��������� 2,000��
- ���гߴ����Ϊ��������С�иߺ��п�Ϊ 2 ���ס�
- �װ汣֤δ��ת��δ��б�ı���ɱ༭��
- �װ����������Ű���ö������롣
- �û�����ʹ�� GitHub �ƶ˱��룬����ȷҪ���½������ֿ⡣
- SDK ���ύ�����ֿ⡣
- û�� SDK ʱ���������Ĳ��Գɹ���ԭ������������ȷʧ�ܲ�˵��ȱʧ������ϴ�ռλ `.aip`��
- �ƶ˹���������� Illustrator 28.5 �������غͽ������ա�
- δ����ʵ�����²� SDK ����ǩ��������׮�ӿڻ������ҳ����ԭ�������Ѿ���ɡ�

## Review Focus

1. �������뷨�������ֽ׶Σ��л�����ѡ�񲻵ýػ������ʧδ�ύ���֣��� Task 3 ʵ�⡣
2. �ϲ�������ɾ�����������У��ṹ�������������ֱ������� Task 4 �Զ����ԡ�
3. �û���ԭ�����ֹ��߸��ֺ��϶����ߣ����ñ��������ݸ��ǣ��� Task 5 ʵ�⡣
4. �������ƻ��������ź��ٴα༭������ʵ���������������³ߴ�һ�£��� Task 5 ʵ�⡣
5. �������л��ĵ������������ȡ���϶����������һ���ĵ���Ҳ�����°��Ʒ���� Task 3��5 ���ԡ�

## File Structure

```text
CMakeLists.txt                         ���ġ����ԺͲ���������
cmake/IllustratorSdk.cmake              SDK ���ҡ��汾����빹������
include/table/Types.hpp                ���Ρ���ʽ����Ԫ��ʹ�������
include/table/TableModel.hpp           ����ṹ��ߴ�����ӿ�
include/table/Geometry.hpp             ���С�ѡ���빲�����߽ӿ�
include/table/Interaction.hpp          ��ȡ����ѡ�����϶�״̬��
include/table/Serialization.hpp        ģ�Ͱ汾�����л��ӿ�
src/core/TableModel.cpp                �ṹ���ı������ͳߴ�У��
src/core/Geometry.cpp                  ���꼰�ɼ����߼���
src/core/Interaction.cpp               �����������Ľ����߼�
src/core/Serialization.cpp             ���汾���޶�ĳ־�ģ�͸�ʽ
src/plugin/Plugin.cpp                  SDK ��ڡ�suite �������ں���Ϣ·��
src/plugin/ToolController.cpp          �����¼��������״̬�л�
src/plugin/DocumentAdapter.cpp         �ĵ������ֵ䡢���ּ�����
src/plugin/AnnotationRenderer.cpp      ����ѡ����Ԥ���������ʾ
src/plugin/NativeTextEditor.cpp        ԭ�����ֱ༭����
src/plugin/Dialogs.cpp                 ���Ĳ�������ʽ�Ի���
resources/                            �����Դ����꼰�汾��Ϣ
tests/core_tests.cpp                   ���Ŀ�ִ�в������
tests/model_tests.cpp                  �ṹ���ı��ͳ־û�����
tests/geometry_tests.cpp               ���κ����в���
tests/interaction_tests.cpp            �϶���ѡ���ȡ������
scripts/build.ps1                     ���ؼ� Actions ���õı������
scripts/package.ps1                   ������顢�嵥��ժҪ�� ZIP
.github/workflows/build.yml           ���Ĳ����� SDK �������
docs/SDK_SETUP.md                     �ٷ� SDK ��ȡ�빹������
docs/HOST_ACCEPTANCE.md               28.5 ʵ�����Բ��輰��ʵ���
README.md                            ״̬����װ��ʹ�ú�ж��
```

ԭ�� SDK ����Դ��չ������������Դ��;��幤�����ã��� Task 1 ��ȡ�ٷ�������̶������¸��ļ��б�����Ҫ��ǰα�� PiPL ����� ABI��

## Task 1: ȡ�ò���֤ SDK�������ɼ��ص��ƶ�ԭ������

**Files:** `CMakeLists.txt`��`cmake/IllustratorSdk.cmake`��`src/plugin/Plugin.cpp`��`resources/`��`scripts/build.ps1`��`.github/workflows/build.yml`��`docs/SDK_SETUP.md`��`README.md`��

**Interfaces:**
- Consumes: �ٷ� Illustrator 2024 Windows SDK �ı���·�����ƶ�ʹ�� Secret `ILLUSTRATOR_SDK_URL` �� `ILLUSTRATOR_SDK_SHA256`��
- Produces: `scripts/build.ps1 -SdkRoot <path> -Configuration Release`����� `build/plugin/Release/IllustratorTableTool.aip`��CMake ѡ�� `TABLE_BUILD_PLUGIN` Ĭ�� `ON`���رո�ѡ��ֻ�������������Ĳ��ԡ�
- Plugin SDK ��ڡ�suite ��ȡ�ͷź���Դ�����ϸ������Ѻ�ʵ�Ķ�Ӧ�汾������

- [ ] ��¼ SDK ѹ������Դ��SHA-256���汾������ Windows ���߼���������Ҫ�����û���δ��¼�ٷ���ڣ��ȴ��û���ɵ�¼���ṩ�ļ�λ�ã���ʱ��׼���ƻ������������������䡣
- [ ] �������ر�ʱ�����ú��Ĺ��̣���������� SDK ȱʧʱ���ñ���ʧ�ܣ�˵��Ӧ���õ� SDK ·����
- [ ] �Ķ� 2024 SDK ����С������ Annotator ʾ��������ֻ�г�ʼ�����رպͲ��Բ˵��������ʵ `.aip` ���̡�
- [ ] ���س�ʼ�� Git�����ĵ���Դ���ύ�� `main`�����������˻� `a769568933` �´����û���Ȩ�Ĺ����ֿ� `illustrator-table-tool`���������ѱ�ռ���ȼ�����вֿ���������ݣ�������������Ŀ��
- [ ] ���Ĳ�������ͨ�ύ����ȡ�������У���Ҫ SDK Secret �Ĳ���������������ε� `main` �ύ���ֶ�����ʱ���С����� SDK ����֤ SHA-256���ٽ�ѹ������ӡ����ƾ�ݡ�
- [ ] ��ȡ�ƶ˹����������־���޸���������ʵ x64 `.aip`������� PE �ܹ���������ں���Դ�������������ĺ�׺���ɲ��
- [ ] ȡ�ù�������� Illustrator 28.5 �ֶ���ͨ�����õ�ԭ��Ӧ�ù��߼��ء�ȷ�ϲ˵����֡�ִ�в�������ɹ����˳����ޱ�����д����ʵ�����
- [ ] �ύ��`build: add verified Illustrator 2024 native plugin pipeline`��

**Verification:** �ƶ� Windows ����ɹ���������ʵ������ 28.5 ���سɹ������ǽ�������ԭ�����ܿ�����ǰ�ᡣSDK δȡ��ʱ�����񱣳�δ��ɡ�

## Task 2: ����ģ�͡����κ�ԭ���������

**Files:** `include/table/Types.hpp`��`include/table/TableModel.hpp`��`include/table/Geometry.hpp`����Ӧ `src/core/`��`src/plugin/DocumentAdapter.cpp`��`src/plugin/Dialogs.cpp`��`tests/model_tests.cpp`��`tests/geometry_tests.cpp`��

**Interfaces:**
- `using CellId = std::uint64_t;`
- `struct Point { double x; double y; };`��`struct Rect { double left; double top; double right; double bottom; };`���������� x ���ҡ�y ���£��������为�� AI ����ת����
- `TableModel::create(int rows, int columns, double widthPt, double heightPt) -> TableModel`�����Ϸ������׳� `ValidationError`�������ز���ģ�͡�
- `TableModel::cellAt(int row, int column) const -> const Cell&`�����ظ��Ǹ������ê�㵥Ԫ��
- `TableModel::setText(CellId id, const std::u16string& text) -> void`��
- `Geometry::cellBounds(const TableModel&, CellId) -> Rect`��
- `Geometry::visibleEdges(const TableModel&) -> std::vector<Edge>`��ÿ�������߽�һ�Ρ�
- `DocumentAdapter::createTable(const TableModel&, Point topLeft) -> TableRef`��`TableRef` �����������������ĵ����ݣ����ܿ��ĵ����á�

- [ ] ��д������ʧ�ܲ��ԣ�4��4 Ĭ�ϱ���100��20 �Ϸ��� 100��21 �ܾ������С�51 �С�������NaN������Ͳ���ÿ�� 2 ���׵ĳߴ�ȫ���ܾ���
- [ ] ʵ�ֵȷֳߴ硢�ȶ���Ԫ���ź�����ӳ�䡣ʹ�� `72.0 / 25.4` ������ס�
- [ ] ��д������ʧ�ܲ��ԣ�2��2 ������ 12 �����ظ��ĵ���߶Σ��߶�����ƥ�䵥Ԫ�񼸺Σ��ϲ�������ر߲���������
- [ ] ʵ�ִ����ļ��β�ͨ�� `cmake -S . -B build/core -DTABLE_BUILD_PLUGIN=OFF`��`cmake --build build/core --config Release`��`ctest --test-dir build/core -C Release --output-on-failure`��
- [ ] ʵ�������½��Ի����뻭���������ã�������ͨ�顢��ɫ·�����������ߡ����ԭ���������ֿ򡣰��ĵ�ģʽʹ�� RGB/CMYK��
- [ ] ��֤ȡ���½������ĵ���ʧ��ʱɾ���½��Ĳ������飻���� 2��2 �������ԭ�����ֹ���ȷ��ÿ�����ֿɱ༭��
- [ ] �ύ��`feat: create editable native table artwork`��

## Task 3: ����ѡ�񡢱����϶���ԭ�����ֱ༭�ջ�

**Files:** `include/table/Interaction.hpp`��`src/core/Interaction.cpp`��`src/plugin/ToolController.cpp`��`src/plugin/AnnotationRenderer.cpp`��`src/plugin/NativeTextEditor.cpp`��`tests/interaction_tests.cpp`��`docs/HOST_ACCEPTANCE.md`��

**Interfaces:**
- `Geometry::hitTest(const TableModel&, Point localPoint, double zoom, double tolerancePx) -> HitResult`��`HitResult` ��ȷ���ֵ�Ԫ�񡢿ɼ��߶κͱ����ⲿ��
- `Interaction::pointerDown(const TableModel&, Point, bool shift, double zoom) -> void`��
- `Interaction::pointerMove(Point) -> void`��`Interaction::pointerUp() -> std::optional<TableModel>`��`Interaction::cancel() -> void`������ԭģ�͵ĸ�����ȡ������ԭ״̬��
- `NativeTextEditor::beginEdit(TableRef, CellId) -> bool`��ֻ�ڳɹ�������ʵ�������ֱ༭ʱ���� `true`��
- `DocumentAdapter::applyGeometry(TableRef, const TableModel&) -> void`��һ���϶�����ֻ�ύһ�Σ�Ԥ��ֻ�߸������ơ�

- [ ] ʧ�ܲ��Ը��ǣ�����ѡ��Shift ����ѡ����������������ر߲������У�25%��100%��400% ��������ͬ��Ļ�ݲ�����һ�¡�
- [ ] ʧ�ܲ��Ը��ǣ��ڲ�����ֻ�ı������������ܿ����䣻�ⲿ�ұ��߸ı��ܿ�������ͬ������С�ߴ��ȡ��Esc ��ģ����ȫ���䡣
- [ ] ʵ�����к��϶�״̬��������ȫ�����Ĳ��ԡ�
- [ ] ע�Ṥ���¼������߹�ꡢ����������Ԥ�����������ݲ���д���ĵ�·�����ĵ��л������ʧЧʱȡ����ǰ������
- [ ] �� SDK ��ʵ�ķ�������ԭ�����ֱ༭��˫����ʵ��΢��ƴ�����֡���ѡȷ�ϡ����Ļ��С�����ճ������Ӣ�Ļ��䣻��ʹ�������ַ���������������档
- [ ] ʵ���϶�����һ�μ��ص��϶�ǰ״̬��Esc �������������ݣ�ѡ�������� PDF ������
- [ ] ��ԭ��˫�����ֱ༭�޷��ɿ���ɣ���¼��־��ʵ�ʱ��֣��Ƚ�������û��޶���ƣ������롰��������������ɡ���״̬��
- [ ] �ύ��`feat: edit table cells and resize tracks on canvas`��

## Task 4: ���нṹ��ϲ����

**Files:** `src/core/TableModel.cpp`��`src/core/Geometry.cpp`��`src/plugin/ToolController.cpp`��`tests/model_tests.cpp`��

**Interfaces:**
- `struct CellRange { int firstRow; int firstColumn; int lastRow; int lastColumn; };`�������䣬����ʱ�淶������
- `TableModel::merge(CellRange) -> void`��`TableModel::split(CellId) -> void`��
- `TableModel::insertRows(int before, int count) -> void`��`insertColumns(int before, int count) -> void`��
- `TableModel::eraseRows(int first, int count) -> void`��`eraseColumns(int first, int count) -> void`��
- ���нṹ�������ں�ѡģ����֤�����ύ��ʧ�ܱ���ԭģ�����ݺͱ�Ų��䡣

- [ ] ��д������ʧ�ܲ��ԣ����κϲ����������������֣������п����кϲ����ܾ���δ�༭�ĺϲ����ֻ�ԭԭ���ݣ��ѱ༭�ĺϲ����ְ���Ʊ����¾����ݡ�
- [ ] ��д������ʧ�ܲ��ԣ���ϲ���������չ��ȣ�ɾ��ê��ʱǨ�����ݵ���Чê�㣻ɾ�����ֿ�Ⱥ�������ȷ��֣���ֹɾ�⡢Խ��ͳ����ܸ����޶
- [ ] ʵ�������ṹ�㷨����Ӧ�˵����ÿ�β˵�����ǰͬ����ʵ���֡�
- [ ] ����ȫ�����Ĳ��ԣ��� AI ����֤ 3��3 ����ϲ��м�����󷴸����롢ɾ���ͳ��������������ظ����޶��֡�
- [ ] �ύ��`feat: add table structure and merged cell editing`��

## Task 5: �ĵ��־û�������ͬ�����ճ��༭

**Files:** `include/table/Serialization.hpp`��`src/core/Serialization.cpp`��`src/plugin/DocumentAdapter.cpp`��`src/plugin/Plugin.cpp`��`tests/model_tests.cpp`��`docs/HOST_ACCEPTANCE.md`��

**Interfaces:**
- `serialize(const TableModel&) -> std::string`��`deserialize(std::string_view) -> TableModel`��UTF-8���汾 `1`���ϸ�У��ṹ�������޶
- `DocumentAdapter::readTable(TableRef) -> TableModel`�����ĵ�ʵ�����ֿ����ݸ��ǻ����ı�����ȡʵ�����ͬ��λ�ú�����ߴ硣
- `DocumentAdapter::commit(TableRef, const TableModel&, const std::u16string& undoLabel) -> void`�����¶��󼰳־��ֵ䣬ʧ�ָܻ���״̬��
- `DocumentAdapter::detach(TableRef) -> void`���Ƴ����Ԫ���ݣ������ɼ�������·����

- [ ] ʧ�ܲ��Ը��ǣ�ģ�ͺ������������л�����һ�£�δ֪�汾���𻵿�ȡ��ظ� ID������������ܾ���ʧ�ܲ����ɰ��ģ�͡�
- [ ] ʵ�ָ�ʽ�������ֵ��д����ͨ��ִ�нű�����ģ�����ݡ�����ʵ��������ʱ������ʵ�ʶ������͵�ǰ�ĵ�Ϊ׼����ֻ�������ƹ������ַ��� ID��
- [ ] �� AI ���� `.ai`���رա��ؿ�����֤�ϲ������ּ������Կɱ༭��
- [ ] ��ԭ�����ֹ������޸����ֺ��϶����ߣ�ȷ�����ּ����оֲ����ָ�ʽ���������θ��µ������ֿ�Χ�����ؽ�����Ҫ�����ֶ���
- [ ] �������ű����ֱ�༭���ƶ����������ź���֤���У���ת����б��ܾ��ṹ�༭����ʾԭ�򡣶������������ػ��������ȱʧʱ���޸ġ�
- [ ] �϶������ĵ���ȡ����ȷ��ԭ�ĵ������ĵ�������ģ���ɾ���ϲ�����ּ�����ͬ����ֱ���������
- [ ] ʵ��ת����ͨ�����ɼ���ʹ��ԭ�����߱༭�������񹤾߲��ٽ���ʶ��Ϊ�ṹ������
- [ ] �ύ��`feat: persist tables and preserve native document edits`��

## Task 6: ������ʽ���������������

**Files:** `src/plugin/Dialogs.cpp`��`src/plugin/DocumentAdapter.cpp`��`scripts/package.ps1`��`.github/workflows/build.yml`��`README.md`��`docs/HOST_ACCEPTANCE.md`��

**Interfaces:**
- `DocumentAdapter::applyStyle(TableRef, CellRange, const CellStyle&) -> void`��`CellStyle` ������ɫ���߿�������ɫ���ֺš����塢ˮƽ���뼰�����ڱ߾࣬Ĭ�϶������롣
- `scripts/package.ps1 -PluginPath <aip> -OutputDirectory <directory>`���������������� x64 PE �����ɰ��������˵���� `SHA256SUMS.txt` �� ZIP��

- [ ] ʵ��������ʽ�Ի��򣬶���ѡ��ΧӦ����ʽ��ȡ�����޸��ĵ���ȱʧ��������ɲ���˵����������ʾ�滻��
- [ ] �� RGB �� CMYK �ĵ��ֱ���֤��ɫ���������塢�߿�Ͷ������룻��������ʾ���������ʾ�������֡����Զ����ֺš�
- [ ] �Դ��������֤��ȱʧ������ı�αװ `.aip`���ܹ�����ʱʧ�ܣ��ɹ� ZIP ���� SDK ��ƾ�ݡ�
- [ ] �ƶ�ȫ����������Ժ��������ղ���� 28.5 ִ�����ձ���������ѡ�����ߡ����ı༭���ϲ���֡���ɾ���С������ؿ����������š�������������ʽ��������
- [ ] README д����װλ�á�������������ڡ��״�ʹ�á��汾������ж�ط��������ʵ�ʲ������ƺ�˵��һ�¡�
- [ ] ��¼ÿ���������ڡ��汾�������ʧ��ԭ���κ�δ��֤��Ŀ���֡�δ��֤������д��ͨ����
- [ ] �ύ������������Դ�����ӡ��ɹ��������ӡ�ZIP��SHA-256 ��ʵ�����ռ�¼��

## Execution Handoff

- ��ȷ�ϣ��û��ظ�����ʼ�ɡ�����ƻ��Ͽɡ�
- ��ǰʵʩ���飺�ɱ��Ի�ֱ��ִ���������񣬱����� SDK ������δȷ��ǰ��ɶ�����й�����
- �����û����ı��ƻ���ȷ��ִ�з�ʽ��
- �ⲿ��Դ���ٷ� Illustrator 2024 Windows C++ SDK ��δȡ�ã�û�и���Դ���޷���� Task 1 ��ԭ���������������ء�����ʵʩ�п���׼�������� SDK �ĺ��Ĳ��ԣ�����������ԭ������ѽ�����
