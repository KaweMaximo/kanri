#pragma once
// ============================================================================
//  kanri_core/button.h — Um botao que nao mente
// ============================================================================
//  Contato mecanico nao fecha limpo: ele TREME. Ao pressionar, o sinal pula
//  entre aberto e fechado dezenas de vezes em poucos milissegundos, antes de
//  assentar. Lido cru, um toque vira cinco.
//
//  Num aparelho que troca a grandeza exibida a cada toque, isso significa
//  passar de temperatura direto para tensao sem o motorista entender por que.
//
//  O tratamento e simples: so aceitar uma mudanca de estado depois que ela se
//  manteve estavel por um tempo minimo. O que nao e simples e testar isso no
//  hardware — reproduzir tremulacao de proposito e dificil. Como funcao pura
//  do tempo, cada padrao vira um teste de microssegundos.
//
//  Alem do clique, reconhece o toque LONGO: com um botao so, ele e o unico
//  jeito de ter uma segunda acao sem outro fio.
// ============================================================================

#include <cstdint>

namespace kanri::core {

/// O que o botao acabou de fazer.
enum class ButtonEvent : std::uint8_t {
  None = 0,   ///< Nada mudou neste instante.
  Click,      ///< Toque curto, ja solto e confirmado.
  LongPress,  ///< Passou do tempo de toque longo, ainda pressionado.
  Released,   ///< Soltou depois de um toque longo (o Click ja nao vem).
};

class Button {
 public:
  /// @param debounce_ms    quanto tempo o sinal precisa ficar estavel.
  ///                       30 ms cobre a tremulacao de um botao tatil comum
  ///                       sem que o toque pareca lento.
  /// @param long_press_ms  a partir de quando o toque conta como longo.
  constexpr Button(std::uint16_t debounce_ms = 30,
                   std::uint16_t long_press_ms = 800)
      : debounce_ms_(debounce_ms), long_press_ms_(long_press_ms) {}

  /// Alimenta a leitura CRUA do pino e devolve o evento, se houver.
  ///
  /// Chame a cada volta do loop. `pressed_raw` e o nivel logico ja
  /// normalizado (true = pressionado), independente de o botao ser ativo em
  /// nivel alto ou baixo — isso e assunto do adaptador de hardware.
  ButtonEvent update(bool pressed_raw, std::uint32_t now_ms);

  /// O botao esta pressionado, do ponto de vista ja estabilizado?
  bool is_pressed() const { return estavel_; }

 private:
  std::uint16_t debounce_ms_;
  std::uint16_t long_press_ms_;

  bool estavel_ = false;        ///< Estado ja confirmado.
  bool ultimo_bruto_ = false;   ///< Ultima leitura crua vista.
  std::uint32_t mudou_em_ = 0;  ///< Quando a leitura crua mudou.
  std::uint32_t apertou_em_ = 0;///< Quando o toque confirmado comecou.
  bool longo_disparado_ = false;///< LongPress ja avisado neste toque.
  bool iniciado_ = false;       ///< Primeira chamada ja aconteceu.
};

}  // namespace kanri::core
