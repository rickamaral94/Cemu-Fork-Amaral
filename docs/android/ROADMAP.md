# Roadmap executável

Cada etapa exige build limpa, testes reproduzíveis, documentação e ausência de
regressão conhecida. A ordem evita construir recursos temporais sobre pacing ou
renderização instáveis.

| Etapa | Entrega | Gate de saída |
|---:|---|---|
| 0 | baseline, arquitetura, matrizes, riscos e sincronização | commits/base registrados; CI baseline conhecida |
| 1A | segurança de pacotes e CI Android | testes de ZIP; APK somente arm64; unit tests no CI |
| 1B | identidade/versionamento do fork | `applicationId`, assinatura e política decididos |
| 1C | APK reproduzível e smoke test legal | instala, inicia e executa homebrew/teste público |
| 2 | sincronização upstream | conflitos resolvidos isoladamente; matriz sem regressão |
| 3 | estabilidade/ciclo de vida | pause/resume, surface e device loss testados |
| 4 | correção do JIT ARM64 | diferencial interpretador/JIT e invalidação do cache |
| 5 | Vulkan/Turnip | capability report, fallback e driver por jogo validados |
| 6 | frame pacing/latência | 30/60 em displays 60/90/120/144 Hz medidos |
| 7 | shader/pipeline cache | stutter e uso de memória medidos em sessões longas |
| 8 | graphic packs | atualização íntegra e matriz de packs por Title ID |
| 9 | SGSR 1 | presets, HUD e custo GPU validados |
| 10 | SGSR 2 experimental | contrato temporal e testes de ghosting aprovados |
| 11 | frame generation experimental | base estável, latência e contadores separados |
| 12 | compatibilidade/diagnóstico | esquema versionado e bundle sem dados pessoais |
| 13 | atualizador | canais, retomada, SHA-256, assinatura e instalador padrão |
| 14 | release | SBOM, changelog, hashes, assinatura e testes prolongados |

## Decisões que bloqueiam release, não o desenvolvimento

- `applicationId` definitivo;
- nome público e ícone;
- chave de assinatura e custódia;
- `minSdk` suportado;
- esquema dos canais stable/beta/nightly;
- formato do manifesto assinado do atualizador.

