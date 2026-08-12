# Luban 源码地图

本文件为 Luban V1.0 版本的源码路径索引。

## Linux 5.10 AIC 驱动

### 系统

| 模块 | 源码目录 |
|------|----------|
| CLK  | `source/linux-5.10/drivers/clk/artinchip/` |
| Pinctrl | `source/linux-5.10/drivers/pinctrl/artinchip/` |
| Reset | `source/linux-5.10/drivers/reset/reset-artinchip.c` |
| DMA | `source/linux-5.10/drivers/dma/artinchip-dma.c` |
| Watchdog | `source/linux-5.10/drivers/watchdog/artinchip_wdt.c` |
| RTC | `source/linux-5.10/drivers/rtc/rtc-artinchip.c` |
| SID/eFuse | `source/linux-5.10/drivers/nvmem/artinchip-sid.c` |
| Thermal | `source/linux-5.10/drivers/thermal/artinchip_thermal.c` |
| ADC | `source/linux-5.10/drivers/iio/adc/artinchip_adc.c` |
| PSADC | `source/linux-5.10/drivers/iio/adc/artinchip_psadc.c` |
| CIR | `source/linux-5.10/drivers/media/rc/artinchip-cir.c` |
| ADCIM | `source/linux-5.10/drivers/misc/artinchip-adcim.c` |
| MTOP | `source/linux-5.10/drivers/misc/artinchip-mtop.c` |
| PBUS | `source/linux-5.10/drivers/misc/artinchip-pbus.c` |
| SYSCFG | `source/linux-5.10/drivers/misc/artinchip-syscfg.c` |
| WRI | `source/linux-5.10/drivers/misc/artinchip-wri.c` |

### 多媒体

| 模块 | 源码目录 |
|------|----------|
| DE | `source/linux-5.10/drivers/video/artinchip/disp/` | DE 显示引擎 |
| GE | `source/linux-5.10/drivers/video/artinchip/ge/` | GE 图形引擎 |
| VE | `source/linux-5.10/drivers/video/artinchip/ve/` | VE 视频引擎 |
| DVP | `source/linux-5.10/drivers/media/platform/artinchip/` |
| Audio (I2S/Codec) | `source/linux-5.10/sound/soc/artinchip/` |

### 接口

| 模块 | 源码目录 |
|------|----------|
| UART | `source/linux-5.10/drivers/tty/serial/artinchip/` |
| I2C  | `source/linux-5.10/drivers/i2c/busses/i2c-artinchip-master.c` |
| SPI | `source/linux-5.10/drivers/spi/spi-artinchip.c` |
| GMAC | `source/linux-5.10/drivers/net/ethernet/artinchip/` |
| SDMC | `source/linux-5.10/drivers/mmc/host/artinchip_mmc.c` |
| PWM | `source/linux-5.10/drivers/pwm/pwm-artinchip.c` |
| EPWM | `source/linux-5.10/drivers/pwm/epwm-artinchip.c` |
| CAP | `source/linux-5.10/drivers/pwm/cap-artinchip.c` |
| RTP | `source/linux-5.10/drivers/input/touchscreen/artinchip.c` |

### 安全

| 模块 | 源码目录 |
|------|----------|
| CE | `source/linux-5.10/drivers/crypto/artinchip/` |
| SPIENC | `source/linux-5.10/drivers/mtd/spi-nor/artinchip_spi*_enc.h` |

## U-Boot AIC 驱动

### 系统

| 模块 | 源码目录 |
|------|----------|
| Board 初始化 | `source/uboot-2021.10/board/artinchip/` |
| CLK | `source/uboot-2021.10/drivers/clk/artinchip/` |
| Pinctrl | `source/uboot-2021.10/drivers/pinctrl/artinchip/` |
| Reset | `source/uboot-2021.10/drivers/reset/reset-artinchip.c` |
| DMA | `source/uboot-2021.10/drivers/dma/artinchip_dma.c` |
| Watchdog | `source/uboot-2021.10/drivers/watchdog/artinchip_wdt_v1.0.c` |
| Thermal | `source/uboot-2021.10/drivers/thermal/artinchip_thermal.c` |

### 多媒体

| 模块 | 源码目录 |
|------|----------|
| Display (DE/DSI/LVDS/RGB) | `source/uboot-2021.10/drivers/video/artinchip/display/` |
| VE | `source/uboot-2021.10/drivers/video/artinchip/decoder/` |

### 接口

| 模块 | 源码目录 |
|------|----------|
| UART | `source/uboot-2021.10/drivers/serial/serial_artinchip.c` |
| SPI | `source/uboot-2021.10/drivers/spi/artinchip_spi.c` |
| SDMC | `source/uboot-2021.10/drivers/mmc/artinchip_mmc.c` |
| GPIO | `source/uboot-2021.10/drivers/gpio/gpio-artinchip.c` |
| PWM | `source/uboot-2021.10/drivers/pwm/pwm-artinchip.c` |

### 安全

| 模块 | 源码目录 |
|------|----------|
| CE | `source/uboot-2021.10/drivers/crypto/artinchip/` |
| SID/eFuse | `source/uboot-2021.10/drivers/misc/artinchip-sid.c` |
| SPIENC | `source/uboot-2021.10/drivers/misc/artinchip_spienc.c` |
| ADCIM | `source/uboot-2021.10/drivers/misc/artinchip-adcim.c` |
| SYSCFG | `source/uboot-2021.10/drivers/misc/artinchip-syscfg.c` |

### 其他

| 模块 | 源码目录 |
|------|----------|
| 公共头文件 | `source/uboot-2021.10/include/artinchip/` |

## OpenSBI

| 模块 | 源码目录 |
|------|----------|
| 平台代码 (generic) | `source/opensbi/platform/generic/` |
| lib/sbi | `source/opensbi/lib/sbi/` |
| lib/utils | `source/opensbi/lib/utils/` |

## 设备树

| SoC | Board | board.dts 路径 |
|-----|-------|----------------|
| d211 | aishoot88_nor | `target/d211/aishoot88_nor/board.dts` |
| d211 | demo100_nand | `target/d211/demo100_nand/board.dts` |
| d211 | demo100_nand_secure | `target/d211/demo100_nand_secure/board.dts` |
| d211 | demo128_nand | `target/d211/demo128_nand/board.dts` |
| d211 | demo88_nand | `target/d211/demo88_nand/board.dts` |
| d211 | demo88_nor | `target/d211/demo88_nor/board.dts` |
| d211 | demo88_nor_musl | `target/d211/demo88_nor_musl/board.dts` |
| d211 | demo | `target/d211/demo/board.dts` |
| d211 | ota | `target/d211/ota/board.dts` |
| d211 | ota_emmc | `target/d211/ota_emmc/board.dts` |

## AIC 自研软件组件 (source/artinchip/)

> Buildroot 编译配置位于 `package/artinchip/<模块名>/`，与源码目录一一对应。

| 模块 | 源码目录 | 说明 |
|------|----------|------|
| aic-mpp | `source/artinchip/aic-mpp/` | 多媒体处理平台 |
| aicp-dec | `source/artinchip/aicp-dec/` | 解码库 |
| aic-mem | `source/artinchip/aic-mem/` | 内存管理 |
| aic-authorization | `source/artinchip/aic-authorization/` | 授权管理 |
| aic-cast | `source/artinchip/aic-cast/` | 投屏 |
| aic-logo | `source/artinchip/aic-logo/` | 开机 Logo |
| gst1-plugins-aic | `source/artinchip/gst1-plugins-aic/` | GStreamer AIC 插件 |
| openssl-hwengine | `source/artinchip/openssl-hwengine/` | OpenSSL 硬件加速 |
| reg-dump | `source/artinchip/reg-dump/` | 寄存器转储工具 |
| wifimanager | `source/artinchip/wifimanager/` | WiFi 管理器 |
| http-wificonfig | `source/artinchip/http-wificonfig/` | WiFi 配置 Web |
| wifi_ap_config | `source/artinchip/wifi_ap_config/` | WiFi AP 配置 |
| wifi_p2p_config | `source/artinchip/wifi_p2p_config/` | WiFi P2P 配置 |
| p2p_auto | `source/artinchip/p2p_auto/` | P2P 自动连接 |
| selfie | `source/artinchip/selfie/` | 拍照应用 |
| libuserid | `source/artinchip/libuserid/` | 用户 ID 库 |
| lvgl-ui | `source/artinchip/lvgl-ui/` | LVGL UI |
| awtk-ui | `source/artinchip/awtk-ui/` | AWTK UI |
| qtlauncher | `source/artinchip/qtlauncher/` | Qt 启动器 |

## 测试示例 (source/artinchip/test-*)

> Buildroot 编译配置位于 `package/artinchip/test-<模块名>/`，与源码目录一一对应。

| 模块 | 源码目录 |
|------|----------|
| test-adc | `source/artinchip/test-adc/` |
| test-audio | `source/artinchip/test-audio/` |
| test-battery | `source/artinchip/test-battery/` |
| test-blkdev | `source/artinchip/test-blkdev/` |
| test-can | `source/artinchip/test-can/` |
| test-ce | `source/artinchip/test-ce/` |
| test-clock | `source/artinchip/test-clock/` |
| test-common | `source/artinchip/test-common/` |
| test-coredump | `source/artinchip/test-coredump/` |
| test-dma-buf | `source/artinchip/test-dma-buf/` |
| test-dvp | `source/artinchip/test-dvp/` |
| test-efuse | `source/artinchip/test-efuse/` |
| test-eth | `source/artinchip/test-eth/` |
| test-fb | `source/artinchip/test-fb/` |
| test-gpio | `source/artinchip/test-gpio/` |
| test-keyadc | `source/artinchip/test-keyadc/` |
| test-key-suspend | `source/artinchip/test-key-suspend/` |
| test-libmad | `source/artinchip/test-libmad/` |
| test-mem | `source/artinchip/test-mem/` |
| test-mtop | `source/artinchip/test-mtop/` |
| test-rtc | `source/artinchip/test-rtc/` |
| test-sdmc | `source/artinchip/test-sdmc/` |
| test-secure-ota | `source/artinchip/test-secure-ota/` |
| test-spi | `source/artinchip/test-spi/` |
| test-touchscreen | `source/artinchip/test-touchscreen/` |
| test-tp2825 | `source/artinchip/test-tp2825/` |
| test-uart | `source/artinchip/test-uart/` |
| test-watchdog | `source/artinchip/test-watchdog/` |

## 板级配置

### 板级目录

| SoC | Board | 目录 |
|-----|-------|------|
| d211 | aishoot88_nor | `target/d211/aishoot88_nor/` |
| d211 | demo100_nand | `target/d211/demo100_nand/` |
| d211 | demo100_nand_secure | `target/d211/demo100_nand_secure/` |
| d211 | demo128_nand | `target/d211/demo128_nand/` |
| d211 | demo88_nand | `target/d211/demo88_nand/` |
| d211 | demo88_nor | `target/d211/demo88_nor/` |
| d211 | demo88_nor_musl | `target/d211/demo88_nor_musl/` |
| d211 | demo | `target/d211/demo/` |
| d211 | ota | `target/d211/ota/` |
| d211 | ota_emmc | `target/d211/ota_emmc/` |

## 构建工具

| 工具 | 路径 | 说明 |
|------|------|------|
| onestep.sh | `tools/onestep.sh` | 一键编译脚本 |
| add_board.py | `tools/scripts/add_board.py` | 新增板级配置脚本 |
| del_board.py | `tools/scripts/del_board.py` | 删除板级配置脚本 |
| gen_sdcard_image.py | `tools/scripts/gen_sdcard_image.py` | 生成 SD 卡镜像 |
| imgtools.sh | `tools/scripts/imgtools.sh` | 镜像工具 |
| image_cfg_parser.py | `tools/scripts/image_cfg_parser.py` | 镜像配置解析 |
| get_env_info.py | `tools/scripts/get_env_info.py` | 获取环境信息 |
