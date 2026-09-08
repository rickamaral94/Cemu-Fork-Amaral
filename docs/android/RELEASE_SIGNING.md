# Assinatura de releases Android

## Política

O identificador definitivo do aplicativo é `io.github.rickamaral94.cemu`. A
chave de release é dedicada ao fork, fica fora do repositório e deve possuir ao
menos duas cópias de recuperação criptografadas sob custódia do mantenedor.

O GitHub Actions comum produz somente o aplicativo Nightly
`io.github.rickamaral94.cemu.nightly`. Ele usa uma chave pública de teste estável
para permitir atualização entre builds Nightly sem remover o app. Essa chave
está deliberadamente no repositório, não representa confiança e nunca pode
assinar o aplicativo Stable.

Uma release pública deverá usar o identificador `io.github.rickamaral94.cemu`,
um ambiente GitHub protegido, aprovação manual e a chave offline. O job deve
definir `ANDROID_REQUIRE_RELEASE_SIGNING=true` e falhar se a configuração estiver
ausente ou incompleta.

## Criação e custódia

Crie a chave em uma máquina confiável. O comando abaixo solicita as senhas de
forma interativa; não coloque senhas na linha de comando ou no histórico do
shell.

```bash
keytool -genkeypair -v \
  -keystore cemu-fork-amaral-release.jks \
  -alias cemu-fork-amaral \
  -keyalg RSA \
  -keysize 4096 \
  -validity 10000
```

Registre separadamente o SHA-256 do certificado:

```bash
keytool -list -v -keystore cemu-fork-amaral-release.jks
```

Nunca faça commit do `.jks` de **release**, das senhas ou de sua representação
Base64. A perda da chave impede atualizações confiáveis das instalações Stable;
sua troca exige um plano explícito de migração. A única exceção é a chave
explicitamente pública de Nightly em `.github/ci/`, que não pode ser reutilizada
em releases.

## Variáveis de build

| Variável | Finalidade |
|---|---|
| `ANDROID_STORE_FILE` | Caminho local para o keystore materializado pelo job protegido |
| `ANDROID_KEY_STORE_PASSWORD` | Senha do keystore |
| `ANDROID_KEY_ALIAS` | Alias da chave dedicada |
| `ANDROID_KEY_PASSWORD` | Senha da chave |
| `ANDROID_REQUIRE_RELEASE_SIGNING` | Deve ser `true` em uma release pública |
| `EMULATOR_VERSION_CODE` | Inteiro positivo e estritamente maior que o da release anterior |

Sem `EMULATOR_VERSION_CODE`, builds de desenvolvimento usam a contagem de
commits do Git como fallback monotônico. Releases sempre devem fornecer o valor
explicitamente e registrar o certificado e os hashes dos artefatos.

O CI também compara o SHA-256 do certificado Nightly com o valor registrado no
workflow. Uma troca dessa chave deve ser tratada como migração incompatível e
documentada antes de publicar outro APK.

## Migração da identidade antiga

Builds experimentais com `info.cemu.cemu` são outro aplicativo para o Android e
não recebem atualização no lugar. Até existir um fluxo de migração testado, o
usuário deve exportar configurações e saves legalmente acessíveis antes de
remover uma instalação antiga. Não desinstale o aplicativo antigo antes de
verificar o backup.
