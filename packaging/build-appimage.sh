#!/bin/sh
# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera o pacote finan-plus_<versão>_<arquitetura>.AppImage a partir do código.
# Uso: packaging/build-appimage.sh   (na raiz do projeto)
set -eu
cd "$(dirname "$0")/.."
VERSION=$(sed -n "s/^  version: '\(.*\)',/\1/p" meson.build)

UNAME_M=$(uname -m)
case "$UNAME_M" in
    x86_64|amd64) ARCH="x86_64" ;;
    aarch64|arm64) ARCH="aarch64" ;;
    armv7l) ARCH="armhf" ;;
    i386|i686) ARCH="i686" ;;
    *) ARCH="$UNAME_M" ;;
esac

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT

rm -rf build-appimage
meson setup build-appimage --prefix=/usr -Dbuildtype=release -Db_lto=true >/dev/null
ninja -C build-appimage
meson test -C build-appimage --print-errorlogs
DESTDIR="$ROOT" meson install -C build-appimage --no-rebuild >/dev/null
strip --strip-unneeded "$ROOT/usr/bin/finan-plus"

# Estrutura padrão do AppDir para AppImage
cp "$ROOT/usr/share/applications/com.finanplus.FinanPlus.desktop" "$ROOT/"
cp "$ROOT/usr/share/icons/hicolor/256x256/apps/com.finanplus.FinanPlus.png" "$ROOT/"
ln -sf com.finanplus.FinanPlus.png "$ROOT/.DirIcon"

# Copia bibliotecas específicas do núcleo para maior portabilidade entre distribuições
mkdir -p "$ROOT/usr/lib"
for lib in libsodium.so* libsecret-1.so* libjson-glib-1.0.so*; do
    for dir in /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib64; do
        if [ -d "$dir" ]; then
            find "$dir" -maxdepth 1 -name "$lib" -exec cp -a -t "$ROOT/usr/lib/" {} + 2>/dev/null || true
        fi
    done
done

# Ponto de entrada (AppRun)
cat > "$ROOT/AppRun" <<'EOT'
#!/bin/sh
set -e
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/usr/lib64:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
if [ -d "${HERE}/usr/share/glib-2.0/schemas" ]; then
    export GSETTINGS_SCHEMA_DIR="${HERE}/usr/share/glib-2.0/schemas:${GSETTINGS_SCHEMA_DIR:+:$GSETTINGS_SCHEMA_DIR}"
fi
exec "${HERE}/usr/bin/finan-plus" "$@"
EOT
chmod 755 "$ROOT/AppRun"

# Localiza ou baixa o appimagetool
APPIMAGETOOL=""
if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL="$(command -v appimagetool)"
elif [ -x "/tmp/appimagetool-$ARCH" ]; then
    APPIMAGETOOL="/tmp/appimagetool-$ARCH"
elif [ -x "/tmp/appimagetool" ]; then
    APPIMAGETOOL="/tmp/appimagetool"
else
    echo "Baixando appimagetool para $ARCH..."
    TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage"
    curl -sSfL "$TOOL_URL" -o "/tmp/appimagetool-$ARCH" || {
        echo "Falha ao baixar $TOOL_URL" >&2
        exit 1
    }
    chmod +x "/tmp/appimagetool-$ARCH"
    APPIMAGETOOL="/tmp/appimagetool-$ARCH"
fi

OUT="finan-plus_${VERSION}_${ARCH}.AppImage"
ARCH="$ARCH" NO_APPSTREAM=1 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$ROOT" "$OUT" >/dev/null
chmod +x "$OUT"
echo "Pacote gerado: $OUT"
