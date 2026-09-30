# Experimento de uploads de vertex buffers no Android

## Hipótese

Uploads pequenos e realmente dinâmicos interrompem render passes Vulkan com
frequência suficiente para justificar uma cópia direta para um ring buffer
host-visible. Buffers estáticos ou reutilizados devem permanecer no cache
normal, que evita novas cópias por meio dos hashes de página.

## Baseline

- aparelho: AYN Odin2 Portal, Snapdragon 8 Gen 2 / Adreno 740;
- sistema: Android 13, tela em 120 Hz;
- driver: Turnip Amaral 26.3.0-devel v4.7.2.1, Vulkan 1.4.363;
- título: The Legend of Zelda: The Wind Waker HD;
- cenário: personagem parado, aproximadamente 7.200 draws por quadro;
- resultado: 20,03 FPS, 40,05 ms de CPU de renderização, 270 encerramentos
  de render pass por `bufferTransfer` e 275 reaberturas do mesmo FBO por
  quadro, usando as medianas da janela estável.

## Protótipo amplo rejeitado

A primeira implementação enviava todo vertex buffer elegível de até 4 KiB ao
ring buffer antes da verificação do cache. Em 438 amostras estáveis, ela
registrou:

| Métrica | Baseline | Protótipo amplo |
|---|---:|---:|
| FPS mediano | 20,03 | 20,03 |
| CPU de renderização | 40,05 ms | 39,65 ms |
| `bufferTransfer` | 270 | 258 |
| Reaberturas do mesmo FBO | 275 | 263 |
| Vertex pelo cache | 227 chamadas / 284 KiB | 218 / 278 KiB |
| Vertex direto | 0 | 293 / 512 KiB |

O protótipo aumentou o volume total de vertex data para aproximadamente 790
KiB por quadro sem melhorar FPS. Ele foi rejeitado porque copiava conteúdo que
o cache teria reconhecido como inalterado.

Não houve `VK_ERROR`, crash ou throttling no teste. A memória PSS passou de
1.036 para 1.743 MiB em uma única atualização após cinco minutos e permaneceu
estável; esse salto precisa continuar monitorado, mas não caracterizou
crescimento contínuo nessa sessão.

## Classificador dinâmico

A revisão seguinte mantém o cache como fallback e só promove uma combinação de
endereço, tamanho e stride depois de três mudanças de conteúdo consecutivas em
quadros distintos. Isso exige quatro observações antes do primeiro uso direto.
Uma observação estável zera a sequência e devolve o buffer ao cache.

O histórico:

- existe apenas no Android com Vulkan;
- aceita buffers de 1 a 4 KiB;
- é reiniciado em uma nova inicialização do GX2;
- é limitado a 2.048 entradas;
- não modifica hashes nem o conteúdo do buffer cache;
- preserva os limites e o fallback do ring buffer já existentes.

O log acrescenta `directVertexClassifier=[probes:<n>,changes:<n>]`. O número de
buffers efetivamente promovidos continua em `directVertex:<chamadas>/<KiB>`.

## Gate da próxima validação

Repetir o mesmo cenário parado e comparar uma janela estável de pelo menos cinco
minutos. A revisão só avança se:

1. `directVertex` ficar materialmente abaixo dos 512 KiB constantes do
   protótipo amplo;
2. `bufferTransfer` e reaberturas caírem sem aumento relevante de CPU;
3. FPS, correção visual, áudio, controles e save não regredirem;
4. PSS e memória gráfica não apresentarem crescimento contínuo;
5. não houver crash, `VK_ERROR` ou aquecimento que invalide a comparação.

Risco principal: custo adicional do hash FNV-1a nos candidatos pequenos. A
telemetria de `probes`, `changes` e CPU permitirá medir esse custo no aparelho.

## Resultado do classificador preventivo

A build `c1f03f17-nightly` foi testada no mesmo aparelho, driver e cenário por
mais de seis minutos. Em 366 amostras estáveis, o classificador examinou uma
mediana de 1.525 candidatos e encontrou 254 mudanças por quadro, mas não
promoveu nenhum buffer porque os mesmos endereços não mudavam em quadros
consecutivos.

| Métrica | Baseline | Classificador preventivo |
|---|---:|---:|
| FPS mediano | 20,03 | 19,88 |
| CPU de renderização | 40,05 ms | 41,89 ms |
| `bufferTransfer` | 270 | 259 |
| Reaberturas do mesmo FBO | 275 | 266 |
| Vertex direto | 0 | 0 |

O custo dos hashes preventivos sem nenhuma promoção torna essa revisão também
rejeitada. O salto de PSS de aproximadamente 1.035 para 1.742 MiB repetiu-se
mesmo sem uploads diretos, removendo o ring buffer direto como causa específica
desse comportamento.

## Classificador orientado pelo cache

A terceira revisão não calcula hashes preventivos. O cache informa ao chamador
se uma consulta realmente provocou upload. Cada combinação de endereço,
tamanho e stride é promovida depois de três uploads reais dentro de uma janela
de 120 quadros, sem exigir quadros consecutivos.

Somente buffers promovidos recebem hash adicional. Enquanto o conteúdo muda,
eles usam o ring buffer direto. Conteúdo estável rebaixa o buffer imediatamente
e o devolve ao cache; quando necessário, o primeiro upload que apenas atualiza
uma cópia do cache deixada para trás pelo caminho direto não conta como nova
evidência de dinamismo.

O diagnóstico passa a registrar `promotions` e `demotions` junto de `probes` e
`changes`. Nesta revisão, `probes` significa apenas verificações de buffers já
promovidos, e não todos os candidatos elegíveis.

## Diagnóstico dos uploads residuais

Uma sessão de 11 minutos na Ilha Taura, com Link parado na área de visão ampla
da vila, reproduziu uma queda persistente para aproximadamente 20 FPS. A cena
mantém cerca de 7.600 draws por quadro, 88 a 108 encerramentos de render pass
por transferência de buffer e 94 a 113 reaberturas do mesmo FBO. O caminho
direto usa somente 29 a 37 KiB e 154 a 204 chamadas por quadro, portanto não
atinge seus limites de 512 KiB ou 512 chamadas.

Antes de ampliar a otimização, a telemetria passa a classificar o trabalho que
permanece no cache em `directVertexEligibility`:

- `smallRequests`: consultas de vertex buffers elegíveis de até 4 KiB;
- `historyMisses`: chaves de endereço, tamanho e stride vistas pela primeira vez;
- `cacheHits`: consultas que não exigiram upload;
- `learningUploads`: uploads em quadros distintos usados pelo classificador;
- `sameFrameUploads`: uploads repetidos da mesma chave no mesmo quadro;
- `ringRejects`: buffers promovidos que não couberam no ring buffer;
- `oversizedUploads` e `oversizedKiB`: consultas acima de 4 KiB que fizeram upload;
- `historyResets`: limpezas ao atingir o limite de 2.048 entradas.

Esta etapa é somente diagnóstica e não muda limites, promoção, hashing,
sincronização Vulkan nem o fallback para o buffer cache.

Em 1.891 amostras da cena estável, `learningUploads` teve mediana de 41 por
quadro e correlação de 0,918 com `bufferTransfer` e reaberturas do mesmo FBO.
Não houve rejeição pelo ring buffer nem limpeza da tabela, e uploads acima de
4 KiB foram desprezíveis. A instrumentação mostrou que acertos intermediários
do cache zeravam a evidência, fazendo a implementação exigir uploads
consecutivos apesar de a política documentada aceitar três uploads reais na
janela de 120 quadros.

A revisão seguinte preserva `recentUploads` durante acertos do cache e só zera
a evidência quando o último upload ultrapassa 120 quadros. Hashing continua
restrito a buffers já promovidos; conteúdo estável ainda causa rebaixamento e
retorno imediato ao cache.

O teste dessa revisão manteve 27,16 FPS de mediana por aproximadamente 48
minutos, contra 20,06 FPS na sessão anterior, sem crash, erro Vulkan ou
throttling. A CPU de renderização caiu de 39,66 para 31,31 ms. Apesar do ganho,
as transferências caíram apenas 4% e foram observadas 28.155 promoções e 23.330
rebaixamentos, portanto o ganho fica preservado como candidato enquanto a
causa específica continua sob investigação.

Um teste posterior concedeu tolerância até o fim do quadro para impedir que uma
consulta estável desfizesse imediatamente uma promoção. Em 2.132 amostras e 38
minutos, as despromoções caíram de 8,11 para 0,11 por segundo e as transferências
de buffer caíram de 99 para 68 por quadro. Porém, o caminho direto subiu de 190
chamadas e 34 KiB para 483 chamadas e 467 KiB por quadro, passou a sofrer 9,46
rejeições do ring por quadro e o FPS médio caiu de 27,16 para 20,85. Não houve
throttling, pressão de memória ou erro Vulkan.

A tolerância foi portanto removida, preservando o comportamento da revisão que
atingiu 27,16 FPS. Para localizar quais tamanhos podem ser promovidos sem saturar
o ring, o log agora registra contagens mutuamente exclusivas em
`directVertexPromotionSizes` e `directVertexBindSizes`, nas faixas de até 256 B,
512 B, 1 KiB, 2 KiB e 4 KiB. Esta etapa não altera o limite elegível de 4 KiB,
os limites do ring, a sincronização Vulkan nem o fallback para o buffer cache.

Em seguida, uma sessão de 11 minutos na cena ampla da Ilha Taura registrou
7.519 draws por quadro em média, 22,09 FPS, 189 binds diretos e 34 KiB por
quadro, sem rejeições do ring. Os histogramas mostraram que buffers de até
256 B responderam por 95,32% dos binds diretos com apenas 11,58% das promoções.
Já a faixa exclusiva de 1.025 a 2.048 B respondeu por 81,65% das promoções,
mas somente 4,44% dos binds.

Com base nessa relação entre custo e reutilização, somente buffers de até 256 B
podem ser promovidos. Os candidatos maiores continuam usando o buffer cache e
o fallback existente; o limite de observação permanece em 4 KiB e os limites
do ring e a sincronização Vulkan não mudam.

## Motivos de encerramento do fast draw

O limite de 256 B reduziu promoções em 86,5%, despromoções em 99,7% e o custo
de CPU normalizado por mil draws em aproximadamente 9,8%. Na cena mais pesada,
porém, cerca de 2.100 draws por quadro ainda iniciam uma nova sequência em vez
de reutilizar a sequência rápida.

Para investigar esse custo no core, sem depender de `TitleId`, GPU ou jogo, o
log passa a registrar `fastDrawPassEnds`: streamout ativo, fim da fila de
comandos, mudança de textura, mudança de contexto, mudança de sampler, comando
Type-3 não suportado ou outro tipo de pacote. Esta etapa é somente diagnóstica
e não amplia o fast path nem altera estado gráfico, sincronização ou fallback.

Os diagnósticos de 26 de setembro mostraram que mudanças de contexto respondem
por aproximadamente 94–95% dos encerramentos na cena pesada. Para localizar o
estado responsável sem adicionar custo de um histograma por registrador, o log
agora divide o espaço de contexto em 16 faixas de 256 registradores (`A0xx` a
`AFxx`). A contagem ocorre somente quando o valor escrito realmente mudou e
continua sendo uma observação global do core, sem condição por jogo.

Uma captura sustentada com 1.092 quadros acima de 7.000 draws confirmou que
`A2xx` representa 92,80% das mudanças de contexto, seguida por `A1xx` com
6,99%. A próxima etapa subdivide exclusivamente `A2xx` em 16 faixas de 16
registradores (`A20x` a `A2Fx`) para separar alterações de programa de shader
das alterações de geometria/VGT. O caminho de execução permanece inalterado.

Uma segunda captura sustentada, com 1.464 quadros acima de 7.000 draws,
concentrou 40,08% das mudanças `A2xx` em `A21x` e 54,82% em `A22x`; todas as
faixas de geometria `A28x` a `A2Fx` permaneceram zeradas. O diagnóstico agora
contabiliza o endereço inicial exato dos pacotes em `A210`–`A21F` e
`A220`–`A22F`, regiões associadas a programas, recursos e anéis de shader.
Nenhum registro é aceito pelo fast path nesta etapa.

A captura seguinte mostrou que 88,24% das mudanças de contexto pertencem a
pacotes iniciados em `A216` e `A225`. Como o endereço inicial não identifica
qual palavra do pacote realmente mudou, o diagnóstico agora também contabiliza
cada valor alterado entre `A210` e `A22F`. Isso separa endereços, tamanhos e
recursos dos programas de shader sem ampliar o fast path ou alterar renderização.

A contagem por palavra confirmou mudanças reais e frequentes nos endereços de
vertex e fetch shader. Como o número de pipelines Vulkan permaneceu estável, o
diagnóstico agora mede inícios de sequência, consultas e hits/misses do cache,
pipelines disponíveis, binds reais e binds redundantes evitados. Esta etapa
continua estritamente observacional e não altera seleção ou criação de pipeline.

Com o cache aquecido, a cena pesada apresentou 100% de hits e nenhuma criação,
apesar de cerca de dois mil reinícios de sequência por quadro. O diagnóstico
agora acumula separadamente o tempo de CPU em início de sequência, consulta do
cache e chamada de bind Vulkan. A medição usa o temporizador já existente no
monitor de performance e não modifica decisões do renderer ou do driver.

Os tempos de reinício, cache e bind representam apenas uma pequena parcela do
custo de CPU observado. Para separar o trabalho do renderer daquele realizado
pelo processador de comandos, o monitor agora acumula o tempo total dos
primeiros draws de cada sequência e dos draws contínuos. Nenhuma etapa interna
é alterada e os dois caminhos continuam executando o código original.


## Tempo ativo e espera por comandos

A captura da build `5e2adce0-nightly` no mesmo Odin2 Portal, Turnip Amaral
26.3.0-devel v4.7.3.1 e cena pesada da Ilha Taura forneceu 1.421 quadros
estáveis acima de 7.000 draws. As medianas foram 19,88 FPS e 41,464 ms no
intervalo monitorado entre apresentações. O primeiro draw de cada sequência
somou 5,895 ms e os draws contínuos 1,478 ms, totalizando 7,498 ms. As 2.056
consultas medianas ao cache de pipeline foram 100% hits; início de sequência,
consulta do cache e bind consumiram 1,316 ms, 0,417 ms e 1,074 ms,
respectivamente.

A telemetria do Android manteve a GPU em 55,06% de uso mediano a 401 MHz, sem
throttling, enquanto `OSSched[core=1]` ficou em 97,87% e `LatteThread` em
45,54%. Portanto, cache, bind e corpo dos draws Vulkan não explicam sozinhos os
aproximadamente 34 ms restantes do intervalo.

O monitor já mede o tempo em que o processador de comandos espera novos dados,
mas esse valor não era exportado. O diagnóstico agora registra
`commandIdleMs` e `nonIdleMs` nas linhas de performance. Também encerra a
medição quando um comando chega durante a segunda consulta do ringbuffer,
evitando subcontagem nesse retorno rápido. A mudança é somente observacional:
não altera parsing de comandos, estado gráfico, sincronização, renderer ou
driver. A próxima captura separará espera pelo produtor PowerPC de trabalho
ativo no processador de comandos antes de qualquer otimização.
### Command processor timing

The Android diagnostics build also reports inclusive command processor timing:

- `commandBufferMs`: indirect command-buffer processing, including renderer calls.
- `continuousPassMs`: the optimized continuous draw parser, including continued draws.
- `genericPathMs`: `commandBufferMs - continuousPassMs`.
- `outsideCommandBufferMs`: active Latte time not accounted for by indirect command-buffer processing.

The values deliberately use broad, low-frequency scopes. Existing `firstDrawMs` and
`continuedDrawMs` can be subtracted during offline analysis without adding a timer to
every packet. This keeps the diagnostic global and limits measurement overhead.

The following diagnostic revision adds packet and payload-word counts for five broad
categories in both the generic and continuous parsers. The counters run on the GPU
thread and intentionally avoid atomics and per-packet timers. They identify the hot
packet family while keeping the measured timings useful.
