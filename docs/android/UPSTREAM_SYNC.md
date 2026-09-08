# Política de sincronização upstream

Remotos locais esperados:

- `origin`: `rickamaral94/Cemu-Fork-Amaral`;
- `upstream`: `cemu-project/Cemu`;
- `android-reference`: `SSimco/Cemu`.

## Fluxo

1. Atualizar referências e registrar os SHAs.
2. Criar branch `sync/upstream-AAAA-MM-DD` a partir do `main` do fork.
3. Gerar lista de commits, diffstat e arquivos sobrepostos desde o merge-base.
4. Mesclar o upstream sem alterações funcionais próprias no mesmo commit.
5. Resolver conflitos preservando a interface Android e a correção upstream.
6. Executar unit tests, análise de arquitetura e builds suportadas.
7. Fazer smoke test Android e subconjunto canário antes da matriz completa.
8. Registrar conflitos, decisões e regressões.

Não fazer rebase destrutivo do histórico público do fork. Correções gerais devem
ser pequenas e adequadas a PR upstream; integrações móveis permanecem isoladas.

## Baseline de conflitos em 2026-09-08

A simulação encontrou nove arquivos alterados pelos dois lados. Os conflitos de
maior risco estão no Vulkan renderer e em `vcpkg.json`; arquivos wxWidgets não
devem ditar decisões da UI Android, mas precisam continuar compilando no desktop.

