# Assinatura de releases Android

## Política

O identificador definitivo do aplicativo é `io.github.rickamaral94.cemu`. A
chave de release é dedicada ao fork, fica fora do repositório e deve possuir ao
menos duas cópias de recuperação criptografadas sob custódia do mantenedor.

O GitHub Actions comum produz somente APK de teste assinado com a chave debug.
Uma release pública deverá usar um ambiente GitHub protegido, exigir aprovação
manual e definir `ANDROID_REQUIRE_RELEASE_SIGNING=true`. O job deve falhar se a
configuração estiver ausente ou incompleta.

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

Nunca faça commit do `.jks`, das senhas ou de sua representação Base64. A perda
da chave impede atualizações confiáveis das instalações existentes; sua troca
exige um plano explícito de migração.

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

## Migração da identidade antiga

Builds experimentais com `info.cemu.cemu` são outro aplicativo para o Android e
não recebem atualização no lugar. Até existir um fluxo de migração testado, o
usuário deve exportar configurações e saves legalmente acessíveis antes de
remover uma instalação antiga. Não desinstale o aplicativo antigo antes de
verificar o backup.
