# CLAUDE.md

Guia para agentes trabalhando neste repositório. Mantenha-o curto e atual.

## O que é

Firmware para um **ESP32-2432S028 ("CYD")** que atua como **cliente de exibição**
do mascote Clawd. Conecta-se pela LAN ao app [clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk)
via **Mobile Protocol v1** (WebSocket, somente leitura), calcula o estado
dominante das sessões do Claude e reproduz a animação correspondente no TFT.

Setup é **zero-touch**: no 1º boot sem rede configurada, o dispositivo sobe um
SoftAP com portal captive (UI temática) para o usuário configurar tudo pelo
celular. Não precisa de `secrets.h`.

Documentos de referência: [`docs/clawd-esp32/01-PROTOCOL.md`](docs/clawd-esp32/01-PROTOCOL.md)
e [`docs/clawd-esp32/02-ARCHITECTURE.md`](docs/clawd-esp32/02-ARCHITECTURE.md).

## Build, flash e monitor

Ambiente PlatformIO (o binário pode estar em `~/.platformio/penv/bin/pio`):

```bash
pio run -e esp32-2432S028                 # compila o firmware
pio run -e esp32-2432S028 -t uploadfs     # grava os assets em LittleFS (só quando data/ muda)
pio run -e esp32-2432S028 -t upload       # grava o firmware
pio device monitor                        # 115200 baud
pio run -e esp32-2432S028 -t erase        # apaga a flash (força provisionamento do zero)
```

`esp32-2432S028` é o ambiente-alvo (`default_envs`). `esp32dev` é só um placeholder.

`include/secrets.h` é **opcional** (gitignored). Se presente com valores reais,
ele apenas **semeia** a config no 1º boot (conveniência de dev). Sem ele, a
configuração é feita pelo portal. Nunca commitar `secrets.h`.

## Fluxo de boot

```
ConfigStore::load() (NVS) → WiFiConnection.begin()
  ├─ rede conhecida de maior prioridade visível → STA
  │     → WebPortal dashboard (auth admin) + NetLink (WebSocket) + mascote
  └─ nenhuma rede / falha → SoftAP "Clawd-Setup-XXXX"
        → WebPortal setup captive + tela alternando mascote e QR (join + IP)
```

## Arquitetura (módulos em `lib/`, bibliotecas PlatformIO)

- **`ClawdCore`** — domínio puro, sem hardware: `ClawdState.h` (enum, `STATE_PRIORITY`,
  cores) e `SessionStore.h` (mapa de sessões + estado dominante).
- **`ClawdConfig`** — `Config.h` + `ConfigStore.{h,cpp}`: config completo em **NVS**
  como JSON (redes Wi-Fi com prioridade, hosts do clawd-on-desk, token, hash
  SHA-256 da senha de admin). Sobrevive a `upload`/`uploadfs`.
- **`ClawdNet`** — `NetLink.{h,cpp}`: Wi-Fi + WebSocket (`links2004/WebSockets`) +
  parser do protocolo (`ArduinoJson`, com filtro). Depende de `ClawdCore`.
- **`ClawdPortal`** — `WiFiConnection.{h,cpp}` (conexão por prioridade / SoftAP) e
  `WebPortal.{h,cpp}` (portal captive + dashboard, HTML embutido, auth admin via
  Basic + hash). Usa `WebServer`+`DNSServer` nativos. Depende de `ClawdConfig`.
- **`ClawdDisplay`** — `AnimationManager.{h,cpp}` (TFT + decodificador **CRLI**) e
  `InfoScreen.{h,cpp}` (tela de QR + texto, `ricmoo/QRCode`). Depende de `ClawdCore`.
- **`src/main.cpp`** — composition root: liga config → conexão → portal → tela.
  Não colocar lógica de domínio aqui.

## Assets (formato CRLI)

Animações são geradas por [`tools/build_assets.py`](tools/build_assets.py) (Pillow)
a partir dos GIFs do clawd-on-desk, no formato **CRLI** (paleta indexada + RLE de
índices RGB565, com `delayMs` por frame). Decodificado por streaming do LittleFS
em `AnimationManager.cpp` (sem lib de GIF). Regenerar (todos os frames):

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
- **Config:** fica em **NVS** (não em LittleFS) para sobreviver a re-flash. Senha de
  admin nunca em texto puro — só o hash SHA-256 (mbedTLS).
- **Pipeline de frames:** extrair cada frame como cópia independente; NÃO usar
  `list(ImageSequence.Iterator(im))` (retorna refs ao mesmo objeto → frames iguais).
- **Partição:** `partitions.csv` custom (~1,31 MB app + ~2,62 MB LittleFS, sem OTA).
  Mudar a partição exige re-`uploadfs` e `upload`.
- Não é preciso rodar `uploadfs` a cada `upload` — só quando `data/` muda.

## Estilo

C++ Arduino. Um módulo = uma responsabilidade. Headers junto das implementações,
por biblioteca. Comentários curtos explicando o "porquê", não o "o quê".
