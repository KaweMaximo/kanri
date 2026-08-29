// ============================================================================
//  Testes do parser ELM327 — a fronteira de confianca do firmware
// ============================================================================
//  Rode com:  pio test -e native
//
//  Como um teste Unity funciona:
//    - cada `void test_algo(void)` e um caso de teste;
//    - dentro dele, TEST_ASSERT_* afirma o que deve ser verdade;
//    - setUp()/tearDown() rodam antes/depois de CADA caso;
//    - main() lista os casos com RUN_TEST e devolve o resultado.
//
//  Estes testes nao precisam de ESP32, adaptador nem carro: o parser e codigo
//  puro. Por isso rodam em segundos, em todo Pull Request.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_obd/elm327_parser.h"

using kanri::obd::ParsedFrame;
using kanri::obd::parse_response;
using kanri::obd::ParseStatus;

void setUp(void) {}
void tearDown(void) {}

// Atalho: chama o parser com uma string literal, pedindo modo 01 / PID 0x0C
// (rotacao do motor), que e o caso mais comum.
static ParsedFrame parse_rpm(const char* raw) {
  return parse_response(raw, std::strlen(raw), 0x01, 0x0C);
}

static void assert_status(ParseStatus expected, const ParsedFrame& frame) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                        static_cast<int>(frame.status));
}

// ---------------------------------------------------------------------------
//  CAMINHO FELIZ
// ---------------------------------------------------------------------------

// "41 0C 1A F8" -> 41=resposta ao modo 01, 0C=PID da rotacao, 1AF8=dados.
// RPM real = (0x1A*256 + 0xF8)/4 = 1726. A conversao entra na v0.2; aqui
// verificamos que os BYTES chegaram intactos.
void test_resposta_valida_com_espacos(void) {
  const ParsedFrame frame = parse_rpm("41 0C 1A F8\r\r>");
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_TRUE(frame.ok());
  TEST_ASSERT_EQUAL_UINT8(0x41, frame.mode);
  TEST_ASSERT_EQUAL_UINT8(0x0C, frame.pid);
  TEST_ASSERT_EQUAL_UINT8(2, frame.length);
  TEST_ASSERT_EQUAL_UINT8(0x1A, frame.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF8, frame.data[1]);
}

// Com ATS0 o adaptador manda tudo junto, sem espacos.
void test_resposta_valida_sem_espacos(void) {
  const ParsedFrame frame = parse_rpm("410C1AF8");
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(0x1A, frame.data[0]);
}

// Clones baratos as vezes respondem em minusculas.
void test_resposta_em_minusculas(void) {
  const ParsedFrame frame = parse_rpm("410c1af8");
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(0xF8, frame.data[1]);
}

// Com ATE1 (eco ligado) o adaptador repete o comando antes de responder.
// O parser tem de pular esse eco.
void test_ignora_eco_do_comando(void) {
  const ParsedFrame frame = parse_rpm("010C\r41 0C 1A F8\r\r>");
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(2, frame.length);
}

// Na primeira consulta o ELM327 avisa "SEARCHING..." e SO DEPOIS manda o dado.
// Um parser ingenuo devolveria "erro" e perderia a leitura boa.
void test_searching_seguido_de_dado_valido(void) {
  const ParsedFrame frame = parse_rpm("SEARCHING...\r41 0C 1A F8\r>");
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(0x1A, frame.data[0]);
}

void test_modo_09_resposta_valida(void) {
  const char* raw = "49 02 01 33 41 33\r>";
  const ParsedFrame frame = parse_response(raw, std::strlen(raw), 0x09, 0x02);
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(0x49, frame.mode);
  TEST_ASSERT_EQUAL_UINT8(4, frame.length);
}

void test_payload_de_um_byte(void) {
  // PID 0x05 = temperatura do motor, 1 byte. 0x7B = 123 - 40 = 83 C.
  const char* raw = "41 05 7B\r>";
  const ParsedFrame frame = parse_response(raw, std::strlen(raw), 0x01, 0x05);
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(1, frame.length);
  TEST_ASSERT_EQUAL_UINT8(0x7B, frame.data[0]);
}

// ---------------------------------------------------------------------------
//  MENSAGENS DE TEXTO DO ADAPTADOR
// ---------------------------------------------------------------------------

void test_no_data(void) {
  const ParsedFrame frame = parse_rpm("NO DATA\r\r>");
  assert_status(ParseStatus::NoData, frame);
  TEST_ASSERT_FALSE(frame.ok());
  TEST_ASSERT_EQUAL_UINT8(0, frame.length);
}

void test_unable_to_connect(void) {
  assert_status(ParseStatus::UnableToConnect, parse_rpm("UNABLE TO CONNECT\r>"));
}

void test_can_error(void) {
  assert_status(ParseStatus::BusError, parse_rpm("CAN ERROR\r>"));
}

void test_bus_error(void) {
  assert_status(ParseStatus::BusError, parse_rpm("BUS ERROR\r>"));
}

void test_buffer_full(void) {
  assert_status(ParseStatus::BufferFull, parse_rpm("BUFFER FULL\r>"));
}

void test_stopped(void) {
  assert_status(ParseStatus::Stopped, parse_rpm("STOPPED\r>"));
}

void test_interrogacao_comando_desconhecido(void) {
  assert_status(ParseStatus::UnknownCommand, parse_rpm("?\r>"));
}

void test_searching_sozinho_e_transitorio(void) {
  const ParsedFrame frame = parse_rpm("SEARCHING...\r>");
  assert_status(ParseStatus::Searching, frame);
  TEST_ASSERT_TRUE(kanri::obd::is_transient(frame.status));
}

// ---------------------------------------------------------------------------
//  ENTRADAS DEGENERADAS — aqui e onde bugs de firmware costumam morar
// ---------------------------------------------------------------------------

void test_ponteiro_nulo(void) {
  assert_status(ParseStatus::Empty, parse_response(nullptr, 0, 0x01, 0x0C));
}

void test_comprimento_zero(void) {
  assert_status(ParseStatus::Empty, parse_response("410C1AF8", 0, 0x01, 0x0C));
}

void test_apenas_prompt(void) {
  assert_status(ParseStatus::Empty, parse_rpm(">"));
}

void test_apenas_cr_lf(void) {
  assert_status(ParseStatus::Empty, parse_rpm("\r\n\r\n"));
}

void test_numero_impar_de_digitos_hex(void) {
  // Um byte partido pela metade: transmissao truncada.
  assert_status(ParseStatus::OddHexDigits, parse_rpm("410C1AF\r>"));
}

void test_caractere_nao_hexadecimal(void) {
  // 'Z' nao existe em hexadecimal.
  assert_status(ParseStatus::InvalidCharacter, parse_rpm("41 0C 1Z F8\r>"));
}

void test_resposta_curta_demais(void) {
  // Nem o eco de modo + PID cabe.
  assert_status(ParseStatus::TooShort, parse_rpm("41\r>"));
}

// Cenario real e perigoso: pedimos o PID 0x0C, mas chega a resposta ATRASADA
// de um pedido anterior (PID 0x0D, velocidade). Sem esta checagem, a
// velocidade seria exibida como se fosse a rotacao.
void test_pid_diferente_do_pedido_e_rejeitado(void) {
  assert_status(ParseStatus::UnexpectedPid, parse_rpm("410D1AF8\r>"));
}

void test_modo_diferente_do_pedido_e_rejeitado(void) {
  // 0x43 = resposta ao modo 03 (ler DTCs). Nao foi o que pedimos.
  assert_status(ParseStatus::UnexpectedMode, parse_rpm("430C1AF8\r>"));
}

void test_payload_maior_que_o_limite(void) {
  // 2 bytes de eco + 33 bytes de dados = 1 acima de kMaxPayloadBytes (32).
  char raw[4 + (33 * 2) + 1];
  std::memcpy(raw, "410C", 4);
  for (std::size_t i = 0; i < 33; ++i) std::memcpy(raw + 4 + (i * 2), "AA", 2);
  raw[4 + (33 * 2)] = '\0';
  assert_status(ParseStatus::PayloadTooLong, parse_rpm(raw));
}

void test_entrada_gigante_e_recusada_sem_analise(void) {
  char raw[kanri::obd::kMaxRawResponseBytes + 10];
  std::memset(raw, 'A', sizeof(raw));
  const ParsedFrame frame = parse_response(raw, sizeof(raw), 0x01, 0x0C);
  assert_status(ParseStatus::RawTooLong, frame);
}

void test_linha_longa_demais(void) {
  // Acima de kMaxLineBytes, mas ainda abaixo do limite da entrada crua.
  char raw[kanri::obd::kMaxLineBytes + 20];
  std::memset(raw, 'A', sizeof(raw) - 1);
  raw[sizeof(raw) - 1] = '\0';
  assert_status(ParseStatus::BufferFull, parse_rpm(raw));
}

// Lixo binario e o que um adaptador com contato ruim realmente produz.
void test_lixo_binario_e_rejeitado(void) {
  const char raw[] = {'\x01', '\xFF', '\x7F', '\x00', '\x9A', '\x41'};
  const ParsedFrame frame = parse_response(raw, sizeof(raw), 0x01, 0x0C);
  TEST_ASSERT_FALSE(frame.ok());
}

void test_nulo_no_meio_da_resposta_nao_encerra_a_analise(void) {
  // Se o parser usasse strlen, pararia no '\0' e perderia a linha seguinte.
  const char raw[] = "41\0 0C 1A F8\r41 0C 1A F8\r>";
  const ParsedFrame frame = parse_response(raw, sizeof(raw) - 1, 0x01, 0x0C);
  assert_status(ParseStatus::Ok, frame);
  TEST_ASSERT_EQUAL_UINT8(0x1A, frame.data[0]);
}

// ---------------------------------------------------------------------------
//  INVARIANTES — valem para QUALQUER entrada, nao para um exemplo
// ---------------------------------------------------------------------------

// Varre milhares de entradas pseudoaleatorias (com semente fixa, entao o
// resultado e sempre o mesmo) e confere as garantias do parser. Um teste
// assim pega classes inteiras de bug que exemplos escolhidos a dedo passam
// batido — tipico caso de leitura fora dos limites do buffer.
void test_fuzz_deterministico_mantem_invariantes(void) {
  std::uint32_t seed = 0x12345678U;
  char buffer[128];

  for (int iteration = 0; iteration < 5000; ++iteration) {
    // Gerador linear congruente: simples, rapido e reproduzivel.
    seed = (seed * 1103515245U) + 12345U;
    const std::size_t len = (seed >> 16) % sizeof(buffer);
    for (std::size_t i = 0; i < len; ++i) {
      seed = (seed * 1103515245U) + 12345U;
      buffer[i] = static_cast<char>((seed >> 16) & 0xFF);
    }

    const ParsedFrame frame = parse_response(buffer, len, 0x01, 0x0C);

    // Invariante 1: length nunca passa da capacidade de data[].
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(
        static_cast<std::uint8_t>(kanri::obd::kMaxPayloadBytes), frame.length);
    // Invariante 2: se disse Ok, o modo/PID conferem com o que foi pedido.
    if (frame.ok()) {
      TEST_ASSERT_EQUAL_UINT8(0x41, frame.mode);
      TEST_ASSERT_EQUAL_UINT8(0x0C, frame.pid);
    }
    // Invariante 3: to_string nunca devolve nullptr (usado em log e display).
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(frame.status));
  }
}

// "NO DATA" significa que a ECU nao tem esse PID. Repetir e desperdicio de
// banda no barramento. Ja "SEARCHING" pede paciencia, nao desistencia.
void test_politica_de_retentativa(void) {
  TEST_ASSERT_FALSE(kanri::obd::is_transient(ParseStatus::NoData));
  TEST_ASSERT_FALSE(kanri::obd::is_transient(ParseStatus::Ok));
  TEST_ASSERT_FALSE(kanri::obd::is_transient(ParseStatus::UnableToConnect));
  TEST_ASSERT_FALSE(kanri::obd::is_transient(ParseStatus::UnknownCommand));
  TEST_ASSERT_TRUE(kanri::obd::is_transient(ParseStatus::Searching));
  TEST_ASSERT_TRUE(kanri::obd::is_transient(ParseStatus::InvalidCharacter));
  TEST_ASSERT_TRUE(kanri::obd::is_transient(ParseStatus::BufferFull));
}

// to_string e is_transient alimentam log, display e politica de retentativa.
// Um nullptr ali viraria travamento; um status esquecido viraria decisao errada.
void test_to_string_cobre_todos_os_status(void) {
  const ParseStatus all[] = {
      ParseStatus::Ok,               ParseStatus::Empty,
      ParseStatus::NoData,           ParseStatus::Searching,
      ParseStatus::UnableToConnect,  ParseStatus::BusError,
      ParseStatus::Stopped,          ParseStatus::BufferFull,
      ParseStatus::UnknownCommand,   ParseStatus::RawTooLong,
      ParseStatus::InvalidCharacter, ParseStatus::OddHexDigits,
      ParseStatus::TooShort,         ParseStatus::PayloadTooLong,
      ParseStatus::UnexpectedMode,   ParseStatus::UnexpectedPid,
      ParseStatus::NotImplemented,
  };
  for (const ParseStatus status : all) {
    const char* name = kanri::obd::to_string(status);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
    // is_transient tem de ter uma resposta definida para todo status.
    (void)kanri::obd::is_transient(status);
  }
}

// Status com valor fora do enum (memoria corrompida). Base uint8_t, entao o
// cast e legal. A defesa tem de responder "nao insista" e "Unknown".
void test_status_corrompido_e_tratado_com_seguranca(void) {
  const ParseStatus corrupted = static_cast<ParseStatus>(240);
  TEST_ASSERT_FALSE(kanri::obd::is_transient(corrupted));
  TEST_ASSERT_EQUAL_STRING("Unknown", kanri::obd::to_string(corrupted));
}

// ---------------------------------------------------------------------------
//  RESPOSTA SEM PID (modos de codigo de falha)
// ---------------------------------------------------------------------------
//  Estes modos sao pedidos sozinhos e a ECU responde sem ecoar PID. O segundo
//  byte e a CONTAGEM de codigos — usar o parser normal aqui faria ele cobrar
//  um PID que nao existe e rejeitar a contagem como se fosse PID errado.

static ParsedFrame parse_dtc_modo(const char* raw) {
  return kanri::obd::parse_mode_response(raw, std::strlen(raw), 0x03);
}

void test_resposta_sem_pid_valida(void) {
  // 43 = resposta ao modo 03; 02 = dois codigos; depois os quatro bytes.
  const ParsedFrame f = parse_dtc_modo("43 02 03 01 04 20\r>");
  assert_status(ParseStatus::Ok, f);
  TEST_ASSERT_EQUAL_UINT8(0x43, f.mode);
  TEST_ASSERT_EQUAL_UINT8(2, f.pid);      // aqui `pid` guarda a contagem
  TEST_ASSERT_EQUAL_UINT8(4, f.length);   // e o byte de contagem NAO entra
  TEST_ASSERT_EQUAL_UINT8(0x03, f.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, f.data[3]);
}

void test_resposta_sem_pid_sem_codigos(void) {
  const ParsedFrame f = parse_dtc_modo("4300\r>");
  assert_status(ParseStatus::Ok, f);
  TEST_ASSERT_EQUAL_UINT8(0, f.pid);
  TEST_ASSERT_EQUAL_UINT8(0, f.length);
}

void test_resposta_sem_pid_ignora_eco(void) {
  // Com ATE1 o adaptador repete o comando, que aqui e so "03".
  const ParsedFrame f = parse_dtc_modo("03\r43 01 01 71\r>");
  assert_status(ParseStatus::Ok, f);
  TEST_ASSERT_EQUAL_UINT8(2, f.length);
}

void test_resposta_sem_pid_recusas(void) {
  assert_status(ParseStatus::NoData, parse_dtc_modo("NO DATA\r>"));
  assert_status(ParseStatus::BusError, parse_dtc_modo("CAN ERROR\r>"));
  assert_status(ParseStatus::Empty, parse_dtc_modo(">"));
  assert_status(ParseStatus::Empty,
                kanri::obd::parse_mode_response(nullptr, 0, 0x03));
  assert_status(ParseStatus::Empty,
                kanri::obd::parse_mode_response("4300", 0, 0x03));
  // Modo ecoado errado: 47 e resposta ao 07, nao ao 03.
  assert_status(ParseStatus::UnexpectedMode, parse_dtc_modo("4701 0171\r>"));
  assert_status(ParseStatus::InvalidCharacter, parse_dtc_modo("43 0Z 01\r>"));
  assert_status(ParseStatus::OddHexDigits, parse_dtc_modo("4302031\r>"));
  assert_status(ParseStatus::TooShort, parse_dtc_modo("43\r>"));
}

void test_resposta_sem_pid_entrada_gigante(void) {
  char raw[kanri::obd::kMaxRawResponseBytes + 10];
  std::memset(raw, 'A', sizeof(raw));
  assert_status(ParseStatus::RawTooLong,
                kanri::obd::parse_mode_response(raw, sizeof(raw), 0x03));
}

void test_resposta_sem_pid_linha_longa_demais(void) {
  char raw[kanri::obd::kMaxLineBytes + 20];
  std::memset(raw, 'A', sizeof(raw) - 1);
  raw[sizeof(raw) - 1] = '\0';
  assert_status(ParseStatus::BufferFull, parse_dtc_modo(raw));
}

void test_resposta_sem_pid_payload_longo_demais(void) {
  // 2 bytes de cabecalho + 33 de dados: um alem do que cabe.
  char raw[4 + (33 * 2) + 1];
  std::memcpy(raw, "4300", 4);
  for (std::size_t i = 0; i < 33; ++i) std::memcpy(raw + 4 + (i * 2), "AA", 2);
  raw[4 + (33 * 2)] = '\0';
  assert_status(ParseStatus::PayloadTooLong, parse_dtc_modo(raw));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_resposta_valida_com_espacos);
  RUN_TEST(test_resposta_valida_sem_espacos);
  RUN_TEST(test_resposta_em_minusculas);
  RUN_TEST(test_ignora_eco_do_comando);
  RUN_TEST(test_searching_seguido_de_dado_valido);
  RUN_TEST(test_modo_09_resposta_valida);
  RUN_TEST(test_payload_de_um_byte);

  RUN_TEST(test_no_data);
  RUN_TEST(test_unable_to_connect);
  RUN_TEST(test_can_error);
  RUN_TEST(test_bus_error);
  RUN_TEST(test_buffer_full);
  RUN_TEST(test_stopped);
  RUN_TEST(test_interrogacao_comando_desconhecido);
  RUN_TEST(test_searching_sozinho_e_transitorio);

  RUN_TEST(test_ponteiro_nulo);
  RUN_TEST(test_comprimento_zero);
  RUN_TEST(test_apenas_prompt);
  RUN_TEST(test_apenas_cr_lf);
  RUN_TEST(test_numero_impar_de_digitos_hex);
  RUN_TEST(test_caractere_nao_hexadecimal);
  RUN_TEST(test_resposta_curta_demais);
  RUN_TEST(test_pid_diferente_do_pedido_e_rejeitado);
  RUN_TEST(test_modo_diferente_do_pedido_e_rejeitado);
  RUN_TEST(test_payload_maior_que_o_limite);
  RUN_TEST(test_entrada_gigante_e_recusada_sem_analise);
  RUN_TEST(test_linha_longa_demais);
  RUN_TEST(test_lixo_binario_e_rejeitado);
  RUN_TEST(test_nulo_no_meio_da_resposta_nao_encerra_a_analise);

  RUN_TEST(test_fuzz_deterministico_mantem_invariantes);
  RUN_TEST(test_politica_de_retentativa);
  RUN_TEST(test_resposta_sem_pid_valida);
  RUN_TEST(test_resposta_sem_pid_sem_codigos);
  RUN_TEST(test_resposta_sem_pid_ignora_eco);
  RUN_TEST(test_resposta_sem_pid_recusas);
  RUN_TEST(test_resposta_sem_pid_entrada_gigante);
  RUN_TEST(test_resposta_sem_pid_linha_longa_demais);
  RUN_TEST(test_resposta_sem_pid_payload_longo_demais);

  RUN_TEST(test_to_string_cobre_todos_os_status);
  RUN_TEST(test_status_corrompido_e_tratado_com_seguranca);

  return UNITY_END();
}
