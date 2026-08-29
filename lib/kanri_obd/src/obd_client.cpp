#include "kanri_obd/obd_client.h"

#include <cstring>

#include "kanri_obd/elm327_commands.h"
#include "kanri_obd/obd_pid.h"

namespace kanri::obd {
namespace {

/// Buffer de trabalho para uma resposta. Fica na pilha, com tamanho fixo:
/// nada de alocacao dinamica no caminho critico.
constexpr std::size_t kResponseBuffer = kMaxRawResponseBytes + 1;

void byte_to_hex(std::uint8_t value, char* out) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  out[0] = kDigits[(value >> 4) & 0x0F];
  out[1] = kDigits[value & 0x0F];
}

/// Procura um trecho numa resposta ja terminada em nulo, ignorando caixa e
/// espacos. O ELM327 pode responder "OK" ou "ok", com ou sem espacos.
bool contem_sem_caixa(const char* texto, const char* trecho) {
  if (trecho == nullptr || trecho[0] == '\0') return true;

  for (std::size_t i = 0; texto[i] != '\0'; ++i) {
    std::size_t j = 0;
    std::size_t k = i;
    // Avanca enquanto casar. Sair do laco com trecho[j] == '\0' significa
    // que o trecho inteiro foi encontrado a partir de `i`.
    while (trecho[j] != '\0' && texto[k] != '\0') {
      char c = texto[k];
      if (c == ' ' || c == '\r' || c == '\n') {  // separadores nao contam
        ++k;
        continue;
      }
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
      char t = trecho[j];
      if (t >= 'a' && t <= 'z') t = static_cast<char>(t - ('a' - 'A'));
      if (c != t) break;
      ++j;
      ++k;
    }
    if (trecho[j] == '\0') return true;
  }
  return false;  // varreu o texto inteiro sem achar
}

}  // namespace

ObdClient::ObdClient(ITransport& transport, const core::IClock& clock,
                     const ObdClientConfig& config)
    : transport_(transport), clock_(clock), config_(config) {}

bool ObdClient::write_command(const char* command) {
  if (command == nullptr) return false;
  const std::size_t len = std::strlen(command);
  if (len == 0 || len > 32) return false;

  // Monta comando + CR num buffer unico: duas escritas separadas dariam ao
  // adaptador a chance de processar um comando incompleto.
  char linha[34];
  std::memcpy(linha, command, len);
  linha[len] = kCommandTerminator;

  const std::size_t total = len + 1;
  const std::size_t escritos =
      transport_.write(reinterpret_cast<const std::uint8_t*>(linha), total);
  return escritos == total;
}

std::size_t ObdClient::read_until_prompt(char* out, std::size_t cap,
                                         std::uint32_t timeout_ms) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';

  const std::uint32_t inicio = clock_.now_ms();
  std::size_t n = 0;

  while (core::elapsed_ms(clock_.now_ms(), inicio) < timeout_ms) {
    if (transport_.available() == 0) continue;

    std::uint8_t pedaco[64];
    const std::size_t lidos = transport_.read(pedaco, sizeof(pedaco));
    for (std::size_t i = 0; i < lidos; ++i) {
      const char c = static_cast<char>(pedaco[i]);
      if (c == kPromptChar) {
        // O prompt marca o fim da resposta. Nao entra no buffer: ele nao e
        // dado, e o parser ja o descartaria de qualquer forma.
        out[n] = '\0';
        return n;
      }
      // Buffer cheio: paramos de guardar mas seguimos consumindo ate o
      // prompt, senao o lixo restante contaminaria a proxima leitura.
      if (n + 1 < cap) out[n++] = c;
    }
  }

  out[n] = '\0';
  return n;  // estourou o tempo: devolve o que deu para ler
}

std::size_t ObdClient::send_at(const char* command, char* out, std::size_t cap,
                               std::uint32_t timeout_ms) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';

  // SEGURANCA: a allowlist vale aqui tambem. Nao existe caminho neste cliente
  // que escreva no transporte sem passar por uma checagem.
  if (command == nullptr ||
      check_at_command(command, std::strlen(command)) !=
          RequestVerdict::Allowed) {
    ++rejected_;
    return 0;
  }
  if (!write_command(command)) return 0;

  return read_until_prompt(
      out, cap, timeout_ms == 0 ? config_.response_timeout_ms : timeout_ms);
}

bool ObdClient::initialize() {
  ready_ = false;
  if (!transport_.is_connected()) return false;

  char resposta[kResponseBuffer];

  for (std::size_t i = 0; i < kInitSequenceLength; ++i) {
    const ElmCommand& passo = kInitSequence[i];

    // ATZ reinicia o chip e demora bem mais que os demais.
    const std::uint32_t tempo =
        (i == 0) ? config_.init_timeout_ms : config_.response_timeout_ms;

    const std::size_t n = send_at(passo.command, resposta, sizeof(resposta), tempo);
    const bool respondeu = n > 0;
    const bool casou = respondeu && contem_sem_caixa(resposta, passo.expected);

    if (!casou && passo.required) return false;
  }

  ready_ = true;
  return true;
}

ParsedFrame ObdClient::read_pid(std::uint8_t mode, std::uint8_t pid) {
  ParsedFrame frame;

  // SEGURANCA PRIMEIRO: nada e escrito no transporte antes desta checagem.
  const RequestVerdict verdict = check_obd_request(mode, pid);
  if (verdict != RequestVerdict::Allowed) {
    ++rejected_;
    frame.status = ParseStatus::UnknownCommand;
    return frame;
  }
  if (!transport_.is_connected()) {
    frame.status = ParseStatus::UnableToConnect;
    return frame;
  }

  // "010C" — modo e PID em hexadecimal, sem separador.
  char comando[5];
  byte_to_hex(mode, &comando[0]);
  byte_to_hex(pid, &comando[2]);
  comando[4] = '\0';

  char resposta[kResponseBuffer];

  // Retentativa somente para falhas passageiras. Insistir num "NO DATA"
  // (a ECU nao tem esse PID) so gastaria banda do barramento.
  for (std::uint8_t tentativa = 0; tentativa <= config_.max_retries; ++tentativa) {
    if (!write_command(comando)) {
      frame.status = ParseStatus::UnableToConnect;
      return frame;
    }

    const std::size_t n =
        read_until_prompt(resposta, sizeof(resposta), config_.response_timeout_ms);

    frame = parse_response(resposta, n, mode, pid);

    if (frame.ok()) {
      // Ultima barreira: o tamanho tem de bater com o catalogo. Uma resposta
      // bem formada mas com tamanho errado e suspeita — pode ser de outra
      // ECU, ou de um pedido anterior.
      if (!has_expected_length(mode, pid, frame.length)) {
        ++rejected_;
        frame.status = ParseStatus::PayloadTooLong;
        return frame;
      }
      ++ok_;
      return frame;
    }

    ++rejected_;
    if (!is_transient(frame.status)) return frame;  // insistir nao ajudaria
  }

  return frame;  // acabaram as tentativas
}

bool ObdClient::read_adapter_voltage(float& out_volts) {
  char resposta[64];
  if (send_at("ATRV", resposta, sizeof(resposta)) == 0) return false;

  // Resposta tipica: "13.8V". Interpretamos manualmente para nao depender de
  // atof/sscanf, que arrastam bastante codigo para a flash.
  int inteiro = 0;
  int decimal = 0;
  int casas = 0;
  bool viu_digito = false;
  bool depois_do_ponto = false;

  for (std::size_t i = 0; resposta[i] != '\0'; ++i) {
    const char c = resposta[i];
    if (c >= '0' && c <= '9') {
      viu_digito = true;
      if (depois_do_ponto) {
        if (casas < 3) {
          decimal = decimal * 10 + (c - '0');
          ++casas;
        }
      } else {
        inteiro = inteiro * 10 + (c - '0');
        if (inteiro > 100) return false;  // fora de qualquer realidade veicular
      }
    } else if (c == '.' && viu_digito && !depois_do_ponto) {
      depois_do_ponto = true;
    } else if (viu_digito) {
      break;  // chegamos ao 'V' ou a outro separador
    }
  }
  if (!viu_digito) return false;

  float valor = static_cast<float>(inteiro);
  float divisor = 1.0F;
  for (int i = 0; i < casas; ++i) divisor *= 10.0F;
  valor += static_cast<float>(decimal) / divisor;

  // Faixa fisica: abaixo de 4 V nao ha ECU viva; acima de 30 V nao ha rede
  // veicular de 12 V. Fora disso, a leitura esta corrompida.
  if (valor < 4.0F || valor > 30.0F) return false;

  out_volts = valor;
  return true;
}

}  // namespace kanri::obd
