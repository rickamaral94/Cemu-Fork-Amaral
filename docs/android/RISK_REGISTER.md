# Registro inicial de riscos

| ID | Risco | Prob. | Impacto | Gate/Mitigação |
|---|---|---:|---:|---|
| R-01 | regressão ao sincronizar 28 commits upstream | Alta | Alto | merge isolado, CI completa e matriz de jogos |
| R-02 | semântica incorreta no JIT ARM64 | Média | Crítico | diferencial interpretador/JIT; fallback por jogo |
| R-03 | ZIP malicioso em driver/graphic pack | Alta | Crítico | canonicalização, limites, testes e instalação transacional |
| R-04 | driver customizado causa `DEVICE_LOST`/boot loop | Alta | Alto | último driver válido, modo seguro e fallback de sistema |
| R-05 | regras Adreno quebram Mali/Immortalis | Média | Alto | detecção por capabilities; defaults neutros |
| R-06 | cache incompatível entre driver/build | Alta | Alto | chave por GPU, driver, app e formato; evicção segura |
| R-07 | updater aponta para upstream/asset errado | Média | Crítico | origem fixa do fork, manifesto assinado e validação APK |
| R-08 | SGSR 2/FG sem vetores confiáveis | Alta | Alto | contrato temporal e status experimental por jogo |
| R-09 | FG mascara baixa velocidade de emulação | Alta | Alto | FPS real/apresentado separados; base mínima estável |
| R-10 | alteração de identidade quebra updates/saves | Média | Crítico | decidir `applicationId`/chave antes do primeiro release |
| R-11 | aquecimento invalida benchmark curto | Alta | Médio | sessões longas, AB/BA e gate térmico |
| R-12 | asset/proveniência incompatível | Média | Crítico | SBOM, revisão de licença e autoria por integração |

