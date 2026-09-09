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

## Validação no próprio aparelho Android

O fluxo de teste do usuário não depende de computador, ADB ou root:

1. instalar o APK e confirmar que a biblioteca abre;
2. iniciar homebrew público ou dump obtido legalmente;
3. validar vídeo, áudio, controle, save e carregamento;
4. testar por tempo suficiente para observar stutter, aquecimento e vazamentos;
5. retornar à biblioteca, abrir o menu de três pontos e selecionar
   **Compartilhar pacote de diagnóstico**;
6. anexar o ZIP ao relato e informar os defeitos observados.

Para validar ciclo de vida, durante uma sessão ativa pressione Home, aguarde 30
segundos, retorne ao jogo e repita o ciclo cinco vezes. Faça também um bloqueio e
desbloqueio de tela. Aprovado significa: áudio e jogo não avançam em segundo
plano, a imagem retorna sem tela preta, controles respondem, o áudio não duplica
e o save continua carregável.

O ZIP contém um `report.json` versionado com dados do app, aparelho, tela,
driver selecionado e configurações gráficas não sensíveis. Quando disponível,
inclui até 4 MiB do log mais relevante: sessão atual com jogo, sessão anterior
com jogo/crash ou última sessão de jogo preservada. O campo `log.source` informa
qual origem foi escolhida. O log aplica redação automática de e-mail,
URI, caminhos Android, endereços IP e MAC. Jogos, keys, firmware, saves, serial do
aparelho e Android ID não são adicionados.

O aplicativo mantém no máximo os cinco pacotes mais recentes em sua pasta de
diagnósticos. Nada é enviado automaticamente: o compartilhamento sempre depende
de confirmação explícita no seletor do Android.

Para validar a apresentação Vulkan, registre a linha `Vulkan: Present mode` do
log. Ela diferencia o modo solicitado na interface, o modo efetivamente usado,
os modos anunciados pelo driver e se houve fallback. Um fallback para FIFO é
válido quando Immediate ou Mailbox não estiver disponível; ele não deve ser
descrito como VSync desligado ou triple buffering efetivo no resultado do teste.

Para validar o JIT ARM64, execute uma sessão de pelo menos 30 minutos, encerre o
jogo pela opção **Sair** do menu do emulador e compartilhe o pacote de diagnóstico
depois de reabrir o aplicativo. Em uma nightly, o log deve conter
`JIT ARM64 differential: result=PASS` antes do carregamento do título e
`JIT ARM64 stats` no encerramento. Registre os campos `backendFailures`,
`publicationFailures`, `invalidatedFunctions` e
`retainedInvalidatedAllocationBytes`. A retenção representa alocações de código
invalidado que ainda não podem ser liberadas com segurança durante a execução;
crescimento contínuo em uma sessão prolongada exige investigação, não uma
liberação imediata insegura.

## Método de performance

Usar AB/BA, aquecimento controlado, no mínimo cinco repetições quando o teste for
curto e uma sessão prolongada para vazamentos/throttling. Reportar mediana e
dispersão; não aprovar mudança apenas pelo FPS médio.
