# Baseline do JIT ARM64

## Escopo

Este documento registra o estado do recompilador PowerPC para AArch64 antes de
alterações no emissor. O backend existente do Cemu é a base obrigatória; uma nova
camada de tradução não será criada sem evidência de que ele é insuficiente.

## Proteções já presentes

- O backend AArch64 usa a revisão fixada `904b8923457f3ec0d6f82ea2d6832a792851194d`
  de `xbyak_aarch64`, registrada no submódulo do repositório.
- A memória gerada começa gravável e passa para somente leitura/execução antes de
  ser publicada, mantendo W^X no fluxo normal.
- `readyRE()` conclui a transição de proteção e sincroniza os caches de dados e
  instruções na implementação Android/Linux.
- Falhas de tradução retornam ao fluxo do interpretador; o primeiro bloco da
  tabela de saltos é reservado para esse fallback.
- A invalidação retira os pontos de entrada da tabela antes de remover as faixas
  da busca de funções.

## Dívida confirmada

`PPCRecompiler_deleteFunction()` não pode liberar imediatamente uma função já
publicada: outro núcleo emulado pode ainda estar executando o código nativo. A
implementação anterior também não media essas alocações retidas nem as liberava
ao encerrar o título.

O primeiro incremento adiciona contadores fora do hot path de execução e corrige
dois casos seguros:

1. a estrutura da função é descartada quando o backend falha;
2. em AArch64, o código gerado é liberado quando a publicação falha, pois nunca
   foi inserido na tabela de saltos nem executado.

Código publicado e posteriormente invalidado continua retido durante a sessão.
Todo código AArch64 publicado agora é registrado uma única vez e liberado no
encerramento do título. Esse ponto é seguro porque `CafeSystem` já removeu todas
as threads PowerPC e `PPCRecompiler_Shutdown()` já encerrou e aguardou o worker
de compilação antes da liberação.

A linha `JIT ARM64 shutdown cleanup` registra a quantidade de funções e bytes
reclamados. Isso impede o acúmulo entre títulos iniciados no mesmo processo, mas
não limita o crescimento causado por muitas invalidações dentro de uma única
sessão longa. Reutilização durante a execução ainda exige uma estratégia por
época/RCU ou outra barreira global comprovada.

No Android, a ação **Sair** agora executa `CafeSystem::Shutdown()` antes de
encerrar o processo. Anteriormente o fluxo registrava a telemetria e chamava
diretamente `exitProcess(0)`, pulando `ShutdownTitle()` e toda a limpeza nativa.
O flush do log ocorre depois do shutdown, preservando a evidência de liberação
para o pacote de diagnóstico da próxima inicialização. Antes de encerrar o
processo, o Android também cria uma cópia limitada do gameplay concluído. Essa
cópia tem prioridade sobre crashes históricos e mantém o cabeçalho e o final do
log, onde ficam a telemetria e a linha de limpeza do code cache.

## Telemetria

Ao sair pelo menu do emulador, o log registra uma linha `JIT ARM64 stats` com:

- tentativas e falhas de tradução/backend;
- funções e bytes gerados;
- funções publicadas e falhas de publicação;
- código não publicado descartado com segurança;
- funções invalidadas e bytes mantidos por segurança.

Os contadores são atualizados durante compilação, publicação e invalidação. Não
há instrumentação adicionada ao caminho que executa cada bloco recompilado.

## Testes diferenciais da nightly

Builds nightly executam onze casos sintéticos isolados antes do início do
título. Cada caso parte do mesmo estado, roda uma vez no interpretador e uma vez
no JIT AArch64 e compara o estado arquitetural resultante. A cobertura inicial
inclui:

- operações inteiras, Condition Register e rotação;
- branch condicional;
- load/store com validação de endianness;
- ponto flutuante e Paired Singles;
- preservação bit a bit de valores especiais nos quatro `ps_merge`, incluindo
  destino sobreposto a uma das fontes;
- reserva atômica `lwarx`/`stwcx.` com sucesso;
- falha de `stwcx.` quando o valor reservado foi alterado;
- smoke test de tradução para `eieio`, `sync` e `isync`.
- loads e stores desalinhados de 16, 32 e 64 bits, incluindo extensão de sinal,
  endianness e ponto flutuante de precisão dupla.
- carry de `subfic` com imediato `-1`, que exige preservar o carry de entrada
  separado do imediato já convertido;
- `sthbrx` com confirmação de que o registrador-base permanece inalterado.

Dois casos negativos confirmam separadamente que `fcmpo` e `mcrfs` não são
traduzidos como `fcmpu`. Como `fcmpo` ainda não possui implementação completa no
backend, ambos devem recusar a tradução e seguir pelo fallback do interpretador,
em vez de executar uma semântica diferente silenciosamente.

Os casos atômicos também verificam a limpeza da reserva e todos os bits de CR0.
O bit SO de CR0 deve copiar `XER[SO]`; ele não pode reutilizar o valor anterior
de CR0.

O código sintético usa uma pequena alocação temporária no code cave, nunca é
publicado na tabela de saltos do jogo e é liberado antes de o título começar. Os
contadores do autoteste são zerados em seguida para não contaminar a telemetria
da sessão. A linha `JIT ARM64 differential` no log informa `PASS`, `FAIL` ou
`SKIP`. O recurso permanece desligado em builds stable/release.

A nightly também publica um bloco sintético simples na jump table, executa o
bloco e invalida toda a faixa PowerPC correspondente antes de iniciar as
threads do jogo. A linha `JIT ARM64 invalidation` confirma separadamente que o
bloco executou, que o ponto de entrada voltou ao fallback não visitado e que uma
tentativa sem recompilação não reutilizou o ponteiro nativo obsoleto. O código
de teste é reclamado ainda nesse ponto quiescente e seus contadores são zerados
antes do título real.

## Validação física da invalidação publicada

A build `54c32fe-nightly`, gerada para o PR #10 com head
`406850a33865a90d3c4d2fbc88112bc94f6293b0`, foi validada em um AYN Odin2
Portal com Android 13, Snapdragon 8 Gen 2 e Adreno 740. Uma sessão real de
aproximadamente 13 minutos e 36 segundos registrou:

- `JIT ARM64 invalidation: result=PASS executed=true unlinked=true staleEntryBlocked=true reclaimed=true`;
- `JIT ARM64 differential: result=PASS passed=8 failed=0 total=8`;
- 13.521 funções geradas e publicadas, sem falha de backend ou publicação;
- 13.521 funções e 65.282.048 bytes reclamados no encerramento.

O log veio da sessão anterior de jogo, sem truncamento, após encerramento pelo
fluxo normal do aplicativo. Essa evidência aprova o caso funcional isolado e a
limpeza no ponto quiescente. Ela não comprova recompilação de uma segunda versão
do bloco, invalidação concorrente nem ausência de crescimento em sessões longas.

## Recompilação após invalidação

O incremento seguinte corrige um bloqueio de progresso encontrado no caminho de
publicação. Quando uma faixa era invalidada durante a compilação, a função nova
era rejeitada corretamente, mas o ponto de entrada podia permanecer marcado
como `visited`. O interpretador não agenda um endereço nesse estado novamente,
portanto a tradução atualizada poderia deixar de ser produzida.

Ao rejeitar uma publicação atingida por invalidação, o JIT agora devolve o ponto
de entrada para `unvisited` sob o mesmo lock. A nightly amplia o teste publicado
para escrever e executar uma primeira versão, substituir as instruções PowerPC,
invalidar o bloco, simular outra invalidação entre compilação e publicação e
confirmar que:

- a tradução produzida antes da última invalidação não é publicada;
- uma nova visita pode solicitar outra compilação;
- a segunda versão executa `li r3, 0x5678`, em vez do resultado antigo
  `0x1234`;
- as duas funções publicadas são removidas e reclamadas no ponto quiescente do
  autoteste.

O teste continua restrito à nightly e ocorre antes do worker e das CPUs
emuladas. Ele cobre a transição funcional e a recuperação do retry, mas ainda
não constitui um teste concorrente entre núcleos emulados.

## Validação física da recompilação após invalidação

A build `270f183-nightly`, produzida para o PR #11 com head
`276d848bf00c22c55c335ccfc924d76f7e5745cd`, foi validada no AYN Odin2 Portal
com Android 13, Snapdragon 8 Gen 2 e Adreno 740 em dois títulos distintos:

| Título | Duração aproximada | Funções publicadas | Bytes reclamados | Falhas de tradução | Falhas de backend/publicação |
|---|---:|---:|---:|---:|---:|
| The Legend of Zelda: The Wind Waker HD (`0005000010143500`) | 9 min 33 s | 11.071 | 54.026.240 | 1 | 0 / 0 |
| Tekken Tag Tournament 2 (`000500001010f800`) | 4 min 38 s | 19.302 | 193.650.688 | 15 | 0 / 0 |

Nos dois pacotes o autoteste registrou integralmente:

`JIT ARM64 invalidation: result=PASS executed=true unlinked=true staleEntryBlocked=true retryUnblocked=true replacementExecuted=true oldResultBlocked=true reclaimed=true`

O diferencial também passou com `passed=8 failed=0 total=8`. O encerramento
reclamou todas as 11.071 e 19.302 funções publicadas, respectivamente, sem crash
registrado. As falhas de tradução são recusas do frontend com fallback e não
falhas do backend AArch64; nenhuma publicação falhou.

A evidência aprova a transição funcional, o desbloqueio do retry, a execução da
tradução substituta e o bloqueio do resultado antigo no teste isolado. Os títulos
não invalidaram funções durante essas sessões (`invalidatedFunctions=0`), então
a validação não comprova invalidação concorrente causada pelo jogo nem limita o
crescimento do code cache em uma sessão longa.

## Invalidação durante execução concorrente

A nightly também mantém uma tradução sintética ARM64 executando em uma thread
host enquanto outra thread invalida a faixa PowerPC correspondente. O teste
confirma que a entrada é removida da jump table, novas entradas não alcançam o
ponteiro obsoleto e a execução que já estava ativa consegue terminar. A mapping
nativa só é reclamada depois do `join`, em um ponto de quiescência explícito.

O log esperado é:

`JIT ARM64 concurrent invalidation: result=PASS executionStarted=true
unlinkedWhileRunning=true staleEntryBlocked=true activeExecutionCompleted=true
reclaimedAfterJoin=true`

Esse teste valida a regra de retenção usada pelo fork, mas não implementa
reclamação durante gameplay e não simula o escalonamento completo dos três
núcleos PowerPC. Reutilização de código durante a sessão continua dependendo de
uma estratégia por época/RCU ou barreira global comprovada.

## Valores especiais de Paired Singles

O caso `paired-single-special-values` amplia o diferencial sem depender de
arredondamento ou de regras de propagação de NaN do host. Os quatro `ps_merge`
movem combinações de `+0`, `-0`, infinito, NaN silencioso com payload e
subnormal, e o resultado é comparado bit a bit entre interpretador e JIT.

`ps_merge10 f2, f1, f2` mantém o destino sobreposto ao segundo operando. Esse
formato exercita explicitamente o temporário necessário para que o backend não
sobrescreva `f2.ps0` antes de copiá-lo para `f2.ps1`. O autoteste continua
isolado em nightly e não altera o estado do título real.

### Validação física

A build `d8b93c2-nightly`, produzida no PR #12 com head
`55bb7e6637c3c6b927aca543d8c59c73964f00ea`, foi executada no AYN Odin2
Portal com Android 13, Snapdragon 8 Gen 2, Adreno 740 e Turnip Amaral
26.3.0-devel v4.6.1.1. Xenoblade Chronicles X
(`00050000101c4d00`) executou por aproximadamente 4 minutos e 8 segundos.

O log registrou `case=paired-single-special-values result=PASS`, resumo
`passed=9 failed=0 total=9` e o gate de invalidação integralmente `PASS`.
No encerramento foram reclamadas 15.108 funções e 108.257.280 bytes.

O título chamou `coreinit.exit(1)` após um `OSPanic` próprio e, depois da
limpeza, o host Android sofreu `SIGSEGV` em `coreinit::OSShutdownThread`.
A inspeção mostrou que `NotifyPPCProcessExit()` desreferencia
`s_implementation`, que não é instalado no frontend Android. Esse defeito de
encerramento é independente do autoteste FP e será corrigido em incremento
isolado; ele não deve ser ocultado nem atribuído ao backend AArch64.


O caminho de referência do interpretador é executado com o recompilador
temporariamente suspenso. Isso impede que o `blr` usado para encerrar cada caso
marque o endereço de escape `0x00000000` para compilação assíncrona. Essa
isolação é obrigatória: o autoteste não pode alterar a fila nem os metadados do
JIT usados pelo título real.

## Barreiras de memória

`sync` e `eieio` agora geram uma operação IML com efeito colateral. No backend
AArch64 ela é emitida como `DMB ISH`, ordenando de forma conservadora os acessos
à RAM compartilhada pelos núcleos emulados. O interpretador usa uma barreira
sequencialmente consistente equivalente. O backend x86-64 preserva o contrato
genérico do IML com `MFENCE`.

O smoke test diferencial comprova que as três instruções são decodificadas, que
o bloco é gerado pelo backend AArch64 e que o estado arquitetural continua
equivalente ao interpretador. Ele não comprova sozinho ordenação entre núcleos;
essa propriedade exige um teste litmus concorrente e repetido em dispositivo.

`isync` continua sem emitir `ISB` do host. Um `ISB` AArch64 sincronizaria a busca
das instruções AArch64, mas não invalidaria código PowerPC já traduzido. A
semântica de `isync` será fechada junto de `icbi`, invalidação do JIT e
self-modifying code para evitar uma correção apenas aparente.

## Alinhamento e exceções

O caso `unaligned-load-store` usa apenas RAM normal já mapeada e endereços dentro
da alocação temporária do teste. A preparação e a validação são feitas byte a
byte para que o próprio teste não dependa de um acesso C++ desalinhado. O caso
compara o interpretador com o JIT para `lwz`, `lhz`, `lha`, `stw`, `sth`, `lfd` e
`stfd`.

Operações de reserva atômica desalinhadas não entram nesse teste. Além de a
semântica PowerPC desse uso não oferecer um resultado portável, loads/stores
atômicos AArch64 podem gerar uma falha de alinhamento no host. O manipulador de
`SIGBUS`/`SIGSEGV` atual do Android registra o crash e encerra o processo; ele
ainda não converte uma falha do JIT em exceção do convidado ou fallback seguro.
Portanto, exceções de memória continuam pendentes e serão tratadas junto do
desenho de recuperação do JIT, sem declarar suporte com base apenas neste smoke
test.

## Critério para o próximo incremento

O diferencial `9/9` e o caso de valores especiais foram validados em dispositivo.
O próximo incremento do JIT deve cobrir aritmética de ponto flutuante com
arredondamento. O crash observado após `coreinit.exit(1)` pertence ao ciclo de
vida Android e será tratado separadamente. Exceções recuperáveis e concorrência
continuam pendentes. Nenhuma otimização do emissor, alocador de registradores ou
linking de blocos será aprovada apenas por FPS médio.
