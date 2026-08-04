# Documento 2 — Arquitetura do firmware ESP32 (ESP32-2432S028 "CYD") e plano de implementação

> Como portar **apenas a camada de exibição** do mascote Clawd para um ESP32 com
> display TFT integrado, atuando como cliente do protocolo Mobile v1
> (ver [01-PROTOCOL.md](01-PROTOCOL.md)).

---

## 1. Hardware alvo — ESP32-2432S028 ("Cheap Yellow Display" / CYD)

| Componente | Especificação | Impacto no projeto |
|---|---|---|
| SoC | ESP32-WROOM-32 (dual-core 240 MHz, Wi-Fi/BT) | Roda Wi-Fi + WS + decode de GIF confortavelmente. |
| RAM | **520 KB SRAM interna**; **~297 KB de heap livre** medido no boot / ~248 KB após Wi-Fi. **Sem PSRAM** (confirmado — ver callout abaixo) | É a restrição dominante. Uma animação em RAM por vez; desenho direto na tela. |
| Flash | **4 MB** (confirmado) | Aperta app + assets + (OTA). Ver §3/§4. |
| Display | 2.8" TFT **ILI9341 240×320**, SPI | Portrait 240×320; arte do Clawd é ~quadrada → *letterbox* + barra de status. |
| Touch | XPT2046 (resistivo) | Opcional (não requerido para exibição). |
| microSD | Slot SPI em **barramento separado** do TFT | Coexiste com o TFT; usável para config e/ou assets. Ver §4. |
| Extras | LED RGB, LDR, alto-falante | Opcionais (ex.: LED como indicador de estado; som em `notification`). |

> ### ✅ Resultados do bring-up (Fase 1) — medidos no hardware real
> A placa em mãos é a CYD **sem PSRAM**. Confirmado ao rodar o firmware de
> bring-up ([`src/main.cpp`](../../src/main.cpp)):
> - **Chip:** ESP32-**D0WD-V3 rev3** (WROOM, dual-core 240 MHz) — **sem PSRAM**.
> - **Flash:** 4,0 MB. **Heap livre:** ~297 KB (boot) / ~248 KB (pós-Wi-Fi).
> - **Display:** ILI9341 240×320 OK, **porém precisa de `TFT_INVERSION_ON`**
>   (sem ela as cores saem em negativo); ordem de canais **`TFT_BGR` correta**
>   (vermelho aparece como seu inverso, não trocado com azul).
> - **Wi-Fi:** conecta normalmente (RSSI ~−52 dBm).
>
> **Consequência:** vale o **plano sem PSRAM** — assets GIF curados em LittleFS,
> **uma animação em RAM por vez**, **desenho direto na tela** (sem framebuffer
> completo). Os ~250–290 KB de heap comportam isso com folga (ver §3/§8).
>
> *(Se no futuro se usar uma placa com PSRAM, ela seria um upgrade opcional:
> permitiria cache de várias animações e canvas off-screen — mas **não** é o alvo
> atual e **não** adiciona Flash.)*

Observação sobre o `platformio.ini` atual: o env `esp32-2432S028` usa
`board = esp32dev` "cru", sem flags do TFT_eSPI, sem `lib_deps` e sem tabela de
partição customizada. Isso está correto eletricamente (é um WROOM-32), mas
precisará das flags de build do display e de um esquema de partição adequado
(proposto na §7). **Nenhum código/config foi alterado ainda** — conforme pedido.

---

## 2. Assets gráficos — análise

### 2.1 O que existe no projeto de origem

| Fonte | Formato | Resolução | Qtd. | Tamanho | Portável p/ ESP32? |
|---|---|---|---|---|---|
| `themes/clawd/assets/*.svg` (tema **padrão**) | **SVG** com animação SMIL/CSS | vetorial | 48 | — | ❌ Não — exige engine SVG+CSS+SMIL. Inviável no ESP32. |
| `assets/gif/clawd-*.gif` | **GIF** (89a, paleta ≤256 cores) | **302×300** | 24 | **1,9 MB** | ✅ **Sim** — é a fonte prática. |
| `assets/gif/*` (clawd + calico + cloudling) | GIF | 302×300 (mini menores) | 60 | **10 MB** | ✅ Sim, mas grande (multi-tema). |
| `themes/calico/assets/*.apng` | APNG | — | ~25 | — | ⚠️ Só se optar pelo tema Calico (decode APNG é mais raro). |
| `themes/cloudling/assets/*.svg`+png | SVG/PNG | — | ~30 | — | ❌ SVG animado não. |

**Conclusão:** o desktop desenha **SVG animado** (não portável). Para o ESP32, a
fonte de arte realista são os **GIFs** `assets/gif/clawd-*.gif` (pixel-art de
paleta reduzida — ideal para GIF), que serão **curados, redimensionados e
reotimizados** por um pipeline no host (§6).

### 2.2 Subconjunto necessário (tema "clawd", estados essenciais)

10–13 arquivos cobrem todos os estados do protocolo (ver mapa em
[01-PROTOCOL.md §6.3](01-PROTOCOL.md)):

`clawd-idle`, `clawd-thinking`, `clawd-typing` (working), `clawd-error`,
`clawd-happy` (attention), `clawd-notification`, `clawd-sweeping`,
`clawd-carrying`, `clawd-sleeping`, `clawd-headphones-groove`/`clawd-juggling`.
Opcionais de "tier": `clawd-building`.

Estimativa **após pipeline** (redimensionar 302×300 → ~220×220 e requantizar
para 32–64 cores com `gifsicle -O3`):

| Cenário | Arquivos | Tamanho estimado |
|---|---|---|
| MVP clawd (essenciais, ~220px, 32–64 cores) | ~12 | **~0,6–1,2 MB** |
| Clawd completo (com tiers, mini, reações) | ~24 | ~1,5–2,2 MB |
| Multi-tema (clawd+calico+cloudling) | ~60 | ~5–10 MB |

Esse número é o que decide flash vs. SD (§4).

---

## 3. Estratégia de renderização (a decisão técnica central)

Três abordagens avaliadas:

| Abordagem | RAM | Flash/Storage | CPU | Qualidade | Veredito |
|---|---|---|---|---|---|
| **A. GIF decode em runtime** (`AnimatedGIF` + `TFT_eSPI`) | Baixa (buffer LZW ~alguns KB + 1 GIF em RAM opcional) | Pequeno (assets GIF) | Média | Boa p/ pixel-art | ✅ **Recomendada** |
| B. Frames RGB565 crus pré-renderizados | Alta se bufferizar | **Enorme** (240×240×2 = 115 KB **por frame**) → dezenas de MB | ~Zero | Perfeita | ❌ Exige SD grande; mata a flash |
| C. Frames com RLE/paleta custom | Média | Médio | Baixa | Boa | ⚠️ Muita engenharia; sem ganho claro sobre A |

**Escolha: Abordagem A — decodificar GIF em tempo de execução.**
- Bibliotecas comprovadas no CYD: `bitbank2/AnimatedGIF` (Larry Bank) + `bodmer/TFT_eSPI`.
- O `AnimatedGIF` decodifica **linha a linha** e desenha via callback no TFT —
  **não precisa de framebuffer inteiro**. Uso de RAM: buffer de trabalho LZW +
  um buffer de linha (poucos KB).
- GIF é paletizado (≤256 cores) — perfeito para a arte pixel-art do Clawd.

**Padrão de projeto recomendado — "carregar-para-RAM na troca de estado":**

> Quando o estado dominante muda, carregue **uma vez** os bytes do GIF do estado
> (do flash **ou** do SD) para um **buffer em heap** e, a partir daí, faça o loop
> de decode **a partir da RAM**. Assim a velocidade do storage (flash vs SD) só
> importa no instante da troca de estado — **nunca** durante a reprodução, o que
> torna a origem dos assets **irrelevante para o FPS**.

Um GIF curado fica em ~40–150 KB → cabe no heap (temos ~250–290 KB livres, **sem
PSRAM** — ver §1). Duas variantes do padrão, conforme o storage:
- **Carregar 1 GIF para RAM na troca de estado** (recomendado): mantém **uma**
  animação em heap e faz o loop de decode a partir dela. A origem (flash/SD) só
  importa no instante da troca.
- **Decodificar direto do arquivo** (`AnimatedGIF::open` por arquivo): RAM ainda
  menor, útil se o heap ficar apertado; latência de troca um pouco maior.

> **Sem framebuffer completo.** Sem PSRAM, evitamos um `TFT_eSprite` de quadro
> inteiro (240×320×2 ≈ 150 KB comeria mais da metade do heap). O `AnimatedGIF`
> desenha **linha a linha** direto no TFT, então não precisamos dele. Para evitar
> cintilação na barra de status, atualizamos apenas as regiões que mudam.

---

## 4. Decisão: cartão microSD é necessário?

**Resposta curta:** **Não é obrigatório para o MVP** (tema "clawd", estados
essenciais em **LittleFS na flash**). O SD passa a ser **recomendado** para a
biblioteca completa / multi-tema, para assets atualizáveis sem reflash e como
meio prático de **provisionamento por arquivo de config**.

### Análise
- Os assets curados do MVP (~0,6–1,2 MB) **cabem** numa partição LittleFS de 4 MB
  de flash — desde que se use uma tabela de partição customizada (ex.: app ~1,4 MB
  + LittleFS ~2,2 MB, **sem** segundo slot OTA). Ver §7.
- O SD do CYD fica em **barramento SPI separado** do TFT, então **coexiste** sem
  conflito. Leitura SPI do SD é mais lenta que flash, mas — pelo padrão
  "carregar-para-RAM" da §3 — isso **não afeta o FPS**.

### Prós e contras

| Critério | Flash (LittleFS/SPIFFS) | Cartão microSD |
|---|---|---|
| Capacidade p/ assets | ~1,5–2,5 MB (compete com app/OTA) | GBs — biblioteca inteira, multi-tema |
| Velocidade de leitura | Rápida (memória mapeada/QSPI) | Mais lenta (SPI), mas irrelevante c/ cache em RAM |
| Atualizar assets | Requer reflash/upload de imagem FS | Trocar arquivos no cartão (sem reflash) |
| Confiabilidade | Alta (sem partes removíveis) | Cartão pode faltar/corromper → precisa de fallback |
| Custo/complexidade | Zero extra | Precisa montar FS, tratar ausência do cartão |
| OTA firmware | Difícil coexistir (2 slots ~ não cabem c/ assets grandes) | Libera a flash p/ 2 slots OTA | 
| Provisionamento | Via NVS/portal/serial | **`clawd.json` no cartão** (ssid/senha/host/port/token) — muito prático |

### Recomendação
1. **MVP:** assets essenciais em **LittleFS (flash)**; **SD opcional apenas para
   `clawd.json`** de provisionamento (Wi-Fi + host/port/token).
2. **Projete o módulo de armazenamento atrás de uma interface** (`AssetStore`)
   com dois back-ends intercambiáveis (`FlashAssetStore`, `SdAssetStore`), de
   modo que "biblioteca completa / multi-tema / assets atualizáveis" seja um
   *drop-in* no futuro, sem refatorar o renderizador.
3. Há **necessidade de carregamento dinâmico?** Não em runtime além da troca de
   animação por estado. Se um dia os assets forem baixados via rede/OTA, o SD
   vira o destino natural.

> **Multi-tema sem PSRAM.** Como esta placa **não tem PSRAM** (§1), não há cache
> grande em RAM: para a biblioteca completa/multi-tema (~5–10 MB) os **arquivos**
> ficam no **SD** e carregamos **uma animação por vez** para o heap na troca de
> estado/tema (padrão da §3). Funciona bem; só não há a troca "instantânea" que
> um cache em PSRAM daria.

---

## 5. Arquitetura do firmware (módulos)

```text
┌───────────────────────────────────────────────────────────────────┐
│                              main / app                            │
│   loop principal: bombeia rede, decide estado, avança animação      │
└───────────────────────────────────────────────────────────────────┘
        │            │              │               │            │
        ▼            ▼              ▼               ▼            ▼
┌────────────┐ ┌───────────┐ ┌────────────┐ ┌────────────┐ ┌──────────┐
│  NetLink   │ │ Protocol  │ │   State    │ │ Animation  │ │  Config  │
│ Wi-Fi + WS │ │  Parser   │ │  Manager   │ │  Manager   │ │  (NVS/   │
│  cliente   │ │(ArduinoJson)│ (Map+prio) │ │(estado→gif)│ │  SD)     │
└─────┬──────┘ └─────┬─────┘ └─────┬──────┘ └─────┬──────┘ └────┬─────┘
      │              │             │              │             │
      │ frames JSON  │ eventos     │ estado       │ pede bytes  │ token/
      ▼              ▼ tipados     ▼ dominante     ▼ do asset    ▼ wifi
                                          ┌────────────────┐  ┌──────────┐
                                          │   AssetStore   │  │ Renderer │
                                          │ Flash | SD     │─►│ GIF→TFT  │
                                          └────────────────┘  │(TFT_eSPI)│
                                                              └──────────┘
```

| Módulo | Responsabilidade | Biblioteca sugerida |
|---|---|---|
| **NetLink (Comunicação)** | Wi-Fi STA; cliente WebSocket p/ `/ws?token=`; ping/pong; reconexão c/ backoff; trata close codes | `links2004/arduinoWebSockets`, `WiFi.h` |
| **Protocol Parser** | Desserializa envelope v1; extrai `snapshot`/`state`/`session_deleted`/`token_rotate`; ignora desconhecidos; usa *filter* p/ ler só `sessionId`+`state` | `bblanchon/ArduinoJson` |
| **State Manager** | `Map<sessionId,state>`; aplica snapshot/upsert/delete; calcula **estado dominante** por `STATE_PRIORITY`; debounce de one-shots; (opcional) contagem p/ tiers | próprio |
| **Animation Manager** | Mapa estado→arquivo; orquestra troca de animação; controla timing/loop; min-display | próprio |
| **AssetStore** | Interface p/ obter bytes do GIF por nome; back-ends Flash (LittleFS) e SD | `LittleFS`, `SD`/`SdFat` |
| **Renderer (Driver da tela)** | Inicializa TFT; decodifica GIF→desenha; *letterbox*/escala; barra de status (conexão/estado) | `bodmer/TFT_eSPI`, `bitbank2/AnimatedGIF` |
| **Config** | Persiste Wi-Fi + host/port/token; **persiste token rotacionado**; carrega de NVS ou `clawd.json` no SD | `Preferences` (NVS) |

### Modelo de execução (dual-core)
- **Core 0:** rede (Wi-Fi + WebSocket + parser). Mantém o link vivo e atualiza o
  State Manager (com mutex no mapa).
- **Core 1:** loop de render (decode GIF → TFT), lendo o "estado dominante atual".
- Comunicação entre cores por variável compartilhada protegida (o estado
  dominante é um enum pequeno; troca é barata). Evita que o decode bloqueie o
  heartbeat da rede.

---

## 6. Pipeline de assets (ferramenta no host — a construir)

Um script (Node ou Python + `gifsicle`/`ImageMagick`) que:
1. Seleciona os GIFs de estado do tema clawd (`assets/gif/clawd-*.gif`).
2. Redimensiona 302×300 → alvo (ex.: **220×220** para caber sob a barra de status
   num display 240×320), preservando pixel-art (sem suavização agressiva).
3. Requantiza paleta (32–64 cores) e otimiza (`gifsicle -O3 --colors 64`).
4. Renomeia por **estado** (`idle.gif`, `thinking.gif`, …) para o mapa do firmware.
5. Emite para `data/` (imagem LittleFS via `pio run -t uploadfs`) **ou** para o SD.

Este é código **novo** (tooling), não reaproveitado do projeto de origem.

---

## 7. Configuração PlatformIO proposta (rascunho — não aplicado)

> Apenas **proposta de design**; nada foi escrito no `platformio.ini` ainda.

```ini
[env:esp32-2432S028]
platform = espressif32
board = esp32dev
framework = arduino
board_build.partitions = huge_app.csv   ; Fase 3: trocar por tabela custom (app + LittleFS ~2MB, sem OTA)
board_build.filesystem = littlefs
lib_deps =
    bodmer/TFT_eSPI
    bitbank2/AnimatedGIF
    links2004/WebSockets
    bblanchon/ArduinoJson
build_flags =
    ; (Sem flags de PSRAM — placa é WROOM, confirmado no bring-up.)
    ; --- TFT_eSPI p/ CYD (ILI9341, SPI) ---
    -D USER_SETUP_LOADED=1
    -D ILI9341_2_DRIVER=1
    -D TFT_WIDTH=240
    -D TFT_HEIGHT=320
    -D TFT_MISO=12
    -D TFT_MOSI=13
    -D TFT_SCLK=14
    -D TFT_CS=15
    -D TFT_DC=2
    -D TFT_RST=-1
    -D TFT_BL=21
    -D TFT_BACKLIGHT_ON=HIGH
    -D TFT_RGB_ORDER=TFT_BGR                ; confirmado nesta unidade
    -D TFT_INVERSION_ON                     ; confirmado necessário (senão cores em negativo)
    -D SPI_FREQUENCY=40000000
```

> ✅ A config acima foi **validada no hardware** (Fase 1): pinos usuais do CYD
> 2432S028, `TFT_BGR` + `TFT_INVERSION_ON`. A versão real e completa está em
> [`platformio.ini`](../../platformio.ini). O SD do CYD usa VSPI separado
> (CS 5, MOSI 23, MISO 19, SCK 18) — a validar quando/se o SD entrar.

---

## 8. Performance — estimativas

| Recurso | Estimativa | Comentário |
|---|---|---|
| **SRAM interna (heap)** | **~297 KB livre** (boot) / ~248 KB pós-Wi-Fi — medido | Orçamento: WS+JSON (~10–20 KB) + workspace do AnimatedGIF (poucos KB) + **1 GIF ativo em RAM (~40–150 KB)** + buffers de linha do TFT. Cabe com folga. **Sem PSRAM.** |
| **Flash** | app ~0,8 MB (bring-up) → ~1,0–1,4 MB (completo) + LittleFS assets ~0,6–1,2 MB | Cabe em 4 MB **se abrir mão de 2 slots OTA**. Biblioteca completa → SD. |
| **FPS** | ~15–30 fps realista | ILI9341 @40 MHz SPI: blit de 240×240 RGB565 ≈ 23 ms (~40 fps teórico). GIFs de origem já são ~10–15 fps → suficiente e fluido. |
| **Impacto flash vs SD na leitura** | Só afeta o **instante da troca de estado** (~dezenas de ms) | Com "carregar-para-RAM" (§3), **não** afeta o FPS de reprodução. Latência de troca imperceptível. |
| **Latência de estado ponta a ponta** | até ~2 s | Limitada pelo *poll* de 2 s do servidor (não é do ESP32). |

Alavancas de otimização (Fase 5): reduzir resolução/paleta dos GIFs; usar
atualização parcial (o GIF só codifica regiões alteradas); DMA no push do TFT;
decode em Core 1 separado da rede; `SPI_FREQUENCY` até onde o painel aceitar.

---

## 9. Reuso vs. reimplementação

### Reutilizável **diretamente** (conceito/dados, não o runtime JS)
- **Semântica do protocolo v1** (envelope, tipos de mensagem, close codes) — implementar cliente equivalente.
- **Lista de estados + `STATE_PRIORITY`** (`src/state-priority.js`) — portar a tabela/algoritmo p/ C++.
- **Mapa estado → animação** (`state-mapping.md` / `theme.json` `states`) — tabela estática em C++.
- **A arte** (`assets/gif/clawd-*.gif`) — após curadoria/redimensionamento.

### Precisa ser **reimplementado** (não há reuso de código)
- Cliente WebSocket (browser API → `arduinoWebSockets`).
- Parse JSON (→ ArduinoJson, com filtro p/ economizar RAM).
- Renderizador (DOM/SVG/CSS/SMIL → `TFT_eSPI` + `AnimatedGIF`).
- Persistência/ack de rotação de token (→ NVS).
- Provisionamento (sem câmera p/ QR → `clawd.json`/serial/portal).

### **Não é necessário** portar
UI de sessões/cartões do PWA, service worker, notificações do navegador, engine
de temas/SVG, acessórios, eye-tracking, `tool_output`, operações de escrita/
aprovação, e toda a stack Electron/Node do desktop.

---

## 10. Riscos

| # | Risco | Severidade | Mitigação |
|---|---|---|---|
| 1 | **Sem PSRAM** (confirmado: WROOM, ~297 KB heap) → pressão de RAM | Média (gerenciável) | Assets curados/reduzidos; **1 animação em RAM por vez**; desenho direto (sem framebuffer); buffer do GIF ativo ≤ ~150 KB. |
| 2 | **4 MB de flash** apertam app+assets+OTA | Média | Partição custom (sem 2º slot OTA no MVP); ou mover assets p/ SD. |
| 3 | **Lockout por rotação de token** após reboot (grace de 5 min) | Média | Persistir `newToken` em NVS a cada `token_rotate` + enviar ack. |
| 4 | **Variância de hardware** do CYD (ILI9341×ST7789, RGB/inversão, pinos, SD) | Média | Fase 1 de bring-up dedicada; confirmar controlador/pinos antes do resto. |
| 5 | **Descoberta de porta/token** (porta 23334–23338, token só via QR) | Média | Config manual em `clawd.json`/serial; usar `/api/connection-info` p/ porta; sem câmera, token digitado/gravado. |
| 6 | **Performance de decode** em frames complexos | Baixa | Downscale + paleta reduzida + updates parciais + DMA. |
| 7 | **Só LAN, sem TLS** | Baixa (aceito por design) | Usar apenas em rede confiável; espelha o modelo do PWA. |
| 8 | **Aspecto** 240×320 vs arte quadrada | Baixa | *Letterbox* 240×240 + barra de status 240×80. |
| 9 | **Heartbeat/timeout** (90 s) se a lib não responder ping | Baixa | Confirmar auto-pong; watchdog de reconexão. |
| 10 | **Latência de 2 s** do servidor | Baixa (aceito) | Nada a fazer no ESP32; documentar expectativa. |

---

## 11. Plano de implementação (roadmap por fases)

### Fase 0 — Preparação
- Confirmar variante exata do CYD: controlador (ILI9341/ST7789), **PSRAM sim/não**,
  pinos do TFT/SD/backlight.
- Fechar escopo do MVP: **tema clawd, estados essenciais, assets em flash**.
- Definir `platformio.ini` (flags TFT_eSPI + partição + `lib_deps`) — §7.

### Fase 1 — Estudo do protocolo + bring-up ✅ **CONCLUÍDA**
- Protocolo v1 documentado em [01-PROTOCOL.md](01-PROTOCOL.md).
- **Bring-up validado no hardware** ([`src/main.cpp`](../../src/main.cpp)): display
  240×320 (BGR + inversão), **sem PSRAM** (~297 KB heap), Wi-Fi OK. Ver callout §1.

### Fase 2 — Comunicação ✅ **CONCLUÍDA** (validada no hardware)
- Wi-Fi STA + `arduinoWebSockets` conectando em `ws://host:port/ws?token=`.
- Parser (ArduinoJson, com filtro) de `snapshot`/`state`/`session_deleted`/`token_rotate`.
- `SessionStore` + estado dominante; token em NVS + ack; heurística de auth.
- **Aceite atingido:** `[net] snapshot` + transições `state -> THINKING/WORKING`
  reais do desktop no Serial e na tela.
- ⚠️ **Gotcha resolvido:** `setReconnectInterval()` grande **bloqueia a 1ª conexão**
  no `arduinoWebSockets` (o `loop()` compara `millis()-_lastConnectionFail`, que
  começa em 0, contra o intervalo). Usar valor pequeno (3 s) e deixar a lib
  reconectar. Desligar `WiFi.setSleep(false)` p/ TCP estável.

### Fase 3 + 4 — Renderização + animações ⏳ **IMPLEMENTADA** (aguardando validação)
- **Formato próprio `CRLI`** (paleta indexada + RLE de índices, RGB565) em vez de
  GIF: o GIF exigia lib de terceiros e teve atrito repetido (ver gotchas). O CRLI
  é gerado no host e decodificado por nós — controle total, ~40 linhas, sem lib.
- Pipeline `tools/build_assets.py` (Pillow): 10 GIFs do clawd → 240×238,
  **todos os frames** (45–90 por estado) com a **duração original de cada frame**,
  paleta 32 cores → `data/*.crli` (~2,08 MB).
- Decode em `lib/ClawdDisplay/AnimationManager.cpp`: **streaming do LittleFS**
  (buffer de runs ~5 KB), `tft.pushImage` linha a linha, timing por frame (campo
  `delayMs` do CRLI); fallback (bloco colorido) se o asset faltar.
- Partição custom `partitions.csv` (~1,31 MB app + **~2,62 MB LittleFS**, sem OTA)
  — dimensionada para caber todos os frames.
- Barra de status (238..320): link + estado + sessões/heap/IP.
- **Aceite:** o mascote na tela deve acompanhar o estado do desktop em ~2 s.

### Fase 4 — Sistema de animações (integração)
- State Manager: mapa de sessões + **estado dominante** (`STATE_PRIORITY`).
- Animation Manager: troca de GIF na mudança de estado; **debounce de one-shots**.
- Padrão "carregar-para-RAM na troca de estado" (§3).
- (Opcional) tiers de `working`/`juggling` por contagem de sessões.
- **Critério de aceite:** animação do ESP32 acompanha o mascote do desktop em
  tempo real (dentro da latência de ~2 s).

### Fase 5 — Otimizações
- Rotação de token persistida em NVS + ack (fechar o risco #3).
- Decode em Core 1 separado da rede (Core 0); mutex do estado.
- Ajuste de resolução/paleta/`SPI_FREQUENCY`; updates parciais; DMA.
- Medir heap livre, FPS e latência de troca; ajustar buffers.

### Fase 6 — Testes e robustez
- Reconexão (derrubar Wi-Fi, matar desktop, esgotar 10 clientes → 1013).
- Ciclo de rotação de token (forçar rotação; reboot pós-rotação).
- Estresse: muitas sessões, trocas rápidas de estado (debounce), sessão deletada.
- Ausência de SD (fallback), cartão corrompido, `clawd.json` inválido.
- Estabilidade de longa duração (24 h+ atravessando ≥1 rotação).
- (Opcional) `AssetStore` no SD para multi-tema; LED RGB/som por estado.

---

## 12. Decisões em aberto (para o dono do projeto)

1. **Escopo visual:** só o tema **clawd** (recomendado p/ MVP) ou multi-tema
   (clawd+calico+cloudling)? Multi-tema empurra os assets para o **SD**.
2. ~~Variante do CYD com PSRAM?~~ **Resolvido no bring-up: WROOM sem PSRAM**
   (ESP32-D0WD-V3, ~297 KB heap). Segue o plano sem PSRAM — viável. Opção aberta:
   trocar por uma placa com PSRAM no futuro (upgrade, não bloqueia o MVP).
3. ~~Provisionamento?~~ **Resolvido: portal captive Wi-Fi** (SoftAP + web server),
   config em NVS. Ver §13.
4. **Uso do SD já no MVP:** só para `clawd.json`, ou também para os assets desde
   o início (deixando a flash livre para OTA)?
5. **Extras de hardware:** usar LED RGB e/ou alto-falante como reforço de estado
   (ex.: som/`notification`)?

---

## 13. Provisionamento e configuração (zero-touch) ✅ IMPLEMENTADO

Elimina a necessidade de `secrets.h`: o usuário configura tudo pelo celular.

**Armazenamento (`lib/ClawdConfig`):** config em **NVS** como JSON (sobrevive a
`upload`/`uploadfs`): `networks[]` (ssid/pass/priority), `hosts[]` (host/port),
`token`, `adminHash` (SHA-256 via mbedTLS), `provisioned`.

**Fluxo (`src/main.cpp` + `lib/ClawdPortal/WiFiConnection`):**
```
load config → conectar à rede conhecida de MAIOR prioridade visível
 ├─ conectou → STA: dashboard (auth admin) + NetLink (WS) + mascote;
 │             tela "Conectado" com QR da URL do painel por ~8 s
 └─ nenhuma  → SoftAP "Clawd-Setup-XXXX" (aberto) + DNS captive;
               tela alterna mascote "aguardando" e QR (join + IP do portal)
```

**Portal (`lib/ClawdPortal/WebPortal`, `WebServer`+`DNSServer` nativos, HTML em
PROGMEM):**
- **Setup (AP):** formulário (rede, host/porta/token, senha de admin) → salva → reboot.
- **Dashboard (STA):** protegido por **HTTP Basic** contra o hash de admin; permite
  gerenciar múltiplas redes com prioridade (casa/trabalho sem reset), a lista de
  hosts do clawd-on-desk (failover se o IP muda), o token e a **aparência**
  (tamanho do mascote e brilho, aplicados ao vivo via `onConfigChanged`).

**Tela (`lib/ClawdDisplay/InfoScreen`):** QR (via `ricmoo/QRCode`) + texto, tema
Clawd. QR de join Wi-Fi no AP; QR da URL do painel no STA.

**`secrets.h`** vira **seed opcional**: se presente com valores reais, semeia a
config no 1º boot; caso contrário, o portal cuida de tudo. `-t erase` limpa o NVS
para reprovisionar do zero.

**Prioridades técnicas confirmadas:** páginas embutidas no firmware (funcionam sem
`uploadfs`); AP aberto (o QR já facilita o join); web stack nativo (sem AsyncTCP).

---

## 14. Dashboard v2, telemetria, OTA e assets no SD ✅ IMPLEMENTADO

**Dashboard SPA (`lib/ClawdPortal/WebPortal`):** página única em PROGMEM, responsiva,
**tema claro/escuro automático** (`prefers-color-scheme`; terminal sempre escuro).
Renderiza a partir de uma API JSON:
- `GET /api/state` → `{config, status, info}` (redes/hosts/token/aparência + estado +
  telemetria + flags do build). `main` injeta o `status` via `portal.statusProvider`.
- `GET /api/log?since=N` → linhas novas do ring buffer (`ClawdConsole`) para o terminal.
- `POST /api/{net,host,token,display,admin,reboot,reset}` → mutações (auth Basic).

**Telemetria:** heap, temperatura (`temperatureRead()`), uptime, flash e **loops/s**
(proxy honesto — "CPU %" real exigiria stats do FreeRTOS). Sparkline de heap no canvas.

**Terminal serial ao vivo:** `ClawdConsole` (`console.logf/log`) espelha o `Serial`
num ring buffer de ~48 linhas; o dashboard faz polling de `/api/log`.

**OTA de firmware (build `-ota`):** `POST /update` (lib `Update`) grava no slot de app
livre e reinicia. Exige `partitions_ota.csv` (2 slots de app) — por isso é um **modo
de build** (`-D CLAWD_ENABLE_OTA`), não o padrão.

**Assets no SD (build `-ota`):** com 2 slots de app na flash de 4 MB não há espaço p/
os ~2 MB de assets, então eles vão para o **cartão SD** (VSPI: CS 5, MOSI 23, MISO 19,
SCK 18). O `AnimationManager` lê de um `fs::FS` genérico (LittleFS **ou** SD). O
dashboard (aba Armazenamento) lista/apaga arquivos e **sincroniza os `.crli` do
GitHub** (`raw.githubusercontent.com/.../data/`) via `HTTPClient`+`WiFiClientSecure`,
reiniciando ao final. No build padrão os assets seguem em LittleFS (`uploadfs`).
