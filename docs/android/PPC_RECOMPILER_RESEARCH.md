# Pesquisa de recompilação PPC — 2026-10-15

## Escopo e referência

Base: `021d5105756a13269512ae1d9aaa20b0234aabfc`, árvore do APK
`998e73fd-nightly`, com 19/19 diferenciais validados no Odin2 Portal.
O PR desta etapa permanece empilhado sobre o PR #27; nenhum gate canário
pendente é dispensado.

Referência conceitual: ScarletNX, commit
`a5056dea736ac83f8de4daccdbfb1384ee2b0bee`, biblioteca de recompilação ARM
e testes diferenciais de ponto flutuante. Esta implementação é própria;
não importa código, dependências GPL, LLVM ou libgccjit do ScarletNX.
O frontend do Cemu permanece PowerPC e o backend permanece AArch64.

## Casos diferenciais

A nightly avança para `passed=22 failed=0 total=22`. Os três novos casos usam
expectativas independentes do JIT, além de comparar o estado completo com o
interpretador:

- `floating-sign-payload-alias`: `fmr` seguido de `fneg` com destino sobreposto;
  zeros com sinal, infinitos, NaNs com payload e subnormais de double.
- `floating-abs-payload-ps1`: `fabs` e `fnabs`, preservando os payloads e o PS1
  anterior de cada destino. Não é um teste de propagação aritmética de NaNs.
- `frsp-nearest-even-boundaries`: oito resultados literais, incluindo os dois
  sentidos de empate para pares, sinais, zeros e maior single finito. O modo
  é nearest-even; não declara cobertura dos outros modos de FPSCR nem FTZ.

O host confere os 16 vetores de `stfs` contra o helper real do interpretador
e os oito vetores de arredondamento contra conversão nearest-even. A execução
interpretador/JIT ARM64 continua sendo um gate físico separado do CI x86.

## Contadores opcionais por título

No Android, abra **Edit game profile > CPU execution diagnostics**, ative a
opção e inicie o título novamente. O default é desligado. Ao sair normalmente
pelo menu do Cemu, o log exporta o resumo e até 32 endereços com maior número
de execuções. O `report.json` acrescenta `ppcEntryProfile`, mantendo schema 4
e todos os campos anteriores. Logs antigos recebem `unavailable`; durante
uma sessão ainda ativa, o estado é `awaiting-shutdown`.

A métrica é `entry-executions`, com escopo `enterable-iml-segments`: conta
visitas a segmentos que podem ser entrada externa do JIT, inclusive visitas
internas a esses segmentos. Não conta todos os basic blocks, instruções PPC,
chamadas de função, ciclos reais ou tempo de CPU. O ranking aponta candidatos
para investigação, não demonstra quais funções custam mais tempo.

Quando desativado, nenhum contador por entrada é alocado e nenhuma instrução
de contagem é emitida no código do título. Quando ativado:

- A compilação registra o endereço PPC e aloca três contadores de 64 bits,
  cada um em uma linha de cache de 64 bytes. Até 32.768 entradas: 6 MiB para
  os contadores, além do mapa e alocações administrativas limitados pelo cap.
- Cada núcleo virtual escreve apenas seu contador. O emissor usa os scratch
  x25/x26 reservados pelo backend e instruções que preservam NZCV. Não há
  chamadas C++, locks, timers ou atomics por execução de entrada.
- Recompilações do mesmo endereço reutilizam os contadores. As alocações
  sobrevivem à invalidação do código, assim como o código antigo retido.
- A leitura ocorre somente depois de todas as CPUs convidadas pararem e do
  worker de compilação terminar. O código nativo é reclamado antes dos
  contadores. Nunca ler ou zerar os contadores durante gameplay.
- Capacidade esgotada ou falha de alocação omite a instrumentação daquela
  tradução, preservando o JIT normal. `uninstrumentedSegments` e
  `allocationFailures` deixam a cobertura parcial explícita (`complete=false`).
  Esses contadores de omissão são por tentativa/segmento, não endereços únicos.

Os testes sintéticos não entram nos contadores da sessão. Um autoteste adicional
ativa a instrumentação isoladamente e executa o mesmo programa uma vez em cada
UPIR 0/1/2, compara todo o estado com o interpretador e exige um incremento
por núcleo. Deve aparecer `JIT PPC entry profile selftest: result=PASS cores=3
stateCompared=true`, mesmo se a opção do jogo estiver desligada.

## Validação e custo

Testes do host: desligado por padrão, ponteiros estáveis, reaproveitamento por
endereço, limite de capacidade, três escritores em linhas distintas, ranking
determinístico e limpeza entre títulos. Testes Kotlin: serialização, cobertura
parcial, ranking incompleto/malformado e isolamento de sessões. CI: testes
Android, build ARM64, assinatura e identidade Nightly.

Gate físico: primeiro confirmar 22/22 e o autoteste de instrumentação; depois
capturar a mesma cena com a opção desligada e ligada, mesmo driver e clocks,
cache aquecido, duração e temperatura equivalentes. Medir CPU/frame, FPS,
stutter e RAM para quantificar overhead. Ele permanece `unmeasured` até esse
teste. Não usar a captura instrumentada como benchmark neutro de driver.

Para Wind Waker HD, a última baseline validada é o APK `998e73fd`, Turnip
Amaral v4.7.4.1, Odin2 Portal/Adreno 740: últimos cinco minutos com 30,060 FPS,
CPU 31,701 ms/frame, GPU 23,968 ms/frame a 475 MHz e zero quadros >50 ms.
Esses números são referência histórica; não são resultado desta etapa.

## Próximos experimentos

Usar os perfis para escolher trechos representativos, ampliar os diferenciais
PowerPC e só então comparar um protótipo opcional PPC→LLVM→AArch64 com o
emissor atual. Considerar tempo de preparação, memória e tamanho de código,
além de CPU/frame e stutter. Nenhum ganho de FPS é declarado nesta etapa.

Um cache persistente de código exige ainda formato versionado, hashes de
módulo/código, perfil e backend, relocations, publicação W^X, invalidação de
código modificado e fallback. O código instrumentado contém ponteiros locais
aos contadores e não pode ser salvo como código nativo reutilizável. Nenhum
cache AOT ou novo backend é habilitado por este diagnóstico.
