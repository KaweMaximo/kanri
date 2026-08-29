// ============================================================================
//  Testes da checagem de pinos
// ============================================================================
//  Este arquivo existe por causa de um pedido real: "liga pra mim o GPIO 28".
//
//  O GPIO 28 nao existe no ESP32. E o problema nao e o pedido — e que
//  `digitalWrite(28, HIGH)` COMPILA sem aviso e nao faz nada. Quem pediu vai
//  medir um pino inexistente procurando defeito na fiacao.
//
//  Todo veredito aqui evita uma falha SILENCIOSA. E por isso que a checagem
//  vale um modulo testado, e nao um `if` na hora do comando.
// ============================================================================

#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "kanri_core/pin_guard.h"

using kanri::core::check_output_pin;
using kanri::core::PinVerdict;

void setUp(void) {}
void tearDown(void) {}

namespace {
PinVerdict checar(std::uint8_t pino) {
  return check_output_pin(pino, nullptr, 0);
}
}  // namespace

// O caso que originou o modulo.
void test_gpio_28_nao_existe(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::DoesNotExist),
                        static_cast<int>(checar(28)));
}

// Os seis buracos na numeracao do ESP32. Um deles de fora e uma falha
// silenciosa esperando acontecer.
void test_todos_os_buracos_da_numeracao_sao_recusados(void) {
  const std::uint8_t inexistentes[] = {20, 24, 28, 29, 30, 31};
  for (const std::uint8_t p : inexistentes) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(PinVerdict::DoesNotExist),
                                  static_cast<int>(checar(p)), "deveria nao existir");
  }
}

// pinMode(34, OUTPUT) compila, roda, nao da erro — e o pino nunca muda.
void test_input_only_e_recusado(void) {
  const std::uint8_t entrada[] = {34, 35, 36, 39};
  for (const std::uint8_t p : entrada) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::InputOnly),
                          static_cast<int>(checar(p)));
  }
}

// 6 a 11 sao a flash SPI: acionar trava o chip na hora.
void test_pinos_da_flash_sao_recusados(void) {
  for (std::uint8_t p = 6; p <= 11; ++p) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::SpiFlash),
                          static_cast<int>(checar(p)));
  }
}

void test_console_usb_e_recusado(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::UsbSerial),
                        static_cast<int>(checar(1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::UsbSerial),
                        static_cast<int>(checar(3)));
}

void test_strapping_que_impede_o_boot_e_recusado(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::BootStrapping),
                        static_cast<int>(checar(12)));
}

void test_fora_da_faixa_e_recusado(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::OutOfRange),
                        static_cast<int>(checar(40)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::OutOfRange),
                        static_cast<int>(checar(255)));
}

// O que o firmware ja usa nao pode ser mexido por engano.
void test_pino_reservado_pelo_firmware_e_recusado(void) {
  const std::uint8_t ocupados[] = {2, 5, 17, 18, 23, 36};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::Reserved),
                        static_cast<int>(check_output_pin(18, ocupados, 6)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::Ok),
                        static_cast<int>(check_output_pin(22, ocupados, 6)));
}

// A razao mais GRAVE tem que vencer: dizer "ja usado pelo firmware" quando o
// pino e da flash esconderia que acionar aquilo trava o chip.
void test_a_razao_mais_grave_prevalece(void) {
  const std::uint8_t ocupados[] = {7, 34};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::SpiFlash),
                        static_cast<int>(check_output_pin(7, ocupados, 2)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::InputOnly),
                        static_cast<int>(check_output_pin(34, ocupados, 2)));
}

// Os que sobraram na alocacao do Kanri precisam mesmo passar — senao o
// comando recusaria tudo e nao serviria para nada.
void test_pinos_livres_do_kanri_sao_aceitos(void) {
  const std::uint8_t ocupados[] = {2, 5, 17, 18, 23, 36};
  const std::uint8_t livres[] = {4, 13, 14, 16, 19, 21, 22, 25, 26, 27, 32, 33};
  for (const std::uint8_t p : livres) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(PinVerdict::Ok),
        static_cast<int>(check_output_pin(p, ocupados, 6)), "deveria ser livre");
  }
}

// Toda recusa precisa de uma explicacao legivel: "recusado" sem motivo faria
// a pessoa achar que o comando esta quebrado.
void test_todo_veredito_tem_explicacao(void) {
  const PinVerdict todos[] = {
      PinVerdict::Ok,        PinVerdict::DoesNotExist, PinVerdict::OutOfRange,
      PinVerdict::InputOnly, PinVerdict::SpiFlash,     PinVerdict::UsbSerial,
      PinVerdict::BootStrapping, PinVerdict::Reserved};
  for (const PinVerdict v : todos) {
    const char* txt = kanri::core::to_string(v);
    TEST_ASSERT_NOT_NULL(txt);
    TEST_ASSERT_TRUE(std::strlen(txt) > 0);
  }
  TEST_ASSERT_NOT_NULL(kanri::core::to_string(static_cast<PinVerdict>(99)));
}

// Varredura completa: nenhum pino pode ficar sem veredito definido.
void test_todos_os_pinos_tem_veredito(void) {
  for (int p = 0; p <= 255; ++p) {
    const PinVerdict v = checar(static_cast<std::uint8_t>(p));
    TEST_ASSERT_NOT_NULL(kanri::core::to_string(v));
    if (p > 39) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(PinVerdict::OutOfRange),
                            static_cast<int>(v));
    }
  }
}

// ---------------------------------------------------------------------------
//  Lista de pinos
// ---------------------------------------------------------------------------

namespace {
const std::uint8_t kOcupados[] = {2, 5, 17, 18, 23, 36};
kanri::core::PinList lista(const char* txt) {
  return kanri::core::parse_pin_list(txt, kOcupados, 6);
}
}  // namespace

// Os dois separadores, porque ninguem lembra qual e o certo — e errar isso
// nao deveria custar uma tentativa.
void test_aceita_virgula_e_espaco(void) {
  for (const char* txt : {"22,21,19", "22 21 19", "22, 21  19"}) {
    const auto l = lista(txt);
    TEST_ASSERT_TRUE_MESSAGE(l.ok(), txt);
    TEST_ASSERT_EQUAL_UINT32(3, l.count);
    TEST_ASSERT_EQUAL_UINT8(22, l.pins[0]);
    TEST_ASSERT_EQUAL_UINT8(21, l.pins[1]);
    TEST_ASSERT_EQUAL_UINT8(19, l.pins[2]);
  }
}

void test_lista_vazia_e_recusada(void) {
  TEST_ASSERT_FALSE(lista("").ok());
  TEST_ASSERT_FALSE(lista("   ").ok());
  TEST_ASSERT_FALSE(kanri::core::parse_pin_list(nullptr, nullptr, 0).ok());
}

// A LISTA INTEIRA e recusada se UM pino for ruim. Aceitar o resto deixaria
// metade dos LEDs funcionando e a outra metade em silencio — e o silencio e
// justamente o que este modulo existe para combater.
void test_um_pino_ruim_reprova_a_lista_inteira(void) {
  const auto l = lista("22,28,19");
  TEST_ASSERT_FALSE(l.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinListError::BadPin),
                        static_cast<int>(l.error));
  TEST_ASSERT_EQUAL_UINT8(28, l.offending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinVerdict::DoesNotExist),
                        static_cast<int>(l.verdict));
}

void test_pino_ja_usado_reprova_a_lista(void) {
  const auto l = lista("22,18");
  TEST_ASSERT_FALSE(l.ok());
  TEST_ASSERT_EQUAL_UINT8(18, l.offending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinVerdict::Reserved),
                        static_cast<int>(l.verdict));
}

// Um pino duas vezes piscaria fora de ritmo com ele mesmo, e ninguem
// entenderia por que.
void test_pino_repetido_e_recusado(void) {
  const auto l = lista("22,21,22");
  TEST_ASSERT_FALSE(l.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinListError::Duplicate),
                        static_cast<int>(l.error));
  TEST_ASSERT_EQUAL_UINT8(22, l.offending);
}

void test_texto_no_meio_da_lista_e_recusado(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinListError::NotANumber),
                        static_cast<int>(lista("22,abc,19").error));
}

void test_lista_longa_demais_e_recusada(void) {
  const auto l = lista("22 21 19 16 13 14 25 26 27");
  TEST_ASSERT_FALSE(l.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::core::PinListError::TooMany),
                        static_cast<int>(l.error));
}

void test_numero_absurdo_nao_estoura(void) {
  const auto l = lista("999999999");
  TEST_ASSERT_FALSE(l.ok());
}

void test_todo_erro_de_lista_tem_explicacao(void) {
  const kanri::core::PinListError todos[] = {
      kanri::core::PinListError::None,      kanri::core::PinListError::Empty,
      kanri::core::PinListError::TooMany,   kanri::core::PinListError::NotANumber,
      kanri::core::PinListError::Duplicate, kanri::core::PinListError::BadPin};
  for (const auto e : todos) {
    TEST_ASSERT_NOT_NULL(kanri::core::to_string(e));
    TEST_ASSERT_TRUE(std::strlen(kanri::core::to_string(e)) > 0);
  }
  TEST_ASSERT_NOT_NULL(
      kanri::core::to_string(static_cast<kanri::core::PinListError>(99)));
}

// ---------------------------------------------------------------------------
//  Barra de LEDs
// ---------------------------------------------------------------------------

void test_piscar_acende_metade_do_ciclo(void) {
  TEST_ASSERT_EQUAL_HEX8(0x07, kanri::core::bar_blink_mask(0, 3, 600));
  TEST_ASSERT_EQUAL_HEX8(0x07, kanri::core::bar_blink_mask(299, 3, 600));
  TEST_ASSERT_EQUAL_HEX8(0x00, kanri::core::bar_blink_mask(300, 3, 600));
  TEST_ASSERT_EQUAL_HEX8(0x00, kanri::core::bar_blink_mask(599, 3, 600));
  TEST_ASSERT_EQUAL_HEX8(0x07, kanri::core::bar_blink_mask(600, 3, 600));
}

// Oito LEDs sao o teto; a mascara cheia nao pode estourar o deslocamento.
void test_barra_cheia_nao_estoura_o_deslocamento(void) {
  TEST_ASSERT_EQUAL_HEX8(0xFF, kanri::core::bar_blink_mask(0, 8, 600));
  TEST_ASSERT_EQUAL_HEX8(0xFF, kanri::core::bar_blink_mask(0, 99, 600));
}

void test_barra_vazia_fica_apagada(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, kanri::core::bar_blink_mask(0, 0, 600));
  TEST_ASSERT_EQUAL_UINT32(0, kanri::core::bar_test_steps(0));
}

// O aparelho fica ligado no carro; millis() envolve a cada ~49,7 dias e o
// piscar nao pode parar na virada.
void test_piscar_sobrevive_a_virada_do_contador(void) {
  bool aceso = false, apagado = false;
  for (std::uint32_t i = 0; i < 3000; ++i) {
    const std::uint32_t t = 0xFFFFFA00U + i;
    if (kanri::core::bar_blink_mask(t, 3, 600) != 0) aceso = true; else apagado = true;
  }
  TEST_ASSERT_TRUE(aceso);
  TEST_ASSERT_TRUE(apagado);
}

// UM LED POR VEZ e o que localiza fio trocado: com todos acesos, dois LEDs
// invertidos ficam indistinguiveis.
void test_autoteste_acende_um_led_por_vez(void) {
  for (std::size_t i = 0; i < 3; ++i) {
    const std::uint8_t m = kanri::core::bar_test_mask(i, 3);
    TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>(1U << i), m);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m & (m - 1), "mais de um LED aceso");
  }
}

// A varredura precisa cobrir TODOS os LEDs, cada um exatamente uma vez. Um
// LED que nenhum passo acende e um defeito que o autoteste nao encontra.
void test_autoteste_cobre_todos_os_leds_sem_repetir(void) {
  const std::size_t n = 5;
  std::uint8_t acumulado = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t m = kanri::core::bar_test_mask(i, n);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, acumulado & m, "LED repetido");
    acumulado |= m;
  }
  TEST_ASSERT_EQUAL_HEX8(0x1F, acumulado);
}

void test_autoteste_acende_todos_e_termina_apagado(void) {
  const std::size_t n = 4;
  TEST_ASSERT_EQUAL_UINT32(n + 2, kanri::core::bar_test_steps(n));
  TEST_ASSERT_EQUAL_HEX8(0x0F, kanri::core::bar_test_mask(n, n));
  TEST_ASSERT_EQUAL_HEX8(0x00, kanri::core::bar_test_mask(n + 1, n));
  TEST_ASSERT_EQUAL_HEX8(0x00, kanri::core::bar_test_mask(999, n));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_gpio_28_nao_existe);
  RUN_TEST(test_todos_os_buracos_da_numeracao_sao_recusados);
  RUN_TEST(test_input_only_e_recusado);
  RUN_TEST(test_pinos_da_flash_sao_recusados);
  RUN_TEST(test_console_usb_e_recusado);
  RUN_TEST(test_strapping_que_impede_o_boot_e_recusado);
  RUN_TEST(test_fora_da_faixa_e_recusado);
  RUN_TEST(test_pino_reservado_pelo_firmware_e_recusado);
  RUN_TEST(test_a_razao_mais_grave_prevalece);
  RUN_TEST(test_pinos_livres_do_kanri_sao_aceitos);
  RUN_TEST(test_todo_veredito_tem_explicacao);
  RUN_TEST(test_todos_os_pinos_tem_veredito);

  RUN_TEST(test_aceita_virgula_e_espaco);
  RUN_TEST(test_lista_vazia_e_recusada);
  RUN_TEST(test_um_pino_ruim_reprova_a_lista_inteira);
  RUN_TEST(test_pino_ja_usado_reprova_a_lista);
  RUN_TEST(test_pino_repetido_e_recusado);
  RUN_TEST(test_texto_no_meio_da_lista_e_recusado);
  RUN_TEST(test_lista_longa_demais_e_recusada);
  RUN_TEST(test_numero_absurdo_nao_estoura);
  RUN_TEST(test_todo_erro_de_lista_tem_explicacao);

  RUN_TEST(test_piscar_acende_metade_do_ciclo);
  RUN_TEST(test_barra_cheia_nao_estoura_o_deslocamento);
  RUN_TEST(test_barra_vazia_fica_apagada);
  RUN_TEST(test_piscar_sobrevive_a_virada_do_contador);
  RUN_TEST(test_autoteste_acende_um_led_por_vez);
  RUN_TEST(test_autoteste_cobre_todos_os_leds_sem_repetir);
  RUN_TEST(test_autoteste_acende_todos_e_termina_apagado);
  return UNITY_END();
}
