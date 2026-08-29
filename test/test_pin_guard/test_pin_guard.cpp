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
  return UNITY_END();
}
