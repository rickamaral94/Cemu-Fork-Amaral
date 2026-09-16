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

Para validar saída iniciada pelo próprio título, reproduza um caminho que chame
`coreinit.exit()`. O log deve registrar
`Android: forwarding PPC process exit status=<n> to activity`, concluir o
shutdown nativo e retornar/encerrar a atividade sem `SIGSEGV` em
`coreinit::OSShutdownThread`. A ação é reentrante: somente a primeira
notificação pode iniciar o encerramento. Um `OSPanic` do título continua sendo
registrado como falha do título; o frontend não deve acrescentar um segundo
crash durante a limpeza.

Esse gate foi aprovado no AYN Odin2 Portal com a build `e6b1e26-nightly` e
Xenoblade Chronicles X v16 (`00050000101c4d00`). Após o mesmo `OSPanic`, o
log registrou o encaminhamento de `status=1`, não registrou `SIGSEGV` e foi
preservado como `previous-game-session`. O shutdown reclamou 14.744 funções e
87.945.216 bytes. Uma tradução atingida pela invalidação concorrente foi
rejeitada antes da publicação e seus 4.096 bytes foram descartados com
segurança; não houve falha do backend.

Quando um título chama `OSPanic`, builds de diagnóstico registram o estado dos
registradores inteiros PPC e uma janela numérica de 128 bytes a partir do stack
pointer. A captura só ocorre depois do panic, não adiciona instrumentação ao hot
path e não desreferencia ponteiros nem converte a pilha em texto. O objetivo é
correlacionar argumentos preservados e frames internos antes de criar qualquer
workaround por Title ID. Um pacote usado para esse teste deve conter as linhas
`PPC panic context`, `PPC GPR` e `PPC stack` antes do stack trace simbólico.

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

Na build de barreiras, o resumo diferencial deve informar `passed=7 failed=0
total=7`, incluindo `case=memory-barrier-smoke`. Esse caso confirma tradução e
execução de `eieio`, `sync` e `isync`; não substitui o futuro teste concorrente de
ordenação entre núcleos.

Na build de alinhamento, o resumo deve informar `passed=8 failed=0 total=8`,
incluindo `case=unaligned-load-store`. O caso cobre acessos comuns de 16, 32 e 64
bits em RAM normal. Ele não testa atomics desalinhados nem comprova recuperação
de exceções de memória; provocar uma falha de host antes dessa infraestrutura
poderia encerrar o APK em vez de produzir um resultado diagnóstico controlado.

Na build de invalidação funcional, o log deve manter o resumo diferencial em
`passed=8 failed=0 total=8` e também registrar `JIT ARM64 invalidation:
result=PASS executed=true unlinked=true staleEntryBlocked=true reclaimed=true`.
O caso publica e executa um bloco antes de removê-lo da jump table. Ele ainda
não recompila uma segunda versão do bloco e não simula concorrência entre
núcleos.

Esse gate foi aprovado no AYN Odin2 Portal (Android 13, Snapdragon 8 Gen 2,
Adreno 740) com a build `54c32fe-nightly`. A sessão de aproximadamente 13
minutos e 36 segundos publicou 13.521 funções, registrou zero falhas de backend
e publicação e reclamou as 13.521 funções no encerramento. O resultado valida
este incremento isolado; a exigência geral de sessão prolongada permanece para
investigar aquecimento, vazamentos e crescimento do code cache.

Na build de recompilação após invalidação, a linha deve acrescentar
`retryUnblocked=true replacementExecuted=true oldResultBlocked=true`. O teste
altera o mesmo endereço PowerPC de `li r3, 0x1234` para `li r3, 0x5678`, rejeita
uma publicação atingida por invalidação, confirma que o endereço volta para
`unvisited` e publica uma tradução criada depois da invalidação. O resumo
diferencial continua em `passed=8 failed=0 total=8`.

Esse gate foi aprovado no AYN Odin2 Portal com a build `270f183-nightly` em
The Legend of Zelda: The Wind Waker HD e Tekken Tag Tournament 2. Os dois logs
registraram todos os campos do gate como `true`, diferencial `8/8`, zero falhas
de backend/publicação e limpeza integral no encerramento: 11.071 funções e
54.026.240 bytes no primeiro título; 19.302 funções e 193.650.688 bytes no
segundo. As sessões não registraram invalidações originadas pelos títulos
(`invalidatedFunctions=0`); o teste concorrente e a sessão prolongada continuam
pendentes.

Na build de valores especiais de Paired Singles, o resumo diferencial deve
avançar para `passed=9 failed=0 total=9` e incluir
`case=paired-single-special-values result=PASS`. O caso confirma preservação
bit a bit de `+0`, `-0`, infinito, NaN silencioso com payload e subnormal nos
quatro `ps_merge`. Ele também cobre `ps_merge10` com o destino sobreposto ao
segundo operando, exercitando o caminho temporário do backend ARM64. Essa
validação não cobre ainda modos de arredondamento, exceções de FP ou propagação
aritmética de NaN.

O gate foi aprovado no AYN Odin2 Portal com a build `d8b93c2-nightly` durante
uma sessão de aproximadamente 4 minutos e 8 segundos de Xenoblade Chronicles X.
O diferencial passou `9/9`, a invalidação permaneceu integralmente `PASS` e
15.108 funções, totalizando 108.257.280 bytes, foram reclamadas. O título chamou
`coreinit.exit(1)` após `OSPanic` e o frontend Android sofreu `SIGSEGV` no
shutdown por ausência de `SystemImplementation`; esse defeito de ciclo de vida
é registrado separadamente e não invalida o resultado determinístico do
autoteste FP.

Para validar a reclamação do code cache no ponto de quiescência, inicie um
título, jogue por pelo menos dez minutos e use **Sair** no menu do emulador. A
ação encerra o processo Android depois do shutdown nativo. Reabra o aplicativo,
gere o primeiro pacote de diagnóstico e repita o ciclo. Cada pacote deve
registrar `JIT ARM64 shutdown cleanup` com `reclaimedFunctions` e
`reclaimedAllocationBytes` maiores que zero. A segunda inicialização deve
continuar funcional e executar novamente os oito casos diferenciais. Essa
validação comprova a liberação entre execuções; não comprova reutilização segura
de código invalidado durante uma sessão ainda ativa.

O campo `log.source` deve ser `last-completed-game-session`,
`previous-game-session` ou `current-game-session`. `last-crash-session` indica
um crash histórico preservado e não serve como evidência deste ciclo de teste.

## Método de performance

Usar AB/BA, aquecimento controlado, no mínimo cinco repetições quando o teste for
curto e uma sessão prolongada para vazamentos/throttling. Reportar mediana e
dispersão; não aprovar mudança apenas pelo FPS médio.
