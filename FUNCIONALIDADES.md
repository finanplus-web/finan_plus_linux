# Finan+ para Linux — todas as funcionalidades

Lista completa do que o app faz e de onde fica cada coisa, comparada com o app Android. O código C do núcleo é uma tradução direta do Kotlin e passa nos mesmos 60 testes.

## Janela para computador e notebook

| O quê | Como funciona |
|---|---|
| Barra lateral | Início, Lançamentos, Relatórios, Assistente e Ajustes. No rodapé, o saldo atual e o previsto para o fim do mês ficam sempre à vista. |
| Colunas que se reorganizam | A largura útil define o layout. Com **1080 px ou mais** são três colunas; **de 720 a 1080 px**, duas; **abaixo de 720 px**, uma só. |
| Janela estreita | Abaixo de 720sp a barra lateral vira uma página, com botão de voltar, e os botões do topo ficam só com o ícone. A janela vai até 360 × 480. |
| Barra superior | Botões "Despesa" e "Receita", busca, ocultar valores, bloquear (quando há PIN) e o menu (PDF, CSV, backup, restaurar, atalhos, sobre). |
| Formulários | Editores em diálogos, no padrão GNOME: Cancelar à esquerda, Salvar à direita, Enter salva. |
| Tamanho da janela | O tamanho e o estado maximizado são lembrados entre as sessões. |
| Avisos rápidos | Confirmações curtas na parte de baixo da janela ("Lançamento salvo", "Backup salvo"…). |

### Atalhos de teclado (Ctrl+? mostra todos)

| Atalho | Ação |
|---|---|
| Ctrl+N / Ctrl+Shift+N | Nova despesa / nova receita |
| Ctrl+M | Nova meta |
| Ctrl+F | Buscar lançamentos |
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

- Data completa do dia ("03 de Outubro de 2026"). Ela muda sozinha à meia-noite e quando a janela volta a ficar ativa.
- Saldo atual (soma das contas) e saldo previsto para o fim do mês. O previsto inclui pendências e as faturas que vencem até lá.
- Receitas e despesas realizadas no mês, uma barra de uso das receitas e o selo "% economizado".
- Botões rápidos: Receita, Despesa, Meta.
- **Vencimentos dos próximos 30 dias** (só na versão Linux; no Android esse papel é do widget): contas a pagar, valores a receber e faturas. Itens em atraso aparecem em vermelho, e um clique abre o lançamento ou o pagamento da fatura.
- Cartão do assistente com o resumo do mês e as 2 dicas mais importantes.
- Contas, com o saldo de cada uma, e cartões, com a fatura atual, o vencimento, o disponível e o botão "Pagar fatura".
- Limites do mês, com barra que muda de cor: a partir de 80% ("Atenção") e acima do limite (vermelha).
- Metas: quanto foi guardado, quanto falta guardar por mês até o prazo, o mês previsto de conclusão pelo plano e o aviso "após o prazo".

## Lançamentos

- Período com campos De/Até (DD/MM/AAAA, ou pelo calendário) e os atalhos Este mês, 30 dias e Tudo.
- Busca por descrição ou categoria sem diferenciar acento ("cafe" encontra "Café"). Filtros por tipo (receitas/despesas) e situação (realizados/pendentes).
- Totais do período: receitas, despesas, saldo e a comparação receitas × despesas, com os pendentes à parte.
- Lista com ícone da categoria, descrição, categoria · conta ou cartão, data, situação ("Em atraso" em vermelho), valor, e o botão de pago/recebido (compras no cartão mostram o ícone do cartão).
- Clique (ou Enter) numa linha abre o editor. A lista mostra 300 itens por vez, com "Mostrar mais".

## Editor de lançamento

- Despesa ou receita, descrição, valor ("59,90", "1.500,00", "R$ 2.000"…), categoria (com busca), forma de pagamento (conta ou cartão), conta/cartão, data e "já paga/recebida".
- **Parcelas** (até 60): o valor informado pode ser o total, dividido em centavos com a diferença na 1ª parcela, ou o valor de cada parcela. As parcelas ficam ligadas, e ao excluir uma o app pergunta se exclui as seguintes.
- **Repetir mensalmente**: cria uma recorrência a partir da data.
- Sugestão de categoria do assistente logo abaixo da descrição, com "Usar" e "Por quê?".
- Pagamento de fatura editado mostra o aviso "não conta como despesa nova".

## Cartões e faturas

- Dia de fechamento e de vencimento: compras após o fechamento vão para a fatura seguinte.
- O limite usado inclui as parcelas futuras. Os pagamentos abatem primeiro a fatura mais antiga.
- "Pagar fatura" registra o pagamento debitando a conta escolhida, sem contar como despesa nova.

## Recorrências

- Geradas na abertura e na virada do dia. Meses em que o app ficou fechado são recuperados, até 24 de uma vez, e nunca antes da data de início.
- Dia 31 vira o último dia em meses mais curtos. Recorrências podem ser pausadas.

## Relatórios

- Período: o mesmo da aba Lançamentos.
- Despesas por categoria em **gráfico de rosca** (7 maiores + "Outras") e em barras, com o aviso de limite mensal ("Dentro do" / "⚠ Acima do").
- Evolução dos últimos 6 meses (receitas × despesas), com descrição completa para leitores de tela.
- Este mês × mês anterior.
- Botão "Exportar relatório em PDF".

## Relatório em PDF

- Atalhos (este mês, mês passado, este ano, 12 meses, tudo) ou datas livres, com prévia dos totais.
- A4: resumo com variação contra o período anterior de mesmo tamanho, a receber, a pagar, média diária e nº de lançamentos.
- Rosca e tabela por categoria (%, nº, média mensal, limite e "acima"), gráfico e tabela mensal, receitas por categoria, 10 maiores despesas.
- Contas e metas, lista completa de lançamentos (opcional), "Como ler este relatório" e "Página n de N".
- Gerado com Cairo, no computador. O diálogo avisa que o PDF não é criptografado.

## Assistente (detalhes em ASSISTENTE.md)

- Sugestão de categoria em três etapas: mesma descrição, aprendizado Naive Bayes com os seus lançamentos e dicionário aberto.
- Resumo do mês, comparado com os mesmos dias do mês anterior.
- 7 dicas: duplicado, aumento de preço, ritmo do limite, ritmo do mês, acima da média, pequenos gastos e gastos fixos. Cada dica pode ser dispensada e restaurada.
- Perguntas rápidas em português, com a linha "Como entendi".
- Tudo sem internet, com "Por quê?". Cada função pode ser desligada.

## Ajustes (todos os cartões abrem e fecham com + / −)

| Cartão | O que tem |
|---|---|
| Aparência | Temas Sistema, Claro, Material You, OLED Cinza, Tokyo Night e Nord. "Sistema" acompanha o modo claro/escuro da área de trabalho. |
| Privacidade e segurança | PIN de 4 a 8 números, ocultar valores e bloqueio automático (1, 5, 15 ou 30 min sem usar). Informa onde estão os dados e onde está a chave. |
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
| PIN | Hash Argon2id (crypto_pwhash_str), verificado em segundo plano. A partir do 5º erro seguido há espera crescente (30 s, 60 s…). O PIN nunca vai para o backup. |
| Bloqueio | Ao abrir, com Ctrl+L e pelo bloqueio automático. Bloquear fecha diálogos e seletores de arquivo abertos. |
| Ocultar valores | "R$ ••••" na tela, nos gráficos e nos avisos. |
| Backup e CSV | Gravados com permissão só do usuário. O CSV tem proteção contra fórmulas (=, +, -, @). |
| Leitura de backups | Validação completa: tamanho máximo 30 MB, UTF-8, profundidade de JSON, tipos, datas, ids e referências. Itens inválidos são descartados e contados. |

## Diferenças em relação ao Android (e por quê)

| Android | Linux |
|---|---|
| Widget na tela inicial | Não existe widget no GNOME. O mesmo conteúdo fica no rodapé da barra lateral e no cartão "Vencimentos" do Início. |
| Desbloqueio por digital | Não incluído. O PIN funciona em qualquer computador. |
| Bloquear capturas de tela | Impossível por aplicativo no Linux/Wayland. Ajustes recomenda "Ocultar valores" ao compartilhar a tela. |
| Notificações com WorkManager às 9h | Com o app aberto: uma vez por dia a partir das 9h. Fechado: opcional, ao entrar na sessão (`finan-plus --avisos`). |
| Material You com cores do papel de parede | No Linux não há essa API. O tema usa o azul do Material Design. |
| Chave no Android Keystore | Chave no chaveiro do sistema (libsecret). |

## Compatibilidade

- Backup JSON idêntico ao do Finan+ web e do app Android (versão 5, valores em reais). Dá para levar os dados de um para o outro nos dois sentidos.
- Requer GTK 4.12+ e libadwaita 1.5+: Ubuntu 24.04 ou mais novo, Debian 13, Fedora 40+, Linux Mint 22.
