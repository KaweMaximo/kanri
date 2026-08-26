// ============================================================================
//  Testes de configuracao persistente
// ============================================================================
//  Cenario que estamos protegendo: o carro e desligado durante uma gravacao na
//  flash e a configuracao fica pela metade. Na proxima partida o firmware tem
//  de perceber, corrigir e continuar funcionando — nunca travar nem operar com
//  valores absurdos (ex.: consultar o carro a cada 0 ms).
// ============================================================================

#include <unity.h>

#include <cstring>

#include "fake_config_store.h"
#include "kanri_config/settings.h"

using kanri::config::clamp_to_valid;
using kanri::config::default_settings;
using kanri::config::KanriSettings;
using kanri::config::SettingsError;
using kanri::config::validate;

void setUp(void) {}
void tearDown(void) {}

static void assert_error(SettingsError expected, SettingsError actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

// ---------------------------------------------------------------------------
//  PADROES E VALIDACAO
// ---------------------------------------------------------------------------

// O padrao de fabrica e o porto seguro: se ele fosse invalido, nao haveria
// para onde voltar quando a flash falha.
void test_padrao_de_fabrica_e_valido(void) {
  assert_error(SettingsError::None, validate(default_settings()));
}

void test_padrao_de_fabrica_tem_valores_razoaveis(void) {
  const KanriSettings settings = default_settings();
  TEST_ASSERT_EQUAL_STRING("OBDII", settings.adapter_name);
  TEST_ASSERT_EQUAL_UINT8(1, settings.use_metric_units);  // km/h e Celsius
  TEST_ASSERT_GREATER_THAN_UINT16(0, settings.poll_interval_ms);
}

void test_intervalo_de_consulta_fora_da_faixa_e_recusado(void) {
  KanriSettings settings = default_settings();
  settings.poll_interval_ms = 0;  // consultar sem pausa entope o ELM327
  assert_error(SettingsError::PollIntervalOutOfRange, validate(settings));

  settings.poll_interval_ms = 60000;  // 1 minuto: inutil como telemetria
  assert_error(SettingsError::PollIntervalOutOfRange, validate(settings));
}

void test_timeout_fora_da_faixa_e_recusado(void) {
  KanriSettings settings = default_settings();
  settings.elm_timeout_ms = 10;
  assert_error(SettingsError::ElmTimeoutOutOfRange, validate(settings));
}

// Flash corrompida pode deixar um "booleano" com o byte 0x5A. Guardar isso
// como uint8_t (em vez de bool) torna a leitura legal, e a validacao pega.
void test_flag_com_byte_invalido_e_recusada(void) {
  KanriSettings settings = default_settings();
  settings.use_metric_units = 0x5A;
  assert_error(SettingsError::InvalidFlag, validate(settings));

  TEST_ASSERT_TRUE(clamp_to_valid(settings));
  TEST_ASSERT_EQUAL_UINT8(1, settings.use_metric_units);
  assert_error(SettingsError::None, validate(settings));
}

void test_brilho_acima_de_cem_e_recusado(void) {
  KanriSettings settings = default_settings();
  settings.display_brightness = 200;
  assert_error(SettingsError::BrightnessOutOfRange, validate(settings));
}

void test_sem_nome_e_sem_mac_e_recusado(void) {
  KanriSettings settings = default_settings();
  settings.adapter_name[0] = '\0';
  settings.adapter_mac[0] = '\0';
  assert_error(SettingsError::AdapterNameEmpty, validate(settings));
}

void test_mac_valido_e_aceito(void) {
  KanriSettings settings = default_settings();
  std::memcpy(settings.adapter_mac, "1A:2B:3C:4D:5E:6F", 18);
  assert_error(SettingsError::None, validate(settings));
}

void test_mac_malformado_e_recusado(void) {
  KanriSettings settings = default_settings();
  std::memcpy(settings.adapter_mac, "1A-2B-3C-4D-5E-6F", 18);
  assert_error(SettingsError::MalformedMac, validate(settings));

  std::memcpy(settings.adapter_mac, "1A:2B:3C\0", 9);
  assert_error(SettingsError::MalformedMac, validate(settings));
}

void test_esquema_de_outra_versao_e_detectado(void) {
  KanriSettings settings = default_settings();
  settings.schema_version = 99;
  assert_error(SettingsError::SchemaMismatch, validate(settings));
}

// Sem terminador nulo, qualquer strlen/strcmp nessa string leria memoria
// alheia. Detectar isso ANTES de tocar no conteudo e o que evita a falha.
void test_string_sem_terminador_nulo_e_detectada(void) {
  KanriSettings settings = default_settings();
  std::memset(settings.adapter_name, 'A', sizeof(settings.adapter_name));
  assert_error(SettingsError::AdapterNameNotTerminated, validate(settings));
}

// ---------------------------------------------------------------------------
//  CORRECAO AUTOMATICA (o fail-safe)
// ---------------------------------------------------------------------------

void test_clamp_corrige_valores_fora_da_faixa(void) {
  KanriSettings settings = default_settings();
  settings.poll_interval_ms = 0;
  settings.elm_timeout_ms = 60000;
  settings.display_brightness = 250;

  TEST_ASSERT_TRUE(clamp_to_valid(settings));
  assert_error(SettingsError::None, validate(settings));
  TEST_ASSERT_EQUAL_UINT16(kanri::config::kMinPollIntervalMs,
                           settings.poll_interval_ms);
  TEST_ASSERT_EQUAL_UINT16(kanri::config::kMaxElmTimeoutMs,
                           settings.elm_timeout_ms);
  TEST_ASSERT_EQUAL_UINT8(kanri::config::kMaxBrightness,
                          settings.display_brightness);
}

void test_clamp_nao_mexe_no_que_ja_esta_valido(void) {
  KanriSettings settings = default_settings();
  TEST_ASSERT_FALSE(clamp_to_valid(settings));
}

// Esquema desconhecido = layout de bytes desconhecido. Nao ha campo em que
// confiar, entao voltamos inteiro ao padrao de fabrica.
void test_clamp_reseta_tudo_quando_o_esquema_nao_bate(void) {
  KanriSettings settings = default_settings();
  settings.schema_version = 42;
  settings.poll_interval_ms = 1234;

  TEST_ASSERT_TRUE(clamp_to_valid(settings));
  TEST_ASSERT_EQUAL_UINT8(kanri::config::kSettingsSchemaVersion,
                          settings.schema_version);
  TEST_ASSERT_EQUAL_UINT16(default_settings().poll_interval_ms,
                           settings.poll_interval_ms);
}

// MAC ruim nao e motivo para desistir: ainda da para achar o adaptador
// pelo nome. Degradar em vez de falhar.
void test_clamp_descarta_mac_invalido_e_mantem_a_busca_por_nome(void) {
  KanriSettings settings = default_settings();
  std::memcpy(settings.adapter_mac, "LIXO!!!!!!!!!!!!!", 18);

  TEST_ASSERT_TRUE(clamp_to_valid(settings));
  assert_error(SettingsError::None, validate(settings));
  TEST_ASSERT_EQUAL_STRING("", settings.adapter_mac);
  TEST_ASSERT_TRUE(settings.adapter_name[0] != '\0');
}

void test_clamp_forca_terminador_em_string_corrompida(void) {
  KanriSettings settings = default_settings();
  std::memset(settings.adapter_name, 'A', sizeof(settings.adapter_name));

  TEST_ASSERT_TRUE(clamp_to_valid(settings));
  assert_error(SettingsError::None, validate(settings));
}

// A INVARIANTE MAIS IMPORTANTE DESTE MODULO: nao importa o lixo que venha da
// flash, depois de clamp_to_valid() a configuracao E valida. E essa garantia
// que permite ao firmware sempre inicializar.
void test_clamp_sempre_produz_configuracao_valida(void) {
  // Caso 1: flash apagada (todos os bits em 1) — o estado real de uma flash
  // nova ou apagada.
  KanriSettings all_ones;
  std::memset(&all_ones, 0xFF, sizeof(all_ones));
  clamp_to_valid(all_ones);
  assert_error(SettingsError::None, validate(all_ones));

  // Caso 2: tudo zerado.
  KanriSettings all_zeros;
  std::memset(&all_zeros, 0x00, sizeof(all_zeros));
  clamp_to_valid(all_zeros);
  assert_error(SettingsError::None, validate(all_zeros));

  // Caso 3: bytes pseudoaleatorios, com semente fixa (reproduzivel).
  std::uint32_t seed = 0xC0FFEEU;
  for (int iteration = 0; iteration < 500; ++iteration) {
    KanriSettings noisy{};
    auto* bytes = reinterpret_cast<unsigned char*>(&noisy);
    for (std::size_t i = 0; i < sizeof(noisy); ++i) {
      seed = (seed * 1103515245U) + 12345U;
      bytes[i] = static_cast<unsigned char>((seed >> 16) & 0xFF);
    }
    clamp_to_valid(noisy);
    assert_error(SettingsError::None, validate(noisy));
  }
}

void test_to_string_nunca_devolve_nulo(void) {
  const SettingsError all[] = {
      SettingsError::None,
      SettingsError::SchemaMismatch,
      SettingsError::AdapterNameEmpty,
      SettingsError::AdapterNameNotTerminated,
      SettingsError::PollIntervalOutOfRange,
      SettingsError::ElmTimeoutOutOfRange,
      SettingsError::BrightnessOutOfRange,
      SettingsError::MalformedMac,
      SettingsError::InvalidFlag,
  };
  for (const SettingsError error : all) {
    TEST_ASSERT_NOT_NULL(kanri::config::to_string(error));
  }
}

// ---------------------------------------------------------------------------
//  CONTRATO DO IConfigStore (verificado contra o dublê de teste)
// ---------------------------------------------------------------------------

void test_store_devolve_padroes_quando_esta_vazio(void) {
  kanri::test::FakeConfigStore store;
  KanriSettings loaded;
  // Suja o buffer de proposito: se load() nao cumprir o contrato, o teste
  // pega. memset e legal aqui exatamente porque a struct e um POD trivial.
  std::memset(&loaded, 0xAB, sizeof(loaded));

  TEST_ASSERT_FALSE(store.load(loaded));  // nada gravado
  // O contrato exige que `out` fique com os padroes, e nao meio preenchido.
  assert_error(SettingsError::None, validate(loaded));
  TEST_ASSERT_EQUAL_STRING("OBDII", loaded.adapter_name);
}

void test_store_faz_ida_e_volta_dos_dados(void) {
  kanri::test::FakeConfigStore store;
  KanriSettings saved = default_settings();
  saved.poll_interval_ms = 500;
  saved.display_brightness = 42;

  TEST_ASSERT_TRUE(store.save(saved));

  KanriSettings loaded{};
  TEST_ASSERT_TRUE(store.load(loaded));
  TEST_ASSERT_EQUAL_UINT16(500, loaded.poll_interval_ms);
  TEST_ASSERT_EQUAL_UINT8(42, loaded.display_brightness);
}

void test_store_com_leitura_falhando_ainda_entrega_padroes(void) {
  kanri::test::FakeConfigStore store;
  store.preload(default_settings());
  store.fail_load();

  KanriSettings loaded{};
  TEST_ASSERT_FALSE(store.load(loaded));
  assert_error(SettingsError::None, validate(loaded));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_padrao_de_fabrica_e_valido);
  RUN_TEST(test_padrao_de_fabrica_tem_valores_razoaveis);
  RUN_TEST(test_intervalo_de_consulta_fora_da_faixa_e_recusado);
  RUN_TEST(test_timeout_fora_da_faixa_e_recusado);
  RUN_TEST(test_brilho_acima_de_cem_e_recusado);
  RUN_TEST(test_flag_com_byte_invalido_e_recusada);
  RUN_TEST(test_sem_nome_e_sem_mac_e_recusado);
  RUN_TEST(test_mac_valido_e_aceito);
  RUN_TEST(test_mac_malformado_e_recusado);
  RUN_TEST(test_esquema_de_outra_versao_e_detectado);
  RUN_TEST(test_string_sem_terminador_nulo_e_detectada);

  RUN_TEST(test_clamp_corrige_valores_fora_da_faixa);
  RUN_TEST(test_clamp_nao_mexe_no_que_ja_esta_valido);
  RUN_TEST(test_clamp_reseta_tudo_quando_o_esquema_nao_bate);
  RUN_TEST(test_clamp_descarta_mac_invalido_e_mantem_a_busca_por_nome);
  RUN_TEST(test_clamp_forca_terminador_em_string_corrompida);
  RUN_TEST(test_clamp_sempre_produz_configuracao_valida);
  RUN_TEST(test_to_string_nunca_devolve_nulo);

  RUN_TEST(test_store_devolve_padroes_quando_esta_vazio);
  RUN_TEST(test_store_faz_ida_e_volta_dos_dados);
  RUN_TEST(test_store_com_leitura_falhando_ainda_entrega_padroes);

  return UNITY_END();
}
