# clawd-on-esp

Cliente de exibição do mascote **Clawd** para um ESP32 com display TFT
(Cheap Yellow Display — ESP32-2432S028). O dispositivo conecta-se, pela LAN, ao
app **clawd-on-desk** usando o **Mobile Protocol v1** (WebSocket, somente
leitura), acompanha o estado das sessões do Claude e reproduz a animação do
mascote correspondente ao estado dominante.

> Análise completa em [docs/clawd-esp32/](docs/clawd-esp32/):
> [01-PROTOCOL.md](docs/clawd-esp32/01-PROTOCOL.md) (protocolo) e
> [02-ARCHITECTURE.md](docs/clawd-esp32/02-ARCHITECTURE.md) (arquitetura + plano).

## Hardware

- **Placa:** ESP32-2432S028 ("CYD") — ESP32-WROOM (D0WD-V3, **sem PSRAM**), 4 MB flash.
- **Display:** ILI9341 240×320 SPI (config `TFT_BGR` + `TFT_INVERSION_ON`).

## Estrutura do projeto

```
clawd-on-esp/
├── platformio.ini          # ambientes, flags do TFT_eSPI, libs
├── partitions.csv          # ~1,31 MB app + ~2,62 MB LittleFS (assets), sem OTA
├── include/
│   ├── secrets.example.h   # modelo de credenciais (versionado)
│   └── secrets.h           # credenciais reais (gitignored)
├── src/
│   └── main.cpp            # composition root: liga rede -> estado -> tela
├── lib/                    # módulos por domínio (bibliotecas PlatformIO)
│   ├── ClawdCore/          #   domínio puro: estados, prioridade, SessionStore
│   ├── ClawdConfig/        #   persistência do token em NVS
│   ├── ClawdNet/           #   Wi-Fi + WebSocket + parser do protocolo v1
│   └── ClawdDisplay/       #   TFT + decodificador de animação CRLI
├── data/                   # assets *.crli (imagem do LittleFS)
├── tools/
│   └── build_assets.py     # gera data/*.crli a partir dos GIFs do clawd-on-desk
└── docs/clawd-esp32/       # documento de protocolo e de arquitetura
```

Cada pasta em `lib/` é uma biblioteca independente com uma responsabilidade
única; `main.cpp` apenas as compõe. `ClawdCore` não tem dependências de hardware.

## Configuração

Copie o modelo de credenciais e preencha (o arquivo real é ignorado pelo git):

```bash
cp include/secrets.example.h include/secrets.h
# edite WIFI_SSID, WIFI_PASSWORD e CLAWD_HOST/PORT/TOKEN
```

`CLAWD_HOST`/`PORT`/`TOKEN` vêm do desktop em **Settings -> Mobile pairing**.

## Assets

Os `data/*.crli` já estão no repositório. Para regenerá-los a partir dos GIFs
originais do [clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk):

```bash
pip install Pillow
python3 tools/build_assets.py <caminho>/clawd-on-desk/assets/gif ./data
```

## Build & flash

```bash
pio run -e esp32-2432S028 -t uploadfs   # grava os assets (LittleFS)
pio run -e esp32-2432S028 -t upload     # grava o firmware
pio device monitor                      # 115200 baud
```

`uploadfs` só precisa rodar quando os arquivos de `data/` mudam.
