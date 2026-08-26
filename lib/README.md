# `lib/` — a lógica do Kanri

Cada subpasta aqui é uma biblioteca local do PlatformIO. O que as une é uma
regra só:

> **Nenhum arquivo em `lib/` inclui `Arduino.h`.**

É isso que permite rodar `pio test -e native` e ver 98 testes passarem no PC
em ~1,4 s, sem ESP32 conectado. `Arduino.h` não existe no PC — se a lógica
dependesse dele, não haveria como testá-la.

Quando a lógica precisa de hardware, ela declara uma **interface** (uma
"porta") e recebe a implementação de fora:

| Porta | Implementação real (`src/hal/`) | Dublê de teste (`test/helpers/`) |
|-------|--------------------------------|----------------------------------|
| `IClock` | `ArduinoClock` → `millis()` | `FakeClock` → tempo controlado pelo teste |
| `ITransport` | `NullTransport` (v0.1) → Bluetooth na v0.2 | `FakeTransport` → bytes enfileirados na mão |
| `IDisplay` | `SerialDisplay` → monitor serial | `FakeDisplay` → guarda o último frame |
| `IConfigStore` | `NvsConfigStore` → flash | `FakeConfigStore` → struct em RAM |

## Os módulos

| Módulo | Responsabilidade | Depende de |
|--------|------------------|------------|
| **`kanri_core`** | Máquina de estados, backoff exponencial, telemetria, porta de tempo, versão | — (é a base) |
| **`kanri_obd`** | Parser ELM327, portão read-only, catálogo de PIDs, porta de transporte | `kanri_core` |
| **`kanri_config`** | Struct de configuração, padrões, validação, porta de persistência | — |
| **`kanri_display`** | View model (o *que* mostrar), porta de display | `kanri_core` |

O gráfico de dependências é **acíclico** de propósito: `kanri_core` é a base e
não conhece ninguém. A "cola" que junta tudo — e que decide quais adaptadores
concretos usar — fica em `src/main.cpp`, porque escolher adaptador **é** uma
decisão de hardware.

Detalhes em [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).

## Adicionando um módulo

1. `lib/kanri_novo/library.json` — com `"frameworks": "*"` e `"platforms": "*"`,
   para que o ambiente `native` também compile.
2. `lib/kanri_novo/include/kanri_novo/*.h` — headers, com um bloco de
   comentário explicando **por que o módulo existe**.
3. `lib/kanri_novo/src/*.cpp` — implementação pura.
4. Acrescente o nome em `lib_deps_local`, no `platformio.ini`.
5. Escreva os testes em `test/test_novo/`.
