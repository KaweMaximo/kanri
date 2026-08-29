#pragma once
// ============================================================================
//  FakeElm327 — um adaptador ELM327 de mentira, mas realista
// ============================================================================
//  O FakeTransport entrega bytes que o teste enfileirou. Isso basta para
//  testar o parser, mas nao para testar o ObdClient, que precisa de um
//  interlocutor que RESPONDA — e que responda com as manias de um adaptador
//  de verdade:
//
//    - demora alguns milissegundos antes de responder;
//    - termina toda resposta com o prompt '>';
//    - devolve "SEARCHING..." antes do primeiro dado;
//    - devolve "NO DATA" para PID que a ECU nao implementa;
//    - as vezes simplesmente nao responde;
//    - em clones ruins, mistura lixo no meio.
//
//  Este dublê simula tudo isso. E o que permite responder, na bancada e sem
//  carro, perguntas como "o que acontece se o adaptador ficar mudo no meio de
//  uma leitura?" — que com hardware real exigiria provocar a falha de
//  proposito, algo entre dificil e impossivel.
//
//  O TRUQUE DO TEMPO: o ObdClient espera a resposta consultando o IClock. Com
//  um relogio falso, o tempo nao passa sozinho e a espera nunca terminaria.
//  Por isso este dublê ADIANTA o relogio a cada consulta — o tempo corre
//  quando o codigo sob teste olha para ele, e um timeout de 1 segundo e
//  exercitado em microssegundos.
// ============================================================================

#include <cstring>
#include <map>
#include <string>

#include "fake_clock.h"
#include "kanri_obd/i_transport.h"

namespace kanri::test {

class FakeElm327 final : public obd::ITransport {
 public:
  explicit FakeElm327(FakeClock& clock) : clock_(clock) {}

  // --- Configuracao pelo teste -------------------------------------------

  /// Resposta para um par modo/PID. O prompt '>' e acrescentado sozinho.
  void on_pid(std::uint8_t mode, std::uint8_t pid, const char* resposta) {
    respostas_[chave(mode, pid)] = resposta;
  }

  /// Resposta para um comando AT. Padrao para os nao configurados: "OK".
  void on_at(const char* comando, const char* resposta) {
    at_[normalizar(comando)] = resposta;
  }

  /// Quanto o "adaptador" demora para comecar a responder.
  void set_latency_ms(std::uint32_t ms) { latencia_ = ms; }

  /// As proximas `n` requisicoes ficam sem resposta (simula adaptador mudo).
  void mute_next(int n) { mudo_ = n; }

  /// As proximas `n` escritas falham (simula canal caindo no meio do envio).
  void fail_write_next(int n) { falhar_escrita_ = n; }

  /// Liga o eco do comando, como faz um ELM327 com ATE1.
  void set_echo(bool on) { eco_ = on; }

  /// Prefixa a proxima resposta com "SEARCHING...", como na primeira leitura.
  void searching_once() { searching_ = true; }

  /// Acrescenta lixo binario DEPOIS da resposta (clone com mau contato).
  /// A resposta boa continua la — e o parser deve ignorar o resto.
  void garbage_once() { lixo_ = true; }

  /// CORROMPE as proximas `n` respostas por inteiro: o dado nao chega, so
  /// ruido. E assim que se testa a retentativa de verdade.
  void corrupt_next(int n) { corromper_ = n; }

  /// Tudo que o codigo sob teste enviou. Vazio = nada foi ao barramento.
  const std::string& written() const { return enviado_; }
  void clear_written() { enviado_.clear(); }

  /// Quantos comandos completos (terminados em CR) foram recebidos.
  int command_count() const { return comandos_; }

  // --- ITransport ---------------------------------------------------------
  bool connect() override {
    conectado_ = true;
    return true;
  }
  void disconnect() override { conectado_ = false; }
  bool is_connected() const override { return conectado_; }

  std::size_t write(const std::uint8_t* data, std::size_t len) override {
    if (!conectado_) return 0;
    if (falhar_escrita_ > 0) {
      --falhar_escrita_;
      return 0;  // o transporte aceitou zero bytes
    }
    for (std::size_t i = 0; i < len; ++i) {
      const char c = static_cast<char>(data[i]);
      enviado_ += c;
      if (c == '\r') {
        ++comandos_;
        responder(atual_);
        atual_.clear();
      } else {
        atual_ += c;
      }
    }
    return len;
  }

  std::size_t read(std::uint8_t* out, std::size_t max_len) override {
    if (!conectado_ || out == nullptr) return 0;
    if (!pronta()) return 0;
    const std::size_t n = saida_.size() < max_len ? saida_.size() : max_len;
    std::memcpy(out, saida_.data(), n);
    saida_.erase(0, n);
    return n;
  }

  std::size_t available() const override {
    // O relogio anda quando o codigo sob teste consulta a porta. E isso que
    // faz um timeout de 1 s ser exercitado sem esperar 1 s de verdade.
    clock_.advance(1);
    if (!conectado_ || !pronta()) return 0;
    return saida_.size();
  }

 private:
  static std::uint16_t chave(std::uint8_t mode, std::uint8_t pid) {
    return static_cast<std::uint16_t>((mode << 8) | pid);
  }

  static std::string normalizar(const char* s) {
    std::string r;
    for (std::size_t i = 0; s[i] != '\0'; ++i) {
      char c = s[i];
      if (c == ' ' || c == '\r' || c == '\n') continue;
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
      r += c;
    }
    return r;
  }

  bool pronta() const { return clock_.now_ms() >= disponivel_em_; }

  void responder(const std::string& comando) {
    const std::string cmd = normalizar(comando.c_str());
    if (cmd.empty()) return;

    if (mudo_ > 0) {
      --mudo_;
      saida_.clear();
      disponivel_em_ = clock_.now_ms() + 1000000;  // nunca, na pratica
      return;
    }

    if (corromper_ > 0) {
      --corromper_;
      saida_ = std::string("\x01\xFF\x9A\x7F\r\r>");
      disponivel_em_ = clock_.now_ms() + latencia_;
      return;
    }

    std::string corpo;
    if (eco_) corpo += comando + "\r";
    if (searching_) {
      corpo += "SEARCHING...\r";
      searching_ = false;
    }

    if (cmd.rfind("AT", 0) == 0) {
      const auto it = at_.find(cmd);
      corpo += (it != at_.end()) ? it->second : std::string("OK");
    } else if (cmd.size() >= 4) {
      const std::uint8_t mode = hex2(cmd[0], cmd[1]);
      const std::uint8_t pid = hex2(cmd[2], cmd[3]);
      const auto it = respostas_.find(chave(mode, pid));
      corpo += (it != respostas_.end()) ? it->second : std::string("NO DATA");
    } else {
      corpo += "?";
    }

    if (lixo_) {
      corpo += "\r\x01\xFF\x9A";  // ruido de contato ruim
      lixo_ = false;
    }
    corpo += "\r\r>";

    saida_ = corpo;
    disponivel_em_ = clock_.now_ms() + latencia_;
  }

  static std::uint8_t hex2(char a, char b) {
    auto v = [](char c) -> std::uint8_t {
      if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
      if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
      return 0;
    };
    return static_cast<std::uint8_t>((v(a) << 4) | v(b));
  }

  FakeClock& clock_;
  bool conectado_ = false;
  bool eco_ = false;
  bool searching_ = false;
  bool lixo_ = false;
  int mudo_ = 0;
  int falhar_escrita_ = 0;
  int corromper_ = 0;
  int comandos_ = 0;
  std::uint32_t latencia_ = 5;
  std::uint32_t disponivel_em_ = 0;
  std::string enviado_;
  std::string atual_;
  std::string saida_;
  std::map<std::uint16_t, std::string> respostas_;
  std::map<std::string, std::string> at_;
};

}  // namespace kanri::test
