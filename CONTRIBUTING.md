# Luban 开发流程

## 新增 Linux 内核驱动

- [ ] 驱动源码：`source/linux-5.10/drivers/{subsystem}/artinchip/`
- [ ] Kconfig：在对应子系统目录添加 `CONFIG_ARTINCHIP_*` 选项
- [ ] Makefile：在对应子系统目录添加编译项
- [ ] 设备树：`target/{chip}/common/` 中添加或修改 `.dtsi` 文件
- [ ] defconfig：`target/configs/{chip}_{board}_defconfig` 中启用相关选项
- [ ] 遵循 Linux 内核编码规范
- [ ] 使用 `dev_xxx()` 日志接口，优先于 `pr_xxx()`

## 新增 U-Boot 驱动

- [ ] 驱动源码：`source/uboot-2021.10/drivers/{subsystem}/artinchip/`
- [ ] Board 代码：`source/uboot-2021.10/board/artinchip/{chip}/`
- [ ] Kconfig + Makefile
- [ ] defconfig：`target/configs/{chip}_{board}_defconfig`
- [ ] 公共头文件：`source/uboot-2021.10/include/artinchip/`（如需要）

## 新增 OpenSBI 平台支持 (仅对内)

- [ ] 平台代码：`source/opensbi/platform/generic/`
- [ ] 遵循 OpenSBI 编码规范

## 新增 Buildroot 包

- [ ] 包目录：`package/artinchip/{package-name}/`
- [ ] `Config.in` + `{package-name}.mk`
- [ ] 在 `package/artinchip/Config.in` 中 source
- [ ] 源码放在 `source/artinchip/{package-name}/` 或使用预编译

## 新增 Board

- [ ] 使用命令 `make add_board` 自动生成模板
- [ ] 目录：`target/{chip}/{board}/`
- [ ] SDK defconfig：`target/configs/{chip}_{board}_defconfig`
- [ ] 设备树：`target/{chip}/{board}/` 中添加 `board.dts` 文件
- [ ] post-image 脚本（如需自定义打包）

## 新增测试示例

- [ ] 包目录：`package/artinchip/test-{module}/`
- [ ] `Config.in` + `test-{module}.mk`
- [ ] 源码目录：`source/artinchip/test-{module}/`
- [ ] 在 `package/artinchip/Config.in` 中 source
- [ ] 提供 README.md 使用说明

## 编译验证

```bash
source tools/onestep.sh
lunch {chip}_{board}_defconfig
m
```

验证要点：
- [ ] 全量编译无错误
- [ ] 生成的镜像可正常烧写启动
- [ ] 新增测试示例的功能测试通过

## 文档同步

完成以上任何操作后，必须同步更新：

- [ ] [AGENTS.md](AGENTS.md) — 如果构建命令或目录结构有变化
- [ ] 子模块 README.md — 如果是新增模块
- [ ] [CODEMAP.md](CODEMAP.md) — 追加或删除对应行
