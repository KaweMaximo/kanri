# Política de Testes — Kanri

> **Regra do projeto: código novo em `lib/` chega com teste. Sempre.**
>
> Não é recomendação. É cobrado pelo CI de três formas independentes, e o
> merge é bloqueado se qualquer uma falhar.

---

## Estado atual

| Métrica | Valor | Cobrado pelo CI |
|---------|-------|-----------------|
| Casos de teste | **122** em 6 suítes | Todos têm de passar |
| Tempo de execução | ~2 s no PC | — |
| Cobertura de **linhas** de `lib/` | **100%** | Falha abaixo de 100% |
| Cobertura de **funções** de `lib/` | **100%** | — |
| Cobertura de **ramos** (branches) de `lib/` | **92,7%** | Falha abaixo de 90% |

```bash
pio test -e native          # 122 casos, ~2 s
```

---

## Como a regra é cobrada

Três camadas independentes em `.github/workflows/ci.yml`:

### 1. Os testes têm de passar
Job **`Testes unitários (native)`**. Roda `pio test -e native` com `-Werror`
ligado: no ambiente de teste, **warning é erro**.

> Durante a construção da v0.1 isso já pegou um bug real: um `memset` numa
> struct que o compilador sabia não ser POD trivial (`-Wclass-memaccess`) —
> justamente a struct gravada byte a byte na flash, onde o problema apareceria
> como corrupção silenciosa.

### 2. A cobertura não pode cair
Job **`Cobertura (100% linhas)`**. Roda os testes instrumentados com `gcov` e
falha se a cobertura de linhas de `lib/` ficar abaixo de **100%**.

Consequência prática: **se você adicionar uma linha em `lib/` sem teste que a
execute, o CI fica vermelho.** É essa a garantia de "todo desenvolvimento novo
tem teste".

### 3. Mexer em `lib/` sem mexer em `test/` é bloqueado
Job **`Política de testes`**. Se o PR altera `lib/**/*.cpp` ou `lib/**/*.h` e
**não** altera nada em `test/`, o job falha com explicação.

**Válvula de escape:** se a mudança realmente não pede teste (só comentário,
só renomear, só `#include`), adicione a label **`sem-teste-necessario`** no
PR. A dispensa fica registrada e visível na revisão — não é um `--no-verify`
invisível.

---

## Rodando localmente

```bash
# O do dia a dia — rode antes de todo commit
pio test -e native

# Uma suíte só, quando está iterando
pio test -e native -f test_safety_guard

# Ver cada asserção
pio test -e native -v

# Medir cobertura (precisa: pip install gcovr)
pio test -e native_coverage
gcovr --root . --filter 'lib/.*' --exclude '.*\.h$' --print-summary

# Relatório HTML navegável, para achar a linha descoberta
gcovr --root . --filter 'lib/.*' --exclude '.*\.h$' --html-details cobertura.html
```

> ⚠️ Use `native_coverage` **só** para medir. A instrumentação deixa o binário
> mais lento e espalha `.gcda`/`.gcno` pela pasta de build.

---

## O que testar, e o que não

| Camada | Testar? | Por quê |
|--------|---------|---------|
| `lib/**` — lógica pura | ✅ **Sempre** | É onde as decisões moram. Compila no PC, então não há desculpa. |
| `test/helpers/**` — dublês | ✅ Sim | Se o dublê tem bug, todos os testes que o usam viram teatro. Ver `test_obd_client`. |
| `src/hal/**` — adaptadores | ❌ Não | Devem ser burros: uma linha que repassa a chamada ao hardware. Se um adaptador precisa de teste, ele tem lógica demais — mova a lógica para `lib/`. |
| `src/main.cpp` — a cola | ❌ Não | Só instancia e conecta as peças. Sem regra de negócio. |

Se você se pegar querendo testar algo em `src/`, é sinal de que aquilo devia
estar em `lib/`.

---

## Como escrever um teste aqui

```cpp
#include <unity.h>

#include "kanri_obd/safety.h"

void setUp(void) {}      // roda antes de cada caso
void tearDown(void) {}   // roda depois de cada caso

void test_modo_04_limpar_dtcs_e_bloqueado(void) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(kanri::obd::RequestVerdict::ForbiddenMode),
      static_cast<int>(kanri::obd::check_obd_request(0x04, 0x00)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_modo_04_limpar_dtcs_e_bloqueado);
  return UNITY_END();
}
```

**Cada pasta `test_*/` é um binário independente**, com o seu próprio `main()`.
É por isso que dá para rodar uma suíte isolada com `-f`.

Nomes de teste **em português e descritivos**:
`test_pid_diferente_do_pedido_e_rejeitado` diz o que está garantido.
`test_parser_2` não diz nada.

---

## Prefira invariante a exemplo

O padrão mais valioso deste projeto. Um exemplo escolhido a dedo prova **um
caso**; uma invariante cobre **o que você não pensou**.

```cpp
// Exemplo — prova um caso:
void test_modo_04_e_bloqueado(void) { ... }

// Invariante — prova todos. Não pode ser enganada por um caso esquecido:
void test_todos_os_outros_254_modos_sao_bloqueados(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    if (mode == 0x01 || mode == 0x09) continue;
    // ... exige bloqueio
  }
}
```

### Os quatro padrões já usados no repo

| Padrão | Onde ver | O que pega |
|--------|----------|------------|
| **Varredura exaustiva** | `test_safety_guard` — os 256 modos OBD2; `test_state_machine` — todas as combinações estado × evento | Caso esquecido numa allowlist ou numa tabela de transição |
| **Fuzz determinístico** | `test_elm327_parser` — 5.000 entradas pseudoaleatórias | Leitura fora dos limites do buffer, travamento com entrada estranha |
| **Corrupção simulada** | `test_settings` — flash com todos os bits em 1, tudo zerado, 500 amostras de ruído | Firmware que não liga por causa de dado corrompido |
| **Verificação de silêncio** | `test_obd_client` — pedido proibido não escreve **um único byte** no transporte | Verificação de segurança decorativa, que "checa" mas deixa passar |

**Semente sempre fixa.** Teste aleatório de verdade falha em dias diferentes,
e ninguém confia num teste que falha sozinho.

### Enum corrompido é testável, não é UB

Todos os `enum class` do projeto têm base `std::uint8_t`. Isso significa que
**os 256 valores são representáveis**, então `static_cast<AppState>(99)` é
C++ legal — não é comportamento indefinido.

Aproveite: dá para testar de verdade o caminho defensivo contra memória
corrompida, em vez de só confiar nele.

```cpp
const AppState corrupted = static_cast<AppState>(99);
assert_state(AppState::Fault, next_state(corrupted, AppEvent::DataValid));
TEST_ASSERT_EQUAL_STRING("Unknown", kanri::core::to_string(corrupted));
```

---

## Quando uma linha é genuinamente inalcançável

Raro, mas acontece. Nesse caso, marque explicitamente **com justificativa**:

```cpp
// GCOVR_EXCL_START
// Inalcançável: <explique POR QUE nenhum teste consegue chegar aqui>.
...
// GCOVR_EXCL_STOP
```

Regras:
- A justificativa é **obrigatória**. Marcação sem explicação é dívida escondida.
- Antes de marcar, tente o teste. Na v0.1, três lacunas que pareciam
  "inalcançáveis" viraram testes reais — inclusive o portão de segurança do
  `ObdClient`, que estava em **0% de cobertura**.
- Uma exclusão nova é ponto de discussão no PR, não detalhe de formatação.

---

## Checklist antes do PR

```bash
pio test -e native      # 122 casos verdes
pio run -e esp32dev     # compila para o hardware
```

- [ ] Todo código novo em `lib/` tem teste que o executa
- [ ] Existe pelo menos uma **invariante**, não só exemplos
- [ ] Cobertura de linhas segue em 100% (`pio test -e native_coverage` + `gcovr`)
- [ ] Nenhuma exclusão `GCOVR_EXCL` nova sem justificativa escrita
- [ ] Nenhum teste depende de tempo real, rede ou hardware
- [ ] Nenhuma semente aleatória sem valor fixo

E a [checklist de segurança](SAFETY.md#6-antes-de-abrir-um-pull-request).
