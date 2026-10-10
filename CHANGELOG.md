# Changelog — Finan+ para Linux

## 1.2.1 — Recorrências aparecem nos próximos meses (10/10/2026)

**Problema (relatado pelo autor no Android e na web, mesma regra aqui):** a receita fixa "Adiantamento Quinzenal" (todo dia 15) não aparecia em novembro e dezembro no calendário, enquanto uma despesa parcelada aparecia. Causa: toda recorrência só vira lançamento quando o mês chega; as parcelas são criadas de uma vez.

**Agora:** nos meses que ainda não chegaram, as recorrências ativas aparecem como **Previsto** no calendário, na Lista (quando o período chega lá), nas pendências do mês e no saldo previsto ao fim do dia. Clicar num previsto abre a recorrência; mudar ou pausar a recorrência muda os previstos na hora. **Nada é gravado**: o lançamento real continua sendo criado quando o mês chega. Mesma regra do Android 1.4.1 e do Finan+ web 1.3.1. Detalhes em [RECORRENCIAS.md](RECORRENCIAS.md).

| Arquivo | Mudança |
|---|---|
| `src/core/finance.c` | `projection_between`, `tx_is_projected`; `future_balance` conta os previstos |
| `src/core/period.c` | Calendário (`CalMonth.projected`) e pendências do mês contam os previstos |
| `src/core/ops.c` | Previsto não alterna pago/pendente |
| `src/ui/page_moves.c`, `page_calendar.c` | Lista inclui os previstos; linha "Previsto · recorrência" abre a recorrência (`tx_row_activate`) |
| `tests/test_period.c` | 4 testes novos (os mesmos do Android e da web) |

## 1.2.0 — Calendário, simulador "E se…?", Relatórios, Início e Lista renovados (10/10/2026)

O Finan+ para Linux recebe tudo o que chegou ao app Android (1.1.1 a 1.4.0) e ao Finan+ web (1.2.0 e 1.3.0) desde a primeira versão, com as mesmas regras e os mesmos testes. Fica de fora só o que existe apenas no celular: acesso pela rede, gestos de deslizar e widget.

**Calendário de lançamentos** (Lançamentos › Calendário; detalhes em [CALENDARIO.md](CALENDARIO.md)):
- O mês em grade (semana começando no domingo), com o saldo de cada dia abreviado ("+5,2 mil", "−120"), pontinhos de receita (verde), despesa (vermelho) e cartão (roxo) e alerta nos dias com conta atrasada ou fatura vencida. Hoje tem contorno; o dia escolhido fica preenchido.
- Faturas em aberto no dia do vencimento (clicar abre "Pagar fatura"). Compras no cartão aparecem no dia, mas só contam no saldo pela fatura.
- Totais do mês (Entradas, Saídas, Resultado) e, no dia escolhido: lançamentos, saldo do dia, **saldo previsto ao fim do dia** (de hoje em diante) e botões **Receita** e **Despesa** já com a data.
- **Clicar de novo** no dia escolhido (ou dois cliques), **botão direito** ou **segurar** abre o lançamento novo com a data; numa data futura, ele começa pendente.
- Setas, **Page Up/Page Down** e "Voltar para hoje" trocam de mês. Calendário à esquerda e o dia à direita; em janelas estreitas, um embaixo do outro.
- "Ocultar valores": ficam só os pontinhos. Leitor de tela: cada dia é lido como frase ("6 de outubro, terça-feira, 1 lançamento, saldo do dia menos R$ 119,90, em atraso").

**Simulador "E se…?"** (em Relatórios; detalhes e contas em [SIMULADOR.md](SIMULADOR.md)): quatro perguntas — economizar por mês, quanto tempo para comprar algo, mudança na renda e antecipar uma dívida parcelada. Parte da média dos 3 meses completos anteriores (só realizados), que dá para ajustar. **Nada é gravado.** "Transformar em meta" abre o formulário de meta já preenchido.

**Relatórios renovados:** mesmo ‹ mês › da aba Lançamentos (período compartilhado), botão **PDF** no título, Receitas e Despesas realizadas com comparação justa (mês atual contra os mesmos dias do mês anterior; outro mês contra o anterior inteiro; período livre contra o mesmo tamanho logo antes), cartão "Nada realizado… ainda" com o que falta receber e pagar e o link **Ver no calendário**, e texto no lugar do gráfico de evolução vazio. Saiu o cartão "Este mês × mês anterior".

**Início mais enxuto:**
- Data no formato "Sábado, 10 de outubro". Embaixo de Receitas e Despesas do mês: "a receber" e "a pagar" (inclui faturas que vencem no mês). A barra de uso das receitas só aparece quando já entrou receita.
- Saíram os botões Receita, Despesa e Meta do cartão de saldo (os botões do topo e Ctrl+N, Ctrl+Shift+N e Ctrl+M fazem o mesmo).
- Assistente compacto: as 2 frases mais úteis (regra em [ASSISTENTE.md](ASSISTENTE.md)), a dica principal e um link só.
- Seções com um título só; uma conta só ocupa a linha inteira; Limites e Metas só aparecem quando existem, e antes disso o cartão **"Comece por aqui"** tem os atalhos.
- O cartão "Vencimentos (30 dias)", que só existe no computador, continua, agora com "Ver pendentes".
- Duas colunas a partir de 720 px: saldo, vencimentos e contas na larga; assistente, limites, metas e "Comece por aqui" na estreita.

**Lançamentos › Lista mais enxuta:** ‹ mês › com o botão **Período e filtros** (datas livres, Este mês, 30 dias, Tudo e a situação, inclusive "Realizados"); busca compacta; filtros de um toque (Todos, Receitas, Despesas, Pendentes); resumo do período num cartão só, com o que está pendente e o saldo previsto; lançamentos agrupados por dia com o saldo do dia (a data não se repete em cada linha). Saiu a comparação receitas × despesas (está em Relatórios).

**Segurança e acabamento (auditoria do app Android 1.1.1):**
- **Limite de tentativas do PIN gravado no computador:** depois de 5 erros, a espera começa em 30 s e dobra até 1 hora. Fechar o app ou reiniciar o computador não zera mais a contagem (antes ficava só na memória). A tentativa é contada antes da conferência, e "Remover PIN" em Ajustes segue o mesmo limite.
- **Bloqueio automático** saiu dos dados (que vão para o backup) e ficou só neste computador: restaurar um backup não muda mais a segurança. Novas opções: *Imediatamente* (padrão, bloqueia ao sair da janela), 1, 5, 15 ou 30 minutos, ou *Só ao abrir o app*. Quem usava "Desativado" passa para *Imediatamente*, como no Android; quem prefere o comportamento antigo escolhe *Só ao abrir o app*. Voltar do seletor de arquivos do próprio Finan+ não pede o PIN de novo (até 5 minutos).
- **"Ocultar valores"** também oculta as porcentagens (uso das receitas, limites, variação entre períodos e a parte do que sobra no simulador).
- **Descartar alterações?** Fechar um editor (Esc, Cancelar ou ×) com alterações não salvas pergunta antes de descartar; sem alterações, fecha direto.

| Arquivo | Mudança |
|---|---|
| `src/core/period.c`, `period.h` (novos) | Período e setas de mês, pendências, saldo do dia, comparação dos Relatórios e regras do calendário (tradução de `Period.kt` e `MonthCalendar.kt`) |
| `src/core/simulator.c`, `simulator.h` (novos) | Base, economizar, comprar, renda, dívidas e quitação (tradução de `Simulator.kt`) |
| `src/core/assist_insights.c`, `assist.h` | `MonthReport.highlights`: as 2 frases do Início por prioridade |
| `src/core/prefs.c`, `prefs.h` | Limite de tentativas do PIN gravado (relógio monotônico no mesmo boot, de parede em outro) e bloqueio automático do computador |
| `src/ui/page_calendar.c` (novo) | Tela do calendário |
| `src/ui/simulator.c` (novo) | Diálogo "E se…?" |
| `src/ui/page_home.c` | Início enxuto |
| `src/ui/page_moves.c` | Chave Lista/Calendário, ‹ mês ›, Período e filtros, filtros de um toque, resumo e lista por dia |
| `src/ui/page_reports.c` | Relatórios renovados |
| `src/ui/editors.c` | `editor_tx_on` (lançamento com data), `editor_goal_pre` (meta preenchida), "Descartar alterações?" |
| `src/ui/window.c`, `lock.c`, `page_settings.c`, `data.c`, `pdf.c` | Bloqueio automático novo, limite de tentativas e tolerância do seletor de arquivos |
| `data/icons/symbolic/` | Ícones `chevron-left` e `view-list` (Material Symbols, Apache 2.0) |
| `tests/test_period.c` (novo), `tests/test_store.c` | 17 casos do Android 1.4.0 (período, calendário, simulador e frases do Início) e o limite de tentativas: 88 testes no total |

Os dados e o formato do backup **não mudaram** (o campo `autoLock` continua sendo lido e gravado, para compatibilidade, mas não manda mais no bloqueio deste computador).

Como foi verificado: os 88 testes passaram; o app foi aberto sob Xvfb (GTK 4.14, libadwaita 1.5, Ubuntu 24.04, o mesmo do GitHub Actions) com dados de demonstração, em 1440, 1100 e 700 px de largura: Início, Lista, filtros, Período e filtros, calendário (clique, dois cliques, botão direito, lançamento com data futura), Relatórios, as quatro perguntas do simulador, "Ocultar valores" e "Descartar alterações?", sem avisos do GTK.

## Ícone novo: F+ (10/10/2026)

- O ícone do Finan+ passou a ser o monograma **F+**: o F em azul (`#4269d8`) com o "+" num círculo, sobre o fundo claro do app (`#eef4ff`, com os brilhos azul e rosa). É o mesmo ícone do app Android e do Finan+ web.
- `data/icons/app/` traz os PNG novos de 16 a 512 px (instalados em `hicolor/<tamanho>/apps`) e dois arquivos novos que o `meson install` também instala: `com.finanplus.FinanPlus.svg` em `hicolor/scalable/apps` (nítido em qualquer tamanho e escala de tela) e `com.finanplus.FinanPlus-symbolic.svg` em `hicolor/symbolic/apps` (silhueta para painéis e temas que usam ícones simbólicos).
- O ícone aparece no menu de aplicativos, na janela, na tela de bloqueio e nas notificações. Nenhum código C mudou: tudo usa o nome `com.finanplus.FinanPlus`, como antes.
- Fontes em vetor (com e sem sombra, só o símbolo e uma cor) em [`docs/icone/`](docs/icone/).

## 1.1.8 — correção das dicas de ritmo do assistente (08/10/2026)

**Problema (relatado pelo autor no Finan+ web, mesma regra aqui):** no dia 8, com R$ 500 de receita e uma única despesa de R$ 200, o assistente avisava "Despesas podem passar das receitas" com R$ 775 previstos. A conta multiplicava aquela compra pelos dias do mês (R$ 200 ÷ 8 × 31), como se ela se repetisse todo dia. O mesmo valia para "Ritmo do limite".

**Regra nova** (`insights_project` em `src/core/assist_insights.c`; detalhes em [ASSISTENTE.md](ASSISTENTE.md)), a mesma do app Android 1.3.0 e do Finan+ web 1.2.1:
- **Mínimo de dados:** a projeção só é feita com pelo menos **5 despesas variáveis pagas no mês** ("Ritmo do mês") ou **3 na categoria** ("Ritmo do limite"). Com menos, a dica não aparece.
- **Gasto pontual:** uma despesa que sozinha passa de **metade** do gasto variável conta uma vez, sem ser multiplicada pelos dias.
- O "Por quê?" mostra o gasto pontual separado e explica o mínimo de despesas.

Testes: os dois testes antigos de ritmo usavam uma ou duas despesas (o padrão do problema) e passaram a usar dados suficientes; um teste novo cobre o caso relatado e o gasto pontual. 70 no total.

## 1.1.7 — correções da auditoria do app Android (06/10/2026)

A auditoria do Finan+ Android encontrou três erros nas regras financeiras que as três versões compartilham. Esta versão aplica as mesmas correções do Android 1.1.1 e do Finan+ web 1.1.2.

- **Recorrência reativada:** ao reativar uma recorrência pausada (Ajustes › Recorrências), os meses em que ela ficou parada não geram mais lançamentos de uma vez. Ela retoma a partir do mês atual (`ops_resumed_last`). Uma recorrência que já estava ativa continua recuperando os meses atrasados.
- **Pagamento de fatura:** em Lançamentos, o pagamento de fatura mostrava o botão de pago/pendente, e desmarcá-lo descontava o mesmo valor duas vezes (fatura reaberta + pagamento pendente). Agora ele aparece com um ícone fixo, e o núcleo recusa a troca (`ops_can_toggle_paid`), como já acontecia com as compras no cartão.
- **Backup com valores gigantes ou booleanos:** valores acima de R$ 9.999.999.999.999,99 (o limite da digitação) e `true`/`false` em campos de dinheiro são recusados; antes, somas estouravam e o saldo podia trocar de sinal.
- 3 testes novos: 69 no total.

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
