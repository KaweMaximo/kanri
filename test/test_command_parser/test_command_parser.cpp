// ============================================================================
//  Testes dos comandos de configuracao
// ============================================================================
//  Estes comandos serao digitados dentro do carro, provavelmente com pressa e
//  com o notebook no colo. Entao a interpretacao precisa ser tolerante com a
//  forma (espacos, caixa, "set" opcional) e rigorosa com o conteudo (nao
//  aceitar valor que deixe o firmware sem conseguir operar).
// ============================================================================

#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "kanri_config/command_parser.h"

using kanri::config::apply_command;
using kanri::config::CommandAction;
using kanri::config::CommandError;
using kanri::config::default_settings;
using kanri::config::KanriSettings;
using kanri::config::parse_command;
using kanri::config::ParsedCommand;

void setUp(void) {}
void tearDown(void) {}

namespace {

ParsedCommand p(const char* linha) {
  return parse_command(linha, std::strlen(linha));
}

void assert_acao(CommandAction esperada, const ParsedCommand& c) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(esperada), static_cast<int>(c.action));
}

void assert_erro(CommandError esperado, const ParsedCommand& c) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(esperado), static_cast<int>(c.error));
}

}  // namespace

// ---------------------------------------------------------------------------
//  TOLERANCIA NA FORMA
// ---------------------------------------------------------------------------

void test_set_e_opcional(void) {
  assert_acao(CommandAction::SetName, p("set nome OBDII"));
  assert_acao(CommandAction::SetName, p("nome OBDII"));
  TEST_ASSERT_EQUAL_STRING("OBDII", p("set nome OBDII").text);
  TEST_ASSERT_EQUAL_STRING("OBDII", p("nome OBDII").text);
}

void test_espacos_extras_sao_ignorados(void) {
  TEST_ASSERT_EQUAL_STRING("OBDII", p("   nome    OBDII   ").text);
  TEST_ASSERT_EQUAL_STRING("OBDII", p("\tnome\tOBDII\t").text);
  assert_acao(CommandAction::Status, p("  status  "));
}

void test_terminadores_de_terminal_sao_ignorados(void) {
  assert_acao(CommandAction::Status, p("status\r\n"));
  assert_acao(CommandAction::Status, p("status\n"));
  TEST_ASSERT_EQUAL_STRING("OBDII", p("nome OBDII\r\n").text);
}

// O NOME do comando ignora caixa; o VALOR nao — nome Bluetooth diferencia
// maiusculas, e "obdii" nao e o mesmo dispositivo que "OBDII".
void test_comando_ignora_caixa_mas_o_valor_nao(void) {
  assert_acao(CommandAction::SetName, p("NOME OBDII"));
  assert_acao(CommandAction::SetName, p("NoMe OBDII"));
  assert_acao(CommandAction::Status, p("STATUS"));
  TEST_ASSERT_EQUAL_STRING("ObDiI", p("nome ObDiI").text);
}

void test_aceita_portugues_e_ingles(void) {
  assert_acao(CommandAction::Save, p("save"));
  assert_acao(CommandAction::Save, p("salvar"));
  assert_acao(CommandAction::Help, p("help"));
  assert_acao(CommandAction::Help, p("ajuda"));
  assert_acao(CommandAction::Help, p("?"));
  assert_acao(CommandAction::Restart, p("reiniciar"));
  assert_acao(CommandAction::ReadDtc, p("dtc"));
  assert_acao(CommandAction::ReadDtc, p("falhas"));
}

// Nome de adaptador pode ter espaco ("Android-Vlink BT"). O argumento e todo
// o resto da linha, nao so a proxima palavra.
void test_valor_pode_conter_espacos(void) {
  TEST_ASSERT_EQUAL_STRING("Android Vlink BT", p("nome Android Vlink BT").text);
}

// ---------------------------------------------------------------------------
//  RIGOR NO CONTEUDO
// ---------------------------------------------------------------------------

void test_linha_vazia_nao_e_erro(void) {
  assert_acao(CommandAction::None, p(""));
  assert_acao(CommandAction::None, p("   "));
  assert_acao(CommandAction::None, p("\r\n"));
  TEST_ASSERT_TRUE(p("").ok());
  assert_acao(CommandAction::None, parse_command(nullptr, 0));
}

void test_comando_desconhecido(void) {
  assert_erro(CommandError::UnknownCommand, p("voar"));
  assert_erro(CommandError::UnknownCommand, p("set voar alto"));
  assert_erro(CommandError::UnknownCommand, p("set"));
}

void test_falta_de_argumento(void) {
  assert_erro(CommandError::MissingArgument, p("nome"));
  assert_erro(CommandError::MissingArgument, p("intervalo"));
  assert_erro(CommandError::MissingArgument, p("set pin"));
}

// `mac` sem valor e legitimo: e assim que se limpa o MAC fixado e volta a
// casar por nome.
void test_mac_aceita_valor_vazio_para_limpar(void) {
  const ParsedCommand c = p("mac");
  assert_acao(CommandAction::SetMac, c);
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_EQUAL_STRING("", c.text);
}

void test_numero_invalido(void) {
  assert_erro(CommandError::InvalidNumber, p("intervalo abc"));
  assert_erro(CommandError::InvalidNumber, p("intervalo 12a"));
  assert_erro(CommandError::InvalidNumber, p("intervalo -5"));
  assert_erro(CommandError::InvalidNumber, p("brilho 1.5"));
}

// Numero gigante nao pode estourar o inteiro e virar um valor pequeno.
void test_numero_gigante_nao_estoura(void) {
  assert_erro(CommandError::InvalidNumber, p("intervalo 99999999999999999999"));
}

void test_linha_longa_demais(void) {
  char linha[200];
  std::memset(linha, 'a', sizeof(linha));
  linha[sizeof(linha) - 1] = '\0';
  assert_erro(CommandError::TooLong, p(linha));
}

void test_argumento_longo_demais(void) {
  char linha[80];
  std::strcpy(linha, "nome ");
  std::memset(linha + 5, 'X', 60);
  linha[65] = '\0';
  assert_erro(CommandError::ArgumentTooLong, p(linha));
}

// ---------------------------------------------------------------------------
//  APLICAR O COMANDO
// ---------------------------------------------------------------------------

void test_aplica_nome(void) {
  KanriSettings s = default_settings();
  TEST_ASSERT_TRUE(apply_command(p("nome V-LINK"), s));
  TEST_ASSERT_EQUAL_STRING("V-LINK", s.adapter_name);
  assert_erro(CommandError::None, p("nome V-LINK"));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::config::SettingsError::None),
                        static_cast<int>(validate(s)));
}

void test_aplica_mac_valido_e_recusa_invalido(void) {
  KanriSettings s = default_settings();
  TEST_ASSERT_TRUE(apply_command(p("mac 1A:2B:3C:4D:5E:6F"), s));
  TEST_ASSERT_EQUAL_STRING("1A:2B:3C:4D:5E:6F", s.adapter_mac);

  // MAC malformado seria descartado em silencio por clamp_to_valid mais
  // tarde. Recusar agora, com alguem lendo a resposta, e melhor.
  TEST_ASSERT_FALSE(apply_command(p("mac NAO-EH-MAC"), s));
  TEST_ASSERT_EQUAL_STRING("1A:2B:3C:4D:5E:6F", s.adapter_mac);  // intacto

  TEST_ASSERT_TRUE(apply_command(p("mac"), s));  // limpa
  TEST_ASSERT_EQUAL_STRING("", s.adapter_mac);
}

void test_aplica_numeros_dentro_da_faixa(void) {
  KanriSettings s = default_settings();
  TEST_ASSERT_TRUE(apply_command(p("intervalo 500"), s));
  TEST_ASSERT_EQUAL_UINT16(500, s.poll_interval_ms);
  TEST_ASSERT_TRUE(apply_command(p("timeout 2000"), s));
  TEST_ASSERT_EQUAL_UINT16(2000, s.elm_timeout_ms);
  TEST_ASSERT_TRUE(apply_command(p("brilho 40"), s));
  TEST_ASSERT_EQUAL_UINT8(40, s.display_brightness);
}

// Fora da faixa e RECUSADO, nao ajustado em silencio: quem digitou precisa
// saber que o valor nao valia.
void test_recusa_numeros_fora_da_faixa(void) {
  KanriSettings s = default_settings();
  const std::uint16_t antes = s.poll_interval_ms;

  TEST_ASSERT_FALSE(apply_command(p("intervalo 0"), s));
  TEST_ASSERT_FALSE(apply_command(p("intervalo 999999"), s));
  TEST_ASSERT_FALSE(apply_command(p("timeout 10"), s));
  TEST_ASSERT_FALSE(apply_command(p("brilho 200"), s));
  TEST_ASSERT_EQUAL_UINT16(antes, s.poll_interval_ms);
}

void test_aplica_unidades(void) {
  KanriSettings s = default_settings();
  TEST_ASSERT_TRUE(apply_command(p("unidades imperial"), s));
  TEST_ASSERT_EQUAL_UINT8(0, s.use_metric_units);
  TEST_ASSERT_TRUE(apply_command(p("unidades metrico"), s));
  TEST_ASSERT_EQUAL_UINT8(1, s.use_metric_units);
  TEST_ASSERT_FALSE(apply_command(p("unidades marcianas"), s));
}

void test_padroes_restaura_tudo(void) {
  KanriSettings s = default_settings();
  apply_command(p("nome OUTRO"), s);
  apply_command(p("intervalo 1000"), s);
  TEST_ASSERT_TRUE(apply_command(p("padroes"), s));
  TEST_ASSERT_EQUAL_STRING("OBDII", s.adapter_name);
  TEST_ASSERT_EQUAL_UINT16(default_settings().poll_interval_ms,
                           s.poll_interval_ms);
}

void test_comandos_de_acao_nao_mexem_na_configuracao(void) {
  KanriSettings s = default_settings();
  for (const char* c : {"status", "scan", "save", "load", "help", "reiniciar"}) {
    TEST_ASSERT_FALSE(apply_command(p(c), s));
  }
  TEST_ASSERT_EQUAL_STRING("OBDII", s.adapter_name);
}

void test_pin_longo_demais_e_recusado(void) {
  KanriSettings s = default_settings();
  TEST_ASSERT_FALSE(apply_command(p("pin 123456789012"), s));
  TEST_ASSERT_EQUAL_STRING("1234", s.adapter_pin);
}

// apply_command() e publico e aceita um ParsedCommand montado a mao — nao so
// os que saem do parser. Um SetName com valor vazio nao chega pelo parser
// (que exige argumento), mas chegaria por aqui, e deixaria o firmware sem
// alvo nenhum para procurar.
void test_apply_recusa_nome_vazio_montado_a_mao(void) {
  KanriSettings s = default_settings();
  s.adapter_mac[0] = '\0';  // sem MAC tambem

  ParsedCommand c;
  c.action = CommandAction::SetName;
  c.text[0] = '\0';

  TEST_ASSERT_FALSE(apply_command(c, s));
  TEST_ASSERT_EQUAL_STRING("OBDII", s.adapter_name);  // intacto

  // Com um MAC fixado, porem, apagar o nome e legitimo: ainda ha como achar
  // o adaptador.
  KanriSettings com_mac = default_settings();
  std::memcpy(com_mac.adapter_mac, "1A:2B:3C:4D:5E:6F", 18);
  TEST_ASSERT_TRUE(apply_command(c, com_mac));
  TEST_ASSERT_EQUAL_STRING("", com_mac.adapter_name);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::config::SettingsError::None),
                        static_cast<int>(validate(com_mac)));
}

// Acao com valor fora do enum (memoria corrompida): nao pode alterar nada.
void test_apply_ignora_acao_corrompida(void) {
  KanriSettings s = default_settings();
  ParsedCommand c;
  c.action = static_cast<CommandAction>(99);
  TEST_ASSERT_FALSE(apply_command(c, s));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kanri::config::SettingsError::None),
                        static_cast<int>(validate(s)));
}

// ---------------------------------------------------------------------------
//  INVARIANTES
// ---------------------------------------------------------------------------

// A INVARIANTE CENTRAL: nenhum comando, com nenhum argumento, pode deixar a
// configuracao invalida. Se pudesse, uma linha digitada errada tornaria o
// firmware incapaz de operar ate alguem regravar.
void test_nenhum_comando_deixa_a_configuracao_invalida(void) {
  const char* entradas[] = {
      "nome", "nome X", "nome ", "mac", "mac 00:00:00:00:00:00", "mac lixo",
      "pin", "pin 0", "pin 99999999999", "intervalo 0", "intervalo 1",
      "intervalo 99999", "timeout 0", "timeout 999999", "brilho 0",
      "brilho 100", "brilho 255", "unidades x", "padroes", "set", "voar",
      "", "   ", "?", "status",
  };
  for (const char* entrada : entradas) {
    KanriSettings s = default_settings();
    apply_command(p(entrada), s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(kanri::config::SettingsError::None),
        static_cast<int>(validate(s)), entrada);
  }
}

// Fuzz: qualquer sequencia de bytes entra sem travar e sem corromper nada.
void test_fuzz_de_linhas_aleatorias(void) {
  std::uint32_t semente = 0x5EED;
  char buffer[120];
  for (int it = 0; it < 3000; ++it) {
    semente = semente * 1103515245U + 12345U;
    const std::size_t len = (semente >> 16) % sizeof(buffer);
    for (std::size_t i = 0; i < len; ++i) {
      semente = semente * 1103515245U + 12345U;
      buffer[i] = static_cast<char>((semente >> 16) & 0xFF);
    }
    const ParsedCommand c = parse_command(buffer, len);
    TEST_ASSERT_NOT_NULL(kanri::config::to_string(c.action));
    TEST_ASSERT_NOT_NULL(kanri::config::to_string(c.error));

    KanriSettings s = default_settings();
    apply_command(c, s);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(kanri::config::SettingsError::None),
        static_cast<int>(validate(s)));
  }
}

void test_ajuda_lista_os_comandos(void) {
  const char* const* linhas = kanri::config::help_lines();
  TEST_ASSERT_NOT_NULL(linhas);
  int n = 0;
  for (; linhas[n] != nullptr; ++n) {
    TEST_ASSERT_TRUE(linhas[n][0] != '\0');
  }
  TEST_ASSERT_GREATER_THAN_INT(8, n);
}

// A REGRA COBRADA PELA MAQUINA: toda ACAO tem que aparecer na ajuda.
//
// Por acao, e nao por palavra: `help` e `ajuda` fazem a mesma coisa, e
// documentar as duas seria ruido. Mas uma acao cujas palavras nenhuma
// aparece na ajuda e uma acao que so quem escreveu conhece.
//
// Aconteceu com `teste`, `seg` e `auto`: foram adicionados, funcionavam, e
// o usuario nao tinha como descobrir que existiam. Agora quebra o CI.
void test_toda_acao_aparece_na_ajuda(void) {
  const kanri::config::CommandWord* palavras = kanri::config::command_words();
  TEST_ASSERT_NOT_NULL(palavras);

  for (std::size_t i = 0; palavras[i].word != nullptr; ++i) {
    // `None` nao e comando digitavel.
    if (palavras[i].action == CommandAction::None) continue;

    bool documentada = false;
    // Qualquer palavra desta acao servindo ja documenta a acao.
    for (std::size_t j = 0; palavras[j].word != nullptr && !documentada; ++j) {
      if (palavras[j].action != palavras[i].action) continue;
      for (const char* const* l = kanri::config::help_lines(); *l; ++l) {
        if (std::strstr(*l, palavras[j].word) != nullptr) {
          documentada = true;
          break;
        }
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(documentada, palavras[i].word);
  }
}

// E o contrario: toda palavra listada precisa mesmo ser aceita pelo parser.
// Ajuda que documenta comando inexistente e pior do que ajuda faltando.
void test_toda_palavra_listada_e_reconhecida(void) {
  const kanri::config::CommandWord* palavras = kanri::config::command_words();
  for (std::size_t i = 0; palavras[i].word != nullptr; ++i) {
    const ParsedCommand cmd =
        parse_command(palavras[i].word, std::strlen(palavras[i].word));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        static_cast<int>(CommandError::UnknownCommand),
        static_cast<int>(cmd.error), palavras[i].word);
    // A acao nao e conferida aqui: comandos que exigem argumento devolvem
    // MissingArgument sem preencher a acao, e isso e o comportamento certo.
  }
}

void test_to_string_cobre_tudo(void) {
  const CommandAction acoes[] = {
      CommandAction::None, CommandAction::Help, CommandAction::Status,
      CommandAction::Scan, CommandAction::Save, CommandAction::Load,
      CommandAction::Defaults, CommandAction::Restart, CommandAction::SetName,
      CommandAction::SetMac, CommandAction::SetPin, CommandAction::SetInterval,
      CommandAction::SetTimeout, CommandAction::SetBrightness,
      CommandAction::SetUnits, CommandAction::ReadDtc,
      CommandAction::SegTest, CommandAction::SegShow,
      CommandAction::SegAuto, CommandAction::PotStatus,
      CommandAction::GpioWrite,
  };
  for (const CommandAction a : acoes) {
    TEST_ASSERT_NOT_NULL(kanri::config::to_string(a));
  }
  const CommandError erros[] = {
      CommandError::None, CommandError::TooLong, CommandError::UnknownCommand,
      CommandError::MissingArgument, CommandError::ArgumentTooLong,
      CommandError::InvalidNumber, CommandError::InvalidValue,
  };
  for (const CommandError e : erros) {
    TEST_ASSERT_NOT_NULL(kanri::config::to_string(e));
  }
  TEST_ASSERT_EQUAL_STRING("Unknown",
                           kanri::config::to_string(static_cast<CommandAction>(99)));
  TEST_ASSERT_EQUAL_STRING("erro desconhecido",
                           kanri::config::to_string(static_cast<CommandError>(99)));
}

// Comandos do mostrador: existem para validar a fiacao sem o carro e sem
// multimetro, entao precisam ser reconhecidos exatamente como digitados.
void test_comandos_do_mostrador(void) {
  const ParsedCommand teste = parse_command("teste", 5);
  TEST_ASSERT_TRUE(teste.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandAction::SegTest),
                        static_cast<int>(teste.action));

  const char* linha = "seg 13.8";
  const ParsedCommand mostrar = parse_command(linha, std::strlen(linha));
  TEST_ASSERT_TRUE(mostrar.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandAction::SegShow),
                        static_cast<int>(mostrar.action));
  TEST_ASSERT_EQUAL_STRING("13.8", mostrar.text);
}

// `auto` solta o mostrador de volta para a telemetria. Precisa ser um
// comando proprio, e nao "seg auto", porque a decisao de quem manda na tela
// mora no main.cpp — a unica parte sem cobertura — e ja produziu tres bugs.
void test_comando_auto_solta_o_mostrador(void) {
  const ParsedCommand cmd = parse_command("auto", 4);
  TEST_ASSERT_TRUE(cmd.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandAction::SegAuto),
                        static_cast<int>(cmd.action));
}

// "seg" sem texto nao tem o que mostrar. Recusar aqui e melhor do que apagar
// o mostrador e deixar o operador achando que o display morreu.
void test_mostrar_exige_texto(void) {
  const ParsedCommand cmd = parse_command("seg", 3);
  TEST_ASSERT_FALSE(cmd.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandError::MissingArgument),
                        static_cast<int>(cmd.error));
}

// Nenhum dos dois mexe em configuracao: sao acoes, tratadas no main.cpp.
void test_comandos_do_mostrador_nao_alteram_configuracao(void) {
  KanriSettings s = kanri::config::default_settings();
  const KanriSettings antes = s;

  TEST_ASSERT_FALSE(apply_command(parse_command("teste", 5), s));
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(&antes, &s, sizeof(s)));
}

// `gpio <pino> <valor>` precisa de DOIS numeros, e a ordem importa: trocar
// pino por valor acionaria o pino errado sem nenhum aviso.
void test_gpio_le_dois_numeros_na_ordem(void) {
  const char* linha = "gpio 22 1";
  const ParsedCommand cmd = parse_command(linha, std::strlen(linha));
  TEST_ASSERT_TRUE(cmd.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandAction::GpioWrite),
                        static_cast<int>(cmd.action));
  TEST_ASSERT_EQUAL_UINT32(22, cmd.number);
  TEST_ASSERT_EQUAL_UINT32(1, cmd.number2);
}

void test_gpio_com_um_numero_so_e_recusado(void) {
  const char* linha = "gpio 22";
  const ParsedCommand cmd = parse_command(linha, std::strlen(linha));
  TEST_ASSERT_FALSE(cmd.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandError::MissingArgument),
                        static_cast<int>(cmd.error));
}

void test_gpio_com_texto_no_lugar_de_numero_e_recusado(void) {
  const char* a = "gpio 22 alto";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandError::InvalidNumber),
                        static_cast<int>(parse_command(a, std::strlen(a)).error));
  const char* b = "gpio xx 1";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandError::InvalidNumber),
                        static_cast<int>(parse_command(b, std::strlen(b)).error));
}

// Espacos extras entre os numeros nao podem quebrar: quem digita dentro do
// carro nao acerta espacamento.
void test_gpio_tolera_espacos_extras(void) {
  const char* linha = "gpio   22    0";
  const ParsedCommand cmd = parse_command(linha, std::strlen(linha));
  TEST_ASSERT_TRUE(cmd.ok());
  TEST_ASSERT_EQUAL_UINT32(22, cmd.number);
  TEST_ASSERT_EQUAL_UINT32(0, cmd.number2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_set_e_opcional);
  RUN_TEST(test_espacos_extras_sao_ignorados);
  RUN_TEST(test_terminadores_de_terminal_sao_ignorados);
  RUN_TEST(test_comando_ignora_caixa_mas_o_valor_nao);
  RUN_TEST(test_aceita_portugues_e_ingles);
  RUN_TEST(test_valor_pode_conter_espacos);

  RUN_TEST(test_linha_vazia_nao_e_erro);
  RUN_TEST(test_comando_desconhecido);
  RUN_TEST(test_falta_de_argumento);
  RUN_TEST(test_mac_aceita_valor_vazio_para_limpar);
  RUN_TEST(test_numero_invalido);
  RUN_TEST(test_numero_gigante_nao_estoura);
  RUN_TEST(test_linha_longa_demais);
  RUN_TEST(test_argumento_longo_demais);

  RUN_TEST(test_aplica_nome);
  RUN_TEST(test_aplica_mac_valido_e_recusa_invalido);
  RUN_TEST(test_aplica_numeros_dentro_da_faixa);
  RUN_TEST(test_recusa_numeros_fora_da_faixa);
  RUN_TEST(test_aplica_unidades);
  RUN_TEST(test_padroes_restaura_tudo);
  RUN_TEST(test_comandos_de_acao_nao_mexem_na_configuracao);
  RUN_TEST(test_pin_longo_demais_e_recusado);

  RUN_TEST(test_apply_recusa_nome_vazio_montado_a_mao);
  RUN_TEST(test_apply_ignora_acao_corrompida);
  RUN_TEST(test_nenhum_comando_deixa_a_configuracao_invalida);
  RUN_TEST(test_fuzz_de_linhas_aleatorias);
  RUN_TEST(test_ajuda_lista_os_comandos);
  RUN_TEST(test_toda_acao_aparece_na_ajuda);
  RUN_TEST(test_toda_palavra_listada_e_reconhecida);
  RUN_TEST(test_gpio_le_dois_numeros_na_ordem);
  RUN_TEST(test_gpio_com_um_numero_so_e_recusado);
  RUN_TEST(test_gpio_com_texto_no_lugar_de_numero_e_recusado);
  RUN_TEST(test_gpio_tolera_espacos_extras);
  RUN_TEST(test_to_string_cobre_tudo);
  RUN_TEST(test_comandos_do_mostrador);
  RUN_TEST(test_comando_auto_solta_o_mostrador);
  RUN_TEST(test_mostrar_exige_texto);
  RUN_TEST(test_comandos_do_mostrador_nao_alteram_configuracao);
  return UNITY_END();
}
