# Hardware — Kanri

## Veículo alvo

**Mitsubishi Lancer 2.0 2014 — motor 4B11**

| Item | Valor |
|------|-------|
| Conector | OBD2 / SAE J1962, 16 pinos, sob o painel do lado do motorista |
| Protocolo | ISO 15765-4 (CAN, 11 bits, 500 kbit/s) |
| Detecção | Automática, via `ATSP0` — não é preciso chumbar o protocolo |
| Tensão no pino 16 | 12 V nominal, **normalmente permanente** (mesmo com o carro desligado) |

> Todo veículo pós-2008 usa CAN. O `ATSP0` (protocolo automático) resolve a
> negociação, então o firmware não precisa saber disso — mas você precisa,
> para diagnosticar quando algo não funcionar.

### PIDs prováveis no 4B11

Nenhuma montadora implementa todos os PIDs do padrão. A lista abaixo é o que
o Kanri **sabe pedir** (`lib/kanri_obd/include/kanri_obd/obd_pid.h`), não o
que a ECU garantidamente responde.

| PID | Grandeza | Bytes | Unidade |
|-----|----------|-------|---------|
| `0x04` | Carga calculada do motor | 1 | % |
| `0x05` | Temperatura do líquido de arrefecimento | 1 | °C |
| `0x0B` | Pressão absoluta no coletor (MAP) | 1 | kPa |
| `0x0C` | Rotação do motor | 2 | rpm |
| `0x0D` | Velocidade do veículo | 1 | km/h |
| `0x0E` | Avanço de ignição | 1 | ° |
| `0x0F` | Temperatura do ar admitido | 1 | °C |
| `0x10` | Fluxo de ar (MAF) | 2 | g/s |
| `0x11` | Posição da borboleta | 1 | % |
| `0x1F` | Tempo de motor ligado | 2 | s |
| `0x2F` | Nível de combustível | 1 | % |
| `0x42` | Tensão do módulo de controle | 2 | V |
| `0x43` | Carga absoluta | 2 | % |
| `0x46` | Temperatura ambiente | 1 | °C |
| `0x5C` | Temperatura do óleo | 1 | °C |

`0x5C` (temperatura do óleo) **provavelmente não é suportado** neste motor.
Está na lista para a descoberta em runtime confirmar.

**Descoberta de suporte:** os PIDs `0x00`, `0x20` e `0x40` devolvem um mapa de
bits com "quais PIDs eu suporto". Consultar isso no boot, em vez de chumbar
uma lista, é a forma correta — está no roadmap da v0.2.

**Tensão da bateria — duas fontes diferentes:**
- PID `0x42` = a tensão que a **ECU** mede internamente.
- Comando `ATRV` = a tensão que o **adaptador** mede no pino 16 do conector.

As duas normalmente diferem um pouco. `ATRV` não envolve a ECU e continua
funcionando mesmo sem link com o veículo — útil para diagnóstico.

---

## Placa: tem que ser ESP32 **clássico**

> ⚠️ **A escolha da placa não é livre.**

| Placa | Bluetooth Classic (SPP) | Serve? |
|-------|------------------------|--------|
| **ESP32-WROOM-32 / DevKit v1** (`esp32dev`) | ✅ Sim | ✅ **Use esta** |
| ESP32-WROVER | ✅ Sim | ✅ Serve |
| ESP32-S3 | ❌ Só BLE | ❌ Não |
| ESP32-C3 / C6 | ❌ Só BLE | ❌ Não |
| ESP32-S2 | ❌ Sem rádio Bluetooth | ❌ Não |

**Por quê:** a grande maioria dos adaptadores ELM327 baratos usa **Bluetooth
Classic SPP** (Serial Port Profile), não BLE. As variantes mais novas do ESP32
trocaram Bluetooth Classic por BLE-only. Uma ESP32-S3 é uma placa melhor em
quase tudo — e simplesmente não conversa com o adaptador.

Existem adaptadores ELM327 **BLE** (comuns em versões "para iPhone"). Se você
tiver um desses, o alvo muda. Confirme antes de comprar a placa.

---

## Adaptador ELM327 — o modelo confirmado deste projeto

**Scanner Automotivo OBD2 "Placa Dupla" VS1.5 — ELM327 com PIC18F25K80,
Bluetooth Classic.**

Essa combinação é uma **boa notícia**, e vale entender por quê — ajuda a
interpretar o comportamento quando algo não funcionar.

| Característica | O que significa na prática |
|---|---|
| **PIC18F25K80** | É o microcontrolador. Tem módulo CAN nativo (ECAN), 32 KB de flash e ~3,6 KB de RAM. É a base usada nos clones **melhores**. Os inferiores usam PIC18F2480 (menos RAM, engasga em resposta longa) ou MCUs genéricos com firmware reduzido. |
| **Placa dupla** | Duas placas empilhadas: uma com o MCU e os transceivers, outra com o módulo Bluetooth. É o layout associado às versões mais completas — e permite inspecionar/trocar o módulo BT se precisar. |
| **VS1.5** | Versão de firmware anunciada. Aqui `1.5` é bom sinal: os adaptadores que anunciam "v2.1" são, na prática, quase sempre v1.4 com rótulo trocado. |
| **Bluetooth Classic** | Confirma o alvo do projeto: **ESP32-WROOM-32 clássico** com `BluetoothSerial` (SPP). O módulo costuma ser um CSR BC417 (Bluetooth 2.x, perfil SPP). |

### Parâmetros para configurar

| Campo em `KanriSettings` | Valor típico neste adaptador |
|---|---|
| `adapter_name` | `OBDII` |
| `adapter_pin` | `1234` (às vezes `0000` ou `6789`) |
| `adapter_mac` | Deixe vazio na primeira vez; preencha depois de descobrir, para conectar mais rápido |

Descobrir o MAC e o nome reais, no Linux:

```bash
bluetoothctl
> scan on
# procure a linha do OBDII e anote o endereço AA:BB:CC:DD:EE:FF
```

### ⚠️ Consequência de segurança: este adaptador é capaz de escrever

Um detalhe importante e contraintuitivo: **por ser um clone bom, ele
implementa o conjunto de comandos AT completo** — incluindo `ATSH` (definir
header CAN), `ATCRA` (filtro) e os comandos de envio de quadro arbitrário.

Ou seja: **o hardware que você tem é perfeitamente capaz de escrever na ECU.**
A única coisa que impede isso é o firmware do Kanri.

É exatamente por isso que a allowlist em
[`safety.h`](../lib/kanri_obd/include/kanri_obd/safety.h) não é
paranoia teórica, e por isso `ATSH` está bloqueado com teste. Um adaptador
"burro" perdoaria um bug no nosso lado; este não perdoa.
Ver [SAFETY.md](SAFETY.md#1-somente-leitura--a-regra-que-não-se-negocia).

### O que esperar em operação

Mesmo sendo dos bons, é um clone. Comportamentos **normais**, não defeitos:

- responde `SEARCHING...` na primeira consulta, antes do dado;
- pode responder fora de ordem sob carga;
- emite `BUFFER FULL` se você consultar rápido demais (comece com
  `poll_interval_ms = 200`);
- pode devolver lixo com contato ruim no conector;
- `NO DATA` para PID que a ECU do 4B11 não implementa — isso é a ECU, não o
  adaptador.

O parser em `lib/kanri_obd/src/elm327_parser.cpp` trata todos esses casos, e
tem 33 testes justamente por causa disso.

### ⚡ Consumo parasita — atenção real

O módulo Bluetooth fica ligado e detectável enquanto o adaptador estiver
plugado, consumindo na ordem de **30–50 mA continuamente**. O pino 16 do OBD2
normalmente é energizado **mesmo com o carro desligado**.

Contas rápidas: 40 mA × 24 h ≈ 1 Ah/dia. Uma bateria de 60 Ah já parcialmente
descarregada pode não dar partida depois de alguns dias assim.

**Desplugue o adaptador quando não estiver usando**, ou instale uma chave na
linha de +12 V.

## Alimentação — leia antes de ligar qualquer coisa

> ⚠️ **Ligar um ESP32 direto nos 12 V do carro destrói o ESP32.**
> A tabela completa de fenômenos elétricos e os requisitos obrigatórios estão
> em [SAFETY.md § 5](SAFETY.md#5-requisitos-elétricos--a-rede-de-12-v-é-hostil).
> Resumo: a rede de 12 V vai de 6 V (partida) a 40 V+ (*load dump*), com
> transientes ISO 7637-2 de centenas de volts.

### Topologia recomendada

```
        +12 V (OBD2 pino 16, ou linha comutada pela ignição)
          │
        [Fusível 500 mA–1 A]
          │
        [Proteção de polaridade reversa]      ← Schottky ou MOSFET-P ideal-diode
          │
        [TVS: SMBJ26A / P6KE30A]  ──┐         ← corta os picos
          │                          │
        [Buck ≥40 V entrada, ≥1 A]  GND       ← TPS54360 / MP2315 / LM2596-HV
          │
        5 V ──[470 µF bulk]──[100 nF cerâmico]
          │
        ESP32 (pino 5V/VIN — o regulador da placa faz 5 V → 3,3 V)
```

### Erros comuns e como se manifestam

| Erro | Sintoma | Diagnóstico enganoso |
|------|---------|---------------------|
| Regulador subdimensionado (< 1 A) | Reset ao ligar o Bluetooth | "Parece bug de software" — não é. O rádio puxa ~500 mA de pico. |
| Sem capacitor de *bulk* | Resets aleatórios, mais frequentes com o motor ligado | "Firmware instável" |
| AMS1117 / 7805 direto nos 12 V | Esquenta muito, morre em semanas | "Componente ruim" |
| Sem proteção de transientes | Funciona meses, morre de repente | "Azar" |
| Sem proteção de polaridade | Morte instantânea na montagem | Esse pelo menos é óbvio |

### Consumo parasita

O pino 16 do OBD2 é normalmente **permanentemente energizado**. Um aparelho
esquecido plugado descarrega a bateria em poucos dias.

Opções: alimentar por linha comutada pela ignição, implementar *deep sleep*
(roadmap v0.5), ou simplesmente desplugar.

---

## Display: 7 segmentos, 3 dígitos, via MAX7219

**A referência de produto é o FuelTech WB-O2 Nano**: um mostrador compacto de
LED vermelho, com um número grande, montado no painel e legível de relance —
inclusive com sol direto.

| Item | Escolha |
|---|---|
| Mostrador | 7 segmentos, **3 dígitos**, com pontos decimais |
| Driver | **MAX7219** (SPI, 3 fios) |
| Navegação | **Botão físico** avança para a próxima medida |

### Por que MAX7219 e não um shift register

O MAX7219 faz **multiplexação e brilho por hardware**. Com um 74HC595, o
firmware teria de redesenhar os dígitos dezenas de vezes por segundo a partir
de um timer — e cada milissegundo gasto ali é um milissegundo a menos para ler
o barramento OBD. O MAX7219 recebe o número e cuida do resto.

O brilho por hardware também importa no carro: a cabine vai de sol direto a
escuridão total, e o `set_brightness()` que `IDisplay` já tem desde a v0.1
passa a ter para onde ir.

### O que muda no firmware

Três dígitos mostram **uma medida por vez**. O `DisplayFrame` de texto (4
linhas × 24 caracteres) não serve: "1726 rpm" não cabe em três dígitos.

A abstração para este mostrador é outra — um valor numérico, quantas casas
decimais usar, e qual medida está no ar. Ver
[`kanri_display/seven_seg.h`](../lib/kanri_display/include/kanri_display/seven_seg.h).

O `SerialDisplay` e o `DisplayFrame` textual continuam úteis para
desenvolvimento e diagnóstico pelo painel — são duas saídas, não uma
substituindo a outra.

### Alimentação do mostrador

**Não alimente o display pelo regulador 3,3 V da placa ESP32.** Um mostrador
de 7 segmentos com todos os dígitos acesos puxa bem mais que esse regulador
entrega. O MAX7219 quer **5 V**, e a corrente dos segmentos vem dele, não do
ESP32 — que fala com ele apenas por três fios de sinal.

## Revisão do esquema elétrico

Esquema desenhado por Jose Rodrigues (29/08/2026): ELM327 por Bluetooth →
ESP32-WROOM-32D → MAX7219 (SPI) → display 5361AS de 3 dígitos, botão no
GPIO 17, alimentação por conversor buck.

A topologia está correta. Os pontos abaixo são o que falta fechar antes de
montar.

### 🔴 Nível lógico: o ESP32 não alcança o MAX7219

**Este é o problema mais importante, e ele não aparece na bancada.**

| | Valor |
|---|---|
| `VIH` mínimo do MAX7219 (VCC = 5 V) | **3,5 V** |
| `VOH` máximo do ESP32 | **3,3 V** |

Os sinais `DIN`, `CLK` e `CS` saem do ESP32 **abaixo do que o MAX7219 exige
para reconhecer nível alto**. Fora de especificação.

Isso costuma "funcionar" na bancada porque o limiar real do chip fica abaixo
do garantido — mas a margem depende de temperatura, e a cabine de um carro vai
de −5 °C a 70 °C. O sintoma seria dígito trocado ou mostrador congelando de
vez em quando: o tipo de defeito que consome fins de semana e que ninguém
associa ao nível lógico.

**Solução: um 74HCT125** (buffer quádruplo, ~R$ 3) nas três linhas de sinal.
O sufixo **HCT** é o que importa — ele aceita entrada em nível TTL
(`VIH` = 2,0 V), então lê os 3,3 V do ESP32 como alto e entrega 5 V ao
MAX7219. Um 74HC**125** comum **não serve**: o HC tem `VIH` = 0,7 × VCC = 3,5 V,
exatamente o problema que queremos resolver.

Alternativa sem CI extra: alimentar o MAX7219 com **3,3 V**. Mas o `VCC`
mínimo dele é **4,0 V** — fica fora de spec do outro lado, e o display perde
brilho justamente onde ele mais precisa (sol direto).

### ✅ LM2596HV em vez do MP2359 — a escolha certa

| Conversor | Entrada máxima | Sobrevive no carro? |
|---|---|---|
| MP2359 | **24 V** | ❌ |
| **LM2596HV** | **60 V** | ✅ |

A rede de 12 V chega a **40 V+** num *load dump* (ver
[SAFETY.md § 5](SAFETY.md#5-requisitos-elétricos--a-rede-de-12-v-é-hostil)).
O MP2359 morre nesse evento; o LM2596HV atravessa. A diferença de preço
(~R$ 35) é seguro barato — trocar o conversor queimado sai mais caro, e ele
leva o ESP32 junto.

### O que ainda falta no esquema

Nenhum destes é opcional numa instalação permanente:

| Item | Por quê |
|---|---|
| **Fusível 500 mA–1 A** | Na linha de +12 V, antes de tudo |
| **Proteção de polaridade reversa** | Schottky em série, ou MOSFET-P *ideal diode* (menor queda) |
| **TVS na entrada** (SMBJ26A / P6KE30A) | Mesmo 60 V não cobre transientes ISO 7637-2, que são rápidos e altos |
| **10 µF + 100 nF no `V+` do MAX7219** | O datasheet é explícito. A multiplexação chaveia dezenas de mA; sem desacoplamento o ruído corrompe o próprio SPI |
| **≥ 470 µF de bulk na saída 5 V** | O ESP32 puxa picos de ~500 mA no rádio Bluetooth |

### Conferências que passaram

- **5361AS é cátodo comum**, e o MAX7219 faz *sink* nos dígitos — compatíveis.
- **`ISET` de 10 kΩ** dá ~37 mA por segmento, perto do máximo. Está bom: o
  brilho é reduzido por software (o MAX7219 tem registro de intensidade), e é
  melhor ter margem para o sol direto do que faltar.
- **Botão no GPIO 17** com `INPUT_PULLUP`: o GPIO 17 tem pull-up interno e está
  livre no WROOM-32. (No WROVER ele é usado pela PSRAM — mas a placa aqui é
  WROOM.) O debounce está em `kanri_core/button.h`.
- **GPIO 36 é *input-only* e sem pull-up interno.** Correto para leitura
  analógica (medir a tensão da bateria por divisor resistivo); não serviria
  para o botão.

### De onde tirar o 5 V do MAX7219

**Pelo `VIN` — decisão do Jose, e está correta.** As contas abaixo existem
para registrar *por que* ela é a certa, não para questioná-la: a diferença
entre os dois pinos de alimentação da placa muda o resultado por completo, e
vale ter isso escrito para quem montar depois.

| Origem | Funciona? | Por quê |
|---|---|---|
| Pino **3,3 V** da DevKit | ❌ **Não** | Dois impedimentos independentes, abaixo |
| Pino **5 V / VIN** | ✅ **Sim** | É o trilho do próprio conversor buck |

#### Por que o 3,3 V não serve

**1. Está abaixo do mínimo do chip.** O `V+` do MAX7219 é especificado de
**4,0 V a 5,5 V**. Em 3,3 V ele fica fora de faixa: o display perde brilho
justamente onde mais precisa (sol direto no painel), e o oscilador interno
passa a operar sem garantia.

**2. O regulador da placa não dá conta.** Com `ISET` de 10 kΩ:

```
corrente por segmento ............  37 mA
pico (um dígito, 8 segmentos) ... 296 mA
ESP32 em pico de rádio .......... 500 mA
──────────────────────────────────────────
pico somado ..................... 796 mA
```

O AMS1117 da DevKit é anunciado para 800 mA, mas o limite real é **térmico**:
a 500 mA ele já dissipa 0,85 W num encapsulamento SOT-223 sem dissipador, e a
796 mA seriam **1,36 W**. Ele entraria em proteção térmica — e o sintoma seria
o ESP32 reiniciando quando o display acende, algo que parece bug de firmware e
não é.

#### O caminho certo — o `VIN`, como o Jose indicou

```
        LM2596HV (buck)
              │
              ├──► 5 V ──┬──► pino 5V/VIN da DevKit ──► AMS1117 ──► ESP32 (3,3 V)
              │          │
              │          ├──► V+ do MAX7219
              │          │
              │          └──► VCC do 74HCT125
              │
             GND (comum a tudo)
```

Continua sendo **um trilho só, do mesmo conversor** — que é o que você queria.
A diferença é que o display puxa sua corrente direto do buck (dimensionado
para ≥ 1 A), e não através do reguladorzinho da placa.

O `74HCT125` entra alimentado nesse mesmo 5 V, e é ele que eleva os 3,3 V do
ESP32 ao nível que o MAX7219 exige. Sem ele, os dois problemas — corrente e
nível lógico — não se resolvem juntos: baixar para 3,3 V conserta o nível
lógico e quebra a alimentação; subir para 5 V conserta a alimentação e quebra
o nível lógico.

> **O capacitor não é opcional aqui.** Aquele pico de 296 mA aparece e some a
> cada varredura de dígito. Quem o supre é o par 10 µF + 100 nF junto ao `V+`
> do MAX7219 — sem ele, o pico vira queda de tensão no trilho, e essa queda
> chega ao ESP32.

### Pinos SPI sugeridos

O esquema não fixa quais GPIOs. Usando o VSPI padrão do ESP32:

| Sinal | GPIO |
|---|---|
| `SCK` / CLK | 18 |
| `DIN` / MOSI | 23 |
| `CS` | 5 |

Evite GPIO 6–11 (ligados à flash) e GPIO 0, 2, 12 e 15 (afetam o boot).

## Mapa de pinos — ESP32 DevKit V1 (30 pinos)

Placa confirmada na bancada em 29/08/2026, pela serigrafia: **DevKit V1 de 30
pinos**, módulo ESP32-WROOM-32. As duas fileiras, como vêm impressas:

```
 3V3  GND  D15  D2  D4  RX2  TX2  D5  D18  D19  D21  RX0  TX0  D22  D23
 VIN  GND  D13  D12  D14  D27  D26  D25  D33  D32  D35  D34  VN   VP   EN
```

### 🔴 Não use — por motivo físico, não por preferência

| Pino | GPIO | Por quê |
|---|---|---|
| `RX0` / `TX0` | 3 / 1 | Console USB. Usar aqui **derruba o painel Kanri** e a gravação |
| `D34`, `D35` | 34, 35 | **Input-only.** Não têm driver de saída — não acendem LED nunca |
| `VP`, `VN` | 36, 39 | **Input-only**, pelo mesmo motivo |
| `D12` | 12 | Strapping `MTDI`. **HIGH no boot** faz o chip procurar flash de 1,8 V e **não iniciar** |

Os quatro *input-only* são a pegadinha cruel: `pinMode(34, OUTPUT)` **compila,
roda e não dá erro nenhum**. O pino simplesmente nunca muda de nível.

### 🟡 Use com ressalva

| Pino | GPIO | Ressalva |
|---|---|---|
| `D15` | 15 | Strapping `MTDO`. LOW no boot silencia o log — você perde diagnóstico |
| `D5` | 5 | Strapping com pull-up interno. Um LED aqui pisca durante o boot |
| `D2` | 2 | Já é o **LED de status** do firmware (e é strapping) |

### ✅ O caminho escolhido: três fios

**Decisão de Jose Rodrigues (29/08/2026):** o display é acionado pelo MAX7219,
e o ESP32 fala com ele por **três pinos** em vez de onze.

| Sinal | Pino do MAX7219 | GPIO |
|---|---|---|
| `DIN` | 1 | **23** |
| `CLK` | 13 | **18** |
| `LOAD` | 12 | **5** |

> ⚠️ **`LOAD` e `CS` são o mesmo pino.** O pino 12 chama-se `LOAD` no MAX7219
> e `CS` no MAX7221. Datasheets e bibliotecas usam os dois nomes para o mesmo
> sinal — quem lê "DIN, LOAD e CS" como três fios acaba esquecendo o `CLK`, e
> aí o display fica apagado sem dar erro nenhum.

O ganho não é só a economia de pinos. **A multiplexação sai da CPU:** no
acionamento direto o ESP32 teria de redesenhar os dígitos dezenas de vezes por
segundo num timer, e uma leitura OBD trava o laço por até 1 s — o display
piscaria a cada leitura. Com o MAX7219 o número é enviado **uma vez**.

O orçamento de corrente também deixa de existir: os segmentos puxam do **5 V
do MAX7219**, não do GPIO.

### Plano B — acionamento direto, sem o MAX7219

Só vale se o chip não estiver disponível. Fica registrado porque a análise de
pinos proibidos acima continua valendo em qualquer cenário.

Restam 14 GPIOs plenamente livres e são necessários 11. Cabe com folga.

| Função | GPIO | Onde fica |
|---|---|---|
| Segmento `a` | 13 | linha de baixo |
| Segmento `b` | 14 | linha de baixo |
| Segmento `c` | 27 | linha de baixo |
| Segmento `d` | 26 | linha de baixo |
| Segmento `e` | 25 | linha de baixo |
| Segmento `f` | 33 | linha de baixo |
| Segmento `g` | 32 | linha de baixo |
| Ponto decimal `dp` | 4 | linha de cima |
| Dígito 1 (comum) | 19 | linha de cima |
| Dígito 2 (comum) | 21 | linha de cima |
| Dígito 3 (comum) | 22 | linha de cima |
| Botão | 17 | `TX2`, com `INPUT_PULLUP` |
| LED de status | 2 | já no firmware |

Os sete segmentos `a`–`g` ficam **contíguos na linha de baixo** (13, 14, 27,
26, 25, 33, 32 — pulando o 12) e os três comuns **contíguos na de cima**. Não é
capricho: fiação de protoboard erra por vizinhança, e um bloco contínuo é
conferível de relance.

**Reservados para o MAX7219** quando ele chegar: **23** (`DIN`), **18** (`CLK`)
e **5** (`CS`). Ficam de fora da alocação acima de propósito, para os dois
caminhos coexistirem na placa durante a transição.

### 🔴 A corrente dos pinos de dígito — só no plano B

Nos três pinos de **dígito comum** passa a **soma** de todos os segmentos
acesos daquele dígito — não a corrente de um segmento.

| | |
|---|---|
| GPIO do ESP32, recomendado | 12 mA |
| GPIO do ESP32, **máximo absoluto** | 40 mA |
| 8 segmentos a 6 mA (resistor de 220 Ω) | **48 mA** ❌ |
| 8 segmentos a 1,3 mA (resistor de **1 kΩ**) | **10 mA** ✅ |

Por isso, no acionamento direto, o resistor de **1 kΩ** não é escolha de
brilho: é o que mantém o pino dentro do especificado. O display fica fraco de
propósito. Brilho é problema do MAX7219, que faz o *sink* dos dígitos com a
corrente dele.

E o modo de falha é traiçoeiro: exceder o máximo **não queima na hora**,
degrada. Funciona na bancada, funciona uma semana no carro, e depois o pino
morre sem explicação.

## Potenciômetro de brilho

Decisão de Jose Rodrigues (29/08/2026): pernas externas em **3,3 V** e **GND**,
cursor no **GPIO 36 (`VP`)**.

```
3,3 V ──[ perna 1 ]
         [ cursor  ] ──► GPIO 36 (VP)
GND   ──[ perna 3 ]
```

Valor sugerido: **10 kΩ**. Menos desperdiça corrente ligado direto no trilho;
muito mais e a impedância atrapalha o ADC.

### Por que o GPIO 36 é o pino certo

| Motivo | Consequência de errar |
|---|---|
| É do **ADC1** | O `ADC2` não funciona com o rádio ativo. Um potenciômetro no ADC2 pararia de responder assim que o Bluetooth conectasse — e funcionaria perfeitamente na bancada antes disso |
| É ***input-only*** | Não desperdiça um pino capaz de acionar coisa. É justamente um dos que não serviam para o display |

Do ADC1 e disponíveis nesta placa: **32, 33, 34, 35, `VP` (36), `VN` (39)**.

### Os três modos de falha, e o que faz cada um

Um botão giratório no ADC parece trivial e não é. São **cinco níveis** (decisão
do Kawe) mais dois filtros, e cada peça resolve um problema diferente:

| Problema | Sintoma no painel | O que resolve |
|---|---|---|
| ADC do ESP32 é ruidoso (±40 contagens parado) | mostrador **pulsa** com o botão imóvel | **5 níveis**: cada faixa tem ~819 contagens, muito acima do ruído |
| Ruído exatamente na fronteira entre faixas | pisca entre **dois** brilhos, só numa posição | **histerese** de 150 contagens para sair da faixa |
| GPIO 36 sem potenciômetro ligado | brilho **passeia sozinho** | **6 leituras** concordando, ancoradas na primeira |

O terceiro é o menos óbvio. O GPIO 36 não tem *pull-up* interno: sem o fio,
vira antena. E o discriminador não é o valor lido — é a **estabilidade**. Um
potenciômetro é fonte de baixa impedância e oscila pouco; um pino solto varre
a escala inteira entre amostras.

> ⚠️ **Não faça média das leituras.** Parece melhoria e é o contrário: a média
> aproxima um pino solto do meio da escala, e aí lixo passa a parecer posição
> estável. O filtro precisa **ver** o ruído para poder rejeitá-lo.

### Os cinco níveis não são lineares

```
nível 1 →   3 %      nível 4 →  60 %
nível 2 →  12 %      nível 5 → 100 %
nível 3 →  30 %
```

A percepção de brilho é aproximadamente logarítmica. `20/40/60/80/100`
desperdiçaria três passos na faixa clara — justamente onde a diferença menos
aparece — e não deixaria nenhum ajuste útil para dirigir à noite.

O nível 1 em 3 % **não apaga**: é o passo mais fraco do MAX7219 (1/32 do
ciclo). Um nível que apagasse o painel seria indistinguível de defeito.

### Conferindo a fiação

```
pot
```

Mostra a leitura crua do ADC. Gire de ponta a ponta: deve ir de **~0 a ~4095**.
Se o número pular sem você girar, o cursor está solto.

## Instalação permanente no veículo

O objetivo é o aparelho ficar **ligado direto no carro**, acendendo com a
ignição. Isso muda duas coisas em relação à bancada.

### ⚠️ Consumo parasita deixa de ser teórico

Na bancada, o ESP32 é alimentado por USB e desliga com o notebook. Instalado,
ele fica ligado ao carro **o tempo todo** — e o pino 16 do conector OBD2 é
normalmente energizado mesmo com o carro desligado.

| Consumo | Efeito em 3 dias |
|---|---|
| ESP32 ativo com Bluetooth (~120 mA) | ~8,6 Ah — **suficiente para não dar partida** |
| ELM327 detectável (~40 mA) | ~2,9 Ah |
| ESP32 em deep sleep (~10 µA) | desprezível |

**Alimentar pela linha comutada da ignição resolve isso na origem** — o
aparelho simplesmente não tem energia com o carro desligado, e é o caminho que
o esquema elétrico deve seguir. Se por algum motivo a alimentação for
permanente, o firmware precisa entrar em *deep sleep* ao perder o barramento
(roadmap v0.5).

### O resto continua valendo

Os requisitos elétricos de [SAFETY.md § 5](SAFETY.md#5-requisitos-elétricos--a-rede-de-12-v-é-hostil)
— regulador de entrada ≥ 40 V, proteção de polaridade, TVS, fusível — valem
igual, e com mais razão numa instalação permanente: o aparelho passa a viver
os transientes do veículo todos os dias, não só durante um teste.

---

## Montagem no veículo

- ❌ **Não** obstrua a visão do motorista.
- ❌ **Não** monte na área de acionamento de airbag.
- ✅ Fixe firmemente — objeto solto na cabine é projétil em uma frenagem.
- ✅ Passe o cabeamento longe das bobinas de ignição (ruído).
- ✅ Prenda o cabo para não enroscar nos pedais.
