<div align="center">

<img src="DOC/unlockzip-icon.png" alt="UnlockZip Logo" width="120" height="120" />

# UnlockZip

**极速启动 · 现代简约界面 · 自动云解密 · 全算法增强版开源解压工具**

[![Latest Release](https://img.shields.io/github/v/release/Xitaoyou/7-Zip-ZS-PwFree?include_prereleases&style=flat-square&color=0078D4&label=Download)](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases)
[![Windows CI](https://img.shields.io/github/actions/workflow/status/Xitaoyou/7-Zip-ZS-PwFree/build-windows.yml?branch=master&label=Windows%20CI&style=flat-square)](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/actions/workflows/build-windows.yml)
[![Platform](https://img.shields.io/badge/Windows-x64%20%7C%20ARM64%20%7C%20x86-0078D4?style=flat-square)](#下载与安装)
[![License](https://img.shields.io/badge/License-LGPL--2.1%20%2F%20BSD-2ea44f?style=flat-square)](COPYING)

**UnlockZip** 基于 [7-Zip Zstandard (7-Zip-ZS)](https://github.com/mcmilk/7-Zip-zstd) 深度重构：保留 7-Zip 原生极速与全套现代压缩算法（Zstd / Brotli / LZ4 / Lizard / Fast-LZMA2），内置 **PwFree 多核硬件加速自动云解密引擎**，并以纯 Win32 原生代码重做 **Bandizip 风格现代简约界面与 Win11 Fluent 图标**——冷启动 **< 30ms**、内存占用仅 **~8MB**、零广告、零额外运行时依赖。

[立即下载 Releases](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases) · [为什么选 UnlockZip](#为什么选择-unlockzip) · [核心功能](#核心功能) · [界面介绍](#界面介绍) · [隐私与配置](#隐私安全与配置)

</div>

---

## 为什么选择 UnlockZip

| 对比维度 | UnlockZip | 原版 7-Zip | 常见商业解压软件 |
| :--- | :---: | :---: | :---: |
| **加密包自动云解密（PwFree）** | ✅ 原生无感内置 | ❌ 无 | ⚠️ 付费专业版或无 |
| **现代简约界面 & 高 DPI** | ✅ PerMonitorV2 + 扁平矢量 UI | ❌ 复古界面 | ✅ 现代 UI |
| **启动速度与内存** | ✅ **< 30ms / ~8MB**（纯 Win32） | ✅ 极快 | ⚠️ 偏重或带后台进程 |
| **纯净无广告 & 开源免费** | ✅ 100% 开源无广告 | ✅ 开源无广告 | ❌ 免费版常含广告 |
| **Zstd / Brotli / LZ4 / Fast-LZMA2** | ✅ 完整内置 | ❌ 不支持 | ⚠️ 部分支持 |

---

## 核心功能

### 1. PwFree 自动云解密引擎（双击即解，零感体验）

打开或解压加密压缩包时，PwFree 在密码弹窗之前自动按耗时从低到高执行多级恢复链；若未命中或离线，则立即无缝回退至原生密码输入框：

1. **Seed / Cache（0ms）**：复用已知密码及进程内已恢复的档案缓存。
2. **Cloud 云端实时查询（~100ms）**：仅计算压缩包 MD5 指纹查询云端数据库，**绝不上传任何文件内容**，实时跟随云端新密码库更新。
3. **Dict 实时在线字典**：纯内存解密不落盘，支持 **`ETag` / `Last-Modified`（HTTP 304 条件刷新）** 与 **`data8.dt`+ 新分片自动发现**。
4. **Mask 可选掩码搜索**：支持 4~8 位数字掩码（默认关闭，可按需开启预算）。

#### 硬件级性能与零信任安全校验
- **零信任本地校验**：所有云端/字典候选密码必须通过本地真实解密与完整性校验后才被采纳：
  - **ZIP (ZipCrypto)**：12 字节加密头初筛 + 最小加密条目完整解压 CRC-32 校验。
  - **ZIP (WinZip AES-128/192/256)**：Windows CNG `BCryptDeriveKeyPBKDF2` + 流式 10 字节 HMAC-SHA1 验证（无体积上限）。
  - **7z（`-mhe=on` 加密头 & 加密载荷）**：复用 7-Zip 原生 **SHA-NI / ARMv8 硬件汇编**（`Sha256Opt`），兼容短 IV、多文件夹独立 KDF、非固实多文件与 AES + LZMA2 + BCJ/Delta 过滤链。
  - **SFX 自解压包**：自动兼容 ZIP / 7z 自解压 Stub 偏移。
- **多核候选校验池**：针对 7z / WinZip AES 的字典与掩码阶段自动启用多核并行（最高 16 线程），命中或取消立即原子中断。
- **全形态覆盖**：图形界面（`7zFM.exe`）、右键弹窗（`7zG.exe`）与命令行（`7z.exe` / `7za.exe` / `7zz.exe` / `7zr.exe`）全部生效。

---

### 2. 界面介绍（Bandizip 风格现代简约主窗口）

摒弃 Electron / WebView2 等重型框架，完全基于纯 Win32 原生绘制打造，在保持毫秒级冷启动的同时焕然一新：

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ [🔒] archive.7z - UnlockZip                               [─] [□] [×]   │  ← Win11 圆角 & 深/浅色标题栏
├──────────────────────────────────────────────────────────────────────────┤
│ 文件  编辑  查看  收藏  工具  帮助                                       │
├──────────────────────────────────────────────────────────────────────────┤
│  ┌──────┐ ┌──────┐ ┌──────┐  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  ┌──┐│
│  │  [+] │ │  [↑] │ │  [✓] │  │  │ [❐]  │ │ [→]  │ │ [🗑] │ │ [i]  │  │⚙ ││  ← 2x 超采样抗锯齿矢量工具栏
│  │ 添加 │ │ 解压 │ │ 测试 │  │  │ 复制 │ │ 移动 │ │ 删除 │ │ 信息 │  │设置││    圆角悬停 + 语义色 + 右侧设置
├──┴──────┴─┴──────┴─┴──────┴──┴──┴──────┴─┴──────┴─┴──────┴─┴──────┴──┴──┘
│ [↑]  │  D:  ›  Downloads  ›  archive.7z  ›  src                          │  ← 面包屑地址栏 (点击空白变输入)
├──────────────────────────────────────────────────────────────────────────┤
│  名称                         │ 大小      │ 压缩后    │ 修改日期         │  ← 30dip 扁平自绘表头 (无3D边)
├───────────────────────────────┼───────────┼───────────┼──────────────────┤
│  📁  docs                     │           │           │ 2026/11/24 10:20 │  ← 双缓冲现代文件列表
│  📄  README.md                │  12.4 KB  │   4.2 KB  │ 2026/11/24 10:35 │     去除 3D 凹边, ClearType 字体
│  🔒  secret.bin               │ 128.0 KB  │ 128.0 KB  │ 2026/11/24 10:40 │     柔和选中层级与丝滑滚动
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│ 共 12 项  ·  已选 1 项 (12.4 KB)          [ PwFree: password database ]  │  ← 扁平状态栏 + PwFree 胶囊徽标
└──────────────────────────────────────────────────────────────────────────┘
```

- **全新 Fluent 品牌图标**：蓝青渐变圆角锁 + 拉链多尺寸透明 `.ico`（16~256px），覆盖主程序、安装器、卸载器与关于页。
- **2x 超采样抗锯齿矢量工具栏**：告别旧版洋红掩码 BMP，工具栏右侧直接集成快捷 **Options（设置）** 入口。
- **双态面包屑地址栏**：路径按层级显示为可点击胶囊，压缩包节点自动高亮；点击空白处或按快捷键立即切换为路径编辑框，点击列表自动复原。
- **PerMonitorV2 高 DPI & 主题联动**：100%~250% 多屏缩放锐利清晰，监听系统主题变化实时切换浅色 / 深色配色。

---

### 3. 全套现代压缩、加密与校验算法（源自 7-Zip-ZS）

- **压缩算法**：Zstandard v1.5.7、Brotli v1.2.1、LZ4 v1.11.1、LZ5 v1.5、Lizard v2.1、Fast-LZMA2 v1.0.1，以及标准 7z / ZIP / RAR5(解压) / TAR / WIM / ISO 等数十种格式。
- **加密算法**：7zAES、XChaCha20、XChaCha20-Poly1305、AXP / AXA / XAA 级联认证加密。
- **哈希校验**：CRC32/64、MD5、SHA-1/2/3、XXH32/64/XXH3、BLAKE2sp、BLAKE3。

---

## 下载与安装

前往 [**Releases 页面**](https://github.com/Xitaoyou/7-Zip-ZS-PwFree/releases) 下载最新版本（每次发布均经过 CI 自动构建 + 静默安装烟测 + GUI 启动自检）：

| 发行包 | 适用场景 |
| :--- | :--- |
| **`UnlockZip-x64-setup.exe`** | **推荐**：Windows x64 原生安装包，自动注册 `UnlockZip` 资源管理器右键菜单、开始菜单快捷方式与卸载程序 |
| **`UnlockZip-x64-portable.zip`** | Windows x64 便携绿色版，解压即用，内含完整 GUI、CLI 工具及全部 `Codecs\` 插件 |

---

## 隐私安全与配置

- **隐私说明**：仅在开启 `UseCloud` 时发送压缩包的 **MD5 摘要**（单向哈希），**不会上传文件名、目录结构或任何压缩包内容**；HTTPS 默认禁止静默降级为明文 HTTP。
- **界面开关**：打开 UnlockZip → 点击工具栏右侧 **Options（设置）** → **Settings** 页面底部即可一键开关 PwFree 各项功能。
- **高级注册表参数**（`HKCU\Software\7-Zip-Zstandard\PwFree`）：

| 参数 | 默认值 | 说明 |
| :--- | :---: | :--- |
| `Enabled` | `1` | PwFree 自动解密总开关 |
| `UseCloud` | `1` | 允许云端 MD5 实时密码查询 |
| `UseLocalDict` | `1` | 允许在线字典匹配（纯内存 + ETag/304 刷新 + 动态分片发现） |
| `ShowSource` | `1` | 在状态栏胶囊/控制台显示恢复密码的来源 |
| `AllowHttpFallback` | `0` | HTTPS 失败时是否允许回退明文 HTTP（默认关闭） |
| `CloudBudgetMs` | `3000` | 云端查询超时预算（毫秒） |
| `DictBudgetMs` | `120000` | 字典匹配时间预算（毫秒） |
| `MaskBudgetMs` | `0` | 4~8 位数字掩码搜索预算（毫秒，`0` 为关闭） |

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

推送 `v*` 标签即可通过 GitHub Actions 自动完成全架构编译、GUI 启动烟测、安装器静默测试并发布正式 Release。

---

## 致谢与许可

- [7-Zip](https://www.7-zip.org/) — Igor Pavlov
- [7-Zip-zstd (7-Zip-ZS)](https://github.com/mcmilk/7-Zip-zstd) & [zstdmt](https://github.com/mcmilk/zstdmt) — Tino Reichardt & 贡献者
- 本项目遵循上游开源协议（LGPL-2.1 / BSD），详见 [COPYING](COPYING)。
