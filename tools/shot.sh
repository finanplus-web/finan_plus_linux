#!/bin/sh
# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
# Captura de tela sob Xvfb: shot.sh <saída.png> <página 0-4> [largura] [altura]
out=$1; page=$2; w=${3:-1440}; h=${4:-900}
mkdir -p "$FINAN_PLUS_CONFIG_DIR"
printf "[aparelho]\njanela_largura=%s\njanela_altura=%s\n" "$w" "$h" > "$FINAN_PLUS_CONFIG_DIR/aparelho.ini"
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1 FINAN_PLUS_START_PAGE=$page
xvfb-run -a -s "-screen 0 ${w}x${h}x24" dbus-run-session -- sh -c "
  ./build/finan-plus >/tmp/finan-run.log 2>&1 &
  pid=\$!; sleep 4
  import -window root '$out'
  kill \$pid" 
