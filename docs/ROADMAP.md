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
- [x] 98 testes unitários rodando no PC, incluindo *fuzz* determinístico
- [x] Watchdog armado
- [x] CI no GitHub Actions: build + testes em todo PR
- [x] Documentação de segurança, arquitetura e hardware

**Como verificar:** `pio test -e native` → 98 casos verdes.
`pio run -e esp32dev` → compila (RAM 6,6%, Flash 20,6%).

---

## 🔜 v0.2.0 — Conexão e leitura de PIDs

O primeiro dado real vindo do carro.

### Bluetooth
- [ ] `src/hal/bt_serial_transport.h/.cpp` — Bluetooth Classic SPP
- [ ] Varredura procurando `adapter_name` ou `adapter_mac`
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

**Critério de pronto:** o firmware conecta no adaptador, lê RPM e temperatura
do Lancer, e mostra no monitor serial.

---

## 📋 v0.3.0 — Display físico

- [ ] Escolher o display (ver [HARDWARE.md](HARDWARE.md#display-v03--ainda-não-definido))
- [ ] Driver concreto implementando `IDisplay`
- [ ] `build_frame()` real: telas Splash, Connecting, Dashboard e Error
- [ ] Formatação com `--` para toda medida com `valid == false`
- [ ] Tela de erro informativa: qual estado, qual motivo, quanto até retentar
- [ ] Brilho ajustável
- [ ] Testes de formatação (com `FakeDisplay`), incluindo truncamento de texto

**Critério de pronto:** dá para dirigir com o aparelho no painel e ler os
dados sem esforço.

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
