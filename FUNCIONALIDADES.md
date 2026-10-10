# Finan+ para Linux — todas as funcionalidades

Lista completa do que o app faz e de onde fica cada coisa, comparada com o app Android. O código C do núcleo é uma tradução direta do Kotlin e passa nos mesmos casos de teste (núcleo, assistente, relatório, período, calendário e simulador). Desde a 1.2.0 tem as mesmas funções do app Android 1.4.0 e do Finan+ web 1.3.0, menos as que só fazem sentido no celular (veja o fim desta página).

## Janela para computador e notebook

| O quê | Como funciona |
|---|---|
| Barra lateral | Início, Lançamentos, Relatórios, Assistente e Ajustes. No rodapé, o saldo atual e o previsto para o fim do mês ficam sempre à vista. |
| Colunas que se reorganizam | A largura da janela define o layout. Com **720 px ou mais**, o Início fica em duas colunas (saldo, vencimentos e contas na larga; assistente, limites e metas na estreita), Lançamentos e Relatórios em duas; **abaixo de 720 px**, uma só. |
| Janela estreita | Abaixo de 720sp a barra lateral vira uma página, com botão de voltar, e os botões do topo ficam só com o ícone. A janela vai até 360 × 480. |
| Barra superior | Botões "Despesa" e "Receita", busca, ocultar valores, bloquear (quando há PIN) e o menu (PDF, CSV, backup, restaurar, atalhos, sobre). |
| Formulários | Editores em diálogos, no padrão GNOME: Cancelar à esquerda, Salvar à direita, Enter salva. Fechar (Esc, Cancelar ou ×) com alterações não salvas pergunta **"Descartar alterações?"**; sem alterações, fecha direto. |
| Tamanho da janela | O tamanho e o estado maximizado são lembrados entre as sessões. |
| Avisos rápidos | Confirmações curtas na parte de baixo da janela ("Lançamento salvo", "Backup salvo"…). |

### Atalhos de teclado (Ctrl+? mostra todos)

| Atalho | Ação |
|---|---|
| Ctrl+N / Ctrl+Shift+N | Nova despesa / nova receita |
| Ctrl+M | Nova meta |
| Ctrl+F | Buscar lançamentos |
| Page Up / Page Down | Mês anterior / próximo mês (no calendário) |
| Ctrl+K | Perguntar ao assistente |
| Ctrl+1 … Ctrl+5 (Ctrl+, para Ajustes) | Trocar de seção |
| Ctrl+H | Ocultar ou mostrar valores |
| Ctrl+L | Bloquear agora (com PIN) |
| Ctrl+P / Ctrl+E | Relatório em PDF / exportar CSV |
| Ctrl+S / Ctrl+O | Salvar backup JSON / restaurar backup |
| F10 | Menu principal |
| Ctrl+Q | Sair |

Com um diálogo aberto, os atalhos globais esperam, para não abrir um editor por cima do outro.

## Início

- Data do dia ("Sábado, 10 de outubro"), que muda sozinha à meia-noite e quando a janela volta a ficar ativa, e o selo "Privado".
- Saldo atual e saldo previsto para o fim do mês. O previsto inclui pendências e as faturas que vencem até lá.
- Receitas e despesas realizadas no mês e, embaixo delas, o que ainda falta: **"a receber"** e **"a pagar"** (contas pendentes fora do cartão e faturas em aberto que vencem no mês), para o R$ 0,00 não esconder o que vem.
- Barra de uso das receitas e selo "% economizado", só quando já entrou alguma receita.
- **Vencimentos (30 dias)** (só na versão Linux; no Android esse papel é do widget): contas a pagar, valores a receber e faturas. Itens em atraso aparecem em vermelho, e um clique abre o lançamento ou o pagamento da fatura. "Ver pendentes" abre a Lista com o filtro Pendentes.
- **Assistente compacto:** as 2 frases mais úteis do mês (regra em [ASSISTENTE.md](ASSISTENTE.md)), a dica principal e um link só ("Abrir assistente" ou "Ver as N dicas"). O resumo completo, o "Por quê?" e as perguntas ficam na tela Assistente.
- **Contas e cartões:** contas com o saldo de cada uma; cartões com a fatura atual, o vencimento, o disponível e "Pagar fatura". Com uma conta só e nenhum cartão, ela ocupa a linha inteira.
- **Limites do mês** e **Metas** só aparecem quando existe algo, com "Novo"/"Nova" no título. Limites: barra que muda de cor a partir de 80% ("Atenção") e acima do limite. Metas: quanto foi guardado, quanto guardar por mês até o prazo, o mês previsto de conclusão e o aviso "após o prazo".
- **Comece por aqui:** enquanto não há limites ou metas, atalhos para "Definir um limite mensal" e "Criar uma meta"; cada linha some quando deixa de fazer sentido.
- Os botões Receita, Despesa e Meta saíram do cartão de saldo: os botões do topo da janela (e Ctrl+N, Ctrl+Shift+N, Ctrl+M) fazem o mesmo.

## Lançamentos

- Chave **Lista | Calendário** no alto. O calendário está descrito em [CALENDARIO.md](CALENDARIO.md).
- **‹ Outubro de 2026 ›:** as setas andam um mês inteiro. O botão ao lado abre **Período e filtros**: datas livres (De/Até), Este mês, 30 dias, Tudo e a situação (Todos, Realizados, Pendentes). Ele fica destacado quando há período livre ou "Realizados". Um período livre aparece como "01/10/2026 a 15/10/2026". O período também vale para Relatórios.
- Busca por descrição ou categoria sem diferenciar acento ("cafe" encontra "Café").
- **Filtros de um toque:** Todos, Receitas, Despesas e Pendentes (Receitas/Despesas combinam com Pendentes).
- **Resumo do período** num cartão só: Receitas, Despesas e Saldo, com "a receber", "a pagar" e o saldo "previsto" (com as pendências), e a frase "As despesas são X% das receitas do período".
- **Lista por dia:** os lançamentos ficam agrupados por dia ("Hoje · Sábado, 10 de outubro"), com o saldo do dia à direita, pela mesma regra do calendário. Cada linha mostra ícone da categoria, descrição, categoria · conta ou cartão, situação ("Em atraso" em vermelho), valor e o botão de pago/recebido (compras no cartão mostram o ícone do cartão).
- Clique (ou Enter) numa linha abre o editor. A lista mostra cerca de 300 itens por vez, com "Mostrar mais" (um dia nunca fica partido).
- A comparação receitas × despesas saiu da Lista: a análise completa está em Relatórios.

## Editor de lançamento

- Despesa ou receita, descrição, valor ("59,90", "1.500,00", "R$ 2.000"…), categoria (com busca), forma de pagamento (conta ou cartão), conta/cartão, data e "já paga/recebida".
- **Parcelas** (até 60): o valor informado pode ser o total, dividido em centavos com a diferença na 1ª parcela, ou o valor de cada parcela. As parcelas ficam ligadas, e ao excluir uma o app pergunta se exclui as seguintes.
- **Repetir mensalmente**: cria uma recorrência a partir da data.
- Sugestão de categoria do assistente logo abaixo da descrição, com "Usar" e "Por quê?".
- Pagamento de fatura editado mostra o aviso "não conta como despesa nova".
- Lançamento novo pelo calendário já vem com a data do dia; numa data futura, começa como pendente.

## Cartões e faturas

- Dia de fechamento e de vencimento: compras após o fechamento vão para a fatura seguinte.
- O limite usado inclui as parcelas futuras. Os pagamentos abatem primeiro a fatura mais antiga.
- "Pagar fatura" registra o pagamento debitando a conta escolhida, sem contar como despesa nova.

## Recorrências

- Geradas na abertura e na virada do dia. Meses em que o app ficou fechado são recuperados, até 24 de uma vez, e nunca antes da data de início.
- Dia 31 vira o último dia em meses mais curtos. Recorrências podem ser pausadas.

## Relatórios

- O mesmo **‹ mês ›** e **Período e filtros** de Lançamentos (o período é compartilhado), botão **PDF** no título. Só valores realizados.
- **Resumo:** Receitas e Despesas com comparação justa (mês atual contra os mesmos dias do mês anterior; outro mês contra o anterior inteiro; período livre contra o mesmo tamanho logo antes).
- Sem nada realizado no período: "Nada realizado em outubro ainda", com o que falta receber e pagar e o link **Ver no calendário**.
- **E se…?:** simulador de decisões, sem gravar nada ([SIMULADOR.md](SIMULADOR.md)).
- Despesas por categoria em **gráfico de rosca** (7 maiores + "Outras") e em barras, com o aviso de limite mensal ("Dentro do" / "Acima do").
- Evolução dos últimos 6 meses (receitas × despesas), com descrição completa para leitores de tela; texto no lugar do gráfico enquanto nenhum mês tem valores.

## Relatório em PDF

- Atalhos (este mês, mês passado, este ano, 12 meses, tudo) ou datas livres, com prévia dos totais.
- A4: resumo com variação contra o período anterior de mesmo tamanho, a receber, a pagar, média diária e nº de lançamentos.
- Rosca e tabela por categoria (%, nº, média mensal, limite e "acima"), gráfico e tabela mensal, receitas por categoria, 10 maiores despesas.
- Contas e metas, lista completa de lançamentos (opcional), "Como ler este relatório" e "Página n de N".
- Gerado com Cairo, no computador. O diálogo avisa que o PDF não é criptografado.

## Assistente (detalhes em ASSISTENTE.md)

- Sugestão de categoria em três etapas: mesma descrição, aprendizado Naive Bayes com os seus lançamentos e dicionário aberto.
- Resumo do mês, comparado com os mesmos dias do mês anterior. No Início, só as 2 frases mais úteis e a dica principal.
- 7 dicas: duplicado, aumento de preço, ritmo do limite, ritmo do mês, acima da média, pequenos gastos e gastos fixos. Cada dica pode ser dispensada e restaurada.
- Perguntas rápidas em português, com a linha "Como entendi".
- Tudo sem internet, com "Por quê?". Cada função pode ser desligada.

## Ajustes (todos os cartões abrem e fecham com + / −)

| Cartão | O que tem |
|---|---|
| Aparência | Temas Sistema, Claro, Material You, OLED Cinza, Tokyo Night e Nord. "Sistema" acompanha o modo claro/escuro da área de trabalho. |
| Privacidade e segurança | PIN de 4 a 8 números, ocultar valores e bloqueio automático (Imediatamente, 1, 5, 15 ou 30 min sem usar, ou Só ao abrir o app), guardado só neste computador. Informa onde estão os dados e onde está a chave. |
| Avisos de vencimento | Avisos diários a partir das 9h, opção de avisar ao entrar na sessão e "Avisar agora". |
| Assistente | 3 interruptores, restaurar dicas dispensadas e "Ver o que o assistente aprendeu". |
| Contas e cartões | Lista com saldos e limites, editar, ＋ Conta, ＋ Cartão. |
| Recorrências | Lista com tipo, valor, dia, categoria, conta/cartão e "pausada". |
| Limites mensais | Por categoria de despesa (pendentes do mês também contam). |
| Categorias | Adicionar, renomear (leva junto lançamentos, recorrências e limites) e excluir, com as proteções de uso. |
| Dados | Exportar CSV, backup JSON, relatório em PDF, restaurar (com revisão antes de substituir) e apagar tudo. |
| Sobre | Texto do projeto, autoria, licença GPL v3 e licença dos ícones (textos completos dentro do app), e o diálogo "Sobre" com as bibliotecas. |

## Segurança e privacidade

| Recurso | Implementação |
|---|---|
| Criptografia dos dados | XChaCha20-Poly1305 (libsodium), chave aleatória de 256 bits. O cabeçalho do arquivo também é autenticado. |
| Onde fica a chave | No chaveiro do sistema (Secret Service: GNOME Keyring, KWallet), via libsecret, com uma chave por pasta de dados. Sem chaveiro, ela vai para um arquivo 0600, e Ajustes avisa. |
| Gravação segura | Arquivo temporário + fsync + rename, com a versão anterior guardada. |
| Nunca sobrescreve o que não abriu | Se a chave não abre os dados, o app mostra a tela "Não foi possível abrir seus dados" e não grava nada. O usuário pode guardar o arquivo à parte (nada é apagado) e começar do zero. |
| PIN | Hash Argon2id (crypto_pwhash_str), verificado em segundo plano. Depois de 5 erros seguidos, cada tentativa espera 30 s, 1 min, 2 min… (dobrando, até 1 hora). A contagem fica gravada neste computador e a tentativa é contada antes da conferência: fechar o app ou reiniciar não zera a espera. "Remover PIN" segue o mesmo limite. O PIN nunca vai para o backup. |
| Bloqueio | Ao abrir, com Ctrl+L e pelo bloqueio automático: "Imediatamente" (padrão) bloqueia ao sair da janela; N minutos, depois de N minutos fora dela; "Só ao abrir o app" nunca bloqueia sozinho. O seletor de arquivos aberto pelo próprio Finan+ tem 5 minutos de tolerância. A opção fica só neste computador: restaurar um backup não muda a segurança. Bloquear fecha diálogos e seletores de arquivo abertos. |
| Ocultar valores | "R$ ••••" na tela, nos gráficos e nos avisos. Também oculta as porcentagens (uso das receitas, limites e variação entre períodos) e o valor de cada dia do calendário. |
| Backup e CSV | Gravados com permissão só do usuário. O CSV tem proteção contra fórmulas (=, +, -, @). |
| Leitura de backups | Validação completa: tamanho máximo 30 MB, UTF-8, profundidade de JSON, tipos, datas, ids e referências. Itens inválidos são descartados e contados. |

## Diferenças em relação ao Android (e por quê)

| Android | Linux |
|---|---|
| Widget na tela inicial | Não existe widget no GNOME. O mesmo conteúdo fica no rodapé da barra lateral e no cartão "Vencimentos" do Início. |
| Desbloqueio por digital | Não incluído. O PIN funciona em qualquer computador. |
| Trocar de aba ou de mês deslizando o dedo | Barra lateral, atalhos Ctrl+1…5, setas ‹ › e Page Up/Page Down. |
| Acesso pela rede (o celular serve o Finan+ web para outro aparelho) | Não se aplica: no computador os dados já ficam no próprio computador. |
| Tocar e segurar um dia do calendário | Clicar de novo no dia, dois cliques, botão direito ou segurar o botão do mouse. |
| Bloquear capturas de tela | Impossível por aplicativo no Linux/Wayland. Ajustes recomenda "Ocultar valores" ao compartilhar a tela. |
| Notificações com WorkManager às 9h | Com o app aberto: uma vez por dia a partir das 9h. Fechado: opcional, ao entrar na sessão (`finan-plus --avisos`). |
| Material You com cores do papel de parede | No Linux não há essa API. O tema usa o azul do Material Design. |
| Chave no Android Keystore | Chave no chaveiro do sistema (libsecret). |

## Compatibilidade

- Backup JSON idêntico ao do Finan+ web e do app Android (versão 5, valores em reais). Dá para levar os dados de um para o outro nos dois sentidos.
- Requer GTK 4.12+ e libadwaita 1.5+: Ubuntu 24.04 ou mais novo, Debian 13, Fedora 40+, Linux Mint 22.
