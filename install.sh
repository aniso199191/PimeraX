#!/bin/bash
# PimeraX 一键安装脚本
# 编译 pimerax + 安装 prx 压缩工具到系统

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
echo "=== PimeraX 安装 ==="

# 1. 编译 C++ 框架
echo "[1/3] 编译 pimerax..."
g++ -O3 -std=c++17 -o "$DIR/pimerax" "$DIR/pimerax.cpp"
echo "  ✔ pimerax 编译完成"

# 2. 安装到 PATH（可选，需要 sudo）
if [ "$1" = "--system" ]; then
    sudo cp "$DIR/pimerax" /usr/local/bin/pimerax
    echo "  ✔ 已安装到 /usr/local/bin/pimerax"
else
    echo "  提示: 加 --system 参数可安装到 /usr/local/bin"
fi

# 3. 验证
echo "[2/3] 验证..."
"$DIR/pimerax" 2>&1 | head -1

# 4. Python 压缩工具
echo "[3/3] PRX 压缩工具..."
if command -v python3 &>/dev/null; then
    python3 -c "import zstandard" 2>/dev/null || pip3 install zstandard 2>/dev/null || echo "  ⚠ zstandard 未装，prx 压缩功能受限"
    echo "  ✔ prx.py 可用: python3 prx.py 转 <模型.pt> <输出.prx>"
fi

echo ""
echo "=== 安装完成 ==="
echo "用法:"
echo "  ./pimerax info <模型.prx>      # 查看模型"
echo "  ./pimerax gen <模型.prx>       # 生成"
echo "  python3 prx.py 转 <模型.pt> <输出.prx>   # 转换+压缩"
echo "  python3 prx.py 压 <模型.prx> <输出.prx>  # 超压缩"
