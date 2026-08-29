// ============================================================================
//  Testes do ObdClient — a garantia de que NADA vaza para o barramento
// ============================================================================
//  O ObdClient ainda e esqueleto (a implementacao real vem na v0.2). Mas o
//  PORTAO DE SEGURANCA dentro dele ja esta ativo, e por isso ja tem teste.
//
//  A afirmacao central desta suite: para um pedido proibido, o transporte
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
#include "fake_elm327.h"
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

  Harness() {
    // O transporte adianta o relogio a cada consulta. Sem isso, uma leitura
    // que este duble nunca responde deixaria o cliente esperando para sempre.
    transport.drive_clock(clock);
    transport.connect();
  }
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

// Varredura exaustiva: nenhum modo PROIBIDO consegue escrever.
//
// A lista de permitidos e consultada em is_read_only_mode(), e nao repetida
// aqui: este teste cobra a CONSEQUENCIA (nada sai para o barramento), enquanto
// test_safety_guard cobra a LISTA em si, escrita la de forma independente da
// implementacao. Os dois juntos impedem tanto afrouxar a regra quanto furar a
// implementacao dela.
void test_nenhum_modo_proibido_alcanca_o_barramento(void) {
  for (int mode = 0; mode <= 0xFF; ++mode) {
    const std::uint8_t m = static_cast<std::uint8_t>(mode);
    if (kanri::obd::is_read_only_mode(m)) continue;

    Harness h;
    h.client.read_pid(m, 0x0C);

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
//  CONVERSA REAL COM UM ELM327 SIMULADO
// ---------------------------------------------------------------------------
//  Daqui para baixo usamos o FakeElm327, que responde como um adaptador de
//  verdade: demora, termina com '>', diz "NO DATA" para PID que nao conhece,
//  e as vezes fica mudo. E o que permite testar o dialogo inteiro na bancada.

namespace {

/// Cliente ligado a um ELM327 simulado, ja conectado.
struct Elm {
  kanri::test::FakeClock clock;
  kanri::test::FakeElm327 elm{clock};
  ObdClient client{elm, clock};

  Elm() { elm.connect(); }
};

}  // namespace

void test_inicializacao_envia_a_sequencia_at_completa(void) {
  Elm h;
  TEST_ASSERT_TRUE(h.client.initialize());
  TEST_ASSERT_TRUE(h.client.ready());

  // Os seis passos de elm327_commands.h, na ordem.
  const std::string& enviado = h.elm.written();
  TEST_ASSERT_TRUE(enviado.find("ATZ\r") != std::string::npos);
  TEST_ASSERT_TRUE(enviado.find("ATE0\r") != std::string::npos);
  TEST_ASSERT_TRUE(enviado.find("ATL0\r") != std::string::npos);
  TEST_ASSERT_TRUE(enviado.find("ATS0\r") != std::string::npos);
  TEST_ASSERT_TRUE(enviado.find("ATH0\r") != std::string::npos);
  TEST_ASSERT_TRUE(enviado.find("ATSP0\r") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(6, h.elm.command_count());
}

// Se um passo obrigatorio falha, a inicializacao falha — nao seguimos com um
// adaptador meio configurado, que responderia com eco e espacos e faria o
// parser trabalhar contra um formato que nao esperamos.
void test_inicializacao_falha_se_um_passo_obrigatorio_nao_responde(void) {
  Elm h;
  h.elm.on_at("ATH0", "?");  // adaptador nao entendeu
  TEST_ASSERT_FALSE(h.client.initialize());
  TEST_ASSERT_FALSE(h.client.ready());
}

void test_inicializacao_falha_com_adaptador_mudo(void) {
  Elm h;
  h.elm.mute_next(1);  // ATZ nao responde
  TEST_ASSERT_FALSE(h.client.initialize());
}

void test_le_rpm_de_verdade(void) {
  Elm h;
  TEST_ASSERT_TRUE(h.client.initialize());
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(2, f.length);
  TEST_ASSERT_EQUAL_UINT8(0x1A, f.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF8, f.data[1]);
  TEST_ASSERT_EQUAL_UINT32(1, h.client.ok_count());
}

// "SEARCHING..." antes do dado e o normal na primeira leitura. Nao pode ser
// tratado como erro, senao a primeira consulta de toda sessao falharia.
void test_searching_antes_do_dado_nao_atrapalha(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  h.elm.searching_once();

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(0x1A, f.data[0]);
}

// Com ATE1 o adaptador repete o comando antes de responder.
void test_eco_do_comando_nao_atrapalha(void) {
  Elm h;
  h.elm.set_echo(true);
  h.elm.on_pid(0x01, 0x05, "41 05 7B");

  const ParsedFrame f = h.client.read_pid(0x01, 0x05);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(0x7B, f.data[0]);
}

// PID que a ECU nao implementa. Nao adianta insistir: um unico envio.
void test_no_data_nao_gera_retentativa(void) {
  Elm h;
  h.elm.clear_written();
  const ParsedFrame f = h.client.read_pid(0x01, 0x2F);  // sem resposta configurada

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::NoData),
                        static_cast<int>(f.status));
  TEST_ASSERT_EQUAL_INT(1, h.elm.command_count());
}

// Lixo DEPOIS da resposta boa nao atrapalha, e nem gera retentativa: o
// parser processa linha a linha e devolve a primeira que for valida.
//
// Este teste comecou errado. Eu supunha que qualquer lixo estragaria a
// leitura e forcaria uma nova tentativa — e o parser mostrou ser mais robusto
// do que eu presumi. Vale registrar o comportamento real: uma retentativa
// desnecessaria custaria banda do barramento a toa.
void test_lixo_depois_da_resposta_boa_nao_atrapalha(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  h.elm.garbage_once();
  h.elm.clear_written();

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(0x1A, f.data[0]);
  TEST_ASSERT_EQUAL_INT(1, h.elm.command_count());  // um envio bastou
}

// Agora a retentativa de verdade: a PRIMEIRA resposta vem corrompida por
// inteiro (sem dado nenhum), a segunda vem boa.
void test_resposta_corrompida_gera_retentativa(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  h.elm.corrupt_next(1);
  h.elm.clear_written();

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(0x1A, f.data[0]);
  TEST_ASSERT_EQUAL_INT(2, h.elm.command_count());  // uma retentativa
  TEST_ASSERT_EQUAL_UINT32(1, h.client.rejected_count());
}

// Corrompida em TODAS as tentativas: desiste depois de max_retries, sem
// insistir para sempre.
void test_desiste_apos_esgotar_as_retentativas(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  h.elm.corrupt_next(10);
  h.elm.clear_written();

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_FALSE(f.ok());
  // 1 envio + 2 retentativas (max_retries padrao = 2).
  TEST_ASSERT_EQUAL_INT(3, h.elm.command_count());
}

// Adaptador mudo: o cliente respeita o timeout e desiste, em vez de travar.
void test_adaptador_mudo_respeita_o_timeout(void) {
  Elm h;
  h.elm.mute_next(10);
  const std::uint32_t antes = h.clock.now_ms();

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);

  TEST_ASSERT_FALSE(f.ok());
  // Tres envios (1 + 2 retentativas), cada um esperando o timeout inteiro.
  const std::uint32_t decorrido = h.clock.now_ms() - antes;
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3000, decorrido);
}

// Resposta bem formada, mas com MENOS bytes do que o catalogo manda. Pode ser
// resposta de outra ECU — rejeitar e o correto.
void test_tamanho_diferente_do_catalogo_e_rejeitado(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A");  // RPM tem 2 bytes, veio 1

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_FALSE(f.ok());
  TEST_ASSERT_GREATER_THAN_UINT32(0, h.client.rejected_count());
}

void test_le_a_tensao_do_adaptador(void) {
  Elm h;
  h.elm.on_at("ATRV", "13.8V");

  float volts = 0.0F;
  TEST_ASSERT_TRUE(h.client.read_adapter_voltage(volts));
  TEST_ASSERT_FLOAT_WITHIN(0.05F, 13.8F, volts);
}

void test_tensao_fora_da_faixa_fisica_e_recusada(void) {
  float volts = 0.0F;
  {
    Elm h;
    h.elm.on_at("ATRV", "99.9V");  // nao existe em rede de 12 V
    TEST_ASSERT_FALSE(h.client.read_adapter_voltage(volts));
  }
  {
    Elm h;
    h.elm.on_at("ATRV", "0.4V");  // sem ECU viva
    TEST_ASSERT_FALSE(h.client.read_adapter_voltage(volts));
  }
  {
    Elm h;
    h.elm.on_at("ATRV", "SEM RESPOSTA");
    TEST_ASSERT_FALSE(h.client.read_adapter_voltage(volts));
  }
}

// Mesmo com o adaptador conversando normalmente, a allowlist continua
// valendo: um modo proibido nao gera UM BYTE no ar.
void test_allowlist_vale_mesmo_com_adaptador_respondendo(void) {
  Elm h;
  TEST_ASSERT_TRUE(h.client.initialize());
  h.elm.clear_written();

  h.client.read_pid(0x04, 0x00);  // limpar DTCs
  h.client.read_pid(0x08, 0x00);  // comandar atuador

  TEST_ASSERT_EQUAL_UINT32(0, h.elm.written().size());
}

// send_at() nao pode virar porta dos fundos para a allowlist.
void test_send_at_tambem_respeita_a_allowlist(void) {
  Elm h;
  char resposta[64];
  TEST_ASSERT_EQUAL_UINT32(
      0, h.client.send_at("ATSH7E0", resposta, sizeof(resposta)));
  TEST_ASSERT_EQUAL_UINT32(0, h.elm.written().size());
}

// Canal cai no meio do envio: o cliente nao pode ficar esperando resposta de
// um comando que nunca saiu.
void test_falha_de_escrita_e_reportada_na_hora(void) {
  Elm h;
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  h.elm.fail_write_next(1);

  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::UnableToConnect),
                        static_cast<int>(f.status));
}

// Resposta que nao contem o esperado e nao termina em separador: exercita a
// varredura completa do texto sem encontrar o trecho.
void test_resposta_inesperada_na_inicializacao(void) {
  Elm h;
  h.elm.on_at("ATE0", "BUS INIT: ERROR");
  TEST_ASSERT_FALSE(h.client.initialize());
}

// ---------------------------------------------------------------------------
//  CODIGOS DE FALHA
// ---------------------------------------------------------------------------

// A resposta do modo 03 nao ecoa PID: vem "43 <contagem> <codigos...>".
// Usar o parser normal aqui cobraria um PID que nao existe.
void test_le_codigos_de_falha(void) {
  Elm h;
  // Dois codigos: P0301 e P0420.
  h.elm.on_mode(0x03, "43 02 03 01 04 20");

  const auto lista = h.client.read_dtcs(kanri::obd::DtcKind::Stored);
  TEST_ASSERT_EQUAL_UINT8(2, lista.count);
  TEST_ASSERT_EQUAL_STRING("P0301", lista.items[0].text);
  TEST_ASSERT_EQUAL_STRING("P0420", lista.items[1].text);
}

// O comando enviado e SO o modo. Mandar "0300" seria malformado, e alguns
// adaptadores respondem "?" a isso.
void test_modo_de_dtc_e_pedido_sem_pid(void) {
  Elm h;
  h.elm.on_mode(0x03, "43 01 03 01");
  h.elm.clear_written();

  h.client.read_dtcs(kanri::obd::DtcKind::Stored);
  TEST_ASSERT_EQUAL_STRING("03\r", h.elm.written().c_str());
}

void test_le_os_tres_tipos_de_codigo(void) {
  Elm h;
  h.elm.on_mode(0x03, "43 01 03 01");  // gravado
  h.elm.on_mode(0x07, "47 01 01 71");  // pendente
  h.elm.on_mode(0x0A, "4A 01 04 20");  // permanente

  TEST_ASSERT_EQUAL_STRING("P0301",
      h.client.read_dtcs(kanri::obd::DtcKind::Stored).items[0].text);
  TEST_ASSERT_EQUAL_STRING("P0171",
      h.client.read_dtcs(kanri::obd::DtcKind::Pending).items[0].text);
  TEST_ASSERT_EQUAL_STRING("P0420",
      h.client.read_dtcs(kanri::obd::DtcKind::Permanent).items[0].text);
}

// "Nenhum codigo" e uma boa noticia; "nao consegui ler" nao e. As duas
// devolvem lista vazia, e quem chama precisa poder distinguir.
void test_sem_codigos_e_diferente_de_falha_na_leitura(void) {
  {
    Elm h;
    h.elm.on_mode(0x03, "43 00");  // a ECU respondeu: zero codigos
    const auto l = h.client.read_dtcs(kanri::obd::DtcKind::Stored);
    TEST_ASSERT_EQUAL_UINT8(0, l.count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::Ok),
                          static_cast<int>(h.client.last_dtc_status()));
  }
  {
    Elm h;
    h.elm.mute_next(5);  // adaptador mudo: nao sabemos se ha codigos
    const auto l = h.client.read_dtcs(kanri::obd::DtcKind::Stored);
    TEST_ASSERT_EQUAL_UINT8(0, l.count);
    TEST_ASSERT_NOT_EQUAL_INT(static_cast<int>(ParseStatus::Ok),
                              static_cast<int>(h.client.last_dtc_status()));
  }
}

// A allowlist vale aqui tambem: o Modo 04 (limpar) nunca sai.
void test_leitura_de_dtc_nao_abre_caminho_para_limpar(void) {
  Elm h;
  h.elm.clear_written();
  // Nenhum DtcKind mapeia para 0x04 — mas garantimos pela consequencia:
  // depois de ler os tres tipos, nada com "04" foi ao barramento.
  h.client.read_dtcs(kanri::obd::DtcKind::Stored);
  h.client.read_dtcs(kanri::obd::DtcKind::Pending);
  h.client.read_dtcs(kanri::obd::DtcKind::Permanent);

  const std::string& enviado = h.elm.written();
  TEST_ASSERT_TRUE(enviado.find("04") == std::string::npos);
}

void test_dtc_sem_conexao_e_com_falha_de_escrita(void) {
  {
    Elm h;
    h.elm.disconnect();
    const auto l = h.client.read_dtcs(kanri::obd::DtcKind::Stored);
    TEST_ASSERT_EQUAL_UINT8(0, l.count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::UnableToConnect),
                          static_cast<int>(h.client.last_dtc_status()));
  }
  {
    Elm h;
    h.elm.fail_write_next(1);
    h.client.read_dtcs(kanri::obd::DtcKind::Stored);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::UnableToConnect),
                          static_cast<int>(h.client.last_dtc_status()));
  }
}

// Resposta corrompida e passageira: vale tentar de novo.
void test_dtc_corrompido_gera_retentativa(void) {
  Elm h;
  h.elm.on_mode(0x03, "43 01 03 01");
  h.elm.corrupt_next(1);
  h.elm.clear_written();

  const auto l = h.client.read_dtcs(kanri::obd::DtcKind::Stored);
  TEST_ASSERT_EQUAL_UINT8(1, l.count);
  TEST_ASSERT_EQUAL_INT(2, h.elm.command_count());
}

// ---------------------------------------------------------------------------
//  ENTRADAS DEGENERADAS
// ---------------------------------------------------------------------------
//  Estes casos nao acontecem no fluxo normal, mas acontecem quando alguem usa
//  a API de um jeito que nao previmos — e a resposta certa e recusar, nunca
//  escrever fora de um buffer.

void test_send_at_com_argumentos_invalidos(void) {
  Elm h;
  char buf[16];
  TEST_ASSERT_EQUAL_UINT32(0, h.client.send_at("ATZ", nullptr, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, h.client.send_at("ATZ", buf, 0));
  TEST_ASSERT_EQUAL_UINT32(0, h.client.send_at(nullptr, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, h.elm.written().size());
}

// Comando maior do que qualquer AT legitimo: a allowlist barra antes.
void test_comando_longo_demais_e_barrado(void) {
  Elm h;
  char buf[16];
  TEST_ASSERT_EQUAL_UINT32(
      0, h.client.send_at("ATZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, h.elm.written().size());
}

// Buffer pequeno demais para a resposta: o cliente para de guardar, mas segue
// consumindo ate o prompt. Sem isso, o resto contaminaria a leitura seguinte.
void test_buffer_pequeno_nao_contamina_a_proxima_leitura(void) {
  Elm h;
  h.elm.on_at("ATI", "ELM327 v1.5 RESPOSTA BEM LONGA PARA NAO CABER");
  char pequeno[8];
  h.client.send_at("ATI", pequeno, sizeof(pequeno));

  // A proxima leitura tem de vir limpa.
  h.elm.on_pid(0x01, 0x0C, "41 0C 1A F8");
  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_TRUE(f.ok());
  TEST_ASSERT_EQUAL_UINT8(0x1A, f.data[0]);
}

// A resposta do ATRV varia entre adaptadores: com e sem decimal, com espaco,
// em minusculas. Todas precisam funcionar; texto sem numero, nao.
void test_formatos_de_tensao_aceitos_e_recusados(void) {
  struct Caso { const char* resposta; bool aceita; float valor; };
  const Caso casos[] = {
      {"13.8V", true, 13.8F},
      {"12V", true, 12.0F},        // sem decimal
      {"14.02V", true, 14.02F},    // duas casas
      {" 13.5 V", true, 13.5F},    // com espacos
      {"13.8", true, 13.8F},       // sem o V
      {"NO DATA", false, 0.0F},    // sem numero nenhum
      {"V", false, 0.0F},
      {"999.9V", false, 0.0F},     // fora da faixa fisica
      {"0.1V", false, 0.0F},       // sem ECU viva
  };
  for (const Caso& c : casos) {
    Elm h;
    h.elm.on_at("ATRV", c.resposta);
    float volts = -1.0F;
    const bool ok = h.client.read_adapter_voltage(volts);
    TEST_ASSERT_EQUAL_INT_MESSAGE(c.aceita, ok, c.resposta);
    if (c.aceita) TEST_ASSERT_FLOAT_WITHIN(0.05F, c.valor, volts);
  }
}

// A resposta do ELM327 pode vir em minusculas; "ok" tem de contar como "OK".
void test_inicializacao_aceita_resposta_em_minusculas(void) {
  Elm h;
  h.elm.on_at("ATE0", "ok");
  h.elm.on_at("ATL0", "o k");   // com espaco no meio, como alguns clones
  TEST_ASSERT_TRUE(h.client.initialize());
}

void test_sem_conexao_nao_tenta_ler(void) {
  Elm h;
  h.elm.disconnect();
  const ParsedFrame f = h.client.read_pid(0x01, 0x0C);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::UnableToConnect),
                        static_cast<int>(f.status));
  TEST_ASSERT_FALSE(h.client.initialize());
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

  RUN_TEST(test_aceita_configuracao_customizada);

  RUN_TEST(test_inicializacao_envia_a_sequencia_at_completa);
  RUN_TEST(test_inicializacao_falha_se_um_passo_obrigatorio_nao_responde);
  RUN_TEST(test_inicializacao_falha_com_adaptador_mudo);
  RUN_TEST(test_le_rpm_de_verdade);
  RUN_TEST(test_searching_antes_do_dado_nao_atrapalha);
  RUN_TEST(test_eco_do_comando_nao_atrapalha);
  RUN_TEST(test_no_data_nao_gera_retentativa);
  RUN_TEST(test_lixo_depois_da_resposta_boa_nao_atrapalha);
  RUN_TEST(test_resposta_corrompida_gera_retentativa);
  RUN_TEST(test_desiste_apos_esgotar_as_retentativas);
  RUN_TEST(test_adaptador_mudo_respeita_o_timeout);
  RUN_TEST(test_tamanho_diferente_do_catalogo_e_rejeitado);
  RUN_TEST(test_le_a_tensao_do_adaptador);
  RUN_TEST(test_tensao_fora_da_faixa_fisica_e_recusada);
  RUN_TEST(test_allowlist_vale_mesmo_com_adaptador_respondendo);
  RUN_TEST(test_send_at_tambem_respeita_a_allowlist);
  RUN_TEST(test_le_codigos_de_falha);
  RUN_TEST(test_modo_de_dtc_e_pedido_sem_pid);
  RUN_TEST(test_le_os_tres_tipos_de_codigo);
  RUN_TEST(test_sem_codigos_e_diferente_de_falha_na_leitura);
  RUN_TEST(test_leitura_de_dtc_nao_abre_caminho_para_limpar);
  RUN_TEST(test_dtc_sem_conexao_e_com_falha_de_escrita);
  RUN_TEST(test_dtc_corrompido_gera_retentativa);

  RUN_TEST(test_send_at_com_argumentos_invalidos);
  RUN_TEST(test_comando_longo_demais_e_barrado);
  RUN_TEST(test_buffer_pequeno_nao_contamina_a_proxima_leitura);
  RUN_TEST(test_formatos_de_tensao_aceitos_e_recusados);
  RUN_TEST(test_inicializacao_aceita_resposta_em_minusculas);
  RUN_TEST(test_falha_de_escrita_e_reportada_na_hora);
  RUN_TEST(test_resposta_inesperada_na_inicializacao);
  RUN_TEST(test_sem_conexao_nao_tenta_ler);

  RUN_TEST(test_fake_transport_registra_escritas);
  RUN_TEST(test_fake_transport_desconectado_nao_aceita_escrita);
  RUN_TEST(test_fake_transport_entrega_o_que_foi_enfileirado);
  RUN_TEST(test_fake_transport_respeita_o_tamanho_do_buffer);
  RUN_TEST(test_fake_transport_simula_falha_de_conexao);
  RUN_TEST(test_fake_clock_avanca_sob_controle_do_teste);

  return UNITY_END();
}
