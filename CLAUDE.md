# CLAUDE.md

Guia para agentes trabalhando neste repositório. Mantenha-o curto e atual.

## O que é

Firmware para um **ESP32-2432S028 ("CYD")** que atua como **cliente de exibição**
do mascote Clawd. Conecta-se pela LAN ao app [clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk)
via **Mobile Protocol v1** (WebSocket, somente leitura), calcula o estado
dominante das sessões do Claude e reproduz a animação correspondente no TFT.

Documentos de referência: [`docs/clawd-esp32/01-PROTOCOL.md`](docs/clawd-esp32/01-PROTOCOL.md)
e [`docs/clawd-esp32/02-ARCHITECTURE.md`](docs/clawd-esp32/02-ARCHITECTURE.md).

## Build, flash e monitor

Ambiente PlatformIO (o binário pode estar em `~/.platformio/penv/bin/pio`):

```bash
pio run -e esp32-2432S028                 # compila o firmware
pio run -e esp32-2432S028 -t uploadfs     # grava os assets em LittleFS (só quando data/ muda)
pio run -e esp32-2432S028 -t upload       # grava o firmware
pio device monitor                        # 115200 baud
```

`esp32-2432S028` é o ambiente-alvo (`default_envs`). `esp32dev` é só um placeholder.

Antes do primeiro build: `cp include/secrets.example.h include/secrets.h` e
preencher Wi-Fi + `CLAWD_HOST/PORT/TOKEN` (o token vem do desktop em
Settings → Mobile pairing). `include/secrets.h` é gitignored — nunca commitar.

## Arquitetura (módulos em `lib/`, bibliotecas PlatformIO)

- **`ClawdCore`** — domínio puro, sem hardware: `ClawdState.h` (enum, `STATE_PRIORITY`,
  cores) e `SessionStore.h` (mapa de sessões + estado dominante).
- **`ClawdConfig`** — `ConfigStore.h`: persiste o token rotacionado em NVS.
- **`ClawdNet`** — `NetLink.{h,cpp}`: Wi-Fi + WebSocket (`links2004/WebSockets`) +
  parser do protocolo (`ArduinoJson`, com filtro). Depende de `ClawdCore`.
- **`ClawdDisplay`** — `AnimationManager.{h,cpp}`: TFT (`TFT_eSPI`) + decodificador
  do formato próprio **CRLI**. Depende de `ClawdCore`.
- **`src/main.cpp`** — composition root: liga rede → estado → tela. Não colocar
  lógica de domínio aqui.

## Assets (formato CRLI)

Animações são geradas por [`tools/build_assets.py`](tools/build_assets.py) (Pillow)
a partir dos GIFs do clawd-on-desk, no formato **CRLI** (paleta indexada + RLE de
índices RGB565, com `delayMs` por frame). Decodificado por streaming do LittleFS
em `AnimationManager.cpp` (sem lib de GIF). Regenerar:

```bash
python3 tools/build_assets.py <clawd-on-desk>/assets/gif ./data
```

## Convenções e armadilhas (não repetir)

- **Sem PSRAM** na placa (ESP32-D0WD-V3): manter RAM enxuta; nada de framebuffer
  inteiro; animações decodificadas por streaming.
- **Display:** requer `TFT_BGR` + `TFT_INVERSION_ON` (senão cores em negativo).
- **WebSocket (`arduinoWebSockets`):** NÃO usar `setReconnectInterval` grande — o
  `loop()` compara `millis()-_lastConnectionFail` (que inicia em 0) contra o
  intervalo e bloqueia a 1ª conexão. Usar ~3 s. Ligar `WiFi.setSleep(false)`.
- **Token:** ao receber `token_rotate`, persistir em NVS e enviar `token_rotate_ack`
  (grace de só 5 min no servidor).
- **Partição:** `partitions.csv` custom (~1,31 MB app + ~2,62 MB LittleFS, sem OTA)
  para caber todos os frames. Mudar a partição exige re-`uploadfs` e `upload`.
- Não é preciso rodar `uploadfs` a cada `upload` — só quando `data/` muda.

## Estilo

C++ Arduino. Um módulo = uma responsabilidade. Headers junto das implementações,
por biblioteca. Comentários curtos explicando o "porquê", não o "o quê".
