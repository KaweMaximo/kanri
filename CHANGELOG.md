# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Versionamento segue [SemVer](https://semver.org/lang/pt-BR/).

## [Não lançado]

Próxima versão planejada: [v0.2.0 — Conexão e leitura de PIDs](docs/ROADMAP.md#-v020--conexão-e-leitura-de-pids).

### Adicionado
- O CI pode ser disparado à mão (`workflow_dispatch`), por exemplo com
  `gh workflow run CI --ref main`. Necessário porque o GitHub perdeu dois
  eventos de Actions na criação do repositório, e sem isso a única forma de
  reverificar uma branch seria empurrar um commit vazio.
- Job de CI **`CHANGELOG atualizado`**: PR que altera código ou infra sem
  atualizar o `CHANGELOG.md` é bloqueado. Dispensa explícita pela label
  `sem-changelog`.
- `CONTRIBUTING.md` ganhou a seção "O CHANGELOG", com as seções do Keep a
  Changelog e exemplos de entrada boa e ruim.

### Corrigido
- **Instruções de instalação do PlatformIO.** O repositório mandava rodar
  `pip install platformio`, que **falha** no Ubuntu 24.04+ e Debian 12+: essas
  distribuições marcam o Python do sistema como *externally managed*
  (PEP 668) e recusam instalação global. Documentadas três alternativas que
  funcionam — `pipx`, venv dedicado com symlinks (sem `sudo`) e a extensão do
  VS Code — além do que evitar (`--break-system-packages`, `sudo pip`).

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
- 122 casos em 6 suítes Unity, rodando no PC em ~2 s
- **100% de cobertura de linhas e de funções** em `lib/`; 92,7% de ramos
- Dublês de teste header-only: `FakeClock`, `FakeTransport`, `FakeDisplay`,
  `FakeConfigStore` — com testes próprios, porque dublê com bug transforma
  todos os testes que o usam em teatro
- `test_obd_client`: prova que um pedido proibido não escreve **um único byte**
  no transporte, e não apenas que a função devolve erro
- Quatro padrões de invariante: varredura exaustiva (256 modos OBD2, todas as
  combinações estado × evento), fuzz determinístico (5.000 entradas no parser),
  corrupção simulada (flash 0xFF, 0x00 e 500 amostras de ruído) e verificação
  de silêncio (nenhum byte no transporte)
- Testes do comportamento defensivo contra enum corrompido — possível porque
  todos os `enum class` têm base `uint8_t`, então o cast é C++ legal, não UB
- Ambiente `native_coverage` para medir cobertura com gcov/gcovr

#### Infraestrutura
- CI no GitHub Actions com 5 jobs em todo PR: testes unitários, cobertura
  (falha abaixo de 100% de linhas), política de testes (PR que mexe em `lib/`
  sem mexer em `test/` é bloqueado, com dispensa explícita via label
  `sem-teste-necessario`), build do firmware e Conventional Commits
- Relatório de cobertura publicado no resumo do PR e como artefato HTML
- Hook local de `commit-msg` validando Conventional Commits
- `.gitignore`, `.editorconfig`, `.clang-format`, `.gitattributes`
- Template de Pull Request com checklist de segurança

#### Documentação
- `README.md` com arquitetura em Mermaid e justificativa da escolha do
  framework
- `docs/SAFETY.md` — requisitos de segurança, incluindo os elétricos
- `docs/TESTING.md` — política de testes obrigatória e os padrões de invariante
- `docs/ARCHITECTURE.md` — decisões de projeto, diagramas de estado e sequência
- `docs/HARDWARE.md` — veículo, placa, adaptador (ELM327 Placa Dupla VS1.5 /
  PIC18F25K80 confirmado, e por que ser um clone bom **aumenta** a importância
  da allowlist), alimentação e consumo parasita
- `docs/ROADMAP.md` — o que entra em cada versão
- `CLAUDE.md` — convenções do projeto
- `CONTRIBUTING.md` — fluxo de trabalho

### Ainda não existe

Bluetooth, sequência AT de inicialização, leitura real de PIDs, conversão para
unidades de engenharia, persistência na flash e display físico. Ver
[ROADMAP.md](docs/ROADMAP.md).

[Não lançado]: https://github.com/kawemaximo/kanri/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/kawemaximo/kanri/releases/tag/v0.1.0
