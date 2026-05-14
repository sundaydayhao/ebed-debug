#pragma once

#include "Domain.h"
#include "TransportClient.h"

#include <QMainWindow>
#include <QMap>
#include <QVariantMap>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

#ifdef EBED_HAS_QT_CHARTS
namespace QtCharts {
class QChartView;
class QLineSeries;
}
#endif

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private:
  void setupUi();
  void populateChannels();
  void populateSerialPorts();
  void populateTemplates();
  void populateScripts();
  void populateCanSignals();
  void refreshAllViews();
  void refreshTabVisibility();
  void refreshHeader();
  void refreshTerminal();
  void refreshLogTable();
  void refreshMetrics();
  void refreshChart();
  void refreshScriptEditor();
  void refreshProtocolViews();
  void refreshReportSummary();
  void refreshConnectionForm();
  void selectChannel(ChannelKey channel);
  void setConnected(ChannelKey channel, bool connected);
  bool isConnected(ChannelKey channel) const;
  void addLog(ChannelKey channel, Direction direction, PayloadFormat format, const QString &payload, const QString &meta = {}, const QString &decodedOverride = {});
  void sendPayload(ChannelKey channel, const QString &payload, PayloadFormat format, const QVariantMap &meta = {});
  void connectChannel(ChannelKey channel);
  void closeChannel(ChannelKey channel, bool logClose = true);
  void saveTextFile(const QString &title, const QString &defaultPath, const QString &content, const QString &filter);
  AbstractTransportClient *buildClient(ChannelKey channel);
  QVariantMap configForChannel(ChannelKey channel) const;
  PayloadFormat currentTxFormat() const;
  QVector<LogEntry> filteredLogs() const;
  QVector<ChartPoint> filteredChartPoints() const;
  int currentScriptIndex() const;
  QStringList activeScriptLines() const;
  void stopScript(const QString &scriptId, bool logStop = true);

private slots:
  void onChannelSelectionChanged();
  void onConnectClicked();
  void onSendClicked();
  void onRecordingClicked();
  void onClearLogsClicked();
  void onSearchChanged();
  void onRunTemplate();
  void onTemplateActivated(QTreeWidgetItem *item, int column);
  void onScriptSelectionChanged();
  void onToggleScript();
  void onAddScript();
  void onScriptEdited();
  void onSendGeneratedModbus();
  void onParseCanFrame();
  void onExportCsv();
  void onExportJson();
  void onExportReport();

private:
  QVector<ChannelInfo> m_channels;
  QVector<ProtocolTemplate> m_templates;
  QVector<AutoScript> m_scripts;
  QVector<CanSignal> m_canSignals;
  QVector<LogEntry> m_logs;
  QVector<ChartPoint> m_chartPoints;

  ChannelKey m_activeChannel = ChannelKey::Serial;
  bool m_recording = true;
  QMap<int, bool> m_connected;
  QMap<int, AbstractTransportClient *> m_clients;
  QMap<QString, QTimer *> m_scriptTimers;

  QListWidget *m_channelList = nullptr;
  QLabel *m_activeChannelLabel = nullptr;
  QLabel *m_activeChannelSummaryLabel = nullptr;
  QLabel *m_runtimeLabel = nullptr;
  QLabel *m_serialSupportLabel = nullptr;
  QLabel *m_mqttSupportLabel = nullptr;
  QPushButton *m_connectButton = nullptr;
  QPushButton *m_recordButton = nullptr;
  QLabel *m_onlineMetric = nullptr;
  QLabel *m_rxMetric = nullptr;
  QLabel *m_txMetric = nullptr;
  QLabel *m_bytesMetric = nullptr;
  QTabWidget *m_tabs = nullptr;
  QWidget *m_consolePage = nullptr;
  QWidget *m_chartPage = nullptr;
  QWidget *m_scriptsPage = nullptr;
  QWidget *m_templatePage = nullptr;
  QWidget *m_protocolPage = nullptr;
  QWidget *m_logsPage = nullptr;
  QWidget *m_reportPage = nullptr;

  QPlainTextEdit *m_terminalView = nullptr;
  QComboBox *m_terminalViewModeCombo = nullptr;
  QComboBox *m_txFormatCombo = nullptr;
  QPlainTextEdit *m_txEditor = nullptr;
  QStackedWidget *m_connectionStack = nullptr;

  QComboBox *m_serialPathCombo = nullptr;
  QComboBox *m_serialBaudCombo = nullptr;
  QComboBox *m_serialDataBitsCombo = nullptr;
  QComboBox *m_serialStopBitsCombo = nullptr;
  QComboBox *m_serialParityCombo = nullptr;

  QLineEdit *m_tcpHostEdit = nullptr;
  QSpinBox *m_tcpPortSpin = nullptr;

  QSpinBox *m_udpLocalPortSpin = nullptr;
  QLineEdit *m_udpRemoteHostEdit = nullptr;
  QSpinBox *m_udpRemotePortSpin = nullptr;

  QLineEdit *m_mqttUrlEdit = nullptr;
  QLineEdit *m_mqttClientIdEdit = nullptr;
  QLineEdit *m_mqttSubscribeEdit = nullptr;
  QLineEdit *m_mqttPublishEdit = nullptr;
  QLineEdit *m_mqttUsernameEdit = nullptr;
  QLineEdit *m_mqttPasswordEdit = nullptr;

  QLineEdit *m_modbusTcpHostEdit = nullptr;
  QSpinBox *m_modbusTcpPortSpin = nullptr;
  QComboBox *m_modbusSerialPathCombo = nullptr;
  QComboBox *m_modbusBaudCombo = nullptr;

  QLineEdit *m_canInputEdit = nullptr;

#ifdef EBED_HAS_QT_CHARTS
  QtCharts::QChartView *m_chartView = nullptr;
  QtCharts::QLineSeries *m_voltageSeries = nullptr;
  QtCharts::QLineSeries *m_currentSeries = nullptr;
  QtCharts::QLineSeries *m_temperatureSeries = nullptr;
  QtCharts::QLineSeries *m_rpmSeries = nullptr;
#else
  QTextEdit *m_chartFallbackView = nullptr;
#endif

  QListWidget *m_scriptList = nullptr;
  QLineEdit *m_scriptNameEdit = nullptr;
  QComboBox *m_scriptChannelCombo = nullptr;
  QSpinBox *m_scriptIntervalSpin = nullptr;
  QCheckBox *m_scriptLoopCheck = nullptr;
  QPlainTextEdit *m_scriptLinesEdit = nullptr;
  QPushButton *m_scriptToggleButton = nullptr;

  QTreeWidget *m_templateTree = nullptr;
  QStackedWidget *m_protocolStack = nullptr;
  QWidget *m_protocolEmptyPage = nullptr;
  QWidget *m_protocolModbusPage = nullptr;
  QWidget *m_protocolCanPage = nullptr;

  QComboBox *m_modbusVariantCombo = nullptr;
  QSpinBox *m_modbusUnitIdSpin = nullptr;
  QComboBox *m_modbusFunctionCombo = nullptr;
  QSpinBox *m_modbusAddressSpin = nullptr;
  QSpinBox *m_modbusQuantitySpin = nullptr;
  QSpinBox *m_modbusValueSpin = nullptr;
  QSpinBox *m_modbusTransactionIdSpin = nullptr;
  QLineEdit *m_modbusFrameEdit = nullptr;
  QPlainTextEdit *m_modbusDecodeEdit = nullptr;
  QLabel *m_modbusDecodeLabel = nullptr;
  QLabel *m_canFrameLabel = nullptr;
  QTableWidget *m_canSignalTable = nullptr;

  QLineEdit *m_searchEdit = nullptr;
  QTableWidget *m_logTable = nullptr;

  QLabel *m_reportSummaryLabel = nullptr;
};
