# Material Symbols (ícones do app)

- **O que é:** 76 ícones do conjunto *Material Symbols Rounded* do Google, peso 300, em `data/icons/symbolic/fin-*-symbolic.svg`. Eles vão embutidos no programa (GResource) e o GTK os pinta com a cor do tema.
  - 36 são os mesmos do app Android: os SVGs foram refeitos a partir dos vetores Android, sem mudar os traços.
  - 40 são novos, para a interface de computador: busca, olho, cadeado, menu, calendário, PDF, CSV, download/upload, setas, lixeira, apagar etc. Foram baixados do repositório oficial.
- **Origem:** https://github.com/google/material-design-icons (pasta `symbols/web/<nome>/materialsymbolsrounded/<nome>_wght300_24px.svg`).
- **Licença:** Apache License 2.0. O texto está em `LICENSE`, nesta pasta, e dentro do app em *Ajustes › Sobre*. A Apache 2.0 é compatível com a GPL-3.0 do Finan+.
- **O que foi alterado:** só o nome dos arquivos (prefixo `fin-`, sufixo `-symbolic`) e a tag `<svg>`, que foi normalizada (viewBox e tamanho de 24 px).
