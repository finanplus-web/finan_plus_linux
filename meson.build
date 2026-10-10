# Finan+ — Copyright (C) 2026 Juscelino Be
# SPDX-License-Identifier: GPL-3.0-or-later
project('finan-plus', 'c',
  version: '1.1.8',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.63',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

cc = meson.get_compiler('c')
add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Wformat=2', '-Wformat-security',
    '-Werror=implicit-function-declaration', '-Werror=format-security', '-fstack-protector-strong',
    '-fno-strict-aliasing', '-D_FORTIFY_SOURCE=2']),
  '-DAPP_VERSION="@0@"'.format(meson.project_version()),
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76',
  language: 'c')
add_project_link_arguments(cc.get_supported_link_arguments(['-Wl,-z,relro', '-Wl,-z,now', '-Wl,--as-needed']), language: 'c')

glib = dependency('glib-2.0', version: '>= 2.76')
gio = dependency('gio-2.0')
json = dependency('json-glib-1.0')
sodium = dependency('libsodium')
secret = dependency('libsecret-1')
gtk = dependency('gtk4', version: '>= 4.12')
adw = dependency('libadwaita-1', version: '>= 1.5')
m = cc.find_library('m')

gnome = import('gnome')
resources = gnome.compile_resources('finan-resources', 'data/finan-plus.gresource.xml',
  source_dir: 'data', c_name: 'finan')

core_src = files(
  'src/core/date.c', 'src/core/model.c', 'src/core/money.c', 'src/core/finance.c', 'src/core/ops.c',
  'src/core/backup.c', 'src/core/assist_text.c', 'src/core/assist_dict.c', 'src/core/assist_categorizer.c',
  'src/core/assist_insights.c', 'src/core/assist_ask.c', 'src/core/report.c', 'src/core/store.c', 'src/core/prefs.c',
  'src/core/period.c', 'src/core/simulator.c')

# núcleo sem interface (testado separadamente)
core_lib = static_library('financore', core_src,
  include_directories: include_directories('src'),
  dependencies: [glib, gio, json, sodium, secret, m])
core_dep = declare_dependency(link_with: core_lib, include_directories: include_directories('src'),
  dependencies: [glib, gio, json, sodium, secret, m])

ui_src = files(
  'src/main.c', 'src/ui/window.c', 'src/ui/theme.c', 'src/ui/widgets.c', 'src/ui/page_home.c',
  'src/ui/page_moves.c', 'src/ui/page_reports.c', 'src/ui/page_assist.c', 'src/ui/page_settings.c',
  'src/ui/editors.c', 'src/ui/lock.c', 'src/ui/data.c', 'src/ui/notify.c', 'src/ui/misc.c', 'src/ui/pdf.c',
  'src/ui/pdf_render.c')

executable('finan-plus', ui_src, resources,
  dependencies: [core_dep, gtk, adw],
  install: true)

# ---------------------------------------------------------------- arquivos do sistema
datadir = get_option('datadir')
install_data('data/com.finanplus.FinanPlus.desktop', install_dir: datadir / 'applications')
install_data('data/com.finanplus.FinanPlus.metainfo.xml', install_dir: datadir / 'metainfo')
foreach s : ['16', '32', '48', '64', '128', '192', '256', '512']
  install_data('data/icons/app/@0@.png'.format(s),
    install_dir: datadir / 'icons/hicolor/@0@x@0@/apps'.format(s),
    rename: 'com.finanplus.FinanPlus.png')
endforeach
install_data('data/icons/app/com.finanplus.FinanPlus.svg', install_dir: datadir / 'icons/hicolor/scalable/apps')
install_data('data/icons/app/com.finanplus.FinanPlus-symbolic.svg', install_dir: datadir / 'icons/hicolor/symbolic/apps')
install_data('data/dicionario.txt', install_dir: datadir / 'finan-plus')
install_data('LICENSE', 'README.md', 'FUNCIONALIDADES.md', 'ASSISTENTE.md', 'CHANGELOG.md', install_dir: datadir / 'doc/finan-plus')
install_data('third_party/material-symbols/LICENSE', install_dir: datadir / 'doc/finan-plus', rename: 'LICENSE-icones-Apache-2.0')
install_man('data/finan-plus.1')

# ---------------------------------------------------------------- testes
test_core = executable('test-core', 'tests/test_core.c',
  dependencies: [core_dep],
  c_args: ['-DDICT_PATH="@0@"'.format(meson.current_source_dir() / 'data/dicionario.txt')])
test('núcleo (60 casos do app Android)', test_core, env: ['FINAN_PLUS_NO_KEYRING=1'])
test_period = executable('test-period', 'tests/test_period.c', dependencies: [core_dep])
test('período, calendário e simulador (casos do Android 1.4.0)', test_period, env: ['FINAN_PLUS_NO_KEYRING=1'])
test_store = executable('test-store', 'tests/test_store.c', dependencies: [core_dep])
test('armazenamento e PIN', test_store, env: ['FINAN_PLUS_NO_KEYRING=1'])
test_pdf = executable('test-pdf', 'tests/test_pdf.c', 'src/ui/pdf_render.c', 'src/ui/theme.c',
  dependencies: [core_dep, gtk, adw])
test('relatório em PDF', test_pdf)

# ferramenta de desenvolvimento (não instalada): importa um JSON para o armazenamento criptografado
executable('import-json', 'tools/import-json.c', dependencies: [core_dep], install: false)
