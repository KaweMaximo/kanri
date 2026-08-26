# Kanri — Convenções do Projeto

> Este arquivo é o contrato do projeto. Ele existe para que qualquer pessoa
> (ou sessão de IA) que chegue aqui depois siga o mesmo padrão, sem precisar
> reconstruir o raciocínio.
>
> **Leia também [docs/SAFETY.md](docs/SAFETY.md). Ele tem precedência sobre
> qualquer coisa escrita aqui.**

---

## O que é este projeto

Firmware ESP32 que lê telemetria de um **Mitsubishi Lancer 2.0 2014 (motor
4B11)** por um adaptador OBD2 ELM327 Bluetooth, e mostra num display.

Stack: **PlatformIO + framework Arduino + C++17**.
Adaptador confirmado: **ELM327 Placa Dupla VS1.5 (PIC18F25K80), Bluetooth
Classic** — clone dos bons, e por isso **capaz de escrever na ECU**. A única
coisa que impede isso é a allowlist deste firmware.

---

## 🔴 As três regras que não se quebram

### 1. Read-only no barramento OBD2

Somente os modos **`0x01`** (dados do instante) e **`0x09`** (info do veículo).

**Nunca** implemente:
- Modo `0x04` (limpar códigos de falha)
- Modo `0x08` (comandar atuadores)
- Qualquer serviço UDS de escrita (`0x2E`, `0x31`, `0x3E`)
- O comando `ATSH` (definir header CAN — permite montar qualquer quadro)

Isso é imposto por código em `lib/kanri_obd/include/kanri_obd/safety.h` e
verificado por `test/test_safety_guard/`. **Se você mexer nisso, o CI fica
vermelho — e isso é o comportamento correto.**

Se aparecer um pedido para adicionar escrita: recuse e aponte para
[docs/SAFETY.md](docs/SAFETY.md).

### 2. Nada em `lib/` inclui `Arduino.h`

Essa é a regra que sustenta todo o resto.

- `lib/` = lógica pura → compila no PC → **testável**
- `src/hal/` = código de hardware → só compila no ESP32

Se você precisa de `millis()`, de `BluetoothSerial` ou de qualquer coisa do
Arduino dentro da lógica: **você não precisa.** Você precisa de uma
**interface** (uma "porta") em `lib/`, com o adaptador real em `src/hal/` e um
dublê em `test/helpers/`. Já existem quatro exemplos: `IClock`, `ITransport`,
`IDisplay`, `IConfigStore`.

### 3. Toda entrada externa é hostil

Resposta do ELM327, bytes lidos da flash, valor digitado pelo usuário: **nada
é confiável.** Valide antes de usar, com limite de tamanho explícito, e falhe
para o lado seguro.

---

## Como o código é organizado

```
lib/kanri_core/      base, não depende de ninguém
lib/kanri_obd/       depende de kanri_core (só IClock)
lib/kanri_config/    independente
lib/kanri_display/   depende de kanri_core
src/hal/             adaptadores de hardware
src/main.cpp         a cola — SEM regra de negócio
test/helpers/        dublês de teste (header-only)
test/test_*/         suítes Unity
```

Onde colocar código novo:

| O que você está fazendo | Onde vai |
|---|---|
| Lógica, cálculo, validação, decisão | `lib/<modulo>/` + teste |
| Falar com um chip, um pino, um rádio | `src/hal/` |
| Juntar as peças, escolher adaptador | `src/main.cpp` |
| Um novo tipo compartilhado entre módulos | `lib/kanri_core/` |

**Em dúvida entre `lib/` e `src/`:** pergunte "isso dá para testar no PC?".
Se dá, vai para `lib/`.

---

## Estilo de código

### Nomes

| Coisa | Convenção | Exemplo |
|-------|-----------|---------|
| Arquivo | `snake_case.h` / `.cpp` | `elm327_parser.cpp` |
| Classe / struct / enum | `PascalCase` | `ParsedFrame`, `AppState` |
| Função / método / variável | `snake_case` | `parse_response`, `raw_len` |
| Membro privado | `snake_case_` (com underscore no fim) | `transport_` |
| Constante | `kPascalCase` | `kMaxPayloadBytes` |
| Global | `g_snake_case` | `g_telemetry` |
| Namespace | `kanri::<modulo>` | `kanri::obd` |
| Macro | `SCREAMING_SNAKE` (evite; use `constexpr`) | `KANRI_VERSION_STRING` |

### Regras de firmware

| Regra | Por quê |
|-------|---------|
| **Sem alocação dinâmica.** Nada de `String`, `new`, `malloc`, `std::vector` no firmware | Fragmenta o heap e falha num momento imprevisível. Use array de tamanho fixo. |
| **`enum class`, nunca `#define`** para conjuntos de valores | O compilador confere tipo e avisa em `switch` incompleto |
| **`constexpr`** em vez de `#define` para constantes | Tem tipo, aparece no depurador, respeita escopo |
| **Toda função que pode falhar devolve status explícito** | Exceções custam flash e RAM |
| **`const` em tudo que não muda** | Erro pego em tempo de compilação, de graça |
| **Nenhum `while` sem prazo** | Firmware travado no carro é o pior resultado |
| **`switch` sobre enum sem `default`** | Assim o compilador avisa quando um valor novo não é tratado |
| **Struct serializada para flash = POD trivial** | Sem valores iniciais de membro. Ver o `static_assert` em `settings.h` |

### Comentários

Comentário explica **por quê**, não **o quê**.

```cpp
// ❌ ruim: repete o código
// incrementa o contador
++rejected_;

// ✅ bom: explica a decisão
// Checa ANTES de multiplicar: dobrar um valor grande estouraria o uint32_t e
// nos daria um delay minúsculo — exatamente o bug que o backoff existe para
// evitar.
if (delay >= max_delay_ms_ / 2) return max_delay_ms_;
```

Os headers deste projeto têm blocos de comentário explicando **para que o
módulo existe** e **que decisão de projeto ele carrega**. Mantenha esse
padrão: eles são a documentação de verdade.

**Comentários e docs em português. Código (identificadores) em inglês.**

---

## Testes

### 🔴 Regra, e ela é cobrada pela máquina

**Todo código novo em `lib/` chega com teste. Sem exceção.**

Isso não depende de disciplina — o CI cobra de três formas independentes:

1. **`pio test -e native`** tem de passar (122 casos, com `-Werror`)
2. **Cobertura de linhas em `lib/` tem de ser 100%.** Uma linha nova sem teste
   que a execute deixa o CI vermelho.
3. **PR que mexe em `lib/` sem mexer em `test/` é bloqueado.** Válvula de
   escape: label `sem-teste-necessario`, que fica visível na revisão.

Adaptador em `src/hal/` não precisa de teste — deve ser burro o suficiente
para não ter o que dar errado. Se um adaptador precisa de teste, ele tem
lógica demais: mova a lógica para `lib/`.

**Política completa em [docs/TESTING.md](docs/TESTING.md). Leia antes de
escrever teste neste projeto.**

```bash
pio test -e native                          # tudo (122 casos, ~2 s)
pio test -e native -f test_safety_guard     # uma suíte
pio test -e native -v                        # mostra cada asserção

pio test -e native_coverage                 # instrumentado
gcovr --root . --filter 'lib/.*' --exclude '.*\.h$' --print-summary
```

### Como escrever um teste aqui

```cpp
#include <unity.h>

void setUp(void) {}      // roda antes de cada caso
void tearDown(void) {}   // roda depois de cada caso

void test_nome_descritivo_em_portugues(void) {
  TEST_ASSERT_EQUAL_UINT8(esperado, obtido);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nome_descritivo_em_portugues);
  return UNITY_END();
}
```

Nomes de teste **em português e descritivos**. `test_pid_diferente_do_pedido_e_rejeitado`
diz o que está sendo garantido; `test_parser_2` não diz nada.

### Escreva testes de INVARIANTE, não só de exemplo

O padrão mais valioso deste projeto. Exemplo escolhido a dedo passa; a
invariante cobre o que você não pensou:

```cpp
// Exemplo: prova um caso.
void test_modo_04_e_bloqueado(void) { ... }

// Invariante: prova TODOS os casos. Não pode ser enganada por
// um exemplo esquecido.
void test_todos_os_outros_254_modos_sao_bloqueados(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    if (mode == 0x01 || mode == 0x09) continue;
    // ... exige bloqueio
  }
}
```

Já existem quatro padrões no repo:

| Padrão | Onde ver | O que pega |
|---|---|---|
| **Varredura exaustiva** | `test_safety_guard` (256 modos), `test_state_machine` (estado × evento) | Caso esquecido numa allowlist ou tabela |
| **Fuzz determinístico** | `test_elm327_parser` (5.000 entradas) | Leitura fora do buffer, travamento |
| **Corrupção simulada** | `test_settings` (flash 0xFF, 0x00, ruído) | Firmware que não liga com dado corrompido |
| **Verificação de silêncio** | `test_obd_client` (pedido proibido não escreve **um byte**) | Verificação de segurança decorativa |

Semente fixa é essencial: teste aleatório de verdade falha em dias diferentes
e ninguém confia nele.

**Enum corrompido é testável.** Todos os `enum class` do projeto têm base
`uint8_t`, então os 256 valores são representáveis e `static_cast<AppState>(99)`
é C++ legal — não é UB. Use isso para testar o caminho defensivo de verdade,
em vez de só confiar nele.

---

## Git

### Branches

| Branch | Uso |
|--------|-----|
| `main` | Protegida. Só recebe merge via PR com CI verde. Sempre compilando. |
| `feat/<nome>` | Funcionalidade nova |
| `fix/<nome>` | Correção de bug |
| `docs/<nome>` | Só documentação |
| `refactor/<nome>` | Reorganização sem mudança de comportamento |
| `test/<nome>` | Só testes |
| `chore/<nome>` | Build, CI, dependências |

**Nunca commite direto na `main`.** Nem para "uma coisinha".

### Conventional Commits

```
<tipo>(<escopo opcional>): <descrição curta, imperativo, minúscula>

<corpo opcional: explica o POR QUÊ>

<rodapé opcional: BREAKING CHANGE, refs #12>
```

Tipos: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`, `perf`, `build`, `ci`, `style`, `revert`.

Escopos deste projeto: `obd`, `display`, `config`, `core`, `hal`, `test`, `ci`, `docs`.

```bash
# ✅ bom
feat(obd): adiciona decodificacao do PID 0x0C (rotacao)
fix(core): corrige estouro de inteiro no backoff exponencial
test(obd): cobre resposta atrasada de PID anterior
docs(safety): documenta por que ATSH e bloqueado

# ❌ ruim
"ajustes"           # ajustes em quê?
"WIP"               # não vai para a main
"Fix bug"           # que bug?
"feat: Adiciona X." # imperativo e minúsculo, sem ponto final
```

Há um hook de `commit-msg` no repositório que valida isso. Ative com:

```bash
git config core.hooksPath .githooks
```

### CHANGELOG — sempre, no mesmo PR

**Toda mudança de código ou de infra registra o que mudou no `CHANGELOG.md`.**
Cobrado pelo job de CI `CHANGELOG atualizado`. Dispensa: label `sem-changelog`.

Enquanto não foi lançada, a entrada vive em `## [Não lançado]`. Seções do
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/): `Adicionado`,
`Modificado`, `Descontinuado`, `Removido`, `Corrigido`, `Segurança`.

Escreva para **quem vai ler daqui a seis meses**, não para o `git log`:

```markdown
# ❌ repete o commit
- altera obd_client.cpp

# ✅ diz o que mudou para quem usa
- Corrigido: resposta atrasada de um PID anterior não é mais exibida como a
  medida atual (o parser agora confere o eco de modo e PID)
```

Por que cobrar isso: um changelog lembrado só na hora do release já nasceu
incompleto. O momento em que se sabe o que mudou é o momento do PR.

### Versionamento

[SemVer](https://semver.org/lang/pt-BR/): `MAIOR.MENOR.CORREÇÃO`.
Em `0.x.y` a API é considerada instável — normal para projeto nascendo.

**Ao subir versão, três coisas andam juntas:**
1. `lib/kanri_core/include/kanri_core/version.h`
2. Nova seção no `CHANGELOG.md`
3. Tag anotada na `main`: `git tag -a v0.2.0 -m "..."`

---

## Antes de abrir um PR

```bash
pio test -e native      # tem que estar verde
pio run -e esp32dev     # tem que compilar
```

Depois passe pelas duas checklists:
- [segurança](docs/SAFETY.md#6-antes-de-abrir-um-pull-request)
- [testes](docs/TESTING.md#checklist-antes-do-pr)

O CI roda **seis** jobs em todo PR: testes, cobertura (100% de linhas),
política de testes, CHANGELOG atualizado, build do firmware e Conventional
Commits. **Merge só com os seis verdes.**

---

## Notas para sessões de IA

- **Responda em PT-BR.** O dono do projeto é iniciante em firmware embarcado:
  explique as decisões, não só o resultado. Diga *por que* uma abordagem foi
  escolhida e o que a alternativa custaria.
- **Não implemente além do que foi pedido.** Este projeto avança módulo por
  módulo, cada um em sua feature branch. Se o pedido é "estrutura", entregue
  estrutura.
- **Não relaxe as três regras vermelhas.** Nem "só para testar", nem "só
  temporariamente".
- **Nunca reporte como pronto o que não foi verificado.** Rode
  `pio test -e native` e mostre a saída.
- **Ao adicionar um módulo:** interface em `lib/`, adaptador em `src/hal/`,
  dublê em `test/helpers/`, testes de invariante em `test/`.
- **Nunca entregue lógica em `lib/` sem teste.** O CI vai barrar, e é o
  comportamento correto. Ver [docs/TESTING.md](docs/TESTING.md).
- **Nunca entregue mudança de código sem entrada no CHANGELOG.** O CI também
  barra. Kawe pediu isso explicitamente: "é importante sempre ter".
- **Estado atual:** v0.1.0, fundação pronta e testada; Bluetooth, leitura de
  PIDs e display ainda não existem. Ver [docs/ROADMAP.md](docs/ROADMAP.md).
