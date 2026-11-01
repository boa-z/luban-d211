#!/bin/bash

# strip-ko.sh - Buildroot post-build script
# 功能：在根文件系统创建完成后，strip 所有 .ko 内核模块文件

# 颜色定义
COLOR_BEGIN="\033["
COLOR_RED="${COLOR_BEGIN}41;37m"
COLOR_YELLOW="${COLOR_BEGIN}43;30m"
COLOR_GREEN="${COLOR_BEGIN}42;37m"
COLOR_WHITE="${COLOR_BEGIN}47;30m"
COLOR_END="\033[0m"

function mk_error()
{
    echo -e "      ${COLOR_RED}$*${COLOR_END}"
}

function mk_warn()
{
    echo -e "      ${COLOR_YELLOW}$*${COLOR_END}"
}

function mk_info()
{
    echo -e "      ${COLOR_WHITE}$*${COLOR_END}"
}

function mk_success()
{
    echo -e "      ${COLOR_GREEN}$*${COLOR_END}"
}

# Strip 内核模块文件
function strip_kernel_modules()
{
    local STRIP_TOOL="${HOST_DIR}/bin/riscv64-unknown-linux-gnu-strip"
    local MODULES_DIR="${TARGET_DIR}/lib/modules"
    local KO_COUNT=0
    local STRIP_COUNT=0

    mk_info "Stripping kernel modules..."

    # 检查 strip 工具是否存在
    if [ ! -x "${STRIP_TOOL}" ]; then
        mk_error "Strip tool not found: ${STRIP_TOOL}"
        return 1
    fi

    # 检查模块目录是否存在
    if [ ! -d "${MODULES_DIR}" ]; then
        mk_warn "Kernel modules directory not found: ${MODULES_DIR}"
        return 0
    fi

    # 查找并 strip 所有 .ko 文件
    while IFS= read -r -d '' ko_file; do
        KO_COUNT=$((KO_COUNT + 1))
        mk_info "Stripping: ${ko_file}"

        ${STRIP_TOOL} --strip-unneeded "${ko_file}"
        if [ $? -eq 0 ]; then
            STRIP_COUNT=$((STRIP_COUNT + 1))
        else
            mk_warn "Failed to strip: ${ko_file}"
        fi
    done < <(find "${MODULES_DIR}" -type f -name "*.ko" -print0)

    mk_success "Stripped ${STRIP_COUNT}/${KO_COUNT} kernel modules"

    return 0
}

# 主函数
function main()
{
    strip_kernel_modules
}

main
