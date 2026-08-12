# Luban SDK

匠芯创（ArtInChip）Linux SDK，基于 Buildroot 系统深度定制和优化，支持 OpenSBI + U-Boot + Linux-5.10 启动链。

## 架构概览

### 启动链层级

```
┌─────────────────────────────────────────────────────┐
│ OpenSBI (source/opensbi/)                           │
│   RISC-V S-mode 固件，提供 SBI 接口                    │
├─────────────────────────────────────────────────────┤
│ U-Boot (source/uboot-2021.10/)                      │
│   Bootloader，负责硬件初始化、加载内核                   │
├─────────────────────────────────────────────────────┤
│ Linux Kernel (source/linux-5.10)                │
│   Linux-5.10 内核，含 AIC 自研驱动                 │
├─────────────────────────────────────────────────────┤
│ Rootfs (package/)                                   │
│   构建的根文件系统，含自研中间件和测试程序                  │
└─────────────────────────────────────────────────────┘
```

### AIC 驱动架构

AIC 驱动遵循 Linux 内核子系统规范，分布在不同子系统目录中。
下面是部分驱动的目录：

```
Linux Kernel
  ├── drivers/crypto/artinchip/      ← 加密引擎
  ├── drivers/clk/artinchip/         ← 时钟框架
  ├── drivers/pinctrl/artinchip/     ← 引脚复用
  ├── drivers/phy/artinchip/         ← MIPI D-PHY
  ├── drivers/media/platform/artinchip/ ← DVP
  ├── drivers/net/ethernet/artinchip/ ← 以太网
  ├── drivers/net/can/artinchip/     ← CAN
  ├── drivers/video/artinchip/       ← DE/GE/VE
  └── sound/soc/artinchip/           ← 音频
```

### 支持的 SoC（仅对内）

| 内部编号 | Luban 版本 | OpenSBI | U-Boot | Linux |
|----------|-----------|---------|--------|-------|
| D211 |  V1.0 | 0.9  | 2021.10 | 5.10 |

## 构建简介

### 方式一：常规 make 命令

```bash
make list                           # 查看所有 defconfig
make d211_demo128_nand_defconfig    # 选择配置
make                                # 编译全部并生成镜像
```

### 方式二：OneStep 快捷命令

```bash
source tools/onestep.sh             # 初始化环境，一步直达命令
lunch d211_demo128_nand_defconfig   # 选择配置
m                                   # 编译全部并生成镜像
```

### 其他常用命令

| 命令 | 说明 |
|------|------|
| `mm` | 编译当前路径所在的 package |
| `me` | 打开 Buildroot 配置菜单 |
| `km` | 打开 Linux 内核配置菜单 |
| `um` | 打开 U-Boot 配置菜单 |
| `make s` | 单独编译 OpenSBI |
| `make u` | 单独编译 U-Boot |
| `make k` | 单独编译 Linux 内核 |
| `m` | 编译全部模块并生成最终镜像 |
| `c` | 清理全部编译输出 |
| `buildall` | 编译检查所有defconfig |
| `rebuildall` | 先clean再build的方式，编译检查所有defconfig |
| `h` | 查看所有可用命令 |

编译输出位于 `output/` 目录，最终镜像在 `output/images/` 中。

## 编码规范

本 SDK 沿用 Linux/u-Boot 编码规范，详见：
https://www.kernel.org/doc/html/latest/process/coding-style.html

### Git 提交规范

 提交的描述信息需要按照下面的格式进行

 - 全部使用英文
  - _标题_： 模块名字：小于 50 字符的简述
  - _内容_： 对所解决的问题的详细描述，最好包括背景，解决方法，以及其他有用的信息。
  - _脚注_： 额外的提醒，比如一些不兼容等，需要特别注意的事项；解决的bug的链接等。
  - _标题_ 与 _内容_ 之间应该要有一行空白
  - _内容_ 与 _脚注_ 之间应该要有一行空白

```
HEADER: <Module>: <Short description>
BLANK :
BODY  : <Detail description about this commit>
BLANK :
FOOTER: <Addtional information>
```

## 文档索引

- [AGENTS.md](AGENTS.md) — AI Agent 上下文文档
- [CODEMAP.md](CODEMAP.md) — 源码文件地图
- [CONTRIBUTING.md](CONTRIBUTING.md) — 开发流程 Checklist

## 在线文档

- SDK文档：https://aicdoc.artinchip.com/topics/sdk/luban-user-guide-luban.html

## 版权

Copyright (c) 2020-2026, ArtInChip Technology Co., Ltd
