"""
PaltaCheck — Servidor prueba camara (sin dependencias de JSON)
==============================================================
Uso: python servidor_camara.py

Recibe fotos JPEG directas del ESP32-CAM y las guarda en fotos_test/
No necesita ArduinoJson en el Arduino.
"""

import os
import socket
from datetime import datetime
from flask import Flask, request, jsonify
from colorama import init, Fore

init(autoreset=True)
app = Flask(__name__)


def obtener_ip_local():
    """Detecta la IP local de esta máquina en la red WiFi."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "0.0.0.0"

CARPETA = "fotos_test"
os.makedirs(CARPETA, exist_ok=True)
contador = {"n": 0}


@app.route("/foto", methods=["POST"])
def recibir_foto():
    try:
        img_bytes = request.data          # imagen JPEG cruda
        num_foto  = request.headers.get("X-Foto-Num", "?")
        ip_esp32  = request.remote_addr
        tamano    = len(img_bytes)
        ahora     = datetime.now().strftime("%H:%M:%S")

        if tamano < 100:
            return jsonify({"error": "imagen muy pequeña"}), 400

        contador["n"] += 1
        nombre = f"{CARPETA}/foto_{contador['n']:03d}_{datetime.now().strftime('%H%M%S')}.jpg"

        with open(nombre, "wb") as f:
            f.write(img_bytes)

        print(f"\n{Fore.CYAN}{'─'*45}")
        print(f"{Fore.WHITE}[{ahora}] Foto #{num_foto} recibida")
        print(f"{Fore.YELLOW}  ESP32-CAM  : {ip_esp32}")
        print(f"{Fore.YELLOW}  Tamaño     : {tamano:,} bytes ({tamano/1024:.1f} KB)")
        print(f"{Fore.GREEN}  Guardada   : {nombre}")
        print(f"{Fore.CYAN}  Total fotos: {contador['n']}")

        return jsonify({"ok": True, "foto": contador["n"]}), 200

    except Exception as e:
        print(f"{Fore.RED}ERROR: {e}")
        return jsonify({"error": str(e)}), 500


@app.route("/", methods=["GET"])
def health():
    return jsonify({
        "servicio"       : "PaltaCheck camara test",
        "fotos_recibidas": contador["n"],
        "carpeta"        : CARPETA,
    }), 200


if __name__ == "__main__":
    mi_ip = obtener_ip_local()
    print(f"""
{Fore.GREEN}╔═══════════════════════════════════════════╗
{Fore.GREEN}║   PaltaCheck — Servidor prueba camara    ║
{Fore.GREEN}╚═══════════════════════════════════════════╝

{Fore.WHITE}  Tu IP local     : {Fore.YELLOW}{mi_ip}
{Fore.WHITE}  Puerto          : {Fore.YELLOW}8000
{Fore.WHITE}  Fotos en        : {Fore.YELLOW}{CARPETA}/
{Fore.WHITE}  Endpoint activo : {Fore.YELLOW}POST http://{mi_ip}:8000/foto

{Fore.CYAN}  En el .ino configurar:
{Fore.WHITE}    #define BACKEND_HOST  "{mi_ip}"
{Fore.WHITE}    #define BACKEND_PORT  8000

{Fore.GREEN}  Esperando fotos... (Ctrl+C para salir)
{Fore.WHITE}{'─'*45}
""")

    import logging
    logging.getLogger("werkzeug").setLevel(logging.ERROR)
    app.run(host="0.0.0.0", port=8000, debug=False)