# Changelog — Finan+ para Linux

## 1.1.6 — cantos das listas arredondadas (05/10/2026)

- Com tema próprio do sistema (ex.: Blackline no KDE), as listas arredondadas — como "O que há de novo" e "Detalhes" na janela Sobre, e as dos editores — mostravam um quadrado claro atrás dos cantos: o tema pintava o fundo da lista sem arredondar. O tema do Finan+ agora define fundo, cantos e contorno dessas listas (`list.boxed-list` em `build_widgets()`, `src/ui/theme.c`).
- A barra lateral também deixa de receber o fundo de linha desses temas; só o item escolhido e o item sob o mouse ficam destacados.
- Conferido nos temas Claro e Tokyo Night com um `gtk.css` que imita o problema.

## 1.1.5 — acabamento visual (05/10/2026)

- **Lançamentos vazio:** sem lançamentos no período, a lista ficava em branco. Agora aparece um aviso com ícone ("Nenhum lançamento neste período" e como adicionar). O aviso é um componente próprio ao lado da lista, porque `gtk_list_box_remove_all()` descartava o aviso nativo da `GtkListBox` a cada atualização.
- **Tema próprio do sistema (KDE, ex.: Blackline):** o `~/.config/gtk-4.0/gtk.css` desses temas repinta botões principais, seleção da barra lateral, campos, chaves e caixas de marcar, deixando cinza sobre cinza dentro do Finan+. O tema do Finan+ agora também define essas partes diretamente (`build_widgets()` em `src/ui/theme.c`), só dentro do app.
- **Seletor Despesa/Receita** do novo lançamento: o lado escolhido usa a cor de destaque do tema (antes ficava cinza apagado).
- **Só ícones Material:** setas ↑/↓ de Receitas/Despesas, "●" de Privado, "✓"/"⚠" de metas e limites e as bolinhas de cor dos temas eram símbolos de texto; viraram ícones Material Symbols (função nova `w_icon_label()` em `widgets.c`) ou desenho. As bolinhas de cor dos temas ganharam contorno, para a cor clara não sumir no cartão claro.
- **Gráfico "Últimos 6 meses":** mês sem valor não desenha mais um tracinho no zero.
- Conferido em português, GTK 4.18 + libadwaita 1.7, com um `gtk.css` imitando tema escuro do KDE: Início, Lançamentos (vazio e com dados), Relatórios, Ajustes e novo lançamento, temas Sistema e Tokyo Night.

## 1.1.4 — Relatórios não fecham mais o app (05/10/2026)

- Abrir Relatórios sem lançamentos nos últimos 6 meses fechava o programa (divisão por zero no gráfico "Últimos 6 meses": a escala do eixo arredondava para zero). A escala agora tem passo mínimo de R$ 1,00, e sem valores o gráfico mostra o eixo de R$ 0 a R$ 100.
- Novo teste: o passo da escala nunca é zero, para qualquer valor de 0 a R$ 4,99.
- Conferido abrindo as 5 telas sem nenhum lançamento, em português, com GTK 4.14 e 4.18.

## 1.1.3 — botões da janela na tela de bloqueio (05/10/2026)

- Com o app bloqueado pelo PIN, a janela não tinha os botões de fechar, minimizar e maximizar (a barra superior fica escondida para não mostrar nada). Agora a tela de bloqueio e a tela "Não foi possível abrir seus dados" têm uma barra transparente só com esses botões; também dá para arrastar a janela por ela.

## 1.1.2 — temas com o sistema em português (05/10/2026)

**Causa real de "clico no tema e nada muda":** as cores do tema eram escritas com a vírgula decimal do idioma do sistema. Em português saía `rgba(17,24,39,0,86)` em vez de `rgba(17,24,39,0.860)`; o GTK recusava todas as cores ("Theme parser error: Expected ')' at end of rgba()") e nenhum tema era aplicado. Agora as cores são escritas sempre com ponto (`g_ascii_formatd`), independentemente do idioma.

Conferido com o sistema em português (pt_BR.UTF-8), GTK 4.18 e libadwaita 1.7: a versão anterior gerava 474 erros de tema ao abrir; esta, nenhum, e os 6 temas trocam normalmente.

## 1.1.1 — correção dos temas (05/10/2026)

Em alguns sistemas, clicar num tema em Ajustes › Aparência não mudava nada. Causas tratadas:

- **Tema forçado pelo sistema:** algumas distribuições e ferramentas de personalização gravam `~/.config/gtk-4.0/gtk.css` redefinindo as cores do libadwaita, e esse arquivo tinha prioridade sobre o Finan+. Agora o tema do Finan+ fica acima dele (só dentro do Finan+; os outros programas continuam como estão).
- **`GTK_THEME`:** se essa variável estiver definida, ela é ignorada pelo Finan+, porque força um tema GTK por cima do libadwaita.
- **libadwaita 1.6 ou mais nova** (distribuições recentes) pinta os componentes com variáveis CSS; o tema agora define também essas variáveis.
- Trocar o tema mostra a confirmação "Tema: …", e, se a gravação falhar, o app avisa (antes o erro ficava escondido).
- Novo `finan-plus --diagnostico`: mostra as versões do GTK e do libadwaita, `GTK_THEME` e se existe `gtk.css` do usuário. Com `FINAN_PLUS_DEBUG=1`, o app registra no terminal cada tema aplicado.

Testado sob Xvfb com um `gtk.css` do usuário que força outras cores e com `GTK_THEME=Adwaita:dark`: os 6 temas trocam normalmente. Os 66 testes continuam passando.

## 1.1.0 — primeira versão para Linux (03/10/2026)

Versão nativa em **C com GTK 4 e libadwaita**, com todas as funções do app Android 1.1.0 e janela pensada para computador e notebook. A lista completa está em [FUNCIONALIDADES.md](FUNCIONALIDADES.md).

### O que foi feito

| Parte | Arquivos | O que é |
|---|---|---|
| Núcleo | `src/core/date.c`, `model.c`, `money.c`, `finance.c`, `ops.c`, `backup.c` | Tradução do núcleo Kotlin: datas, modelo, dinheiro em centavos, faturas, saldos, recorrências, parcelas, metas, lembretes, CSV, validações com as mesmas mensagens e backup JSON compatível |
| Assistente | `src/core/assist_*.c`, `assist.h` | Texto, dicionário, categorizador (Naive Bayes), resumo, 7 dicas e perguntas, com as mesmas regras e limites |
| Relatório | `src/core/report.c` | Números do relatório em PDF |
| Armazenamento | `src/core/store.c` | Arquivo criptografado (libsodium) com a chave no chaveiro do sistema (libsecret) e gravação atômica. Nunca sobrescreve dados que não conseguiu abrir |
| Este computador | `src/core/prefs.c` | PIN (Argon2id), avisos, assistente, dicas dispensadas e tamanho da janela |
| Janela | `src/ui/window.c`, `widgets.c`, `theme.c` | Barra lateral, layout em 1, 2 ou 3 colunas conforme a largura, atalhos, temas em CSS, relógio do dia, bloqueio automático |
| Telas | `src/ui/page_home.c`, `page_moves.c`, `page_reports.c`, `page_assist.c`, `page_settings.c` | Início, Lançamentos, Relatórios, Assistente e Ajustes |
| Diálogos | `src/ui/editors.c`, `lock.c`, `data.c`, `pdf.c`, `pdf_render.c`, `notify.c`, `misc.c` | Editores, PIN, arquivos, PDF (Cairo + Pango), avisos, Sobre e atalhos |
| Sistema | `data/*.desktop`, `*.metainfo.xml`, `finan-plus.1`, ícones | Integração com o menu de aplicativos, ícone, manual (`man finan-plus`) |
| Ícones | `data/icons/symbolic/` | 76 Material Symbols Rounded (peso 300): os 36 do app Android, convertidos de volta para SVG, e 40 novos para a interface de computador. Apache 2.0, ver `third_party/material-symbols/` |
| Testes | `tests/test_core.c`, `test_store.c`, `test_pdf.c` | Os 60 casos do app Android, mais 4 de armazenamento/PIN e 2 de PDF |

### Novidades em relação ao Android

- Cartão "Vencimentos" (30 dias) no Início e saldo na barra lateral, no lugar do widget.
- Rosca de despesas por categoria também na tela de Relatórios, não só no PDF.
- Atalhos de teclado para todas as ações principais.
- Opção de avisar vencimentos ao entrar na sessão, com o app fechado.
- Tela de recuperação quando a chave do chaveiro não abre os dados (nada é sobrescrito).

### Verificação

- `meson test`: 66 testes, todos passando. O núcleo também foi testado com AddressSanitizer e UndefinedBehaviorSanitizer.
- A interface foi exercitada sob Xvfb com ASan (criar lançamento, marcar como pago, exportar CSV/backup, PDF, assistente, Ajustes, PIN), sem erros de memória.
- Uma revisão de código encontrou 4 problemas, já corrigidos:
  - atalhos ativos na tela de problema poderiam gravar por cima dos dados;
  - seletores de arquivo continuavam abertos depois do bloqueio;
  - Enter repetido podia salvar duas vezes;
  - a busca de categoria no editor não filtrava.
