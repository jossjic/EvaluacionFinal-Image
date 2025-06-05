from PyQt5 import QtWidgets, uic
from PyQt5.QtWidgets import QFileDialog, QMenuBar, QAction
from PyQt5.QtCore import QThread, pyqtSignal
import sys
import subprocess
import os

class Worker(QThread):
    progreso = pyqtSignal(str)
    terminado = pyqtSignal(str)
    error = pyqtSignal(str)
    progreso_avance = pyqtSignal(int)

    def __init__(self, comando, reporte_path, total_esperado):
        super().__init__()
        self.comando = comando
        self.reporte_path = reporte_path
        self.total_esperado = total_esperado

    def run(self):
        try:
            proceso = subprocess.Popen(self.comando, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            procesadas = 0
            for linea in proceso.stdout:
                linea = linea.strip()
                self.progreso.emit(linea)
                if "-> [" in linea:
                    procesadas += 1
                    porcentaje = int((procesadas / self.total_esperado) * 100)
                    self.progreso_avance.emit(min(porcentaje, 100))

            stderr = proceso.stderr.read()
            returncode = proceso.wait()

            salida = ""
            if returncode != 0:
                salida += f"\n⚠ El programa terminó con código {returncode}\n"
            if stderr:
                salida += f"\n⚠ STDERR:\n{stderr.strip()}"

            if os.path.exists(self.reporte_path):
                with open(self.reporte_path, "r") as f:
                    salida += "\n📄 Reporte total:\n" + f.read()
            else:
                salida += "\n⚠ No se encontró 'reporte_total.txt'."

            self.terminado.emit(salida)

        except Exception as e:
            self.error.emit(str(e))


class CopiadorWorker(QThread):
    progreso = pyqtSignal(str)
    terminado = pyqtSignal(list)
    error = pyqtSignal(str)
    avance = pyqtSignal(int)

    def __init__(self, origen_dir, destino_dir):
        super().__init__()
        self.origen_dir = origen_dir
        self.destino_dir = destino_dir

    def run(self):
        try:
            imagenes = [
                f for f in os.listdir(self.origen_dir)
                if f.lower().endswith(".bmp") and os.path.isfile(os.path.join(self.origen_dir, f))
            ]
            if not imagenes:
                self.error.emit("⚠ No se encontraron imágenes BMP válidas.")
                return

            for f in os.listdir(self.destino_dir):
                try:
                    os.remove(os.path.join(self.destino_dir, f))
                except Exception as e:
                    self.progreso.emit(f"❌ No se pudo borrar {f}: {e}")

            copiadas = []
            total = len(imagenes)
            for i, f in enumerate(imagenes, 1):
                origen = os.path.join(self.origen_dir, f)
                destino = os.path.join(self.destino_dir, f)
                try:
                    with open(origen, "rb") as src, open(destino, "wb") as dst:
                        dst.write(src.read())
                    copiadas.append(f)
                    self.avance.emit(int((i / total) * 100))
                except Exception as e:
                    self.progreso.emit(f"❌ Error copiando {f}: {e}")

            self.terminado.emit(copiadas)
        except Exception as e:
            self.error.emit(str(e))

class Interfaz(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        uic.loadUi("/mirror/diff_images/qdesigner.ui", self)

        # Conexiones de botones
        self.botonEntrada.clicked.connect(self.seleccionar_entrada)
        self.botonProcesar.clicked.connect(self.ejecutar_programa_c)
        self.horizontalSlider.valueChanged.connect(self.actualizar_valor_slider)

        # Configuración del slider
        self.horizontalSlider.setMinimum(3)
        self.horizontalSlider.setMaximum(155)
        self.horizontalSlider.setSingleStep(2)
        self.horizontalSlider.setPageStep(2)
        self.horizontalSlider.setTickInterval(2)
        self.horizontalSlider.setValue(3)

        self.actualizar_valor_slider(self.horizontalSlider.value())

        # Directorio de procesamiento
        self.img_dir = "/mirror/diff_images/img_gui"
        os.makedirs(self.img_dir, exist_ok=True)

        self.machinefile_path = "/mirror/machinefile"
        self.total_esperado = 0

    def actualizar_valor_slider(self, valor):
        # Forzar a valor impar
        if valor % 2 == 0:
            valor += 1
            self.horizontalSlider.setValue(valor)
        self.sliderLabel.setText(f"Kernel: {valor}")

    def seleccionar_entrada(self):
        self.resultadosTexto.clear()

        carpeta = QFileDialog.getExistingDirectory(self, "Selecciona carpeta de entrada")
        if carpeta:
            if os.path.abspath(carpeta) == os.path.abspath(self.img_dir):
                self.resultadosTexto.setText("⚠ No puedes seleccionar la carpeta de procesamiento 'img/' como carpeta de entrada.")
                return

            self.rutaEntrada.setText(carpeta)
            self.resultadosTexto.append("📥 Copiando imágenes, espera...")
            self.progressBar.setValue(0)

            self.copiador = CopiadorWorker(carpeta, self.img_dir)
            self.copiador.progreso.connect(lambda msg: self.resultadosTexto.append(msg))
            self.copiador.avance.connect(self.progressBar.setValue)
            self.copiador.terminado.connect(self.copiado_finalizado)
            self.copiador.error.connect(self.mostrar_error)
            self.copiador.start()

    def copiado_finalizado(self, copiadas_ok):
        self.total_esperado = len(copiadas_ok) * 6
        if not copiadas_ok:
            self.resultadosTexto.append("❌ No se pudieron copiar las imágenes BMP.")
            return

        self.resultadosTexto.append(
            f"✔ Se copiaron {len(copiadas_ok)} imágenes BMP válidas a 'img/'.\n"
            f"📂 Carpeta de procesamiento: {self.img_dir}"
        )

    def ejecutar_programa_c(self):
        carpeta = self.rutaEntrada.text().strip()
        if not carpeta or not os.path.exists(carpeta):
            self.resultadosTexto.setText("⚠ Debes seleccionar una carpeta válida primero.")
            return

        self.resultadosTexto.append("\n🚀 Ejecutando programa en C con MPI...")
        self.progressBar.setValue(0)

        kernel_size = self.horizontalSlider.value()
        wrapper_path = "/mirror/diff_images/procesador_wrapper.sh"
        reporte_path = "/mirror/diff_images/reporte_total.txt"

        if not os.path.exists(wrapper_path) or not os.access(wrapper_path, os.X_OK):
            self.resultadosTexto.append(f"❌ El script no está disponible o sin permisos: {wrapper_path}")
            return
        if not os.path.exists(self.machinefile_path):
            self.resultadosTexto.append(f"❌ No se encontró el machinefile: {self.machinefile_path}")
            return

        comando = [
            "mpiexec",
            "-n", "13",
            "-f", self.machinefile_path,
            wrapper_path,
            str(kernel_size)
        ]

        self.botonProcesar.setEnabled(False)
        self.worker = Worker(comando, reporte_path, self.total_esperado)
        self.worker.progreso.connect(lambda linea: self.resultadosTexto.append(f"> {linea}"))
        self.worker.progreso_avance.connect(self.progressBar.setValue)
        self.worker.terminado.connect(self.procesamiento_finalizado)
        self.worker.error.connect(self.mostrar_error)
        self.worker.start()

    def procesamiento_finalizado(self, resumen):
        self.resultadosTexto.append(resumen + "\n🏁 Proceso finalizado.")
        self.botonProcesar.setEnabled(True)

    def mostrar_error(self, err):
        self.resultadosTexto.append(f"❌ Error ejecutando el programa: {err}")
        self.botonProcesar.setEnabled(True)


if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    ventana = Interfaz()
    ventana.show()
    sys.exit(app.exec_())
