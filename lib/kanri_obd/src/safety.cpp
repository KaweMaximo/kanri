#include "kanri_obd/safety.h"

#include <cstring>

#include "kanri_obd/obd_pid.h"

namespace kanri::obd {
namespace {

/// Nenhum comando AT legitimo do ELM327 passa disso.
constexpr std::size_t kMaxAtCommandLen = 16;

bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

// ---------------------------------------------------------------------------
//  A ALLOWLIST de comandos AT (correspondencia exata).
//
//  Comandos AT configuram o ADAPTADOR, nao a ECU — mas alguns permitem montar
//  quadros arbitrarios no barramento, e por isso ficam de fora.
//
//  DELIBERADAMENTE AUSENTES, e por que:
//    ATSH xx yy zz -> define o header CAN. Com ele da para enderecar qualquer
//                     modulo e enviar qualquer servico, inclusive escrita.
//                     E a porta de entrada para tudo que este projeto proibe.
//    ATCRA / ATCF  -> filtros de recepcao; so fazem sentido junto com ATSH.
//    ATMA / ATMR   -> "monitor all": inunda o canal e nos faz perder respostas.
//    ATPP xx SV yy -> grava parametros permanentes no ELM327. Escrita, e pode
//                     deixar o adaptador inutilizavel.
//    ATBI          -> pula a inicializacao do barramento. Inseguro.
//    ATTP xx       -> forca protocolo sem verificacao; ATSP0 (auto) e melhor.
// ---------------------------------------------------------------------------
constexpr const char* kAllowedAtCommands[] = {
    "ATZ",    // reset do adaptador
    "ATD",    // volta aos padroes de fabrica
    "ATWS",   // warm start (reset mais leve que o ATZ)
    "ATE0", "ATE1",            // eco do comando desligado/ligado
    "ATL0", "ATL1",            // linefeed
    "ATS0", "ATS1",            // espacos na resposta
    "ATH0", "ATH1",            // headers na resposta
    "ATM0", "ATM1",            // memoria
    "ATAT0", "ATAT1", "ATAT2", // adaptive timing
    "ATCAF0", "ATCAF1",        // formatacao automatica de CAN
    "ATDP", "ATDPN",           // "que protocolo voce esta usando?"
    "ATRV",   // le a tensao no pino 16 do conector (nao envolve a ECU)
    "ATI",    // versao do adaptador
    "AT@1",   // descricao do dispositivo
};

constexpr std::size_t kAllowedAtCommandCount =
    sizeof(kAllowedAtCommands) / sizeof(kAllowedAtCommands[0]);

}  // namespace

RequestVerdict check_obd_request(std::uint8_t mode, std::uint8_t pid) {
  // Barreira 1: somente os modos de leitura. Esta e a linha que separa
  // "ler o carro" de "mexer no carro".
  if (!is_read_only_mode(mode)) {
    return RequestVerdict::ForbiddenMode;
  }
  // Barreira 2 (defesa em profundidade): o PID precisa estar no catalogo.
  // Mesmo dentro do modo 01, so pedimos o que declaramos explicitamente.
  if (find_pid(mode, pid) == nullptr) {
    return RequestVerdict::ForbiddenPid;
  }
  return RequestVerdict::Allowed;
}

RequestVerdict check_at_command(const char* cmd, std::size_t len) {
  if (cmd == nullptr || len == 0 || len > kMaxAtCommandLen) {
    return RequestVerdict::Malformed;
  }

  // Normaliza: sem espacos, sem CR/LF, tudo em maiusculas.
  char norm[kMaxAtCommandLen + 1];
  std::size_t n = 0;
  for (std::size_t i = 0; i < len; ++i) {
    const unsigned char uc = static_cast<unsigned char>(cmd[i]);
    if (uc == ' ' || uc == '\t' || uc == '\r' || uc == '\n') continue;
    // Byte nao imprimivel em um comando AT nunca e legitimo.
    if (uc < 0x20 || uc >= 0x7F) return RequestVerdict::Malformed;
    char c = static_cast<char>(uc);
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    norm[n++] = c;
  }
  norm[n] = '\0';

  // "ATZ" e o menor comando valido: 3 caracteres.
  if (n < 3) return RequestVerdict::Malformed;
  if (norm[0] != 'A' || norm[1] != 'T') return RequestVerdict::ForbiddenAtCommand;

  for (std::size_t i = 0; i < kAllowedAtCommandCount; ++i) {
    if (std::strcmp(norm, kAllowedAtCommands[i]) == 0) {
      return RequestVerdict::Allowed;
    }
  }

  // ATSPh / ATSPAh — selecao de protocolo. ATSP0 (automatico) e o que usamos.
  if (std::strncmp(norm, "ATSP", 4) == 0) {
    const std::size_t tail = n - 4;
    if (tail == 1 && (is_hex_digit(norm[4]) || norm[4] == 'A')) {
      return RequestVerdict::Allowed;
    }
    if (tail == 2 && norm[4] == 'A' && is_hex_digit(norm[5])) {
      return RequestVerdict::Allowed;
    }
    return RequestVerdict::ForbiddenAtCommand;
  }

  // ATSTxx — timeout de resposta, dois digitos hexadecimais.
  if (std::strncmp(norm, "ATST", 4) == 0) {
    if (n == 6 && is_hex_digit(norm[4]) && is_hex_digit(norm[5])) {
      return RequestVerdict::Allowed;
    }
    return RequestVerdict::ForbiddenAtCommand;
  }

  // Nao esta na allowlist: bloqueado. Este `return` e o que torna a politica
  // uma allowlist de verdade — o padrao e NEGAR.
  return RequestVerdict::ForbiddenAtCommand;
}

const char* to_string(RequestVerdict v) {
  switch (v) {
    case RequestVerdict::Allowed:            return "Allowed";
    case RequestVerdict::ForbiddenMode:      return "ForbiddenMode";
    case RequestVerdict::ForbiddenPid:       return "ForbiddenPid";
    case RequestVerdict::ForbiddenAtCommand: return "ForbiddenAtCommand";
    case RequestVerdict::Malformed:          return "Malformed";
  }
  return "Unknown";
}

}  // namespace kanri::obd
