# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Versionamento segue [SemVer](https://semver.org/lang/pt-BR/).

## [Não lançado]

### Adicionado
- **Cliente OBD2 completo** (`ObdClient`): sequência AT de inicialização,
  envio de comando, leitura até o prompt `>` com timeout, e retentativa
  apenas para falhas passageiras — insistir num `NO DATA` (a ECU não tem o
  PID) só gastaria banda do barramento.
- **Decodificação de PIDs** (`kanri_obd/pid_decoder`) com as fórmulas da SAE
  J1979: rotação, temperaturas, velocidade, borboleta, MAP, MAF, tensão do
  módulo, avanço de ignição e tempo de motor ligado.

  Com **duas barreiras, não uma**: aplicar a fórmula não basta. Um frame pode
  passar pelo parser — hexadecimal válido, modo e PID corretos, tamanho certo
  — e ainda conter um valor impossível, porque ruído elétrico faz exatamente
  isso. Todo valor decodificado passa por uma faixa física do 4B11: 16.383 rpm
  é o que a fórmula permite, mas não é uma medida — é ruído, e não vai para a
  tela.
- **Simulador de ELM327** para testes (`test/helpers/fake_elm327.h`). Responde
  como um adaptador de verdade: demora, termina com `>`, diz `NO DATA` para
  PID que não conhece, ecoa o comando quando configurado, prefixa
  `SEARCHING...`, e sabe ficar mudo, corromper a resposta ou falhar na escrita.

  Ele adianta o relógio falso a cada consulta à porta — é o que permite
  exercitar um timeout de 1 segundo em microssegundos, e testar "o adaptador
  ficou mudo no meio da leitura" sem provocar a falha no hardware.
- **Ciclo de leitura** no firmware: rodízio de PIDs um por vez, respeitando
  `poll_interval_ms`. Ler todos de uma vez congelaria o LED e a tela por quase
  meio segundo.
- **Varredura Bluetooth real** (`BtSerialTransport`). O ESP32 procura o
  adaptador no ar, lista o que encontrou com nome, MAC e potência de sinal, e
  entrega a lista para a lógica decidir.
- **Escolha do adaptador** (`kanri_obd/adapter_matcher`), com regras
  explícitas: MAC tem prioridade **e é exclusivo** (não cai para o nome se o
  MAC não aparecer); nome casa de forma exata ignorando caixa; empate de nome
  desempata pelo sinal mais forte; dispositivo com nome ou MAC malformado é
  ignorado. Nome de dispositivo Bluetooth é escolhido por quem anuncia — é
  entrada hostil, e tratada como tal.
- **LED de status como canal de comunicação** (`kanri_core/led_pattern`).
  Dentro do carro não há monitor serial; o LED é o que se vê. Cada estado tem
  um padrão próprio: busca pisca rápido e contínuo; a conexão progride com
  2, 3 e 4 piscadas conforme se aproxima de operar; operando é um heartbeat
  discreto a cada 2 s; degradado pisca lento; falha terminal fica aceso fixo —
  o único padrão que não pisca, reconhecível de relance.
- `[boot] motivo do reset` no log: distinguir "liguei na tomada" de "o
  watchdog me reiniciou" é o que torna visível um bloqueio no loop.
- Primeira exclusão `GCOVR_EXCL` do projeto, em `adapter_matcher.cpp`: o
  retorno defensivo de `iguais_sem_caixa()` é inalcançável pelo caminho
  público, porque `utilizavel()` já rejeitou dispositivos sem terminador antes
  da comparação. A justificativa está escrita no código, como exige
  [docs/TESTING.md](docs/TESTING.md).
- `board_build.partitions = huge_app.csv`: a pilha Bluetooth leva o binário de
  270 KB para 1,1 MB, que não cabe com folga na tabela padrão. Abre-se mão da
  partição de OTA, que não está no roadmap.

### Modificado
- `contem_sem_caixa()` e `decode()` tinham cada um **duas defesas para o mesmo
  caso**. A medição de cobertura expôs a redundância: uma das duas nunca era
  alcançada. Ambas foram simplificadas para um único ponto de decisão — no
  decodificador, a checagem de PID ficou junto das fórmulas que ela protege,
  onde não pode divergir delas.

### Corrigido
- **A varredura bloqueava o loop por 5 segundos.** `BluetoothSerial::discover()`
  é síncrono, e durante ele o `loop()` não roda — com duas consequências: o
  LED congelava exatamente durante a busca (o oposto do que deveria mostrar)
  e o watchdog não era alimentado. Medido no hardware: **o ESP32 reiniciava a
  cada ~28 s**. Trocado por `discoverAsync()` com o tempo controlado no loop.
  Depois da correção: 56 s contínuos sem reboot, `motivo do reset` sempre
  power-on, varreduras de 5,0 s cronometradas e backoff de 1, 2, 4, 8, 16 s
  cumprido à risca.
- **Log serial saía cortado.** Com `CORE_DEBUG_LEVEL=3`, a pilha Bluetooth
  escrevia na Serial a partir de outra task e intercalava com o nosso log,
  partindo linhas ao meio (`MICRO88  70:08:94:ec[ 11752][I][BluetoothSerial...`).
  Reduzido para nível 1 (apenas erros).

Próxima versão planejada: [v0.2.0 — Conexão e leitura de PIDs](docs/ROADMAP.md#-v020--conexão-e-leitura-de-pids).

### Adicionado
- **Kanri Console** (`./start.sh`) — painel web local de desenvolvimento, em
  `tools/kanri-console/`. Mostra o estado da máquina de estados do firmware ao
  vivo (extraído dos logs seriais), o log com carimbo de tempo, e oferece
  botões para gravar firmware, reiniciar a placa, compilar, rodar os testes e
  detectar o chip.

  Detalhe que faz funcionar: a porta serial é exclusiva de um processo. Ao
  gravar, o painel fecha a serial, roda o `pio` transmitindo a saída, e
  reabre — sem isso o upload falharia com *"could not open port"*.

  Escuta **apenas em `127.0.0.1`**: o painel executa comandos na máquina, e
  expor isso na rede sem autenticação deixaria qualquer um no mesmo Wi-Fi
  gravar firmware no ESP32.

  Sem dependência nova de execução: usa a biblioteca padrão do Python mais o
  `pyserial` que já vem com o PlatformIO. Logs em tempo real via Server-Sent
  Events.

  Interface com Tailwind CSS **vendorizado** (`vendor/tailwind.js`), não via
  CDN: a ferramenta precisa funcionar offline, que é exatamente onde o ESP32
  vai estar — garagem, carro. Ícones são SVG inline (Lucide, ISC), pelo mesmo
  motivo. Layout de tela cheia, sem rolagem de página: a rolagem acontece
  dentro do log.

  O log classifica cada linha (`estado`, `retry`, `sistema`, `serial`,
  `display`, `cmd`) e permite filtrar por categoria. A moldura ASCII que o
  `SerialDisplay` redesenha a cada 500 ms vem **desligada por padrão** — sem
  isso ela afoga o log. Linhas idênticas consecutivas são agrupadas com um
  contador `×N`.
- `start.sh` na raiz: encontra sozinho um Python que tenha `pyserial` (não é
  óbvio no Ubuntu 24.04+, onde ele costuma existir só dentro do venv do
  PlatformIO) e explica como instalar quando não encontra.
- O job `CHANGELOG atualizado` passa a cobrir também `tools/**` e `start.sh`.
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
- **Backoff pulava o intervalo base.** Na primeira falha de conexão o firmware
  esperava 2 s em vez de 1 s, e a sequência real era 2, 4, 8, 16 s — não a
  1, 2, 4, 8 s que `retry_policy.h` documentava. O `main.cpp` chamava
  `on_failure()` **antes** de ler `current_delay_ms()`, então a primeira falha
  já dobrava o intervalo e o valor base nunca era usado.

  A `RetryPolicy` estava correta e testada; o erro estava na **ordem de uso**,
  que morava no `main.cpp` — a única parte do projeto sem teste. Os 122 testes
  passavam. O bug só apareceu ao gravar o firmware no ESP32 e ler o log serial.

  Corrigido com `RetryPolicy::record_failure()`, que lê o intervalo e avança o
  contador numa única chamada: não existe mais ordem para errar. Coberto por
  três testes de regressão.
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
