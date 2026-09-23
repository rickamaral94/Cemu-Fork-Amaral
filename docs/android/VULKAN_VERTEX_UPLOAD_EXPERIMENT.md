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
