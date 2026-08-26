# `test/` — os testes do Kanri

```bash
pio test -e native                          # tudo (~1,4 s)
pio test -e native -f test_safety_guard     # uma suíte só
pio test -e native -v                        # mostra cada asserção
```

Nada aqui precisa de ESP32, adaptador OBD2 ou carro. O PlatformIO compila a
lógica de `lib/` com o `g++` do seu PC e linka com o framework de teste
[Unity](https://github.com/ThrowTheSwitch/Unity).

## As suítes

| Suíte | Casos | O que garante |
|-------|-------|---------------|
| `test_elm327_parser/` | 31 | Que nenhuma resposta malformada do adaptador seja tratada como dado válido |
| `test_safety_guard/` | 18 | **Que o firmware não consiga escrever na ECU.** Ver [SAFETY.md](../docs/SAFETY.md) |
| `test_state_machine/` | 19 | Que nenhum caminho de falha reinicie o firmware |
| `test_settings/` | 21 | Que configuração corrompida na flash não impeça o boot |
| `test_display/` | 9 | Que a tela não exiba número em que não confiamos |

## `helpers/` — os dublês

Header-only, para não precisarem ser compilados separadamente.

| Dublê | Substitui | Para quê |
|-------|-----------|----------|
| `FakeClock` | `millis()` | Testar um timeout de 30 s em microssegundos |
| `FakeTransport` | Bluetooth | Enfileirar exatamente os bytes que quisermos — inclusive lixo binário |
| `FakeDisplay` | O display | Afirmar o que apareceria na tela, sem tela |
| `FakeConfigStore` | A flash | Simular leitura falhando, escrita falhando, dados corrompidos |

## Como escrever um teste aqui

```cpp
#include <unity.h>

void setUp(void) {}      // antes de cada caso
void tearDown(void) {}   // depois de cada caso

void test_nome_descritivo_em_portugues(void) {
  TEST_ASSERT_EQUAL_UINT8(esperado, obtido);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nome_descritivo_em_portugues);
  return UNITY_END();
}
```

**Cada pasta `test_*/` é um binário independente**, com o seu próprio `main()`.
É por isso que o PlatformIO consegue rodar uma suíte isolada com `-f`.

## Prefira invariante a exemplo

O padrão mais valioso deste projeto. Um exemplo escolhido a dedo prova um
caso; uma invariante cobre o que você não pensou.

```cpp
// Exemplo — prova um caso:
void test_modo_04_e_bloqueado(void) { ... }

// Invariante — prova TODOS. Não pode ser enganada por um caso esquecido:
void test_todos_os_outros_254_modos_sao_bloqueados(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    if (mode == 0x01 || mode == 0x09) continue;
    // ... exige bloqueio
  }
}
```

Já existem exemplos de:

- **varredura exaustiva** — os 256 modos OBD2; todas as combinações de
  estado × evento da máquina de estados
- **fuzz determinístico** — 5.000 entradas pseudoaleatórias no parser,
  verificando invariantes (nunca estoura o buffer, nunca diz "Ok" para um
  modo/PID errado)
- **corrupção simulada** — configuração com todos os bits em 1 (flash
  apagada), tudo zerado, e 500 amostras de ruído

**Semente sempre fixa.** Teste aleatório de verdade falha em dias diferentes,
e ninguém confia num teste que falha sozinho.

## `-Werror` está ligado no ambiente `native`

De propósito: no ambiente de teste, warning é erro. Durante a construção
deste projeto isso já pegou um bug real — um `memset` numa struct que o
compilador sabia não ser um POD trivial (`-Wclass-memaccess`), justamente a
struct que é gravada byte a byte na flash.
