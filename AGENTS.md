# Regras de contribuição do fork

Leia `FORK.md`, `CODING_STYLE.md` e os documentos em `docs/android/` antes de
alterar o projeto.

- Preserve autoria, cabeçalhos e licenças.
- Não use código de SDKs vazados, jogos, keys, firmware ou material proprietário.
- Não misture sincronização de upstream, refatoração e funcionalidade no mesmo
  commit.
- Não altere semântica PowerPC, sincronização Vulkan ou formatos de save sem
  testes diferenciais/reprodutíveis.
- Não use `fast-math` global nem dependa de comportamento Vulkan indefinido.
- Recursos experimentais devem ser opt-in, por jogo quando aplicável, e possuir
  fallback.
- Toda otimização deve registrar hipótese, métrica anterior/posterior, aparelho,
  driver, jogo, duração, temperatura e risco de regressão.
- APK de release deve conter somente `arm64-v8a`, possuir origem rastreável e ser
  validado pelo CI.
- Nunca afirme 100% de compatibilidade. Use os estados definidos em
  `docs/android/TEST_MATRIX.md`.

