# Calendário de lançamentos

Disponível desde a versão 1.2.0 do Finan+ para Linux, na seção **Lançamentos › Calendário**. As regras são as mesmas do app Android 1.3.0 e do Finan+ web 1.2.0.

O calendário mostra o mês em grade, com o que entra e o que sai em cada dia. Ele serve para ver os dias apertados do mês (por exemplo, o aluguel vencendo antes do salário cair) sem ler a lista inteira.

## Como usar

1. Abra **Lançamentos** e clique em **Calendário**, no alto à direita. Para voltar à lista, clique em **Lista**.
2. Troque de mês pelas setas **‹ ›** ou pelas teclas **Page Up** e **Page Down**. Fora do mês atual aparece **Voltar para hoje**.
3. Clique num dia para ver os lançamentos dele. Em janelas largas o calendário fica à esquerda e o dia escolhido à direita; em janelas estreitas, o dia aparece embaixo do calendário.
4. Para lançar algo num dia, há quatro caminhos, todos com a data já preenchida:
   - clique **de novo** no dia que já está escolhido (dois cliques num dia fazem o mesmo);
   - clique com o **botão direito** em qualquer dia;
   - **segure** o botão do mouse (ou o dedo, em tela de toque) sobre qualquer dia;
   - use os botões **Receita** ou **Despesa** do dia escolhido.

   Os três primeiros abrem o formulário como despesa; dá para trocar para receita no próprio formulário. Se a data for futura, o lançamento começa como pendente.
5. Na lista do dia:
   - clique num lançamento para editá-lo;
   - clique no círculo para marcar como pago ou recebido, como na lista normal;
   - clique numa fatura para abrir **Pagar fatura**.

A escolha entre Lista e Calendário, o mês e o dia escolhido continuam enquanto o Finan+ está aberto, inclusive depois do bloqueio por PIN. Com o app aberto na virada do dia, o contorno de "hoje" passa para a nova data.

## O que cada dia mostra

| Elemento | Significado |
|---|---|
| Número do dia | Mais claro nos dias que já passaram. Hoje tem contorno azul. O dia escolhido fica preenchido. |
| Valor pequeno (+5,2 mil, −120) | Saldo do dia: o que entra menos o que sai das contas nesse dia. Verde quando o saldo é positivo ou zero, vermelho quando é negativo. Fica sem "R$" e é arredondado para caber (veja abaixo). |
| Pontinho verde | Há receita no dia. |
| Pontinho vermelho | Há despesa na conta no dia, paga ou pendente. |
| Pontinho roxo | Há algo de cartão: compra no cartão, pagamento de fatura ou fatura vencendo. |
| Ícone de alerta | Há conta pendente com data passada ou fatura vencida ainda em aberto. |

Passar o mouse sobre um dia mostra a frase completa, com o valor exato ("6 de outubro, terça-feira, 1 lançamento, saldo do dia menos R$ 119,90, em atraso").

### Arredondamento do valor do dia

O quadradinho do dia é estreito, então o valor aparece abreviado e arredondado ao mais próximo:

| Valor | Mostra |
|---|---|
| R$ 182,40 | 182 |
| R$ 119,90 | 120 |
| R$ 1.500,00 | 1,5 mil |
| R$ 15.499,00 | 15 mil |
| R$ 1.200.000,00 | 1,2 mi |

O valor exato aparece no resumo do dia escolhido e na dica do mouse, e é o que o leitor de tela fala.

## Que lançamentos entram na conta

O calendário mostra **dinheiro entrando e saindo das contas**, o que inclui o que ainda está pendente:

- **Receitas e despesas fora do cartão**, realizadas ou pendentes, na data do lançamento.
- **Pagamentos de fatura**, na data em que foram feitos (o dinheiro sai da conta nesse dia).
- **Faturas em aberto**, no dia do vencimento, com o valor que ainda falta pagar.
- **Compras no cartão** aparecem na lista do dia em que foram feitas, com o pontinho roxo, mas **não entram no saldo do dia**. Esse dinheiro só sai da conta quando a fatura é paga, e já é contado na fatura. Assim nada é contado duas vezes.

Abaixo do calendário ficam os totais do mês: **Entradas**, **Saídas** e **Resultado**. Eles são exatamente a soma dos dias. Por incluir pendências e faturas, podem ser diferentes dos totais da Lista, que somam só o que já foi realizado no período.

No dia escolhido, de hoje em diante, aparece também o **Saldo previsto ao fim do dia**. É a mesma conta do "Saldo previsto" do Início: o saldo atual das contas mais tudo o que está pendente até aquele dia, menos as faturas em aberto que vencem até lá.

O calendário mostra todos os lançamentos do mês. A busca e os filtros de tipo e situação valem só para a Lista.

A Lista usa a mesma regra para o **saldo do dia** que aparece ao lado de cada data ("Quinta, 15 de outubro … − R$ 186,00").

## Privacidade e acessibilidade

- **Ocultar valores (Ctrl+H):** os valores somem de dentro dos dias e ficam só os pontinhos; os totais e os lançamentos aparecem como "R$ ••••", e o leitor de tela não fala valores.
- **Leitor de tela:** cada dia é um botão lido como frase completa; o dia escolhido é anunciado como pressionado, com a dica "Clique de novo para lançar nesta data". O cabeçalho da semana e a legenda são decorativos.
- **Teclado:** Tab e as setas chegam aos dias, Enter escolhe o dia (Enter de novo abre o lançamento novo), Page Up e Page Down trocam de mês.
- O valor do dia nunca é cortado com "…": ele já é abreviado para caber.

## Como foi feito

| Arquivo | O quê |
|---|---|
| `src/core/period.c`, `period.h` | Regras (tradução de `MonthCalendar.kt` e `Period.kt` do Android): grade, dias, faturas, atrasos, totais, valor abreviado, títulos e texto do leitor de tela |
| `src/ui/page_calendar.c` | Tela do calendário |
| `src/ui/page_moves.c` | Chave Lista \| Calendário e o saldo do dia na Lista |
| `src/ui/editors.c` | `editor_tx_on(tipo, data, id)`: lançamento novo com a data do dia |
| `tests/test_period.c` | Os mesmos casos de `CalendarTest.kt` e `PeriodTest.kt` |

Os dados e o formato do backup **não mudaram**: o calendário só mostra os lançamentos que já existem.
