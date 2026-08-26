# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Versionamento segue [SemVer](https://semver.org/lang/pt-BR/).

## [Não lançado]

Nada ainda. Próxima versão: [v0.2.0 — Conexão e leitura de PIDs](docs/ROADMAP.md#-v020--conexão-e-leitura-de-pids).

---

## [0.1.0] — 2026-08-26

Primeira versão. **Fundação apenas** — sem features de telemetria.

O objetivo desta versão é ter a estrutura, a rede de segurança e o CI no lugar
**antes** de escrever a primeira linha de comunicação com o carro.

### Adicionado

#### Segurança
- Portão read-only (`kanri_obd/safety.h`): allowlist de modos OBD2 (só `0x01`
  e `0x09`), allowlist de PIDs e allowlist de comandos AT
- Bloqueio explícito e documentado de `ATSH`, `ATMA`, `ATPP`, `ATBI`, `ATCRA`,
  `ATTP` — cada um com o motivo escrito no código
- Parser ELM327 com sanitização completa: limites de buffer explícitos, sem
  alocação dinâmica, sem `strlen` em buffer cru, rejeição de modo/PID que não
  casem com o pedido
- Máquina de estados com invariantes de fail-safe: nenhum caminho de falha
  reinicia o firmware; `Boot` é inalcançável de qualquer outro estado
- Backoff exponencial com proteção contra estouro de inteiro
- `clamp_to_valid()` para configuração: garante que o firmware nunca opere
  com valores inválidos vindos da flash
- Watchdog armado (8 s) em `main.cpp`

#### Estrutura
- Projeto PlatformIO com dois ambientes: `esp32dev` (firmware) e `native`
  (testes no PC)
- Quatro módulos em `lib/`, todos livres de `Arduino.h`: `kanri_core`,
  `kanri_obd`, `kanri_config`, `kanri_display`
- Camada de adaptadores de hardware em `src/hal/`: `ArduinoClock`,
  `SerialDisplay`, `NullTransport`, `NvsConfigStore`
- Portas (interfaces) definidas: `IClock`, `ITransport`, `IDisplay`,
  `IConfigStore`
- Catálogo de PIDs para o Mitsubishi Lancer 2.0 2014 (4B11)

#### Testes
- 98 casos em 5 suítes Unity, rodando no PC em ~1,4 s
- Dublês de teste header-only: `FakeClock`, `FakeTransport`, `FakeDisplay`,
  `FakeConfigStore`
- Testes de invariante: varredura exaustiva dos 256 modos OBD2, de todas as
  combinações estado × evento, fuzz determinístico de 5.000 entradas no
  parser, e 500 amostras de configuração corrompida

#### Infraestrutura
- CI no GitHub Actions: testes unitários, build do firmware e validação de
  Conventional Commits, todos em todo PR
- Hook local de `commit-msg` validando Conventional Commits
- `.gitignore`, `.editorconfig`, `.clang-format`, `.gitattributes`
- Template de Pull Request com checklist de segurança

#### Documentação
- `README.md` com arquitetura em Mermaid e justificativa da escolha do
  framework
- `docs/SAFETY.md` — requisitos de segurança, incluindo os elétricos
- `docs/ARCHITECTURE.md` — decisões de projeto, diagramas de estado e sequência
- `docs/HARDWARE.md` — veículo, placa, adaptador, alimentação
- `docs/ROADMAP.md` — o que entra em cada versão
- `CLAUDE.md` — convenções do projeto
- `CONTRIBUTING.md` — fluxo de trabalho

### Ainda não existe

Bluetooth, sequência AT de inicialização, leitura real de PIDs, conversão para
unidades de engenharia, persistência na flash e display físico. Ver
[ROADMAP.md](docs/ROADMAP.md).

[Não lançado]: https://github.com/kawemaximo/kanri/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/kawemaximo/kanri/releases/tag/v0.1.0
