#include "MainWindow.h"

#include "ProtocolUtils.h"
#include "TemplateRepository.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#ifdef EBED_HAS_QT_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif

namespace {

QString metricText(const QString &label, const QString &value) {
  return QString("<b>%1</b><br/><span style='color:#64748b'>%2</span>").arg(value, label);
}

QWidget *wrapPanel(QWidget *child, const QString &title, const QString &subtitle = {}) {
  auto *panel = new QFrame;
  panel->setObjectName("panel");
  auto *layout = new QVBoxLayout(panel);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(12);

  if (!title.isEmpty()) {
    auto *titleLabel = new QLabel(QString("<b>%1</b>%2").arg(title, subtitle.isEmpty() ? "" : QString("<br/><span style='color:#64748b'>%1</span>").arg(subtitle)));
    titleLabel->setTextFormat(Qt::RichText);
    layout->addWidget(titleLabel);
  }

  layout->addWidget(child);
  return panel;
}

void configureMetricLabel(QLabel *label) {
  label->setTextFormat(Qt::RichText);
  label->setAlignment(Qt::AlignCenter);
  label->setMinimumWidth(120);
  label->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
}

QString linesToText(const QStringList &lines) {
  return lines.join('\n');
}

QString serialModuleHint() {
  return QStringLiteral("当前构建未启用 Qt SerialPort。请安装 qt6-serialport-dev 后重新执行 cmake -S . -B build && cmake --build build。");
}

QString mqttModuleHint() {
  return QStringLiteral("当前构建未启用 Qt MQTT。请安装 qt6-mqtt-dev 后重新执行 cmake -S . -B build && cmake --build build。");
}

QString unsupportedChannelMessage(ChannelKey channel, const QString &modbusVariant = {}) {
  switch (channel) {
    case ChannelKey::Serial:
      return serialModuleHint();
    case ChannelKey::Mqtt:
      return mqttModuleHint();
    case ChannelKey::Modbus:
      if (modbusVariant != "tcp") {
        return QStringLiteral("当前构建未启用 Qt SerialPort，Modbus RTU 不可用。请安装 qt6-serialport-dev 后重新执行 cmake -S . -B build && cmake --build build。");
      }
      break;
    case ChannelKey::Tcp:
    case ChannelKey::Udp:
    case ChannelKey::Can:
      break;
  }
  return {};
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_channels = defaultChannels();
  m_templates = defaultProtocolTemplates();
  m_scripts = defaultStarterScripts();
  m_canSignals = defaultCanSignals();
  m_chartPoints.append(ChartPoint{0, nowStamp(), 3.3, 0.42, 35.0, 1200.0});

  setupUi();
  populateChannels();
  populateSerialPorts();
  populateTemplates();
  populateScripts();
  populateCanSignals();
  refreshAllViews();
}

MainWindow::~MainWindow() {
  const auto timers = m_scriptTimers;
  for (QTimer *timer : timers) {
    if (timer) {
      timer->stop();
      timer->deleteLater();
    }
  }
  const auto clients = m_clients;
  for (AbstractTransportClient *client : clients) {
    if (client) {
      client->closeClient();
      client->deleteLater();
    }
  }
}

void MainWindow::setupUi() {
  setWindowTitle(QStringLiteral("嵌入式调试上位机 Pro"));
  resize(1560, 980);

  auto *root = new QWidget;
  auto *rootLayout = new QHBoxLayout(root);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto *sidebar = new QFrame;
  sidebar->setObjectName("sidebar");
  sidebar->setMinimumWidth(260);
  auto *sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->setContentsMargins(20, 20, 20, 20);
  sidebarLayout->setSpacing(16);

  auto *brand = new QLabel(QStringLiteral("<div style='font-size:22px;font-weight:700'>嵌入式调试上位机 Pro</div><div style='color:#94a3b8'>Qt Widgets Desktop Workstation</div>"));
  brand->setTextFormat(Qt::RichText);
  sidebarLayout->addWidget(brand);

  m_channelList = new QListWidget;
  connect(m_channelList, &QListWidget::currentRowChanged, this, [this](int) { onChannelSelectionChanged(); });
  sidebarLayout->addWidget(m_channelList, 1);

  auto *runtimeBox = new QFrame;
  runtimeBox->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *runtimeLayout = new QFormLayout(runtimeBox);
  runtimeLayout->setContentsMargins(12, 12, 12, 12);
  runtimeLayout->setHorizontalSpacing(12);
  runtimeLayout->setVerticalSpacing(8);
  m_runtimeLabel = new QLabel;
  m_serialSupportLabel = new QLabel;
  m_mqttSupportLabel = new QLabel;
  runtimeLayout->addRow(QStringLiteral("运行环境"), m_runtimeLabel);
  runtimeLayout->addRow(QStringLiteral("串口模块"), m_serialSupportLabel);
  runtimeLayout->addRow(QStringLiteral("MQTT 模块"), m_mqttSupportLabel);
  sidebarLayout->addWidget(runtimeBox);

  auto *workspace = new QWidget;
  auto *workspaceLayout = new QVBoxLayout(workspace);
  workspaceLayout->setContentsMargins(20, 20, 20, 20);
  workspaceLayout->setSpacing(16);

  auto *header = new QFrame;
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  m_activeChannelLabel = new QLabel;
  m_activeChannelLabel->setTextFormat(Qt::RichText);
  headerLayout->addWidget(m_activeChannelLabel, 1);

  m_connectButton = new QPushButton(QStringLiteral("连接"));
  connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
  headerLayout->addWidget(m_connectButton);

  m_recordButton = new QPushButton(QStringLiteral("记录中"));
  connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::onRecordingClicked);
  headerLayout->addWidget(m_recordButton);

  auto *clearButton = new QPushButton(QStringLiteral("清空"));
  connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearLogsClicked);
  headerLayout->addWidget(clearButton);
  workspaceLayout->addWidget(header);

  auto *metrics = new QWidget;
  auto *metricsLayout = new QHBoxLayout(metrics);
  metricsLayout->setContentsMargins(0, 0, 0, 0);
  metricsLayout->setSpacing(12);
  m_onlineMetric = new QLabel;
  m_rxMetric = new QLabel;
  m_txMetric = new QLabel;
  m_bytesMetric = new QLabel;
  for (QLabel *label : {m_onlineMetric, m_rxMetric, m_txMetric, m_bytesMetric}) {
    configureMetricLabel(label);
    metricsLayout->addWidget(label);
  }
  metricsLayout->addStretch(1);
  workspaceLayout->addWidget(metrics);

  m_tabs = new QTabWidget;
  workspaceLayout->addWidget(m_tabs, 1);

  auto *consolePage = new QWidget;
  auto *consoleLayout = new QHBoxLayout(consolePage);
  consoleLayout->setContentsMargins(0, 0, 0, 0);
  consoleLayout->setSpacing(16);

  auto *terminalPane = new QWidget;
  auto *terminalLayout = new QVBoxLayout(terminalPane);
  terminalLayout->setContentsMargins(0, 0, 0, 0);
  terminalLayout->setSpacing(12);

  m_terminalView = new QPlainTextEdit;
  m_terminalView->setReadOnly(true);
  terminalLayout->addWidget(wrapPanel(m_terminalView, QStringLiteral("收发终端"), QStringLiteral("当前通道最近 120 条日志")), 1);

  auto *composer = new QWidget;
  auto *composerLayout = new QVBoxLayout(composer);
  composerLayout->setContentsMargins(0, 0, 0, 0);
  composerLayout->setSpacing(8);
  auto *formatRow = new QWidget;
  auto *formatRowLayout = new QHBoxLayout(formatRow);
  formatRowLayout->setContentsMargins(0, 0, 0, 0);
  formatRowLayout->setSpacing(8);
  m_txFormatCombo = new QComboBox;
  m_txFormatCombo->addItem("TEXT", static_cast<int>(PayloadFormat::Text));
  m_txFormatCombo->addItem("HEX", static_cast<int>(PayloadFormat::Hex));
  m_txFormatCombo->addItem("JSON", static_cast<int>(PayloadFormat::Json));
  formatRowLayout->addWidget(m_txFormatCombo);
  auto *toHexButton = new QPushButton(QStringLiteral("转 HEX"));
  connect(toHexButton, &QPushButton::clicked, this, [this]() { m_txEditor->setPlainText(textToHex(m_txEditor->toPlainText())); });
  auto *toAsciiButton = new QPushButton(QStringLiteral("转 ASCII"));
  connect(toAsciiButton, &QPushButton::clicked, this, [this]() { m_txEditor->setPlainText(hexToAscii(m_txEditor->toPlainText())); });
  formatRowLayout->addWidget(toHexButton);
  formatRowLayout->addWidget(toAsciiButton);
  formatRowLayout->addStretch(1);
  composerLayout->addWidget(formatRow);

  m_txEditor = new QPlainTextEdit;
  m_txEditor->setPlainText("AT\r\n");
  m_txEditor->setMaximumHeight(120);
  composerLayout->addWidget(m_txEditor);

  auto *sendButton = new QPushButton(QStringLiteral("发送"));
  connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
  composerLayout->addWidget(sendButton, 0, Qt::AlignRight);
  terminalLayout->addWidget(wrapPanel(composer, QStringLiteral("发送器"), QStringLiteral("支持文本 / HEX / JSON")), 0);

  consoleLayout->addWidget(terminalPane, 2);

  auto *formPane = new QWidget;
  auto *formPaneLayout = new QVBoxLayout(formPane);
  formPaneLayout->setContentsMargins(0, 0, 0, 0);
  formPaneLayout->setSpacing(12);

  m_connectionStack = new QStackedWidget;

  auto *serialFormWidget = new QWidget;
  auto *serialForm = new QFormLayout(serialFormWidget);
  m_serialPathCombo = new QComboBox;
  m_serialPathCombo->setEditable(true);
  m_serialBaudSpin = new QSpinBox;
  m_serialBaudSpin->setRange(1200, 2000000);
  m_serialBaudSpin->setValue(115200);
  m_serialDataBitsSpin = new QSpinBox;
  m_serialDataBitsSpin->setRange(5, 8);
  m_serialDataBitsSpin->setValue(8);
  m_serialStopBitsSpin = new QSpinBox;
  m_serialStopBitsSpin->setRange(1, 2);
  m_serialStopBitsSpin->setValue(1);
  m_serialParityCombo = new QComboBox;
  m_serialParityCombo->addItems({"none", "even", "odd"});
  serialForm->addRow(QStringLiteral("端口"), m_serialPathCombo);
  serialForm->addRow(QStringLiteral("波特率"), m_serialBaudSpin);
  serialForm->addRow(QStringLiteral("数据位"), m_serialDataBitsSpin);
  serialForm->addRow(QStringLiteral("停止位"), m_serialStopBitsSpin);
  serialForm->addRow(QStringLiteral("校验"), m_serialParityCombo);
  m_connectionStack->addWidget(serialFormWidget);

  auto *tcpFormWidget = new QWidget;
  auto *tcpForm = new QFormLayout(tcpFormWidget);
  m_tcpHostEdit = new QLineEdit("127.0.0.1");
  m_tcpPortSpin = new QSpinBox;
  m_tcpPortSpin->setRange(1, 65535);
  m_tcpPortSpin->setValue(9000);
  tcpForm->addRow("Host", m_tcpHostEdit);
  tcpForm->addRow("Port", m_tcpPortSpin);
  m_connectionStack->addWidget(tcpFormWidget);

  auto *udpFormWidget = new QWidget;
  auto *udpForm = new QFormLayout(udpFormWidget);
  m_udpLocalPortSpin = new QSpinBox;
  m_udpLocalPortSpin->setRange(0, 65535);
  m_udpLocalPortSpin->setValue(7001);
  m_udpRemoteHostEdit = new QLineEdit("127.0.0.1");
  m_udpRemotePortSpin = new QSpinBox;
  m_udpRemotePortSpin->setRange(1, 65535);
  m_udpRemotePortSpin->setValue(7002);
  udpForm->addRow(QStringLiteral("本地端口"), m_udpLocalPortSpin);
  udpForm->addRow(QStringLiteral("远端 Host"), m_udpRemoteHostEdit);
  udpForm->addRow(QStringLiteral("远端端口"), m_udpRemotePortSpin);
  m_connectionStack->addWidget(udpFormWidget);

  auto *mqttFormWidget = new QWidget;
  auto *mqttForm = new QFormLayout(mqttFormWidget);
  m_mqttUrlEdit = new QLineEdit("mqtt://broker.emqx.io:1883");
  m_mqttClientIdEdit = new QLineEdit("ebed-debug-pro");
  m_mqttSubscribeEdit = new QLineEdit("ebed/debug/rx");
  m_mqttPublishEdit = new QLineEdit("ebed/debug/tx");
  m_mqttUsernameEdit = new QLineEdit;
  m_mqttPasswordEdit = new QLineEdit;
  m_mqttPasswordEdit->setEchoMode(QLineEdit::Password);
  mqttForm->addRow("Broker URL", m_mqttUrlEdit);
  mqttForm->addRow("Client ID", m_mqttClientIdEdit);
  mqttForm->addRow(QStringLiteral("订阅 Topic"), m_mqttSubscribeEdit);
  mqttForm->addRow(QStringLiteral("发布 Topic"), m_mqttPublishEdit);
  mqttForm->addRow("Username", m_mqttUsernameEdit);
  mqttForm->addRow("Password", m_mqttPasswordEdit);
  m_connectionStack->addWidget(mqttFormWidget);

  auto *modbusFormWidget = new QWidget;
  auto *modbusForm = new QFormLayout(modbusFormWidget);
  m_modbusTcpHostEdit = new QLineEdit("127.0.0.1");
  m_modbusTcpPortSpin = new QSpinBox;
  m_modbusTcpPortSpin->setRange(1, 65535);
  m_modbusTcpPortSpin->setValue(502);
  m_modbusSerialPathCombo = new QComboBox;
  m_modbusSerialPathCombo->setEditable(true);
  m_modbusBaudSpin = new QSpinBox;
  m_modbusBaudSpin->setRange(1200, 2000000);
  m_modbusBaudSpin->setValue(9600);
  modbusForm->addRow(QStringLiteral("TCP Host"), m_modbusTcpHostEdit);
  modbusForm->addRow(QStringLiteral("TCP Port"), m_modbusTcpPortSpin);
  modbusForm->addRow(QStringLiteral("RTU 串口"), m_modbusSerialPathCombo);
  modbusForm->addRow(QStringLiteral("RTU 波特率"), m_modbusBaudSpin);
  m_connectionStack->addWidget(modbusFormWidget);

  auto *canFormWidget = new QWidget;
  auto *canForm = new QFormLayout(canFormWidget);
  auto *canConnectionNote = new QLabel(QStringLiteral("CAN 当前为本地解析工具，不建立总线连接。请在“协议”页输入并解析帧。"));
  canConnectionNote->setWordWrap(true);
  canForm->addRow(canConnectionNote);
  m_connectionStack->addWidget(canFormWidget);

  formPaneLayout->addWidget(wrapPanel(m_connectionStack, QStringLiteral("连接配置"), QStringLiteral("桌面端本地连接参数")), 1);
  consoleLayout->addWidget(formPane, 1);

  m_tabs->addTab(consolePage, QStringLiteral("工作台"));

  auto *chartsPage = new QWidget;
  auto *chartsLayout = new QVBoxLayout(chartsPage);
  chartsLayout->setContentsMargins(0, 0, 0, 0);
#ifdef EBED_HAS_QT_CHARTS
  auto *chart = new QtCharts::QChart;
  chart->legend()->setVisible(true);
  chart->setTitle(QStringLiteral("实时数据图表"));
  m_voltageSeries = new QtCharts::QLineSeries;
  m_currentSeries = new QtCharts::QLineSeries;
  m_temperatureSeries = new QtCharts::QLineSeries;
  m_rpmSeries = new QtCharts::QLineSeries;
  m_voltageSeries->setName(QStringLiteral("电压 V"));
  m_currentSeries->setName(QStringLiteral("电流 A"));
  m_temperatureSeries->setName(QStringLiteral("温度 C"));
  m_rpmSeries->setName(QStringLiteral("转速 rpm"));
  chart->addSeries(m_voltageSeries);
  chart->addSeries(m_currentSeries);
  chart->addSeries(m_temperatureSeries);
  chart->addSeries(m_rpmSeries);
  auto *axisX = new QtCharts::QValueAxis;
  auto *axisY = new QtCharts::QValueAxis;
  axisX->setTitleText(QStringLiteral("样本"));
  axisY->setTitleText(QStringLiteral("值"));
  chart->addAxis(axisX, Qt::AlignBottom);
  chart->addAxis(axisY, Qt::AlignLeft);
  for (auto *series : {m_voltageSeries, m_currentSeries, m_temperatureSeries, m_rpmSeries}) {
    series->attachAxis(axisX);
    series->attachAxis(axisY);
  }
  m_chartView = new QtCharts::QChartView(chart);
  m_chartView->setRenderHint(QPainter::Antialiasing);
  chartsLayout->addWidget(wrapPanel(m_chartView, QStringLiteral("实时数据图表"), QStringLiteral("从 RX JSON、文本数值和 Modbus 响应中提取指标")), 1);
#else
  m_chartFallbackView = new QTextEdit;
  m_chartFallbackView->setReadOnly(true);
  chartsLayout->addWidget(wrapPanel(m_chartFallbackView, QStringLiteral("图表降级视图"), QStringLiteral("当前构建未启用 Qt Charts，显示文本摘要")), 1);
#endif
  m_tabs->addTab(chartsPage, QStringLiteral("图表"));

  auto *scriptsPage = new QWidget;
  auto *scriptsLayout = new QHBoxLayout(scriptsPage);
  scriptsLayout->setContentsMargins(0, 0, 0, 0);
  scriptsLayout->setSpacing(16);
  m_scriptList = new QListWidget;
  connect(m_scriptList, &QListWidget::currentRowChanged, this, &MainWindow::onScriptSelectionChanged);
  scriptsLayout->addWidget(wrapPanel(m_scriptList, QStringLiteral("自动发送脚本"), QStringLiteral("按行发送，支持循环和固定间隔")), 1);

  auto *scriptEditor = new QWidget;
  auto *scriptEditorLayout = new QVBoxLayout(scriptEditor);
  scriptEditorLayout->setContentsMargins(0, 0, 0, 0);
  auto *scriptForm = new QFormLayout;
  m_scriptNameEdit = new QLineEdit;
  m_scriptChannelCombo = new QComboBox;
  for (const ChannelInfo &channel : m_channels) {
    m_scriptChannelCombo->addItem(channel.name, static_cast<int>(channel.key));
  }
  m_scriptIntervalSpin = new QSpinBox;
  m_scriptIntervalSpin->setRange(100, 600000);
  m_scriptLoopCheck = new QCheckBox(QStringLiteral("循环发送"));
  scriptForm->addRow(QStringLiteral("名称"), m_scriptNameEdit);
  scriptForm->addRow(QStringLiteral("通道"), m_scriptChannelCombo);
  scriptForm->addRow(QStringLiteral("间隔 ms"), m_scriptIntervalSpin);
  scriptForm->addRow(QString(), m_scriptLoopCheck);
  scriptEditorLayout->addLayout(scriptForm);
  m_scriptLinesEdit = new QPlainTextEdit;
  scriptEditorLayout->addWidget(m_scriptLinesEdit, 1);
  auto *scriptButtons = new QWidget;
  auto *scriptButtonsLayout = new QHBoxLayout(scriptButtons);
  scriptButtonsLayout->setContentsMargins(0, 0, 0, 0);
  auto *addScriptButton = new QPushButton(QStringLiteral("新增"));
  connect(addScriptButton, &QPushButton::clicked, this, &MainWindow::onAddScript);
  m_scriptToggleButton = new QPushButton(QStringLiteral("运行"));
  connect(m_scriptToggleButton, &QPushButton::clicked, this, &MainWindow::onToggleScript);
  scriptButtonsLayout->addWidget(addScriptButton);
  scriptButtonsLayout->addStretch(1);
  scriptButtonsLayout->addWidget(m_scriptToggleButton);
  scriptEditorLayout->addWidget(scriptButtons);
  connect(m_scriptNameEdit, &QLineEdit::textChanged, this, [this](const QString &) { onScriptEdited(); });
  connect(m_scriptChannelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { onScriptEdited(); });
  connect(m_scriptIntervalSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { onScriptEdited(); });
  connect(m_scriptLoopCheck, &QCheckBox::toggled, this, [this](bool) { onScriptEdited(); });
  connect(m_scriptLinesEdit, &QPlainTextEdit::textChanged, this, &MainWindow::onScriptEdited);
  scriptsLayout->addWidget(wrapPanel(scriptEditor, QStringLiteral("脚本编辑器")), 2);
  m_tabs->addTab(scriptsPage, QStringLiteral("脚本"));

  m_templateTree = new QTreeWidget;
  m_templateTree->setHeaderLabels({QStringLiteral("模板命令"), QStringLiteral("通道"), QStringLiteral("说明")});
  connect(m_templateTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) { onTemplateActivated(item, column); });
  auto *templatePage = new QWidget;
  auto *templateLayout = new QVBoxLayout(templatePage);
  templateLayout->setContentsMargins(0, 0, 0, 0);
  templateLayout->addWidget(wrapPanel(m_templateTree, QStringLiteral("协议模板"), QStringLiteral("双击命令可直接发送")), 1);
  auto *templateRunButton = new QPushButton(QStringLiteral("运行选中命令"));
  connect(templateRunButton, &QPushButton::clicked, this, &MainWindow::onRunTemplate);
  templateLayout->addWidget(templateRunButton, 0, Qt::AlignRight);
  m_tabs->addTab(templatePage, QStringLiteral("模板"));

  auto *protocolPage = new QWidget;
  auto *protocolLayout = new QHBoxLayout(protocolPage);
  protocolLayout->setContentsMargins(0, 0, 0, 0);
  protocolLayout->setSpacing(16);

  auto *modbusPane = new QWidget;
  auto *modbusPaneLayout = new QVBoxLayout(modbusPane);
  modbusPaneLayout->setContentsMargins(0, 0, 0, 0);
  auto *modbusBuilderForm = new QFormLayout;
  m_modbusVariantCombo = new QComboBox;
  m_modbusVariantCombo->addItem("RTU", "rtu");
  m_modbusVariantCombo->addItem("TCP", "tcp");
  m_modbusUnitIdSpin = new QSpinBox;
  m_modbusUnitIdSpin->setRange(1, 255);
  m_modbusUnitIdSpin->setValue(1);
  m_modbusFunctionCombo = new QComboBox;
  m_modbusFunctionCombo->addItem("03 Read Holding", 3);
  m_modbusFunctionCombo->addItem("04 Read Input", 4);
  m_modbusFunctionCombo->addItem("06 Write Single", 6);
  m_modbusFunctionCombo->addItem("16 Write Multiple", 16);
  m_modbusAddressSpin = new QSpinBox;
  m_modbusAddressSpin->setRange(0, 65535);
  m_modbusQuantitySpin = new QSpinBox;
  m_modbusQuantitySpin->setRange(1, 125);
  m_modbusQuantitySpin->setValue(4);
  m_modbusValueSpin = new QSpinBox;
  m_modbusValueSpin->setRange(0, 65535);
  m_modbusValueSpin->setValue(1);
  m_modbusTransactionIdSpin = new QSpinBox;
  m_modbusTransactionIdSpin->setRange(0, 65535);
  m_modbusTransactionIdSpin->setValue(1);
  modbusBuilderForm->addRow(QStringLiteral("类型"), m_modbusVariantCombo);
  modbusBuilderForm->addRow(QStringLiteral("Unit ID"), m_modbusUnitIdSpin);
  modbusBuilderForm->addRow(QStringLiteral("Function"), m_modbusFunctionCombo);
  modbusBuilderForm->addRow(QStringLiteral("Address"), m_modbusAddressSpin);
  modbusBuilderForm->addRow(QStringLiteral("Quantity"), m_modbusQuantitySpin);
  modbusBuilderForm->addRow(QStringLiteral("Value"), m_modbusValueSpin);
  modbusBuilderForm->addRow(QStringLiteral("Transaction ID"), m_modbusTransactionIdSpin);
  modbusPaneLayout->addLayout(modbusBuilderForm);
  m_modbusFrameEdit = new QLineEdit;
  m_modbusFrameEdit->setReadOnly(true);
  modbusPaneLayout->addWidget(m_modbusFrameEdit);
  auto *modbusSendButton = new QPushButton(QStringLiteral("发送帧"));
  connect(modbusSendButton, &QPushButton::clicked, this, &MainWindow::onSendGeneratedModbus);
  modbusPaneLayout->addWidget(modbusSendButton, 0, Qt::AlignRight);
  m_modbusDecodeEdit = new QPlainTextEdit;
  m_modbusDecodeEdit->setPlainText("01 03 04 01 4A 00 1F 5A 3B");
  m_modbusDecodeEdit->setMaximumHeight(100);
  m_modbusDecodeLabel = new QLabel;
  m_modbusDecodeLabel->setWordWrap(true);
  modbusPaneLayout->addWidget(new QLabel(QStringLiteral("响应解析")));
  modbusPaneLayout->addWidget(m_modbusDecodeEdit);
  modbusPaneLayout->addWidget(m_modbusDecodeLabel);
  protocolLayout->addWidget(wrapPanel(modbusPane, QStringLiteral("Modbus 帧构造"), QStringLiteral("RTU 自动 CRC，TCP 自动 MBAP")), 1);

  auto *canPane = new QWidget;
  auto *canPaneLayout = new QVBoxLayout(canPane);
  canPaneLayout->setContentsMargins(0, 0, 0, 0);
  auto *canTop = new QWidget;
  auto *canTopLayout = new QHBoxLayout(canTop);
  canTopLayout->setContentsMargins(0, 0, 0, 0);
  auto *canInput = new QLineEdit("18FF50E5#211F245804000001");
  m_canInputEdit = canInput;
  auto *canParseButton = new QPushButton(QStringLiteral("解析入库"));
  connect(canParseButton, &QPushButton::clicked, this, &MainWindow::onParseCanFrame);
  canTopLayout->addWidget(m_canInputEdit, 1);
  canTopLayout->addWidget(canParseButton);
  canPaneLayout->addWidget(canTop);
  m_canFrameLabel = new QLabel;
  m_canFrameLabel->setWordWrap(true);
  canPaneLayout->addWidget(m_canFrameLabel);
  m_canSignalTable = new QTableWidget;
  m_canSignalTable->setColumnCount(6);
  m_canSignalTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("起始位"), QStringLiteral("长度"), QStringLiteral("Scale"), QStringLiteral("Offset"), QStringLiteral("Unit")});
  m_canSignalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  connect(m_canSignalTable, &QTableWidget::cellChanged, this, [this](int row, int column) {
    if (row < 0 || row >= m_canSignals.size()) return;
    auto &signal = m_canSignals[row];
    const QString text = m_canSignalTable->item(row, column) ? m_canSignalTable->item(row, column)->text() : QString();
    switch (column) {
      case 0:
        signal.name = text;
        break;
      case 1:
        signal.startBit = text.toInt();
        break;
      case 2:
        signal.length = text.toInt();
        break;
      case 3:
        signal.scale = text.toDouble();
        break;
      case 4:
        signal.offset = text.toDouble();
        break;
      case 5:
        signal.unit = text;
        break;
      default:
        break;
    }
    refreshProtocolViews();
  });
  canPaneLayout->addWidget(m_canSignalTable, 1);
  protocolLayout->addWidget(wrapPanel(canPane, QStringLiteral("CAN 数据解析"), QStringLiteral("支持 11-bit / 29-bit ID 和 little-endian 信号")), 1);
  m_tabs->addTab(protocolPage, QStringLiteral("协议"));

  auto *logsPage = new QWidget;
  auto *logsLayout = new QVBoxLayout(logsPage);
  logsLayout->setContentsMargins(0, 0, 0, 0);
  auto *searchRow = new QWidget;
  auto *searchRowLayout = new QHBoxLayout(searchRow);
  searchRowLayout->setContentsMargins(0, 0, 0, 0);
  m_searchEdit = new QLineEdit;
  m_searchEdit->setPlaceholderText(QStringLiteral("搜索通道、载荷、解析结果"));
  connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
  auto *csvButton = new QPushButton("CSV");
  connect(csvButton, &QPushButton::clicked, this, &MainWindow::onExportCsv);
  auto *jsonButton = new QPushButton("JSON");
  connect(jsonButton, &QPushButton::clicked, this, &MainWindow::onExportJson);
  searchRowLayout->addWidget(m_searchEdit, 1);
  searchRowLayout->addWidget(csvButton);
  searchRowLayout->addWidget(jsonButton);
  logsLayout->addWidget(searchRow);
  m_logTable = new QTableWidget;
  m_logTable->setColumnCount(5);
  m_logTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("通道"), QStringLiteral("方向"), QStringLiteral("载荷"), QStringLiteral("解析")});
  m_logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  m_logTable->verticalHeader()->setVisible(false);
  m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  logsLayout->addWidget(m_logTable, 1);
  m_tabs->addTab(logsPage, QStringLiteral("日志"));

  auto *reportPage = new QWidget;
  auto *reportLayout = new QVBoxLayout(reportPage);
  reportLayout->setContentsMargins(0, 0, 0, 0);
  m_reportSummaryLabel = new QLabel;
  m_reportSummaryLabel->setWordWrap(true);
  reportLayout->addWidget(wrapPanel(m_reportSummaryLabel, QStringLiteral("报告导出"), QStringLiteral("生成包含统计、图表摘要和日志明细的 HTML 报告")));
  auto *reportButton = new QPushButton(QStringLiteral("导出 HTML 报告"));
  connect(reportButton, &QPushButton::clicked, this, &MainWindow::onExportReport);
  reportLayout->addWidget(reportButton, 0, Qt::AlignLeft);
  auto *licenseNotes = new QTextEdit;
  licenseNotes->setReadOnly(true);
  licenseNotes->setPlainText(QStringLiteral("桌面版建议继续补齐安装包发布、离线授权、设备配置库和协议插件化。Qt 方案更适合后续对接厂商 SDK、原生驱动和工业部署场景。"));
  reportLayout->addWidget(wrapPanel(licenseNotes, QStringLiteral("产品化备注")), 1);
  m_tabs->addTab(reportPage, QStringLiteral("报告"));

  rootLayout->addWidget(sidebar);
  rootLayout->addWidget(workspace, 1);

  setCentralWidget(root);

  connect(m_modbusVariantCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    refreshProtocolViews();
    refreshHeader();
  });
  connect(m_modbusUnitIdSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusFunctionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusAddressSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusQuantitySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusValueSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusTransactionIdSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshProtocolViews(); });
  connect(m_modbusDecodeEdit, &QPlainTextEdit::textChanged, this, [this]() { refreshProtocolViews(); });
  connect(m_canInputEdit, &QLineEdit::textChanged, this, [this](const QString &) { refreshProtocolViews(); });

  setStyleSheet(R"(
    QMainWindow { background: #f3f6fb; }
    #sidebar { background: #0f172a; color: #e2e8f0; }
    QListWidget { border: 1px solid #d7deea; background: white; }
    QTabWidget::pane { border: 0; }
    QTabBar::tab { padding: 10px 16px; background: #e5ebf4; margin-right: 4px; border-top-left-radius: 8px; border-top-right-radius: 8px; }
    QTabBar::tab:selected { background: white; }
    QFrame#panel { background: white; border: 1px solid #dbe4ef; border-radius: 12px; }
    QPushButton { background: #dbeafe; border: 1px solid #bfdbfe; border-radius: 8px; padding: 8px 14px; }
    QPushButton:hover { background: #bfdbfe; }
    QPlainTextEdit, QTextEdit, QLineEdit, QComboBox, QSpinBox, QTableWidget, QTreeWidget { background: white; border: 1px solid #d3dce8; border-radius: 8px; }
  )");
}

void MainWindow::populateChannels() {
  m_channelList->clear();
  for (const ChannelInfo &channel : m_channels) {
    auto *item = new QListWidgetItem(QString("%1\n%2").arg(channel.name, channel.summary), m_channelList);
    item->setData(Qt::UserRole, static_cast<int>(channel.key));
  }
  m_channelList->setCurrentRow(0);
}

void MainWindow::populateSerialPorts() {
  const QString currentSerial = m_serialPathCombo->currentText();
  const QString currentModbusSerial = m_modbusSerialPathCombo->currentText();
  const QStringList ports = availableSerialPorts();

  m_serialPathCombo->clear();
  m_modbusSerialPathCombo->clear();
  for (const QString &port : ports) {
    m_serialPathCombo->addItem(port);
    m_modbusSerialPathCombo->addItem(port);
  }

  if (!currentSerial.isEmpty()) {
    m_serialPathCombo->setEditText(currentSerial);
  } else if (ports.isEmpty()) {
    m_serialPathCombo->setEditText("/dev/ttyUSB0");
  }

  if (!currentModbusSerial.isEmpty()) {
    m_modbusSerialPathCombo->setEditText(currentModbusSerial);
  } else if (ports.isEmpty()) {
    m_modbusSerialPathCombo->setEditText("/dev/ttyUSB0");
  }
}

void MainWindow::populateTemplates() {
  m_templateTree->clear();
  for (const ProtocolTemplate &protocolTemplate : m_templates) {
    auto *parent = new QTreeWidgetItem(m_templateTree, {protocolTemplate.name, protocolTemplate.target, protocolTemplate.description});
    parent->setFirstColumnSpanned(true);
    for (const ProtocolCommand &command : protocolTemplate.commands) {
      auto *child = new QTreeWidgetItem(parent, {command.name, channelKeyToString(command.channel).toUpper(), command.description});
      child->setData(0, Qt::UserRole, static_cast<int>(command.channel));
      child->setData(0, Qt::UserRole + 1, command.payload);
      child->setData(0, Qt::UserRole + 2, static_cast<int>(command.format));
    }
  }
  m_templateTree->expandAll();
}

void MainWindow::populateScripts() {
  m_scriptList->clear();
  for (const AutoScript &script : m_scripts) {
    auto *item = new QListWidgetItem(QString("%1\n%2 · %3 ms").arg(script.name, channelKeyToString(script.channel).toUpper(), QString::number(script.intervalMs)), m_scriptList);
    item->setData(Qt::UserRole, script.id);
  }
  if (!m_scripts.isEmpty()) {
    m_scriptList->setCurrentRow(qMin(m_scriptList->currentRow(), m_scripts.size() - 1));
    if (m_scriptList->currentRow() < 0) {
      m_scriptList->setCurrentRow(0);
    }
  }
}

void MainWindow::populateCanSignals() {
  QSignalBlocker blocker(m_canSignalTable);
  m_canSignalTable->setRowCount(m_canSignals.size());
  for (int row = 0; row < m_canSignals.size(); ++row) {
    const CanSignal &signal = m_canSignals.at(row);
    m_canSignalTable->setItem(row, 0, new QTableWidgetItem(signal.name));
    m_canSignalTable->setItem(row, 1, new QTableWidgetItem(QString::number(signal.startBit)));
    m_canSignalTable->setItem(row, 2, new QTableWidgetItem(QString::number(signal.length)));
    m_canSignalTable->setItem(row, 3, new QTableWidgetItem(QString::number(signal.scale)));
    m_canSignalTable->setItem(row, 4, new QTableWidgetItem(QString::number(signal.offset)));
    m_canSignalTable->setItem(row, 5, new QTableWidgetItem(signal.unit));
  }
}

void MainWindow::refreshAllViews() {
  refreshHeader();
  refreshConnectionForm();
  refreshMetrics();
  refreshTerminal();
  refreshLogTable();
  refreshChart();
  refreshScriptEditor();
  refreshProtocolViews();
  refreshReportSummary();
  m_runtimeLabel->setText(QStringLiteral("Qt 桌面端"));
  m_serialSupportLabel->setText(serialSupportAvailable() ? QStringLiteral("可用") : QStringLiteral("未编译（需 qt6-serialport-dev）"));
  m_serialSupportLabel->setToolTip(serialSupportAvailable() ? QString() : serialModuleHint());
  m_mqttSupportLabel->setText(mqttSupportAvailable() ? QStringLiteral("可用") : QStringLiteral("未编译（需 qt6-mqtt-dev）"));
  m_mqttSupportLabel->setToolTip(mqttSupportAvailable() ? QString() : mqttModuleHint());
}

void MainWindow::refreshHeader() {
  const QString name = channelKeyToDisplayName(m_activeChannel);
  m_activeChannelLabel->setText(QString("<div style='font-size:12px;color:#64748b'>当前通道</div><div style='font-size:24px;font-weight:700'>%1</div>").arg(name));
  const QString modbusVariant = m_modbusVariantCombo ? m_modbusVariantCombo->currentData().toString() : QStringLiteral("rtu");
  const QString unsupportedMessage = unsupportedChannelMessage(m_activeChannel, modbusVariant);

  if (m_activeChannel == ChannelKey::Can) {
    m_connectButton->setEnabled(false);
    m_connectButton->setText(QStringLiteral("解析工具"));
    m_connectButton->setToolTip(QString());
  } else if (isConnected(m_activeChannel)) {
    m_connectButton->setEnabled(true);
    m_connectButton->setText(QStringLiteral("断开"));
    m_connectButton->setToolTip(QString());
  } else if (!unsupportedMessage.isEmpty()) {
    m_connectButton->setEnabled(false);
    m_connectButton->setText(QStringLiteral("模块缺失"));
    m_connectButton->setToolTip(unsupportedMessage);
  } else {
    m_connectButton->setEnabled(true);
    m_connectButton->setText(QStringLiteral("连接"));
    m_connectButton->setToolTip(QString());
  }

  m_recordButton->setText(m_recording ? QStringLiteral("记录中") : QStringLiteral("暂停记录"));
}

void MainWindow::refreshTerminal() {
  QStringList lines;
  int count = 0;
  for (const LogEntry &log : m_logs) {
    if (log.channel != m_activeChannel) {
      continue;
    }
    lines.prepend(QString("%1  %2  %3\n%4").arg(log.ts, directionToString(log.direction).toUpper(), log.payload, log.decoded));
    ++count;
    if (count >= 120) {
      break;
    }
  }
  m_terminalView->setPlainText(linesToText(lines));
}

void MainWindow::refreshLogTable() {
  const QString term = m_searchEdit->text().trimmed().toLower();
  QVector<LogEntry> filtered;
  for (const LogEntry &log : m_logs) {
    const QString haystack = QString("%1 %2 %3 %4 %5 %6 %7")
      .arg(log.ts, channelKeyToString(log.channel), directionToString(log.direction), payloadFormatToString(log.format), log.payload, log.decoded, log.meta)
      .toLower();
    if (term.isEmpty() || haystack.contains(term)) {
      filtered.append(log);
    }
  }

  m_logTable->setRowCount(filtered.size());
  for (int row = 0; row < filtered.size(); ++row) {
    const LogEntry &log = filtered.at(row);
    m_logTable->setItem(row, 0, new QTableWidgetItem(log.ts));
    m_logTable->setItem(row, 1, new QTableWidgetItem(channelKeyToString(log.channel)));
    m_logTable->setItem(row, 2, new QTableWidgetItem(directionToString(log.direction).toUpper()));
    m_logTable->setItem(row, 3, new QTableWidgetItem(log.payload));
    m_logTable->setItem(row, 4, new QTableWidgetItem(log.decoded));
  }
  m_logTable->resizeRowsToContents();
}

void MainWindow::refreshMetrics() {
  int bytes = 0;
  int rx = 0;
  int tx = 0;
  int online = 0;
  for (bool value : m_connected) {
    if (value) ++online;
  }
  for (const LogEntry &log : m_logs) {
    bytes += log.bytes;
    if (log.direction == Direction::Rx) ++rx;
    if (log.direction == Direction::Tx) ++tx;
  }
  m_onlineMetric->setText(metricText(QStringLiteral("在线通道"), QString("%1 / %2").arg(online).arg(m_channels.size() - 1)));
  m_rxMetric->setText(metricText(QStringLiteral("RX 帧"), QString::number(rx)));
  m_txMetric->setText(metricText(QStringLiteral("TX 帧"), QString::number(tx)));
  m_bytesMetric->setText(metricText(QStringLiteral("累计字节"), QString::number(bytes)));
}

void MainWindow::refreshChart() {
#ifdef EBED_HAS_QT_CHARTS
  m_voltageSeries->clear();
  m_currentSeries->clear();
  m_temperatureSeries->clear();
  m_rpmSeries->clear();
  for (int index = 0; index < m_chartPoints.size(); ++index) {
    const ChartPoint &point = m_chartPoints.at(index);
    m_voltageSeries->append(index, point.voltage);
    m_currentSeries->append(index, point.current);
    m_temperatureSeries->append(index, point.temperature);
    m_rpmSeries->append(index, point.rpm);
  }
  auto *chart = m_chartView->chart();
  if (!chart->axes(Qt::Horizontal).isEmpty()) {
    if (auto *axis = qobject_cast<QtCharts::QValueAxis *>(chart->axes(Qt::Horizontal).first())) {
      axis->setRange(0, qMax(10, m_chartPoints.size()));
    }
  }
  if (!chart->axes(Qt::Vertical).isEmpty()) {
    if (auto *axis = qobject_cast<QtCharts::QValueAxis *>(chart->axes(Qt::Vertical).first())) {
      axis->setRange(0, 2000);
    }
  }
#else
  const ChartPoint point = m_chartPoints.isEmpty() ? ChartPoint{} : m_chartPoints.last();
  m_chartFallbackView->setPlainText(
    QStringLiteral("当前样本数：%1\n最新时间：%2\n电压：%3 V\n电流：%4 A\n温度：%5 C\n转速：%6 rpm")
      .arg(m_chartPoints.size())
      .arg(point.time)
      .arg(point.voltage, 0, 'f', 2)
      .arg(point.current, 0, 'f', 2)
      .arg(point.temperature, 0, 'f', 1)
      .arg(point.rpm, 0, 'f', 0)
  );
#endif
}

void MainWindow::refreshScriptEditor() {
  const int index = m_scriptList->currentRow();
  const bool valid = index >= 0 && index < m_scripts.size();
  m_scriptNameEdit->setEnabled(valid);
  m_scriptChannelCombo->setEnabled(valid);
  m_scriptIntervalSpin->setEnabled(valid);
  m_scriptLoopCheck->setEnabled(valid);
  m_scriptLinesEdit->setEnabled(valid);
  m_scriptToggleButton->setEnabled(valid);
  if (!valid) {
    m_scriptNameEdit->clear();
    m_scriptLinesEdit->clear();
    m_scriptToggleButton->setText(QStringLiteral("运行"));
    return;
  }

  QSignalBlocker b1(m_scriptNameEdit);
  QSignalBlocker b2(m_scriptChannelCombo);
  QSignalBlocker b3(m_scriptIntervalSpin);
  QSignalBlocker b4(m_scriptLoopCheck);
  QSignalBlocker b5(m_scriptLinesEdit);

  const AutoScript &script = m_scripts.at(index);
  m_scriptNameEdit->setText(script.name);
  m_scriptChannelCombo->setCurrentIndex(m_scriptChannelCombo->findData(static_cast<int>(script.channel)));
  m_scriptIntervalSpin->setValue(script.intervalMs);
  m_scriptLoopCheck->setChecked(script.loop);
  m_scriptLinesEdit->setPlainText(script.lines);
  m_scriptToggleButton->setText(m_scriptTimers.contains(script.id) ? QStringLiteral("停止") : QStringLiteral("运行"));
}

void MainWindow::refreshProtocolViews() {
  ModbusBuilderState state;
  state.variant = m_modbusVariantCombo->currentData().toString();
  state.unitId = m_modbusUnitIdSpin->value();
  state.functionCode = m_modbusFunctionCombo->currentData().toInt();
  state.address = m_modbusAddressSpin->value();
  state.quantity = m_modbusQuantitySpin->value();
  state.value = m_modbusValueSpin->value();
  state.transactionId = m_modbusTransactionIdSpin->value();
  m_modbusFrameEdit->setText(buildModbusFrame(state));
  m_modbusDecodeLabel->setText(decodeModbusFrame(m_modbusDecodeEdit->toPlainText()));

  bool ok = false;
  const CanFrame frame = parseCanFrameText(m_canInputEdit->text(), &ok);
  if (!ok) {
    m_canFrameLabel->setText(QStringLiteral("CAN 帧格式无效"));
    return;
  }
  const QVector<DecodedCanSignal> decoded = decodeCanSignals(frame, m_canSignals);
  QStringList lines;
  lines.append(QString("ID: %1").arg(frame.id));
  lines.append(QString("DLC: %1").arg(frame.dlc));
  lines.append(QString("DATA: %1").arg(frame.data));
  for (const DecodedCanSignal &signal : decoded) {
    lines.append(QString("%1 = %2 %3 (raw %4)").arg(signal.name).arg(signal.value, 0, 'f', 2).arg(signal.unit).arg(signal.raw));
  }
  m_canFrameLabel->setText(lines.join('\n'));
}

void MainWindow::refreshReportSummary() {
  int rx = 0;
  int tx = 0;
  for (const LogEntry &log : m_logs) {
    if (log.direction == Direction::Rx) ++rx;
    if (log.direction == Direction::Tx) ++tx;
  }
  m_reportSummaryLabel->setText(
    QStringLiteral("日志条数：%1\n图表点：%2\nRX：%3\nTX：%4\n导出内容包含当前日志、统计摘要和最近图表样本。")
      .arg(m_logs.size())
      .arg(m_chartPoints.size())
      .arg(rx)
      .arg(tx)
  );
}

void MainWindow::refreshConnectionForm() {
  int index = 0;
  for (int i = 0; i < m_channels.size(); ++i) {
    if (m_channels.at(i).key == m_activeChannel) {
      index = i;
      break;
    }
  }
  m_connectionStack->setCurrentIndex(index);
}

void MainWindow::selectChannel(ChannelKey channel) {
  m_activeChannel = channel;
  refreshAllViews();
}

void MainWindow::setConnected(ChannelKey channel, bool connected) {
  if (channel == ChannelKey::Can) {
    return;
  }
  m_connected[static_cast<int>(channel)] = connected;
  refreshHeader();
  refreshMetrics();
}

bool MainWindow::isConnected(ChannelKey channel) const {
  return m_connected.value(static_cast<int>(channel), false);
}

void MainWindow::addLog(ChannelKey channel, Direction direction, PayloadFormat format, const QString &payload, const QString &meta, const QString &decodedOverride) {
  LogEntry entry;
  entry.id = QString("%1-%2")
    .arg(QDateTime::currentMSecsSinceEpoch())
    .arg(QRandomGenerator::global()->generate());
  entry.ts = nowStamp();
  entry.channel = channel;
  entry.direction = direction;
  entry.format = format;
  entry.payload = payload;
  entry.bytes = byteLength(payload, format);
  entry.decoded = decodedOverride.isEmpty() ? decodePayload(channel, payload, format) : decodedOverride;
  entry.meta = meta;

  if (direction == Direction::Rx) {
    const ChartPoint previous = m_chartPoints.isEmpty() ? ChartPoint{0, nowStamp(), 3.3, 0.42, 35.0, 1200.0} : m_chartPoints.last();
    m_chartPoints.append(extractMetrics(entry, previous));
    if (m_chartPoints.size() > 180) {
      m_chartPoints.remove(0, m_chartPoints.size() - 180);
    }
  }

  if (m_recording || direction == Direction::Sys) {
    m_logs.prepend(entry);
    if (m_logs.size() > 1500) {
      m_logs.resize(1500);
    }
  }

  refreshMetrics();
  refreshTerminal();
  refreshLogTable();
  refreshChart();
  refreshReportSummary();
}

void MainWindow::sendPayload(ChannelKey channel, const QString &payload, PayloadFormat format, const QVariantMap &meta) {
  const QString cleanPayload = format == PayloadFormat::Hex ? normalizeHex(payload) : payload;
  if (cleanPayload.trimmed().isEmpty()) {
    return;
  }

  addLog(channel, Direction::Tx, format, cleanPayload, meta.value("topic").toString());

  if (channel == ChannelKey::Can) {
    bool ok = false;
    const CanFrame frame = parseCanFrameText(cleanPayload, &ok);
    if (!ok) {
      addLog(channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("CAN 帧格式无效"));
      return;
    }
    const QVector<DecodedCanSignal> decodedSignals = decodeCanSignals(frame, m_canSignals);
    QStringList decodedText;
    for (const DecodedCanSignal &signal : decodedSignals) {
      decodedText.append(QString("%1=%2%3").arg(signal.name).arg(signal.value, 0, 'f', 2).arg(signal.unit));
    }
    QString compactData = frame.data;
    compactData.remove(' ');
    addLog(channel, Direction::Rx, PayloadFormat::Text, QString("%1#%2").arg(frame.id, compactData), {}, decodedText.join(", "));
    return;
  }

  AbstractTransportClient *client = m_clients.value(static_cast<int>(channel), nullptr);
  if (!client || !isConnected(channel)) {
    addLog(channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("连接未打开"));
    return;
  }

  client->sendPayload(cleanPayload, format, meta);
}

void MainWindow::connectChannel(ChannelKey channel) {
  if (channel == ChannelKey::Can) {
    return;
  }

  const QString modbusVariant = channel == ChannelKey::Modbus && m_modbusVariantCombo
    ? m_modbusVariantCombo->currentData().toString()
    : QString();
  const QString unsupportedMessage = unsupportedChannelMessage(channel, modbusVariant);
  if (!unsupportedMessage.isEmpty() && !isConnected(channel)) {
    addLog(channel, Direction::Sys, PayloadFormat::Event, unsupportedMessage);
    return;
  }

  if (isConnected(channel)) {
    closeChannel(channel);
    return;
  }

  closeChannel(channel, false);
  AbstractTransportClient *client = buildClient(channel);
  if (!client) {
    addLog(channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("当前构建不支持该通道。"));
    return;
  }

  connect(client, &AbstractTransportClient::dataReceived, this, [this, channel](const QString &payload, PayloadFormat format, const QString &meta) {
    addLog(channel, Direction::Rx, format, payload, meta);
  });

  connect(client, &AbstractTransportClient::statusChanged, this, [this, client, channel](const QString &message, const QString &level) {
    addLog(channel, Direction::Sys, PayloadFormat::Event, message, level);
    if (level == "ok") {
      setConnected(channel, true);
    } else if (level == "warn" || (level == "error" && !client->isOpen())) {
      setConnected(channel, false);
    }
  });

  m_clients[static_cast<int>(channel)] = client;
  client->open(configForChannel(channel));
}

void MainWindow::closeChannel(ChannelKey channel, bool logClose) {
  AbstractTransportClient *client = m_clients.take(static_cast<int>(channel));
  if (client) {
    client->closeClient();
    client->deleteLater();
  }
  setConnected(channel, false);
  if (logClose) {
    addLog(channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("连接已关闭"));
  }
}

void MainWindow::saveTextFile(const QString &title, const QString &defaultPath, const QString &content, const QString &filter) {
  const QString path = QFileDialog::getSaveFileName(this, title, defaultPath, filter);
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"), file.errorString());
    return;
  }

  file.write(content.toUtf8());
  file.close();
  addLog(m_activeChannel, Direction::Sys, PayloadFormat::Event, QStringLiteral("文件已保存：%1").arg(path), QStringLiteral("export"));
}

AbstractTransportClient *MainWindow::buildClient(ChannelKey channel) {
  switch (channel) {
    case ChannelKey::Serial:
      return new SerialTransportClient(channel, this);
    case ChannelKey::Tcp:
      return new TcpTransportClient(channel, this);
    case ChannelKey::Udp:
      return new UdpTransportClient(channel, this);
    case ChannelKey::Mqtt:
      return new MqttTransportClient(channel, this);
    case ChannelKey::Modbus:
      if (m_modbusVariantCombo->currentData().toString() == "tcp") {
        return new TcpTransportClient(channel, this);
      }
      return new SerialTransportClient(channel, this);
    case ChannelKey::Can:
      return nullptr;
  }
  return nullptr;
}

QVariantMap MainWindow::configForChannel(ChannelKey channel) const {
  switch (channel) {
    case ChannelKey::Serial:
      return {
        {"path", m_serialPathCombo->currentText()},
        {"baudRate", m_serialBaudSpin->value()},
        {"dataBits", m_serialDataBitsSpin->value()},
        {"stopBits", m_serialStopBitsSpin->value()},
        {"parity", m_serialParityCombo->currentText()}
      };
    case ChannelKey::Tcp:
      return {
        {"host", m_tcpHostEdit->text()},
        {"port", m_tcpPortSpin->value()}
      };
    case ChannelKey::Udp:
      return {
        {"localPort", m_udpLocalPortSpin->value()},
        {"remoteHost", m_udpRemoteHostEdit->text()},
        {"remotePort", m_udpRemotePortSpin->value()}
      };
    case ChannelKey::Mqtt:
      return {
        {"url", m_mqttUrlEdit->text()},
        {"clientId", m_mqttClientIdEdit->text()},
        {"subscribeTopic", m_mqttSubscribeEdit->text()},
        {"publishTopic", m_mqttPublishEdit->text()},
        {"username", m_mqttUsernameEdit->text()},
        {"password", m_mqttPasswordEdit->text()}
      };
    case ChannelKey::Modbus:
      if (m_modbusVariantCombo->currentData().toString() == "tcp") {
        return {
          {"host", m_modbusTcpHostEdit->text()},
          {"port", m_modbusTcpPortSpin->value()}
        };
      }
      return {
        {"path", m_modbusSerialPathCombo->currentText()},
        {"baudRate", m_modbusBaudSpin->value()},
        {"dataBits", 8},
        {"stopBits", 1},
        {"parity", "none"}
      };
    case ChannelKey::Can:
      return {};
  }
  return {};
}

PayloadFormat MainWindow::currentTxFormat() const {
  return static_cast<PayloadFormat>(m_txFormatCombo->currentData().toInt());
}

QStringList MainWindow::activeScriptLines() const {
  const int index = m_scriptList->currentRow();
  if (index < 0 || index >= m_scripts.size()) {
    return {};
  }
  QStringList lines = m_scripts.at(index).lines.split(QRegularExpression("\\r?\\n"), Qt::SkipEmptyParts);
  QStringList filtered;
  for (const QString &line : lines) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
      filtered.append(trimmed);
    }
  }
  return filtered;
}

void MainWindow::stopScript(const QString &scriptId, bool logStop) {
  QTimer *timer = m_scriptTimers.take(scriptId);
  if (timer) {
    timer->stop();
    timer->deleteLater();
  }

  for (const AutoScript &script : m_scripts) {
    if (script.id == scriptId && logStop) {
      addLog(script.channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("脚本停止：%1").arg(script.name));
      break;
    }
  }
}

void MainWindow::onChannelSelectionChanged() {
  auto *item = m_channelList->currentItem();
  if (!item) {
    return;
  }
  selectChannel(static_cast<ChannelKey>(item->data(Qt::UserRole).toInt()));
}

void MainWindow::onConnectClicked() {
  connectChannel(m_activeChannel);
}

void MainWindow::onSendClicked() {
  QVariantMap meta;
  if (m_activeChannel == ChannelKey::Mqtt) {
    meta.insert("topic", m_mqttPublishEdit->text());
  }
  sendPayload(m_activeChannel, m_txEditor->toPlainText(), currentTxFormat(), meta);
}

void MainWindow::onRecordingClicked() {
  m_recording = !m_recording;
  refreshHeader();
}

void MainWindow::onClearLogsClicked() {
  m_logs.clear();
  m_chartPoints.clear();
  m_chartPoints.append(ChartPoint{0, nowStamp(), 3.3, 0.42, 35.0, 1200.0});
  refreshAllViews();
}

void MainWindow::onSearchChanged() {
  refreshLogTable();
}

void MainWindow::onRunTemplate() {
  auto *item = m_templateTree->currentItem();
  if (!item || item->childCount() > 0) {
    return;
  }

  const ChannelKey channel = static_cast<ChannelKey>(item->data(0, Qt::UserRole).toInt());
  const QString payload = item->data(0, Qt::UserRole + 1).toString();
  const PayloadFormat format = static_cast<PayloadFormat>(item->data(0, Qt::UserRole + 2).toInt());
  selectChannel(channel);
  m_txEditor->setPlainText(payload);
  m_txFormatCombo->setCurrentIndex(m_txFormatCombo->findData(static_cast<int>(format)));
  QVariantMap meta;
  if (channel == ChannelKey::Mqtt) {
    meta.insert("topic", m_mqttPublishEdit->text());
  }
  sendPayload(channel, payload, format, meta);
}

void MainWindow::onTemplateActivated(QTreeWidgetItem *item, int) {
  m_templateTree->setCurrentItem(item);
  onRunTemplate();
}

void MainWindow::onScriptSelectionChanged() {
  refreshScriptEditor();
}

void MainWindow::onToggleScript() {
  const int index = m_scriptList->currentRow();
  if (index < 0 || index >= m_scripts.size()) {
    return;
  }

  const AutoScript script = m_scripts.at(index);
  if (m_scriptTimers.contains(script.id)) {
    stopScript(script.id);
    refreshScriptEditor();
    populateScripts();
    return;
  }

  const QStringList lines = activeScriptLines();
  if (lines.isEmpty()) {
    return;
  }

  auto *timer = new QTimer(this);
  timer->setInterval(qMax(100, script.intervalMs));
  m_scriptTimers.insert(script.id, timer);

  auto runner = [this, script, lines, timer, index = 0]() mutable {
    const QString line = lines.at(index);
    QVariantMap meta;
    if (script.channel == ChannelKey::Mqtt) {
      meta.insert("topic", m_mqttPublishEdit->text());
    }
    sendPayload(script.channel, line, inferFormat(line), meta);
    ++index;
    if (index >= lines.size()) {
      if (script.loop) {
        index = 0;
      } else {
        stopScript(script.id);
        populateScripts();
        refreshScriptEditor();
      }
    }
  };

  connect(timer, &QTimer::timeout, this, runner);
  addLog(script.channel, Direction::Sys, PayloadFormat::Event, QStringLiteral("脚本启动：%1").arg(script.name));
  timer->start();
  runner();
  populateScripts();
  refreshScriptEditor();
}

void MainWindow::onAddScript() {
  const QString id = QString("script-%1").arg(QDateTime::currentMSecsSinceEpoch());
  m_scripts.append(AutoScript{id, QStringLiteral("新脚本"), m_activeChannel, 1000, true, "PING"});
  populateScripts();
  m_scriptList->setCurrentRow(m_scripts.size() - 1);
}

void MainWindow::onScriptEdited() {
  const int index = m_scriptList->currentRow();
  if (index < 0 || index >= m_scripts.size()) {
    return;
  }

  AutoScript &script = m_scripts[index];
  script.name = m_scriptNameEdit->text();
  script.channel = static_cast<ChannelKey>(m_scriptChannelCombo->currentData().toInt());
  script.intervalMs = m_scriptIntervalSpin->value();
  script.loop = m_scriptLoopCheck->isChecked();
  script.lines = m_scriptLinesEdit->toPlainText();
  populateScripts();
  m_scriptList->setCurrentRow(index);
}

void MainWindow::onSendGeneratedModbus() {
  m_txEditor->setPlainText(m_modbusFrameEdit->text());
  m_txFormatCombo->setCurrentIndex(m_txFormatCombo->findData(static_cast<int>(PayloadFormat::Hex)));
  sendPayload(ChannelKey::Modbus, m_modbusFrameEdit->text(), PayloadFormat::Hex);
}

void MainWindow::onParseCanFrame() {
  sendPayload(ChannelKey::Can, m_canInputEdit->text(), PayloadFormat::Text);
}

void MainWindow::onExportCsv() {
  saveTextFile(QStringLiteral("导出 CSV"), "ebed-debug-log.csv", logsToCsv(m_logs), "CSV (*.csv)");
}

void MainWindow::onExportJson() {
  QJsonArray logsArray;
  for (const LogEntry &log : m_logs) {
    QJsonObject object;
    object["time"] = log.ts;
    object["channel"] = channelKeyToString(log.channel);
    object["direction"] = directionToString(log.direction);
    object["format"] = payloadFormatToString(log.format);
    object["bytes"] = log.bytes;
    object["payload"] = log.payload;
    object["decoded"] = log.decoded;
    object["meta"] = log.meta;
    logsArray.append(object);
  }

  QJsonArray chartArray;
  for (const ChartPoint &point : m_chartPoints) {
    QJsonObject object;
    object["id"] = point.id;
    object["time"] = point.time;
    object["voltage"] = point.voltage;
    object["current"] = point.current;
    object["temperature"] = point.temperature;
    object["rpm"] = point.rpm;
    chartArray.append(object);
  }

  QJsonObject root;
  root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  root["logs"] = logsArray;
  root["chartPoints"] = chartArray;
  saveTextFile(QStringLiteral("导出 JSON"), "ebed-debug-log.json", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)), "JSON (*.json)");
}

void MainWindow::onExportReport() {
  saveTextFile(QStringLiteral("导出 HTML 报告"), "ebed-debug-report.html", buildHtmlReport(m_logs, m_chartPoints), "HTML (*.html)");
}
