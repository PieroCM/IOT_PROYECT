"""Visor de fotos PaltaCheck.

Recorre `backend/capturas/lote_<id>/palta_<id>_<timestamp>.jpg` y muestra:
  - Lista de lotes a la izquierda
  - Grid de miniaturas del lote seleccionado
  - Click en miniatura → ventana ampliada con metadatos

Requisitos: Python 3.10+, Pillow.
    pip install Pillow
Ejecutar desde la raíz del repo:
    python scripts/ver_fotos.py
"""
from __future__ import annotations

import re
import sys
import tkinter as tk
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from tkinter import ttk, messagebox

try:
    from PIL import Image, ImageTk
except ImportError:
    print("Falta Pillow. Instalalo con:  pip install Pillow")
    sys.exit(1)


CAPTURAS_DIR = Path(__file__).resolve().parent.parent / "backend" / "capturas"
THUMB_SIZE   = (220, 165)
GRID_COLS    = 4

PATRON_NOMBRE = re.compile(r"palta_(?P<palta>\w+)_(?P<stamp>\d{8}_\d{6})\.jpg", re.IGNORECASE)


@dataclass
class Foto:
    ruta: Path
    palta_id: str
    timestamp: datetime | None

    @property
    def tamano_kb(self) -> float:
        return self.ruta.stat().st_size / 1024

    @property
    def hora_legible(self) -> str:
        return self.timestamp.strftime("%Y-%m-%d %H:%M:%S") if self.timestamp else "—"


def parsear_foto(p: Path) -> Foto:
    m = PATRON_NOMBRE.match(p.name)
    if not m:
        return Foto(ruta=p, palta_id="?", timestamp=None)
    try:
        ts = datetime.strptime(m["stamp"], "%Y%m%d_%H%M%S")
    except ValueError:
        ts = None
    return Foto(ruta=p, palta_id=m["palta"], timestamp=ts)


def listar_lotes(base: Path) -> list[Path]:
    if not base.exists():
        return []
    return sorted(
        (d for d in base.iterdir() if d.is_dir() and d.name.startswith("lote_")),
        key=lambda d: int(d.name.split("_", 1)[1]) if d.name.split("_", 1)[1].isdigit() else 0,
        reverse=True,
    )


def listar_fotos(carpeta: Path) -> list[Foto]:
    return sorted(
        (parsear_foto(p) for p in carpeta.glob("*.jpg")),
        key=lambda f: f.timestamp or datetime.min,
    )


class VisorFotos(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("PaltaCheck — Visor de capturas")
        self.geometry("1100x700")
        self.minsize(800, 500)

        self._thumb_refs: list[ImageTk.PhotoImage] = []  # evita GC

        self._construir_layout()
        self._cargar_lotes()

    # ── UI ───────────────────────────────────────────────────────────────
    def _construir_layout(self) -> None:
        contenedor = ttk.PanedWindow(self, orient="horizontal")
        contenedor.pack(fill="both", expand=True)

        # Lista de lotes
        izq = ttk.Frame(contenedor, padding=8)
        ttk.Label(izq, text="Lotes", font=("Segoe UI", 11, "bold")).pack(anchor="w")
        self.lista_lotes = tk.Listbox(izq, exportselection=False, activestyle="dotbox")
        self.lista_lotes.pack(fill="both", expand=True, pady=(6, 0))
        self.lista_lotes.bind("<<ListboxSelect>>", self._on_lote_seleccionado)

        ttk.Button(izq, text="↻ Recargar", command=self._cargar_lotes).pack(fill="x", pady=(6, 0))
        contenedor.add(izq, weight=1)

        # Grid de miniaturas con scroll
        der = ttk.Frame(contenedor)
        contenedor.add(der, weight=4)

        self.label_titulo = ttk.Label(der, text="Selecciona un lote", font=("Segoe UI", 12, "bold"), padding=(10, 6))
        self.label_titulo.pack(anchor="w")

        canvas_wrap = ttk.Frame(der)
        canvas_wrap.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(canvas_wrap, highlightthickness=0)
        scroll = ttk.Scrollbar(canvas_wrap, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)

        self.grid_frame = ttk.Frame(self.canvas)
        self.canvas.create_window((0, 0), window=self.grid_frame, anchor="nw")
        self.grid_frame.bind("<Configure>",
                             lambda _e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind_all("<MouseWheel>", lambda e: self.canvas.yview_scroll(int(-e.delta / 120), "units"))

        self.status = ttk.Label(self, anchor="w", padding=(8, 4))
        self.status.pack(fill="x", side="bottom")

    # ── Datos ────────────────────────────────────────────────────────────
    def _cargar_lotes(self) -> None:
        self.lista_lotes.delete(0, "end")
        self.lotes = listar_lotes(CAPTURAS_DIR)
        if not self.lotes:
            self.status.config(text=f"Sin capturas en {CAPTURAS_DIR}")
            self._limpiar_grid()
            self.label_titulo.config(text="Sin lotes")
            return
        for d in self.lotes:
            n_fotos = sum(1 for _ in d.glob("*.jpg"))
            self.lista_lotes.insert("end", f"{d.name}  ·  {n_fotos} fotos")
        self.lista_lotes.selection_set(0)
        self._mostrar_lote(self.lotes[0])

    def _on_lote_seleccionado(self, _evt) -> None:
        sel = self.lista_lotes.curselection()
        if not sel:
            return
        self._mostrar_lote(self.lotes[sel[0]])

    def _mostrar_lote(self, carpeta: Path) -> None:
        self._limpiar_grid()
        fotos = listar_fotos(carpeta)
        if not fotos:
            self.label_titulo.config(text=f"{carpeta.name}  —  sin fotos")
            self.status.config(text="")
            return

        total_kb = sum(f.tamano_kb for f in fotos)
        self.label_titulo.config(text=f"{carpeta.name}  —  {len(fotos)} fotos  ·  {total_kb:.0f} KB en disco")

        for i, foto in enumerate(fotos):
            self._agregar_miniatura(foto, fila=i // GRID_COLS, col=i % GRID_COLS)

        self.status.config(text=str(carpeta.resolve()))

    def _limpiar_grid(self) -> None:
        for child in self.grid_frame.winfo_children():
            child.destroy()
        self._thumb_refs.clear()

    def _agregar_miniatura(self, foto: Foto, *, fila: int, col: int) -> None:
        try:
            with Image.open(foto.ruta) as img:
                img.thumbnail(THUMB_SIZE)
                tk_img = ImageTk.PhotoImage(img)
        except Exception as e:
            tk_img = None
            tooltip = f"(error: {e})"
        else:
            tooltip = ""

        self._thumb_refs.append(tk_img)

        card = ttk.Frame(self.grid_frame, padding=6, relief="solid", borderwidth=1)
        card.grid(row=fila, column=col, padx=6, pady=6, sticky="n")

        if tk_img is not None:
            btn = ttk.Button(card, image=tk_img, command=lambda f=foto: self._abrir_grande(f))
            btn.pack()
        else:
            ttk.Label(card, text=f"⚠ {tooltip}").pack()

        ttk.Label(card, text=f"palta_id: {foto.palta_id}", font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(4, 0))
        ttk.Label(card, text=foto.hora_legible, foreground="#666").pack(anchor="w")
        ttk.Label(card, text=f"{foto.tamano_kb:.1f} KB", foreground="#666").pack(anchor="w")

    # ── Ventana grande ───────────────────────────────────────────────────
    def _abrir_grande(self, foto: Foto) -> None:
        try:
            img = Image.open(foto.ruta)
        except Exception as e:
            messagebox.showerror("Error", f"No se pudo abrir: {e}")
            return

        win = tk.Toplevel(self)
        win.title(foto.ruta.name)

        ancho_max, alto_max = self.winfo_screenwidth() - 100, self.winfo_screenheight() - 150
        copia = img.copy()
        copia.thumbnail((ancho_max, alto_max))
        tk_img = ImageTk.PhotoImage(copia)

        tk.Label(win, image=tk_img).pack()
        info = (
            f"palta_id: {foto.palta_id}   ·   {foto.hora_legible}   ·   "
            f"{img.width}×{img.height}   ·   {foto.tamano_kb:.1f} KB"
        )
        ttk.Label(win, text=info, padding=8).pack()
        ttk.Label(win, text=str(foto.ruta.resolve()), foreground="#666", padding=(8, 0)).pack()

        win.image = tk_img  # evita GC
        img.close()


if __name__ == "__main__":
    VisorFotos().mainloop()
