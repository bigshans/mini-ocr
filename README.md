# Mini OCR

仓库：<https://github.com/bigshans/mini-ocr>

一个 KDE/Wayland 下的轻量截图 OCR 小工具。原生 C++/Qt6，没有 Electron、没有 Web 技术、没有 Rust，也不依赖 Python。

- 截图选区：调用 KDE 自带 Spectacle，选区体验和系统截图一致
- OCR 引擎可选：PP-OCRv5（本地默认）、Tesseract（本地可选）、DeepSeek（云端）
- 结果窗口：可编辑、自动复制、支持暗色/亮色截图
- 独立的图片 OCR 窗口：拖拽/添加图片、移除、清空、开始识别
- 批量 OCR：队列、进度、取消、逐张预览、复制当前/全部、导出 TXT
- 设置界面：自动复制、置信度阈值、历史条数、模型目录、开机自启
- 历史记录：SQLite，默认保留最近 500 条
- 托盘常驻：全局快捷键触发，单实例
- 本地引擎完全离线；云端引擎需要 API Key，图片会上传

## 依赖

Arch Linux：

```bash
sudo pacman -S --needed qt6-base opencv spectacle cmake gcc wl-clipboard

# 可选：Tesseract 后端 + 中英文语言包
sudo pacman -S --needed tesseract tesseract-data-chi_sim tesseract-data-eng
```

`wl-clipboard` 用来在 Wayland 下可靠地自动复制；没有它时会退回 Qt 剪贴板。

其中 `spectacle` 负责区域截图，Qt6 和 OpenCV 负责界面与推理。Tesseract 是可选后端，CMake 检测到 `libtesseract` 才会编译进去；其他语言需要安装对应的 `tesseract-data-*` 包。

## 构建

```bash
./scripts/fetch-models.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

模型约 21MB，下载到 `models/`：

- `ch_PP-OCRv5_det_mobile.onnx`（~4.8MB）
- `ch_PP-OCRv5_rec_mobile.onnx`（~16MB）
- `ppocrv5_dict.txt`

也可以手动设置 `MINI_OCR_MODEL_DIR` 指向其他模型目录。

## 运行

```bash
./build/mini-ocr
```

启动后驻留托盘。左键/双击托盘图标，或右键菜单“截图识别”，会调起 Spectacle 选区，识别完成后弹出结果窗口。

### 绑定全局快捷键

KDE 系统设置 → 快捷键 → 自定义快捷键 → 新建 → 命令或 URL：

```text
/path/to/mini-ocr/build/mini-ocr capture
```

建议绑定 `Ctrl+Alt+O`。已经运行的实例会通过本地 socket 收到命令，不会重复启动托盘。

### 开机自启（可选）

创建 `~/.config/autostart/mini-ocr.desktop`：

```ini
[Desktop Entry]
Type=Application
Name=Mini OCR
Exec=/path/to/mini-ocr/build/mini-ocr
Terminal=false
X-KDE-autostart-after=panel
```

### 设置

托盘右键 → 设置…，或执行 `mini-ocr settings`。可以调整：

- 识别引擎：PP-OCRv5、Tesseract 或 DeepSeek（云端）
- 识别后是否自动复制
- 最低置信度（低于该值的文本行会被丢弃）
- 历史记录保留条数
- PP-OCR 模型目录（修改后立即重新加载）
- Tesseract 语言，例如 `chi_sim+eng`、`eng`、`jpn`
- 云端 OCR 的服务地址、模型、API Key、提示词、超时
- 是否登录时自动启动

切换引擎会立即重新加载。加载失败时设置不会保存，旧引擎继续可用。各引擎的配置分别保留，切换回来不会丢失。设置窗口里会实时显示当前引擎和云端目标。

全局快捷键不在应用内注册，而是在 KDE 系统设置里绑定 `mini-ocr capture`，设置窗口会显示完整命令，可直接复制。

### 图片 OCR 窗口

托盘右键 → **图片 OCR…**，打开一个独立的图片 OCR 窗口：

- 点击“添加图片…”多选，或直接把图片拖进窗口
- 支持多张图片；可以移除选中项或清空列表
- 点击“开始识别”后依次处理，窗口内显示：
  - 每张图的等待/识别中/完成/失败状态
  - 进度条和完成计数
  - 当前图片的图片预览和识别文本预览
- 图片预览支持：
  - 鼠标滚轮缩放
  - 按住拖拽平移
  - `-` / `适应` / `+` 按钮，右上角显示当前缩放比例
- 支持取消当前批次、重新开始、复制当前、复制全部、导出 TXT
- 每张成功识别的图片都会写入历史记录

托盘入口不再直接弹文件选择器，而是先打开这个独立窗口；在窗口里可以继续添加或拖入图片。

也可以用命令行：

```bash
mini-ocr files image1.png image2.png image3.jpg
```

已经运行的实例会通过本地 socket 收到文件列表，打开图片 OCR 窗口并自动开始；单张图片会直接打开结果窗口。

### 命令行

```bash
mini-ocr capture          # 触发一次截图识别
mini-ocr upload           # 打开空的图片 OCR 窗口
mini-ocr files a.png b.png # 图片 / 批量图片识别（GUI）
mini-ocr history          # 打开历史记录
mini-ocr settings         # 打开设置
mini-ocr quit             # 退出托盘实例
mini-ocr ocr image.png    # 只识别并输出到终端（调试用，支持单张）
mini-ocr show image.png   # 识别图片并打开结果窗口
```

## Arch 打包

需要 `base-devel`（包含 `makepkg`）：

```bash
./scripts/package-arch.sh
sudo pacman -U packaging/mini-ocr-0.1.0-1-x86_64.pkg.tar.zst
```

脚本会：

1. 生成 `packaging/mini-ocr-<version>.tar.gz`（包含源码、资源和 ONNX 模型）
2. 调用 `makepkg -f`
3. 输出 `packaging/mini-ocr-<version>-<rel>-x86_64.pkg.tar.zst`

包内容：

```text
/usr/bin/mini-ocr
/usr/share/mini-ocr/models/
/usr/share/applications/mini-ocr.desktop
/usr/share/icons/hicolor/scalable/apps/mini-ocr.svg
/usr/share/licenses/mini-ocr/LICENSE
/usr/share/doc/mini-ocr/README.md
```

依赖：

- `qt6-base`
- `opencv`
- `spectacle`
- `tesseract`（包内编译了 Tesseract 后端）
- 可选：`wl-clipboard`、`tesseract-data-chi_sim`、`tesseract-data-eng`

安装后会提示在 KDE 系统设置中绑定 `/usr/bin/mini-ocr capture`。

`PKGBUILD` 当前使用 `sha256sums=('SKIP')` 方便本地打包；如果要提交 AUR，把源码 tarball 的 sha256 填进去即可。

## Windows（实验性支持）

代码已经做了平台拆分，Linux 构建已通过；Windows 代码路径已加入，但当前环境不是 Windows，**还没有在 Windows 上实际编译和运行验证**。

### 依赖

- Windows 10/11 x64
- Visual Studio 2022（MSVC）
- Qt 6.8+ MSVC 64-bit
- CMake
- OpenCV 5（推荐；OpenCV 4.x 对 PP-OCRv5 的 ONNX 算子可能不完整）
- 可选：Tesseract（vcpkg 安装；Windows 下 pkg-config 通常找不到，会自动禁用）

建议用 vcpkg 管理 OpenCV/Tesseract，Qt 用官方 MSVC 安装包。

### 构建

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
$env:QT_DIR = "C:\Qt\6.8.3\msvc2022_64"
.\scripts\deploy-windows.ps1
```

脚本会：

- CMake 配置 MSVC x64
- Release 构建
- `windeployqt`
- 复制 `models/`
- 复制 vcpkg 运行时 DLL
- 输出便携目录 `dist/`

运行：

```powershell
.\dist\mini-ocr.exe
```

### Windows 端差异

- **截图**：使用 Windows 10/11 自带的 Snipping Tool 浮层（`ms-screenclip:`），结果从剪贴板读取。多屏和 DPI 由系统处理；后续可替换为原生选区窗口。
- **全局快捷键**：应用内通过 `RegisterHotKey` 注册 `Ctrl+Alt+O`，不依赖 AutoHotkey 或 KDE。
- **自动复制**：直接使用 Qt 剪贴板，不再调用 `wl-copy`。
- **开机自启**：写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- **托盘/设置/图片/批量/云端**：沿用同一套 Qt UI。
- **图标**：`resources/mini-ocr.ico` + `resources/mini-ocr.rc`。
- **字体**：优先 `Microsoft YaHei UI`。

### 已知待办

- 在真实 Windows 环境编译并修 MSVC/vcpkg 兼容问题
- 确认 OpenCV 5 的 Windows 包和 PP-OCRv5 算子支持；不稳定的话切 ONNX Runtime
- 把 Snipping Tool 方案替换为原生 Qt/Win32 选区覆盖层
- 可选：接入 `Windows.Media.Ocr` 作为 Windows 本地 OCR 后端
- 安装包（NSIS）和 CI 构建

## 安装到系统（可选）

```bash
sudo cmake --install build --prefix /usr
```

安装后会得到 `/usr/bin/mini-ocr`、`/usr/share/mini-ocr/models/`、桌面文件和图标。之后快捷键直接写 `mini-ocr capture`。

## OCR 引擎

| 引擎 | 适用场景 | 说明 |
|---|---|---|
| **PP-OCRv5（默认）** | 中文、中英混排、代码截图 | 本地 ONNX 模型，21MB，无需额外语言包，精度最好 |
| **Tesseract（可选）** | 英文、实验/对照 | 使用系统 tessdata，需要安装语言包；中文精度明显低于 PP-OCR |
| **DeepSeek（云端）** | 复杂版面、表格、公式、Markdown | 官方 `deepseek-flash` 或 SiliconFlow 的 DeepSeek-OCR，需要 API Key，图片会上传 |

在“设置 → 识别引擎”里切换。Tesseract 的品质取决于语言包和截图清晰度，中文场景建议继续用 PP-OCR。

## 云端 OCR：DeepSeek

先澄清一下：**DeepSeek 官方 API 有视觉能力，但没有单独的 `deepseek-ocr` 模型 ID。** 官方目前通过 `deepseek-flash` 接收图片输入；DeepSeek-OCR 本身是 DeepSeek 开源的 3B OCR 模型，官方 API 没有把它作为独立端点暴露。SiliconFlow 则明确托管了 `deepseek-ai/DeepSeek-OCR`，所以之前默认用了它。

现在设置里有两个预设，随便选：

### 预设 1：DeepSeek 官方（默认）

```text
服务地址：https://api.deepseek.com
模型：    deepseek-flash
提示词：  请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。
```

- 官方文档：<https://api-docs.deepseek.com/guides/vision>
- API Key：<https://platform.deepseek.com/api_keys>
- `deepseek-flash` 是通用视觉模型，不是专门的 OCR 权重；按 token 计费。
- 请求是标准 OpenAI 格式，图片通过 `image_url` 传入。

### 预设 2：SiliconFlow 的 DeepSeek-OCR

```text
服务地址：https://api.siliconflow.cn/v1
模型：    deepseek-ai/DeepSeek-OCR
提示词：  Free OCR.
```

- API Key：<https://cloud.siliconflow.cn/account/ak>
- SiliconFlow 目前把 DeepSeek-OCR 标为限免。
- 这是专门的 OCR 权重，纯文本提取用 `Free OCR.`；Markdown/表格可改成 `<image>\n<|grounding|>Convert the document to markdown.`。
- DeepSeek-OCR 官方 raw prompt 是 `<image>\nFree OCR.`；如果服务端不自动插入图片占位符，手动加上 `<image>`。

### 自定义

服务地址、模型、提示词都可以改，所以也能接自建 vLLM、其他云厂商或别的视觉模型。

其他说明：

- API Key 按预设分别保存（DeepSeek / SiliconFlow / 自定义），切换预设时不会混用；仍以明文保存在 `~/.config/mini-ocr.conf`，建议使用单独的、可随时吊销的 Key。
- 云端结果没有逐行坐标，应用会按行拆分文本，不参与本地低置信度过滤。
- 失败时会在结果窗口/托盘提示 HTTP 状态和服务端错误信息，不会静默失败。
- 云端请求由 Qt Network 在 OCR 工作线程中执行，超时默认 60 秒，可在设置里调整。

配置文件示例（`~/.config/mini-ocr.conf`）：

```ini
[General]
autoCopy=true
minScore=0.35
historyLimit=500
ocrBackend=cloud

# PP-OCR
ppocrModelDir=

# Tesseract
tesseractLang=chi_sim+eng

# DeepSeek / OpenAI 兼容云端（默认官方 deepseek-flash）
cloudPreset=deepseek-official
cloudBaseUrl=https://api.deepseek.com
cloudModel=deepseek-flash
deepseekApiKey=
siliconflowApiKey=
customApiKey=
cloudPrompt=请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。
cloudTimeoutSec=60
```

各引擎的配置字段分开保存，切换引擎时不会互相覆盖；未使用的字段会保留，方便切回来。云端 API Key 也按预设分开保存：`deepseekApiKey`、`siliconflowApiKey`、`customApiKey`，切换预设时只加载对应的 Key，不会混用。旧版 `cloudApiKey` 会自动迁移到当前预设对应的槽位。

## 实现说明

```text
mini-ocr
├─ src/main.cpp          托盘、单实例、快捷键命令、后台 OCR 调度
├─ src/capture.cpp       QProcess 调 spectacle -r -b -n -o
├─ src/ocr_backend.h     OCR 引擎接口与工厂
├─ src/ocr_engine.cpp    PP-OCRv5 det + rec，OpenCV DNN，CTC 解码
├─ src/tesseract_engine.cpp Tesseract TSV 输出解析（可选编译）
├─ src/cloud_engine.cpp  OpenAI 兼容云端 OCR（DeepSeek 官方 / SiliconFlow）
├─ src/batch_window.h/.cpp 独立图片上传 / 批量 OCR 窗口
├─ src/result_window.cpp 结果浮窗
├─ src/history.cpp       SQLite 历史 + 历史窗口
├─ models/               ONNX 模型与字典
└─ scripts/fetch-models.sh
```

OCR 流程：

1. 截图缩放到最长边 960，按 32 对齐；
2. DB 检测模型输出概率图，阈值 0.3，取轮廓；
3. 按轮廓外扩（模拟 PaddleOCR 的 unclip），裁出文本行；
4. 文本行缩放到高 48，宽度按比例动态变化；
5. 识别模型输出 CTC logits，贪心解码，按字典映射成文字；
6. 按行坐标排序，过滤低置信度结果。

预处理使用 BGR、`(x/255 - 0.5)/0.5`，与 PaddleOCR/RapidOCR 的训练预处理保持一致。

## 性能（本机 GTX 1650 Ti / CPU 推理）

- 模型加载：约 0.6–0.8 秒（启动时加载一次）
- 800×300 选区：约 100–300ms
- 1920×1080 全屏、120 行文字：约 3.8 秒
- 常驻内存：空载约 140MB，识别后约 260MB（OpenCV DNN 缓存，之后保持稳定）
- 二进制约 210KB，链接系统 Qt/OpenCV

## 已知限制

- 截图后端目前绑定 Spectacle；其他桌面需要换 `capture.cpp`
- Wayland 不允许应用随意定位窗口，结果窗口默认居中显示
- 目前只做普通文字行，不解析表格/公式/手写
- 没有做 GPU 推理；截图场景 CPU 足够
- 低置信度行会被过滤（阈值 0.35），可能漏掉极淡的文字
- 历史只存文本，不存截图

## 许可

代码 MIT。PP-OCRv5 ONNX 模型来自 RapidOCR/PaddleOCR，遵循 Apache-2.0。
