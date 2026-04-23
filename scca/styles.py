DASHBOARD_QSS = """
QWidget {
    background-color: #0a0f14;
    color: #d8dde7;
    font-family: 'Rajdhani', 'Noto Sans', sans-serif;
}

QMainWindow {
    background: qlineargradient(
        x1: 0,
        y1: 0,
        x2: 1,
        y2: 1,
        stop: 0 #090d12,
        stop: 1 #101720
    );
}

QFrame#panel {
    background-color: rgba(20, 29, 38, 220);
    border: 1px solid #283443;
    border-radius: 12px;
}

QFrame#panel[sidebar="true"] {
    background-color: rgba(18, 26, 34, 240);
    border: 1px solid #324355;
}

QLabel#title {
    font-size: 24px;
    font-weight: 700;
    color: #f58f2d;
    letter-spacing: 1px;
}

QLabel#subtitle {
    font-size: 13px;
    font-weight: 600;
    color: #7f93a8;
    text-transform: uppercase;
}

QLabel#subtitleTelemetry {
    font-size: 13px;
    font-weight: 600;
    color: #7f93a8;
    text-transform: uppercase;
    padding-top: 0px;
    padding-bottom: 0px;
    margin-top: 0px;
    margin-bottom: 0px;
    min-height: 14px;
    max-height: 16px;
}

QLabel#sectionTitle {
    font-size: 16px;
    font-weight: 700;
    color: #d8e4f0;
    margin-top: 6px;
}

QLabel#panelHint {
    font-size: 12px;
    font-weight: 600;
    color: #94a7bb;
    line-height: 1.3;
}

QLabel#displayValue {
    font-size: 56px;
    font-weight: 700;
    color: #ffad4a;
}

QLabel#stateLabel {
    font-size: 14px;
    font-weight: 600;
    padding: 6px 10px;
    border-radius: 8px;
    border: 1px solid #324255;
    background-color: #1d2a38;
}

QLabel[state="ok"] {
    color: #16ff9a;
    border-color: #1a7f5a;
    background-color: #113126;
}

QLabel[state="warn"] {
    color: #ffad4a;
    border-color: #8d5b21;
    background-color: #352312;
}

QLabel[state="off"] {
    color: #8ea0b1;
    border-color: #3b4b5d;
    background-color: transparent;
}

QLabel#criticalAlert {
    font-size: 20px;
    font-weight: 800;
    color: #ffd0d0;
    padding: 10px;
    border-radius: 10px;
    border: 2px solid #af2731;
    background-color: #611015;
}

QLabel[flash="true"] {
    background-color: #98151d;
    border-color: #fa4a52;
    color: #fff4f4;
}

QProgressBar#verticalGauge {
    background-color: #131d2a;
    border: 1px solid #304156;
    border-radius: 8px;
    text-align: center;
}

QProgressBar#verticalGauge::chunk {
    background-color: #f58f2d;
    border-radius: 6px;
}

QPushButton#matrixTile {
    font-size: 14px;
    font-weight: 700;
    padding: 8px;
    border-radius: 10px;
}

QPushButton#matrixTile[tileKind="maneuver"][runState="idle"] {
    color: #d8e3ef;
    border: 2px solid #5a6f87;
    background-color: #273749;
}

QPushButton#matrixTile[tileKind="maneuver"][runState="active"] {
    color: #eafff2;
    border: 2px solid #16ff9a;
    background-color: #1d6548;
}

QPushButton#matrixTile[tileKind="pane"][runState="idle"] {
    color: #d8e3ef;
    border: 2px solid #5a6f87;
    background-color: #273749;
}

QPushButton#matrixTile[tileKind="pane"][runState="active"] {
    color: #fff2f3;
    border: 2px solid #ff6e78;
    background-color: #8c1f2a;
}

QPushButton#matrixTile:hover {
    border-width: 2px;
}

QComboBox#maneuverSelector {
    font-size: 14px;
    font-weight: 700;
    color: #e8edf5;
    min-height: 34px;
    padding: 6px 10px;
    border-radius: 8px;
    border: 1px solid #38516d;
    background-color: #162637;
}

QComboBox#maneuverSelector:hover {
    border-color: #4d6d8f;
    background-color: #1c3147;
}

QComboBox#maneuverSelector::drop-down {
    border: none;
    width: 24px;
}

QComboBox#maneuverSelector QAbstractItemView {
    background-color: #142332;
    color: #dce6f0;
    border: 1px solid #38516d;
    selection-background-color: #254766;
    selection-color: #ffffff;
}

QCheckBox#toggleSwitch {
    spacing: 10px;
    font-size: 14px;
    font-weight: 700;
    color: #dbe5ef;
    padding: 8px 10px;
    border-radius: 9px;
    border: 1px solid #304156;
    background-color: #13202d;
}

QCheckBox#toggleSwitch::indicator {
    width: 48px;
    height: 24px;
    border-radius: 12px;
    border: 1px solid #3b4f63;
    background-color: #334354;
}

QCheckBox#toggleSwitch::indicator:checked {
    border: 1px solid #1e8f62;
    background-color: #16ff9a;
}

QCheckBox#toggleSwitch[danger="true"]::indicator:checked {
    border: 1px solid #b83a44;
    background-color: #f04a54;
}

QCheckBox#toggleSwitch:hover {
    border-color: #46617d;
    background-color: #172838;
}
"""
