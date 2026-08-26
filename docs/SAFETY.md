# Segurança — Kanri

> **Este documento não é opinião nem sugestão. É requisito.**
> Toda alteração no Kanri é avaliada contra o que está aqui. Se uma mudança
> conflita com este documento, a mudança está errada — não o documento.

O Kanri se pluga na porta de diagnóstico de um carro que anda em via pública,
e é alimentado pela rede elétrica desse carro. Isso muda a régua: um bug aqui
não é um `500` num endpoint, é um veículo em movimento.

---

## 1. Somente leitura — a regra que não se negocia

O firmware **nunca** escreve nada na ECU. Apenas dois modos OBD2 são
permitidos:

| Modo | O que é | Por que é permitido |
|------|---------|---------------------|
| `0x01` | Dados do instante (RPM, temperatura, pressão…) | Leitura pura |
| `0x09` | Informação do veículo (VIN, calibração) | Leitura pura |

### O que está proibido, e por quê

| Modo | O que faz | Risco |
|------|-----------|-------|
| `0x04` | **Limpa os códigos de falha (DTCs)** | Apaga o histórico de diagnóstico do carro. Perde-se a informação que o mecânico usaria. Irreversível. |
| `0x08` | **Comanda atuadores** | Aciona componentes do motor por comando. Com o carro andando, é risco físico. |
| `0x2E`, `0x31`, `0x3E` | Serviços UDS: escrever memória, rodar rotinas, manter sessão de diagnóstico | Podem reprogramar módulos. Fora do OBD2 padrão e fora do escopo deste projeto. |
| `0x02`, `0x03`, `0x05`–`0x07`, `0x0A` | Leituras de freeze frame, DTCs, testes | Não escrevem, mas estão **fora de escopo**. A allowlist é fechada: o que não foi decidido, não entra. |

### Como a regra é imposta — não por confiança, por código

Documentação não compila. A regra vive em três camadas:

**Camada 1 — `lib/kanri_obd/include/kanri_obd/safety.h`**
Função `check_obd_request(mode, pid)`. Toda requisição passa por ela antes de
um único byte ir para o transporte. O padrão é **negar**.

**Camada 2 — allowlist de PIDs em `obd_pid.h`**
Mesmo dentro do modo `0x01`, só pedimos PIDs que estão explicitamente na
tabela. Defesa em profundidade: se a barreira de modo falhar, esta segura.

**Camada 3 — `test/test_safety_guard/`**
Um teste percorre **todos os 256** valores possíveis de modo e exige que
apenas `0x01` e `0x09` passem. Outro teste varre a tabela de PIDs e falha se
alguém acrescentar uma entrada com modo de escrita.

> Consequência prática: se alguém — você daqui a seis meses, um colaborador,
> ou uma IA numa sessão futura — tentar habilitar o Modo 04, o CI fica
> vermelho e o merge é bloqueado. A regra passa a ser cobrada pela máquina.

### Comandos AT: allowlist, nunca blocklist

Comandos `AT` configuram o adaptador ELM327, não a ECU. Ainda assim, alguns
permitem montar quadros arbitrários no barramento e por isso ficam de fora.

**Bloqueado, com o motivo:**

| Comando | Por que está fora |
|---------|-------------------|
| `ATSH xx yy zz` | Define o header CAN. Com ele dá para endereçar **qualquer** módulo e enviar **qualquer** serviço, incluindo escrita. É a porta dos fundos que anularia todo o resto. |
| `ATCRA`, `ATCF` | Filtros de recepção; só fazem sentido junto com `ATSH`. |
| `ATMA`, `ATMR` | "Monitor all": inunda o canal e nos faz perder respostas. |
| `ATPP xx SV yy` | Grava parâmetros permanentes no ELM327. É escrita, e pode deixar o adaptador inutilizável. |
| `ATBI` | Pula a inicialização do barramento. |
| `ATTP xx` | Força protocolo sem verificação. `ATSP0` (automático) é melhor. |

**Por que allowlist e não blocklist?** Com blocklist, esquecer de listar um
comando perigoso significa que ele passa. Com allowlist, esquecer de listar
um comando inofensivo apenas o bloqueia — o erro cai para o lado seguro.

---

## 2. Toda resposta do adaptador é entrada hostil

O ELM327 é um chip serial que fala texto. Na prática ele responde:

```
41 0C 1A F8       <- o que a gente espera
NO DATA           <- a ECU não suporta esse PID
SEARCHING...      <- ainda negociando o protocolo
UNABLE TO CONNECT <- ignição desligada / sem barramento
CAN ERROR         <- problema elétrico
BUFFER FULL       <- perdemos dados
?                 <- não entendeu o comando
STOPPED           <- operação abortada
<lixo binário>    <- clone barato com contato ruim
```

**Nada é tratado como válido antes de passar por
`lib/kanri_obd/src/elm327_parser.cpp`.** Essa é a fronteira de confiança do
firmware.

### Garantias do parser

| Garantia | Por que importa |
|----------|-----------------|
| Nunca lê fora dos limites do buffer | Leitura fora dos limites em firmware não dá exceção — dá dado errado na tela ou travamento |
| Não usa `strlen` no buffer cru | Um `\0` no meio da resposta faria o parser parar antes do dado bom |
| Zero alocação dinâmica | Sem `String`, `malloc` ou `new`. Heap fragmentado falha num momento imprevisível |
| Rejeita modo/PID que não casam com o pedido | Uma resposta **atrasada** de um pedido anterior seria exibida como a medida atual |
| Confere o tamanho do payload contra a tabela | Resposta bem formada mas com tamanho errado é suspeita |
| Limite explícito de entrada (256 B) e de linha (96 B) | Contém adaptador defeituoso despejando dados sem fim |
| Ruído binário sobrevive à limpeza como `#` | "Falhar fechado": byte estranho **provoca** a rejeição em vez de ser varrido para debaixo do tapete |
| Todo retorno vem com um `ParseStatus` explícito | Não existe "meio válido" |

Isso é verificado por **33 testes**, incluindo um *fuzz* determinístico de
5.000 entradas pseudoaleatórias que confere as invariantes do parser.

E há uma verificação a mais, no `ObdClient`: `test_obd_client` prova que um
pedido proibido **não escreve um único byte** no transporte. Não é "escreve e
a ECU ignora" — é "nunca sai da nossa memória". Essa distinção importa porque
o adaptador deste projeto (PIC18F25K80) implementa o conjunto AT completo e
**é capaz de escrever na ECU** — ver
[HARDWARE.md](HARDWARE.md#️-consequência-de-segurança-este-adaptador-é-capaz-de-escrever).

### Nunca exiba um número em que você não confia

Cada medida em `TelemetrySnapshot` carrega um campo `valid`. Um valor nunca
lido, ou lido de uma resposta rejeitada, **não pode aparecer na tela como se
fosse verdade**.

Mostrar `0 rpm` quando a rotação é desconhecida é pior do que mostrar `--`:
o motorista acreditaria no zero.

---

## 3. Watchdog: última linha de defesa, não política de erro

O Task Watchdog Timer está habilitado em `src/main.cpp` com prazo de 8 s.

**A distinção é o coração deste documento:**

| | Quem age | O que acontece |
|---|---|---|
| Bluetooth caiu, ECU não responde, resposta corrompida | A **lógica** | Vai para `Degraded`, mostra o erro, retenta com backoff. **Nunca reinicia.** |
| Firmware travou de verdade (loop infinito, deadlock, memória corrompida) | O **watchdog** | Reinicia o chip. É a única saída possível nesse ponto. |

Reset por watchdog é a última linha de defesa. **Não é tratamento de erro.**

---

## 4. Fail-safe: degradar, nunca travar nem reiniciar em loop

> Um firmware que reinicia em loop pendurado no painel é o **pior** resultado
> possível: pisca, não informa nada, e o motorista não tem ideia do que está
> acontecendo.

### Como a máquina de estados garante isso

`lib/kanri_core/src/state_machine.cpp` foi desenhada com esta invariante:

- **Todo** caminho de falha vai para `Degraded`.
- `Degraded` **sempre** tem saída: o timer de retentativa devolve para
  `ScanningAdapter`.
- `Boot` é **inalcançável** a partir de qualquer outro estado — não existe
  caminho de volta que equivalha a um reinício.
- Apenas `DisplayFailed` leva a `Fault`, porque sem tela não há como
  comunicar nada ao motorista. `Fault` é terminal e continua **exibindo o
  erro**, em vez de reiniciar.

Verificado exaustivamente: o teste
`test_boot_e_inalcancavel_de_qualquer_outro_estado` percorre **todas** as
combinações de estado × evento.

### Backoff exponencial

Reconectar em loop apertado torra a bateria do carro, entope o rádio 2.4 GHz
e esconde o problema real do usuário. `RetryPolicy` espera 1 s, 2 s, 4 s,
8 s… até um teto de 30 s.

Dois bugs clássicos, ambos com teste:
- **Estouro de inteiro:** dobrar um valor grande estoura o `uint32_t` e o
  delay volta a ser minúsculo — virando exatamente o loop agressivo que o
  backoff existia para evitar.
- **Esquecer de resetar:** sem `on_success()`, o backoff fica preso no teto
  para sempre e a próxima reconexão demora minutos sem motivo.

### Configuração corrompida não impede o boot

Queda de tensão durante uma gravação na flash é comum em 12 V automotivo.
`clamp_to_valid()` puxa cada campo inválido de volta para uma faixa segura.

**Invariante:** não importa o lixo que venha da flash, depois de
`clamp_to_valid()` a configuração **é** válida. Testado com flash apagada
(todos os bits em 1), tudo zerado, e 500 amostras pseudoaleatórias.

Um aparelho que se recusa a ligar por causa de uma configuração corrompida é
inútil no estacionamento.

---

## 5. Requisitos elétricos — a rede de 12 V é hostil

> **Ligar um ESP32 direto nos 12 V do carro destrói o ESP32.**
> Não é questão de "talvez". Detalhes de projeto em [HARDWARE.md](HARDWARE.md).

A rede elétrica de um veículo **não** é uma fonte de bancada. O que ela faz:

| Fenômeno | Valores reais | Efeito sem proteção |
|----------|---------------|---------------------|
| Tensão nominal | 12 V parado, **13,8–14,4 V** com motor ligado (alternador) | — |
| Partida do motor | Cai para **6–9 V** por centenas de ms | Reset do ESP32 no meio da operação |
| *Load dump* | Pico de **até 40 V+**, se a bateria se desconectar com o alternador carregando | Destrói o regulador e o ESP32 |
| Transientes de comutação | Pulsos ISO 7637-2, **–150 V a +100 V**, microssegundos | Perfura o regulador |
| Ruído de ignição | Alta frequência, contínuo | Corrompe comunicação serial, resets aleatórios |
| Polaridade invertida | Erro de montagem | Destrói tudo instantaneamente |

### Requisitos mínimos obrigatórios

1. **Regulador com entrada ≥ 40 V.** Conversor *buck* apropriado para
   automotivo (ex.: TPS54360, MP2315, LM2596-HV). **Não** use um AMS1117 ou
   7805 direto nos 12 V: além de não sobreviver aos picos, dissiparia o
   excesso em calor.
2. **Proteção contra polaridade reversa.** Diodo Schottky em série, ou um
   MOSFET-P em configuração *ideal diode* (menor queda de tensão).
3. **Supressão de transientes.** Diodo TVS na entrada (ex.: SMBJ26A,
   P6KE30A) + capacitor eletrolítico de *bulk* + cerâmicos de desacoplamento
   junto ao regulador.
4. **Fusível** de 500 mA a 1 A na linha de +12 V. Sem exceção.
5. **Corrente:** o ESP32 puxa picos de **~500 mA** durante transmissão de
   rádio. Dimensione o regulador para **≥ 1 A** e use ≥ 470 µF de *bulk*.
   Alimentação subdimensionada causa reset durante o Bluetooth — um sintoma
   que parece bug de software e não é.
6. **Consumo parasita.** O pino 16 do conector OBD2 é normalmente
   **permanentemente energizado**, mesmo com o carro desligado. Um aparelho
   esquecido plugado descarrega a bateria em dias. Use alimentação comutada
   pela ignição, *deep sleep*, ou desplugue.

### Montagem física

- **Não** obstrua a visão do motorista.
- **Não** monte na área de acionamento de airbag.
- Fixe firmemente: um objeto solto na cabine é projétil em uma frenagem.
- Passe o cabeamento longe das bobinas de ignição.

---

## 6. Antes de abrir um Pull Request

Passe por esta lista:

- [ ] Nenhum modo OBD2 novo além de `0x01` / `0x09`
- [ ] Nenhum comando AT novo sem justificativa escrita em `safety.h`
- [ ] Toda resposta do adaptador passa pelo parser antes de ser usada
- [ ] Nenhum caminho novo que reinicie o firmware por falha de lógica
- [ ] Nenhuma alocação dinâmica no caminho crítico (`String`, `new`, `malloc`)
- [ ] Nenhum `while` sem prazo e sem alimentar o watchdog
- [ ] Todo valor exibido tem `valid == true` verificado
- [ ] `pio test -e native` verde
- [ ] Cobertura de linhas de `lib/` segue em 100% — ver [TESTING.md](TESTING.md)
