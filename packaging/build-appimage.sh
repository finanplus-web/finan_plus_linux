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

# Copia bibliotecas específicas do núcleo para maior portabilidade entre distribuições.
# A biblioteca de PDF (poppler) NÃO vai junto: a cópia do Ubuntu depende de libjpeg.so.8, que o Debian
# não tem (usa libjpeg.so.62), e o app nem abria no Debian 13 (1.3.0). A libpoppler-glib.so.8 do próprio
# sistema tem a mesma interface em todas as distribuições, e é a que o .deb já usa.
mkdir -p "$ROOT/usr/lib"
for lib in libsodium.so* libsecret-1.so* libjson-glib-1.0.so*; do
    for dir in /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib64; do
        if [ -d "$dir" ]; then
            find "$dir" -maxdepth 1 -name "$lib" -exec cp -a -t "$ROOT/usr/lib/" {} + 2>/dev/null || true
        fi
    done
done

# Toda biblioteca copiada só pode depender do que existe em qualquer distribuição recente (glib, libc…):
# nada que seja exclusivo do sistema onde o AppImage foi gerado.
for f in "$ROOT"/usr/lib/*.so.*; do
    [ -L "$f" ] && continue
    for need in $(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        case "$need" in
            libc.so.*|libm.so.*|ld-linux*|libglib-2.0.so.*|libgio-2.0.so.*|libgobject-2.0.so.*|libgmodule-2.0.so.*|libgcrypt.so.*|libgpg-error.so.*|libsodium.so.*) ;;
            *) echo "Biblioteca copiada $f depende de $need, que pode não existir em outras distribuições" >&2; exit 1 ;;
        esac
    done
done

# Ponto de entrada (AppRun)
cat > "$ROOT/AppRun" <<'EOT'
#!/bin/sh
set -e
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="${HERE}/usr/bin:${PATH}"
# sem ":" sobrando no fim: uma entrada vazia faria o sistema procurar bibliotecas na pasta atual
export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
if [ -d "${HERE}/usr/share/glib-2.0/schemas" ]; then
    export GSETTINGS_SCHEMA_DIR="${HERE}/usr/share/glib-2.0/schemas${GSETTINGS_SCHEMA_DIR:+:$GSETTINGS_SCHEMA_DIR}"
fi
exec "${HERE}/usr/bin/finan-plus" "$@"
EOT
chmod 755 "$ROOT/AppRun"

# Localiza ou baixa o appimagetool: versão fixa, conferida pelo SHA-256 (nada de "a mais recente" sem checagem)
APPIMAGETOOL_VERSION=1.9.1
case "$ARCH" in
    x86_64) TOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;;
    aarch64) TOOL_SHA256=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;;
    *) TOOL_SHA256="" ;;
esac
APPIMAGETOOL="${APPIMAGETOOL:-}"
if [ -z "$APPIMAGETOOL" ] && command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL="$(command -v appimagetool)"
fi
if [ -z "$APPIMAGETOOL" ]; then
    [ -n "$TOOL_SHA256" ] || { echo "Sem appimagetool conferido para $ARCH: instale-o ou defina APPIMAGETOOL=/caminho" >&2; exit 1; }
    CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/finan-plus"
    mkdir -p "$CACHE"
    APPIMAGETOOL="$CACHE/appimagetool-${APPIMAGETOOL_VERSION}-${ARCH}.AppImage"
    if ! echo "$TOOL_SHA256  $APPIMAGETOOL" | sha256sum -c - >/dev/null 2>&1; then
        TOOL_URL="https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-${ARCH}.AppImage"
        echo "Baixando appimagetool ${APPIMAGETOOL_VERSION} para $ARCH..."
        curl -sSfL "$TOOL_URL" -o "$APPIMAGETOOL.part" || { echo "Falha ao baixar $TOOL_URL" >&2; exit 1; }
        echo "$TOOL_SHA256  $APPIMAGETOOL.part" | sha256sum -c - >/dev/null || { echo "appimagetool com SHA-256 diferente do esperado" >&2; rm -f "$APPIMAGETOOL.part"; exit 1; }
        mv "$APPIMAGETOOL.part" "$APPIMAGETOOL"
    fi
    chmod +x "$APPIMAGETOOL"
fi

OUT="finan-plus_${VERSION}_${ARCH}.AppImage"
ARCH="$ARCH" NO_APPSTREAM=1 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$ROOT" "$OUT" >/dev/null
chmod +x "$OUT"
echo "Pacote gerado: $OUT"
