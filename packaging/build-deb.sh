#!/bin/sh
# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera o pacote finan-plus_<versão>_<arquitetura>.deb a partir do código.
# Uso: packaging/build-deb.sh   (na raiz do projeto)
set -eu
cd "$(dirname "$0")/.."
VERSION=$(sed -n "s/^  version: '\(.*\)',/\1/p" meson.build)
ARCH=$(dpkg --print-architecture)
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT

rm -rf build-deb
meson setup build-deb --prefix=/usr -Dbuildtype=release -Db_lto=true >/dev/null
ninja -C build-deb
meson test -C build-deb --print-errorlogs
DESTDIR="$ROOT" meson install -C build-deb --no-rebuild >/dev/null
strip --strip-unneeded "$ROOT/usr/bin/finan-plus"

# documentação no formato Debian
DOC="$ROOT/usr/share/doc/finan-plus"
rm -f "$DOC/LICENSE"
cat > "$DOC/copyright" <<EOT
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Finan+
Upstream-Contact: Juscelino Be

Files: *
Copyright: 2026 Juscelino Be
License: GPL-3.0-or-later
 Este programa é software livre: você pode redistribuí-lo e/ou modificá-lo sob os termos da
 GNU General Public License publicada pela Free Software Foundation, na versão 3 da licença
 ou (a seu critério) qualquer versão posterior.
 .
 Em sistemas Debian, o texto completo está em /usr/share/common-licenses/GPL-3.

Files: data/icons/symbolic/*
Copyright: Google LLC
License: Apache-2.0
 Em sistemas Debian, o texto completo está em /usr/share/common-licenses/Apache-2.0.
EOT
gzip -9n "$DOC/CHANGELOG.md"
cat > "$DOC/changelog" <<EOT
finan-plus ($VERSION) unstable; urgency=medium

  * Finan+ para Linux $VERSION (GTK 4 + libadwaita). Novidades desta e das
    versões anteriores em CHANGELOG.md.gz.

 -- Juscelino Be <juscelino@finanplus.invalid>  $(LC_ALL=C date -R)
EOT
gzip -9n "$DOC/changelog"
gzip -9n "$ROOT/usr/share/man/man1/finan-plus.1"
find "$ROOT" -type d -exec chmod 755 {} +
find "$ROOT/usr/share" -type f -exec chmod 644 {} +

mkdir -p "$ROOT/DEBIAN"
SIZE=$(du -sk "$ROOT/usr" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<EOT
Package: finan-plus
Version: $VERSION
Architecture: $ARCH
Maintainer: Juscelino Be <juscelino@finanplus.invalid>
Installed-Size: $SIZE
Depends: libc6 (>= 2.34), libglib2.0-0t64 (>= 2.76) | libglib2.0-0 (>= 2.76), libgtk-4-1 (>= 4.12), libadwaita-1-0 (>= 1.5), libjson-glib-1.0-0 (>= 1.6), libsodium23 (>= 1.0.18), libsecret-1-0 (>= 0.20), libcairo2 (>= 1.16), libpango-1.0-0 (>= 1.50), libpangocairo-1.0-0 (>= 1.50), librsvg2-common, hicolor-icon-theme
Recommends: gnome-keyring | kwalletmanager | keepassxc
Section: utils
Priority: optional
Description: controle financeiro pessoal, privado e offline (GTK 4)
 Finan+ organiza receitas, despesas, contas, cartões de crédito e faturas,
 parcelas, recorrências, limites mensais e metas. Inclui relatórios,
 relatório em PDF, um assistente que roda no computador (sugere categorias,
 resume o mês, dá dicas e responde perguntas, sem internet), bloqueio por
 PIN e avisos de vencimento.
 .
 Os dados ficam só no computador, criptografados com libsodium e chave no
 chaveiro do sistema. O backup JSON é compatível com o Finan+ web e com o
 app Android.
EOT
cat > "$ROOT/DEBIAN/postinst" <<'EOT'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/share/applications || true
fi
EOT
cat > "$ROOT/DEBIAN/postrm" <<'EOT'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/share/applications || true
fi
EOT
chmod 755 "$ROOT/DEBIAN/postinst" "$ROOT/DEBIAN/postrm"
(cd "$ROOT" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums)

OUT="finan-plus_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build -Zxz "$ROOT" "$OUT" >/dev/null
echo "Pacote gerado: $OUT"
