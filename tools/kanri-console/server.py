#!/usr/bin/env python3
"""Kanri Console — painel web local para acompanhar o ESP32.

Serve uma interface no navegador que mostra o log serial ao vivo, o estado da
maquina de estados do firmware, e botoes para gravar/reiniciar/compilar.

Escuta APENAS em 127.0.0.1. Ver a nota de seguranca no README.md desta pasta.

Uso:
    python3 tools/kanri-console/server.py
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

try:
    import serial  # pyserial
except ImportError:
    sys.exit(
        "ERRO: pyserial nao encontrado.\n"
        "  Ele vem com o PlatformIO. Rode com o Python do venv, por exemplo:\n"
        "    ~/.local/share/platformio-venv/bin/python tools/kanri-console/server.py\n"
        "  Ou instale: pip install pyserial"
    )

RAIZ = Path(__file__).resolve().parents[2]
UI = Path(__file__).parent / "ui.html"

# Linhas que o firmware emite e que o painel entende. Manter em sincronia com
# os Serial.printf de src/main.cpp.
RE_ESTADO = re.compile(r"\[estado\]\s+(\S+)\s+--\((\S+)\)-->\s+(\S+)")
RE_RETRY = re.compile(r"\[retry\]\s+tentativa\s+(\d+),\s+proxima em\s+(\d+)\s*ms")
RE_VERSAO = re.compile(r"Kanri v(\S+)")
# "[obd] 0C = 992.0 rpm" — uma leitura decodificada saindo do firmware.
RE_MEDIDA = re.compile(r"\[obd\]\s+([0-9A-Fa-f]{2})\s+=\s+(-?[\d.]+)\s*(\S*)")
# "[hb] Polling ok=123 rej=4 up=87s" — a batida periodica do firmware.
RE_HB = re.compile(r"\[hb\]\s+(\S+)\s+ok=(\d+)\s+rej=(\d+)\s+up=(\d+)s")

# Nome de cada PID. Espelha o catalogo de lib/kanri_obd/include/kanri_obd/obd_pid.h
# — se um PID novo entrar no rodizio do firmware, acrescente aqui tambem.
NOMES_PID = {
    "0C": "Rotação",
    "05": "Temp. do motor",
    "0D": "Velocidade",
    "42": "Tensão",
    "11": "Borboleta",
    "04": "Carga do motor",
    "0B": "Pressão adm.",
    "0F": "Temp. do ar",
    "10": "Fluxo de ar",
    "2F": "Combustível",
    "46": "Temp. ambiente",
}

# Ordem em que aparecem no painel. Rotacao e temperatura primeiro: sao as duas
# que o motorista olha.
ORDEM_PID = ["0C", "05", "42", "11", "0D"]

# Quantos pontos guardar por medida. A ~1 leitura/s por PID, 180 da tres
# minutos de historico — o bastante para ver uma tendencia (a temperatura
# subindo, por exemplo) sem consumir memoria a toa.
MAX_PONTOS = 180

MAX_HISTORICO = 500  # linhas guardadas para quem abrir o painel depois

# Moldura da tela desenhada pelo SerialDisplay. Sao dezenas de linhas por
# segundo e afogam o log — por isso viram uma categoria propria, que o painel
# deixa desligada por padrao.
RE_MOLDURA = re.compile(r"^\s*(\+-+\+|\|.*\|)\s*$")


def classificar(texto: str) -> str:
    """Rotula uma linha de log para que a interface possa filtrar.

    A categoria e derivada aqui, no servidor, e nao no navegador: assim o
    filtro funciona igual para quem abre o painel no meio da sessao e recebe
    o historico ja rotulado.
    """
    if RE_MOLDURA.match(texto):
        return "display"
    if "[estado]" in texto:
        return "estado"
    if "[retry]" in texto:
        return "retry"
    if "[wdt]" in texto or "[config]" in texto:
        return "sistema"
    return "serial"


def achar_porta() -> str | None:
    """Descobre a porta serial do ESP32.

    Prefere /dev/serial/by-id, que e estavel entre reconexoes; cai para
    ttyUSB*/ttyACM* se nao houver.
    """
    for caminho in sorted(glob.glob("/dev/serial/by-id/*")):
        return os.path.realpath(caminho)
    for padrao in ("/dev/ttyUSB*", "/dev/ttyACM*"):
        achados = sorted(glob.glob(padrao))
        if achados:
            return achados[0]
    return None


def achar_pio() -> str | None:
    """Localiza o executavel do PlatformIO."""
    for candidato in (
        shutil.which("pio"),
        shutil.which("platformio"),
        str(Path.home() / ".local/share/platformio-venv/bin/pio"),
        str(Path.home() / ".platformio/penv/bin/pio"),
    ):
        if candidato and Path(candidato).exists():
            return candidato
    return None


class Hub:
    """Distribui eventos para os navegadores conectados (via SSE).

    Guarda um historico curto para que abrir o painel no meio de uma sessao
    ja mostre o que aconteceu, em vez de uma tela em branco.
    """

    def __init__(self) -> None:
        self._assinantes: list[queue.Queue] = []
        self._historico: list[dict] = []
        self._trava = threading.Lock()
        # Estado derivado dos logs, servido em /api/status.
        self.estado_fw = "—"
        self.ultimo_evento = "—"
        self.tentativa = 0
        self.proximo_retry_ms = 0
        self.versao_fw = "—"
        # Telemetria: valor atual e serie historica de cada PID.
        self.medidas: dict[str, dict] = {}
        self.series: dict[str, list] = {}
        self.frames_ok = 0
        self.rejeitados = 0
        self.uptime_s = 0
        self.ultimo_hb = 0.0

    def assinar(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=1000)
        with self._trava:
            for item in self._historico:
                try:
                    q.put_nowait(item)
                except queue.Full:
                    break
            self._assinantes.append(q)
        return q

    def cancelar(self, q: queue.Queue) -> None:
        with self._trava:
            if q in self._assinantes:
                self._assinantes.remove(q)

    def publicar(self, tipo: str, texto: str) -> None:
        cat = classificar(texto) if tipo == "serial" else tipo
        evento = {"tipo": tipo, "cat": cat, "texto": texto, "t": time.time()}
        self._interpretar(tipo, texto)
        with self._trava:
            self._historico.append(evento)
            if len(self._historico) > MAX_HISTORICO:
                del self._historico[0]
            mortos = []
            for q in self._assinantes:
                try:
                    q.put_nowait(evento)
                except queue.Full:
                    mortos.append(q)
            for q in mortos:
                self._assinantes.remove(q)

    def limpar(self) -> None:
        with self._trava:
            self._historico.clear()

    def telemetria(self) -> dict:
        """Retrato atual das medidas, com historico para as sparklines."""
        with self._trava:
            # A ordem vem de ORDEM_PID; o que nao estiver la vai depois, para
            # que um PID novo apareca sem precisar mexer no painel.
            conhecidos = [p for p in ORDEM_PID if p in self.medidas]
            extras = sorted(p for p in self.medidas if p not in ORDEM_PID)
            saida = []
            for pid in conhecidos + extras:
                m = dict(self.medidas[pid])
                m["serie"] = list(self.series.get(pid, []))
                saida.append(m)
            return {
                "agora": time.time(),
                "estado": self.estado_fw,
                "frames_ok": self.frames_ok,
                "rejeitados": self.rejeitados,
                "uptime_s": self.uptime_s,
                "ultimo_hb": self.ultimo_hb,
                "medidas": saida,
            }

    def _interpretar(self, tipo: str, texto: str) -> None:
        """Extrai o estado do firmware das linhas de log."""
        if tipo != "serial":
            return
        m = RE_HB.search(texto)
        if m:
            # A batida tambem carrega o estado: e o que permite ao painel
            # saber onde o firmware esta mesmo entrando no meio da sessao.
            self.estado_fw = m.group(1)
            self.frames_ok = int(m.group(2))
            self.rejeitados = int(m.group(3))
            self.uptime_s = int(m.group(4))
            self.ultimo_hb = time.time()
            return
        m = RE_ESTADO.search(texto)
        if m:
            self.ultimo_evento = m.group(2)
            self.estado_fw = m.group(3)
            return
        m = RE_RETRY.search(texto)
        if m:
            self.tentativa = int(m.group(1))
            self.proximo_retry_ms = int(m.group(2))
            return
        m = RE_VERSAO.search(texto)
        if m:
            self.versao_fw = m.group(1)
            return
        m = RE_MEDIDA.search(texto)
        if m:
            pid = m.group(1).upper()
            try:
                valor = float(m.group(2))
            except ValueError:
                return
            agora = time.time()
            self.medidas[pid] = {
                "pid": pid,
                "nome": NOMES_PID.get(pid, f"PID {pid}"),
                "valor": valor,
                "unidade": m.group(3),
                "t": agora,
            }
            serie = self.series.setdefault(pid, [])
            serie.append([agora, valor])
            if len(serie) > MAX_PONTOS:
                del serie[: len(serie) - MAX_PONTOS]


class LeitorSerial(threading.Thread):
    """Le a serial continuamente e publica cada linha no Hub.

    Sabe se soltar da porta: durante uma gravacao, o `pio` precisa dela com
    exclusividade. Ver `pausar()` / `retomar()`.
    """

    def __init__(self, hub: Hub, porta: str | None, baud: int) -> None:
        super().__init__(daemon=True)
        self.hub = hub
        self.porta = porta
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.pausado = threading.Event()
        self.conectado = False
        self._parar = threading.Event()
        # Backoff de reconexao. Sem ele, um erro persistente vira laco
        # apertado: fecha, reabre, falha, repete — dezenas de vezes por
        # segundo, e o log util some no meio.
        #
        # O caso real que provocou isso: DOIS processos com a mesma porta
        # aberta (o painel e um `pio device monitor`, por exemplo). A porta
        # serial e exclusiva, e o pyserial reclama com
        # "device reports readiness to read but returned no data".
        # A disputa e legitima e vai acontecer; o que nao pode e o painel
        # reagir a ela com um laco de reconexao.
        #
        # E a mesma licao do RetryPolicy do firmware — ver
        # lib/kanri_core/include/kanri_core/retry_policy.h.
        self._espera = 0.0
        self._ultimo_erro = ""
        self._avisou_aberta = False

    def pausar(self) -> None:
        """Solta a porta para que outro processo possa usa-la."""
        self.pausado.set()
        deadline = time.time() + 3
        while self.ser is not None and time.time() < deadline:
            time.sleep(0.05)

    def retomar(self) -> None:
        self.pausado.clear()

    def _erro(self, msg: str) -> None:
        """Publica um erro, sem repetir a mesma mensagem em sequencia."""
        if msg != self._ultimo_erro:
            self.hub.publicar("erro", msg)
            self._ultimo_erro = msg

    def enviar_linha(self, texto: str) -> bool:
        """Escreve uma linha no serial, como se alguem tivesse digitado.

        E assim que o painel conversa com o console de configuracao do
        firmware (ver lib/kanri_config/command_parser.h).
        """
        if self.ser is None:
            return False
        try:
            self.ser.write((texto + "\r\n").encode("utf-8", errors="replace"))
            self.ser.flush()
            return True
        except Exception as exc:
            self._erro(f"falha ao enviar: {exc}")
            return False

    def resetar_placa(self) -> bool:
        """Pulso DTR/RTS — o mesmo reset do botao EN da placa."""
        if self.ser is None:
            return False
        try:
            self.ser.setDTR(False)
            self.ser.setRTS(True)
            time.sleep(0.15)
            self.ser.setRTS(False)
            # A placa leva um instante para voltar. Ler nesse meio tempo
            # produz o mesmo erro de "readiness to read but returned no data".
            time.sleep(0.2)
            return True
        except Exception as exc:  # pragma: no cover - depende do hardware
            self.hub.publicar("erro", f"falha ao resetar: {exc}")
            return False

    def run(self) -> None:
        while not self._parar.is_set():
            if self.pausado.is_set():
                self._fechar()
                time.sleep(0.2)
                continue

            if self.ser is None:
                if self._espera > 0:
                    time.sleep(self._espera)
                porta = self.porta or achar_porta()
                if porta is None:
                    if self.conectado:
                        self.conectado = False
                        self._erro("porta serial desapareceu")
                    self._recuar()
                    continue
                try:
                    self.ser = serial.Serial(porta, self.baud, timeout=0.5)
                    self.conectado = True
                    # Anunciar a abertura so uma vez por sessao estavel: numa
                    # disputa de porta, o par abre/falha se repete, e avisar
                    # toda vez enche o log tanto quanto o erro.
                    if not self._avisou_aberta:
                        self.hub.publicar(
                            "info", f"serial aberta em {porta} @ {self.baud}")
                        self._avisou_aberta = True
                except Exception as exc:
                    self.conectado = False
                    self._erro(
                        f"nao consegui abrir {porta}: {exc}\n"
                        "  (outro processo pode estar com a porta: um segundo "
                        "painel, ou `pio device monitor`)")
                    self._recuar()
                    continue

            try:
                linha = self.ser.readline()
            except Exception as exc:
                self._erro(f"leitura falhou: {exc}")
                self._fechar()
                self._recuar()
                continue

            # So zeramos o backoff depois de uma LEITURA que deu certo.
            # Zerar ao abrir seria inutil: abrir a porta quase sempre
            # funciona mesmo quando outro processo ja a tem — o que falha
            # e a leitura. Com o reset no lugar errado, o backoff nunca
            # chegava a crescer.
            self._espera = 0.0
            self._ultimo_erro = ""

            if linha:
                texto = linha.decode("utf-8", errors="replace").rstrip("\r\n")
                if texto:
                    self.hub.publicar("serial", texto)

    def _recuar(self) -> None:
        """Dobra a espera ate 2 s. Mesma ideia do backoff do firmware."""
        self._espera = 0.25 if self._espera == 0 else min(self._espera * 2, 2.0)

    def _fechar(self) -> None:
        self._avisou_aberta = False
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.conectado = False


def rodar_comando(hub: Hub, leitor: LeitorSerial, argv: list[str],
                  rotulo: str, precisa_da_serial: bool) -> None:
    """Executa um comando externo transmitindo a saida linha a linha.

    Se `precisa_da_serial`, solta a porta antes e reabre depois — sem isso o
    upload falha com "could not open port".
    """
    if precisa_da_serial:
        hub.publicar("info", "soltando a porta serial para o comando...")
        leitor.pausar()
    try:
        hub.publicar("cmd", f"$ {' '.join(argv)}")
        proc = subprocess.Popen(
            argv, cwd=str(RAIZ), stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        assert proc.stdout is not None
        for linha in proc.stdout:
            hub.publicar("cmd", linha.rstrip("\r\n"))
        codigo = proc.wait()
        if codigo == 0:
            hub.publicar("ok", f"{rotulo}: concluido com sucesso")
        else:
            hub.publicar("erro", f"{rotulo}: falhou (codigo {codigo})")
    except FileNotFoundError:
        hub.publicar("erro", f"{rotulo}: executavel nao encontrado ({argv[0]})")
    except Exception as exc:
        hub.publicar("erro", f"{rotulo}: {exc}")
    finally:
        if precisa_da_serial:
            leitor.retomar()
            hub.publicar("info", "porta serial devolvida ao painel")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    hub: Hub
    leitor: LeitorSerial
    baud: int

    def log_message(self, fmt, *args):  # silencia o log de acesso
        pass

    # -- helpers ------------------------------------------------------------
    def _json(self, payload: dict, status: int = 200) -> None:
        corpo = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)

    def _chunk(self, texto: str) -> None:
        dados = texto.encode("utf-8")
        self.wfile.write(f"{len(dados):X}\r\n".encode() + dados + b"\r\n")
        self.wfile.flush()

    # -- rotas --------------------------------------------------------------
    def do_GET(self) -> None:
        if self.path in ("/", "/index.html"):
            corpo = UI.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corpo)))
            self.end_headers()
            self.wfile.write(corpo)
        elif self.path.startswith("/vendor/"):
            self._vendor()
        elif self.path == "/api/status":
            porta = self.leitor.porta or achar_porta()
            self._json({
                "porta": porta,
                "porta_existe": bool(porta and Path(porta).exists()),
                "serial_aberta": self.leitor.conectado,
                "pausado": self.leitor.pausado.is_set(),
                "baud": self.baud,
                "estado_fw": self.hub.estado_fw,
                "ultimo_evento": self.hub.ultimo_evento,
                "tentativa": self.hub.tentativa,
                "proximo_retry_ms": self.hub.proximo_retry_ms,
                "versao_fw": self.hub.versao_fw,
                "frames_ok": self.hub.frames_ok,
                "rejeitados": self.hub.rejeitados,
                "uptime_s": self.hub.uptime_s,
                "ultimo_hb": self.hub.ultimo_hb,
                "pio": achar_pio(),
                "projeto": str(RAIZ),
            })
        elif self.path == "/api/telemetry":
            self._json(self.hub.telemetria())
        elif self.path == "/api/stream":
            self._stream()
        else:
            self._json({"erro": "rota desconhecida"}, 404)

    def do_POST(self) -> None:
        pio = achar_pio()
        acoes = {
            "/api/flash": (
                [pio or "pio", "run", "-e", "esp32dev", "-t", "upload"],
                "gravacao", True),
            "/api/build": (
                [pio or "pio", "run", "-e", "esp32dev"], "compilacao", False),
            "/api/test": (
                [pio or "pio", "test", "-e", "native"], "testes", False),
        }
        if self.path in acoes:
            argv, rotulo, precisa = acoes[self.path]
            if pio is None:
                self._json({"ok": False, "erro": "PlatformIO nao encontrado"}, 400)
                return
            threading.Thread(
                target=rodar_comando,
                args=(self.hub, self.leitor, argv, rotulo, precisa),
                daemon=True,
            ).start()
            self._json({"ok": True, "iniciado": rotulo})
        elif self.path == "/api/reset":
            ok = self.leitor.resetar_placa()
            if ok:
                self.hub.publicar("info", "reset enviado (pulso DTR/RTS)")
            else:
                self.hub.publicar("erro", "reset falhou: serial nao esta aberta")
            self._json({"ok": ok})
        elif self.path == "/api/chip":
            esptool = list(RAIZ.glob(".pio/**/esptool.py"))
            script = str(Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py")
            porta = self.leitor.porta or achar_porta() or ""
            threading.Thread(
                target=rodar_comando,
                args=(self.hub, self.leitor,
                      [sys.executable, script, "--port", porta, "chip_id"],
                      "deteccao do chip", True),
                daemon=True,
            ).start()
            self._json({"ok": True})
        elif self.path == "/api/send":
            tamanho = int(self.headers.get("Content-Length", 0) or 0)
            # Limite explicito: o console do firmware recusa acima de 96
            # caracteres, e nao ha motivo para aceitar mais aqui.
            if tamanho > 512:
                self._json({"ok": False, "erro": "linha longa demais"}, 400)
                return
            corpo = self.rfile.read(tamanho).decode("utf-8", errors="replace")
            try:
                texto = json.loads(corpo).get("linha", "")
            except Exception:
                texto = ""
            texto = texto.strip()
            if not texto:
                self._json({"ok": False, "erro": "linha vazia"}, 400)
                return
            ok = self.leitor.enviar_linha(texto)
            if ok:
                self.hub.publicar("cmd", f"> {texto}")
            else:
                self.hub.publicar("erro", "serial fechada: nao enviei")
            self._json({"ok": ok})
        elif self.path == "/api/clear":
            self.hub.limpar()
            self._json({"ok": True})
        else:
            self._json({"erro": "rota desconhecida"}, 404)

    def _vendor(self) -> None:
        """Serve os arquivos de terceiros que acompanham o painel.

        Eles sao versionados junto (vendorizados) em vez de vir de um CDN
        porque esta ferramenta precisa funcionar OFFLINE — na garagem, ou no
        carro, que e justamente onde o ESP32 vai estar.
        """
        nome = self.path.rsplit("/", 1)[-1]
        # Trava de path traversal: so nome simples, nada de ".." ou barras.
        if not nome or "/" in nome or ".." in nome or nome.startswith("."):
            self._json({"erro": "caminho invalido"}, 400)
            return
        arquivo = Path(__file__).parent / "vendor" / nome
        if not arquivo.is_file():
            self._json({"erro": "nao encontrado"}, 404)
            return
        tipos = {".js": "application/javascript", ".css": "text/css",
                 ".woff2": "font/woff2", ".svg": "image/svg+xml"}
        corpo = arquivo.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type",
                         tipos.get(arquivo.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Cache-Control", "public, max-age=86400")
        self.end_headers()
        self.wfile.write(corpo)

    def _stream(self) -> None:
        """Server-Sent Events: uma conexao aberta por navegador.

        Usamos SSE em vez de WebSocket porque o fluxo e so de mao unica
        (servidor -> navegador) e o SSE cabe na biblioteca padrao do Python,
        sem dependencia extra.
        """
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        q = self.hub.assinar()
        try:
            self._chunk(": conectado\n\n")
            ultimo_ping = time.time()
            while True:
                try:
                    evento = q.get(timeout=1.0)
                    self._chunk(f"data: {json.dumps(evento)}\n\n")
                except queue.Empty:
                    # Comentario periodico: mantem a conexao viva atraves de
                    # proxies e detecta navegador fechado.
                    if time.time() - ultimo_ping > 15:
                        self._chunk(": ping\n\n")
                        ultimo_ping = time.time()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.hub.cancelar(q)


def main() -> None:
    p = argparse.ArgumentParser(description="Painel local do Kanri")
    p.add_argument("--port", type=int, default=8765, help="porta HTTP")
    p.add_argument("--serial", default=None, help="porta serial (auto se omitido)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--no-browser", action="store_true")
    args = p.parse_args()

    hub = Hub()
    leitor = LeitorSerial(hub, args.serial, args.baud)
    leitor.start()

    Handler.hub = hub
    Handler.leitor = leitor
    Handler.baud = args.baud

    # 127.0.0.1 de proposito: este painel EXECUTA COMANDOS (gravar firmware,
    # compilar). Expor na rede sem autenticacao deixaria qualquer um no mesmo
    # Wi-Fi gravar firmware no seu ESP32. Ver README.md desta pasta.
    servidor = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}"

    print(f"Kanri Console  ->  {url}")
    print(f"  projeto : {RAIZ}")
    print(f"  serial  : {args.serial or achar_porta() or 'nao detectada'}")
    print(f"  pio     : {achar_pio() or 'NAO ENCONTRADO'}")
    print("  (Ctrl+C para sair)")

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nencerrando...")
        servidor.shutdown()


if __name__ == "__main__":
    main()
