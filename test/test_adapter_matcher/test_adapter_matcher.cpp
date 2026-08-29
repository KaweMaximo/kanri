// ============================================================================
//  Testes da escolha do adaptador Bluetooth
// ============================================================================
//  O cenario que estes testes protegem: o ESP32 varre o Bluetooth dentro de um
//  carro e acha o celular do motorista, um fone, a central multimidia e o
//  aparelho do carro ao lado. Escolher errado nao e so "nao funciona" — e
//  abrir um canal com um aparelho desconhecido e passar a tratar o que ele
//  responde como telemetria do motor.
//
//  Nome de dispositivo Bluetooth e escolhido por QUEM O ANUNCIA. Nada impede
//  um aparelho de se chamar "OBDII". Por isso ha testes de entrada hostil
//  aqui, no mesmo espirito do parser do ELM327.
// ============================================================================

#include <unity.h>

#include <cstring>

#include "kanri_obd/adapter_matcher.h"

using kanri::obd::DiscoveredDevice;
using kanri::obd::MatchResult;
using kanri::obd::select_adapter;

void setUp(void) {}
void tearDown(void) {}

namespace {

/// Monta um dispositivo de varredura, truncando com seguranca.
DiscoveredDevice dev(const char* nome, const char* mac, std::int8_t rssi) {
  DiscoveredDevice d{};
  std::size_t i = 0;
  for (; nome[i] != '\0' && i + 1 < kanri::obd::kMaxDeviceNameLen; ++i) {
    d.name[i] = nome[i];
  }
  d.name[i] = '\0';
  std::size_t j = 0;
  for (; mac[j] != '\0' && j + 1 < kanri::obd::kMaxDeviceMacLen; ++j) {
    d.mac[j] = mac[j];
  }
  d.mac[j] = '\0';
  d.rssi = rssi;
  return d;
}

void assert_res(MatchResult esperado, MatchResult obtido) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(esperado), static_cast<int>(obtido));
}

}  // namespace

// ---------------------------------------------------------------------------
//  CAMINHO FELIZ
// ---------------------------------------------------------------------------

void test_acha_pelo_nome_exato(void) {
  const DiscoveredDevice lista[] = {
      dev("iPhone de Kawe", "AA:11:22:33:44:55", -60),
      dev("OBDII", "11:22:33:44:55:66", -70),
      dev("Galaxy Buds", "BB:11:22:33:44:55", -50),
  };
  const auto r = select_adapter(lista, 3, "OBDII", "");
  assert_res(MatchResult::Found, r.result);
  TEST_ASSERT_TRUE(r.found());
  TEST_ASSERT_EQUAL_INT(1, r.index);
}

void test_nome_ignora_maiusculas_e_minusculas(void) {
  const DiscoveredDevice lista[] = {dev("obdii", "11:22:33:44:55:66", -70)};
  assert_res(MatchResult::Found, select_adapter(lista, 1, "OBDII", "").result);
  assert_res(MatchResult::Found, select_adapter(lista, 1, "ObDiI", "").result);
}

void test_acha_pelo_mac(void) {
  const DiscoveredDevice lista[] = {
      dev("OBDII", "11:22:33:44:55:66", -70),
      dev("OBDII", "AA:BB:CC:DD:EE:FF", -80),
  };
  const auto r = select_adapter(lista, 2, "", "AA:BB:CC:DD:EE:FF");
  assert_res(MatchResult::Found, r.result);
  TEST_ASSERT_EQUAL_INT(1, r.index);
}

void test_mac_ignora_caixa(void) {
  const DiscoveredDevice lista[] = {dev("OBDII", "aa:bb:cc:dd:ee:ff", -70)};
  assert_res(MatchResult::Found,
             select_adapter(lista, 1, "", "AA:BB:CC:DD:EE:FF").result);
}

// ---------------------------------------------------------------------------
//  AS REGRAS QUE PROTEGEM DE CONECTAR NO APARELHO ERRADO
// ---------------------------------------------------------------------------

// Dois adaptadores com o MESMO nome — o do seu carro e o do carro ao lado.
// O sinal mais forte (RSSI maior, menos negativo) tende a ser o mais proximo.
void test_empate_no_nome_vence_o_sinal_mais_forte(void) {
  const DiscoveredDevice lista[] = {
      dev("OBDII", "11:11:11:11:11:11", -85),  // longe
      dev("OBDII", "22:22:22:22:22:22", -42),  // perto  <-- este
      dev("OBDII", "33:33:33:33:33:33", -70),
  };
  const auto r = select_adapter(lista, 3, "OBDII", "");
  assert_res(MatchResult::Found, r.result);
  TEST_ASSERT_EQUAL_INT(1, r.index);
}

// A REGRA MAIS IMPORTANTE: com MAC configurado, NAO caimos para o nome.
// Quem fixou um MAC quer aquele aparelho; conectar em outro que por acaso
// tenha o mesmo nome anularia exatamente a protecao que fixar o MAC oferece.
void test_mac_configurado_nao_cai_para_o_nome(void) {
  const DiscoveredDevice lista[] = {
      dev("OBDII", "11:22:33:44:55:66", -40),  // nome bate, MAC nao
  };
  const auto r = select_adapter(lista, 1, "OBDII", "AA:BB:CC:DD:EE:FF");
  assert_res(MatchResult::MacNotFound, r.result);
  TEST_ASSERT_FALSE(r.found());
  TEST_ASSERT_EQUAL_INT(-1, r.index);
}

// Correspondencia e EXATA: prefixo nao vale.
void test_nome_parecido_nao_casa(void) {
  const DiscoveredDevice lista[] = {
      dev("OBDII_FALSO", "11:22:33:44:55:66", -40),
      dev("OBD", "22:22:22:22:22:22", -40),
      dev("MEU OBDII", "33:33:33:33:33:33", -40),
  };
  assert_res(MatchResult::NameNotFound,
             select_adapter(lista, 3, "OBDII", "").result);
}

// ---------------------------------------------------------------------------
//  ENTRADA HOSTIL — o nome vem de quem anuncia
// ---------------------------------------------------------------------------

// Um nome sem terminador nulo faria qualquer strcmp ler alem do buffer.
void test_nome_sem_terminador_e_ignorado(void) {
  DiscoveredDevice d{};
  std::memset(d.name, 'A', sizeof(d.name));  // 32 bytes, nenhum nulo
  std::memcpy(d.mac, "11:22:33:44:55:66", 18);
  d.rssi = -40;
  const DiscoveredDevice lista[] = {d};

  // Nao pode casar, e — principalmente — nao pode ler fora do buffer.
  assert_res(MatchResult::NameNotFound,
             select_adapter(lista, 1, "AAAAAAAA", "").result);
}

void test_mac_sem_terminador_e_ignorado(void) {
  DiscoveredDevice d{};
  std::memcpy(d.name, "OBDII", 6);
  std::memset(d.mac, '1', sizeof(d.mac));  // sem nulo
  d.rssi = -40;
  const DiscoveredDevice lista[] = {d};
  assert_res(MatchResult::NameNotFound,
             select_adapter(lista, 1, "OBDII", "").result);
}

// Bytes de controle num "nome" nao sao nome legitimo. Aceitar abriria espaco
// para lixo ir parar no display.
void test_nome_com_bytes_de_controle_e_ignorado(void) {
  DiscoveredDevice d{};
  const char ruim[] = {'O', 'B', '\x01', 'D', 'I', 'I', '\0'};
  std::memcpy(d.name, ruim, sizeof(ruim));
  std::memcpy(d.mac, "11:22:33:44:55:66", 18);
  d.rssi = -40;
  const DiscoveredDevice lista[] = {d};
  assert_res(MatchResult::NameNotFound,
             select_adapter(lista, 1, "OBDII", "").result);
}

// Um dispositivo malformado no meio da lista nao pode atrapalhar a escolha
// do dispositivo bom que vem depois.
void test_dispositivo_ruim_nao_impede_o_bom(void) {
  DiscoveredDevice ruim{};
  std::memset(ruim.name, 'X', sizeof(ruim.name));  // sem nulo
  std::memcpy(ruim.mac, "00:00:00:00:00:00", 18);
  ruim.rssi = -10;  // sinal otimo, mas invalido

  const DiscoveredDevice lista[] = {
      ruim,
      dev("OBDII", "11:22:33:44:55:66", -80),
  };
  const auto r = select_adapter(lista, 2, "OBDII", "");
  assert_res(MatchResult::Found, r.result);
  TEST_ASSERT_EQUAL_INT(1, r.index);
}

// ---------------------------------------------------------------------------
//  CASOS DEGENERADOS
// ---------------------------------------------------------------------------

void test_lista_vazia(void) {
  assert_res(MatchResult::NoDevices, select_adapter(nullptr, 0, "OBDII", "").result);
  const DiscoveredDevice lista[] = {dev("X", "11:22:33:44:55:66", -40)};
  assert_res(MatchResult::NoDevices, select_adapter(lista, 0, "OBDII", "").result);
}

void test_sem_alvo_configurado(void) {
  const DiscoveredDevice lista[] = {dev("OBDII", "11:22:33:44:55:66", -40)};
  assert_res(MatchResult::NoTarget, select_adapter(lista, 1, "", "").result);
  assert_res(MatchResult::NoTarget, select_adapter(lista, 1, nullptr, nullptr).result);
}

void test_nao_encontrado(void) {
  const DiscoveredDevice lista[] = {
      dev("iPhone", "11:22:33:44:55:66", -40),
      dev("MICRO88", "22:22:22:22:22:22", -50),
  };
  assert_res(MatchResult::NameNotFound,
             select_adapter(lista, 2, "OBDII", "").result);
}

// Dispositivo sem nome anunciado e comum: nao pode casar com alvo vazio nem
// derrubar a busca.
void test_dispositivo_sem_nome(void) {
  const DiscoveredDevice lista[] = {
      dev("", "11:22:33:44:55:66", -40),
      dev("OBDII", "22:22:22:22:22:22", -50),
  };
  const auto r = select_adapter(lista, 2, "OBDII", "");
  assert_res(MatchResult::Found, r.result);
  TEST_ASSERT_EQUAL_INT(1, r.index);
}

// ---------------------------------------------------------------------------
//  INVARIANTES
// ---------------------------------------------------------------------------

// Para QUALQUER entrada: ou devolve Found com indice dentro da lista, ou
// devolve -1. Nunca um indice invalido, que viraria leitura fora do array.
void test_indice_sempre_valido(void) {
  std::uint32_t semente = 0xBEEF;
  for (int it = 0; it < 2000; ++it) {
    DiscoveredDevice lista[6];
    std::size_t n = 0;
    for (std::size_t i = 0; i < 6; ++i) {
      semente = semente * 1103515245U + 12345U;
      auto* bytes = reinterpret_cast<unsigned char*>(&lista[i]);
      for (std::size_t b = 0; b < sizeof(DiscoveredDevice); ++b) {
        semente = semente * 1103515245U + 12345U;
        bytes[b] = static_cast<unsigned char>((semente >> 16) & 0xFF);
      }
      ++n;
    }
    const auto r = select_adapter(lista, n, "OBDII", "");
    if (r.result == MatchResult::Found) {
      TEST_ASSERT_TRUE(r.index >= 0 && static_cast<std::size_t>(r.index) < n);
    } else {
      TEST_ASSERT_EQUAL_INT(-1, r.index);
    }
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(r.result));
  }
}

// ---------------------------------------------------------------------------
//  MAC -> BYTES (para conectar sem varredura)
// ---------------------------------------------------------------------------

void test_parse_mac_valido(void) {
  std::uint8_t b[6] = {};
  TEST_ASSERT_TRUE(kanri::obd::parse_mac("00:10:CC:4F:36:03", b));
  TEST_ASSERT_EQUAL_UINT8(0x00, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x10, b[1]);
  TEST_ASSERT_EQUAL_UINT8(0xCC, b[2]);
  TEST_ASSERT_EQUAL_UINT8(0x4F, b[3]);
  TEST_ASSERT_EQUAL_UINT8(0x36, b[4]);
  TEST_ASSERT_EQUAL_UINT8(0x03, b[5]);
}

void test_parse_mac_aceita_caixa_e_separadores(void) {
  std::uint8_t a[6] = {};
  std::uint8_t c[6] = {};
  TEST_ASSERT_TRUE(kanri::obd::parse_mac("aa:bb:cc:dd:ee:ff", a));
  TEST_ASSERT_TRUE(kanri::obd::parse_mac("AA-BB-CC-DD-EE-FF", c));
  for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_UINT8(a[i], c[i]);
  TEST_ASSERT_EQUAL_UINT8(0xAA, a[0]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, a[5]);
}

// Recusar e obrigatorio: um MAC mal interpretado faria o firmware tentar
// conectar num endereco qualquer.
void test_parse_mac_recusa_malformado(void) {
  std::uint8_t b[6] = {};
  const char* ruins[] = {
      "",                        // vazio
      "00:10:CC:4F:36",          // faltando um par
      "00:10:CC:4F:36:03:AA",    // sobrando
      "00:10:CC:4F:36:0",        // par incompleto
      "0010CC4F3603",            // sem separador
      "GG:10:CC:4F:36:03",       // digito invalido
      "00:10:CC:4F:36:0G",
      "00/10/CC/4F/36/03",       // separador errado
      "00:10:CC:4F:36:03 ",      // lixo no fim
  };
  for (const char* r : ruins) {
    TEST_ASSERT_FALSE_MESSAGE(kanri::obd::parse_mac(r, b), r);
  }
  TEST_ASSERT_FALSE(kanri::obd::parse_mac(nullptr, b));
  TEST_ASSERT_FALSE(kanri::obd::parse_mac("00:10:CC:4F:36:03", nullptr));
}

// Em caso de recusa, a saida nao pode ser tocada: um parse parcial deixaria
// metade do endereco novo e metade do antigo.
void test_parse_mac_recusado_nao_altera_a_saida(void) {
  std::uint8_t b[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_FALSE(kanri::obd::parse_mac("00:10:CC:ZZ:36:03", b));
  for (std::uint8_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i + 1, b[i]);
  }
}

// Todo MAC que a configuracao aceita tem de ser convertivel: se validate()
// diz que e valido, parse_mac nao pode recusar.
void test_todo_mac_valido_para_a_config_e_convertivel(void) {
  const char* validos[] = {
      "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "00:10:CC:4F:36:03",
      "1A:2B:3C:4D:5E:6F", "aa:bb:cc:dd:ee:ff",
  };
  std::uint8_t b[6];
  for (const char* m : validos) {
    TEST_ASSERT_TRUE_MESSAGE(kanri::obd::parse_mac(m, b), m);
  }
}

void test_to_string_cobre_todos_os_resultados(void) {
  const MatchResult todos[] = {
      MatchResult::Found, MatchResult::NoDevices, MatchResult::NoTarget,
      MatchResult::MacNotFound, MatchResult::NameNotFound,
  };
  for (const MatchResult r : todos) {
    TEST_ASSERT_NOT_NULL(kanri::obd::to_string(r));
    TEST_ASSERT_TRUE(kanri::obd::to_string(r)[0] != '\0');
  }
  TEST_ASSERT_EQUAL_STRING("Unknown",
                           kanri::obd::to_string(static_cast<MatchResult>(99)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_acha_pelo_nome_exato);
  RUN_TEST(test_nome_ignora_maiusculas_e_minusculas);
  RUN_TEST(test_acha_pelo_mac);
  RUN_TEST(test_mac_ignora_caixa);

  RUN_TEST(test_empate_no_nome_vence_o_sinal_mais_forte);
  RUN_TEST(test_mac_configurado_nao_cai_para_o_nome);
  RUN_TEST(test_nome_parecido_nao_casa);

  RUN_TEST(test_nome_sem_terminador_e_ignorado);
  RUN_TEST(test_mac_sem_terminador_e_ignorado);
  RUN_TEST(test_nome_com_bytes_de_controle_e_ignorado);
  RUN_TEST(test_dispositivo_ruim_nao_impede_o_bom);

  RUN_TEST(test_lista_vazia);
  RUN_TEST(test_sem_alvo_configurado);
  RUN_TEST(test_nao_encontrado);
  RUN_TEST(test_dispositivo_sem_nome);

  RUN_TEST(test_indice_sempre_valido);
  RUN_TEST(test_parse_mac_valido);
  RUN_TEST(test_parse_mac_aceita_caixa_e_separadores);
  RUN_TEST(test_parse_mac_recusa_malformado);
  RUN_TEST(test_parse_mac_recusado_nao_altera_a_saida);
  RUN_TEST(test_todo_mac_valido_para_a_config_e_convertivel);
  RUN_TEST(test_to_string_cobre_todos_os_resultados);
  return UNITY_END();
}
