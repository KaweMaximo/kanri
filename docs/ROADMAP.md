# Roadmap — Kanri

Uma versão por vez, cada uma em sua *feature branch*, cada uma com testes.
A regra: **uma versão só fecha quando o `pio test -e native` está verde e a
`main` tem a tag.**

---

## ✅ v0.1.0 — Fundação *(atual)*

O esqueleto, a rede de segurança e o CI. Nenhuma feature de telemetria.

- [x] Estrutura PlatformIO com dois ambientes: `esp32dev` e `native`
- [x] Quatro módulos com interfaces definidas e responsabilidade documentada
- [x] **Portão read-only** (`safety.h`) com allowlist de modos, PIDs e comandos AT
- [x] **Parser ELM327** completo, com sanitização e validação
- [x] Máquina de estados com invariantes de fail-safe
- [x] Backoff exponencial
- [x] Configuração com validação e `clamp_to_valid()`
- [x] 122 testes unitários rodando no PC, com **100% de cobertura de linhas**
      em `lib/`, incluindo *fuzz* determinístico
- [x] Política de testes cobrada pelo CI (cobertura + PR que mexe em `lib/`
      sem mexer em `test/` é bloqueado) — ver [TESTING.md](TESTING.md)
- [x] Watchdog armado
- [x] CI no GitHub Actions: build + testes em todo PR
- [x] Documentação de segurança, arquitetura e hardware

**Como verificar:** `pio test -e native` → 122 casos verdes.
`pio run -e esp32dev` → compila (RAM 6,6%, Flash 20,6%).

---

## 🔜 v0.2.0 — Conexão e leitura de PIDs

O primeiro dado real vindo do carro.

### Bluetooth
- [ ] `src/hal/bt_serial_transport.h/.cpp` — Bluetooth Classic SPP
- [ ] Varredura procurando pelo **nome** (`adapter_name`, tipicamente `OBDII`)
      — este é o fluxo padrão, e o que vamos implementar primeiro
- [ ] Conexão direta por `adapter_mac` como **otimização opcional**: pula a
      varredura e conecta mais rápido. Só faz sentido depois que o caminho por
      nome estiver funcionando
- [ ] Pareamento com PIN, com tratamento de timeout
- [ ] Reconexão automática usando a `RetryPolicy` que já existe
- [ ] `board_build.partitions = huge_app.csv` no `platformio.ini`
      *(a stack Bluetooth não cabe na partição padrão)*

### Protocolo ELM327
- [ ] `ObdClient::initialize()` — sequência `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`
- [ ] `ObdClient::read_pid()` — envio, leitura até o prompt `>`, timeout
- [ ] `ObdClient::read_adapter_voltage()` — comando `ATRV`
- [ ] Retentativa apenas em falha transitória (`is_transient()`)

### Decodificação
- [ ] Fórmulas de conversão por PID (RPM = `(A*256+B)/4`, temperatura = `A-40`, …)
- [ ] Testes de conversão com valores conhecidos, incluindo os extremos
- [ ] Validação de faixa física: 9.000 rpm num 4B11 é dado corrompido, não leitura

### Descoberta
- [ ] Consultar PIDs `0x00`, `0x20`, `0x40` no boot
- [ ] Consultar somente o que a ECU declarou suportar

### Persistência
- [ ] `NvsConfigStore` real usando a lib `Preferences`
- [ ] `clamp_to_valid()` aplicado em toda leitura da flash

### Testes
- [ ] `ObdClient` testado com `FakeTransport` — inclusive resposta parcial,
      timeout, lixo no meio, resposta atrasada do PID anterior
- [ ] Manter 100% de cobertura de linhas: os testes de esqueleto em
      `test_obd_client` vão **falhar** quando a implementação real entrar, e
      isso é intencional — o vermelho lembra de atualizar a expectativa

**Critério de pronto:** o firmware conecta no adaptador, lê RPM e temperatura
do Lancer, e mostra no monitor serial.

---

## 📋 v0.3.0 — Mostrador de 7 segmentos no painel

Referência de produto: **FuelTech WB-O2 Nano**. Um número grande, legível de
relance, montado no painel. Ver [HARDWARE.md](HARDWARE.md#display-7-segmentos-3-dígitos-via-max7219).

- [x] `build_frame()` textual completo (serve ao `SerialDisplay` e ao painel web)
- [ ] `seven_seg`: formatar um valor para **3 dígitos**, escolhendo sozinho
      quantas casas decimais cabem
- [ ] Rótulo curto antes do valor ao trocar de medida (`rPn`, `AGU`, `bAt`)
- [ ] Debounce do botão físico
- [ ] Driver do **MAX7219** por SPI
- [ ] Brilho por hardware — a cabine vai de sol direto a escuridão total

**Critério de pronto:** dá para dirigir com o aparelho no painel e ler a
medida sem esforço, trocando de grandeza com um toque.

## 📋 v0.5.0 — Instalação permanente

- [ ] *Deep sleep* ao perder o barramento, para o caso de a alimentação não
      ser comutada pela ignição — ESP32 ativo com Bluetooth consome ~120 mA,
      o bastante para impedir a partida depois de alguns dias
- [ ] Religar ao detectar atividade no barramento

---

## 📋 v0.4.0 — Dashboard e usabilidade

- [ ] Múltiplas telas com navegação por botão
- [ ] Mínimo/máximo por sessão (RPM máximo, temperatura máxima)
- [ ] Alertas configuráveis (ex.: temperatura acima do limite)
- [ ] VIN via Modo 09, com suporte a resposta multi-frame
- [ ] Menu de configuração no próprio aparelho
- [ ] Cadência de leitura adaptativa: PIDs que mudam rápido lidos mais vezes

---

## 💭 Além disso (ideias, sem compromisso)

- Registro em cartão SD, para análise depois
- *Deep sleep* quando a ignição desliga (resolve o consumo parasita)
- Portal Wi-Fi para configuração
- Envio de telemetria para a nuvem
- Cálculo de consumo instantâneo a partir de MAF + velocidade
- Suporte a mais de um veículo, com perfis

---

## O que está fora de escopo — permanentemente

Estas não são "ainda não". São **não**, e o motivo está em
[SAFETY.md](SAFETY.md#1-somente-leitura--a-regra-que-não-se-negocia):

- ❌ Limpar códigos de falha (Modo 04)
- ❌ Comandar atuadores (Modo 08)
- ❌ Qualquer escrita na ECU
- ❌ Reprogramação de módulos
- ❌ Comando `ATSH` (definir header CAN)
