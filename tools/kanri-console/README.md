# Kanri Console

Painel web local para acompanhar o ESP32 enquanto você desenvolve.

```bash
python3 tools/kanri-console/server.py
# abre em http://127.0.0.1:8765
```

## O que ele mostra

| Painel | Conteúdo |
|---|---|
| **Conexão** | Porta serial detectada, se está aberta, e qual chip respondeu |
| **Estado do firmware** | O estado atual da máquina de estados, lido dos logs em tempo real (`Boot`, `Degraded`, `Polling`…) |
| **Retentativas** | Número da tentativa e intervalo do backoff |
| **Log ao vivo** | Saída serial com carimbo de tempo, colorida por tipo de linha |

## O que ele faz

| Botão | Comando por baixo |
|---|---|
| **Gravar firmware** | `pio run -e esp32dev -t upload` |
| **Reiniciar** | Pulso DTR/RTS na serial (mesmo reset do botão EN) |
| **Compilar** | `pio run -e esp32dev` |
| **Rodar testes** | `pio test -e native` |
| **Detectar chip** | `esptool chip_id` |

A saída de cada comando aparece no mesmo log, ao vivo.

## O detalhe que faz funcionar: a porta é exclusiva

Só **um** processo pode abrir `/dev/ttyUSB0` por vez. Se o painel ficasse
segurando a porta, `pio run -t upload` falharia com *"could not open port"*.

Então, ao gravar, o painel:

1. fecha a serial e avisa no log;
2. roda o `pio`, transmitindo a saída;
3. reabre a serial e volta a mostrar o log do firmware.

É a mesma razão pela qual você não consegue ter o monitor serial do VS Code
aberto e gravar ao mesmo tempo.

## Segurança

O servidor escuta **apenas em `127.0.0.1`** — não na rede.

Isso é deliberado: o painel **executa comandos** na sua máquina (gravar
firmware, compilar). Se escutasse em `0.0.0.0`, qualquer um na mesma rede Wi-Fi
poderia gravar firmware no seu ESP32 ou disparar builds. Não há autenticação,
justamente porque o acesso é restrito à sua própria máquina.

**Não mude o bind para `0.0.0.0`** sem colocar autenticação antes.

## Por que o log filtra "display" por padrão

O `SerialDisplay` do firmware redesenha a moldura da tela a cada 500 ms — são
sete linhas ASCII por ciclo. Sem filtro, elas afogam as linhas que importam
(`[estado]`, `[retry]`).

O servidor classifica cada linha e o painel deixa a categoria `display`
desligada por padrão. Ligue no chip `display` se quiser ver a tela como ela
aparece no monitor serial. Linhas idênticas consecutivas também são agrupadas
com um contador `×N`.

## Backoff na reconexão da serial

A porta serial é exclusiva de um processo. Se outro processo a abrir (um
segundo painel, ou `pio device monitor`), o `pyserial` falha com
*"device reports readiness to read but returned no data"*.

O painel **não** reage a isso reconectando em laço apertado — isso gerava
dezenas de mensagens por segundo e escondia o log útil. Ele recua
progressivamente (0,25 s → 2 s) e não repete a mesma mensagem em sequência.

É a mesma lição do `RetryPolicy` do firmware, e vale pelo mesmo motivo:
insistir sem pausa transforma um problema em ruído.

## Requisitos

- Python 3.9+
- `pyserial` — já vem com o PlatformIO. Se rodar fora do venv:
  `pip install pyserial` (ver [CONTRIBUTING](../../CONTRIBUTING.md#instalando-o-platformio))

Sem dependência de rede: a interface não carrega nada de CDN.

## Opções

```bash
python3 tools/kanri-console/server.py --port 9000        # outra porta HTTP
python3 tools/kanri-console/server.py --serial /dev/ttyUSB1
python3 tools/kanri-console/server.py --baud 115200
python3 tools/kanri-console/server.py --no-browser       # não abrir o navegador
```
