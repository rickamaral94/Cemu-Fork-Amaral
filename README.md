# Cemu Fork Amaral — Wii U no Android ARM64

[![Android CI](https://github.com/rickamaral94/Cemu-Fork-Amaral/actions/workflows/android.yml/badge.svg)](https://github.com/rickamaral94/Cemu-Fork-Amaral/actions/workflows/android.yml)
[![Licença MPL-2.0](https://img.shields.io/badge/license-MPL--2.0-blue.svg)](LICENSE.txt)

O Cemu Fork Amaral é um fork do [Cemu](https://github.com/cemu-project/Cemu)
voltado ao Android ARM64. O projeto reutiliza o núcleo consolidado do Cemu e o
port Android em análise no
[PR cemu-project/Cemu#1909](https://github.com/cemu-project/Cemu/pull/1909),
mantendo as adaptações móveis isoladas sempre que possível.

> **Estado do projeto:** desenvolvimento inicial. Um APK compilar e abrir não
> significa que todo jogo seja jogável. A meta de cobrir todo o catálogo do Wii
> U é uma direção de longo prazo, não uma alegação de compatibilidade atual.

## Objetivos

- compatibilidade e correção antes de desempenho;
- Vulkan como renderizador principal no Android;
- recompilação PowerPC para AArch64 com fallback seguro;
- suporte sustentável a Adreno, Turnip e GPUs Mali/Immortalis;
- frame pacing consistente, baixa latência e consumo controlado;
- interface adequada a celular, tablet e portáteis Android;
- sincronização rastreável com o upstream do Cemu;
- otimizações somente quando acompanhadas por medições reproduzíveis.

Recursos como SGSR 2 e geração de quadros permanecerão experimentais e
desligados por padrão até existirem os dados temporais e testes necessários.

## Estado atual

| Componente | Estado |
|---|---|
| ABI | `arm64-v8a` exclusivamente |
| Android | `minSdk 30`, `targetSdk 35`, `compileSdk 36` |
| Renderer | Vulkan |
| JIT | backend AArch64 existente no Cemu |
| Interface | Jetpack Compose com controles físicos e touchscreen |
| Armazenamento | Android Storage Access Framework |
| Drivers customizados | AdrenoTools, global e por jogo, sem root |
| Graphic packs | base do port Android; integridade/atualização ainda em evolução |
| Frame pacing móvel | ainda não implementado |
| SGSR e frame generation | ainda não implementados |
| Atualizador do aplicativo | ainda não implementado |

A auditoria completa, as diferenças em relação ao desktop e o roadmap estão em
[`docs/android/`](docs/android/).

## Compatibilidade

Cada teste deve registrar aparelho, GPU, driver, Android, versão do jogo,
update/DLC, configuração e versão do emulador. Os únicos estados aceitos são:

- **Não testado**;
- **Não inicializa**;
- **Inicializa**;
- **Menu**;
- **In-game**;
- **Jogável**;
- **Completo**;
- **Perfeito**.

Nenhum título ou catálogo será anunciado como 100% compatível sem evidências
reproduzíveis. Consulte a
[`matriz de testes`](docs/android/TEST_MATRIX.md) antes de enviar resultados.

## APKs e releases

Ainda não há uma release pública estável. O
[GitHub Actions](https://github.com/rickamaral94/Cemu-Fork-Amaral/actions/workflows/android.yml)
produz APKs temporários para testes de engenharia. Esses artefatos usam a chave
debug do Android, expiram e **não** constituem releases oficiais.

Quando os gates de release forem atendidos, os canais Stable, Beta e Nightly
serão publicados exclusivamente nas
[releases deste fork](https://github.com/rickamaral94/Cemu-Fork-Amaral/releases).

## Requisitos de execução

- aparelho Android ARM64;
- Android 11/API 30 ou superior;
- suporte Vulkan funcional no driver do sistema;
- conteúdo do Wii U obtido legalmente pelo próprio usuário.

Drivers customizados utilizam o formato AdrenoTools e não alteram o driver
global do Android. Eles são opcionais, destinados a GPUs Adreno compatíveis e
podem ser piores que o driver do sistema para determinados jogos. Dispositivos
Mali/Immortalis continuam usando o driver Vulkan fornecido pelo sistema.

## Conteúdo e uso legal

Este repositório e seus artefatos não incluem jogos, keys, firmware, arquivos
proprietários da Nintendo nem outros conteúdos protegidos. O usuário é
responsável por obter e utilizar seus próprios dumps de acordo com a legislação
aplicável.

Não envie conteúdo protegido, dados pessoais ou chaves em issues e relatórios.

## Compilação Android

### Dependências

- Git com suporte a submódulos;
- Java 21;
- Android SDK com `compileSdk 36`;
- Android NDK `29.0.14206865`;
- CMake 3.25 ou superior;
- `gettext` e `mono` no ambiente Linux de CI.

### Build local

```bash
git clone --recursive https://github.com/rickamaral94/Cemu-Fork-Amaral.git
cd Cemu-Fork-Amaral/src/android
./gradlew --no-daemon test assembleRelease
cd ../..
tools/android/verify-apk.sh src/android/app/build/outputs/apk/release/app-release.apk
```

Sem uma configuração completa de assinatura, o build `release` local utiliza a
chave debug apenas para permitir instalação e testes. Uma distribuição pública
deve usar a chave dedicada do fork e seguir
[`docs/android/RELEASE_SIGNING.md`](docs/android/RELEASE_SIGNING.md).

O `applicationId` definitivo é `io.github.rickamaral94.cemu`. Builds antigos do
port com `info.cemu.cemu` são tratados pelo Android como outro aplicativo e não
são atualizados no lugar.

## Documentação técnica

- [Baseline e commit-base](docs/android/BASELINE.md)
- [Arquitetura](docs/android/ARCHITECTURE.md)
- [Matriz desktop versus Android](docs/android/FEATURE_MATRIX.md)
- [Registro de riscos](docs/android/RISK_REGISTER.md)
- [Roadmap executável](docs/android/ROADMAP.md)
- [Matriz de testes](docs/android/TEST_MATRIX.md)
- [Política de assinatura](docs/android/RELEASE_SIGNING.md)
- [Sincronização com upstream](docs/android/UPSTREAM_SYNC.md)

## Contribuição

Leia [`AGENTS.md`](AGENTS.md), [`FORK.md`](FORK.md) e
[`CODING_STYLE.md`](CODING_STYLE.md) antes de alterar o projeto.

Contribuições devem:

- preservar autoria, cabeçalhos e licenças;
- manter commits pequenos e separados por área;
- incluir testes para mudanças de correção ou segurança;
- apresentar métricas antes/depois para otimizações;
- manter recursos experimentais opcionais e com fallback;
- evitar código, documentação ou conhecimento derivado de SDKs proprietários
  vazados.

Correções gerais devem ser estruturadas para possível envio ao upstream. As
regras próprias do projeto Cemu continuam valendo para contribuições enviadas
diretamente a ele.

## Projetos e comunidades relacionados

- [Cemu upstream](https://github.com/cemu-project/Cemu)
- [Site oficial do Cemu](https://cemu.info)
- [Wiki de compatibilidade do Cemu](https://wiki.cemu.info/wiki/Main_Page)
- [Graphic Packs oficiais](https://github.com/cemu-project/cemu_graphic_packs)
- [Cemu-Language](https://github.com/cemu-project/Cemu-Language)
- [Discord do Cemu](https://discord.gg/5psYsup)
- [Matrix do Cemu](https://matrix.to/#/#cemu:cemu.info)

## Licença e créditos

O Cemu é desenvolvido originalmente por Exzap, Petergov e colaboradores. O
port Android usado como base foi desenvolvido por SSimco e demais autores
preservados no histórico Git.

O código principal é licenciado sob a
[Mozilla Public License 2.0](LICENSE.txt). Dependências e arquivos com cabeçalhos
específicos permanecem sujeitos às suas próprias licenças. O uso do nome deste
fork não altera a autoria nem implica endosso oficial do projeto Cemu.
