#!/bin/sh
# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
# Captura com interação: shot2.sh <saída.png> <página> <largura> <altura> "<comandos xdotool separados por ;>"
out=$1; page=$2; w=$3; h=$4; cmds=$5
mkdir -p "$FINAN_PLUS_CONFIG_DIR"
grep -q janela_largura "$FINAN_PLUS_CONFIG_DIR/aparelho.ini" 2>/dev/null && sed -i "s/^janela_largura=.*/janela_largura=$w/; s/^janela_altura=.*/janela_altura=$h/" "$FINAN_PLUS_CONFIG_DIR/aparelho.ini" || printf "[aparelho]\njanela_largura=%s\njanela_altura=%s\n" "$w" "$h" >> "$FINAN_PLUS_CONFIG_DIR/aparelho.ini"
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1 FINAN_PLUS_START_PAGE=$page
xvfb-run -a -s "-screen 0 ${w}x${h}x24" dbus-run-session -- sh -c "
  ${FINAN_BIN:-./build/finan-plus} >/tmp/finan-run.log 2>&1 &
  pid=\$!; sleep 3
  wid=\$(xdotool search --name 'Finan' | head -1)
  xdotool windowactivate --sync \$wid 2>/dev/null; xdotool windowfocus \$wid 2>/dev/null
  IFS=';'; for c in \$CMDS; do eval xdotool \$c; sleep 0.6; done
  sleep 1.2
  import -window root '$out'
  kill \$pid" 
