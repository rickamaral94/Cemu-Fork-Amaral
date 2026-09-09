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
implementação anterior também não media essas alocações retidas.

O primeiro incremento adiciona contadores fora do hot path de execução e corrige
dois casos seguros:

1. a estrutura da função é descartada quando o backend falha;
2. em AArch64, o código gerado é liberado quando a publicação falha, pois nunca
   foi inserido na tabela de saltos nem executado.

Código publicado e posteriormente invalidado continua retido até existir um
ponto de quiescência comprovado. A próxima decisão de arquitetura deve comparar
reclamação por época/RCU, barreira global dos núcleos emulados e liberação apenas
no encerramento do título.

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

Builds nightly executam sete casos sintéticos isolados antes do início do
título. Cada caso parte do mesmo estado, roda uma vez no interpretador e uma vez
no JIT AArch64 e compara o estado arquitetural resultante. A cobertura inicial
inclui:

- operações inteiras, Condition Register e rotação;
- branch condicional;
- load/store com validação de endianness;
- ponto flutuante e Paired Singles;
- reserva atômica `lwarx`/`stwcx.` com sucesso;
- falha de `stwcx.` quando o valor reservado foi alterado;
- smoke test de tradução para `eieio`, `sync` e `isync`.

Os casos atômicos também verificam a limpeza da reserva e todos os bits de CR0.
O bit SO de CR0 deve copiar `XER[SO]`; ele não pode reutilizar o valor anterior
de CR0.

O código sintético usa uma pequena alocação temporária no code cave, nunca é
publicado na tabela de saltos do jogo e é liberado antes de o título começar. Os
contadores do autoteste são zerados em seguida para não contaminar a telemetria
da sessão. A linha `JIT ARM64 differential` no log informa `PASS`, `FAIL` ou
`SKIP`. O recurso permanece desligado em builds stable/release.

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

## Critério para o próximo incremento

Ampliar gradualmente os casos diferenciais para exceções, alinhamento,
invalidação, concorrência e resultados de ponto flutuante especiais. Nenhuma otimização do
emissor, alocador de registradores ou linking de blocos será aprovada apenas por
FPS médio.
