#!/bin/bash
# Build rapide avec Ninja + ccache + mold + PCH + parallélisme
# Usage: ./build-fast.sh [jobs]
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build-ninja"
JOBS="${1:-$(nproc)}"

# Dépendances Qt5 locales (téléchargées si /tmp vidé)
QT5_LOCAL=/tmp/qt5-full
if [ ! -d "$QT5_LOCAL" ]; then
    echo "==> Réinstallation des dépendances Qt5 locales..."
    mkdir -p "$QT5_LOCAL"
    cd /tmp
    apt download qtpositioning5-dev libqt5positioning5 libqt5positioning5-plugins qttools5-dev-tools 2>/dev/null
    for deb in qtpositioning5-dev*.deb libqt5positioning5_*.deb libqt5positioning5-plugins*.deb qttools5-dev-tools*.deb; do
        [ -f "$deb" ] && dpkg-deb -x "$deb" "$QT5_LOCAL/"
    done
    ln -sf "$QT5_LOCAL/usr/lib/qt5/bin/lrelease" "$HOME/.local/bin/lrelease" 2>/dev/null
fi

# Configuration si le répertoire de build n'existe pas
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "==> Configuration CMake (Ninja + ccache + mold)..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_LINKER_TYPE=MOLD \
        -DCMAKE_CXX_FLAGS="-pipe" \
        -DQt5Positioning_DIR="$QT5_LOCAL/usr/lib/aarch64-linux-gnu/cmake/Qt5Positioning" \
        ..
    # Patch lrelease (wrapper qtchooser cassé)
    sed -i 's|/usr/bin/lrelease|lrelease|g; s|/usr/lib/qt5/bin/lrelease|lrelease|g' build.ninja
fi

echo "==> Build velo-touch (utitibike) avec $JOBS jobs..."
cd "$BUILD_DIR"
ninja -j"$JOBS"
echo "==> Terminé: $BUILD_DIR/utitibike"
