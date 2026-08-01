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
│   └── secrets.h           # seed opcional de dev (gitignored)
├── src/
│   └── main.cpp            # composition root: config -> conexão -> portal -> tela
├── lib/                    # módulos por domínio (bibliotecas PlatformIO)
│   ├── ClawdCore/          #   domínio puro: estados, prioridade, SessionStore
│   ├── ClawdConfig/        #   config completo em NVS (redes, hosts, token, admin)
│   ├── ClawdNet/           #   Wi-Fi + WebSocket + parser do protocolo v1
│   ├── ClawdPortal/        #   conexão por prioridade + portal captive + dashboard
│   └── ClawdDisplay/       #   TFT + animação CRLI + tela de QR (InfoScreen)
├── data/                   # assets *.crli (imagem do LittleFS)
├── tools/
│   └── build_assets.py     # gera data/*.crli a partir dos GIFs do clawd-on-desk
└── docs/clawd-esp32/       # documento de protocolo e de arquitetura
```

Cada pasta em `lib/` é uma biblioteca independente com uma responsabilidade
única; `main.cpp` apenas as compõe. `ClawdCore` não tem dependências de hardware.

## Primeira inicialização (provisionamento)

Setup é **zero-touch** — não precisa de `secrets.h`. No 1º boot sem rede
configurada, o dispositivo:

1. Sobe um Wi-Fi **`Clawd-Setup-XXXX`** (aberto). A tela alterna o mascote
   "aguardando" com um **QR code** para entrar na rede + o IP do portal.
2. No celular, o **portal captive** (UI temática) pede: rede Wi-Fi, host/porta/
   token do clawd-on-desk e uma **senha de admin**.
3. Salva e reinicia conectando à rede. A tela mostra um QR com a URL do painel.

Depois disso, o **dashboard** em `http://<ip-do-dispositivo>/` (protegido pela
senha de admin) permite gerenciar **múltiplas redes com prioridade** (casa/
trabalho, sem reset), a **lista de hosts** do clawd-on-desk e o token.

`CLAWD_HOST`/`PORT`/`TOKEN` vêm do desktop em **Settings -> Mobile pairing**.

### secrets.h (opcional, só para dev)

Se quiser pular o portal durante o desenvolvimento, um `include/secrets.h`
(gitignored) com valores reais **semeia** a config no 1º boot:

```bash
cp include/secrets.example.h include/secrets.h
# edite WIFI_SSID, WIFI_PASSWORD e CLAWD_HOST/PORT/TOKEN
```

Para forçar o provisionamento do zero (limpar a config em NVS):

```bash
pio run -e esp32-2432S028 -t erase
```

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
