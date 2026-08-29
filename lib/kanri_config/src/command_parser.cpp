#include "kanri_config/command_parser.h"

#include <cstring>

namespace kanri::config {
namespace {

bool espaco(char c) { return c == ' ' || c == '\t'; }

char minuscula(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

/// Compara ignorando caixa. Usado so para o NOME do comando: o VALOR e
/// preservado como digitado, porque nome Bluetooth diferencia maiusculas.
bool palavra_igual(const char* texto, std::size_t len, const char* alvo) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    if (alvo[i] == '\0') return false;
    if (minuscula(texto[i]) != minuscula(alvo[i])) return false;
  }
  return alvo[i] == '\0';
}

ParsedCommand erro(CommandError e) {
  ParsedCommand c;
  c.error = e;
  return c;
}

/// Converte texto em numero sem sinal. Recusa vazio, sinal, letras e estouro.
bool para_numero(const char* texto, std::size_t len, std::uint32_t& saida) {
  if (len == 0) return false;
  std::uint32_t valor = 0;
  for (std::size_t i = 0; i < len; ++i) {
    const char c = texto[i];
    if (c < '0' || c > '9') return false;
    const std::uint32_t digito = static_cast<std::uint32_t>(c - '0');
    // Checa ANTES de multiplicar: depois do estouro seria tarde.
    if (valor > (0xFFFFFFFFU - digito) / 10U) return false;
    valor = valor * 10U + digito;
  }
  saida = valor;
  return true;
}

struct Entrada {
  const char* palavra;
  CommandAction acao;
  bool precisa_argumento;
  bool argumento_numerico;
  /// Espera DOIS numeros separados por espaco, em `number` e `number2`.
  bool argumento_dois_numeros;
};

// Aceita tanto "set nome X" quanto "nome X": o `set` e opcional, porque
// digitar dentro do carro deve ser o mais curto possivel.
constexpr Entrada kTabela[] = {
    {"help",       CommandAction::Help,          false, false, false},
    {"ajuda",      CommandAction::Help,          false, false, false},
    {"?",          CommandAction::Help,          false, false, false},
    {"status",     CommandAction::Status,        false, false, false},
    {"scan",       CommandAction::Scan,          false, false, false},
    {"varrer",     CommandAction::Scan,          false, false, false},
    {"save",       CommandAction::Save,          false, false, false},
    {"salvar",     CommandAction::Save,          false, false, false},
    {"load",       CommandAction::Load,          false, false, false},
    {"carregar",   CommandAction::Load,          false, false, false},
    {"defaults",   CommandAction::Defaults,      false, false, false},
    {"padroes",    CommandAction::Defaults,      false, false, false},
    {"restart",    CommandAction::Restart,       false, false, false},
    {"reiniciar",  CommandAction::Restart,       false, false, false},
    {"dtc",        CommandAction::ReadDtc,       false, false, false},
    {"falhas",     CommandAction::ReadDtc,       false, false, false},
    {"nome",       CommandAction::SetName,       true,  false, false},
    {"name",       CommandAction::SetName,       true,  false, false},
    {"mac",        CommandAction::SetMac,        true,  false, false},
    {"pin",        CommandAction::SetPin,        true,  false, false},
    {"intervalo",  CommandAction::SetInterval,   true,  true, false},
    {"interval",   CommandAction::SetInterval,   true,  true, false},
    {"timeout",    CommandAction::SetTimeout,    true,  true, false},
    {"brilho",     CommandAction::SetBrightness, true,  true, false},
    {"unidades",   CommandAction::SetUnits,      true,  false, false},
    // Autoteste do mostrador: acende segmento por segmento para localizar
    // fio solto sem multimetro e sem o carro.
    {"teste",      CommandAction::SegTest,       false, false, false},
    {"selftest",   CommandAction::SegTest,       false, false, false},
    // Escreve direto no mostrador. Util para conferir a fonte e a ordem dos
    // digitos com um valor escolhido a mao.
    {"seg",        CommandAction::SegShow,       true,  false, false},
    {"mostrar",    CommandAction::SegShow,       true,  false, false},
    // Solta o mostrador: volta a mostrar a telemetria do carro.
    {"auto",       CommandAction::SegAuto,       false, false, false},
    // Leitura crua do ADC: e o unico jeito de conferir a fiacao do
    // potenciometro, ja que um pino solto tambem "le" alguma coisa.
    {"pot",        CommandAction::PotStatus,     false, false, false},
    // Acionar um pino livre da bancada, sem gravar firmware a cada teste.
    {"gpio",       CommandAction::GpioWrite,     true,  false, true},
    // Barra de LEDs: hoje serve para testar a fiacao, e e a base do
    // contagiro. Sem argumento, mostra a barra atual.
    {"leds",       CommandAction::LedBar,        false, false, false},
    {"piscar",     CommandAction::LedBlink,      false, false, false},
};

constexpr std::size_t kTabelaLen = sizeof(kTabela) / sizeof(kTabela[0]);

// A lista das palavras aceitas, terminada em nullptr.
//
// Existe para o TESTE poder cobrar que todo comando esteja documentado. Sem
// isso, um comando novo nasce invisivel: funciona, mas ninguem descobre que
// existe — foi o que aconteceu com `teste`, `seg` e `auto`, adicionados sem
// entrar na ajuda nem na dica do painel.
const CommandWord* command_words_impl() {
  static CommandWord palavras[kTabelaLen + 1] = {};
  for (std::size_t i = 0; i < kTabelaLen; ++i) {
    palavras[i].word = kTabela[i].palavra;
    palavras[i].action = kTabela[i].acao;
  }
  palavras[kTabelaLen].word = nullptr;
  return palavras;
}

void copiar(char* destino, std::size_t cap, const char* origem,
            std::size_t len) {
  std::size_t i = 0;
  for (; i < len && (i + 1) < cap; ++i) destino[i] = origem[i];
  for (std::size_t j = i; j < cap; ++j) destino[j] = '\0';
}

}  // namespace

ParsedCommand parse_command(const char* line, std::size_t len) {
  if (line == nullptr || len == 0) return ParsedCommand{};
  if (len > kMaxCommandLen) return erro(CommandError::TooLong);

  // Recorta a linha, ignorando espacos nas pontas e o CR/LF do terminal.
  std::size_t inicio = 0;
  while (inicio < len && (espaco(line[inicio]) || line[inicio] == '\r' ||
                          line[inicio] == '\n')) {
    ++inicio;
  }
  std::size_t fim = len;
  while (fim > inicio && (espaco(line[fim - 1]) || line[fim - 1] == '\r' ||
                          line[fim - 1] == '\n')) {
    --fim;
  }
  if (inicio >= fim) return ParsedCommand{};  // linha vazia nao e erro

  // Primeira palavra.
  std::size_t p1 = inicio;
  while (p1 < fim && !espaco(line[p1])) ++p1;
  const char* palavra = line + inicio;
  std::size_t palavra_len = p1 - inicio;

  // "set" e opcional: se veio, a palavra util e a proxima.
  if (palavra_igual(palavra, palavra_len, "set")) {
    std::size_t p2 = p1;
    while (p2 < fim && espaco(line[p2])) ++p2;
    std::size_t p3 = p2;
    while (p3 < fim && !espaco(line[p3])) ++p3;
    if (p2 >= fim) return erro(CommandError::UnknownCommand);
    palavra = line + p2;
    palavra_len = p3 - p2;
    p1 = p3;
  }

  // Resto da linha = argumento (pode conter espacos, como um nome composto).
  std::size_t arg_ini = p1;
  while (arg_ini < fim && espaco(line[arg_ini])) ++arg_ini;
  const std::size_t arg_len = (arg_ini < fim) ? (fim - arg_ini) : 0;

  for (std::size_t i = 0; i < kTabelaLen; ++i) {
    if (!palavra_igual(palavra, palavra_len, kTabela[i].palavra)) continue;

    ParsedCommand comando;
    comando.action = kTabela[i].acao;

    if (!kTabela[i].precisa_argumento) {
      // Argumento OPCIONAL: comandos como `leds` funcionam sozinhos (mostram
      // o estado) e tambem com lista (definem). Copiar quando existe custa
      // nada e evita uma terceira categoria na tabela.
      if (arg_len > 0 && arg_len < kMaxArgLen) {
        copiar(comando.text, sizeof(comando.text), line + arg_ini, arg_len);
      }
      return comando;
    }

    // `mac` aceita argumento vazio: e assim que se limpa o MAC fixado.
    if (arg_len == 0 && kTabela[i].acao != CommandAction::SetMac) {
      return erro(CommandError::MissingArgument);
    }
    if (arg_len >= kMaxArgLen) return erro(CommandError::ArgumentTooLong);

    if (kTabela[i].argumento_dois_numeros) {
      // "22 1" -> number=22, number2=1. Separados de proposito: sao coisas
      // diferentes, e um argumento so faria alguem inverter a ordem.
      std::size_t corte = arg_ini;
      while (corte < fim && !espaco(line[corte])) ++corte;
      if (corte >= fim) return erro(CommandError::MissingArgument);

      std::size_t seg = corte;
      while (seg < fim && espaco(line[seg])) ++seg;
      if (seg >= fim) return erro(CommandError::MissingArgument);

      if (!para_numero(line + arg_ini, corte - arg_ini, comando.number) ||
          !para_numero(line + seg, fim - seg, comando.number2)) {
        return erro(CommandError::InvalidNumber);
      }
      return comando;
    }

    if (kTabela[i].argumento_numerico) {
      if (!para_numero(line + arg_ini, arg_len, comando.number)) {
        return erro(CommandError::InvalidNumber);
      }
    } else {
      copiar(comando.text, sizeof(comando.text), line + arg_ini, arg_len);
    }
    return comando;
  }

  return erro(CommandError::UnknownCommand);
}

bool apply_command(const ParsedCommand& command, KanriSettings& settings) {
  if (!command.ok()) return false;

  switch (command.action) {
    case CommandAction::SetName: {
      KanriSettings copia = settings;
      copiar(copia.adapter_name, kAdapterNameLen, command.text,
             std::strlen(command.text));
      // Um nome vazio deixaria o firmware sem alvo. Recusamos em vez de
      // aceitar e so descobrir na proxima varredura.
      if (copia.adapter_name[0] == '\0' && copia.adapter_mac[0] == '\0') {
        return false;
      }
      settings = copia;
      return true;
    }
    case CommandAction::SetMac: {
      KanriSettings copia = settings;
      copiar(copia.adapter_mac, kAdapterMacLen, command.text,
             std::strlen(command.text));
      // MAC malformado seria descartado por clamp_to_valid mais tarde, em
      // silencio. Melhor recusar agora, enquanto ha alguem lendo a resposta.
      if (validate(copia) != SettingsError::None) return false;
      settings = copia;
      return true;
    }
    case CommandAction::SetPin: {
      if (std::strlen(command.text) >= kAdapterPinLen) return false;
      copiar(settings.adapter_pin, kAdapterPinLen, command.text,
             std::strlen(command.text));
      return true;
    }
    case CommandAction::SetInterval:
      if (command.number < kMinPollIntervalMs ||
          command.number > kMaxPollIntervalMs) {
        return false;
      }
      settings.poll_interval_ms = static_cast<std::uint16_t>(command.number);
      return true;

    case CommandAction::SetTimeout:
      if (command.number < kMinElmTimeoutMs ||
          command.number > kMaxElmTimeoutMs) {
        return false;
      }
      settings.elm_timeout_ms = static_cast<std::uint16_t>(command.number);
      return true;

    case CommandAction::SetBrightness:
      if (command.number > kMaxBrightness) return false;
      settings.display_brightness = static_cast<std::uint8_t>(command.number);
      return true;

    case CommandAction::SetUnits: {
      if (palavra_igual(command.text, std::strlen(command.text), "metrico") ||
          palavra_igual(command.text, std::strlen(command.text), "metric")) {
        settings.use_metric_units = 1;
        return true;
      }
      if (palavra_igual(command.text, std::strlen(command.text), "imperial")) {
        settings.use_metric_units = 0;
        return true;
      }
      return false;
    }

    case CommandAction::Defaults:
      settings = default_settings();
      return true;

    // Comandos que nao mexem na configuracao.
    case CommandAction::None:
    case CommandAction::Help:
    case CommandAction::Status:
    case CommandAction::Scan:
    case CommandAction::Save:
    case CommandAction::Load:
    case CommandAction::Restart:
    case CommandAction::SegTest:
    case CommandAction::SegShow:
    case CommandAction::SegAuto:
    case CommandAction::PotStatus:
    case CommandAction::GpioWrite:
    case CommandAction::LedBar:
    case CommandAction::LedBlink:
    case CommandAction::ReadDtc:
      return false;
  }
  return false;
}

const char* to_string(CommandAction action) {
  switch (action) {
    case CommandAction::None:          return "None";
    case CommandAction::Help:          return "Help";
    case CommandAction::Status:        return "Status";
    case CommandAction::Scan:          return "Scan";
    case CommandAction::Save:          return "Save";
    case CommandAction::Load:          return "Load";
    case CommandAction::Defaults:      return "Defaults";
    case CommandAction::Restart:       return "Restart";
    case CommandAction::ReadDtc:       return "ReadDtc";
    case CommandAction::SetName:       return "SetName";
    case CommandAction::SetMac:        return "SetMac";
    case CommandAction::SetPin:        return "SetPin";
    case CommandAction::SetInterval:   return "SetInterval";
    case CommandAction::SetTimeout:    return "SetTimeout";
    case CommandAction::SetBrightness: return "SetBrightness";
    case CommandAction::SetUnits:      return "SetUnits";
    case CommandAction::SegTest:       return "SegTest";
    case CommandAction::SegShow:       return "SegShow";
    case CommandAction::SegAuto:       return "SegAuto";
    case CommandAction::PotStatus:     return "PotStatus";
    case CommandAction::GpioWrite:     return "GpioWrite";
    case CommandAction::LedBar:        return "LedBar";
    case CommandAction::LedBlink:      return "LedBlink";
  }
  return "Unknown";
}

const char* to_string(CommandError error) {
  switch (error) {
    case CommandError::None:            return "ok";
    case CommandError::TooLong:         return "linha longa demais";
    case CommandError::UnknownCommand:  return "comando desconhecido";
    case CommandError::MissingArgument: return "falta o valor";
    case CommandError::ArgumentTooLong: return "valor longo demais";
    case CommandError::InvalidNumber:   return "esperava um numero";
    case CommandError::InvalidValue:    return "valor fora da faixa";
  }
  return "erro desconhecido";
}

const CommandWord* command_words() { return command_words_impl(); }

const char* const* help_lines() {
  static const char* const kAjuda[] = {
      "comandos (o 'set' e opcional):",
      "  ajuda              mostra esta lista",
      "  nome <texto>       nome Bluetooth do adaptador (ex.: OBDII)",
      "  mac <AA:BB:..>     fixa o MAC; sem valor, limpa",
      "  pin <numero>       PIN de pareamento (ex.: 1234)",
      "  intervalo <ms>     entre leituras de PID (50-5000)",
      "  timeout <ms>       espera por resposta (200-10000)",
      "  brilho <0-100>     brilho do display",
      "  unidades <metrico|imperial>",
      "  save               grava na flash",
      "  load               recarrega da flash",
      "  padroes            volta ao padrao de fabrica (nao grava)",
      "  status             mostra estado e configuracao",
      "  scan               forca uma nova varredura",
      "  reiniciar          reinicia o ESP32",
      "  dtc                le os codigos de falha da ECU",
      "mostrador de 7 segmentos:",
      "  teste              autoteste: acende segmento por segmento",
      "  seg <texto>        escreve e SEGURA a tela (ex.: seg 13.8, seg 8--)",
      "  auto               devolve a tela para a telemetria",
      "  pot                leitura crua do potenciometro de brilho",
      "  gpio <pino> <0|1>  aciona um pino livre (recusa os ocupados)",
      "barra de LEDs (base do contagiro):",
      "  leds 22,21,19      define a barra; sem argumento, mostra a atual",
      "  piscar             liga/desliga o piscar da barra",
      nullptr,
  };
  return kAjuda;
}

}  // namespace kanri::config
