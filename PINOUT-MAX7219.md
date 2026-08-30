# MAX7219 — mapa de segmentos e registradores

Referência de bancada do Kanri. Feita para consultar com o ferro de solda na
mão, não para ler de ponta a ponta.

> Neste projeto o MAX7219 é chamado de "microcontrolador" na conversa, mas ele
> **não é**: é um *driver de LED*. Não tem CPU nem firmware — nada para
> programar nele. É um periférico de registradores: você escreve "dígito 2 = 7"
> e ele acende. Todo o código continua no ESP32.

---

## Ligação com o ESP32

| Sinal | Pino do MAX7219 | GPIO do ESP32 |
|---|---|---|
| `DIN` | 1 | **23** |
| `CLK` | 13 | **18** |
| `LOAD` | 12 | **5** |

> ⚠️ **`LOAD` e `CS` são o mesmo pino.** O 12 chama-se `LOAD` no MAX7219 e `CS`
> no MAX7221. Quem lê "DIN, LOAD e CS" como três fios **esquece o `CLK`** — e o
> sintoma é display totalmente apagado, sem erro nenhum.

> ⚠️ **Nível lógico.** O ESP32 entrega 3,3 V e o MAX7219 exige **3,5 V** de
> `VIH` quando alimentado com 5 V. Os três sinais passam por um **74HCT125**.
> O `74HC125` comum **não serve** — tem o mesmo limiar.

---

## 🔑 O mapa dos bits (o que mais se consulta)

Cada dígito é **um byte**. Cada bit é um segmento:

```
bit:   7    6    5    4    3    2    1    0
      DP    A    B    C    D    E    F    G
valor 128  64   32   16    8    4    2    1
```

```
     AAAA
    F    B
    F    B
     GGGG
    E    C
    E    C
     DDDD   DP
```

| Segmento | Bit | Valor | Comando de teste |
|---|---|---|---|
| `A` (cima) | 6 | **64** | `dig 4 64` |
| `B` (sup. dir.) | 5 | **32** | `dig 4 32` |
| `C` (inf. dir.) | 4 | **16** | `dig 4 16` |
| `D` (baixo) | 3 | **8** | `dig 4 8` |
| `E` (inf. esq.) | 2 | **4** | `dig 4 4` |
| `F` (sup. esq.) | 1 | **2** | `dig 4 2` |
| `G` (meio) | 0 | **1** | `dig 4 1` |
| `DP` (ponto) | 7 | **128** | `dig 4 128` |

**A ordem não é alfabética a partir do bit 0.** Começa no `DP` no bit mais alto
e desce `A B C D E F G`. É contraintuitiva — e é por isso que a fonte do
firmware é tabela testada, e não conta feita na cabeça.

Como é máscara, **basta somar**: `dig 4 6` acende `F + E` (2 + 4).

---

## Os números prontos

Valores que o firmware usa (`lib/kanri_display/src/max7219.cpp`):

| | Hex | Dec | | | Hex | Dec |
|---|---|---|---|---|---|---|
| `0` | `0x7E` | 126 | | `5` `S` | `0x5B` | 91 |
| `1` `I` | `0x30` | 48 | | `6` | `0x5F` | 95 |
| `2` | `0x6D` | 109 | | `7` | `0x70` | 112 |
| `3` | `0x79` | 121 | | `8` | `0x7F` | 127 |
| `4` | `0x33` | 51 | | `9` | `0x7B` | 123 |

`1`/`I` e `5`/`S` são idênticos por natureza do mostrador, não por descuido —
não há como distingui-los em 7 segmentos.

Letras disponíveis: `A b C d E F H I J L n o P r S t U y`
(`G`, `M`, `W` e outras não têm forma reconhecível.)

---

## Dígitos: 3 em uso, 5 sobrando

O chip tem **8 saídas de dígito**. O mostrador usa 3, então **5 estão livres** —
e cada uma comanda **8 LEDs** pelas próprias linhas de segmento.

| Dígito (contando de 1) | Registrador | Uso |
|---|---|---|
| 1, 2, 3 | `Digit0`–`Digit2` | mostrador |
| **4 – 8** | `Digit3`–`Digit7` | **livres — 8 LEDs cada** |

São até **40 posições de LED sem gastar um GPIO do ESP32**, e sem resistor,
porque o `ISET` limita a corrente de todos.

> 🔴 **A armadilha do `ScanLimit`.** O chip só varre até esse limite. Ligar LEDs
> no dígito 4 e não subir o `ScanLimit` resulta em **nada** — sem erro e sem
> pista. O comando `dig` sobe sozinho.
>
> 🟡 **E o preço:** o ciclo se divide entre os dígitos varridos. Passar de 3
> para 4 deixa tudo **~25 % mais fraco**. É multiplexação, não defeito.

---

## Registradores

| Registrador | Endereço | Para que serve |
|---|---|---|
| `NoOp` | `0x00` | encadear chips |
| `Digit0`–`Digit7` | `0x01`–`0x08` | os oito dígitos |
| `DecodeMode` | `0x09` | **`0x00` aqui** — sem decode, controlamos cada segmento |
| `Intensity` | `0x0A` | brilho, 0–15 (*duty cycle*) |
| `ScanLimit` | `0x0B` | índice do **último** dígito varrido |
| `Shutdown` | `0x0C` | `0x00` desliga, `0x01` opera |
| `DisplayTest` | `0x0F` | `0x00` normal (`0x01` acende tudo no máximo) |

**Por que sem decode:** o `Code B` embutido só sabe `0-9`, `-`, `E`, `H`, `L`,
`P`. Os rótulos do painel precisam de `b`, `C`, `d`, `n`, `o`, `r`, `t`, `U`,
`y` — com decode, `tEP` viraria símbolo aleatório.

---

## Alimentação

| Item | Valor |
|---|---|
| `V+` | **5 V** (mínimo 4,0 V) |
| Desacoplamento | **10 µF + 100 nF** no `V+`, o mais perto possível do chip |
| `ISET` | 10 kΩ → ~37 mA por segmento |
| Pico (um dígito, 8 segmentos) | ~296 mA |

O desacoplamento não é opcional: a multiplexação chaveia dezenas de mA, e sem
ele o ruído corrompe o próprio SPI.

---

## Comandos do console

```
dig <dígito> <0-255>   aciona um dígito sobrando (sobe o ScanLimit sozinho)
seg <texto>            escreve no mostrador e SEGURA a tela
auto                   devolve a tela para a telemetria
teste                  autoteste: segmento por segmento, depois dígito por dígito
brilho <0-100>         intensidade
```

Ver também [`PINOUT-ESP32.md`](PINOUT-ESP32.md) e
[`docs/HARDWARE.md`](docs/HARDWARE.md).
