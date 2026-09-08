# Matriz inicial de funcionalidades

Legenda: **Sim**, **Parcial**, **Ausente**, **Não auditado**.

| Área | Desktop | Android baseline | Próxima ação |
|---|---:|---:|---|
| Vulkan | Sim | Sim | sincronizar correções upstream e testar por capacidade |
| OpenGL | Sim | Ausente | não priorizar; Vulkan é o backend móvel |
| Metal | Sim | N/A | manter código comum compilável |
| JIT AArch64 | Sim | Sim | testes diferenciais e correções upstream |
| Fallback interpretador | Sim | Sim | expor diagnóstico por jogo |
| Biblioteca de jogos | Sim | Sim | validar grandes bibliotecas/SAF |
| Instalar jogo/update/DLC | Sim | Sim | testes de integridade e rollback |
| WUA/WUX/WUD/RPX/ELF/WUHB | Sim | Sim/Não auditado | matriz por formato e conteúdo legal |
| Saves | Sim | Parcial | backup, export/import e testes de suspensão |
| Controles físicos | Sim | Sim | matriz de dispositivos e hotplug |
| Touch overlay | N/A | Sim | perfis por jogo e acessibilidade |
| Gyro | Sim | Sim | calibração e dispositivos externos |
| Rumble | Sim | Sim | intensidade/fallback por dispositivo |
| Teclado virtual Wii U | Sim | Sim | regressão por jogo |
| USB emulado/figuras | Sim | Sim | hardware real e persistência |
| Áudio | Sim | Sim | medir latência; avaliar AAudio |
| Pausar/retomar | Sim | Parcial | ciclo de vida, tela e áudio prolongados |
| Graphic packs | Sim | Sim | endurecer download/integridade e classificar packs |
| Driver AdrenoTools global | N/A | Sim | validar ABI/metadados e modo seguro |
| Driver por jogo | N/A | Sim | registrar driver efetivamente carregado |
| Mali/Immortalis | Sim via Vulkan | Parcial | somente driver do sistema; matriz real |
| Frame pacing Android | N/A | Ausente | Swappy/AChoreographer antes de FG |
| Overlay técnico móvel | Parcial | Parcial | frame times, clocks, temperatura e throttling |
| SGSR 1/2 | Não | Ausente | SGSR 1 após estabilidade/pacing; SGSR 2 experimental |
| FSR 1 | Graphic pack/casos | Ausente como módulo | definir pass comum de upscale |
| Frame generation | Não | Ausente | somente após contrato temporal |
| Atualizador do app | Sim no desktop | Ausente | releases exclusivas do fork, hash e assinatura |
| Base de compatibilidade | Wiki externa | Ausente | esquema versionado e relatório sanitizado |
| Download manager | Sim | Ausente | fora do MVP; não incluir conteúdo protegido |

