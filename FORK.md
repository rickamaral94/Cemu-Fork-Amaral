# Cemu Fork Amaral

Este repositório mantém um fork Android ARM64 do Cemu. A meta de compatibilidade
de 100% do catálogo é uma direção de trabalho, não uma alegação de estado atual.

## Princípios

1. Compatibility first.
2. Correctness before performance.
3. Performance measured.
4. Features optional.
5. Upstream-friendly.
6. No fake optimizations.

Recursos experimentais permanecem desligados por padrão. Nenhum ganho é
declarado sem baseline, método reproduzível e resultado medido. FPS emulado e
FPS apresentado nunca devem ser misturados.

## Proveniência do bootstrap

- Base Android: `cemu-project/Cemu#1909`, commit
  `7c2324eda01a3974ff89aa1e4ee7a594d278662a`.
- Autor e referência do port: `SSimco/Cemu`, branch `android`.
- Ponto comum com o upstream no bootstrap:
  `c2e807f8a3ff90eaa4b6867e038c277226466c4e`.
- Upstream auditado em 2026-09-08:
  `cee557d46d63e3407dc3bbda6480c0c674e8bb80`.
- Licença principal: Mozilla Public License 2.0; dependências preservam suas
  próprias licenças.

O histórico original é preservado. Alterações Android específicas devem ser
isoladas para que correções gerais possam voltar ao Cemu quando apropriado.

## Estado declarado

Consulte `docs/android/BASELINE.md` e `docs/android/FEATURE_MATRIX.md`. Nenhum
título deve ser marcado como jogável, completo ou perfeito sem evidência
reproduzível na matriz de compatibilidade.

