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

## Método de performance

Usar AB/BA, aquecimento controlado, no mínimo cinco repetições quando o teste for
curto e uma sessão prolongada para vazamentos/throttling. Reportar mediana e
dispersão; não aprovar mudança apenas pelo FPS médio.

