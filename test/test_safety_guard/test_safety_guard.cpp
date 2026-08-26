// ============================================================================
//  Testes do portao READ-ONLY — a rede de seguranca do projeto
// ============================================================================
//  Estes testes existem por um motivo unico: garantir que o Kanri NUNCA
//  consiga enviar um comando de escrita para a ECU do carro.
//
//  Se alguem — voce daqui a seis meses, um colaborador, ou uma IA numa sessao
//  futura — tentar habilitar o Modo 04 (limpar DTCs) ou o Modo 08 (comandar
//  atuadores), esta suite fica VERMELHA e o CI bloqueia o merge.
//
//  E a diferenca entre uma regra escrita no README e uma regra que o
//  computador cobra. Ver docs/SAFETY.md.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_obd/obd_pid.h"
#include "kanri_obd/safety.h"

using kanri::obd::check_at_command;
using kanri::obd::check_obd_request;
using kanri::obd::RequestVerdict;

void setUp(void) {}
void tearDown(void) {}

static void assert_verdict(RequestVerdict expected, RequestVerdict actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

static RequestVerdict at(const char* cmd) {
  return check_at_command(cmd, std::strlen(cmd));
}

// ---------------------------------------------------------------------------
//  O QUE PODE: apenas leitura
// ---------------------------------------------------------------------------

void test_modo_01_com_pid_do_catalogo_e_permitido(void) {
  assert_verdict(RequestVerdict::Allowed, check_obd_request(0x01, 0x0C));  // RPM
  assert_verdict(RequestVerdict::Allowed, check_obd_request(0x01, 0x05));  // temp agua
  assert_verdict(RequestVerdict::Allowed, check_obd_request(0x01, 0x42));  // tensao
}

void test_modo_09_e_permitido(void) {
  assert_verdict(RequestVerdict::Allowed, check_obd_request(0x09, 0x02));  // VIN
}

// ---------------------------------------------------------------------------
//  O QUE NAO PODE: qualquer coisa que escreva
// ---------------------------------------------------------------------------

// O TESTE MAIS IMPORTANTE DO REPOSITORIO.
// Modo 04 apaga a luz de injecao e todo o historico de falhas da ECU.
void test_modo_04_limpar_dtcs_e_bloqueado(void) {
  assert_verdict(RequestVerdict::ForbiddenMode, check_obd_request(0x04, 0x00));
}

// Modo 08 comanda atuadores. Com o carro andando, isso e risco fisico.
void test_modo_08_controle_de_atuador_e_bloqueado(void) {
  assert_verdict(RequestVerdict::ForbiddenMode, check_obd_request(0x08, 0x00));
}

// Servicos UDS de escrita/rotina, fora do OBD2 padrao.
void test_modos_uds_de_escrita_sao_bloqueados(void) {
  assert_verdict(RequestVerdict::ForbiddenMode, check_obd_request(0x2E, 0x00));
  assert_verdict(RequestVerdict::ForbiddenMode, check_obd_request(0x31, 0x00));
  assert_verdict(RequestVerdict::ForbiddenMode, check_obd_request(0x3E, 0x00));
}

// Exaustivo, nao por amostragem: TODOS os 256 valores possiveis de modo.
// So 0x01 e 0x09 passam da primeira barreira. Um teste como este nao pode ser
// enganado por um exemplo esquecido.
void test_todos_os_outros_254_modos_sao_bloqueados(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    if (mode == 0x01 || mode == 0x09) continue;
    const RequestVerdict verdict =
        check_obd_request(static_cast<std::uint8_t>(mode), 0x0C);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(RequestVerdict::ForbiddenMode),
        static_cast<int>(verdict), "um modo de escrita passou pelo portao");
  }
}

// Segunda barreira: mesmo dentro do modo 01, so pedimos PIDs do catalogo.
void test_pid_fora_do_catalogo_e_bloqueado(void) {
  assert_verdict(RequestVerdict::ForbiddenPid, check_obd_request(0x01, 0xFF));
  assert_verdict(RequestVerdict::ForbiddenPid, check_obd_request(0x01, 0x7E));
  assert_verdict(RequestVerdict::ForbiddenPid, check_obd_request(0x09, 0x0A));
}

// Invariante da tabela: nenhuma entrada de obd_pid.h pode usar um modo de
// escrita. Protege contra alguem acrescentar {0x04, ...} na tabela.
void test_catalogo_de_pids_contem_somente_modos_de_leitura(void) {
  for (std::size_t i = 0; i < kanri::obd::kSupportedPidCount; ++i) {
    const std::uint8_t mode = kanri::obd::kSupportedPids[i].mode;
    TEST_ASSERT_TRUE_MESSAGE(kanri::obd::is_read_only_mode(mode),
                             "a tabela de PIDs tem um modo que nao e leitura");
  }
}

void test_catalogo_nao_tem_pids_duplicados(void) {
  for (std::size_t i = 0; i < kanri::obd::kSupportedPidCount; ++i) {
    for (std::size_t j = i + 1; j < kanri::obd::kSupportedPidCount; ++j) {
      const bool same = kanri::obd::kSupportedPids[i].mode ==
                            kanri::obd::kSupportedPids[j].mode &&
                        kanri::obd::kSupportedPids[i].pid ==
                            kanri::obd::kSupportedPids[j].pid;
      TEST_ASSERT_FALSE_MESSAGE(same, "PID duplicado no catalogo");
    }
  }
}

void test_find_pid_e_has_expected_length(void) {
  const kanri::obd::PidDescriptor* rpm = kanri::obd::find_pid(0x01, 0x0C);
  TEST_ASSERT_NOT_NULL(rpm);
  TEST_ASSERT_EQUAL_UINT8(2, rpm->expected_bytes);
  TEST_ASSERT_EQUAL_STRING("engine_rpm", rpm->key);

  TEST_ASSERT_NULL(kanri::obd::find_pid(0x01, 0xFF));

  // Um frame bem formado mas com tamanho errado tambem e recusado.
  TEST_ASSERT_TRUE(kanri::obd::has_expected_length(0x01, 0x0C, 2));
  TEST_ASSERT_FALSE(kanri::obd::has_expected_length(0x01, 0x0C, 1));
  TEST_ASSERT_FALSE(kanri::obd::has_expected_length(0x01, 0x0C, 3));
  TEST_ASSERT_FALSE(kanri::obd::has_expected_length(0x01, 0xFF, 2));
}

// ---------------------------------------------------------------------------
//  COMANDOS AT — allowlist do adaptador
// ---------------------------------------------------------------------------

void test_comandos_at_de_configuracao_sao_permitidos(void) {
  assert_verdict(RequestVerdict::Allowed, at("ATZ"));
  assert_verdict(RequestVerdict::Allowed, at("ATE0"));
  assert_verdict(RequestVerdict::Allowed, at("ATL0"));
  assert_verdict(RequestVerdict::Allowed, at("ATS0"));
  assert_verdict(RequestVerdict::Allowed, at("ATH0"));
  assert_verdict(RequestVerdict::Allowed, at("ATSP0"));
  assert_verdict(RequestVerdict::Allowed, at("ATDP"));
  assert_verdict(RequestVerdict::Allowed, at("ATRV"));
  assert_verdict(RequestVerdict::Allowed, at("ATSTFF"));
}

void test_comandos_at_aceitam_minusculas_e_espacos(void) {
  assert_verdict(RequestVerdict::Allowed, at("atz"));
  assert_verdict(RequestVerdict::Allowed, at("at sp 0"));
  assert_verdict(RequestVerdict::Allowed, at("ATZ\r"));
}

// ATSH define o header CAN: com ele da para enderecar qualquer modulo e
// enviar qualquer servico, inclusive escrita. E a porta dos fundos que
// anularia todo o resto — por isso fica fora da allowlist.
void test_atsh_definir_header_e_bloqueado(void) {
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATSH7E0"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("AT SH 7E0"));
}

void test_comandos_at_perigosos_sao_bloqueados(void) {
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATMA"));       // monitor all
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATPP2CSV01")); // grava params
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATBI"));       // pula init
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATCRA7E8"));   // filtro
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATTP6"));      // forca protocolo
}

void test_o_que_nao_comeca_com_at_e_bloqueado(void) {
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("010C"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("0401"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("STDI"));
}

void test_comandos_at_malformados(void) {
  assert_verdict(RequestVerdict::Malformed, check_at_command(nullptr, 0));
  assert_verdict(RequestVerdict::Malformed, at(""));
  assert_verdict(RequestVerdict::Malformed, at("AT"));  // curto demais
  assert_verdict(RequestVerdict::Malformed,
                 at("ATSP0000000000000000000000"));  // longo demais
  const char with_control[] = {'A', 'T', 'Z', '\x01'};
  assert_verdict(RequestVerdict::Malformed,
                 check_at_command(with_control, sizeof(with_control)));
}

// Uma allowlist mal implementada aceita prefixos: se "ATZ" passa, "ATZX"
// nao pode passar de carona.
void test_allowlist_exige_correspondencia_exata(void) {
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATZX"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATE2"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATRVX"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATSTZZ"));
  assert_verdict(RequestVerdict::ForbiddenAtCommand, at("ATSP"));
}

void test_to_string_nunca_devolve_nulo(void) {
  const RequestVerdict all[] = {
      RequestVerdict::Allowed,       RequestVerdict::ForbiddenMode,
      RequestVerdict::ForbiddenPid,  RequestVerdict::ForbiddenAtCommand,
      RequestVerdict::Malformed,
  };
  for (const RequestVerdict verdict : all) {
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(verdict));
  }
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_modo_01_com_pid_do_catalogo_e_permitido);
  RUN_TEST(test_modo_09_e_permitido);

  RUN_TEST(test_modo_04_limpar_dtcs_e_bloqueado);
  RUN_TEST(test_modo_08_controle_de_atuador_e_bloqueado);
  RUN_TEST(test_modos_uds_de_escrita_sao_bloqueados);
  RUN_TEST(test_todos_os_outros_254_modos_sao_bloqueados);
  RUN_TEST(test_pid_fora_do_catalogo_e_bloqueado);

  RUN_TEST(test_catalogo_de_pids_contem_somente_modos_de_leitura);
  RUN_TEST(test_catalogo_nao_tem_pids_duplicados);
  RUN_TEST(test_find_pid_e_has_expected_length);

  RUN_TEST(test_comandos_at_de_configuracao_sao_permitidos);
  RUN_TEST(test_comandos_at_aceitam_minusculas_e_espacos);
  RUN_TEST(test_atsh_definir_header_e_bloqueado);
  RUN_TEST(test_comandos_at_perigosos_sao_bloqueados);
  RUN_TEST(test_o_que_nao_comeca_com_at_e_bloqueado);
  RUN_TEST(test_comandos_at_malformados);
  RUN_TEST(test_allowlist_exige_correspondencia_exata);
  RUN_TEST(test_to_string_nunca_devolve_nulo);

  return UNITY_END();
}
