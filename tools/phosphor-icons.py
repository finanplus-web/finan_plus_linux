#!/usr/bin/env python3
# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gera os ícones simbólicos do Finan+ para Linux a partir do Phosphor Icons (MIT), como no app Android 1.12.

Uso: npm pack @phosphor-icons/core@2.1.1 && tar xzf phosphor-icons-core-2.1.1.tgz
     python3 tools/phosphor-icons.py package/assets

Para cada linha de tools/phosphor-map.txt ("nome desenho"), grava em data/icons/symbolic:
  fin-<nome>-symbolic.svg      peso Regular (ou Fill, se o nome termina em "-fill")
  fin-<nome>-duo-symbolic.svg  peso Duotone (ícones dentro de quadradinhos: categorias, Meu dinheiro, Ajustes)
e reescreve a lista de ícones em data/finan-plus.gresource.xml. A cor vem do tema (ícone simbólico do GTK).
"""
import os, re, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
assets = sys.argv[1]
out = os.path.join(root, 'data/icons/symbolic')
names = []
for ln in open(os.path.join(root, 'tools/phosphor-map.txt'), encoding='utf-8'):
    ln = ln.strip()
    if not ln or ln.startswith('#'):
        continue
    name, glyph = ln.split()
    names.append((name, glyph))

def clean(svg):
    svg = svg.replace(' fill="currentColor"', '')
    return svg.replace('<svg xmlns="http://www.w3.org/2000/svg"', '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"', 1)

for f in os.listdir(out):
    if f.startswith('fin-') and f.endswith('-symbolic.svg'):
        os.remove(os.path.join(out, f))
files = []
for name, glyph in names:
    weight = 'fill' if name.endswith('-fill') else 'regular'
    src = os.path.join(assets, weight, glyph + ('-fill' if weight == 'fill' else '') + '.svg')
    with open(os.path.join(out, f'fin-{name}-symbolic.svg'), 'w') as o:
        o.write(clean(open(src).read()) + '\n')
    files.append(f'fin-{name}-symbolic.svg')
    if weight == 'regular':
        duo = os.path.join(assets, 'duotone', glyph + '-duotone.svg')
        with open(os.path.join(out, f'fin-{name}-duo-symbolic.svg'), 'w') as o:
            o.write(clean(open(duo).read()) + '\n')
        files.append(f'fin-{name}-duo-symbolic.svg')

gr = os.path.join(root, 'data/finan-plus.gresource.xml')
x = open(gr, encoding='utf-8').read()
x = re.sub(r'\s*<file alias="icons/scalable/actions/fin-[^"]+">[^<]+</file>', '', x)
entries = ''.join(f'\n    <file alias="icons/scalable/actions/{f}">icons/symbolic/{f}</file>' for f in sorted(files))
x = re.sub(r'(<file alias="icons/256x256/apps/com.finanplus.FinanPlus.png">[^<]+</file>)', lambda m: m.group(1) + entries, x, count=1)
open(gr, 'w', encoding='utf-8').write(x)
print(len(files), 'ícones')
