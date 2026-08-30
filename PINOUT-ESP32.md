# ESP32 DevKit V1 (30 pinos) — mapa de pinos

Referência de bancada do Kanri. Placa confirmada pela serigrafia:
**DevKit V1 de 30 pinos**, módulo ESP32-WROOM-32.

---

## As duas fileiras, como vêm impressas

```
 3V3  GND  D15  D2   D4   RX2  TX2  D5   D18  D19  D21  RX0  TX0  D22  D23
 VIN  GND  D13  D12  D14  D27  D26  D25  D33  D32  D35  D34  VN   VP   EN
```

---

## 🔑 Alocação atual do Kanri

| GPIO | Função | Onde fica |
|---|---|---|
| **2** | LED de status (azul, da placa) | cima |
| **5** | `LOAD` do MAX7219 | cima |
| **17** | Botão (troca a medida) — `INPUT_PULLUP` | cima (`TX2`) |
| **18** | `CLK` do MAX7219 | cima |
| **23** | `DIN` do MAX7219 | cima |
| **36** | Potenciômetro de brilho (`ADC1`) | baixo (`VP`) |

### Livres

```
4 · 13 · 14 · 16 · 19 · 21 · 22 · 25 · 26 · 27 · 32 · 33
```

Doze pinos de saída plena. Testar com `gpio 22 1` / `gpio 22 0`.

---

## 🔴 Proibidos — por motivo físico, não por preferência

| Pino | GPIO | Por quê |
|---|---|---|
| — | **20, 24, 28, 29, 30, 31** | **Não existem.** São buracos na numeração do ESP32 |
| `D34`, `D35` | 34, 35 | ***Input-only*** — não têm driver de saída |
| `VP`, `VN` | 36, 39 | ***Input-only***, mesmo motivo |
| `RX0`, `TX0` | 3, 1 | Console USB — derruba o painel Kanri e a gravação |
| — | 6 – 11 | Flash SPI — acionar **trava o chip na hora** |
| `D12` | 12 | *Strapping* `MTDI`: **HIGH no boot e a placa não inicia** |

> ### Por que isso importa mais do que parece
>
> Todas essas falham **em silêncio**:
>
> - `digitalWrite(28, HIGH)` **compila sem aviso e não faz nada**
> - `pinMode(34, OUTPUT)` **compila, roda, não dá erro** — e o pino nunca muda
>
> Você vai medir um pino procurando defeito na fiação que não está lá. Por isso
> o comando `gpio` **recusa e explica** em vez de aceitar calado.

---

## 🟡 Com ressalva

| Pino | GPIO | Ressalva |
|---|---|---|
| `D15` | 15 | *Strapping* `MTDO`: LOW no boot silencia o log — você perde diagnóstico |
| `D5` | 5 | *Strapping* com *pull-up* interno: um LED aqui pisca durante o boot |
| `D2` | 2 | Já é o LED de status (e é *strapping*) |

---

## ADC — qual conversor usar

| | Pinos | Serve? |
|---|---|---|
| **ADC1** | 32, 33, 34, 35, `VP` (36), `VN` (39) | ✅ **use este** |
| **ADC2** | 0, 2, 4, 12–15, 25–27 | ❌ **não funciona com o rádio ativo** |

O Kanri usa Bluetooth o tempo todo. Um sensor no `ADC2` **funcionaria
perfeitamente na bancada e pararia de responder no instante em que o adaptador
conectasse** — é o tipo de defeito que consome uma tarde inteira.

Repare na ironia útil: `34`, `35`, `36` e `39` são *input-only* e por isso
inúteis para acionar coisa — mas são **perfeitos para sensores**, que são
entrada por natureza. Foi por aí que o potenciômetro foi para o `36`.

---

## Corrente

| | |
|---|---|
| Por GPIO, recomendado | **12 mA** |
| Por GPIO, máximo absoluto | **40 mA** |

LED direto no pino pede **220 Ω a 1 kΩ** em série.

> Exceder o máximo **não queima na hora — degrada**. Funciona na bancada,
> funciona uma semana no carro, e depois o pino morre sem explicação.

---

## Comandos do console

```
gpio <pino> <0|1>   aciona um pino livre (recusa os ocupados, dizendo o motivo)
leds 22,21,19       define a barra de LEDs (vírgula ou espaço)
piscar              liga/desliga o piscar da barra
pot                 leitura crua do ADC do potenciômetro
teste               autoteste do mostrador, emendando a varredura da barra
```

Ver também [`PINOUT-MAX7219.md`](PINOUT-MAX7219.md) e
[`docs/HARDWARE.md`](docs/HARDWARE.md).
