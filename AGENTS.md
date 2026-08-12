# Luban SDK — AI Agent Context

> 本文件为 AI 辅助编程工具提供项目上下文，请优先阅读。

## 一句话描述

Luban 是匠芯创（ArtInChip）的 Linux SDK，基于 Buildroot 系统深度定制和优化，支持 OpenSBI + U-Boot + Linux-5.10 启动链，覆盖的SoC有：

- Luban V1.0
   - d211


## 构建命令

| 命令 | 说明 |
|------|------|
| `source tools/onestep.sh` | 初始化构建环境 |
| `lunch <keyword>` | 按关键字筛选并选择 defconfig，如 `lunch mmc`; 不加参数会进入交互式选择 |
| `m` | 编译全部模块并生成最终镜像 |
| `c` | 清理全部编译输出 |
| `mm` | 编译当前路径所在的package |
| `me` | 打开 Buildroot 配置菜单 |
| `km` | 打开 Linux 内核配置菜单 |
| `um` | 打开 U-Boot 配置菜单 |
| `make s` | 单独编译 OpenSBI |
| `make u` | 单独编译 U-Boot 系统 |
| `make k` | 单独编译 Linux 内核 |
| `buildall` | 编译检查所有defconfig |
| `rebuildall` | 先clean再build的方式，编译检查所有defconfig |
| `h` | 查看所有可用命令 |

## 目录结构速查

| 目录 | 功能 | 备注 |
|------|------|------|
| `source/linux-5.10/` | Linux 5.10 内核 | Luban V1.0 |
| `source/opensbi/` | OpenSBI |  |
| `source/uboot-2021.10/` | U-Boot 2021.10 |  |
| `source/artinchip/` | AIC 自研工具与测试程序 |  |
| `package/` | Buildroot 包管理 |  |
| `target/` | 板级配置与设备树 |  |
| `tools/` | 构建脚本与工具 |  |
| `prebuilt/` | 预编译工具链 |  |
| `dl/` | Buildroot 下载缓存 |  |
| `doc/` | SDK 文档（含根目录文档源文件） |  |
| `output/` | 编译输出 |  |

> 根目录的 AGENTS.md / README.md / CODEMAP.md / CONTRIBUTING.md 均为 `doc/` 仓库的符号链接，由 Repo linkfile 管理。

## 启动链

```
OpenSBI (source/opensbi/)
  └─ U-Boot SPL → U-Boot Proper (source/uboot/)
      └─ Linux Kernel + DTB (source/linux)
          └─ Rootfs (Buildroot, package/)
```

## AIC 驱动

1. AIC驱动 在 Linux、U-Boot 中的位置都符合内核社区代码的目录规范。
2. DTS文件的位置比较特殊，我们将其单独存放，位于 `target/soc/boards/` 目录下，并将其分为两份：
   - `target/soc/common/soc.dtsi`：包含通用的芯片级设备信息，如时钟、Register、IRQ等定义。
   - `target/soc/board/board.dts`：包含board级别的设备信息，如 GPIO、Frequency、绑定关系等。

## 关键类型与宏

Luban 使用 Linux、U-Boot 原生的标准类型定义。

## Repo 多仓库管理（对内）

Luban 使用 Repo 管理多个 Git 仓库：

```bash
repo init -u <manifest-url> -m luban-2.0.xml -b luban-dev
repo sync
repo start luban-dev
```

Manifest 文件位于 `.repo/manifests/`，定义了各仓库的路径和分支。V1.0 和 V2.0 通过不同的 manifest 文件区分。

## 更多文档

- [CODEMAP.md](CODEMAP.md) — 源码文件地图
- [README.md](README.md) — 架构与编码规范
- [CONTRIBUTING.md](CONTRIBUTING.md) — 开发流程

## 在线文档

- SDK文档：https://aicdoc.artinchip.com/topics/sdk/luban-user-guide-luban.html
