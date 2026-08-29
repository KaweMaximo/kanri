#pragma once
// ============================================================================
//  kanri_display/brightness_knob.h — O potenciometro de brilho do painel
// ============================================================================
//  Um botao giratorio ligado no ADC do ESP32. Parece trivial e nao e, por
//  causa de tres coisas que so aparecem montado:
//
//  1. O ADC DO ESP32 E RUIDOSO. Parado, a leitura oscila dezenas de contagens.
//     Mapeando direto para o brilho, o mostrador PULSA sozinho com o botao
//     imovel — e a leitura natural disso e "o display esta com defeito".
//
//     Por isso 5 niveis, e nao 16: cada faixa tem ~800 contagens, muito acima
//     do ruido. Foi decisao do Kawe, e e a defesa mais barata que existe.
//
//  2. QUANTIZAR NAO BASTA, NA FRONTEIRA. Exatamente no limite entre duas
//     faixas, o ruido ainda faz o nivel oscilar. Dai a HISTERESE: para mudar
//     de faixa, a leitura precisa ultrapassar a fronteira com margem.
//
//  3. GPIO SEM POTENCIOMETRO FLUTUA. O GPIO 34 nao tem pull-up interno — se o
//     fio nao estiver la, o pino vira antena e le lixo. Sem tratamento, o
//     brilho passearia sozinho num aparelho sem o botao instalado.
//
//     Dai a CONFIRMACAO: o nivel novo so vale depois de N leituras seguidas
//     concordarem. Um pino flutuando quase nunca concorda com ele mesmo; um
//     potenciometro de verdade concorda sempre.
//
//  Tudo aqui e logica pura. Da para testar o ruido do ADC sem ter o ADC.
// ============================================================================

#include <cstdint>

namespace kanri::display {

/// Quantos niveis de brilho o botao oferece.
///
/// Oito. Com 4096 contagens, cada faixa fica com 512 — ainda MUITO acima do
/// ruido do ADC, entao o motivo de nao usar 16 continua valendo, com o dobro
/// de resolucao para o motorista.
constexpr std::uint8_t kKnobLevels = 8;

/// Fim de escala do ADC do ESP32 em 12 bits.
constexpr std::uint16_t kAdcMax = 4095;

/// Margem de histerese, em contagens do ADC.
///
/// Com faixas de ~819 contagens, 120 e cerca de 15% da faixa: bem acima do
/// ruido tipico do ADC e bem abaixo do giro que uma pessoa faz de proposito.
constexpr std::uint16_t kKnobHysteresis = 120;

/// Quanto duas leituras podem diferir e ainda serem "a mesma posicao".
///
/// E o discriminador que separa potenciometro de pino solto. Um potenciometro
/// e uma fonte de baixa impedancia: parado, oscila umas +-40 contagens. Um
/// GPIO 34 sem nada ligado varre a escala inteira entre amostras.
///
/// 90 fica bem acima do ruido real (medido em +-1 na bancada do Kanri, e
/// dimensionado para +-40 no pior caso) e bem abaixo dos saltos de um pino
/// solto. Acompanha a largura da faixa: ~18% dela, como era com 5 niveis.
constexpr std::uint16_t kKnobMaxJitter = 90;

/// Quantas leituras seguidas precisam concordar para o nivel mudar.
///
/// Cada leitura tem de sugerir o mesmo nivel E estar a menos de
/// kKnobMaxJitter da PRIMEIRA delas — ancorada, nao acumulando desvio, senao
/// um passeio aleatorio lento passaria.
///
/// O numero saiu de conta, nao de chute. Com o valor original (3), o teste de
/// pino solto falhava em minutos de operacao simulada.
///
/// E ATENCAO ao que NAO e a causa da demora: o atraso do botao nunca veio
/// daqui, e sim do intervalo entre leituras. Ler o ADC custa ~100 us, entao
/// amostrar a cada 20 ms em vez de 200 ms deixou a resposta em ~160 ms —
/// imperceptivel — e ainda permitiu SUBIR as confirmacoes de 6 para 8.
///
/// Mais rapido e mais seguro ao mesmo tempo: o intervalo era um custo que
/// nao comprava nada.
constexpr std::uint8_t kKnobConfirmations = 8;

/// O nivel em que o mostrador nasce, antes de qualquer leitura valida.
///
/// Comeca no meio de proposito: se o botao nao estiver ligado, o painel fica
/// legivel do mesmo jeito. Nascer no minimo pareceria display queimado.
constexpr std::uint8_t kKnobDefaultLevel = 2;

/// Converte um nivel no percentual de brilho.
///
/// Os passos NAO sao lineares. A percepcao de brilho e aproximadamente
/// logaritmica: 20/40/60/80/100 desperdicaria tres passos na faixa clara,
/// justamente onde a diferenca menos aparece, e nao daria nenhum ajuste util
/// para dirigir a noite.
std::uint8_t knob_level_percent(std::uint8_t level);

/// O botao giratorio, com filtro de ruido.
class BrightnessKnob {
 public:
  /// Processa uma leitura do ADC.
  ///
  /// @param raw  leitura crua (0..kAdcMax); valores acima sao limitados.
  /// @return true SOMENTE quando o nivel mudou de verdade.
  ///
  /// O retorno importa: escrever no MAX7219 a cada leitura brigaria com o
  /// comando `brilho` do console, que seria desfeito 5 vezes por segundo.
  /// Escrevendo so na mudanca, o comando vale ate alguem girar o botao — que
  /// e como um controle fisico deve se comportar.
  bool update(std::uint16_t raw);

  std::uint8_t level() const { return nivel_; }
  std::uint8_t percent() const { return knob_level_percent(nivel_); }

  /// O nivel que a ultima leitura sugeriu, antes das confirmacoes. Existe
  /// para o diagnostico pelo console mostrar o que o botao esta "tentando".
  std::uint8_t pending_level() const { return candidato_; }

 private:
  std::uint8_t nivel_ = kKnobDefaultLevel;
  std::uint8_t candidato_ = kKnobDefaultLevel;
  std::uint8_t confirmacoes_ = 0;
  /// A leitura que abriu a candidatura. As seguintes sao comparadas com ELA,
  /// e nao com a anterior: comparar com a anterior deixaria passar um passeio
  /// aleatorio, que avanca pouco por vez e chega longe.
  std::uint16_t ancora_ = 0;
};

/// O nivel que uma leitura sugere, respeitando a histerese.
///
/// Separada da classe para poder ser testada sozinha: e a conta onde um erro
/// de sinal faz o botao girar ao contrario.
std::uint8_t knob_level_for(std::uint16_t raw, std::uint8_t current);

}  // namespace kanri::display
