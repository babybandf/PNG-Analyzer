# PNG Analyzer 项目准备与第三方代码引入规范 v0.1

> 状态：Proposed  
> 基准日期：2026-08-20  
> 适用范围：PNG Analyzer GitHub 开源仓库的 M0 阶段  
> 对应工作包：在 `WP-000` 与 `WP-001` 之间插入 `WP-00A Project Bootstrap`

## 1. 结论

现有架构和 Agent 开发计划已经规定了“使用 C++20、Qt 6、CMake、libpng、zlib”，但还不足以让 Agent 无歧义地准备项目。缺少的内容包括：

- 从哪个上游取得依赖和参考源码。
- 哪些库是产品依赖，哪些只是测试工具或设计参考。
- 如何固定版本、commit、哈希与许可证。
- 哪些上游源码允许复制和改造，哪些只允许通过库 API 使用。
- 新开发者和 CI 如何从干净环境重建相同工程。
- 依赖漂移、来源不明或许可证缺失时，Agent 应如何停止。

本规范补齐上述缺口。核心决策是：

1. **Qt 由 Qt 官方安装器提供**，不在默认开发流程中从源码编译，也不通过 vcpkg 构建。
2. **libpng、zlib、Catch2 使用 vcpkg manifest mode**，由仓库内 `vcpkg.json` 和固定 baseline 解析。
3. **不把 libpng、zlib 整仓复制进项目仓库**；常规生产依赖通过包管理器取得。
4. **`zran` 和 `puff` 只在对应 WP 获得授权后引入**；引入时固定上游 commit、保留许可、明确标注修改。
5. **测试图片不是“从网上随便找”**；每个 fixture 都必须进入 corpus manifest，记录来源、许可、期望结果和 SHA-256。

## 2. 依赖分类和唯一允许来源

### 2.1 产品与测试依赖

| 组件 | 用途 | 取得方式 | 唯一上游 | 初始基准 | 是否进入产品包 |
|---|---|---|---|---|---|
| Qt 6 | GUI、model/view、线程与平台集成 | Qt Online Installer；CI 使用其命令行模式 | [Qt 官方安装文档](https://doc.qt.io/qt-6/get-and-install-qt.html) | CI 固定 6.11.2；源码兼容下限 6.8 | 是，动态库 |
| libpng | Reference Backend、最终图像差分基准 | vcpkg manifest | [pnggroup/libpng](https://github.com/pnggroup/libpng) | 1.6.58，`libpng16` 稳定分支 | 是 |
| zlib | Fast Inflate、Adler-32、checkpoint 恢复 | vcpkg manifest | [madler/zlib](https://github.com/madler/zlib) | 1.3.2 | 是 |
| Catch2 | Core 单元测试与测试发现 | vcpkg manifest，仅开发/CI | [catchorg/Catch2](https://github.com/catchorg/Catch2) | 3.11.0 | 否 |
| CMake | 构建和 presets | 系统安装或官方安装包 | [cmake.org](https://cmake.org/download/) | 最低 3.28 | 否 |
| Ninja | 默认本地/CI generator | 系统安装或官方 release | [ninja-build/ninja](https://github.com/ninja-build/ninja) | 最低 1.11 | 否 |
| Python | bootstrap、corpus 生成与验证脚本 | 系统安装 | [python.org](https://www.python.org/downloads/) | 最低 3.11 | 否 |
| vcpkg | C/C++ 依赖解析与构建 | bootstrap 脚本 clone 固定 commit | [microsoft/vcpkg](https://github.com/microsoft/vcpkg) | 固定 40 位 commit，不跟随 `master` | 否 |

说明：

- “初始基准”是仓库创建时应尝试锁定的版本，不代表永远不升级。
- 如果 vcpkg 当前 registry 尚未包含表中精确版本，`WP-00A` 必须 `BLOCKED` 并提交证据，不能静默降级或改用不明二进制包。
- 不采用系统自带 libpng/zlib 作为 CI 参考版本，因为不同发行版会解析到不同版本和编译选项。
- Qt 6.11.2 是当前参考环境。CMake 声明的 API 下限为 Qt 6.8，但 M0 只承诺对 CI 实际固定的版本提供通过证据。
- 不在 v1 前切换到 libpng 1.8/2.x 预览分支；如需迁移，必须单独建立 ADR 和兼容性矩阵。

### 2.2 规范、参考工具和可借用源码

| 项目 | 分类 | 建议用途 | 引入规则 |
|---|---|---|---|
| [PNG Specification, Third Edition](https://www.w3.org/TR/png-3/) | 规范 | Chunk、过滤、Adam7、APNG 的规范依据 | 链接引用，不复制整份规范 |
| [pngcheck](https://www.libpng.org/pub/png/apps/pngcheck.html) | 外部测试 oracle | Chunk/CRC/结构诊断结果交叉检查 | CI 工具可固定版本；不得成为产品运行时依赖 |
| [zlib `examples/zran`](https://github.com/madler/zlib/blob/develop/examples/zran.h) | 可借用示例源码 | Deflate 稀疏索引、窗口字典和随机访问设计参考 | 在 WP-400/401 明确授权后引入固定 commit |
| [zlib `contrib/puff`](https://github.com/madler/zlib/tree/develop/contrib/puff) | 可借用示例源码 | Deep Trace 解码器的可读 Deflate 基线 | 在 WP-500/501 明确授权后引入固定 commit |
| PngSuite | 测试 corpus | 色型、位深、Adam7、透明度等覆盖 | 只从原始发布点或经审计镜像取得；记录逐文件许可与哈希 |
| libpng 自带 tests/testpngs | 测试 corpus | Reference Backend 回归和异常输入 | 从与 libpng 依赖相同的 tag/commit 取得 |
| libspng | 可选差分后端 | v1 后增加第三个 decoder oracle | 不进入 MVP；需新 WP 批准依赖 |

这些类别必须分开：

- **产品依赖**：链接到应用，参与发布和 SBOM。
- **测试依赖**：只在开发或 CI 中构建。
- **外部 oracle**：进程外运行，结果用于交叉检查，不链接进应用。
- **参考源码**：默认只阅读；复制或派生必须由具体 WP 授权。
- **测试资产**：每个文件都必须有来源、许可、哈希和预期分类。

## 3. 为什么使用 Qt 官方安装器 + vcpkg manifest

### 3.1 Qt 单独安装

Qt 官方文档把 Online Installer 列为桌面开发的常规安装方式，也提供命令行模式用于自动化。项目应通过 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR` 找到 Qt，而不是让每位开发者本地编译 Qt。

推荐安装内容：

- Qt 6.11.2 Desktop kit。
- Windows：MSVC 2022 64-bit kit。
- Linux：GCC 64-bit kit。
- macOS：对应宿主架构的 Desktop kit。
- Qt 模块仅启用 `Core`、`Gui`、`Widgets`、`Test`；未经 WP 批准不增加 WebEngine、Multimedia 等大型模块。

CMake 约束：

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)
```

`libs/core` 不允许链接任何 `Qt6::*` target。只有 GUI adapter、GUI app 和 Qt model tests 可以链接 Qt。

### 3.2 libpng、zlib、Catch2 由 vcpkg 管理

vcpkg 官方建议项目使用 manifest mode；`vcpkg.json` 的 `builtin-baseline` 指向 vcpkg registry 的具体 Git commit，因此可以让三平台解析相同的 port 版本。项目不允许使用 vcpkg classic mode 的全局安装树。

初始 `vcpkg.json` 模板：

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "png-analyzer",
  "version-string": "0.1.0",
  "builtin-baseline": "<WP-00A-FILLS-40-HEX-COMMIT>",
  "dependencies": [
    {
      "name": "libpng",
      "default-features": false
    },
    {
      "name": "zlib",
      "default-features": false
    },
    {
      "name": "catch2",
      "default-features": false
    }
  ],
  "overrides": [
    {
      "name": "libpng",
      "version": "1.6.58"
    },
    {
      "name": "zlib",
      "version": "1.3.2"
    },
    {
      "name": "catch2",
      "version": "3.11.0"
    }
  ]
}
```

模板中的 commit 占位符不能合入 `main`。`WP-00A` 必须选择一个能在三平台解析三个 override 的 vcpkg commit，并把实际 40 位 commit 写入文件。

CMake 必须使用上游导出的 targets：

```cmake
find_package(PNG CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

target_link_libraries(pnga_reference_backend PRIVATE PNG::PNG ZLIB::ZLIB)
target_link_libraries(pnga_unit_tests PRIVATE Catch2::Catch2WithMain)
```

如果某个平台的 vcpkg port 暂时没有 `PNG::PNG`，依赖适配应集中在 `cmake/Dependencies.cmake`，不得把平台分支散落到业务 targets。

## 4. 哪些上游代码可以实际复用

### 4.1 libpng：通过 API 使用，不复制内部实现

允许：

- 使用 libpng public read API 实现 Reference Backend。
- 使用 progressive/read callbacks 获取行级输出和错误。
- 在测试中使用同版本 libpng 生成 reference pixels。
- 固定 tag 后审阅其测试 fixture 和公开示例。

禁止：

- 直接访问 `png_struct` 或 `png_info` 私有字段。
- 从 libpng 内部文件摘抄过滤、Adam7 或 Chunk 处理函数后不记录来源。
- 为了 trace 直接长期维护一份 libpng 私有 fork。

原因是产品需要稳定的参考后端，而细粒度可观测能力应由独立 Trace Backend 提供。

### 4.2 `zran`：复用设计，不直接照搬文件 I/O

`zran` 已明确展示了一个 access point 至少需要：

- 未压缩流 offset。
- 压缩流 byte/bit offset。
- 到 32 KiB 的前置滑动窗口字典。
- 用于恢复 inflate 的 stream 状态。

它还指出每个 access point 约需 32 KiB，因此可以直接用于 checkpoint 密度与随机访问延迟的权衡估算。

PNG Analyzer 不能原样使用 `zran` 的 `FILE*` 连续文件假设。派生实现必须把输入替换为 `VirtualIDATStream`，同时保存 logical stream offset 到 physical IDAT span 的映射。建议流程：

1. 在 WP-400 中只把 `zran` 作为设计 oracle，不复制源码。
2. 完成自己的 `CheckpointRecord` 接口和 zlib API 验证。
3. 如果 WP-401 仍决定复用，复制来自固定 tag/commit 的最小文件。
4. 原样副本放在 `third_party/zlib-zran/original/`；派生实现放在 `libs/deflate-index/`。
5. `MODIFICATIONS.md` 逐项记录对 I/O、offset、dictionary 和错误处理的改动。

### 4.3 `puff`：适合做可读的 Deep Trace 起点，不适合 Fast Backend

`puff.c` 的上游定位就是一个易读、带注释、速度慢于 zlib 的 Deflate 实现。因此它适合用来建立 Huffman 表、block、literal、length/distance token 的事件模型，但不应替代 Fast Backend 的 zlib。

授权引入时采用以下目录：

```text
third_party/zlib-puff/
  UPSTREAM.md
  LICENSE
  original/
libs/deflate-trace/
  include/pnga/deflate-trace/
  src/
  MODIFICATIONS.md
```

`UPSTREAM.md` 必须写明：

- upstream URL。
- tag 和完整 commit。
- 下载日期。
- 原始文件 SHA-256。
- 许可证。
- 获得授权的 WP 编号。

派生源码的文件头必须标注“derived from zlib contrib/puff”并指向 `MODIFICATIONS.md`。zlib 许可要求修改版源码明确标记，原版权/许可声明不得移除。

## 5. 测试 corpus 获取规则

初始 corpus 不追求数量，而追求来源可审计和覆盖可解释。建议三层：

1. **项目自己生成的 tiny corpus**：1×1、短行、每种 color type/bit depth、每种 filter、边界 Chunk 长度；生成器和期望值都在仓库内。
2. **上游 conformance corpus**：PngSuite、libpng testpngs；固定 archive/tag 和哈希。
3. **缺陷 corpus**：每个安全或兼容性 bug 的最小复现；默认不包含敏感用户文件。

每个外部 corpus 进入 `tests/corpus/manifest.yaml`，字段至少为：

```yaml
- id: basn2c08
  path: pngsuite/basn2c08.png
  source_url: https://example.invalid/fill-with-reviewed-origin
  upstream_version: 2017-07-19
  upstream_commit: null
  sha256: fill-with-64-hex
  license: fill-with-reviewed-license
  expected_class: valid
  expected_features: [rgb, bit_depth_8, non_interlaced]
  expected_width: 32
  expected_height: 32
```

带 `example.invalid`、空哈希、未知许可或模糊来源的记录不得合入 `main`。外部 corpus 的下载由 `scripts/fetch_corpus.py` 完成，脚本必须先校验 archive SHA-256，再解包到构建缓存；大型 corpus 默认不直接提交 Git。

## 6. 仓库在 WP-00A 后必须出现的文件

```text
/
├─ vcpkg.json
├─ CMakeUserPresets.example.json
├─ .gitignore
├─ cmake/
│  ├─ Dependencies.cmake
│  └─ dependencies.lock.json
├─ scripts/
│  ├─ bootstrap.py
│  ├─ verify_dependencies.py
│  └─ fetch_corpus.py
├─ third_party/
│  ├─ README.md
│  └─ sources.lock.yaml
├─ tests/bootstrap/
│  ├─ CMakeLists.txt
│  ├─ CMakePresets.json
│  └─ version_smoke.cpp
├─ tests/corpus/
│  ├─ README.md
│  └─ manifest.yaml
└─ THIRD_PARTY_NOTICES.md
```

> 根 `CMakeLists.txt` 与根 `CMakePresets.json`（`dev`/`asan`/`release`）不属于 WP-00A，
> 随 WP-001 根工程建立。WP-00A 的 `deps-smoke` 是 `tests/bootstrap` 下的独立工程。

职责分工：

| 文件 | 内容 |
|---|---|
| `vcpkg.json` | libpng、zlib、Catch2 及 registry baseline（`builtin-baseline` 固定 vcpkg commit） |
| `dependencies.lock.json` | 产品依赖锁定版本、vcpkg release/commit、Qt CI 版本、CMake/Ninja/Python 最低版本 |
| `tests/bootstrap/CMakePresets.json` | WP-00A 的 `deps-smoke` 预置（vcpkg manifest 模式、Ninja、`VCPKG_MANIFEST_DIR` 指向根）；不写开发机绝对路径 |
| `CMakeUserPresets.example.json` | 演示如何指向本机 Qt；实际 UserPresets 被 `.gitignore` |
| `bootstrap.py` | 环境检查、固定 commit 的 vcpkg clone/bootstrap、依赖安装 |
| `verify_dependencies.py` | 检查占位符、浮动分支、哈希、许可和未登记 vendor 文件 |
| `sources.lock.yaml` | 所有复制进仓库的第三方源码及 provenance |
| `manifest.yaml` | 测试图片来源、哈希、许可和预期行为 |
| `THIRD_PARTY_NOTICES.md` | 发布时需要携带的第三方声明 |

不采用 Git submodule 作为默认依赖机制。submodule 对普通贡献者、release source archive 和 Agent 工作树都更容易产生“目录存在但内容未取回”的不一致状态。vcpkg clone 位于 gitignored 的 `.deps/vcpkg/`，由 bootstrap 脚本按固定 commit 创建。

## 7. 开发者从空机器开始的流程

### 7.1 一次性安装

Windows：

- Visual Studio 2022 Build Tools，启用 Desktop development with C++。
- Qt Online Installer：Qt 6.11.2 / MSVC 2022 64-bit。
- CMake、Ninja、Python 3.11+、Git。

macOS：

- Xcode Command Line Tools。
- Qt Online Installer：Qt 6.11.2 Desktop kit。
- CMake、Ninja、Python 3.11+、Git。
- `pkg-config`（`brew install pkg-config`）：catch2 的 vcpkg port 构建时需要。

Linux：

- GCC 或 Clang、标准 C/C++ 开发环境和 Qt 运行所需系统库。
- Qt Online Installer：Qt 6.11.2 GCC 64-bit。
- CMake、Ninja、Python 3.11+、Git。
- `pkg-config`：catch2 的 vcpkg port 构建时需要。

说明：Windows 上 catch2 所需的 pkg-config 由 vcpkg 在构建时自动获取，无需系统安装；Linux/macOS 需系统提供。

### 7.2 克隆和 bootstrap

`WP-00A` 完成后，统一命令应为：

```bash
git clone https://github.com/<org>/png-analyzer.git
cd png-analyzer
python3 scripts/bootstrap.py --qt-root /absolute/path/to/Qt/6.11.2/<kit>
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev --output-on-failure
```

Windows PowerShell 使用同一个 Python 脚本：

```powershell
py -3.11 scripts/bootstrap.py --qt-root C:\Qt\6.11.2\msvc2022_64
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
```

bootstrap 只允许做以下写入：

- `.deps/`
- `vcpkg_installed/`
- `build/`
- `CMakeUserPresets.json`

这些路径全部 gitignored。脚本不能更改 `vcpkg.json`、lock 文件或第三方来源清单。

## 8. `WP-00A Project Bootstrap` 的 Agent 任务

```yaml
id: WP-00A
title: 固定并验证项目工具链与第三方依赖来源
milestone: M0
depends_on: [WP-000]
risk: medium
estimated_size: M

goal: |
  干净机器只需安装编译器、Qt、Git 和 Python，即可用仓库统一命令
  取得固定版本的 libpng、zlib、Catch2，并生成可审计依赖报告。

allowed_paths:
  - vcpkg.json
  - CMakePresets.json
  - CMakeUserPresets.example.json
  - cmake/**
  - scripts/bootstrap.py
  - scripts/verify_dependencies.py
  - third_party/**
  - THIRD_PARTY_NOTICES.md
  - tests/bootstrap/**
  - tests/corpus/**
  - .gitignore
  - .github/workflows/deps-smoke.yml

forbidden_changes:
  - 不实现 PNG parser、decoder 或 GUI
  - 不复制 libpng/zlib 整仓源码
  - 不引入 FetchContent、Conan 或第二套包管理器
  - 不使用浮动 main/master 作为下载版本

stop_conditions:
  - vcpkg registry 无法解析批准的版本
  - Qt 许可或动态链接发布方式需要改变
  - 任一外部资产没有明确许可
  - 需要修改 ADR-0001 或第三方依赖政策
```

实施顺序：

1. 选择并记录 vcpkg tool commit 和 registry `builtin-baseline`。
2. 创建 manifest，验证三个依赖在本机可解析。
3. 创建 `dependencies.lock.json` 和依赖来源/许可清单。
4. 实现只做环境检查和依赖准备的 `bootstrap.py`。
5. 创建 `tests/bootstrap/CMakePresets.json`（`deps-smoke` 预置）和 Qt 本机路径示例（`CMakeUserPresets.example.json`）。
6. 实现 `verify_dependencies.py`。
7. 在 `tests/bootstrap/` 创建只链接 `PNG::PNG`、`ZLIB::ZLIB`、Catch2 的 dependency smoke target，输出版本并与 lock 比对。
8. 在 Windows、Linux、macOS CI 中完成干净 bootstrap（`deps-smoke.yml`）；之后 WP-001 才创建正式 app/core targets。

## 9. WP-00A 自我验证

### 9.1 自动验证命令

`deps-smoke` 是 `tests/bootstrap` 下的独立 CMake 工程，其预置文件位于
`tests/bootstrap/CMakePresets.json`（CMake 从当前工作目录读取预置），因此
以下 smoke 命令在仓库根执行静态检查，在 `tests/bootstrap` 目录执行构建：

```bash
python3 scripts/verify_dependencies.py
python3 scripts/bootstrap.py --qt-root "$PNG_ANALYZER_QT_ROOT" --check-only
cd tests/bootstrap
cmake --preset deps-smoke
cmake --build --preset deps-smoke
ctest --preset deps-smoke --output-on-failure
```

`vcpkg.json` 位于仓库根，由 `VCPKG_MANIFEST_DIR` 显式指向（smoke 工程作为
子目录配置时 vcpkg manifest 需显式定位）。

### 9.2 二值验收条件

- `vcpkg.json` 不含占位符、浮动 branch 或未经批准的 registry。
- 三个平台解析到同一 libpng、zlib、Catch2 upstream 版本。
- smoke executable 能输出实际编译时的 `PNG_LIBPNG_VER_STRING`、`ZLIB_VERSION`、`CATCH_VERSION_*` 和 Qt runtime version。
- 输出版本与 lock 文件完全一致；不一致则测试失败。
- 清空 `.deps/`、`vcpkg_installed/`、`build/` 后，可以重新 bootstrap。
- 断网后，在已有 binary/source cache 条件下可以完成第二次构建；没有缓存时应给出明确缺失项，而不是改用系统库。
- `git status --short` 不出现下载的源码、构建产物或本机 Qt 路径。
- `verify_dependencies.py` 对以下任一情况返回非零：未知 vendor 文件、缺许可证、缺哈希、40 位 commit 不合法、corpus 来源是占位 URL。

### 9.3 Agent 必须提交的证据

```text
status: PASS | BLOCKED | FAIL
host_matrix:
  - os / compiler / architecture
resolved_versions:
  - Qt / libpng / zlib / Catch2 / vcpkg tool / registry baseline
commands:
  - command / exit code / duration
dependency_report:
  - artifact path / SHA-256
license_check:
  - notices present / missing items
known_limitations:
  - explicit list
```

## 10. 依赖升级流程

依赖升级不能混入功能 PR。每次升级建立独立 PR：

1. 更新 baseline/override/Qt CI version 中必要的最小集合。
2. 生成旧版与新版的 resolved dependency diff。
3. 运行三平台 build、Reference Backend corpus、Trace vs libpng differential、ASan/UBSan。
4. 对 libpng/zlib 升级运行大文件 checkpoint 和 malformed corpus。
5. 更新 `THIRD_PARTY_NOTICES.md`、SBOM 与 release notes。
6. 性能或解码结果出现变化时，不能只更新 golden；必须先解释差异。

建议每月检查一次依赖更新，但只有安全修复、明确 bug 修复或项目需要的新能力才升级。仓库不应每天自动提交 vcpkg baseline 漂移。

## 11. 许可证和发布准备

初始许可证处理建议：

| 组件 | 初步分类 | 项目动作 |
|---|---|---|
| Qt | LGPL-3.0/GPL-2.0/commercial 多许可证 | 开源发行默认动态链接；随包提供适用许可与 attribution，并保留用户替换 Qt 动态库的能力 |
| libpng | libpng license | 将上游许可加入 notices/SBOM |
| zlib、zran、puff | zlib license | 保留声明；修改源码明确标注，不冒充原版 |
| Catch2 | BSL-1.0 | 测试依赖；源码/派生分发时保留许可声明 |
| PngSuite/其他 corpus | 逐来源 | manifest 与发行包分别核验，不把“可下载”视为“可再分发” |

这是一份工程合规清单，不替代正式法律意见。发布 Gate 应单独验证 Qt 部署方式和所有二进制包内的 notices。

## 12. M0 的更新后执行顺序

原顺序：

```text
WP-000 → WP-001 → WP-002
```

更新为：

```text
WP-000 → WP-00A → WP-001 → WP-002
```

其中：

- `WP-000` 冻结治理、ADR 与第三方政策。
- `WP-00A` 固定下载来源、版本、许可和 bootstrap。
- `WP-001` 才开始建立 Core/CLI/GUI walking skeleton。
- `WP-002` 把已验证的同一 bootstrap 流程放入三平台 CI。

只要 `WP-00A` 尚未通过，后续 Agent 不得自行选择包管理器、复制上游源码、下载随机测试图或开始产品代码实现。

