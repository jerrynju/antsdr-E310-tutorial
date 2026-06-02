#!/usr/bin/env bash
# ANTSDR E310 开发环境自动配置脚本
# 用法：bash setup-env.sh [--toolchain-dir <路径>] [--vivado-dir <路径>]
# 示例：bash setup-env.sh --toolchain-dir ~/toolchain --vivado-dir /opt/Xilinx/Vivado/2023.2

set -e

TOOLCHAIN_DIR="${HOME}/toolchain"
VIVADO_DIR="/opt/Xilinx/Vivado/2023.2"
TOOLCHAIN_VERSION="7.3.1-2018.05"
TOOLCHAIN_NAME="gcc-linaro-${TOOLCHAIN_VERSION}-x86_64_arm-linux-gnueabihf"
TOOLCHAIN_URL="https://releases.linaro.org/components/toolchain/binaries/7.3-2018.05/arm-linux-gnueabihf/${TOOLCHAIN_NAME}.tar.xz"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --toolchain-dir) TOOLCHAIN_DIR="$2"; shift 2 ;;
            --vivado-dir)    VIVADO_DIR="$2";    shift 2 ;;
            -h|--help)
                echo "用法: bash setup-env.sh [选项]"
                echo "  --toolchain-dir <路径>   工具链安装目录 (默认: ~/toolchain)"
                echo "  --vivado-dir <路径>      Vivado 安装目录 (默认: /opt/Xilinx/Vivado/2023.2)"
                exit 0 ;;
            *) log_error "未知参数: $1"; exit 1 ;;
        esac
    done
}

check_os() {
    log_info "检查操作系统..."
    if [[ "$(uname -s)" != "Linux" ]]; then
        log_error "仅支持 Linux 系统"
        exit 1
    fi
    if [[ "$(uname -m)" != "x86_64" ]]; then
        log_error "仅支持 64 位系统"
        exit 1
    fi
    local os_id
    os_id=$(. /etc/os-release && echo "$ID")
    if [[ "$os_id" != "ubuntu" ]]; then
        log_warn "当前系统不是 Ubuntu，脚本可能无法完全工作"
    else
        log_info "操作系统: $(. /etc/os-release && echo "$PRETTY_NAME")"
    fi
}

install_dependencies() {
    log_info "安装依赖软件包..."
    sudo apt-get update -q

    sudo apt-get install -y \
        git build-essential fakeroot \
        libncurses5-dev libssl-dev ccache \
        dfu-util u-boot-tools device-tree-compiler mtools \
        bc cpio zip unzip rsync file wget curl \
        libtinfo5 bison flex libmpc-dev \
        minicom picocom

    # 兼容 python 命令
    if ! command -v python &>/dev/null; then
        if apt-get install -y python-is-python3 2>/dev/null; then
            log_info "已安装 python-is-python3"
        else
            sudo ln -sf /usr/bin/python3 /usr/local/bin/python
            log_info "已创建 python -> python3 软链接"
        fi
    fi

    # 移除冲突的 ARM 工具链
    sudo apt-get purge -y gcc-arm-linux-gnueabihf 2>/dev/null || true
    sudo apt-get remove -y libfdt-dev 2>/dev/null || true

    log_info "依赖安装完成"
}

install_toolchain() {
    local toolchain_bin="${TOOLCHAIN_DIR}/${TOOLCHAIN_NAME}/bin"

    if [[ -f "${toolchain_bin}/arm-linux-gnueabihf-gcc" ]]; then
        log_info "工具链已存在：${toolchain_bin}"
        return 0
    fi

    log_info "下载 ARM 交叉编译工具链（Linaro ${TOOLCHAIN_VERSION}）..."
    mkdir -p "$TOOLCHAIN_DIR"

    local archive="${TOOLCHAIN_DIR}/${TOOLCHAIN_NAME}.tar.xz"
    if [[ ! -f "$archive" ]]; then
        wget -c --show-progress -O "$archive" "$TOOLCHAIN_URL"
    else
        log_info "安装包已存在，跳过下载"
    fi

    log_info "解压工具链..."
    tar -xf "$archive" -C "$TOOLCHAIN_DIR"
    log_info "工具链安装完成：${toolchain_bin}"
}

check_vivado() {
    if [[ -f "${VIVADO_DIR}/settings64.sh" ]]; then
        log_info "Vivado 已安装：${VIVADO_DIR}"
    else
        log_warn "Vivado 未在 ${VIVADO_DIR} 找到"
        log_warn "请从 AMD 官网下载 Vivado 2023.2 并安装到 ${VIVADO_DIR}"
        log_warn "下载地址（需登录）: https://account.amd.com/en/forms/downloads/xef.html"
        log_warn "安装完成后重新运行此脚本"
    fi
}

write_env_config() {
    local toolchain_bin="${TOOLCHAIN_DIR}/${TOOLCHAIN_NAME}/bin"
    local env_file="${HOME}/.antsdr_e310_env"

    cat > "$env_file" << EOF
# ANTSDR E310 构建环境变量
# 由 setup-env.sh 自动生成
# 使用方式：source ~/.antsdr_e310_env

export CROSS_COMPILE=arm-linux-gnueabihf-
export PATH="\$PATH:${toolchain_bin}"
export VIVADO_SETTINGS="${VIVADO_DIR}/settings64.sh"

# 默认目标：ANTSDR E310 (原版)
export TARGET=ant

echo "[ANTSDR E310] 环境变量已加载"
echo "  CROSS_COMPILE = \$CROSS_COMPILE"
echo "  TARGET        = \$TARGET"
EOF

    log_info "环境配置文件已写入：${env_file}"

    # 将 source 命令追加到 .bashrc（避免重复添加）
    local marker="# antsdr-e310-env"
    if ! grep -q "$marker" "${HOME}/.bashrc"; then
        cat >> "${HOME}/.bashrc" << EOF

${marker}
if [ -f "${env_file}" ]; then
    source "${env_file}"
fi
EOF
        log_info "已添加到 ~/.bashrc，新终端中自动生效"
    else
        log_info "~/.bashrc 中已有配置，跳过"
    fi
}

verify_setup() {
    log_info "验证环境配置..."
    local ok=true

    local toolchain_bin="${TOOLCHAIN_DIR}/${TOOLCHAIN_NAME}/bin"
    if [[ -f "${toolchain_bin}/arm-linux-gnueabihf-gcc" ]]; then
        local gcc_ver
        gcc_ver=$("${toolchain_bin}/arm-linux-gnueabihf-gcc" --version | head -1)
        log_info "✓ ARM GCC：${gcc_ver}"
    else
        log_error "✗ ARM GCC 未找到"
        ok=false
    fi

    if [[ -f "${VIVADO_DIR}/settings64.sh" ]]; then
        log_info "✓ Vivado：${VIVADO_DIR}"
    else
        log_warn "△ Vivado 未安装（需手动安装）"
    fi

    if command -v dfu-util &>/dev/null; then
        log_info "✓ dfu-util：$(dfu-util --version 2>&1 | head -1)"
    else
        log_error "✗ dfu-util 未安装"
        ok=false
    fi

    if command -v dtc &>/dev/null; then
        log_info "✓ dtc (Device Tree Compiler)"
    else
        log_error "✗ dtc 未安装"
        ok=false
    fi

    if $ok; then
        log_info "========================================="
        log_info "环境配置完成！"
        log_info "请执行以下命令立即生效："
        log_info "  source ~/.antsdr_e310_env"
        log_info "或者开启新终端（已自动生效）"
        log_info "========================================="
    else
        log_error "部分组件未就绪，请检查上述错误"
        exit 1
    fi
}

main() {
    echo "=================================================="
    echo "  ANTSDR E310 开发环境配置脚本"
    echo "=================================================="
    parse_args "$@"
    check_os
    install_dependencies
    install_toolchain
    check_vivado
    write_env_config
    verify_setup
}

main "$@"
