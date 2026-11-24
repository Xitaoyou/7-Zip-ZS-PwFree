<div align="center">

# 7-Zip-ZS-PwFree

**现代简约界面 × 自动云解密 × 全算法增强版 7-Zip**

[![Release](https://img.shields.io/github/v/release/Xitaoyou/7-Zip-ZS-PwFree?include_prereleases&style=flat-square&color=0067C0)](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases)
[![Windows CI](https://img.shields.io/github/actions/workflow/status/Xitaoyou/7-Zip-ZS-PwFree/build-windows.yml?branch=master&label=Windows%20Build&style=flat-square)](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/actions/workflows/build-windows.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20ARM64%20%7C%20x86-lightgrey?style=flat-square)](#下载与安装)
[![License](https://img.shields.io/badge/License-LGPL--2.1%20%2F%20BSD-green?style=flat-square)](COPYING)

基于 [7-Zip Zstandard (7-Zip-ZS)](https://github.com/mcmilk/7-Zip-zstd) 深度定制：在保留全部高性能压缩算法（Zstd / Brotli / LZ4 / Lizard / Fast-LZMA2）的基础上，内置 **PwFree 自动云解密引擎**，并对 `7zFM` 主窗口进行了 **Bandizip 风格现代简约视觉重构**（纯 Win32 原生绘制，零外部运行时依赖）。

[立即下载 Releases](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases) · [功能特性](#核心功能) · [界面介绍](#界面介绍) · [配置说明](#pwfree-配置项) · [源码构建](#从源码构建)

</div>

---

## 核心功能

### 1. PwFree 自动云解密与高速恢复引擎

当你双击打开或解压受密码保护的压缩包时，PwFree 会在弹出密码输入框之前**无感介入**，按优先级尝试自动恢复密码；若未命中或无网络，则立即无缝回退至 7-Zip 原生密码弹窗，绝不阻塞正常使用。

- **多级恢复链（按耗时由低到高）**
  1. `seed` — 复用命令行或同会话中已提供的已知密码
  2. `cache` — 进程内按文件体积/修改时间/路径特征缓存的已恢复密码
  3. `cloud` — **云端 MD5 实时查询**：仅上传压缩包 MD5 指纹，不上传任何文件内容，实时同步云端密码库
  4. `dict` — **在线字典实时拉取 + 动态发现**：纯内存解密不落盘，支持 `ETag` / `Last-Modified`（HTTP 304 条件刷新）以及 `data8.dt`+ 新分片自动发现
  5. `mask` — 可选的纯数字掩码搜索（4~8 位，默认关闭，可按需配置时间预算）
- **零信任本地校验**：所有来自云端或字典的候选密码都会在本地通过真实密码学完整性校验后才被采纳，杜绝误报或恶意响应：
  - **ZIP (ZipCrypto)**：12 字节加密头初筛 + 最小条目完整解压 CRC-32 校验
  - **ZIP (WinZip AES-128/192/256)**：PBKDF2-HMAC-SHA1 2 字节校验值 + 流式 10 字节 HMAC-SHA1 验证（无文件大小上限）
  - **7z（加密文件头 `-mhe=on` & 加密数据载荷）**：支持短 IV、多文件夹独立 KDF、非固实多文件档案及 AES + LZMA2 + BCJ/Delta 过滤链
  - **SFX 自解压包**：自动探测并兼容 ZIP / 7z 自解压 Stub 偏移
- **硬件加速 + 多核并发**
  - **7-Zip 原生 SHA-NI / ARMv8 汇编**：7z KDF 直接复用 7-Zip 硬件加速 SHA-256（`Sha256Opt.asm` / `Sha256Opt.c`），常规密码零堆分配
  - **Windows CNG 原生 PBKDF2**：WinZip AES 直接调用 `BCryptDeriveKeyPBKDF2` 并全局复用算法句柄
  - **多线程候选校验池**：对 7z / WinZip AES 的字典与掩码阶段自动启用多核并行（最高 16 线程），命中或取消瞬间原子中断
- **全目标覆盖**：`7zFM.exe`（图形文件管理器）、`7zG.exe`（右键解压/压缩弹窗）以及 `7z.exe` / `7za.exe` / `7zz.exe` / `7zr.exe`（命令行）全部生效。

---

### 2. 界面介绍（Bandizip 风格现代简约主窗口）

针对原版 7-Zip 界面年代感强、高分屏模糊、位图工具栏锯齿明显等问题，我们在**不引入 Electron / WebView2 / WinUI3 / GDI+ 等任何额外运行时依赖**的前提下，使用纯 Win32 GDI + DWM 对 `7zFM.exe` 主窗口进行了全面重构：

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ [图标] archive.7z - 7-Zip ZS                              [─] [□] [×]   │  ← Win11 圆角 & 深/浅色标题栏
├──────────────────────────────────────────────────────────────────────────┤
│ 文件  编辑  查看  收藏  工具  帮助                                       │
├──────────────────────────────────────────────────────────────────────────┤
│  ┌──────┐ ┌──────┐ ┌──────┐  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │  [+] │ │  [↑] │ │  [✓] │  │  │ [❐]  │ │ [→]  │ │ [🗑] │ │ [i]  │      │  ← 扁平矢量工具栏 (62dip)
│  │ 添加 │ │ 解压 │ │ 测试 │  │  │ 复制 │ │ 移动 │ │ 删除 │ │ 信息 │      │     圆角悬停底色 + 语义色图标
├──┴──────┴─┴──────┴─┴──────┴──┴──┴──────┴─┴──────┴─┴──────┴─┴──────┴──────┤
│ [↑]  │  D:  ›  Downloads  ›  archive.7z  ›  src                          │  ← 面包屑地址栏 (36dip)
├──────────────────────────────────────────────────────────────────────────┤
│  名称                         │ 大小      │ 压缩后    │ 修改日期         │  ← 扁平自绘表头 (30dip, 无3D边)
├───────────────────────────────┼───────────┼───────────┼──────────────────┤
│  📁  docs                     │           │           │ 2026/11/24 10:20 │  ← 现代双缓冲文件列表
│  📄  README.md                │  12.4 KB  │   4.2 KB  │ 2026/11/24 10:35 │     去除 3D 凹边, ClearType 字体
│  🔒  secret.bin               │ 128.0 KB  │ 128.0 KB  │ 2026/11/24 10:40 │     柔和选中与清晰层级
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│ 共 12 项  ·  已选 1 项 (12.4 KB)          [ PwFree: password database ]  │  ← 扁平状态栏 + PwFree 胶囊徽标
└──────────────────────────────────────────────────────────────────────────┘
```

#### 视觉与交互细节
| 模块 | 改造内容 |
| :--- | :--- |
| **高 DPI 与窗口框架** | 升级 Manifest 为 **`PerMonitorV2`**，完整响应 `WM_DPICHANGED`，在 100%~250% 缩放及多屏拖拽下始终锐利清晰；Windows 11 自动启用圆角窗口与深/浅色标题栏（旧版 Windows 优雅降级）。 |
| **扁平矢量工具栏** | 彻底替换旧版洋红掩码 BMP 图标，改为双缓冲纯 GDI 矢量绘制（添加/解压/测试/删除等带柔和语义色强调），支持圆角悬停高亮、按下反馈与大小图标/文字标签切换。 |
| **面包屑地址栏** | 替代经典 XP 风格 Rebar 下拉框：路径按层级拆分为可点击胶囊，压缩包节点以强调色高亮；点击右侧空白处或按快捷键可无缝切换为路径编辑框，点击列表或回车自动恢复面包屑态。 |
| **现代文件列表** | 移除经典 `WS_EX_CLIENTEDGE` 3D 凹陷边框，开启 `LVS_EX_DOUBLEBUFFER` 消除滚动闪烁，统一 `Segoe UI` 缓存字体池，表头重绘为 30dip 扁平风格与柔和 1px 分割线。 |
| **扁平状态栏与主题联动** | 重绘为 26dip 单层扁平状态栏，自动识别 `PwFree:` 解密来源并在右侧渲染圆角胶囊徽标；监听 `WM_SETTINGCHANGE` / `WM_THEMECHANGED`，切换系统浅色/深色模式时实时同步刷新。 |

---

### 3. 完整继承 7-Zip-ZS 全套现代压缩与加密算法

- **扩展压缩算法**：[Zstandard](https://facebook.github.io/zstd/) v1.5.7（1..22）、[Brotli](https://github.com/google/brotli) v1.2.1（0..11）、[LZ4](https://github.com/lz4/lz4) v1.11.1（1..12）、[LZ5](https://github.com/inikep/lz5) v1.5（1..15）、[Lizard](https://github.com/inikep/lizard) v2.1（10..49）、[Fast LZMA2](https://github.com/conor42/fast-lzma2) v1.0.1（1..9）
- **扩展加密算法**：7zAES、XChaCha20、XChaCha20-Poly1305、AES+XChaCha20-Poly1305 (AXP)、AES+XChaCha20+Ascon (AXA)、XChaCha20+AES+AEGIS (XAA)
- **扩展校验算法**：CRC32/64、MD2/4/5、SHA1/SHA2/SHA3、XXH32/64/XXH3、BLAKE2sp、BLAKE3

---

## 下载与安装

前往 [**Releases 页面**](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases) 获取最新预编译版本：

| 包名 | 说明 |
| :--- | :--- |
| **`7-Zip-ZS-PwFree-x64-setup.exe`** | Windows x64 原生安装包，包含完整 GUI、资源管理器右键菜单（`7-zip.dll`）、全部扩展编解码器与卸载程序 |
| **`7-Zip-ZS-PwFree-x64-portable.zip`** | Windows x64 便携版，解压即用，内置 `7zFM.exe`、`7zG.exe`、`7z.exe`、`7za.exe`、`7zz.exe`、`7zFMa.exe`、`7z.dll` 与 `Codecs\` |

---

## PwFree 配置项

可在 `7zFM` 菜单栏 **`工具 -> 选项 -> 设置`** 页面直接开关核心选项，所有参数持久化在注册表 `HKCU\Software\7-Zip-Zstandard\PwFree`：

| 注册表项 | 默认值 | 说明 |
| :--- | :---: | :--- |
| `Enabled` | `1` | PwFree 自动解密总开关 |
| `UseCloud` | `1` | 允许云端 MD5 实时密码查询 |
| `UseLocalDict` | `1` | 允许在线字典拉取与匹配（纯内存，带 ETag/304 刷新与新分片发现） |
| `ShowSource` | `1` | 解密成功后在状态栏/控制台显示密码来源 |
| `AllowHttpFallback` | `0` | HTTPS 失败时是否允许回退明文 HTTP（默认关闭以确保安全） |
| `CloudBudgetMs` | `3000` | 云端查询超时预算（毫秒） |
| `DictBudgetMs` | `120000` | 字典匹配时间预算（毫秒） |
| `MaskBudgetMs` | `0` | 4~8 位数字掩码搜索预算（毫秒，`0` 表示关闭） |

更多模块设计细节请参阅 [CPP/7zip/UI/Common/PwFree/README.md](CPP/7zip/UI/Common/PwFree/README.md)。

---

## 从源码构建

需要 Visual Studio 2022（MSVC v143）及 Windows SDK：

```cmd
:: 在 "x64 Native Tools Command Prompt for VS 2022" 中执行
set PLATFORM=x64
set VC=v143
set SUBSYS=6.00
set APPVEYOR_BUILD_FOLDER=%cd%
cd CPP
build-it.cmd -all
```

编译产物位于 `bin-v143-x64\`。项目已配置 GitHub Actions 自动构建 `x64` / `ARM64` / `x86`，推送 `v*` 标签即可自动执行 `7zFM.exe` 启动自检、打包并发布 GitHub Release。

---

## 致谢与许可

- [7-Zip](https://www.7-zip.org/) — Igor Pavlov
- [7-Zip-zstd (7-Zip-ZS)](https://github.com/mcmilk/7-Zip-zstd) & [zstdmt](https://github.com/mcmilk/zstdmt) — Tino Reichardt & 贡献者
- 本项目遵循上游开源协议，详见 [COPYING](COPYING)。
