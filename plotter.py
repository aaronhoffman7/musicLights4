import serial
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
import numpy as np

# === CONFIG ===
SERIAL_PORT = '/dev/cu.usbserial-0001'
BAUD_RATE = 115200
WINDOW_SIZE = 250
PLOT_BANDS = list(range(7))  # Bands 0 through 6

# === COLOR GROUPING ===
band_groups = {
    0: ('r', 'Bass'),
    1: ('r', 'Bass'),
    2: ('g', 'Mid'),
    3: ('g', 'Mid'),
    4: ('g', 'Mid'),
    5: ('b', 'Treble'),
    6: ('b', 'Treble'),
}

# === SERIAL SETUP ===
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

# === QT APP + LAYOUT ===
app = QtWidgets.QApplication([])
main_widget = QtWidgets.QWidget()
main_layout = QtWidgets.QHBoxLayout(main_widget)

# === PLOT AREA ===
win = pg.GraphicsLayoutWidget(title="MSGEQ7 Smoothed Bands")
win.resize(1000, 600)
plot = win.addPlot(title="Smoothed Bands")
plot.setYRange(0, 1023)
plot.showGrid(x=True, y=True)
legend = plot.addLegend(offset=(10, 10))
main_layout.addWidget(win)

# === CHECKBOX PANEL ===
checkbox_panel = QtWidgets.QVBoxLayout()
checkbox_panel.addWidget(QtWidgets.QLabel("Show/Hide Bands:"))
checkboxes = {}

for band in PLOT_BANDS:
    _, label = band_groups[band]
    cb = QtWidgets.QCheckBox(f"Band {band} ({label})")
    cb.setChecked(True)
    checkboxes[band] = cb
    checkbox_panel.addWidget(cb)

main_layout.addLayout(checkbox_panel)

# === DATA + CURVES ===
data = {band: np.zeros(WINDOW_SIZE) for band in PLOT_BANDS}
curves = {}

text_bass = pg.TextItem("Bass Threshold", color='r', anchor=(0,1))
text_treble = pg.TextItem("Treble Threshold", color='b', anchor=(0,1))
plot.addItem(text_bass)
plot.addItem(text_treble)

for band in PLOT_BANDS:
    color, label = band_groups[band]
    pen = pg.mkPen(color, width=2)
    curve = plot.plot(pen=pen)
    legend.addItem(curve, f"Band {band} ({label})")
    curves[band] = curve



# === Threshold Lines ===


threshold_line_bass = pg.InfiniteLine(pos=0, angle=0)
threshold_line_treble = pg.InfiniteLine(pos=0, angle=0)
threshold_line_bass.setPen(pg.mkPen((255, 0, 0, 150), width=2, style=QtCore.Qt.DashLine))
threshold_line_treble.setPen(pg.mkPen((0, 0, 255, 150), width=2, style=QtCore.Qt.DashLine))
plot.addItem(threshold_line_bass)
plot.addItem(threshold_line_treble)



# === UPDATE FUNCTION ===
def update():
    try:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        line = line.rstrip(",")
        parts = line.split(",")

        print("Parsed parts:", parts)

        if len(parts) == 9 and all(p.replace('.', '', 1).isdigit() for p in parts):
            values = [float(p) for p in parts]

            for band in PLOT_BANDS:
                data[band] = np.roll(data[band], -1)
                data[band][-1] = values[band]

                if checkboxes[band].isChecked():
                    curves[band].setData(data[band])
                else:
                    curves[band].setData([])

            # Update threshold lines and text labels
            bass_thresh = max(0, min(values[7], 1023))
            treble_thresh = max(0, min(values[8], 1023))

            threshold_line_bass.setValue(bass_thresh)
            threshold_line_treble.setValue(treble_thresh)
            text_bass.setPos(0, bass_thresh)
            text_treble.setPos(0, treble_thresh)

        else:
            print("Malformed line skipped:", line)

    except Exception as e:
        print("Plot update error:", e)




# === START TIMER LOOP ===
timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(5)

# === SHOW APP ===
main_widget.setWindowTitle("MSGEQ7 Visualizer with Band Control")
main_widget.show()
QtWidgets.QApplication.instance().exec_()
