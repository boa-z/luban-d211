# D211 / D70T-NAND Linux 6.18 移植开发文档

> Luban SDK 上 `target/d211/d70t_nand` 开发板从 Linux 5.10 迁移到 Linux 6.18 LTS
> 的完整记录：状态、根因分析、API 适配、现场验证手册与后续工作。
> 分支：`feature/linux-6.18-lts`，内核源码：`source/linux-6.18`（独立仓库，
> 基线 `v6.18.52`，见 1.4）。

---

## 1. 移植概览

### 1.1 验证状态总表（截至 2026-09-16）

| 功能 | 状态 | 关键提交 / 备注 |
|------|------|-----------------|
| 启动 / SPI NAND / UBI rootfs | ✅ 上板验证 | `5e386dec0` 等，启动约 11s |
| 串口（8250 AIC）/ clk / reset / pinctrl | ✅ | M1 |
| 显示（DE/RGB/panel/GE/VE/fb，LVGL demo） | ✅ | `ddb853842` + `de6893772`（花屏修复） |
| 触摸（GT911）/ 背光（PWM） | ✅ | `a6da67b7b`、`70ab88e99` |
| ADB（USB gadget UDC） | ✅ | 根因是 DMA 一致性，见 2.2.4 |
| TF 卡（SDMC1） | ✅ | 16GB 卡枚举/挂载正常 |
| 4G（EG800K，USB1 host + RNDIS） | ✅ | DHCP 192.168.43.100；脚本 `S92lte` |
| WiFi（AIC8800D80，SDMC2） | ✅ | 冷启动枚举 + 关联 + DHCP；脚本 `S91wifi` |
| CE 加密引擎 | ✅ 注册/启动；AES 待复测 | `02b229623` + `dc68dcd97`（panic 修复） |
| 音频（内部 codec + PA） | ⏳ 待听音确认 | `78e8e7d12`、`236d91726`、`6cbf3d26d` |
| thermal（tsen） | ⏳ 读数 0，待 5.10 对照 | `a595df1f2`、`944c895fc` |
| PM 挂起 | 已移植未验证 | `546b0f6be` |
| MPP dma-buf heap / 私有 ioctl | ✅ | `fb9cab674`、`c2492b1d1` |

> 上表这些结论**全部基于 6.18.0 基线**上板验证。内核已 rebase 到 **6.18.52**
> （提交序列与改动内容逐字节一致：427 文件 / 172874 插入行），并已通过编译与
> SDK 全量构建；**6.18.52 镜像尚未上板回归**，回归清单见 6.10。

### 1.1.1 暂无硬件验证条件的项（编译通过，未上板）

| 功能 | 缺的硬件/条件 | 备注 |
|------|---------------|------|
| SPI-NOR 加密钩子 | 无 NOR Flash 可测（参考板只有 NAND） | enc 读写分支只走编译；`aic,encrypt` 默认关闭 |
| USB WiFi（AIC8800 USB） | 无 USB 网卡 | get_tx_power 按 SDIO 侧同改 mirror；vendor bulk 未动 |
| DVP 摄像头 + GM7150/XS9950 | 无 sensor 模组 | `MEDIA_SUPPORT` 整机关闭，属死代码 |
| EPWM | 无外设用户 | 刚迁到新 PWM API（apply），仅编译验证 |
| CIR | DT 里 disabled，无遥控器 | 驱动在，默认不使能 |
| I2C slave | 需外部 master | 驱动在，默认不使能 |
| SPIENC 运行时 | 需 NOR + 加密镜像联调 | 见上 |
| PM 挂起 | 待一条命令 | `echo mem > /sys/power/state` 即可测，已接线待跑 |
| GE 真加速 | 待查 `/dev/ge` | probe 侧刚修完 misc 注册顺序，fillrect 测试待写 |
| RISCV_CPUIDLE / CRYPTO_USER_API_AKCIPHER | 评估后不做 | 5.10 有、6.18 无的仅剩两项有用符号；前者需 SBI 配合验证，后者暂无 RSA 用户态测试需求，保持现状 |

### 1.2 构建链关键事实

- 内核目录由版本号决定：`source/linux-<BR2_LINUX_KERNEL_VERSION>`；本移植用
  `6.18` → `source/linux-6.18`（`O=` 外置编译，原地改源码即生效）。
- 内核独立成 git 仓库后，`CONFIG_LOCALVERSION_AUTO=y` 会让版本串带上 git describe
  后缀，例如 `6.18.52-00056-gbc235454d2c8`（= v6.18.52 + 56 个移植提交）。该串是
  `uname -r`，也是模块安装路径 `/lib/modules/<该串>`；SDK 通过
  `make kernelrelease` 动态取值（linux.mk 的 `LINUX_VERSION_PROBED`），**无需改动**。
  升级基线/改提交数后，注意清理 target 里遗留的旧版本模块目录
  （`output/d211_d70t_nand/target/lib/modules/`），否则会打进 rootfs。
- **DTB 由 U-Boot 编译**：`package/uboot/uboot.mk` 把 `target/d211/<board>/board.dts`
  链为 `artinchip-board.dts`，内核通过 FIT 复用 `u-boot.dtb`；内核不需要 DTS。
  改 DTS 后需重建 U-Boot（删除 `output/*/build/uboot-2021.10/arch/riscv/dts/artinchip-board.dtb*`）。
- 内核 `include/dt-bindings/{clock,reset,display,dma,pinctrl}/artinchip*` 会被
  U-Boot 与 `post-image.sh` 使用，必须存在于 6.18 内核树。
- **SDK kconfig stamp 坑**：改 defconfig 后需删除
  `output/<board>/build/linux-6.18/{.stamp_dotconfig,.stamp_kconfig_fixup_done,.stamp_configured}`，
  否则新符号会被静默置 N。
- `target/configs/configs_linux` 是 5.10 的软链接，6.18 的 defconfig 只维护在
  `source/linux-6.18/arch/riscv/configs/d211_d70t_nand_defconfig`。

### 1.3 构建与调试命令

```bash
# 快速内核迭代（编译级）
export PATH=$PWD/output/d211_d70t_nand/host/bin:$PATH
make -C source/linux-6.18 ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     O=/tmp/k61852 -j96 Image modules

# SDK 全量（推荐，产出最终镜像）
source tools/onestep.sh
lunch d70t          # 选 d211_d70t_nand
rm -f output/d211_d70t_nand/build/linux-6.18/.stamp_{dotconfig,kconfig_fixup_done,configured}
m                   # 全量；make k / make u / make s 单独重建

# 最终镜像
output/d211_d70t_nand/images/d211_demo88_nand_page_2k_block_128k_v1.0.0.img
# 历史备份见附录 B
```

> 板上 rootfs **没有 `head`/`tail`/`ip`/`cmp`/`iw`/`wpa_cli`**，
> 写脚本/命令时用 `awk`、`/proc/net/dev`、`ifconfig`、`md5sum` 替代。

---

### 1.4 内核源码：独立仓库管理（快照式）

内核树已从 Luban 主仓拆出，作为独立仓库维护，工作树仍是 SDK 内的
`source/linux-6.18`（与 `source/linux-5.10` 命名一致；`package/linux/linux.mk`
由 `BR2_LINUX_KERNEL_VERSION` 推导 `source/linux-$(LINUX_VERSION)`，
`post-image.sh` 从配置读取同一版本串，因此 SDK 侧无需其它改动）。

**仓库形态：每个 stable 基线一个「vanilla 导入提交」+ 56 个移植提交，
不携带 Linux 官方全量历史**（厂商 BSP 内核的常见做法，也贴合本 SDK
`source/linux-5.10` 本身就是源码快照的现状）：

```
573e4b4f97db  import: Linux 6.18.52 LTS vanilla source   ← 快照根 = 官方 v6.18.52 的树
  └─ 56 个 ArtInChip D211 移植提交
      └─ 82ce28ef6948  cleanup: drop leftover script variables ...   (= artinchip-6.18-lts)
```

为什么不用全量 upstream 历史（实测数据）：

| 形态 | 推送体积 | GitHub 单次 2 GiB 限制 | 仓库内上游 git 历史 |
|------|----------|------------------------|---------------------|
| 全量 upstream 历史（已否决） | 3.69 GiB | **超限**，需分几十段推，实测频繁 408 / TLS 断连 | 有 |
| **快照导入（当前采用）** | **~0.2 GiB** | 一次推完 ✓ | 无（按需从 kernel.org 取到本地） |
| fork 官方镜像（如 `gregkh/linux`） | 依赖 fork 网络对象去重，可能更小 | 受 fork 关系约束 | 有 |

> 快照根与官方 `v6.18.52^{tree}` **树哈希完全相同**（`6ad8bdff7057…`），内容可逐字节
> 对照官方源码；只是不带官方提交链。需要 `git log/blame/bisect` 查上游时，用 `stable`
> remote 按需 `git fetch`（本机另外保留了一份完整血缘分支，见下表）。

| 项目 | 值 |
|------|----|
| 仓库 | `https://github.com/boa-z/d211-linux-6.18`（public） |
| 本地工作树 | `source/linux-6.18` |
| 提交身份 | `boa-z <boa-z@outlook.com>` |
| upstream remote | `stable` = kernel.org stable（**仅按需 fetch 对象**，不做基线父链） |
| 主分支 | `artinchip-6.18-lts` = 6.18.52 快照 + 56 个移植提交（共 57 提交） |
| 当前 tag | `v6.18.52-d211-r1`（编译 + SDK 全量构建通过，**待上板回归**） |
| 回退 tag | `v6.18.0-d211-r1`（6.18.0 快照 + 同样 56 提交，已上板验证） |
| 快照根 tag（本地） | `vanilla-6.18.52`（rebase 的 upstream 基准，随新基线换代） |
| 完整血缘（**仅本地不推送**） | `artinchip-6.18-lts-full` = 真实 v6.18.52 父链 + 56 提交，供查史/bisect |

主仓「摘除」内核的方式（已完成，提交 `b75ac9695`）：`git rm -r --cached
source/linux-6.18-aic` + `.gitignore` 增加 `/source/linux-6.18`；主仓历史里旧的
`source/linux-6.18-aic` 移植提交保留可追溯，之后的改动只进内核仓库。

新环境（或 Repo manifest）接入内核仓库：

```bash
# 独立 clone（工作树就放在 SDK 的 source/linux-6.18）
git clone https://github.com/boa-z/d211-linux-6.18 source/linux-6.18
```

```xml
<!-- .repo/manifests 片段 -->
<project name="d211-linux-6.18" path="source/linux-6.18"
         revision="artinchip-6.18-lts" />
```

升级 stable 基线（换快照根 + rebase 移植提交）：

```bash
cd source/linux-6.18
export https_proxy=http://localhost:7897 http_proxy=http://localhost:7897   # mihomo（实际端口 7897）
git fetch stable tag v6.18.XX                # 只取新 tag 的对象，本地大部分已有
tree=$(git rev-parse v6.18.XX^{tree}); up=$(git rev-parse v6.18.XX^{commit})
newroot=$(git commit-tree "$tree" -m "import: Linux 6.18.XX LTS vanilla source

Vanilla Linux 6.18.XX snapshot (upstream commit $up, tag v6.18.XX).")
git tag -f vanilla-6.18.XX "$newroot"
git rebase --onto vanilla-6.18.XX vanilla-6.18.52 artinchip-6.18-lts
# 解决少量冲突 -> 编译 -> SDK 全量构建 -> 上板回归（启动/NAND/显示/ADB/WiFi/4G/TF/音频）
git tag -a v6.18.XX-d211-rN -m "..."
git push origin artinchip-6.18-lts v6.18.XX-d211-rN   # 单次 ~0.2 GiB，不触发 2 GiB 限制
```

注意事项：仓库内必须保留 `include/dt-bindings/{clock,reset,display,dma,pinctrl}/artinchip*`
（U-Boot 编译 DTB 时链接这些头文件）；`target/configs/configs_linux` 是指向 5.10 的
软链接（历史约定），6.18 的 defconfig 只维护在
`source/linux-6.18/arch/riscv/configs/d211_d70t_nand_defconfig`。

## 2. 关键问题与根因（最有价值的部分）

### 2.1 M1 启动链

#### 2.1.1 D211 SPI 控制器需要 U-Boot 式总线初始化

- 现象：SPI NAND `probe ... error -110`，无 AIC 驱动自身超时打印 →
  `spi_mem_poll_status()` 一直读到 BUSY（RX 通路恒 0）。
- 根因：AIC Linux 驱动只做 CMU 硬复位并配置一次，缺少 U-Boot 的三件事：
  1. 每次传输前清 **TCR 高 16 位**（bit16~25，硬件置位的"粘滞"位，不清则 RX 恒 0）；
  2. **GCR 软复位（SRST）**；
  3. `DCR |= 0x1F`、`PHC |= BIT12|BIT15`。
  另 `spi_ctlr_set_clk()` 的 CDR2 分支漏写 `div`。
- 修复：`drivers/spi/spi-artinchip.c`（`aic_spi_mem_exec_op` 首次调用做 U-Boot 式
  软复位 + 全量重配，之后每次传输前清粘滞位并重读 READ ID 直到 `0xEF`）。
  内核识别到 **Winbond W25N01KV（`EF AA 21`）**。
  - 2026-09-16 补记（6.18.52 回归）：M1 的清除掩码漏了 **TCR BIT(10)（RPSM）**——
    内核与 U-Boot 都不编程该位，CMU 硬复位与 GCR 软复位都不清它；一旦 U-Boot
    交接时它是 1（与所读 kernel.itb 尺寸/模式有关，换个镜像就时好时坏），之后
    所有 PIO 读都采样恒零但传输正常完成（`unknown raw ID 0000000000`，
    `-95`）。定位时能跑版与全零版 try 时刻寄存器仅差这一位（`0x49c4` vs
    `0x4dc4`）。修复：在 `aic_spi_hw_init` 清一次 + 重试循环掩码加上 `BIT(10)`。

#### 2.1.2 `riscv,isa` 丢失 I/M/A/F/D（用户态 SIGILL）

- 6.18 的 ISA 解析器不认识 `g` 缩写、忽略 vendor 部分，`rv64gcxthead` 只解析出 `c`
  → FPU 未启用 → `init` 浮点指令 SIGILL。
- 修复（`target/d211/common/d211.dtsi`）：
  ```dts
  riscv,isa = "rv64imafdcxthead";
  riscv,isa-base = "rv64i";
  riscv,isa-extensions = "i", "m", "a", "f", "d", "c";
  ```

#### 2.1.3 PD6 引脚冲突（UART1 vs 电源键）

`board.dts` 的 `&uart1`（PD6/PD7，SoC↔MCU 链路）与 `gpio-keys` 电源键（PD6）冲突。
按硬件实际用途保留 UART1、删除电源键节点（`0bbae4913`）。

### 2.2 USB / ADB

最终根因是 **2.2.4 DMA 一致性**；前三项是期间暴露的真实缺陷（已修）。

1. **`platform_get_irq()`**（`1125e24e7`）：6.18 的 `of_device_alloc()` 不再从 DT
   生成 `IORESOURCE_IRQ`，`platform_get_resource(..., IORESOURCE_IRQ, 0)` 恒 NULL。
   已排查全部 AIC 驱动，只有 `aic_udc.c` 用了旧接口。
2. **不能清 `driver->driver.bus`**（`8ca9d1fc6`）：6.18 新增 gadget bus，
   `aic_gg_udc_start()` 里 5.10 时代的 `driver->driver.bus = NULL` 会让
   `module_add_driver()` 空指针 oops 并死锁 configfs。
3. **不能把 UDC 的 of_node 复制给 gadget 设备**（`7544fe16b`）：gadget 设备挂在
   gadget bus 上会被 probe，`pinctrl_bind_pins()` 按该 of_node 再次申请 UDC 引脚 →
   重绑必然冲突。
4. **真正根因：6.18 RISC-V 默认把设备当 DMA-coherent**（`de6893772`）。
   5.10 无 `ARCH_DMA_DEFAULT_COHERENT`，而 6.18 `select` 了它；DTS 未声明
   `dma-coherent`/`dma-noncoherent` 时 `of_dma_is_coherent()` 回退为 true →
   设备被当成硬件一致：`dma_alloc_attrs()` 返回**带缓存**内存（不走
   DMA_DIRECT_REMAP 的 uncached 映射）、`dma_sync_*` 变空操作。
   表现：UDC 描述符/状态数据随机陈旧（枚举失败、`bLength=0`、主机误判 Low-Speed）；
   同一根因还导致显示花屏（见 2.3.1）。
   **修复：`d211.dtsi` 根节点加 `dma-noncoherent;`**（6.18 新 DT 属性，
   `of_dma_is_coherent()` 会向上遍历）。设备恢复 non-coherent 语义：DMA 走
   uncached 重映射，缓存维护走 `arch_sync_dma_*()` → T-Head CMO errata
   （`CONFIG_ERRATA_THEAD_CMO=y`，`dcache.cpa/cipa`）。
   修复后 ADB/显示/4G 全部恢复正常。

### 2.3 显示 / 图形

#### 2.3.1 花屏 = 2.2.4 的 DMA 一致性（`de6893772`）

DE/RGB/panel 全部绑定、时序寄存器与 5.10 逐项一致、DE 内部彩条同样花屏 →
内存通路问题。修复见 2.2.4。

#### 2.3.2 LVGL 图片不显示（白屏+文字）= 缺 MPP dma-buf heap（`fb9cab674` + `c2492b1d1`）

LVGL v9 的 AIC 驱动全部经 GE2D（`/dev/ge`）绘图、MPP（`/dev/aic_ve`）解码，
缓冲区来自 **`/dev/dma_heap/mpp`** 并用 AIC 私有 ioctl `DMA_BUF_IOCTL_GET_PHY_ADDR`
取物理地址、`DMA_BUF_IOCTL_SYNC(_RANGE)` 做缓存维护。已移植（保持 5.10 ABI）：
`heap-helpers`/`mpp_heap`（`vmap/vunmap` 适配 `iosys_map`；`allocate` 返回
`struct dma_buf *`；无 `destroy_heap`）、`get_phy_addr` 回调 + 通用 sg 兜底、
`SYNC_RANGE` 走 `arch_sync_dma_for_{device,cpu}()`；
defconfig `CMA_MPP_SIZE_MBYTES=12` + `DMABUF_HEAPS_MPP=y`。
另修 `mpp_heap` 初始化顺序（`f4dc2eb26`，先建池再发布 heap，避免 UAF）。

### 2.4 音频

1. **DMA 引擎未启用**（`fd9a63d4a`）：codec 注册 PCM 需要 dmaengine，
   启用 `ARTINCHIP_DMA/DDMA`（与 5.10 一致，SPI0 走专用通道）。
2. **codec component probe 空指针**（`236d91726`）：平台驱动从不
   `platform_set_drvdata()`，component probe 用 `dev_get_drvdata()` 拿到 NULL →
   codec 绑卡即 oops。改为驱动内全局指针 `g_aic_codec`。
3. **无声 = 板级 ALSA 状态把混音开关关了**（`6cbf3d26d`）：
   `/var/lib/alsa/asound.state` 中 `MIXER0 audoutl switch`、`MIXER1 audoutr switch`
   为 `false`，开机 `S70audiocfg` 恢复后左右各切一路 → 基本无声。已把四个
   AUDOUT 开关置 on。
4. 说明：`AUDIO Playback Volume` 寄存器默认 `0x7FFF7FFF`（最大）✓；
   DAPM 路由 `AUDOUTL/R → MIXER0/1 → DVC3/4 → IF → FADE → SDM → PWM → SPK_OUT → Speaker`
   完整 ✓；PA 使能事件 `aic_codec_spk_event` 正常 ✓。

### 2.5 WiFi / 4G

1. **UART5(RS485) 占 PF0/PF1 与 SDMC2 冲突**：会让 sdmc2 probe -22；
   按硬件实际（RS485 由 MCU 驱动）禁用 `&uart5`（`30fa32a79`）。
2. **4G 模组上电**：`GPF_P7` 需 gpio-hog `output-high`，否则模组不上电
   （`30fa32a79`）；USB1 为 host-only（`&usbh1`+`&ohci1`、`usb1_pins_a`、
   `aic,usb-ext-resistance=0x40`）。
3. **SDIO 冷启动枚举失败 = 非可移除卡只扫描一次**（`944c895fc`）：
   AIC8800 冷上电后需数秒才应答 CMD5，而 `non-removable` 的卡内核只探测一次
   （~0.8s）→ 永远失败（表现为"重启后才好"）。改为 **`broken-cd`**（MMC 核心每秒
   轮询重扫）后冷启动即可枚举（`/sys/bus/sdio/devices/mmc1:390b:1/2`，
   模组 AIC8800D80，SDIO vendor `0xc8a1`）。
4. **SDMC 驱动 unbind 崩溃**（`ce6bf5be0` + `543368966`）：驱动的 remove 未取消
   `cto/dto/cmd11` 超时定时器，解绑后定时器在已释放内存上触发（UAF panic）。
   已修 remove 并移除用户态重绑兜底——**禁止**对 SDMC 驱动做 unbind/bind。
5. 用户态：`S91wifi`（等 wlan0→wpa_supplicant `-D nl80211`，配置**不带
   ctrl_interface**→等 carrier→`udhcpc -b` 常驻续约）、`S92lte`（usb0 + udhcpc）、
   `d211-network.sh`（`D211_SSID`/`D211_PSK` 生成配置）。提交 `0cf80875b`、
   `fc22b32a9`。
6. 4G 数据面：出厂 `usbnet=3`(RNDIS) → `rndis_host` 自动绑定 usb0；
   `option` 无 EG800K VID/PID(`2c7c:6002`)，需要 AT 口时用 `new_id` 动态绑定。

### 2.6 加密引擎（CE）

1. **hash 注册全部 -EINVAL**：6.18 的 `hash_prepare_alg()` 明确禁止 hash 算法设置
   `cra_alignmask`（"not useful for hashes"），驱动 8 个 ahash 都设了 3 → probe 失败。
2. **panic 的直接原因**：probe 失败路径未反注册已成功的 skcipher/akcipher 算法，
   而设备数据随 probe 失败释放；之后内核 `regulatory_init_db` 的 x509 验签创建
   `rsa-aic` tfm，销毁时访问已释放的 `ctx->ce` → UAF panic。
3. 修复（`dc68dcd97`）：删 alignmask + probe 失败时回滚已注册的算法并
   `pm_runtime_disable`。engine 框架适配详见提交信息（ops 紧跟 crypto_alg 的布局、
   prepare 并入 `do_one_request`、unprepare 在 `crypto_finalize_*()` 前、ahash 用
   `init_tfm/exit_tfm`、`akcipher_set_reqsize()`、`sg_pcopy_{to,from}_buffer()`）。

### 2.7 nvmem / thermal / MAC

**SID 的 nvmem cell 未注册**（`944c895fc`）：6.18 只在驱动设置
`add_legacy_fixed_of_cells` 时才解析 DTS 子节点 cell（`chipid`、`t0_low`、
`t1_low`、`envtemp_*`、`ldo30_bg_ctrl`、`cp_version`）。后果：
- `aicmac_macaddr_from_chipid can't get chipid` → MAC 走随机回退；
- thermal 标定数据读不到 → 斜率/偏置为 0 → 温度恒 0。

> 若修复后 thermal 仍为 0，需与 5.10 对照判断是否该芯片 efuse 标定为空（见 6.1）。

---

## 3. 5.10 → 6.18 API 适配速查

| 子系统 | 6.18 变化 | 处理 |
|--------|-----------|------|
| 通用 | `platform_driver.remove` / `i2c_driver.remove` 改 `void` | 去掉 return |
| 通用 | 多数头文件不再传递包含（of/seq_file/vmalloc/of_platform/pinctrl-consumer 等） | 显式 include |
| pinctrl | `PIN_CONFIG_OUTPUT` 删除 → `PIN_CONFIG_LEVEL` | 替换分支 |
| pinctrl | `pinctrl_gpio_request/free/direction_*` 增加 `chip` 参数；`gpio_chip.set` 返回 int；`of_node`→`fwnode`；`irq_linear_revmap`→`irq_find_mapping`；`devm_krealloc` | 见驱动 |
| 8250 | `UART_USR` 等从 serial_reg 删除；`mcr_force` 删除；`serial_in/out` 签名；`set_termios`/`rs485_config` 新参数；serial core 改 kfifo 收发 | AIC UART DMA 暂关（`SERIAL_8250_DMA`） |
| SPI | `spi_alloc_master`→`spi_alloc_host`；`master`→`controller`；`platform_data` 删除 | 见驱动 |
| SPI-MEM/NAND | core 重构；vendor 操作宏重写（`X4_OP`→`1S_1S_4S_OP` 等，需补频率参数） | 翻译宏 |
| MMC | `hw_reset`→`card_hw_reset`；timer_* 改名；gpiod API；remove void | 见驱动 |
| DMA | 位操作宏 `writel_bits/writel_clrbits` 等缺失 | 补到 `include/linux/bits.h` |
| crypto (CE) | engine 框架重构；`akcipher_alg.reqsize`→`cra_reqsize`/`akcipher_set_reqsize`；`crypto/sha.h` 拆分；scatterwalk 删除；`hash_prepare_alg()` 禁 alignmask；`crypto_engine_alloc_init_and_set` 多 retry 参数 | `dc68dcd97` |
| dma-buf | `vmap/vunmap` 用 `iosys_map`；`dma_heap_ops.allocate` 返回 `dma_buf*`；无 `destroy_heap` | 见 2.3.2 |
| PWM | `pwmchip_alloc`/`devm_pwmchip_alloc`；`.apply` 单一回调；`get_state` 返回 int | `70ab88e99` |
| thermal | `thermal_zone_device_register_with_trips`；`thermal_zone_device` 不透明（`_priv()`）；无 `zone->tzp`；无 `get_trip_type/temp/notify`（用 `.critical`） | `a595df1f2` |
| fbdev | `FBINFO_DEFAULT` 删除；`memblock_free` 返回 void；choice 成员必须 bool | `ddb853842` |
| input | i2c `probe` 无 id 参数；`strlcpy`→`strscpy`；`hrtimer_init`→`hrtimer_setup` | `a6da67b7b` |
| ASoC | 无 `non_legacy_dai_naming`；无 DAI `.probe`（用 `for_each_component_dais`） | `78e8e7d12` |
| rc-core | 无 `tx_resolution`；`ir_raw_event_reset`→`set_idle(rc,false)` | `3a7b6ee1d` |
| V4L2 | pad ops 用 `v4l2_subdev_state`；`init_state` 在 internal_ops；frame interval 移到 pad ops；async notifier `v4l2_async_nf_*`；`media_pipeline_start` 收 pad；`min_queued_buffers`；`v4l2_mbus_config_parallel` | `3da556efa` |
| 网络 | phylink 新接口；page_pool；`of_get_mac_address(np,addr)`；`netif_napi_add(_tx)`；ethtool 结构；PTP `adjfine` | `0997e0e48` |
| cfg80211 | ops 增加 `radio_idx`/`netdev`；rx spurious/4addr 增加 link_id | `4f1779592` |
| 无线 timer | `from_timer`→`timer_container_of`；`del_timer(_sync)`→`timer_delete(_sync)`；wakeup_source 合并为 register/unregister | `4f1779592` |
| nvmem | 需 `add_legacy_fixed_of_cells` | `944c895fc` |
| RISC-V | `riscv,isa` 弃用；`ERRATA_THEAD` CMO；`ARCH_DMA_DEFAULT_COHERENT`（**必须 `dma-noncoherent`**） | 2.1.2 / 2.2.4 |

---

## 4. 各子系统移植记录

### 4.1 M1：构建系统 + arch/riscv + 串口 + SPI NAND

- 构建接入：`BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.18"`、
  `BR2_LINUX_KERNEL_DEFCONFIG="d211_d70t_nand"`；`post-image.sh` 的
  `KERNEL_HEADER_DIR` 改为按 `BR2_LINUX_KERNEL_VERSION` 动态解析。
- `arch/riscv/Kconfig.socs` 新增 `ARCH_ARTINCHIP`（select `ARCH_HAS_RESET_CONTROLLER`、
  `ERRATA_THEAD`、`RISCV_SBI_CPUIDLE`）、`MACH_D211`、`DEBUG_ON_FPGA_BOARD_ARTINCHIP`。
- 驱动：clk/reset/pinctrl/8250/spi-artinchip；NAND BBT：
  `NAND_BBT_MANAGE=y`、`NAND_BBT_OFFSET=0x240000`、`aic_bbt.c`，
  在 `spinand.h`/`core.c` 回移 4 处 hook（含加密 hook，用
  `CONFIG_CRYPTO_DEV_ARTINCHIP_SPIENC` 隔离，当前未开）。
- 内核镜像 10.6MB / Image.gz 5.5MB；kernel 分区 12MB（FIT 已占 ~86%，后续加驱动需关注）。

### 4.2 M2：MMC/网络/USB/外设

| 驱动 | 文件 | 说明 |
|------|------|------|
| MMC | `drivers/mmc/host/artinchip_mmc.c` | TF（SDMC1）与 WiFi SDIO（SDMC2）均验证通过；含 2.5.3/2.5.4 的修复 |
| 网络 | `drivers/net/ethernet/artinchip/*` | aicmac（`0997e0e48`）。**本板原理图无 PHY/RJ45，eth0 仅注册无链路** |
| USB host | `ehci-aic.c`/`ohci-aic.c`（`8027cde76`） | USB1 host（4G）；`aic-otg.c` 依赖 AIC 私有 `struct usb_otg`，已移除（`113db810c`） |
| USB device | `gadget/udc/aic_udc.c` | ADB；含 2.2 全部修复 + `1c2096017`（USB0 PHY device 模式） |
| I2C | `i2c-artinchip-{common,master}.c`；slave `45cffda6d`（未启用） | 6.18 仍保留 `reg_slave` 弃用别名 |
| I2C slave/RTC/WDT/SID/misc | `drivers/{i2c,rtc,watchdog,nvmem,misc}/*` | `artinchip-{adcim,syscfg,wri,sid,rtc,wdt}` 启用；`mtop/pbus` 已适配默认不启用 |
| IIO ADC | `artinchip_{adc,psadc}.c` | 依赖 ADCIM，默认不启用 |
| DMA | `artinchip-dma.c` | `writel_bits` 等宏补到 `include/linux/bits.h`；defconfig 启用 `DMA/DDMA` |
| PWM/thermal | M3 完成（API 改动大） | 见 4.3 |
| CIR | `media/rc/artinchip-cir.c`（`3a7b6ee1d`） | D70T 节点 disabled，defconfig 未启用（与 5.10 一致） |

### 4.3 M3：显示 / 触摸 / 音频 / thermal / MPP

- 显示：`drivers/video/artinchip/*`（DE/RGB/LVDS/DSI/DBI/panel/GE/VE）+
  `include/uapi/video/artinchip_*.h` + `dma-noncoherent`（2.3.1）。
- 触摸：`input/touchscreen/artinchip.c`（RTP）+ `gt9xx/`（GT911）。
- 背光：`pwm-artinchip`（新 API）+ `pwm-backlight`。
- 音频：`sound/soc/artinchip/*`（I2S/codec/AC102/AC107/dummy）+ DMA + 2.4 修复。
- thermal：`thermal/artinchip_thermal.c` + 2.7 修复。
- MPP：`dma-buf` heap/ioctl + 2.3.2 修复。

### 4.4 M4：无线 / 加密 / PM / 其它

- AIC8800 SDIO WiFi（`4f1779592` + defconfig `85a7bbaaf`）：fdrv + bsp，BT LPM 关闭；
  固件路径 `/etc/firmware`（Buildroot `aic8800_fw` 包，d80 系）。
- CE 加密（`02b229623` + `dc68dcd97` + defconfig `a543e5d74`）。
- PM 挂起（`546b0f6be`）：`arch/riscv/kernel/artinchip_pm.c`（SBI HSM + 总线时钟选择；
  `SBI_HSM_SUSPEND_RET_PLATFORM` 改由 `asm/sbi.h` 提供）。
- SPI-NAND 厂商（`0060c8a13`）：ZBIT/Elite/UMTEK/BYTE/Xincun/Dosilicon；
  旧式 SPI-mem 宏翻译为新式并补频率参数；厂商 ID 与 mainline 重复（按完整 chip ID 匹配）。
- DVP 摄像头（`3da556efa`，编译级）：DVP 平台驱动 + GM7150/XS9950，
  V4L2 子设备 state API 迁移（见 3 节）。D70T 默认不启用。
- 暂不移植：**SPI-NOR 厂商**（boya/cfx/fmsh/puya/xtx/zbit + `artinchip_spi_nor_enc.h`）——
  6.18 spi-nor core 重构，需逐文件重写；NOR 板型专项处理。
- 暂不移植：**SPIENC**（`spinand_enc_init` 依赖的 `spinand->priv` 已被厂商驱动占用）、
  **UART DMA**（5.10 自定义实现依赖已删除的 serial core 接口）、**soft_vector**（C906 向量模拟）。

---

## 5. 现场验证手册

### 5.1 显示 / 触摸 / 背光

```sh
ls /dev/fb0 ; cat /sys/class/graphics/fb0/name          # aicfb0
echo 1 > /sys/devices/platform/soc/18a00000.de/debug/color_bar   # 内部彩条
echo 80 > /sys/class/backlight/backlight/brightness     # 亮度
cat /dev/input/event0 | od -A x -t x1 | awk 'NR<=5'     # 触摸事件（无 head）
```

### 5.2 ADB

```sh
# 板端
ls /sys/class/udc/ ; cat /sys/kernel/config/usb_gadget/g_adb/UDC
# PC 端
adb devices
```

### 5.3 TF 卡

```sh
ls /dev/mmcblk* ; cat /sys/bus/mmc/devices/*/name
mount /dev/mmcblk0p1 /mnt && ls /mnt && umount /mnt
```

### 5.4 WiFi（AIC8800）

```sh
/etc/init.d/S00lvgl stop                     # 先腾内存（52MB 下 OOM 风险）
ls /sys/bus/sdio/devices/                    # mmc1:390b:1 / mmc1:390b:2
D211_SSID=xxx D211_PSK=yyy /usr/local/bin/d211-network.sh
cat /sys/class/net/wlan0/carrier             # 1=已关联
ifconfig wlan0                               # 应有 inet addr
udhcpc -b -i wlan0 -R                        # 常驻续约（租约短时必需）
```

### 5.5 4G（EG800K/RNDIS）

```sh
ifconfig usb0 up ; udhcpc -i usb0            # 期望 192.168.43.x
dmesg | grep -iaE "rndis|option"             # ttyUSB0-3 可用于 AT
```

### 5.6 音频

```sh
cat /proc/asound/cards
amixer -c 0 contents                         # 确认 4 个 AUDOUT 开关与音量
arecord -d 3 -f S16_LE -r 16000 -c 1 /tmp/rec.wav && aplay /tmp/rec.wav
```

### 5.7 CE 加密

```sh
cd /tmp
/usr/local/bin/crypto_kcapi gen-test-data --size 4096 --out in.bin
/usr/local/bin/crypto_kcapi sha256 --in in.bin --hexout
K=000102030405060708090a0b0c0d0e0f
/usr/local/bin/crypto_kcapi aes-128-ecb --enc --keyhex $K --in in.bin --out e1.bin
/usr/local/bin/crypto_kcapi aes-128-ecb --dec --keyhex $K --in e1.bin --out d1.bin
md5sum in.bin d1.bin                         # 两个和一致即 OK
# 注意：CBC 需用 --ivfile（工具 --ivhex 有长度检查 bug：MAX_IV_SIZE=16 却比 hex 字符串长度）
```

### 5.8 thermal

```sh
cat /sys/class/thermal/thermal_zone*/type /sys/class/thermal/thermal_zone*/temp
```

### 5.9 PM 挂起

```sh
echo mem > /sys/power/state                  # 串口静默，按唤醒源恢复
```

---

## 6. 未完成与后续工作

1. **thermal 读数 0**：nvmem cell 已能读到，需与 5.10 对照（`cat
   /sys/class/thermal/thermal_zone0/temp`）：5.10 也为 0 → 该芯片 efuse 标定为空
   （硬件层面，可放弃或改读 ADC）；5.10 正常 → 6.18 的标定/转换路径需继续查。
2. **音频听音确认**：`6cbf3d26d` 后待确认喇叭实际出声。
3. **CE AES 功能复测**：用 `--ivfile` 或 ECB 方式验证加解密往返正确性。
4. **SPI-NOR 厂商驱动**：按 6.18 新 spi-nor core 重写（NOR 板型）。
5. **DVP 摄像头**：在 demo100/132 等有摄像头的板子上按 3 节/`3da556efa` 提交信息
   启用并验证（用 SDK 的 `test-vin`/`test-dvp`）。
6. **内存优化**：52MB RAM 下 WiFi+4G+应用并发会 OOM（已见 OOM 杀 `test_lvgl`）；
   方向：应用懒加载贴图、MPP/CMA 评估、关闭不必要的 app 自启。
7. **内核体积**：FIT 占 kernel 分区 ~86%，继续加驱动需关注（可用 Image.gz 或调分区）。
8. **PM 挂起**：需上板验证与唤醒源配置。
9. **UART DMA / SPIENC / soft_vector**：见 4.4，按需专项处理。
10. **6.18.52 上板回归**（当前最优先）：烧 `output/backup/d211_d70t_nand_6.18.52_20260916.img`
    （内核 `6.18.52-00056-gbc235454d2c8`），按 1.1 表逐项复测：启动/NAND/串口 →
    显示/触摸/背光 → ADB/TF/4G/WiFi → 音频/thermal/CE → PM。全部通过后即可把
    `v6.18.52-d211-r1` 标为"已验证"，作为后续 stable 跟进（6.18.53…）的新基线。

---

## 7. 关键提交索引（按里程碑）

| 里程碑 | 提交 |
|--------|------|
| M1 | `50c107055`、`5fcee5888`、`81864a014`、`2cabd53cf`、`be08a3639`、`6cfca095a`、`c2af542c6`、`f34223bb4`、`f6f53353c`、`5e386dec0`、`cafc839e7`、`173d6a704` |
| M2 | `adb26e655`(MMC)、`49626d519`(UDC)、`1125e24e7`、`8ca9d1fc6`、`f30fb0b46`、`7544fe16b`、`49c569914`、`082fec9b8`（USB/ADB）、`1c2096017`（USB0 PHY）、`0997e0e48`（aicmac）、`8027cde76`、`113db810c`（USB host）、`604103150`（DMA）、`56e218a06`、`6a05146e3`、`41607c7e7`、`a453fa303`、`3f867ebcd`、`08f28a541`、`935449e68`（外设）、`b9faac9c2`、`0bbae4913`（板级） |
| M3 | `ddb853842`、`70ab88e99`、`a6da67b7b`、`78e8e7d12`、`667d1e0a8`、`de6893772`（显示修复）、`fb9cab674`、`c2492b1d1`、`fd9a63d4a`（MPP/DMA）、`a595df1f2`（thermal）、`236d91726`、`f4dc2eb26`、`6cbf3d26d`（音频） |
| M4 | `4f1779592`、`85a7bbaaf`（WiFi）、`02b229623`、`dc68dcd97`、`a543e5d74`（CE）、`546b0f6be`（PM）、`0060c8a13`（SPI-NAND 厂商）、`45cffda6d`（I2C slave）、`3a7b6ee1d`（CIR）、`3da556efa`（摄像头）、`0cf80875b`、`fc22b32a9`（脚本） |
| 修复 | `944c895fc`（nvmem+WiFi 冷启动）、`ce6bf5be0`、`543368966`（SDMC unbind）、`cafc839e7` |

> 上表是 **Luban 主仓（`d211`）历史里的提交号**，用于追溯"内核移植当时混在 SDK 里"
> 的原始记录。内核拆成独立仓库后，同样的改动在
> `https://github.com/boa-z/d211-linux-6.18` 的 `artinchip-6.18-lts` 上有对应的
> 独立提交（56 个，rebase 到 `v6.18.52`，SHA 不同）；两边内容逐字节一致。

---

## 附录 A：上板调试要点（历史经验，供复用）

- **串口**：PD6/PD7 = UART1（SoC↔MCU），调试口是 UART0；坏 USB-TTL 线会让串口"假死"，
  排查输入无响应时先换线。
- **reg-dump / devmem**：`/usr/local/bin/reg-dump`、busybox `devmem` 可做寄存器对照；
  显示调试用 DE 的 `debug/color_bar`、`debug/display`。
- **USB 调试**：PC 端 UsbTreeView 看速度/描述符；「Device Descriptor Request Failed /
  Low-Speed」在 6.18 上多为 DMA 一致性（2.2.4）。
- **A/B 对照**：任何"5.10 正常 6.18 异常"的问题，先烧 5.10 镜像复现同一条命令，
  能快速判断是移植还是硬件（WiFi SDIO、thermal 都是这样定位的）。

## 附录 B：镜像备份索引（`output/backup/`）

| 镜像 | 内容 |
|------|------|
| `d211_d70t_nand_5.10-official_20260915.img` | 5.10 基线（ADB/显示/WiFi 正常） |
| `6.18-clean_20260915.img` | M1 完成（启动/NAND） |
| `6.18-m3full2_20260916.img` | 显示/触摸/背光/MPP/DMA/thermal |
| `6.18-m4full_20260916.img` | 加 WiFi/CE/PM |
| `6.18-netce_20260916.img` | WiFi/4G 脚本 + CE：WiFi/4G 验证通过 |
| `6.18-nvfix*/6.18-audiofix/6.18-nvfix3_20260916.img` | SID nvmem、WiFi 冷启动、音频混音、SDMC 定时器修复 |
| `d211_d70t_nand_6.18.52_20260916.img` | **6.18.52 基线**（独立内核仓库 + 56 移植提交），待上板回归 |
| 最新 | 见 `output/d211_d70t_nand/images/d211_demo88_nand_page_2k_block_128k_v1.0.0.img` |
