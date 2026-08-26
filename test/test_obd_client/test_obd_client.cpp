// ============================================================================
//  Testes do ObdClient — a garantia de que NADA vaza para o barramento
// ============================================================================
//  O ObdClient ainda e esqueleto (a implementacao real vem na v0.2). Mas o
//  PORTAO DE SEGURANCA dentro dele ja esta ativo, e por isso ja tem teste.
//
//  A afirmacao central desta suite: para um pedido proibido, o FakeTransport
//  nao recebe UM UNICO BYTE. Nao e "recebe e a ECU ignora" — e "nunca sai da
//  nossa memoria". Essa e a diferenca entre uma verificacao de verdade e uma
//  verificacao decorativa.
//
//  Repare que testamos isso sem ESP32, sem adaptador e sem carro: o
//  FakeTransport registra tudo que foi escrito, e o teste simplesmente
//  pergunta se esta vazio.
// ============================================================================

#include <unity.h>

#include "fake_clock.h"
#include "fake_transport.h"
#include "kanri_obd/obd_client.h"

using kanri::obd::ObdClient;
using kanri::obd::ParsedFrame;
using kanri::obd::ParseStatus;

void setUp(void) {}
void tearDown(void) {}

namespace {

/// Monta um cliente ligado a um transporte de mentira JA CONECTADO — assim,
/// se o codigo tentasse escrever, a escrita daria certo e apareceria em
/// written(). Um transporte desconectado esconderia o problema.
struct Harness {
  kanri::test::FakeClock clock;
  kanri::test::FakeTransport transport;
  ObdClient client{transport, clock};

  Harness() { transport.connect(); }
};

}  // namespace

// ---------------------------------------------------------------------------
//  A GARANTIA CENTRAL
// ---------------------------------------------------------------------------

// Modo 04 = limpar codigos de falha. Se um unico byte chegasse ao transporte,
// o adaptador o repassaria para a ECU. Este teste exige silencio absoluto.
void test_modo_proibido_nao_escreve_nada_no_transporte(void) {
  Harness h;
  TEST_ASSERT_TRUE(h.transport.is_connected());

  const ParsedFrame frame = h.client.read_pid(0x04, 0x00);

  TEST_ASSERT_FALSE(frame.ok());
  TEST_ASSERT_EQUAL_UINT32(0, h.transport.written().size());
  TEST_ASSERT_EQUAL_UINT32(1, h.client.rejected_count());
}

// Varredura exaustiva: nenhum dos 254 modos proibidos consegue escrever.
// Um teste de exemplo provaria um caso; este prova que nao ha brecha.
void test_nenhum_modo_proibido_alcanca_o_barramento(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    if (mode == 0x01 || mode == 0x09) continue;

    Harness h;
    h.client.read_pid(static_cast<std::uint8_t>(mode), 0x0C);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, static_cast<std::uint32_t>(h.transport.written().size()),
        "um modo proibido escreveu bytes no transporte");
  }
}

// Segunda barreira: modo permitido, mas PID fora do catalogo.
void test_pid_fora_do_catalogo_nao_escreve_nada(void) {
  Harness h;
  h.client.read_pid(0x01, 0xFF);

  TEST_ASSERT_EQUAL_UINT32(0, h.transport.written().size());
  TEST_ASSERT_EQUAL_UINT32(1, h.client.rejected_count());
}

void test_contador_de_recusas_acumula(void) {
  Harness h;
  h.client.read_pid(0x04, 0x00);
  h.client.read_pid(0x08, 0x00);
  h.client.read_pid(0x01, 0xFF);
  TEST_ASSERT_EQUAL_UINT32(3, h.client.rejected_count());
}

// ---------------------------------------------------------------------------
//  ESTADO ATUAL DA v0.1 — documentado por teste
// ---------------------------------------------------------------------------
//  Estes testes travam o comportamento do esqueleto. Quando a v0.2 implementar
//  de verdade, eles vao FALHAR — e isso e o objetivo: o teste vermelho lembra
//  de atualizar a expectativa em vez de deixar um esqueleto esquecido.

void test_pedido_permitido_devolve_nao_implementado_na_v01(void) {
  Harness h;
  const ParsedFrame frame = h.client.read_pid(0x01, 0x0C);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::NotImplemented),
                        static_cast<int>(frame.status));
  // Pedido legitimo nao conta como recusa de seguranca.
  TEST_ASSERT_EQUAL_UINT32(0, h.client.rejected_count());
}

void test_initialize_ainda_nao_implementado(void) {
  Harness h;
  TEST_ASSERT_FALSE(h.client.initialize());
}

void test_leitura_de_tensao_ainda_nao_implementada(void) {
  Harness h;
  float volts = -1.0F;
  TEST_ASSERT_FALSE(h.client.read_adapter_voltage(volts));
}

// Configuracao customizada tem de ser aceita pelo construtor: a v0.2 vai
// depender disso para respeitar o timeout vindo da NVS.
void test_aceita_configuracao_customizada(void) {
  kanri::test::FakeClock clock;
  kanri::test::FakeTransport transport;
  transport.connect();

  kanri::obd::ObdClientConfig config;
  config.response_timeout_ms = 250;
  config.max_retries = 5;

  ObdClient client(transport, clock, config);
  client.read_pid(0x04, 0x00);  // proibido: mesmo assim, silencio

  TEST_ASSERT_EQUAL_UINT32(0, transport.written().size());
}

// ---------------------------------------------------------------------------
//  O FakeTransport tambem precisa ser confiavel
// ---------------------------------------------------------------------------
//  Se o dublê tiver bug, todos os testes acima viram teatro. Estes casos
//  verificam que ele realmente registra escritas e entrega leituras.

void test_fake_transport_registra_escritas(void) {
  kanri::test::FakeTransport transport;
  transport.connect();

  const std::uint8_t payload[] = {'0', '1', '0', 'C', '\r'};
  TEST_ASSERT_EQUAL_UINT32(5, transport.write(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_STRING("010C\r", transport.written().c_str());
}

void test_fake_transport_desconectado_nao_aceita_escrita(void) {
  kanri::test::FakeTransport transport;  // nao conectado
  const std::uint8_t payload[] = {'X'};
  TEST_ASSERT_EQUAL_UINT32(0, transport.write(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT32(0, transport.written().size());
}

void test_fake_transport_entrega_o_que_foi_enfileirado(void) {
  kanri::test::FakeTransport transport;
  transport.connect();
  transport.queue("41 0C 1A F8\r>");

  std::uint8_t buffer[64] = {};
  const std::size_t read = transport.read(buffer, sizeof(buffer));

  TEST_ASSERT_EQUAL_UINT32(13, read);
  TEST_ASSERT_EQUAL_UINT32(0, transport.available());  // consumiu tudo
}

// read() nunca pode escrever alem do buffer que o chamador ofereceu.
void test_fake_transport_respeita_o_tamanho_do_buffer(void) {
  kanri::test::FakeTransport transport;
  transport.connect();
  transport.queue("ABCDEFGHIJ");

  std::uint8_t buffer[4] = {};
  TEST_ASSERT_EQUAL_UINT32(4, transport.read(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT32(6, transport.available());  // sobrou o resto
}

void test_fake_transport_simula_falha_de_conexao(void) {
  kanri::test::FakeTransport transport;
  transport.fail_next_connect();
  TEST_ASSERT_FALSE(transport.connect());
  TEST_ASSERT_FALSE(transport.is_connected());
}

void test_fake_clock_avanca_sob_controle_do_teste(void) {
  kanri::test::FakeClock clock;
  TEST_ASSERT_EQUAL_UINT32(0, clock.now_ms());
  clock.advance(30000);  // 30 segundos, instantaneamente
  TEST_ASSERT_EQUAL_UINT32(30000, clock.now_ms());
  clock.set(1000);
  TEST_ASSERT_EQUAL_UINT32(1000, clock.now_ms());
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_modo_proibido_nao_escreve_nada_no_transporte);
  RUN_TEST(test_nenhum_modo_proibido_alcanca_o_barramento);
  RUN_TEST(test_pid_fora_do_catalogo_nao_escreve_nada);
  RUN_TEST(test_contador_de_recusas_acumula);

  RUN_TEST(test_pedido_permitido_devolve_nao_implementado_na_v01);
  RUN_TEST(test_initialize_ainda_nao_implementado);
  RUN_TEST(test_leitura_de_tensao_ainda_nao_implementada);
  RUN_TEST(test_aceita_configuracao_customizada);

  RUN_TEST(test_fake_transport_registra_escritas);
  RUN_TEST(test_fake_transport_desconectado_nao_aceita_escrita);
  RUN_TEST(test_fake_transport_entrega_o_que_foi_enfileirado);
  RUN_TEST(test_fake_transport_respeita_o_tamanho_do_buffer);
  RUN_TEST(test_fake_transport_simula_falha_de_conexao);
  RUN_TEST(test_fake_clock_avanca_sob_controle_do_teste);

  return UNITY_END();
}
