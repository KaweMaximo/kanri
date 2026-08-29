# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Versionamento segue [SemVer](https://semver.org/lang/pt-BR/).

## [Não lançado]

### Corrigido
- **O painel web mostrava um contador de retentativas obsoleto.** Ele guardava
  o último `[retry] tentativa N` visto no log e nunca limpava ao recuperar,
  exibindo `Polling / tentativa 32` — duas coisas que não podem ser verdade
  juntas.

  Custou investigação de verdade: essa mesma tela apontou um bug de backoff
  que **era real** (PR #19) e depois continuou apontando um que já não
  existia. Agora entrar em `Polling` zera o contador na tela, do mesmo jeito
  que o firmware zera o dele.

- **O painel congelava sempre que o Bluetooth estava tentando conectar.**
  Sintoma relatado: *"funciona somente conectado"*.

  `BluetoothSerial::connect()` segura o laço por até **10 s**, e cada leitura
  de PID por até 1 s. Nesse tempo o potenciômetro não era lido, o botão não
  respondia e o mostrador não redesenhava. Com o carro respondendo o laço gira
  rápido e tudo parecia bem; sem conexão, o painel morria por 10 s a cada
  tentativa.

  O painel virou uma **task no núcleo 0** (o laço do Arduino roda no 1). O
  Bluetooth pode bloquear à vontade que a tela continua viva — num aparelho
  fixo no carro, o brilho tem de responder com o adaptador fora do ar.

  Regra que mantém isso seguro: **todo acesso ao mostrador acontece na task
  do painel**. O console não escreve no SPI, deixa um pedido sob trava. Dois
  núcleos disputando o mesmo barramento dariam corrupção intermitente — do
  tipo que não se reproduz.

### Adicionado
- **Suavização dos valores no mostrador.** Relatado como *"pulando bastante,
  muito robótico"*.

  Não era ruído: o rodízio lê cinco PIDs, então cada medida chega a cada ~1 s
  e o número ficava parado um segundo e **saltava**. Era a taxa de
  atualização aparecendo na cara do motorista.

  A correção não foi ler mais rápido — o barramento OBD tem o ritmo que tem —
  e sim **andar** até o valor novo a 50 Hz, como um ponteiro faz por inércia.

  Com uma ressalva que importa: mudança **grande vai direto**. Suavizar tudo
  deixaria o painel lento, e pisar no acelerador vendo a rotação chegar um
  segundo depois é pior do que ver o número pular. O limiar acompanha a faixa
  física de cada grandeza, porque 100 é ruído em rpm e é enorme em graus.

- **Brilho em rampa.** Girar o potenciômetro pulava degraus inteiros de
  intensidade; agora caminha um passo do chip por quadro. A 50 Hz, atravessar
  a escala inteira do MAX7219 leva 0,3 s.

### Modificado
- **Potenciômetro passou para 8 níveis e ficou 7,5× mais rápido**, sem
  afrouxar nenhuma proteção.

  O atraso de 1,2 s não vinha das confirmações — vinha do **intervalo entre
  leituras** (`6 × 200 ms`). E o intervalo alto não comprava nada:
  `analogRead()` custa ~100 µs, ou seja 0,5 % do tempo mesmo a 20 ms.

  | | Antes | Agora |
  |---|---|---|
  | Níveis | 5 | **8** |
  | Intervalo | 200 ms | **20 ms** |
  | Confirmações | 6 | **8** |
  | Resposta ao giro | 1200 ms | **160 ms** |

  Mais rápido **e** com rejeição de pino solto mais forte. A lição vale além
  daqui: antes de afrouxar uma proteção para ganhar velocidade, confira se a
  lentidão vem mesmo dela.

  Os oito níveis caem em oito intensidades **distintas** do MAX7219 (0, 1, 2,
  3, 5, 7, 11, 15), com teste cobrando. Dois níveis na mesma intensidade
  dariam duas posições do botão com brilho idêntico — lido como zona morta,
  ou seja, defeito.

### Adicionado
- **Potenciômetro de brilho** no GPIO 36 (`VP`), com comando
  `pot` para conferir a fiação.

  Cinco e não dezesseis por decisão do Kawe, e o motivo é técnico: o ADC do
  ESP32 oscila ±40 contagens parado. Com 16 níveis o mostrador **pulsaria**
  com o botão imóvel; com 5, cada faixa tem ~819 contagens e o ruído some.

  Mas quantizar não basta sozinho, e o módulo trata **três** modos de falha:

  | Problema | Sintoma | Defesa |
  |---|---|---|
  | ruído do ADC | pulsa parado | 5 níveis |
  | ruído na fronteira | pisca entre dois brilhos | histerese de 150 |
  | pino solto | brilho passeia sozinho | 6 leituras ancoradas |

  O terceiro é o menos óbvio: o GPIO 36 não tem *pull-up* interno, então sem o
  fio ele vira antena. O discriminador não é o valor lido — é a
  **estabilidade**: potenciômetro é fonte de baixa impedância e oscila pouco,
  pino solto varre a escala inteira entre amostras.

  O teste de pino solto **reprovou a primeira versão**: com 3 confirmações,
  ruído uniforme conseguia mexer no brilho em minutos de operação. Foi o teste
  que definiu o número 6, não um chute.

  O potenciômetro só escreve no chip **na mudança de nível**. Sem isso ele
  desfaria o comando `brilho` cinco vezes por segundo — o mesmo defeito que o
  `seg` teve com a telemetria.

### Adicionado
- **Autoteste do mostrador**, pelos comandos `teste` e `seg <texto>` no
  console. Responde sem o carro e sem multímetro as três perguntas que a
  fiação de um display de 7 segmentos sempre levanta:

  1. **Todo segmento acende?** — um passo por segmento, isolado nos 3 dígitos.
     Um fio solto aparece como o sumiço de exatamente um traço.
  2. **Todo dígito acende?** — um passo por dígito.
  3. **A ordem dos dígitos está certa?** — mostra `123`. Se sair `321`, os
     fios estão todos bons e o painel mente em silêncio; a correção é uma
     linha no HAL, não um fio.

  A terceira é a que ninguém lembra de conferir.

  Um teste cobra que os oito passos cubram os oito segmentos **cada um
  exatamente uma vez** — um segmento que nenhum passo acende é um segmento
  cujo defeito o autoteste não encontra, e aí ele daria uma confiança que não
  tem.

- **Driver do MAX7219** — o firmware finalmente acende o mostrador do painel.
  Até aqui a única saída era o `SerialDisplay`, que desenha texto no console:
  a fiação podia estar perfeita e o display ficava apagado.

  O protocolo mora em `kanri_display/max7219.h`, em **função pura**, e o SPI
  em `src/hal/max7219_display.cpp`. A divisão importa porque o MAX7219 **não
  responde nada** — não há registrador de leitura nem ACK. Quando o painel
  mostra o número errado, o chip não tem como avisar, e do ponto de vista do
  firmware tudo correu bem. Só teste no PC pega isso.

- **Fonte de 7 segmentos** com os 32 caracteres do alfabeto do painel, e um
  teste que **confronta as duas fontes de verdade**: `is_renderable()` consulta
  a string `kAlfabeto`, `encode_char()` consulta a tabela `kFont` — escritas
  separadamente. Se divergirem em um único caractere entre os 128, uma delas
  está mentindo.

- **Botão no GPIO 17** percorre as medidas do painel; toque longo volta para a
  primeira. Antirrebote pelo `Button` que já existia em `kanri_core`.

- `ISevenSeg`: a porta do mostrador de três dígitos. Irmã de `IDisplay`, não
  substituta — texto e sete segmentos são saídas de naturezas diferentes, e as
  duas continuam existindo.

- `blink_visible()` resolve o piscar do alerta de motor quente, com teste na
  virada do contador de 49,7 dias do `millis()`. O aparelho fica ligado no
  carro, então a virada acontece — e um alerta não pode parar de piscar nela.

### Modificado
- O brilho inicial do mostrador nasce em **30 %**, não no máximo. Na bancada o
  MAX7219 divide os 500 mA do USB com o ESP32, e um mostrador em brilho total
  puxa perto de 300 mA. Se a linha cair, quem reinicia é o ESP32 — e o log diz
  `BROWNOUT`. O registrador de intensidade é *duty cycle*, então o número corta
  a corrente média de verdade, não só o brilho percebido.

### Adicionado
- **Mapa de pinos da ESP32 DevKit V1 (30 pinos)** em `docs/HARDWARE.md`, com o
  acionamento do display pelo MAX7219 em três fios (`DIN` 23, `CLK` 18,
  `LOAD` 5) como caminho escolhido, e a alocação direta pelos GPIOs registrada
  como plano B.

  Registra que **`LOAD` e `CS` são o mesmo pino** — o 12 chama-se `LOAD` no
  MAX7219 e `CS` no MAX7221. Quem lê "DIN, LOAD e CS" como três fios esquece o
  `CLK`, e o display fica apagado sem dar erro nenhum.

  Separa o que **não pode** ser usado por motivo físico do que é só
  preferência: `RX0`/`TX0` derrubam o console, o `D12` impede o boot, e
  `D34`, `D35`, `VP` e `VN` são *input-only* — `pinMode(34, OUTPUT)` compila,
  roda, não dá erro e o pino nunca muda de nível.

  Inclui o orçamento de corrente dos pinos de dígito comum, onde passa a
  **soma** dos segmentos acesos: com resistor de 220 Ω dá 48 mA, acima do
  máximo absoluto de 40 mA do GPIO. Com 1 kΩ dá 10 mA. É o número que torna o
  resistor de 1 kΩ obrigatório no acionamento direto, e não uma escolha de
  brilho.

### Corrigido
- **A ordem dos dígitos estava espelhada.** Para exibir `18.3` era preciso
  digitar `seg 38.1`.

  O ponto decimal acompanhava o dígito certo, o que descartou a fonte e
  apontou direto para o mapeamento posição → registrador: o `Digit0` do chip
  aciona o dígito da **esquerda** nesta ligação, não o da direita.

  A conta estava **duplicada** em `render()` e `render_raw()` — o lugar
  clássico onde uma cópia é corrigida e a outra fica para trás. Virou
  `digit_register()`, função pura com teste, e a orientação da fiação virou
  uma constante nomeada: é a única coisa a mudar se a próxima montagem for ao
  contrário.

- **O comando `seg` só piscava o valor.** A telemetria redesenha o mostrador
  10 vezes por segundo e apagava o texto escrito à mão em 100 ms. O operador
  via o valor aparecer e sumir, e concluía que o display estava com defeito —
  quando na verdade os dois donos da tela estavam brigando por ela.

  Agora `seg <texto>` **segura** a tela. Solta com o comando `auto`, com um
  toque no botão (um toque quer dizer "quero ver o carro"), ou rodando o
  `teste`.

### Adicionado
- Comando `auto`: devolve o mostrador à telemetria.
- **Teste que cobra a documentação de todo comando.** `teste`, `seg` e `auto`
  foram adicionados, funcionavam, e não apareciam nem na `ajuda` do firmware
  nem na dica do painel — comandos que só quem escreveu conhecia.

  A regra agora é de máquina: toda **ação** do parser precisa ter ao menos uma
  de suas palavras citada em `help_lines()`. Por ação e não por palavra, porque
  `help` e `ajuda` fazem a mesma coisa e documentar as duas seria ruído.

  O teste inverso também entrou: toda palavra listada precisa ser aceita pelo
  parser. Ajuda que documenta comando inexistente é pior do que ajuda faltando.

### Corrigido
- A `ajuda` não se listava, e os três comandos do mostrador não estavam nela.
  A dica do campo de comando do painel também passou a citá-los.

### Corrigido
- **O comando `brilho` não chegava ao MAX7219.** Ele alterava as configurações
  e respondia *"aplicado"*, mas o `main.cpp` nunca repassava o valor ao chip —
  o mostrador ficava nos 30 % iniciais para sempre.

  Importa mais do que estética: no MAX7219 a intensidade é *duty cycle*, ou
  seja, é o **controle de corrente**. Quando a fonte está no limite, "responde
  que aplicou e não muda nada" é exatamente o que não se quer.

  Mesma família do backoff preso no teto: o método existia e era testado, e a
  cola no `main.cpp` esquecia de chamá-lo.

- **O backoff de reconexão ficava preso no teto de 30 s para sempre.** O
  `main.cpp` nunca chamava `RetryPolicy::on_success()`, então o contador de
  tentativas só crescia durante toda a vida do aparelho.

  Encontrado em campo, no carro, em 29/08/2026: o firmware rodou 20 minutos,
  perdeu e recuperou o link 28 vezes, e o painel mostrava *"Tentativa #24,
  próx. retry 30000 ms"* enquanto o estado era `Polling` — ou seja, operando
  normalmente, mas com a próxima queda já condenada a **30 segundos de tela
  apagada** em vez de 1 segundo.

  Num aparelho que vai ficar permanentemente no carro, isso é a diferença
  entre uma piscada e meio minuto sem informação no painel.

### Adicionado
- `entered_operation()` e `left_operation()` em `state_machine.h`: predicados
  **de transição** (dependem do par anterior/atual, não só do estado atual).

  Existem para tirar do `main.cpp` — a única parte do projeto sem teste — as
  decisões que dependem de "de onde viemos". Comparar estados na mão ali é
  barato de escrever e caro de descobrir: foi exatamente assim que o backoff
  quebrou. É a **segunda** vez que um defeito desta família nasce nesse
  arquivo; a primeira está documentada em `retry_policy.h`.

  Cobertos por varredura de todos os pares de estados, com a expectativa
  escrita de forma independente da implementação, mais um teste de regressão
  que percorre cinco ciclos de "caiu e voltou" e cobra o backoff no valor base.

- **Catálogo de PIDs expandido de 18 para 50 entradas**, cobrindo ajuste de
  combustível (os quatro *fuel trims*), pressão de combustível e do rail,
  EGR, purga do canister, pressão evaporativa e barométrica, temperatura dos
  quatro catalisadores, consumo instantâneo, teor de etanol, e os contadores
  de distância e tempo desde a última limpeza de códigos.

  Os contadores são úteis no diagnóstico de um jeito que não é óbvio:
  respondem *"o defeito voltou depois de quantos quilômetros?"*.

### Modificado
- **O decodificador passou a ser dirigido pela tabela.** A fórmula e a faixa
  física de cada PID agora ficam na mesma linha do PID que elas decodificam,
  em vez de num `switch` separado.

  Com ~50 entradas, aquele `switch` viraria centenas de linhas em que é fácil
  colar a fórmula errada no PID vizinho — e o resultado seria um número
  plausível e errado, o pior tipo de defeito.
- `formula_byte_count()` virou pública. Ela é a fonte de quantos bytes cada
  fórmula lê, **independente do `expected_bytes` da tabela** — se viessem do
  mesmo lugar, um erro de digitação faria a fórmula ler um byte que não
  chegou. Um teste confronta os dois para todas as entradas.

### Documentação
- **Armadilha registrada: MODO não é PID.** O número `0x2E` aparece nos dois
  lugares significando coisas opostas — o **modo** `0x2E` é UDS
  `WriteDataByIdentifier` (escrita, proibido), e o **PID** `0x2E` dentro do
  Modo 01 é o comando de purga do canister (leitura pura, permitido). O mesmo
  vale para `0x31`.

  Confundi-los levaria a bloquear uma leitura legítima — ou, muito pior, a
  liberar uma escrita achando que é PID. Há um teste que fixa os dois
  comportamentos lado a lado.


### Adicionado
- **Leitura de códigos de falha no firmware.** Comando `dtc` no console, e uma
  leitura automática ao conectar — é a informação que se quer saber assim que
  o aparelho liga, e que não muda a cada segundo.
- **Aba de Diagnóstico no Kanri Console**, com um cartão por tipo de código.

  Ela distingue **três estados**, e confundi-los levaria à conclusão errada:
  *ainda não consultado*, *nenhum código* (boa notícia, em verde) e *não foi
  possível ler* (em âmbar — não sabemos). Uma lista vazia por falha de leitura
  parecendo "carro sem defeito" seria pior do que não mostrar nada.

  A tela também diz o que o aparelho **não** faz: *"somente leitura — este
  aparelho não apaga códigos"*.
- `parse_mode_response()` no parser, para os modos pedidos **sem PID**.
  Os modos de DTC são enviados sozinhos (`03`, não `0300`) e a ECU responde
  sem ecoar PID — o segundo byte é a **contagem** de códigos. Usar o parser
  normal faria ele cobrar um PID inexistente e rejeitar a contagem como se
  fosse PID errado.

### Modificado
- O `FakeElm327` passa a responder a modos pedidos sozinhos, para que o
  diálogo de DTC seja testável sem carro.
- Segunda exclusão `GCOVR_EXCL` do projeto, em `read_dtcs()`: a checagem da
  allowlist é inalcançável hoje, porque `mode_for()` só devolve modos
  permitidos por construção. **A guarda fica** — é a mesma porta que todo
  caminho de saída atravessa, e é ela que impede um `DtcKind` novo de ir ao
  barramento sem passar pela allowlist. Remover uma barreira porque "hoje ela
  não dispara" é exatamente como barreiras somem de projetos.


### Adicionado
- **Escopo ampliado para todos os modos OBD2 de leitura.** Antes só `0x01` e
  `0x09`; agora também `0x02` (freeze frame), `0x03`/`0x07`/`0x0A` (códigos de
  falha gravados, pendentes e permanentes), `0x05` (sonda O2) e `0x06`
  (monitores de bordo).

  **A regra de segurança não mudou** — a linha nunca foi "quantos modos", e sim
  *ler versus alterar*. Nenhum dos modos acrescentados escreve. `0x04` (limpar
  códigos) e `0x08` (comandar atuadores) seguem proibidos, e o teste exaustivo
  dos 256 modos foi reescrito para exigir **exatamente os oito de leitura, nem
  um a mais**.

  A lista esperada está no teste de forma **independente da implementação**:
  mudar `is_read_only_mode()` sem tocar no teste faz o CI falhar. É a decisão
  mais importante do projeto, e não deve ser possível alterá-la em silêncio.
- **Decodificador de códigos de falha** (`kanri_obd/dtc`). Os dois bytes crus
  viram `P0301`, com a letra do sistema (P/C/B/U) e os quatro dígitos.

  Distingue os três estágios, que significam coisas diferentes no diagnóstico:
  **gravado** (acendeu a luz), **pendente** (aconteceu uma vez, ainda não
  confirmado) e **permanente** — este último existe justamente para resistir ao
  Modo 04 e só some quando a ECU confirma que o defeito acabou.
- O `0x22` (UDS `ReadDataByIdentifier`, que leria PIDs proprietários da
  Mitsubishi) fica **fora por decisão registrada, não por esquecimento**: ele é
  leitura, mas exige `ATSH` para endereçar a ECU — e o `ATSH` permite montar
  qualquer quadro CAN, inclusive de escrita. Trocaria uma garantia estrutural
  por disciplina.

### Corrigido
- **Leitura fora do buffer em `parse_dtc_response()`.** O campo `length` é um
  `uint8_t` e pode dizer até 255, mas `data` tem 32 bytes. O parser garante
  essa relação, mas a função é pública — um frame montado à mão, ou um campo
  corrompido em memória, faria o laço ler além do vetor. Agora o tamanho é
  preso ao buffer real, com teste.
- **Um teste travava a suíte inteira.** Ao permitir os modos de DTC, o
  `ObdClient` passou a de fato aguardar resposta do `FakeTransport` — que
  nunca responde **e não avançava o relógio falso**. O laço de timeout
  esperava para sempre: 99,9% de CPU, sem mensagem de erro.

  O dublê agora adianta o relógio a cada consulta à porta, como o
  `FakeElm327` já fazia. Um teste que **trava** é pior que um que falha: ele
  não diz onde está o problema e segura tudo atrás dele.
- Teste do decodificador de DTC esperava `PFFFF`, que **não existe**: o
  primeiro dígito vem de apenas dois bits e nunca passa de 3. O código estava
  certo; o teste é que estava errado. Acrescentada a invariante que cobra isso
  para todas as 65.536 entradas possíveis.


### Adicionado
- **Lógica do mostrador de 7 segmentos** (`kanri_display/seven_seg`), para o
  painel do carro. Referência de produto: FuelTech WB-O2 Nano — um número
  grande, legível de relance.

  Três dígitos mostram **uma medida por vez**, e a precisão possível depende
  da grandeza: `9.52 V` cabe com duas casas, `13.8 V` com uma, `120 km/h` com
  nenhuma. A escolha é automática, sempre pela maior precisão que couber —
  fixar duas casas desperdiçaria dígito na velocidade, e fixar zero jogaria
  fora a precisão da tensão, que é justamente onde ela importa.

  Rotação não cabe em dígitos inteiros e vai em **milhares** (`1.73` para
  1726 rpm), como num tacômetro digital.
- **Debounce do botão** (`kanri_core/button`), com clique e toque longo.
  Contato mecânico treme: lido cru, um toque vira cinco, e o motorista veria a
  grandeza pular sem entender. Reproduzir tremulação de propósito no hardware
  é difícil; como função pura do tempo, cada padrão virou um teste de
  microssegundos — inclusive ligar o aparelho **com o botão já pressionado**,
  que não pode gerar clique fantasma.

### Corrigido
- **O sinal de menos não era descontado** na conta de quantos dígitos cabem:
  −40 °C virava `-40.` — com um ponto solto, porque a casa decimal não tinha
  onde caber. Pego por teste.
- **O rótulo da temperatura era indesenhável.** `AGU` tem a letra G, que não
  tem forma reconhecível em 7 segmentos e viraria um borrão — o motorista
  leria a grandeza errada. Trocado por `tEP`, e há um teste que cobra o
  alfabeto de todos os rótulos.
- **O firmware compilava como C++11.** O framework Arduino injeta
  `-std=gnu++11` **depois** das nossas flags, e a última vence. As variáveis
  `inline` dos catálogos viravam warning e funcionavam por sorte do linker,
  não por garantia da linguagem. Corrigido com `build_unflags`.


> **Primeira leitura real de um veículo.** Em 29/08/2026 o Kanri conectou ao
> ELM327 de um Mitsubishi Lancer 2.0 2014 e leu o motor em funcionamento:
> 933–968 rpm em marcha lenta, temperatura subindo de 46 °C a 75 °C, tensão de
> 14,2–14,4 V com o alternador carregando (contra 12,1 V só na bateria),
> borboleta em 13,3 % e velocidade em 0 km/h.
>
> A auditoria de barramento registrou, em 30 s de operação: 224 comandos,
> **todos de leitura**, e **zero comandos de escrita**.

### Adicionado
- **Dashboard de telemetria** no Kanri Console, em aba própria. Um *stat tile*
  por medida, com valor grande, faixa mín/máx e sparkline do histórico.

  Rotação, temperatura e tensão têm escalas incompatíveis — juntá-las num
  gráfico exigiria dois eixos verticais, que distorcem a comparação. A forma
  correta é *small multiples*: cada medida no seu próprio gráfico, com a mesma
  cor de série.

  A cor só carrega significado na temperatura, e **nunca sozinha**: cada faixa
  vem com rótulo textual (`normal`, `aquecendo`, `atenção`, `crítico`), porque
  em modo claro o amarelo de aviso fica abaixo de 3:1 de contraste. Há também
  tabela com os números exatos.
- **Barra de vivacidade** no dashboard. Com o carro parado, rotação e
  velocidade ficam em 0 e a tensão não se move — sem um indicador de "chegou
  leitura nova", um painel correto e um painel congelado são indistinguíveis.
- **Batida periódica do firmware** (`[hb]`) a cada 3 s, com estado e
  contadores. As linhas `[estado]` só aparecem numa transição, então quem
  abrisse o painel no meio de uma sessão estável não saberia onde o firmware
  estava.
- **Auditoria de barramento**: cada comando é registrado como `[audit] -> 010C`
  pouco antes de ir para o transporte. A garantia read-only já era imposta por
  `safety.h` e testada nos 256 modos, mas quem está com o carro na frente vê o
  log, não os testes. O registro é completo por construção — `write_command()`
  é o único ponto do firmware que escreve no transporte.
- **Conexão direta por MAC**, dispensando a varredura: **1,5 s contra 32 s**,
  medido no carro. O ELM327 foi medido a −80/−90 dBm, no limite da detecção, e
  a varredura do ESP32 não o encontrava; a conexão direta funciona porque não
  depende de captar o anúncio no intervalo certo.

### Corrigido
- **Loop de reboot ao conectar.** `BluetoothSerial::connect()` bloqueia até
  ~10 s e a sequência AT outro tanto; o watchdog é de 8 s. O aparelho achava o
  adaptador e reiniciava antes de conversar com ele. Operações longas e
  conhecidas passam a rodar fora da vigilância do watchdog, de forma
  cirúrgica — afrouxá-lo para 20 s enfraqueceria a proteção o tempo todo.
- **A legenda do motivo do reset estava errada**, e apontou para a causa
  errada durante a depuração: o código 6 é `ESP_RST_TASK_WDT`, não brownout
  (que é 9).
- **O comando `timeout` não fazia nada**: o `elm_timeout_ms` nunca chegava ao
  `ObdClient`. Um ajuste que o usuário faz e que não muda nada é pior do que
  não existir.
- **A primeira leitura falhava com `STOPPED`.** Com `ATSP0`, a primeira
  requisição não é só uma leitura: o ELM327 precisa *descobrir* o protocolo do
  carro, testando um a um. A primeira leitura após conectar agora tem prazo
  próprio.
- **O dashboard não se atualizava sozinho** — só ao trocar de aba. Faltava o
  timer.

### Adicionado
- **Descoberta de PIDs suportados** (`kanri_obd/pid_support`). O catálogo em
  `obd_pid.h` é o que o Kanri *sabe pedir* — não é o que o Lancer *sabe
  responder*. Agora perguntamos à ECU, pelos PIDs `0x00`, `0x20` e `0x40`, e
  o rodízio de leitura pula o que ela não implementa.

  Sem isso, o firmware pede PIDs inexistentes e recebe `NO DATA` a cada ciclo.
  Não quebra nada — o parser trata —, mas desperdiça banda do barramento: num
  rodízio de 5 PIDs, um PID inútil custa 20% das leituras.

  Os blocos são encadeados: o último bit de cada um diz se vale perguntar o
  próximo, e parar quando ele está desligado economiza duas consultas em toda
  partida. Se a ECU não responder ao mapa, seguimos com o catálogo completo —
  melhor tentar e receber alguns `NO DATA` do que não ler nada.

  O mapa é esquecido ao perder o link: a próxima conexão pode ser outro carro,
  e herdar o mapa anterior faria o firmware pular PIDs que existem.
- **Dashboard de verdade** (v0.3 parcial). `build_frame()` deixa de ser
  esqueleto e monta quatro telas: **Splash** (nome e versão), **Connecting**
  (etapa atual e — importante — *quem* estamos procurando, porque nome
  configurado errado é o erro mais comum na primeira instalação),
  **Dashboard** (rotação, temperatura, velocidade, tensão) e **Error** (o que
  houve, qual tentativa, e **contagem regressiva** até a próxima).

  Duas regras que valem tanto quanto os números:

  - **Medida velha vira `--`.** Não basta o valor ser válido: acima de 3 s
    ele deixa de ser "atual". Um valor de 10 s atrás exibido como agora
    engana tanto quanto um valor inventado.
  - **`--` aparece sem unidade.** `-- rpm` sugere uma medida que não existe.

  E o alerta de motor quente (≥105 °C) **não dispara com dado velho** — seria
  assustar por uma leitura que não vale mais.
- **Formatação própria de números** (`kanri_display/text_format`), no lugar do
  `snprintf`. Duas razões: o `snprintf` com float arrasta ~10 KB de flash, e
  estas funções cabem em 40 linhas que dá para auditar — nunca escrevem além
  do buffer, sempre terminam em nulo, não alocam.

  O valor fica alinhado à direita de propósito: número que muda de largura
  (999 → 1000) pulando na tela é difícil de ler de relance, que é como se lê
  um painel dirigindo.
- **Configuração em runtime pelo console serial.** O nome Bluetooth do
  adaptador ELM327 varia entre modelos (`OBDII`, `V-LINK`, `Android-Vlink`).
  Sem isso, descobrir que o seu se chama diferente exigiria recompilar e
  regravar o firmware — dentro do carro, com o notebook no colo. Agora é
  digitar uma linha:

  ```
  nome V-LINK
  save
  ```

  Comandos: `nome`, `mac`, `pin`, `intervalo`, `timeout`, `brilho`,
  `unidades`, `save`, `load`, `padroes`, `status`, `scan`, `reiniciar`,
  `ajuda`. Aceita português e inglês, com `set` opcional.

  A interpretação da linha é lógica pura em `kanri_config/command_parser`,
  com teste; ler do Serial e aplicar é hardware, e fica no `main.cpp`.

  Um valor fora de faixa é **recusado**, não ajustado em silêncio — quem
  digitou precisa saber que não valeu. E uma invariante garante que nenhum
  comando, com nenhum argumento, deixa a configuração inválida.
- **Persistência real na flash** (`NvsConfigStore`). A struct é gravada como
  um blob único: uma escrita só (a flash tem ciclos contados) e sem estado
  intermediário com metade dos campos novos.

  O preço é que o layout da struct vira formato de arquivo — por isso
  `schema_version` existe, e por isso **toda leitura passa por
  `clamp_to_valid()`**: uma queda de tensão durante a gravação, comum em 12 V
  automotivo, deixa lixo ali.

  Verificado no hardware: configurar → `save` → reiniciar → a configuração
  volta da flash.
- **Campo de comando no Kanri Console**, com histórico por seta ↑/↓. Fecha o
  ciclo: dá para configurar o adaptador pelo painel, sem terminal serial.
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

### Removido
- A sobrecarga `build_frame(telemetry, state, metric)` e o auxiliar
  `connecting_label()`. A medição de cobertura mostrou que ambos eram **código
  morto**: ninguém chamava a sobrecarga, e o `default:` do auxiliar era
  inalcançável porque a função só era chamada de dentro dos casos que ela
  tratava. Código que existe só para calar o compilador é dívida.

### Modificado
- A largura da moldura do `SerialDisplay` passa a vir de `kFrameTextLen`, e
  não de um número digitado. Estavam divergindo em um caractere, e os valores
  alinhados à direita vazavam para fora da borda.
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
