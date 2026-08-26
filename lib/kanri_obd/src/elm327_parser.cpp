#include "kanri_obd/elm327_parser.h"

#include <cstring>

namespace kanri::obd {
namespace {

/// Sentinela devolvido por normalize() quando a linha nao cabe no buffer.
constexpr std::size_t kOverflow = static_cast<std::size_t>(-1);

bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

std::uint8_t hex_value(char c) {
  return static_cast<std::uint8_t>((c <= '9') ? (c - '0') : (c - 'A' + 10));
}

void byte_to_hex(std::uint8_t value, char* out) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  out[0] = kDigits[(value >> 4) & 0x0F];
  out[1] = kDigits[value & 0x0F];
}

/// Limpa uma linha crua: remove espacos, tabs, CR/LF e o prompt '>';
/// converte para maiusculas; e substitui QUALQUER byte nao imprimivel por '#'.
///
/// Por que '#' e nao simplesmente descartar o byte estranho? Porque '#' nao e
/// digito hexadecimal e nao aparece em nenhuma mensagem do ELM327. Assim,
/// ruido binario sobrevive a limpeza e provoca a rejeicao da linha em vez de
/// ser varrido para debaixo do tapete. Isso e "falhar fechado": na duvida,
/// rejeitar. Ao mesmo tempo, manter tudo imprimivel garante que o buffer
/// continue terminado em nulo e seguro para strcmp/strstr.
///
/// @return tamanho da linha limpa, ou kOverflow se estourou `cap`.
std::size_t normalize(const char* src, std::size_t len, char* out,
                      std::size_t cap) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < len; ++i) {
    const unsigned char uc = static_cast<unsigned char>(src[i]);
    if (uc == ' ' || uc == '\t' || uc == '\r' || uc == '\n' || uc == '>') {
      continue;
    }
    if (n >= cap) return kOverflow;

    char c = static_cast<char>(uc);
    if (uc < 0x20 || uc >= 0x7F) {
      c = '#';
    } else if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - ('a' - 'A'));
    }
    out[n++] = c;
  }
  out[n] = '\0';
  return n;
}

/// Reconhece as mensagens de texto do ELM327.
/// @return true se a linha e uma mensagem de status (e nao dado hexadecimal).
bool try_text_status(const char* line, ParseStatus& out) {
  struct Entry {
    const char* needle;
    ParseStatus status;
  };
  // A ORDEM IMPORTA: "UNABLE TO CONNECT" tem de ser testado antes do "ERROR"
  // generico, e as mensagens especificas antes das amplas.
  static constexpr Entry kEntries[] = {
      {"UNABLETOCONNECT", ParseStatus::UnableToConnect},
      {"NODATA", ParseStatus::NoData},
      {"BUFFERFULL", ParseStatus::BufferFull},
      {"STOPPED", ParseStatus::Stopped},
      {"SEARCHING", ParseStatus::Searching},
      {"ERROR", ParseStatus::BusError},  // cobre CAN ERROR, BUS ERROR, DATA ERROR
  };
  for (const Entry& entry : kEntries) {
    if (std::strstr(line, entry.needle) != nullptr) {
      out = entry.status;
      return true;
    }
  }
  if (std::strcmp(line, "?") == 0) {
    out = ParseStatus::UnknownCommand;
    return true;
  }
  return false;
}

/// Linhas informativas que nao sao erro nem dado: apenas ignore e siga.
bool is_ignorable(const char* line) {
  return std::strcmp(line, "OK") == 0 ||
         std::strncmp(line, "BUSINIT", 7) == 0 ||
         std::strncmp(line, "ELM327", 6) == 0;
}

/// Converte uma linha que se espera ser hexadecimal em um ParsedFrame.
/// Faz TODAS as checagens antes de escrever um unico byte em `data`.
ParsedFrame parse_hex_line(const char* line, std::size_t len,
                           std::uint8_t expected_mode,
                           std::uint8_t expected_pid) {
  ParsedFrame frame;

  for (std::size_t i = 0; i < len; ++i) {
    if (!is_hex_digit(line[i])) {
      frame.status = ParseStatus::InvalidCharacter;
      return frame;
    }
  }
  if ((len % 2) != 0) {
    frame.status = ParseStatus::OddHexDigits;
    return frame;
  }

  const std::size_t byte_count = len / 2;
  // Precisamos de no minimo o eco de modo + PID.
  if (byte_count < 2) {
    frame.status = ParseStatus::TooShort;
    return frame;
  }
  if ((byte_count - 2) > kMaxPayloadBytes) {
    frame.status = ParseStatus::PayloadTooLong;
    return frame;
  }

  const std::uint8_t mode =
      static_cast<std::uint8_t>((hex_value(line[0]) << 4) | hex_value(line[1]));
  const std::uint8_t pid =
      static_cast<std::uint8_t>((hex_value(line[2]) << 4) | hex_value(line[3]));

  // A ECU responde com o modo pedido + 0x40 (modo 01 -> 0x41, modo 09 -> 0x49).
  // Conferir isso descarta respostas atrasadas de um pedido ANTERIOR, que de
  // outra forma seriam exibidas como se fossem a medida atual.
  if (mode != static_cast<std::uint8_t>(expected_mode + 0x40)) {
    frame.status = ParseStatus::UnexpectedMode;
    return frame;
  }
  if (pid != expected_pid) {
    frame.status = ParseStatus::UnexpectedPid;
    return frame;
  }

  frame.mode = mode;
  frame.pid = pid;
  frame.length = static_cast<std::uint8_t>(byte_count - 2);
  for (std::size_t i = 0; i < frame.length; ++i) {
    const std::size_t offset = 4 + (i * 2);
    frame.data[i] = static_cast<std::uint8_t>((hex_value(line[offset]) << 4) |
                                              hex_value(line[offset + 1]));
  }
  frame.status = ParseStatus::Ok;
  return frame;
}

}  // namespace

ParsedFrame parse_response(const char* raw, std::size_t raw_len,
                           std::uint8_t expected_mode,
                           std::uint8_t expected_pid) {
  ParsedFrame result;
  result.status = ParseStatus::Empty;

  if (raw == nullptr || raw_len == 0) return result;
  if (raw_len > kMaxRawResponseBytes) {
    result.status = ParseStatus::RawTooLong;
    return result;
  }

  // Hexadecimal do comando que pedimos, para reconhecer o eco (ex.: "010C").
  char echo[5];
  byte_to_hex(expected_mode, &echo[0]);
  byte_to_hex(expected_pid, &echo[2]);
  echo[4] = '\0';

  ParseStatus pending_text = ParseStatus::Empty;  // ultimo status de texto visto
  ParseStatus last_hex_error = ParseStatus::Empty;  // ultima falha de hex

  std::size_t index = 0;
  while (index < raw_len) {
    // Recorta um segmento delimitado por CR e/ou LF.
    const std::size_t start = index;
    while (index < raw_len && raw[index] != '\r' && raw[index] != '\n') {
      ++index;
    }
    const std::size_t segment_len = index - start;
    while (index < raw_len && (raw[index] == '\r' || raw[index] == '\n')) {
      ++index;
    }
    if (segment_len == 0) continue;

    char line[kMaxLineBytes + 1];
    const std::size_t line_len =
        normalize(raw + start, segment_len, line, kMaxLineBytes);
    if (line_len == kOverflow) {
      pending_text = ParseStatus::BufferFull;
      continue;
    }
    if (line_len == 0) continue;
    if (std::strcmp(line, echo) == 0) continue;  // eco do proprio comando
    if (is_ignorable(line)) continue;

    ParseStatus text_status = ParseStatus::Empty;
    if (try_text_status(line, text_status)) {
      pending_text = text_status;
      continue;  // "SEARCHING..." costuma vir ANTES do dado bom
    }

    const ParsedFrame candidate =
        parse_hex_line(line, line_len, expected_mode, expected_pid);
    if (candidate.ok()) return candidate;  // primeira linha boa ganha
    last_hex_error = candidate.status;
  }

  // Nenhuma linha valida. Uma mensagem explicita do adaptador ("NO DATA") e
  // um diagnostico melhor do que "caractere invalido", entao ela tem prioridade.
  if (pending_text != ParseStatus::Empty) {
    result.status = pending_text;
  } else if (last_hex_error != ParseStatus::Empty) {
    result.status = last_hex_error;
  }
  return result;
}

bool is_transient(ParseStatus status) {
  switch (status) {
    // Vale repetir o mesmo pedido: o problema tende a passar.
    case ParseStatus::Searching:
    case ParseStatus::BufferFull:
    case ParseStatus::Empty:
    case ParseStatus::RawTooLong:
    case ParseStatus::InvalidCharacter:
    case ParseStatus::OddHexDigits:
    case ParseStatus::TooShort:
    case ParseStatus::PayloadTooLong:
    case ParseStatus::UnexpectedMode:
    case ParseStatus::UnexpectedPid:
      return true;

    // Nao vale repetir:
    //  Ok              -> deu certo, nao ha o que repetir.
    //  NoData          -> a ECU nao tem esse PID. Insistir e desperdicio.
    //  UnableToConnect -> falha de link; quem trata e a maquina de estados.
    //  BusError        -> problema eletrico; escala para a maquina de estados.
    //  Stopped         -> o adaptador abortou; precisa reinicializar.
    //  UnknownCommand  -> bug NOSSO no comando enviado. Repetir nao conserta.
    //  NotImplemented  -> falta codigo. Repetir nao conserta.
    case ParseStatus::Ok:
    case ParseStatus::NoData:
    case ParseStatus::UnableToConnect:
    case ParseStatus::BusError:
    case ParseStatus::Stopped:
    case ParseStatus::UnknownCommand:
    case ParseStatus::NotImplemented:
      return false;
  }
  return false;  // enum corrompido: nao insista
}

const char* to_string(ParseStatus status) {
  switch (status) {
    case ParseStatus::Ok:               return "Ok";
    case ParseStatus::Empty:            return "Empty";
    case ParseStatus::NoData:           return "NoData";
    case ParseStatus::Searching:        return "Searching";
    case ParseStatus::UnableToConnect:  return "UnableToConnect";
    case ParseStatus::BusError:         return "BusError";
    case ParseStatus::Stopped:          return "Stopped";
    case ParseStatus::BufferFull:       return "BufferFull";
    case ParseStatus::UnknownCommand:   return "UnknownCommand";
    case ParseStatus::RawTooLong:       return "RawTooLong";
    case ParseStatus::InvalidCharacter: return "InvalidCharacter";
    case ParseStatus::OddHexDigits:     return "OddHexDigits";
    case ParseStatus::TooShort:         return "TooShort";
    case ParseStatus::PayloadTooLong:   return "PayloadTooLong";
    case ParseStatus::UnexpectedMode:   return "UnexpectedMode";
    case ParseStatus::UnexpectedPid:    return "UnexpectedPid";
    case ParseStatus::NotImplemented:   return "NotImplemented";
  }
  return "Unknown";
}

}  // namespace kanri::obd
