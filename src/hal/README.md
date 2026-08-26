# `src/hal/` — Hardware Abstraction Layer (adaptadores)

Aqui vive **todo** o código que fala com hardware de verdade: `Arduino.h`,
`BluetoothSerial`, `Preferences`, drivers de display.

## Por que separado de `lib/`?

Regra de ouro do Kanri:

> Nada em `lib/` inclui `Arduino.h`.

Isso é o que permite rodar `pio test -e native` e ver os testes passarem no
PC em segundos, sem ESP32 conectado. `Arduino.h` não existe no PC — se a
lógica dependesse dele, não haveria como testá-la.

A divisão é o padrão **Ports & Adapters** (também chamado de arquitetura
hexagonal):

| Peça | Onde fica | O que é |
|------|-----------|---------|
| **Porta** (interface) | `lib/<modulo>/include/` | A promessa: `ITransport`, `IDisplay`, `IClock`, `IConfigStore` |
| **Adaptador real** | `src/hal/` | Cumpre a promessa usando hardware |
| **Adaptador de teste** | `test/helpers/` | Cumpre a promessa usando RAM |

A lógica depende **só da porta**. Trocar OLED por TFT, ou Bluetooth Classic
por BLE, mexe apenas no adaptador.

## Estado atual (v0.1)

| Arquivo | O que faz hoje |
|---------|----------------|
| `arduino_clock.h` | Real. `millis()` do Arduino. |
| `serial_display.h/.cpp` | Real. "Desenha" no monitor serial — dá para desenvolver antes do display físico chegar. |
| `null_transport.h` | Placeholder honesto: sempre falha ao conectar. Faz o firmware exercitar o caminho degradado desde já. |
| `nvs_config_store.h/.cpp` | Esqueleto. Sempre devolve os padrões de fábrica. |

## O que entra depois

- **v0.2** — `bt_serial_transport.h/.cpp` (Bluetooth Classic SPP) e a
  implementação real do `nvs_config_store` com a lib `Preferences`.
  Ao incluir `BluetoothSerial`, o binário cresce muito: será preciso
  adicionar `board_build.partitions = huge_app.csv` no `platformio.ini`.
- **v0.3** — `ssd1306_display.h/.cpp` (OLED 128x64 por I²C).
