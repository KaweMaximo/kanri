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

## Adaptador ELM327

| Item | Recomendação |
|------|--------------|
| Interface | Bluetooth **Classic** (SPP) |
| Versão de firmware anunciada | v1.5 costuma ser mais estável que os "v2.1" (que geralmente são v1.4 com rótulo falso) |
| PIN de pareamento | Quase sempre `1234` ou `0000` |
| Nome Bluetooth | Quase sempre `OBDII` |

**Sobre clones:** praticamente todos os adaptadores baratos são clones do chip
original da ELM Electronics. Eles funcionam, mas:

- respondem devagar e às vezes fora de ordem;
- emitem `BUFFER FULL` sob carga;
- alguns retornam lixo binário com contato ruim;
- alguns não implementam todos os comandos `AT`.

**Isso não é problema a ser contornado — é o cenário normal.** É exatamente
por isso que o parser em `lib/kanri_obd/src/elm327_parser.cpp` trata toda
resposta como entrada hostil, e por isso ele tem 31 testes.

---

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

## Display (v0.3 — ainda não definido)

A escolha está aberta de propósito: `IDisplay` isola essa decisão, então
trocar depois não mexe na lógica.

| Opção | Interface | Prós | Contras |
|-------|-----------|------|---------|
| **SSD1306 OLED 128×64** | I²C (2 fios) | Barato, ótimo contraste, fácil | Pequeno; sofre *burn-in* com imagem estática |
| **ST7789 TFT 240×240** | SPI | Colorido, mais área | Mais pinos, mais RAM de framebuffer |
| **Nokia 5110** | SPI | Muito barato, legível no sol | Monocromático, baixa resolução |

**Consideração automotiva:** a cabine vai de sol direto a escuridão total.
Contraste e brilho ajustável importam mais que resolução. É por isso que
`IDisplay` já tem `set_brightness()` desde a v0.1.

**Não alimente o display pelo regulador 3,3 V da placa ESP32** se ele puxar
mais que algumas dezenas de mA — esse regulador é pequeno.

---

## Montagem no veículo

- ❌ **Não** obstrua a visão do motorista.
- ❌ **Não** monte na área de acionamento de airbag.
- ✅ Fixe firmemente — objeto solto na cabine é projétil em uma frenagem.
- ✅ Passe o cabeamento longe das bobinas de ignição (ruído).
- ✅ Prenda o cabo para não enroscar nos pedais.
