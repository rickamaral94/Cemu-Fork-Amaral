# Matriz de testes e compatibilidade

## Estados permitidos

`Não testado`, `Não inicializa`, `Inicializa`, `Menu`, `In-game`, `Jogável`,
`Completo`, `Perfeito`.

`Perfeito` exige conclusão, correção audiovisual e timing equivalentes ao
hardware dentro do escopo testado; ausência de relato não é evidência.

## Registro mínimo por execução

| Campo | Regra |
|---|---|
| Build | commit completo e tipo de build |
| Conteúdo | Title ID, região, versão/update/DLC e hashes não redistributivos |
| Dispositivo | SoC, GPU, RAM, Android e refresh |
| Driver | fabricante, nome, versão, Vulkan e extensões relevantes |
| Configuração | perfil exportado e graphic packs |
| Sessão | duração, cenário, cold/warm cache |
| Correção | boot, visual, áudio, controle e save/load |
| Performance | FPS real, frame time, 1%/0,1% low e stutter |
| Sistema | RAM, memória gráfica estimada, temperatura, clocks e energia |
| Resultado | estado, crash rate, logs e regressão versus baseline |

## Classes iniciais

- homebrew público e testes sintéticos próprios para smoke/CI;
- 2D e 3D;
- 30 FPS e 60 FPS;
- CPU-bound e GPU-bound;
- open world e sessões longas;
- graphic packs, updates e DLCs;
- dual-screen, gyro, teclado virtual e periféricos quando aplicável.

ROMs, keys, firmware e conteúdo Nintendo não entram no repositório ou no CI.

## Smoke test em aparelho ARM64

Com `adb` e `apkanalyzer` do Android SDK disponíveis, o script
`tools/android/device-smoke-test.sh` valida, sem apagar dados do aplicativo:

1. dispositivo autorizado, API 30+, ABI `arm64-v8a` e recurso Vulkan;
2. ABI, biblioteca nativa, `applicationId` e `versionCode` do APK antes da
   instalação;
3. instalação com atualização preservando dados (`adb install -r`);
4. inicialização da `MainActivity` e permanência do processo pelo intervalo
   configurado;
5. ausência de exceção fatal, sinal fatal ou ANR no log do processo.

Exemplo:

```bash
SMOKE_DURATION_SECONDS=30 \
  tools/android/device-smoke-test.sh app-release.apk artifacts/smoke-odin2
```

O diretório de evidência contém hash do APK, commit, informações técnicas sem o
serial do aparelho, saída de instalação/inicialização e `logcat` restrito ao
processo do fork. O script encerra o processo ao final, mas não desinstala o app
nem limpa seus dados.

Esse smoke cobre instalação e inicialização da interface. A etapa 1C somente
será encerrada depois de também executar homebrew público ou teste legal em
aparelho físico e registrar o resultado.

## Método de performance

Usar AB/BA, aquecimento controlado, no mínimo cinco repetições quando o teste for
curto e uma sessão prolongada para vazamentos/throttling. Reportar mediana e
dispersão; não aprovar mudança apenas pelo FPS médio.
