# Baseline Android — Fase 0

Data da auditoria: 2026-09-08.

## Decisão de base

O repositório `rickamaral94/Cemu-Fork-Amaral` estava vazio. O bootstrap usa o
head do PR Android oficial em análise, e não uma cópia manual de arquivos:

| Referência | Commit | Estado na auditoria |
|---|---|---|
| Base do fork / `cemu-project/Cemu#1909` | `7c2324eda01a3974ff89aa1e4ee7a594d278662a` | PR aberto; CI original aprovado; merge marcado `dirty` |
| Ponto comum Android/upstream | `c2e807f8a3ff90eaa4b6867e038c277226466c4e` | Base de comparação |
| `cemu-project/Cemu` `main` | `cee557d46d63e3407dc3bbda6480c0c674e8bb80` | 28 commits após o ponto comum |
| Android sobre o ponto comum | 21 commits / 353 arquivos alterados | Base funcional escolhida |
| `SSimco/Cemu:android-port` | `8759323e5b72594665b55e02f99db3c1e1e7de8a` | Branch histórica, 200 commits sobre base mais antiga |

A branch `android-port` possui tags e histórico de desenvolvimento, mas parte de
um upstream anterior e divergiu do PR limpo. Ela permanece referência histórica;
não é a base de produção.

## Evidência de build

Os checks publicados para `7c2324e` concluíram com sucesso em Android, Linux
x64/ARM, Windows e macOS x64/ARM. A reprodução local foi bloqueada antes da
configuração do projeto porque o ambiente de auditoria não possui Android SDK/NDK
e não consegue alcançar `services.gradle.org` para obter Gradle 9.4.1.

Comando tentado:

```bash
GRADLE_USER_HOME=<diretorio-gravavel> ./src/android/gradlew --version
```

Resultado: `java.net.SocketException: Network is unreachable`. Isso é uma
limitação do ambiente, não uma aprovação nova do código. A build do fork deve ser
revalidada pelo GitHub Actions depois de cada mudança.

## Configuração Android encontrada

| Item | Baseline |
|---|---|
| ABI | somente `arm64-v8a` |
| Linguagens | C++20, Kotlin, JNI |
| UI | Jetpack Compose com `SurfaceView` para render e overlay |
| Renderer | Vulkan obrigatório; OpenGL desabilitado no Android |
| `compileSdk` | 36 |
| `targetSdk` | 35 |
| `minSdk` | 30 |
| NDK | `29.0.14206865` |
| Gradle | 9.4.1 |
| AGP | 9.2.1 |
| JIT ARM64 | backend AArch64 existente com `xbyak_aarch64` |
| Drivers customizados | `libadrenotools`, global e por jogo |
| Armazenamento | SAF e provider próprio |
| Áudio | cubeb; não há backend AAudio dedicado comprovado |
| Graphic packs | catálogo/ativação/download existentes |
| Atualizador do app | ausente |
| Frame pacing Android | Swappy/AChoreographer ausentes |
| Upscaling moderno | FSR/SGSR ausentes |
| Frame generation | ausente |

O `minSdk 30` é coerente com uma primeira base focada em dispositivos ARM64
modernos, mas ainda precisa de justificativa por APIs realmente usadas antes de
ser congelado como política do fork.

## Divergência e sincronização

Há 9 caminhos modificados pelos dois lados desde o ponto comum:

- `BUILD.md`;
- `src/CMakeLists.txt`;
- `src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.cpp`;
- `src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h`;
- `src/Cafe/OS/libs/swkbd/swkbd.cpp`;
- `src/gui/wxgui/GeneralSettings2.cpp`;
- `src/gui/wxgui/MainWindow.cpp`;
- `src/main.cpp`;
- `vcpkg.json`.

As mudanças upstream de maior prioridade incluem correção de tamanho de código
AArch64, falha imediata em erros de fence Vulkan, cache do estado de erro de
shader e uso opcional de `vertexPipelineStoresAndAtomics`. Elas devem entrar por
uma sincronização dedicada, nunca misturadas com SGSR ou otimizações.

## Dívidas e riscos confirmados

1. O PR está atrás do upstream e não é mesclável automaticamente.
2. A extração ZIP de drivers e graphic packs não validava traversal, número de
   entradas ou tamanho descompactado.
3. O download de graphic packs escolhe o primeiro asset, mantém o ZIP inteiro em
   memória e não verifica hash/assinatura.
4. O `versionCode` Android está fixo em `1`, inviabilizando atualização ordenada.
5. Identidade (`applicationId`, nome e assinatura) ainda é a do Cemu; a decisão
   definitiva do fork precisa ocorrer antes de qualquer release pública.
6. A CI gera APK, mas não executa explicitamente `test` antes do assemble.
7. Não há teste diferencial automatizado interpretador versus JIT AArch64.
8. Não há base versionada de compatibilidade nem pacote de diagnóstico sanitizado.
9. Telemetria de frame time, temperatura, clocks e throttling ainda é incompleta.
10. Não há frame pacing específico do Android, upscaler ou frame generation.

## Componentes que permanecem sincronizados com upstream

- Espresso/PPC, IML, interpretador e recompiladores;
- Latte, GX2, shader translator e renderers comuns;
- IOSU/HLE, formatos de títulos, saves e filesystem comum;
- caches de shader/pipeline e graphic pack engine;
- input, áudio, H264 e bibliotecas compartilhadas;
- dependências, correções de segurança, perfis de jogos e recursos legais.

Camadas específicas do fork — UI Android, SAF/JNI, carregamento AdrenoTools,
frame pacing móvel, diagnósticos e atualização — devem ficar isoladas atrás de
interfaces para reduzir conflitos.

