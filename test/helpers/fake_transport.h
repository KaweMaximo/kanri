#pragma once
// ============================================================================
//  FakeTransport — um "adaptador ELM327" de mentira
// ============================================================================
//  O teste enfileira exatamente os bytes que quer que o firmware receba —
//  inclusive respostas malformadas e lixo binario, que sao dificeis de
//  reproduzir com um adaptador de verdade. Tambem guarda o que foi escrito,
//  para o teste poder afirmar "nada foi enviado ao barramento".
// ============================================================================

#include <cstring>
#include <string>

#include "fake_clock.h"
#include "kanri_obd/i_transport.h"

namespace kanri::test {

class FakeTransport final : public obd::ITransport {
 public:
  // --- Controle a partir do teste ---------------------------------------
  void queue(const char* text) { inbox_ += text; }
  void queue_bytes(const std::uint8_t* data, std::size_t len) {
    inbox_.append(reinterpret_cast<const char*>(data), len);
  }
  void fail_next_connect() { connect_ok_ = false; }
  void drop_link() { connected_ = false; }

  /// Liga este transporte a um relogio falso, para que ele ADIANTE o tempo a
  /// cada consulta a porta.
  ///
  /// Sem isso, um codigo que espera resposta consultando o relogio fica em
  /// laco infinito: o tempo nunca passa e o timeout nunca vence. Foi
  /// exatamente o que aconteceu quando os modos de DTC passaram a ser
  /// permitidos e o ObdClient comecou a de fato aguardar resposta deste
  /// duble. Um teste que TRAVA e pior que um que falha — ele nao diz onde
  /// esta o problema, e segura a suite inteira.
  void drive_clock(FakeClock& clock) { clock_ = &clock; }

  /// Tudo que o codigo sob teste escreveu. Vazio = nada foi ao barramento.
  const std::string& written() const { return outbox_; }
  void clear_written() { outbox_.clear(); }

  // --- ITransport --------------------------------------------------------
  bool connect() override {
    connected_ = connect_ok_;
    return connected_;
  }
  void disconnect() override { connected_ = false; }
  bool is_connected() const override { return connected_; }

  std::size_t write(const std::uint8_t* data, std::size_t len) override {
    if (!connected_) return 0;
    outbox_.append(reinterpret_cast<const char*>(data), len);
    return len;
  }

  std::size_t read(std::uint8_t* out, std::size_t max_len) override {
    if (!connected_) return 0;
    const std::size_t n = inbox_.size() < max_len ? inbox_.size() : max_len;
    std::memcpy(out, inbox_.data(), n);
    inbox_.erase(0, n);
    return n;
  }

  std::size_t available() const override {
    if (clock_ != nullptr) clock_->advance(1);
    return connected_ ? inbox_.size() : 0;
  }

 private:
  std::string inbox_;
  std::string outbox_;
  bool connected_ = false;
  bool connect_ok_ = true;
  FakeClock* clock_ = nullptr;
};

}  // namespace kanri::test
