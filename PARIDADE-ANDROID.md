# Paridade com o app Android (1.7 a 1.13)

O Finan+ para Linux acompanha o app Android. Esta página diz o que da 1.7 à 1.13 do Android já está no Linux e o que ficou de fora de propósito.

## Igual ao Android

| Android | No Linux | Onde |
|---|---|---|
| 1.7 Reserva para metas | Sim | Meu dinheiro › Metas › editar › "Guardar ou retirar agora" + **Tirar da conta** |
| 1.7 Backup automático | Sim (mesmo formato criptografado) | Ajustes › Dados › Backup automático |
| 1.8 Despesas compartilhadas | Sim | Meu dinheiro › Despesas compartilhadas, botão **⋯** |
| 1.8 Acumular saldo entre os meses | Sim | Ajustes › Lançamentos |
| 1.8 Efeito vidro | Sim | Ajustes › Aparência |
| 1.9 Reserva como "guardado" em Lançamentos | Sim | Lista e calendário |
| 1.9 Despesa compartilhada como imagem | Sim (imagem e PDF) | Detalhe da despesa |
| 1.9 Saldo acumulado no Resumo e no PDF | Sim | Resumo e relatório em PDF |
| 1.10/1.11 Glossário único (a receber, a pagar, reservado, previsto) | Sim (`src/core/totals.c`) | Início, barra lateral, Resumo |
| 1.11 Dívidas | Sim | Meu dinheiro › Dívidas |
| 1.11 Categorias × formas de pagamento | Sim (migração única) | Formulário de lançamento |
| 1.11 Sinal de menos tipográfico e plurais | Sim | Todo o app |
| 1.12 Ícones Phosphor | Sim (Regular, Fill e Duotone) | Todo o app |
| 1.13 Transferência entre contas | Sim | Botão **⋯** (Ctrl+T) e Meu dinheiro |

O núcleo de cada item é uma tradução direta do Kotlin (`Transfers.kt`, `Shared.kt`, `Totals.kt`, `Debts.kt`, `TxForm.kt`, `BackupCrypto.kt`) e passa nos mesmos casos de teste, em `tests/test_paridade.c`. Uma cópia criptografada feita com o algoritmo do Android (PBKDF2-HMAC-SHA256 + AES-256-GCM) está em `tests/fixtures/backup/` e é aberta pelo teste.

## Diferente de propósito

- **Telas:** o Linux mantém o layout de computador da 1.4.0 (três colunas, barra lateral). As telas da 1.10/1.11 do Android foram trazidas pelo conteúdo (o que cada cartão mostra e calcula), não pelo desenho de celular.
- **Backup automático:** o Android copia de madrugada com o WorkManager. No Linux a cópia do dia é feita enquanto o Finan+ está aberto (logo ao abrir e, depois, uma vez por dia). A senha fica cifrada com a chave dos dados, como as senhas de fatura.
- **Comprovante anexado da despesa compartilhada:** no Android, a foto ou o PDF fica cifrado só no celular. O Linux não anexa arquivos, mas guarda a referência que vem no backup, para não perdê-la ao salvar de novo.

## Só no Android

- Tela de boas-vindas, widget, acesso pela rede local e leitura de fatura por foto (OCR).
