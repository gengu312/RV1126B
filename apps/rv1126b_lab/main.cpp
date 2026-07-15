#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDataStream>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QSlider>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTouchEvent>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool writeText(const QString &path, const QByteArray &value)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(value) == value.size();
}

QString oneLine(const QString &path, const QString &fallback = QString::fromUtf8("未知"))
{
    const QString value = readText(path).trimmed();
    return value.isEmpty() ? fallback : value;
}

QString formatBytes(quint64 bytes)
{
    const double gib = bytes / 1024.0 / 1024.0 / 1024.0;
    if (gib >= 1.0)
        return QString::fromUtf8("%1 GB").arg(gib, 0, 'f', 1);
    return QString::fromUtf8("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 0);
}

QFrame *makePanel(QWidget *parent = nullptr)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    return panel;
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

struct CpuCounters {
    quint64 total = 0;
    quint64 idle = 0;
    bool valid = false;
};

CpuCounters readCpuCounters()
{
    const QString firstLine = readText(QStringLiteral("/proc/stat")).section('\n', 0, 0);
    const QStringList parts = firstLine.simplified().split(' ');
    if (parts.size() < 5 || parts.first() != QStringLiteral("cpu"))
        return {};

    CpuCounters result;
    QVector<quint64> values;
    for (int i = 1; i < parts.size(); ++i) {
        bool ok = false;
        const quint64 value = parts.at(i).toULongLong(&ok);
        if (!ok)
            return {};
        values.append(value);
        result.total += value;
    }
    result.idle = values.value(3) + values.value(4);
    result.valid = true;
    return result;
}

struct SystemSnapshot {
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double storagePercent = 0.0;
    double temperatureC = 0.0;
    quint64 memoryTotal = 0;
    quint64 memoryAvailable = 0;
    quint64 storageTotal = 0;
    quint64 storageAvailable = 0;
    qint64 uptimeSeconds = 0;
    QString ethState;
    QString wifiState;
    bool cameraReady = false;
    bool npuReady = false;
};

quint64 memInfoValue(const QString &name)
{
    const QStringList lines = readText(QStringLiteral("/proc/meminfo")).split('\n');
    for (const QString &line : lines) {
        if (!line.startsWith(name + ':'))
            continue;
        const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)"))
            .match(line);
        if (match.hasMatch())
            return match.captured(1).toULongLong() * 1024ULL;
    }
    return 0;
}

SystemSnapshot sampleSystem(const CpuCounters &previous, CpuCounters *current)
{
    SystemSnapshot snapshot;
    *current = readCpuCounters();
    if (previous.valid && current->valid && current->total > previous.total) {
        const quint64 totalDelta = current->total - previous.total;
        const quint64 idleDelta = current->idle - previous.idle;
        snapshot.cpuPercent = 100.0 * (totalDelta - std::min(totalDelta, idleDelta)) / totalDelta;
    }

    snapshot.memoryTotal = memInfoValue(QStringLiteral("MemTotal"));
    snapshot.memoryAvailable = memInfoValue(QStringLiteral("MemAvailable"));
    if (snapshot.memoryTotal > 0) {
        snapshot.memoryPercent = 100.0
            * (snapshot.memoryTotal - std::min(snapshot.memoryTotal, snapshot.memoryAvailable))
            / snapshot.memoryTotal;
    }

    struct statvfs storage {};
    if (statvfs("/", &storage) == 0) {
        snapshot.storageTotal = static_cast<quint64>(storage.f_blocks) * storage.f_frsize;
        snapshot.storageAvailable = static_cast<quint64>(storage.f_bavail) * storage.f_frsize;
        if (snapshot.storageTotal > 0) {
            snapshot.storagePercent = 100.0
                * (snapshot.storageTotal - std::min(snapshot.storageTotal, snapshot.storageAvailable))
                / snapshot.storageTotal;
        }
    }

    bool temperatureOk = false;
    const double rawTemperature = oneLine(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"),
        QStringLiteral("0")).toDouble(&temperatureOk);
    if (temperatureOk)
        snapshot.temperatureC = rawTemperature > 1000.0 ? rawTemperature / 1000.0 : rawTemperature;

    bool uptimeOk = false;
    snapshot.uptimeSeconds = readText(QStringLiteral("/proc/uptime"))
        .section(' ', 0, 0).toDouble(&uptimeOk);
    if (!uptimeOk)
        snapshot.uptimeSeconds = 0;

    snapshot.ethState = oneLine(QStringLiteral("/sys/class/net/eth0/operstate"),
        QString::fromUtf8("无设备"));
    snapshot.wifiState = oneLine(QStringLiteral("/sys/class/net/wlan0/operstate"),
        QString::fromUtf8("无设备"));
    snapshot.cameraReady = QFile::exists(QStringLiteral("/dev/video-camera0"));
    snapshot.npuReady = QFile::exists(QStringLiteral("/dev/rknpu"));
    return snapshot;
}

QString uptimeText(qint64 seconds)
{
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (days > 0)
        return QString::fromUtf8("%1天 %2小时").arg(days).arg(hours);
    return QString::fromUtf8("%1小时 %2分钟").arg(hours).arg(minutes);
}

class LineChart final : public QWidget
{
public:
    explicit LineChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(250);
    }

    void setValues(const QVector<int> &values)
    {
        m_values = values;
        update();
    }

    void setThreshold(int value)
    {
        m_threshold = value;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#0b1220")));

        const QRectF plot = rect().adjusted(18, 18, -18, -24);
        painter.setPen(QPen(QColor(QStringLiteral("#263449")), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i <= 6; ++i) {
            const qreal x = plot.left() + plot.width() * i / 6.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        if (m_threshold >= 0) {
            const qreal y = plot.bottom() - plot.height() * m_threshold / 8191.0;
            painter.setPen(QPen(QColor(QStringLiteral("#f59e0b")), 2, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        if (m_values.size() < 2)
            return;

        QPainterPath path;
        for (int i = 0; i < m_values.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / std::max(1, m_values.size() - 1);
            const qreal y = plot.bottom() - plot.height() * qBound(0, m_values.at(i), 8191) / 8191.0;
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        painter.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 4,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

private:
    QVector<int> m_values;
    int m_threshold = -1;
};

class HomePage final : public QWidget
{
public:
    explicit HomePage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 24, 28, 26);
        root->setSpacing(22);

        auto *welcome = new QLabel(QString::fromUtf8("RV1126B 触摸软件实验台"), this);
        welcome->setObjectName(QStringLiteral("heroTitle"));
        welcome->setAlignment(Qt::AlignCenter);
        root->addWidget(welcome);

        auto *subtitle = new QLabel(
            QString::fromUtf8("一个入口完成板卡状态、IO/ADC 和触摸实验"), this);
        subtitle->setObjectName(QStringLiteral("muted"));
        subtitle->setAlignment(Qt::AlignCenter);
        root->addWidget(subtitle);

        auto *metrics = new QHBoxLayout;
        metrics->setSpacing(14);
        metrics->addWidget(metricCard(QString::fromUtf8("CPU"), m_cpuLabel));
        metrics->addWidget(metricCard(QString::fromUtf8("温度"), m_temperatureLabel));
        metrics->addWidget(metricCard(QString::fromUtf8("ADC"), m_adcLabel));
        root->addLayout(metrics);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(18);
        addModuleButton(grid, QString::fromUtf8("IO / ADC"),
            QString::fromUtf8("旋钮、曲线、阈值、LED"), 0, 0, 1);
        addModuleButton(grid, QString::fromUtf8("五点触摸"),
            QString::fromUtf8("触点、坐标、轨迹"), 0, 1, 2);
        addModuleButton(grid, QString::fromUtf8("系统监控"),
            QString::fromUtf8("CPU、内存、温度、设备"), 1, 0, 3);
        addModuleButton(grid, QString::fromUtf8("音频实验"),
            QString::fromUtf8("录音、波形、音量、回放"), 1, 1, 4);
        addModuleButton(grid, QString::fromUtf8("摄像头 / AI"),
            QString::fromUtf8("IMX415、NPU、YOLO"), 2, 0, 5);
        addModuleButton(grid, QString::fromUtf8("总线 / 按键"),
            QString::fromUtf8("I2C、UART、CAN、PWM、输入"), 2, 1, 6);
        addModuleButton(grid, QString::fromUtf8("实验说明"),
            QString::fromUtf8("当前阶段的演示与验收内容"), 3, 0, 7, 1, 2);
        root->addLayout(grid, 1);

        auto *status = makePanel(this);
        auto *statusLayout = new QHBoxLayout(status);
        statusLayout->setContentsMargins(20, 14, 20, 14);
        m_deviceLabel = new QLabel(status);
        m_deviceLabel->setObjectName(QStringLiteral("statusText"));
        m_deviceLabel->setWordWrap(true);
        statusLayout->addWidget(m_deviceLabel);
        root->addWidget(status);
    }

    std::function<void(int)> onNavigate;

    void updateSystem(const SystemSnapshot &snapshot)
    {
        m_cpuLabel->setText(QString::fromUtf8("%1%").arg(snapshot.cpuPercent, 0, 'f', 0));
        m_temperatureLabel->setText(QString::fromUtf8("%1°C").arg(snapshot.temperatureC, 0, 'f', 1));
        m_deviceLabel->setText(QString::fromUtf8("摄像头 %1    NPU %2    网口 %3    Wi-Fi %4")
            .arg(snapshot.cameraReady ? QString::fromUtf8("已识别") : QString::fromUtf8("未识别"))
            .arg(snapshot.npuReady ? QString::fromUtf8("已识别") : QString::fromUtf8("未识别"))
            .arg(snapshot.ethState)
            .arg(snapshot.wifiState));
    }

    void updateAdc(int raw)
    {
        m_adcLabel->setText(QString::number(raw));
    }

private:
    QWidget *metricCard(const QString &title, QLabel *&value)
    {
        auto *panel = makePanel(this);
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12, 14, 12, 14);
        layout->setSpacing(4);
        auto *titleLabel = new QLabel(title, panel);
        titleLabel->setObjectName(QStringLiteral("metricTitle"));
        titleLabel->setAlignment(Qt::AlignCenter);
        value = new QLabel(QStringLiteral("--"), panel);
        value->setObjectName(QStringLiteral("metricValue"));
        value->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);
        layout->addWidget(value);
        return panel;
    }

    void addModuleButton(QGridLayout *layout, const QString &title, const QString &detail,
        int row, int column, int page, int rowSpan = 1, int columnSpan = 1)
    {
        auto *button = new QPushButton(QString::fromUtf8("%1\n%2").arg(title, detail), this);
        button->setObjectName(QStringLiteral("moduleButton"));
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        connect(button, &QPushButton::clicked, this, [this, page]() {
            if (onNavigate)
                onNavigate(page);
        });
        layout->addWidget(button, row, column, rowSpan, columnSpan);
    }

    QLabel *m_cpuLabel = nullptr;
    QLabel *m_temperatureLabel = nullptr;
    QLabel *m_adcLabel = nullptr;
    QLabel *m_deviceLabel = nullptr;
};

class IoPage final : public QWidget
{
public:
    explicit IoPage(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_ledRoot(qEnvironmentVariable("RV1126BLAB_LED_PATH", "/sys/class/leds/work"))
        , m_iioRoot(qEnvironmentVariable("RV1126BLAB_IIO_DEVICE",
              "/sys/bus/iio/devices/iio:device0"))
        , m_readOnly(qEnvironmentVariableIsSet("RV1126BLAB_READ_ONLY"))
    {
        setupUi();
        inspectHardware();
        connect(&m_adcTimer, &QTimer::timeout, this, [this]() { updateAdc(); });
        connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
            if (!setLed(!m_ledOn))
                m_blinkTimer.stop();
        });
        updateAdc();
        m_adcTimer.start(100);
    }

    ~IoPage() override
    {
        restoreLed();
    }

    std::function<void(int)> onAdcChanged;

    void leavePage()
    {
        if (m_mode != Mode::Observe)
            setMode(Mode::Observe);
    }

private:
    enum class Mode {
        Observe,
        Off,
        On,
        Threshold,
        AdcBlink
    };

    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 18, 28, 24);
        root->setSpacing(16);

        auto *topPanel = makePanel(this);
        auto *top = new QVBoxLayout(topPanel);
        top->setContentsMargins(20, 16, 20, 16);
        top->setSpacing(8);
        m_valueLabel = new QLabel(topPanel);
        m_valueLabel->setObjectName(QStringLiteral("adcValue"));
        m_valueLabel->setAlignment(Qt::AlignCenter);
        m_statisticsLabel = new QLabel(topPanel);
        m_statisticsLabel->setObjectName(QStringLiteral("muted"));
        m_statisticsLabel->setAlignment(Qt::AlignCenter);
        top->addWidget(m_valueLabel);
        top->addWidget(m_statisticsLabel);
        root->addWidget(topPanel);

        m_chart = new LineChart(this);
        root->addWidget(m_chart, 1);

        m_thresholdLabel = new QLabel(this);
        m_thresholdLabel->setObjectName(QStringLiteral("statusText"));
        root->addWidget(m_thresholdLabel);

        m_thresholdSlider = new QSlider(Qt::Horizontal, this);
        m_thresholdSlider->setRange(0, 8191);
        m_thresholdSlider->setValue(4096);
        m_thresholdSlider->setPageStep(256);
        connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
            m_chart->setThreshold(value);
            updateThresholdLabel();
            if (m_mode == Mode::Threshold)
                applyThresholdMode();
        });
        root->addWidget(m_thresholdSlider);

        auto *buttons = new QGridLayout;
        buttons->setHorizontalSpacing(12);
        buttons->setVerticalSpacing(12);
        addModeButton(buttons, QString::fromUtf8("只看数据"), Mode::Observe, 0, 0);
        addModeButton(buttons, QString::fromUtf8("LED 熄灭"), Mode::Off, 0, 1);
        addModeButton(buttons, QString::fromUtf8("LED 常亮"), Mode::On, 0, 2);
        addModeButton(buttons, QString::fromUtf8("阈值联动"), Mode::Threshold, 1, 0);
        addModeButton(buttons, QString::fromUtf8("ADC 调速闪烁"), Mode::AdcBlink, 1, 1, 1, 2);
        root->addLayout(buttons);

        auto *bottom = new QHBoxLayout;
        m_modeLabel = new QLabel(QString::fromUtf8("模式：只看数据，不控制 LED"), this);
        m_modeLabel->setObjectName(QStringLiteral("statusText"));
        m_modeLabel->setWordWrap(true);
        auto *resetButton = new QPushButton(QString::fromUtf8("清空统计"), this);
        resetButton->setObjectName(QStringLiteral("smallButton"));
        connect(resetButton, &QPushButton::clicked, this, [this]() { resetStatistics(); });
        bottom->addWidget(m_modeLabel, 1);
        bottom->addWidget(resetButton);
        root->addLayout(bottom);

        m_errorLabel = new QLabel(this);
        m_errorLabel->setObjectName(QStringLiteral("error"));
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        root->addWidget(m_errorLabel);

        m_chart->setThreshold(m_thresholdSlider->value());
        updateThresholdLabel();
    }

    void inspectHardware()
    {
        m_hardwareReady = QFile::exists(m_iioRoot + QStringLiteral("/in_voltage4_raw"));
        m_ledReady = QFile::exists(m_ledRoot + QStringLiteral("/trigger"))
            && QFile::exists(m_ledRoot + QStringLiteral("/brightness"));

        bool scaleOk = false;
        const double scale = readText(m_iioRoot + QStringLiteral("/in_voltage_scale"))
            .trimmed().toDouble(&scaleOk);
        if (scaleOk)
            m_scaleMillivolts = scale;

        if (!m_hardwareReady)
            m_errorLabel->setText(QString::fromUtf8("未找到 SARADC 通道 4。"));
        else if (!m_ledReady)
            m_errorLabel->setText(QString::fromUtf8("ADC 可读，但未找到板载工作灯。"));
        else if (m_readOnly)
            m_errorLabel->setText(QString::fromUtf8("只读测试模式：不会写入 LED。"));
    }

    void addModeButton(QGridLayout *layout, const QString &text, Mode mode,
        int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QStringLiteral("actionButton"));
        button->setProperty("active", mode == Mode::Observe);
        connect(button, &QPushButton::clicked, this, [this, mode]() { setMode(mode); });
        layout->addWidget(button, row, column, rowSpan, columnSpan);
        m_modeButtons.append(qMakePair(mode, button));
    }

    void setMode(Mode mode)
    {
        m_blinkTimer.stop();
        if (mode != Mode::Observe && (!m_ledReady || !claimLed())) {
            m_errorLabel->setText(QString::fromUtf8("无法接管板载工作灯，请检查权限。"));
            return;
        }

        m_mode = mode;
        switch (mode) {
        case Mode::Observe:
            restoreLed();
            m_modeLabel->setText(QString::fromUtf8("模式：只看数据，不控制 LED"));
            break;
        case Mode::Off:
            setLed(false);
            m_modeLabel->setText(QString::fromUtf8("模式：LED 熄灭"));
            break;
        case Mode::On:
            setLed(true);
            m_modeLabel->setText(QString::fromUtf8("模式：LED 常亮"));
            break;
        case Mode::Threshold:
            applyThresholdMode();
            break;
        case Mode::AdcBlink:
            applyBlinkSpeed();
            setLed(true);
            m_blinkTimer.start(m_blinkHalfPeriodMs);
            break;
        }
        refreshModeButtons();
    }

    bool claimLed()
    {
        if (m_ledClaimed)
            return true;

        const QString trigger = readText(m_ledRoot + QStringLiteral("/trigger"));
        const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]"))
            .match(trigger);
        m_originalTrigger = match.hasMatch() ? match.captured(1) : QStringLiteral("none");
        m_originalBrightness = readText(m_ledRoot + QStringLiteral("/brightness")).trimmed();

        if (!m_readOnly && !writeText(m_ledRoot + QStringLiteral("/trigger"), QByteArray("none\n")))
            return false;
        m_ledClaimed = true;
        return true;
    }

    void restoreLed()
    {
        m_blinkTimer.stop();
        if (!m_ledClaimed)
            return;
        if (!m_readOnly) {
            if (!m_originalTrigger.isEmpty())
                writeText(m_ledRoot + QStringLiteral("/trigger"), m_originalTrigger.toUtf8() + '\n');
            if (m_originalTrigger == QStringLiteral("none") && !m_originalBrightness.isEmpty())
                writeText(m_ledRoot + QStringLiteral("/brightness"),
                    m_originalBrightness.toUtf8() + '\n');
        }
        m_ledClaimed = false;
        m_ledOn = false;
    }

    bool setLed(bool on)
    {
        if (!m_ledReady)
            return false;
        if (!m_readOnly && !writeText(m_ledRoot + QStringLiteral("/brightness"),
                on ? QByteArray("1\n") : QByteArray("0\n")))
            return false;
        m_ledOn = on;
        return true;
    }

    void updateAdc()
    {
        bool ok = false;
        const int rawValue = readText(m_iioRoot + QStringLiteral("/in_voltage4_raw"))
            .trimmed().toInt(&ok);
        if (!ok) {
            m_errorLabel->setText(QString::fromUtf8("ADC 读取失败。"));
            return;
        }

        m_currentRaw = qBound(0, rawValue, 8191);
        m_history.append(m_currentRaw);
        if (m_history.size() > 240)
            m_history.remove(0, m_history.size() - 240);
        m_filterWindow.append(m_currentRaw);
        if (m_filterWindow.size() > 10)
            m_filterWindow.removeFirst();

        int filterSum = 0;
        for (int value : m_filterWindow)
            filterSum += value;
        const int filtered = filterSum / std::max(1, m_filterWindow.size());

        if (m_sampleCount == 0) {
            m_minRaw = m_currentRaw;
            m_maxRaw = m_currentRaw;
        }
        m_minRaw = std::min(m_minRaw, m_currentRaw);
        m_maxRaw = std::max(m_maxRaw, m_currentRaw);
        m_sumRaw += m_currentRaw;
        ++m_sampleCount;

        const double millivolts = m_currentRaw * m_scaleMillivolts;
        m_valueLabel->setText(QString::fromUtf8("raw %1    %2 mV    滤波 %3")
            .arg(m_currentRaw)
            .arg(millivolts, 0, 'f', 1)
            .arg(filtered));
        m_statisticsLabel->setText(QString::fromUtf8("最小 %1    最大 %2    平均 %3    采样 %4 次")
            .arg(m_minRaw)
            .arg(m_maxRaw)
            .arg(m_sumRaw / std::max<qint64>(1, m_sampleCount))
            .arg(m_sampleCount));
        m_chart->setValues(m_history);

        if (m_mode == Mode::Threshold)
            applyThresholdMode();
        else if (m_mode == Mode::AdcBlink)
            applyBlinkSpeed();

        if (onAdcChanged)
            onAdcChanged(m_currentRaw);
    }

    void applyThresholdMode()
    {
        const int threshold = m_thresholdSlider->value();
        const bool active = m_currentRaw >= threshold;
        setLed(active);
        m_modeLabel->setText(QString::fromUtf8("模式：阈值联动，ADC %1 %2 %3，LED %4")
            .arg(m_currentRaw)
            .arg(active ? QString::fromUtf8("≥") : QString::fromUtf8("<"))
            .arg(threshold)
            .arg(active ? QString::fromUtf8("亮") : QString::fromUtf8("灭")));
    }

    void applyBlinkSpeed()
    {
        const int newPeriod = 1200 - 1120 * m_currentRaw / 8191;
        if (std::abs(newPeriod - m_blinkHalfPeriodMs) >= 10) {
            m_blinkHalfPeriodMs = newPeriod;
            if (m_blinkTimer.isActive())
                m_blinkTimer.setInterval(m_blinkHalfPeriodMs);
        }
        m_modeLabel->setText(QString::fromUtf8("模式：ADC 调速闪烁，半周期 %1 ms")
            .arg(m_blinkHalfPeriodMs));
    }

    void updateThresholdLabel()
    {
        const int value = m_thresholdSlider->value();
        m_thresholdLabel->setText(QString::fromUtf8("触摸设置阈值：%1（%2 mV）")
            .arg(value)
            .arg(value * m_scaleMillivolts, 0, 'f', 1));
    }

    void resetStatistics()
    {
        m_history.clear();
        m_filterWindow.clear();
        m_sampleCount = 0;
        m_sumRaw = 0;
        m_minRaw = 0;
        m_maxRaw = 0;
        m_chart->setValues(m_history);
    }

    void refreshModeButtons()
    {
        for (const auto &entry : m_modeButtons) {
            entry.second->setProperty("active", entry.first == m_mode);
            entry.second->style()->unpolish(entry.second);
            entry.second->style()->polish(entry.second);
        }
    }

    QString m_ledRoot;
    QString m_iioRoot;
    QString m_originalTrigger;
    QString m_originalBrightness;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_statisticsLabel = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QSlider *m_thresholdSlider = nullptr;
    LineChart *m_chart = nullptr;
    QVector<QPair<Mode, QPushButton *>> m_modeButtons;
    QVector<int> m_history;
    QVector<int> m_filterWindow;
    QTimer m_adcTimer;
    QTimer m_blinkTimer;
    Mode m_mode = Mode::Observe;
    qint64 m_sumRaw = 0;
    qint64 m_sampleCount = 0;
    int m_minRaw = 0;
    int m_maxRaw = 0;
    int m_currentRaw = 0;
    int m_blinkHalfPeriodMs = 600;
    double m_scaleMillivolts = 0.219726562;
    bool m_hardwareReady = false;
    bool m_ledReady = false;
    bool m_ledClaimed = false;
    bool m_ledOn = false;
    bool m_readOnly = false;
};

class AudioWaveform final : public QWidget
{
public:
    explicit AudioWaveform(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(300);
    }

    void setSamples(const QVector<qint16> &samples)
    {
        m_samples = samples;
        update();
    }

    void clear()
    {
        m_samples.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#08111f")));

        const QRectF plot = rect().adjusted(18, 18, -18, -18);
        painter.setPen(QPen(QColor(QStringLiteral("#23354b")), 1));
        painter.drawLine(QPointF(plot.left(), plot.center().y()),
            QPointF(plot.right(), plot.center().y()));
        for (int i = 0; i <= 6; ++i) {
            const qreal x = plot.left() + plot.width() * i / 6.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        if (m_samples.size() < 2) {
            painter.setPen(QColor(QStringLiteral("#8fa6bf")));
            QFont hint = painter.font();
            hint.setPixelSize(25);
            painter.setFont(hint);
            painter.drawText(plot, Qt::AlignCenter,
                QString::fromUtf8("点击“开始录音”后，对着板子说话"));
            return;
        }

        QPainterPath path;
        for (int i = 0; i < m_samples.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / std::max(1, m_samples.size() - 1);
            const qreal normalized = qBound(-1.0, m_samples.at(i) / 32768.0, 1.0);
            const qreal y = plot.center().y() - normalized * plot.height() * 0.46;
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        painter.setPen(QPen(QColor(QStringLiteral("#4ade80")), 4,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

private:
    QVector<qint16> m_samples;
};

class AudioPage final : public QWidget
{
public:
    explicit AudioPage(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_audioDir(qEnvironmentVariable("RV1126BLAB_AUDIO_DIR",
              "/userdata/rv1126b_lab/audio"))
    {
        setupUi();

        connect(&m_recorder, &QProcess::readyReadStandardOutput, this, [this]() {
            consumeAudio(m_recorder.readAllStandardOutput());
        });
        connect(&m_recorder,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                consumeAudio(m_recorder.readAllStandardOutput());
                finishRecording();
            });
        connect(&m_recorder, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    m_recordingFinalized = true;
                    restoreMixer();
                    setRecordingUi(false);
                    m_statusLabel->setText(QString::fromUtf8("录音启动失败，请检查 arecord。"));
                }
            });
        connect(&m_player,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                m_playButton->setEnabled(!m_lastWav.isEmpty());
                m_statusLabel->setText(exitCode == 0
                        ? QString::fromUtf8("回放完成：%1").arg(QFileInfo(m_lastWav).fileName())
                        : QString::fromUtf8("回放失败，请检查扬声器和音量。"));
            });
    }

    ~AudioPage() override
    {
        if (m_recorder.state() != QProcess::NotRunning) {
            m_recorder.kill();
            m_recorder.waitForFinished(500);
        }
        if (m_player.state() != QProcess::NotRunning) {
            m_player.kill();
            m_player.waitForFinished(300);
        }
        restoreMixer();
    }

    void runAutomatedTest(int durationMs)
    {
        startRecording();
        QTimer::singleShot(qBound(500, durationMs, 5000), this, [this]() {
            stopRecording();
        });
    }

private:
    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 18, 28, 24);
        root->setSpacing(15);

        auto *infoPanel = makePanel(this);
        auto *infoLayout = new QVBoxLayout(infoPanel);
        infoLayout->setContentsMargins(20, 14, 20, 14);
        auto *title = makeSectionTitle(QString::fromUtf8("ES8390 音频采集"), infoPanel);
        auto *detail = new QLabel(QString::fromUtf8(
            "16 kHz · 单声道 · 16 bit PCM · 最长 60 秒 · 自动保存 WAV"), infoPanel);
        detail->setObjectName(QStringLiteral("muted"));
        infoLayout->addWidget(title);
        infoLayout->addWidget(detail);
        root->addWidget(infoPanel);

        m_waveform = new AudioWaveform(this);
        root->addWidget(m_waveform, 1);

        auto *levelRow = new QHBoxLayout;
        auto *levelTitle = new QLabel(QString::fromUtf8("实时音量"), this);
        levelTitle->setObjectName(QStringLiteral("statusText"));
        m_level = new QProgressBar(this);
        m_level->setRange(0, 100);
        m_level->setTextVisible(false);
        m_levelValue = new QLabel(QStringLiteral("-60 dB"), this);
        m_levelValue->setObjectName(QStringLiteral("statusText"));
        m_levelValue->setMinimumWidth(105);
        m_levelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        levelRow->addWidget(levelTitle);
        levelRow->addWidget(m_level, 1);
        levelRow->addWidget(m_levelValue);
        root->addLayout(levelRow);

        m_statusLabel = new QLabel(QString::fromUtf8("就绪：点击开始录音"), this);
        m_statusLabel->setObjectName(QStringLiteral("largeStatus"));
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);
        root->addWidget(m_statusLabel);

        auto *buttons = new QGridLayout;
        buttons->setHorizontalSpacing(14);
        buttons->setVerticalSpacing(14);
        m_recordButton = new QPushButton(QString::fromUtf8("● 开始录音"), this);
        m_recordButton->setObjectName(QStringLiteral("actionButton"));
        m_stopButton = new QPushButton(QString::fromUtf8("■ 停止并保存"), this);
        m_stopButton->setObjectName(QStringLiteral("actionButton"));
        m_playButton = new QPushButton(QString::fromUtf8("▶ 回放上次录音"), this);
        m_playButton->setObjectName(QStringLiteral("actionButton"));
        m_stopButton->setEnabled(false);
        m_playButton->setEnabled(false);
        connect(m_recordButton, &QPushButton::clicked, this, [this]() { startRecording(); });
        connect(m_stopButton, &QPushButton::clicked, this, [this]() { stopRecording(); });
        connect(m_playButton, &QPushButton::clicked, this, [this]() { playLastRecording(); });
        buttons->addWidget(m_recordButton, 0, 0);
        buttons->addWidget(m_stopButton, 0, 1);
        buttons->addWidget(m_playButton, 1, 0, 1, 2);
        root->addLayout(buttons);

        auto *safety = new QLabel(QString::fromUtf8(
            "录音时只临时提高麦克风采集增益，不打开 ADC 到扬声器直通；停止或退出后恢复原 ALSA 状态。"), this);
        safety->setObjectName(QStringLiteral("notice"));
        safety->setWordWrap(true);
        safety->setAlignment(Qt::AlignCenter);
        root->addWidget(safety);
    }

    bool configureMixer()
    {
        if (qEnvironmentVariableIsSet("RV1126BLAB_AUDIO_NO_MIXER"))
            return true;

        m_mixerState = QString::fromUtf8("/tmp/rv1126blab-mixer-%1.state")
            .arg(QCoreApplication::applicationPid());
        if (QProcess::execute(QStringLiteral("/usr/sbin/alsactl"),
                { QStringLiteral("-f"), m_mixerState, QStringLiteral("store"),
                    QStringLiteral("0") }) != 0) {
            m_mixerState.clear();
            return false;
        }

        const QVector<QStringList> settings = {
            { QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("0"),
                QStringLiteral("set"), QStringLiteral("ADC OSR Volume ON"),
                QStringLiteral("on") },
            { QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("0"),
                QStringLiteral("set"), QStringLiteral("ADCL"), QStringLiteral("85%") },
            { QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("0"),
                QStringLiteral("set"), QStringLiteral("ADCL PGA"), QStringLiteral("50%") },
            { QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("0"),
                QStringLiteral("set"), QStringLiteral("ADCR"), QStringLiteral("85%") },
            { QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("0"),
                QStringLiteral("set"), QStringLiteral("ADCR PGA"), QStringLiteral("50%") }
        };
        for (const QStringList &arguments : settings) {
            if (QProcess::execute(QStringLiteral("/usr/bin/amixer"), arguments) != 0) {
                restoreMixer();
                return false;
            }
        }
        m_mixerConfigured = true;
        return true;
    }

    void restoreMixer()
    {
        if (!m_mixerConfigured && m_mixerState.isEmpty())
            return;
        if (!m_mixerState.isEmpty()) {
            QProcess::execute(QStringLiteral("/usr/sbin/alsactl"),
                { QStringLiteral("-f"), m_mixerState, QStringLiteral("restore"),
                    QStringLiteral("0") });
            QFile::remove(m_mixerState);
        }
        m_mixerConfigured = false;
        m_mixerState.clear();
    }

    void startRecording()
    {
        if (m_recorder.state() != QProcess::NotRunning)
            return;
        if (m_player.state() != QProcess::NotRunning)
            m_player.kill();
        if (!QFile::exists(QStringLiteral("/usr/bin/arecord"))) {
            m_statusLabel->setText(QString::fromUtf8("系统中没有 arecord。"));
            return;
        }
        if (!configureMixer()) {
            m_statusLabel->setText(QString::fromUtf8("无法保存或设置 ALSA 状态，已取消录音。"));
            return;
        }

        m_pcm.clear();
        m_waveform->clear();
        m_level->setValue(0);
        m_levelValue->setText(QStringLiteral("-60 dB"));
        m_recordingFinalized = false;
        m_recorder.setProcessChannelMode(QProcess::SeparateChannels);
        m_recorder.start(QStringLiteral("/usr/bin/arecord"), {
            QStringLiteral("-q"), QStringLiteral("-D"), QStringLiteral("default"),
            QStringLiteral("-t"), QStringLiteral("raw"), QStringLiteral("-f"),
            QStringLiteral("S16_LE"), QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-r"), QStringLiteral("16000"), QStringLiteral("-")
        });
        setRecordingUi(true);
        m_statusLabel->setText(QString::fromUtf8("正在录音：0.0 秒"));
    }

    void stopRecording()
    {
        if (m_recorder.state() == QProcess::NotRunning)
            return;
        m_statusLabel->setText(QString::fromUtf8("正在停止并保存……"));
        m_stopButton->setEnabled(false);
        m_recorder.terminate();
        QTimer::singleShot(800, this, [this]() {
            if (m_recorder.state() != QProcess::NotRunning)
                m_recorder.kill();
        });
    }

    void consumeAudio(const QByteArray &data)
    {
        if (data.isEmpty())
            return;
        m_pcm.append(data);

        const int sampleCount = data.size() / 2;
        if (sampleCount <= 0)
            return;
        const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
        long double energy = 0.0;
        QVector<qint16> waveform;
        const int step = std::max(1, sampleCount / 180);
        waveform.reserve(sampleCount / step + 1);
        for (int i = 0; i < sampleCount; ++i) {
            const qint16 sample = static_cast<qint16>(bytes[i * 2]
                | (static_cast<quint16>(bytes[i * 2 + 1]) << 8));
            energy += static_cast<long double>(sample) * sample;
            if (i % step == 0)
                waveform.append(sample);
        }
        m_waveform->setSamples(waveform);

        const double rms = std::sqrt(static_cast<double>(energy / sampleCount));
        const double db = rms > 0.0 ? 20.0 * std::log10(rms / 32768.0) : -60.0;
        const double boundedDb = qBound(-60.0, db, 0.0);
        m_level->setValue(qRound((boundedDb + 60.0) * 100.0 / 60.0));
        m_levelValue->setText(QString::fromUtf8("%1 dB").arg(boundedDb, 0, 'f', 1));

        const double duration = m_pcm.size() / 2.0 / 16000.0;
        m_statusLabel->setText(QString::fromUtf8("正在录音：%1 秒").arg(duration, 0, 'f', 1));
        if (duration >= 60.0)
            stopRecording();
    }

    void finishRecording()
    {
        if (m_recordingFinalized)
            return;
        m_recordingFinalized = true;
        setRecordingUi(false);

        if (m_pcm.size() % 2 != 0)
            m_pcm.chop(1);
        const QString savedPath = saveWav();
        restoreMixer();

        if (savedPath.isEmpty()) {
            m_statusLabel->setText(QString::fromUtf8("没有收到有效音频，未保存。"));
            return;
        }
        m_lastWav = savedPath;
        m_playButton->setEnabled(true);
        m_statusLabel->setText(QString::fromUtf8("已保存：%1（%2 秒）")
            .arg(QFileInfo(savedPath).fileName())
            .arg(m_pcm.size() / 2.0 / 16000.0, 0, 'f', 1));
    }

    QString saveWav()
    {
        if (m_pcm.isEmpty())
            return {};
        if (!QDir().mkpath(m_audioDir))
            return {};

        const QString fileName = QString::fromUtf8("recording-%1-%2-%3.wav")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
            .arg(QCoreApplication::applicationPid())
            .arg(++m_recordSequence);
        const QString path = QDir(m_audioDir).filePath(fileName);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};

        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint32 dataSize = static_cast<quint32>(m_pcm.size());
        stream.writeRawData("RIFF", 4);
        stream << static_cast<quint32>(36 + dataSize);
        stream.writeRawData("WAVE", 4);
        stream.writeRawData("fmt ", 4);
        stream << static_cast<quint32>(16);
        stream << static_cast<quint16>(1);
        stream << static_cast<quint16>(1);
        stream << static_cast<quint32>(16000);
        stream << static_cast<quint32>(32000);
        stream << static_cast<quint16>(2);
        stream << static_cast<quint16>(16);
        stream.writeRawData("data", 4);
        stream << dataSize;
        stream.writeRawData(m_pcm.constData(), m_pcm.size());
        return file.commit() ? path : QString();
    }

    void playLastRecording()
    {
        if (m_lastWav.isEmpty() || !QFile::exists(m_lastWav)
            || m_player.state() != QProcess::NotRunning) {
            return;
        }
        m_playButton->setEnabled(false);
        m_statusLabel->setText(QString::fromUtf8("正在回放：%1")
            .arg(QFileInfo(m_lastWav).fileName()));
        m_player.start(QStringLiteral("/usr/bin/aplay"),
            { QStringLiteral("-q"), m_lastWav });
    }

    void setRecordingUi(bool recording)
    {
        m_recordButton->setEnabled(!recording);
        m_stopButton->setEnabled(recording);
        if (recording)
            m_playButton->setEnabled(false);
        else
            m_playButton->setEnabled(!m_lastWav.isEmpty()
                && m_player.state() == QProcess::NotRunning);
    }

    QString m_audioDir;
    QString m_lastWav;
    QString m_mixerState;
    QByteArray m_pcm;
    QProcess m_recorder;
    QProcess m_player;
    AudioWaveform *m_waveform = nullptr;
    QProgressBar *m_level = nullptr;
    QLabel *m_levelValue = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_playButton = nullptr;
    bool m_mixerConfigured = false;
    bool m_recordingFinalized = true;
    int m_recordSequence = 0;
};

class VisionPage final : public QWidget
{
public:
    explicit VisionPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setupUi();
        refreshStatus();

        m_external.setWorkingDirectory(QStringLiteral("/"));
        m_external.setProcessChannelMode(QProcess::ForwardedChannels);
        connect(&m_external, &QProcess::started, this, [this]() {
            m_resultLabel->setText(QString::fromUtf8("已启动 %1；关闭子程序后会自动返回实验台。")
                .arg(m_externalDisplayName));
            setLaunchButtonsEnabled(false);
            if (onExternalAppActive)
                onExternalAppActive(true);
        });
        connect(&m_external,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                setLaunchButtonsEnabled(true);
                m_resultLabel->setText(QString::fromUtf8("%1 已退出（返回码 %2）。")
                    .arg(m_externalDisplayName).arg(exitCode));
                if (onExternalAppActive)
                    onExternalAppActive(false);
                refreshStatus();
            });
        connect(&m_external, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    setLaunchButtonsEnabled(true);
                    m_resultLabel->setText(QString::fromUtf8("启动失败：%1")
                        .arg(m_external.errorString()));
                }
            });

        connect(&m_probe,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                const QString output = QString::fromUtf8(m_probe.readAllStandardOutput()).trimmed();
                const QString error = QString::fromUtf8(m_probe.readAllStandardError()).trimmed();
                m_probeButton->setEnabled(true);
                if (exitCode == 0 && !output.isEmpty()) {
                    m_resultLabel->setText(QString::fromUtf8("摄像头格式检测：\n%1")
                        .arg(output));
                } else {
                    m_resultLabel->setText(QString::fromUtf8("格式检测失败：%1")
                        .arg(error.isEmpty() ? QString::fromUtf8("未知错误") : error));
                }
            });
        connect(&m_probe, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    m_probeButton->setEnabled(true);
                    m_resultLabel->setText(QString::fromUtf8("无法启动 v4l2-ctl。"));
                }
            });
    }

    std::function<void(bool)> onExternalAppActive;

    void runAutomatedProbe()
    {
        probeCamera();
    }

    void runAutomatedLaunch(const QString &binaryName, int durationMs)
    {
        const QString displayName = binaryName == QStringLiteral("aispark")
            ? QString::fromUtf8("AiSpark") : QString::fromUtf8("官方相机");
        launchExternal(binaryName, displayName);
        QTimer::singleShot(qBound(1000, durationMs, 5000), this, [this]() {
            if (m_external.state() != QProcess::NotRunning) {
                m_external.terminate();
                QTimer::singleShot(700, this, [this]() {
                    if (m_external.state() != QProcess::NotRunning)
                        m_external.kill();
                });
            }
        });
    }

private:
    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 18, 28, 24);
        root->setSpacing(16);

        auto *cameraPanel = makePanel(this);
        auto *cameraLayout = new QVBoxLayout(cameraPanel);
        cameraLayout->setContentsMargins(22, 18, 22, 18);
        cameraLayout->setSpacing(12);
        cameraLayout->addWidget(makeSectionTitle(QString::fromUtf8("IMX415 摄像头"), cameraPanel));
        m_cameraStatus = new QLabel(cameraPanel);
        m_cameraStatus->setObjectName(QStringLiteral("statusText"));
        m_cameraStatus->setWordWrap(true);
        m_cameraStatus->setMinimumWidth(0);
        m_cameraStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        cameraLayout->addWidget(m_cameraStatus);
        auto *cameraButtons = new QHBoxLayout;
        m_probeButton = new QPushButton(QString::fromUtf8("检测当前格式"), cameraPanel);
        m_probeButton->setObjectName(QStringLiteral("actionButton"));
        m_cameraButton = new QPushButton(QString::fromUtf8("打开官方相机"), cameraPanel);
        m_cameraButton->setObjectName(QStringLiteral("actionButton"));
        connect(m_probeButton, &QPushButton::clicked, this, [this]() { probeCamera(); });
        connect(m_cameraButton, &QPushButton::clicked, this, [this]() {
            launchExternal(QStringLiteral("camera"), QString::fromUtf8("官方相机"));
        });
        cameraButtons->addWidget(m_probeButton);
        cameraButtons->addWidget(m_cameraButton);
        cameraLayout->addLayout(cameraButtons);
        root->addWidget(cameraPanel);

        auto *aiPanel = makePanel(this);
        auto *aiLayout = new QVBoxLayout(aiPanel);
        aiLayout->setContentsMargins(22, 18, 22, 18);
        aiLayout->setSpacing(12);
        aiLayout->addWidget(makeSectionTitle(QString::fromUtf8("NPU / RKNN 智能识别"), aiPanel));
        m_aiStatus = new QLabel(aiPanel);
        m_aiStatus->setObjectName(QStringLiteral("statusText"));
        m_aiStatus->setWordWrap(true);
        m_aiStatus->setMinimumWidth(0);
        m_aiStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        aiLayout->addWidget(m_aiStatus);
        m_aiButton = new QPushButton(QString::fromUtf8("打开 AiSpark（YOLO）"), aiPanel);
        m_aiButton->setObjectName(QStringLiteral("actionButton"));
        connect(m_aiButton, &QPushButton::clicked, this, [this]() {
            launchExternal(QStringLiteral("aispark"), QString::fromUtf8("AiSpark"));
        });
        aiLayout->addWidget(m_aiButton);
        root->addWidget(aiPanel);

        auto *resultPanel = makePanel(this);
        auto *resultLayout = new QVBoxLayout(resultPanel);
        resultLayout->setContentsMargins(22, 16, 22, 16);
        resultLayout->addWidget(makeSectionTitle(QString::fromUtf8("检测与启动结果"), resultPanel));
        m_resultLabel = new QLabel(QString::fromUtf8(
            "相机和 AI 会以独立子程序运行；实验台负责检查设备并避免重复启动。"), resultPanel);
        m_resultLabel->setObjectName(QStringLiteral("statusText"));
        m_resultLabel->setWordWrap(true);
        m_resultLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_resultLabel->setMinimumHeight(170);
        m_resultLabel->setMinimumWidth(0);
        m_resultLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        resultLayout->addWidget(m_resultLabel);
        root->addWidget(resultPanel, 1);

        auto *note = new QLabel(QString::fromUtf8(
            "摄像头和 NPU 推理会争用同一路 /dev/video-camera0，因此一次只运行一个。"), this);
        note->setObjectName(QStringLiteral("notice"));
        note->setAlignment(Qt::AlignCenter);
        note->setWordWrap(true);
        root->addWidget(note);
    }

    void refreshStatus()
    {
        const QString cameraPath = QStringLiteral("/dev/video-camera0");
        const QFileInfo camera(cameraPath);
        QString node = camera.symLinkTarget();
        if (node.isEmpty() && camera.exists())
            node = cameraPath;
        QString driverName;
        if (!node.isEmpty()) {
            driverName = oneLine(QString::fromUtf8("/sys/class/video4linux/%1/name")
                .arg(QFileInfo(node).fileName()), QString());
        }
        m_cameraStatus->setText(camera.exists()
                ? QString::fromUtf8("已识别：%1 → %2\n驱动节点：%3")
                    .arg(cameraPath, node, driverName.isEmpty() ? QString::fromUtf8("可用") : driverName)
                : QString::fromUtf8("未识别 /dev/video-camera0，请检查摄像头连接。"));

        const bool npuReady = QFile::exists(QStringLiteral("/dev/rknpu"));
        const bool runtimeReady = QFile::exists(QStringLiteral("/usr/lib/librknnrt.so"));
        const bool aiReady = QFile::exists(QStringLiteral("/opt/ui/src/apps/aispark"));
        m_aiStatus->setText(QString::fromUtf8("NPU 设备：%1    RKNN Runtime：%2\nAiSpark 与 YOLO 示例：%3")
            .arg(npuReady ? QString::fromUtf8("已识别") : QString::fromUtf8("未识别"))
            .arg(runtimeReady ? QString::fromUtf8("已安装") : QString::fromUtf8("未安装"))
            .arg(aiReady ? QString::fromUtf8("可启动") : QString::fromUtf8("缺失")));
        m_probeButton->setEnabled(camera.exists() && m_probe.state() == QProcess::NotRunning);
        m_cameraButton->setEnabled(camera.exists()
            && QFile::exists(QStringLiteral("/opt/ui/src/apps/camera")));
        m_aiButton->setEnabled(npuReady && runtimeReady && aiReady);
    }

    bool processNamed(const QString &name) const
    {
        const QStringList entries = QDir(QStringLiteral("/proc"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            bool numeric = false;
            entry.toInt(&numeric);
            if (!numeric)
                continue;
            if (oneLine(QString::fromUtf8("/proc/%1/comm").arg(entry), QString()) == name)
                return true;
        }
        return false;
    }

    void launchExternal(const QString &binaryName, const QString &displayName)
    {
        if (m_external.state() != QProcess::NotRunning) {
            m_resultLabel->setText(QString::fromUtf8("请先关闭当前运行的 %1。")
                .arg(m_externalDisplayName));
            return;
        }
        if (processNamed(QStringLiteral("camera")) || processNamed(QStringLiteral("aispark"))) {
            m_resultLabel->setText(QString::fromUtf8(
                "检测到相机或 AiSpark 已在运行，请先从桌面关闭它，再回来启动。"));
            return;
        }

        const QString path = QString::fromUtf8("/opt/ui/src/apps/%1").arg(binaryName);
        if (!QFileInfo(path).isExecutable()) {
            m_resultLabel->setText(QString::fromUtf8("找不到可执行程序：%1").arg(path));
            return;
        }
        m_externalDisplayName = displayName;
        m_resultLabel->setText(QString::fromUtf8("正在启动 %1……").arg(displayName));
        m_external.start(path, QStringList());
    }

    void probeCamera()
    {
        if (m_probe.state() != QProcess::NotRunning)
            return;
        if (processNamed(QStringLiteral("camera")) || processNamed(QStringLiteral("aispark"))) {
            m_resultLabel->setText(QString::fromUtf8("摄像头正在被其他程序使用，暂不检测。"));
            return;
        }
        m_probeButton->setEnabled(false);
        m_resultLabel->setText(QString::fromUtf8("正在读取 V4L2 格式……"));
        m_probe.start(QStringLiteral("/usr/bin/v4l2-ctl"), {
            QStringLiteral("-d"), QStringLiteral("/dev/video-camera0"),
            QStringLiteral("--get-fmt-video")
        });
    }

    void setLaunchButtonsEnabled(bool enabled)
    {
        m_cameraButton->setEnabled(enabled);
        m_aiButton->setEnabled(enabled);
        m_probeButton->setEnabled(enabled);
    }

    QLabel *m_cameraStatus = nullptr;
    QLabel *m_aiStatus = nullptr;
    QLabel *m_resultLabel = nullptr;
    QPushButton *m_probeButton = nullptr;
    QPushButton *m_cameraButton = nullptr;
    QPushButton *m_aiButton = nullptr;
    QProcess m_external;
    QProcess m_probe;
    QString m_externalDisplayName;
};

class BusPanel final : public QWidget
{
public:
    explicit BusPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 12, 0, 0);
        root->setSpacing(14);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(14);
        grid->addWidget(busCard(QStringLiteral("I2C"), m_i2cValue), 0, 0);
        grid->addWidget(busCard(QStringLiteral("UART"), m_uartValue), 0, 1);
        grid->addWidget(busCard(QStringLiteral("CAN / PWM"), m_canValue), 1, 0);
        grid->addWidget(busCard(QStringLiteral("SPI"), m_spiValue), 1, 1);
        root->addLayout(grid);

        auto *buttons = new QGridLayout;
        buttons->setHorizontalSpacing(12);
        buttons->setVerticalSpacing(12);
        addDetailButton(buttons, QString::fromUtf8("I2C 适配器"), 0, 0,
            [this]() { showI2cDetails(); });
        addDetailButton(buttons, QString::fromUtf8("UART 节点"), 0, 1,
            [this]() { showUartDetails(); });
        addDetailButton(buttons, QString::fromUtf8("CAN / PWM"), 1, 0,
            [this]() { showCanPwmDetails(); });
        addDetailButton(buttons, QString::fromUtf8("SPI 状态"), 1, 1,
            [this]() { showSpiDetails(); });
        root->addLayout(buttons);

        auto *detailPanel = makePanel(this);
        auto *detailLayout = new QVBoxLayout(detailPanel);
        detailLayout->setContentsMargins(20, 15, 20, 15);
        detailLayout->addWidget(makeSectionTitle(QString::fromUtf8("只读诊断结果"), detailPanel));
        m_detailLabel = new QLabel(detailPanel);
        m_detailLabel->setObjectName(QStringLiteral("statusText"));
        m_detailLabel->setWordWrap(true);
        m_detailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_detailLabel->setMinimumHeight(210);
        m_detailLabel->setMinimumWidth(0);
        m_detailLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        detailLayout->addWidget(m_detailLabel);
        root->addWidget(detailPanel, 1);

        auto *note = new QLabel(QString::fromUtf8(
            "本页只检查 Linux 设备节点，不主动扫描或写总线。外接模块前必须先核对 40P 针脚与 3.3V 电平。"), this);
        note->setObjectName(QStringLiteral("notice"));
        note->setAlignment(Qt::AlignCenter);
        note->setWordWrap(true);
        root->addWidget(note);

        refreshSummary();
        showI2cDetails();
    }

    void refreshSummary()
    {
        const QStringList i2c = deviceEntries(QStringLiteral("i2c-*"));
        const QStringList uart = deviceEntries(QStringLiteral("ttyS*"));
        const QStringList spi = deviceEntries(QStringLiteral("spidev*"));
        const QStringList pwm = classEntries(QStringLiteral("/sys/class/pwm"),
            QStringLiteral("pwmchip*"));
        const int canCount = (QFile::exists(QStringLiteral("/sys/class/net/can0")) ? 1 : 0)
            + (QFile::exists(QStringLiteral("/sys/class/net/can1")) ? 1 : 0);

        m_i2cValue->setText(QString::fromUtf8("%1 路可见").arg(i2c.size()));
        m_uartValue->setText(QString::fromUtf8("%1 个节点").arg(uart.size()));
        m_canValue->setText(QString::fromUtf8("CAN %1 · PWM %2")
            .arg(canCount).arg(pwm.size()));
        m_spiValue->setText(spi.isEmpty()
                ? QString::fromUtf8("当前未启用")
                : QString::fromUtf8("%1 个节点").arg(spi.size()));
    }

private:
    QWidget *busCard(const QString &name, QLabel *&value)
    {
        auto *panel = makePanel(this);
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(16, 12, 16, 12);
        auto *title = new QLabel(name, panel);
        title->setObjectName(QStringLiteral("sectionTitle"));
        value = new QLabel(QStringLiteral("--"), panel);
        value->setObjectName(QStringLiteral("largeStatus"));
        value->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(value);
        return panel;
    }

    void addDetailButton(QGridLayout *layout, const QString &text, int row, int column,
        const std::function<void()> &handler)
    {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QStringLiteral("actionButton"));
        connect(button, &QPushButton::clicked, this, handler);
        layout->addWidget(button, row, column);
    }

    QStringList deviceEntries(const QString &pattern) const
    {
        return QDir(QStringLiteral("/dev")).entryList({ pattern },
            QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    }

    QStringList classEntries(const QString &path, const QString &pattern) const
    {
        return QDir(path).entryList({ pattern },
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    }

    void showI2cDetails()
    {
        refreshSummary();
        const QStringList adapters = deviceEntries(QStringLiteral("i2c-*"));
        QStringList lines;
        for (const QString &adapter : adapters) {
            const QString name = oneLine(QString::fromUtf8("/sys/class/i2c-adapter/%1/name")
                .arg(adapter), QString::fromUtf8("名称未知"));
            lines.append(QString::fromUtf8("/dev/%1  ·  %2").arg(adapter, name));
        }
        lines.append(QString());
        lines.append(QString::fromUtf8(
            "说明：这里只列出控制器，不执行 i2cdetect 扫描，避免误触发总线上的敏感器件。"));
        m_detailLabel->setText(lines.isEmpty()
                ? QString::fromUtf8("未发现 I2C 适配器。") : lines.join('\n'));
    }

    void showUartDetails()
    {
        refreshSummary();
        const QStringList uart = deviceEntries(QStringLiteral("ttyS*"));
        m_detailLabel->setText(QString::fromUtf8(
            "可见串口：%1\n\nUART5 已由当前设备树启用。真正收发前需要 3.3V USB-TTL，"
            "并确认 TX/RX 交叉、共地和波特率；不能直接接 RS232 电平。")
            .arg(uart.isEmpty() ? QString::fromUtf8("无")
                                : QStringLiteral("/dev/") + uart.join(QStringLiteral("  /dev/"))));
    }

    void showCanPwmDetails()
    {
        refreshSummary();
        QStringList canLines;
        for (const QString &name : { QStringLiteral("can0"), QStringLiteral("can1") }) {
            if (QFile::exists(QString::fromUtf8("/sys/class/net/%1").arg(name))) {
                canLines.append(QString::fromUtf8("%1：%2")
                    .arg(name, oneLine(QString::fromUtf8("/sys/class/net/%1/operstate")
                        .arg(name), QString::fromUtf8("未知"))));
            }
        }
        const QStringList pwm = classEntries(QStringLiteral("/sys/class/pwm"),
            QStringLiteral("pwmchip*"));
        m_detailLabel->setText(QString::fromUtf8(
            "%1\nPWM 控制器：%2\n\nCAN/CAN-FD 需要外接收发器和另一端节点；"
            "PWM 接 LED、蜂鸣器或舵机前要确认引脚复用与负载能力。")
            .arg(canLines.isEmpty() ? QString::fromUtf8("未发现 CAN 节点")
                                    : canLines.join('\n'))
            .arg(pwm.isEmpty() ? QString::fromUtf8("无") : pwm.join(QStringLiteral("、"))));
    }

    void showSpiDetails()
    {
        refreshSummary();
        const QStringList spi = deviceEntries(QStringLiteral("spidev*"));
        if (spi.isEmpty()) {
            m_detailLabel->setText(QString::fromUtf8(
                "当前没有 /dev/spidev*。本机设备树中的 SPI1 尚未启用，因此现在不能直接做用户态 SPI 收发。\n\n"
                "后续需要在 B 盘 SDK 中修改设备树、确认片选和 40P 复用，再重新构建固件；"
                "这不是缺一根线或安装一个软件就能解决的问题。"));
        } else {
            m_detailLabel->setText(QString::fromUtf8("SPI 节点：/dev/%1")
                .arg(spi.join(QStringLiteral("  /dev/"))));
        }
    }

    QLabel *m_i2cValue = nullptr;
    QLabel *m_uartValue = nullptr;
    QLabel *m_canValue = nullptr;
    QLabel *m_spiValue = nullptr;
    QLabel *m_detailLabel = nullptr;
};

class KeyPanel final : public QWidget
{
public:
    explicit KeyPanel(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_devicePath(qEnvironmentVariable("RV1126BLAB_KEY_DEVICE",
              "/dev/input/by-path/platform-adc-keys-event"))
    {
        setupUi();
        openDevice();
    }

    ~KeyPanel() override
    {
        m_pollTimer.stop();
        if (m_fd >= 0)
            ::close(m_fd);
    }

private:
    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 12, 0, 0);
        root->setSpacing(16);

        auto *devicePanel = makePanel(this);
        auto *deviceLayout = new QVBoxLayout(devicePanel);
        deviceLayout->setContentsMargins(20, 15, 20, 15);
        deviceLayout->addWidget(makeSectionTitle(QString::fromUtf8("adc-keys 输入设备"), devicePanel));
        m_deviceLabel = new QLabel(devicePanel);
        m_deviceLabel->setObjectName(QStringLiteral("statusText"));
        m_deviceLabel->setWordWrap(true);
        m_deviceLabel->setMinimumWidth(0);
        m_deviceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        deviceLayout->addWidget(m_deviceLabel);
        root->addWidget(devicePanel);

        auto *eventPanel = makePanel(this);
        auto *eventLayout = new QVBoxLayout(eventPanel);
        eventLayout->setContentsMargins(22, 20, 22, 20);
        eventLayout->setSpacing(15);
        m_keyLabel = new QLabel(QString::fromUtf8("等待按下板载按键"), eventPanel);
        m_keyLabel->setObjectName(QStringLiteral("keyValue"));
        m_keyLabel->setAlignment(Qt::AlignCenter);
        m_stateLabel = new QLabel(QString::fromUtf8("当前状态：未触发"), eventPanel);
        m_stateLabel->setObjectName(QStringLiteral("largeStatus"));
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_countLabel = new QLabel(QString::fromUtf8("有效按下次数：0"), eventPanel);
        m_countLabel->setObjectName(QStringLiteral("statusText"));
        m_countLabel->setAlignment(Qt::AlignCenter);
        eventLayout->addWidget(m_keyLabel);
        eventLayout->addWidget(m_stateLabel);
        eventLayout->addWidget(m_countLabel);
        root->addWidget(eventPanel);

        auto *historyPanel = makePanel(this);
        auto *historyLayout = new QVBoxLayout(historyPanel);
        historyLayout->setContentsMargins(20, 15, 20, 15);
        auto *historyHeader = new QHBoxLayout;
        historyHeader->addWidget(makeSectionTitle(QString::fromUtf8("最近事件"), historyPanel));
        auto *clearButton = new QPushButton(QString::fromUtf8("清零"), historyPanel);
        clearButton->setObjectName(QStringLiteral("smallButton"));
        connect(clearButton, &QPushButton::clicked, this, [this]() {
            m_pressCount = 0;
            m_history.clear();
            m_countLabel->setText(QString::fromUtf8("有效按下次数：0"));
            updateHistory();
        });
        historyHeader->addStretch();
        historyHeader->addWidget(clearButton);
        m_historyLabel = new QLabel(QString::fromUtf8("暂无事件"), historyPanel);
        m_historyLabel->setObjectName(QStringLiteral("statusText"));
        m_historyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_historyLabel->setMinimumHeight(230);
        m_historyLabel->setWordWrap(true);
        historyLayout->addLayout(historyHeader);
        historyLayout->addWidget(m_historyLabel, 1);
        root->addWidget(historyPanel, 1);

        auto *note = new QLabel(QString::fromUtf8(
            "程序只旁路读取按键事件，不独占设备；部分按键仍可能同时触发系统音量或返回动作。"), this);
        note->setObjectName(QStringLiteral("notice"));
        note->setAlignment(Qt::AlignCenter);
        note->setWordWrap(true);
        root->addWidget(note);
    }

    void openDevice()
    {
        const QByteArray path = QFile::encodeName(m_devicePath);
        m_fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_fd < 0) {
            m_deviceLabel->setText(QString::fromUtf8("打开失败：%1").arg(m_devicePath));
            m_keyLabel->setText(QString::fromUtf8("输入设备不可用"));
            return;
        }

        const QString target = QFileInfo(m_devicePath).symLinkTarget();
        m_deviceLabel->setText(QString::fromUtf8("已连接：%1%2\n按下开发板实体按键查看键值、动作和计数。")
            .arg(m_devicePath)
            .arg(target.isEmpty() ? QString() : QString::fromUtf8(" → %1").arg(target)));
        connect(&m_pollTimer, &QTimer::timeout, this, [this]() { readEvents(); });
        m_pollTimer.start(30);
    }

    void readEvents()
    {
        input_event events[16];
        while (true) {
            const ssize_t bytes = ::read(m_fd, events, sizeof(events));
            if (bytes <= 0)
                break;
            const int count = static_cast<int>(bytes / sizeof(input_event));
            for (int i = 0; i < count; ++i) {
                if (events[i].type != EV_KEY)
                    continue;
                const int code = events[i].code;
                const int value = events[i].value;
                const QString name = keyName(code);
                const QString action = value == 0 ? QString::fromUtf8("松开")
                    : value == 1 ? QString::fromUtf8("按下") : QString::fromUtf8("长按重复");
                if (value == 1)
                    ++m_pressCount;
                m_keyLabel->setText(QString::fromUtf8("%1  ·  code %2").arg(name).arg(code));
                m_stateLabel->setText(QString::fromUtf8("当前状态：%1").arg(action));
                m_countLabel->setText(QString::fromUtf8("有效按下次数：%1").arg(m_pressCount));
                m_history.prepend(QString::fromUtf8("%1  %2  code=%3")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                        action).arg(code));
                while (m_history.size() > 8)
                    m_history.removeLast();
                updateHistory();
            }
        }
    }

    QString keyName(int code) const
    {
        switch (code) {
        case KEY_ESC: return QString::fromUtf8("返回 / ESC");
        case KEY_ENTER: return QString::fromUtf8("确认");
        case KEY_HOME: return QString::fromUtf8("主页");
        case KEY_MENU: return QString::fromUtf8("菜单");
        case KEY_POWER: return QString::fromUtf8("电源");
        case KEY_VOLUMEUP: return QString::fromUtf8("音量加");
        case KEY_VOLUMEDOWN: return QString::fromUtf8("音量减");
        case KEY_UP: return QString::fromUtf8("向上");
        case KEY_DOWN: return QString::fromUtf8("向下");
        case KEY_LEFT: return QString::fromUtf8("向左");
        case KEY_RIGHT: return QString::fromUtf8("向右");
        default: return QString::fromUtf8("按键");
        }
    }

    void updateHistory()
    {
        m_historyLabel->setText(m_history.isEmpty()
                ? QString::fromUtf8("暂无事件") : m_history.join('\n'));
    }

    QString m_devicePath;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_keyLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_historyLabel = nullptr;
    QStringList m_history;
    QTimer m_pollTimer;
    int m_fd = -1;
    int m_pressCount = 0;
};

class HardwarePage final : public QWidget
{
public:
    explicit HardwarePage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 14, 28, 24);
        root->setSpacing(12);

        auto *switches = new QHBoxLayout;
        m_busButton = new QPushButton(QString::fromUtf8("总线与扩展接口"), this);
        m_keyButton = new QPushButton(QString::fromUtf8("板载物理按键"), this);
        for (QPushButton *button : { m_busButton, m_keyButton })
            button->setObjectName(QStringLiteral("actionButton"));
        switches->addWidget(m_busButton);
        switches->addWidget(m_keyButton);
        root->addLayout(switches);

        m_stack = new QStackedWidget(this);
        m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_busPanel = new BusPanel(m_stack);
        m_keyPanel = new KeyPanel(m_stack);
        m_stack->addWidget(m_busPanel);
        m_stack->addWidget(m_keyPanel);
        root->addWidget(m_stack, 1);

        connect(m_busButton, &QPushButton::clicked, this, [this]() { showTab(0); });
        connect(m_keyButton, &QPushButton::clicked, this, [this]() { showTab(1); });
        showTab(0);
    }

    void openKeyForTest()
    {
        showTab(1);
    }

private:
    void showTab(int index)
    {
        m_stack->setCurrentIndex(index);
        m_busButton->setProperty("active", index == 0);
        m_keyButton->setProperty("active", index == 1);
        for (QPushButton *button : { m_busButton, m_keyButton }) {
            button->style()->unpolish(button);
            button->style()->polish(button);
            button->update();
        }
        if (index == 0)
            m_busPanel->refreshSummary();
    }

    QStackedWidget *m_stack = nullptr;
    BusPanel *m_busPanel = nullptr;
    KeyPanel *m_keyPanel = nullptr;
    QPushButton *m_busButton = nullptr;
    QPushButton *m_keyButton = nullptr;
};

class TouchCanvas final : public QWidget
{
public:
    explicit TouchCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_AcceptTouchEvents);
        setMinimumHeight(650);
        setMouseTracking(true);
    }

    std::function<void(const QMap<int, QPointF> &, int)> onPointsChanged;

    void clearCanvas()
    {
        m_active.clear();
        m_trails.clear();
        m_maxContacts = 0;
        notify();
        update();
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::TouchBegin
            || event->type() == QEvent::TouchUpdate
            || event->type() == QEvent::TouchEnd) {
            auto *touch = static_cast<QTouchEvent *>(event);
            for (const QTouchEvent::TouchPoint &point : touch->touchPoints()) {
                const int id = point.id();
                if (point.state() == Qt::TouchPointReleased) {
                    appendTrail(id, point.pos());
                    m_active.remove(id);
                } else {
                    m_active[id] = point.pos();
                    if (point.state() != Qt::TouchPointStationary)
                        appendTrail(id, point.pos());
                }
            }
            if (event->type() == QEvent::TouchEnd)
                m_active.clear();
            m_maxContacts = std::max(m_maxContacts, m_active.size());
            notify();
            update();
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_active[0] = event->localPos();
        appendTrail(0, event->localPos());
        m_maxContacts = std::max(m_maxContacts, 1);
        notify();
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        m_active[0] = event->localPos();
        appendTrail(0, event->localPos());
        notify();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        appendTrail(0, event->localPos());
        m_active.remove(0);
        notify();
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        static const QVector<QColor> colors = {
            QColor(QStringLiteral("#38bdf8")), QColor(QStringLiteral("#fb7185")),
            QColor(QStringLiteral("#4ade80")), QColor(QStringLiteral("#fbbf24")),
            QColor(QStringLiteral("#c084fc"))
        };

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#08111f")));

        painter.setPen(QPen(QColor(QStringLiteral("#1e3046")), 1));
        for (int x = 0; x < width(); x += 80)
            painter.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 80)
            painter.drawLine(0, y, width(), y);

        for (auto it = m_trails.cbegin(); it != m_trails.cend(); ++it) {
            const QColor color = colors.at(std::abs(it.key()) % colors.size());
            painter.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QVector<QPointF> &trail = it.value();
            for (int i = 1; i < trail.size(); ++i)
                painter.drawLine(trail.at(i - 1), trail.at(i));
        }

        QFont idFont = painter.font();
        idFont.setPixelSize(28);
        idFont.setBold(true);
        painter.setFont(idFont);
        for (auto it = m_active.cbegin(); it != m_active.cend(); ++it) {
            const QColor color = colors.at(std::abs(it.key()) % colors.size());
            painter.setBrush(QColor(color.red(), color.green(), color.blue(), 105));
            painter.setPen(QPen(color, 5));
            painter.drawEllipse(it.value(), 45, 45);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(it.value().x() - 45, it.value().y() - 45, 90, 90),
                Qt::AlignCenter, QString::number(it.key() + 1));
        }

        if (m_active.isEmpty() && m_trails.isEmpty()) {
            painter.setPen(QColor(QStringLiteral("#8fa6bf")));
            QFont hintFont = painter.font();
            hintFont.setPixelSize(26);
            hintFont.setBold(false);
            painter.setFont(hintFont);
            painter.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter,
                QString::fromUtf8("请用 1～5 根手指同时触摸并移动\n不同编号会显示不同颜色的轨迹"));
        }
    }

private:
    void appendTrail(int id, const QPointF &point)
    {
        QVector<QPointF> &trail = m_trails[id];
        trail.append(point);
        if (trail.size() > 100)
            trail.remove(0, trail.size() - 100);
    }

    void notify()
    {
        if (onPointsChanged)
            onPointsChanged(m_active, m_maxContacts);
    }

    QMap<int, QPointF> m_active;
    QMap<int, QVector<QPointF>> m_trails;
    int m_maxContacts = 0;
};

class TouchPage final : public QWidget
{
public:
    explicit TouchPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 18, 28, 24);
        root->setSpacing(14);

        auto *top = new QHBoxLayout;
        m_statusLabel = new QLabel(QString::fromUtf8("当前 0 点    本次最多 0 点"), this);
        m_statusLabel->setObjectName(QStringLiteral("statusText"));
        auto *clearButton = new QPushButton(QString::fromUtf8("清除轨迹"), this);
        clearButton->setObjectName(QStringLiteral("smallButton"));
        top->addWidget(m_statusLabel, 1);
        top->addWidget(clearButton);
        root->addLayout(top);

        m_detailLabel = new QLabel(QString::fromUtf8("驱动能力：最多 5 个同时触点"), this);
        m_detailLabel->setObjectName(QStringLiteral("muted"));
        m_detailLabel->setWordWrap(true);
        root->addWidget(m_detailLabel);

        m_canvas = new TouchCanvas(this);
        m_canvas->setObjectName(QStringLiteral("touchCanvas"));
        root->addWidget(m_canvas, 1);

        connect(clearButton, &QPushButton::clicked, m_canvas, [this]() { m_canvas->clearCanvas(); });
        m_canvas->onPointsChanged = [this](const QMap<int, QPointF> &points, int maxContacts) {
            m_statusLabel->setText(QString::fromUtf8("当前 %1 点    本次最多 %2 点")
                .arg(points.size()).arg(maxContacts));
            QStringList details;
            for (auto it = points.cbegin(); it != points.cend(); ++it) {
                details.append(QString::fromUtf8("%1:(%2,%3)")
                    .arg(it.key() + 1)
                    .arg(it.value().x(), 0, 'f', 0)
                    .arg(it.value().y(), 0, 'f', 0));
            }
            if (points.size() >= 2) {
                auto first = points.cbegin();
                const QPointF a = first.value();
                ++first;
                const QPointF b = first.value();
                const double distance = std::hypot(a.x() - b.x(), a.y() - b.y());
                details.append(QString::fromUtf8("前两点距离:%1").arg(distance, 0, 'f', 0));
            }
            m_detailLabel->setText(details.isEmpty()
                    ? QString::fromUtf8("驱动能力：最多 5 个同时触点")
                    : details.join(QStringLiteral("    ")));
        };
    }

private:
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    TouchCanvas *m_canvas = nullptr;
};

class SystemPage final : public QWidget
{
public:
    explicit SystemPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 18, 28, 24);
        root->setSpacing(16);

        auto *identity = makePanel(this);
        auto *identityLayout = new QVBoxLayout(identity);
        identityLayout->setContentsMargins(20, 16, 20, 16);
        auto *identityTitle = makeSectionTitle(QString::fromUtf8("板卡身份"), identity);
        m_identityLabel = new QLabel(identity);
        m_identityLabel->setObjectName(QStringLiteral("statusText"));
        m_identityLabel->setWordWrap(true);
        identityLayout->addWidget(identityTitle);
        identityLayout->addWidget(m_identityLabel);
        root->addWidget(identity);

        auto *resourcePanel = makePanel(this);
        auto *resourceLayout = new QVBoxLayout(resourcePanel);
        resourceLayout->setContentsMargins(20, 16, 20, 16);
        resourceLayout->setSpacing(10);
        resourceLayout->addWidget(makeSectionTitle(QString::fromUtf8("资源占用（每秒刷新）"), resourcePanel));
        addProgressRow(resourceLayout, QString::fromUtf8("CPU"), m_cpuValue, m_cpuProgress);
        addProgressRow(resourceLayout, QString::fromUtf8("内存"), m_memoryValue, m_memoryProgress);
        addProgressRow(resourceLayout, QString::fromUtf8("根分区"), m_storageValue, m_storageProgress);
        root->addWidget(resourcePanel);

        auto *statusPanel = makePanel(this);
        auto *statusLayout = new QVBoxLayout(statusPanel);
        statusLayout->setContentsMargins(20, 16, 20, 16);
        statusLayout->setSpacing(10);
        statusLayout->addWidget(makeSectionTitle(QString::fromUtf8("温度与设备"), statusPanel));
        m_temperatureLabel = new QLabel(statusPanel);
        m_temperatureLabel->setObjectName(QStringLiteral("largeStatus"));
        m_deviceLabel = new QLabel(statusPanel);
        m_deviceLabel->setObjectName(QStringLiteral("statusText"));
        m_deviceLabel->setWordWrap(true);
        statusLayout->addWidget(m_temperatureLabel);
        statusLayout->addWidget(m_deviceLabel);
        root->addWidget(statusPanel);
        root->addStretch();

        const QString model = oneLine(QStringLiteral("/proc/device-tree/model"),
            QString::fromUtf8("Alientek RV1126B Board"));
        const QString kernel = oneLine(QStringLiteral("/proc/sys/kernel/osrelease"));
        const long cores = sysconf(_SC_NPROCESSORS_ONLN);
        m_identityLabel->setText(QString::fromUtf8("%1\nLinux %2 · ARM64 · %3 核 CPU")
            .arg(model, kernel)
            .arg(cores > 0 ? cores : 0));
    }

    void updateSnapshot(const SystemSnapshot &snapshot)
    {
        m_cpuProgress->setValue(qRound(snapshot.cpuPercent));
        m_cpuValue->setText(QString::fromUtf8("%1%").arg(snapshot.cpuPercent, 0, 'f', 1));

        m_memoryProgress->setValue(qRound(snapshot.memoryPercent));
        m_memoryValue->setText(QString::fromUtf8("%1% · 可用 %2 / %3")
            .arg(snapshot.memoryPercent, 0, 'f', 1)
            .arg(formatBytes(snapshot.memoryAvailable))
            .arg(formatBytes(snapshot.memoryTotal)));

        m_storageProgress->setValue(qRound(snapshot.storagePercent));
        m_storageValue->setText(QString::fromUtf8("%1% · 可用 %2 / %3")
            .arg(snapshot.storagePercent, 0, 'f', 1)
            .arg(formatBytes(snapshot.storageAvailable))
            .arg(formatBytes(snapshot.storageTotal)));

        m_temperatureLabel->setText(QString::fromUtf8("CPU %1°C    已运行 %2")
            .arg(snapshot.temperatureC, 0, 'f', 1)
            .arg(uptimeText(snapshot.uptimeSeconds)));
        m_deviceLabel->setText(QString::fromUtf8("摄像头：%1    NPU：%2\n网口：%3    Wi-Fi：%4")
            .arg(snapshot.cameraReady ? QString::fromUtf8("已识别") : QString::fromUtf8("未识别"))
            .arg(snapshot.npuReady ? QString::fromUtf8("已识别") : QString::fromUtf8("未识别"))
            .arg(snapshot.ethState)
            .arg(snapshot.wifiState));
    }

private:
    void addProgressRow(QVBoxLayout *layout, const QString &title,
        QLabel *&value, QProgressBar *&progress)
    {
        auto *row = new QHBoxLayout;
        auto *titleLabel = new QLabel(title, this);
        titleLabel->setObjectName(QStringLiteral("statusText"));
        value = new QLabel(QStringLiteral("--"), this);
        value->setObjectName(QStringLiteral("muted"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(titleLabel);
        row->addWidget(value, 1);
        layout->addLayout(row);
        progress = new QProgressBar(this);
        progress->setRange(0, 100);
        progress->setTextVisible(false);
        layout->addWidget(progress);
    }

    QLabel *m_identityLabel = nullptr;
    QLabel *m_cpuValue = nullptr;
    QLabel *m_memoryValue = nullptr;
    QLabel *m_storageValue = nullptr;
    QLabel *m_temperatureLabel = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QProgressBar *m_cpuProgress = nullptr;
    QProgressBar *m_memoryProgress = nullptr;
    QProgressBar *m_storageProgress = nullptr;
};

class HelpPage final : public QWidget
{
public:
    explicit HelpPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(32, 24, 32, 30);
        root->setSpacing(18);

        root->addWidget(makeSectionTitle(QString::fromUtf8("实验台怎么向导师演示"), this));
        auto *content = makePanel(this);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(16);
        auto *steps = new QLabel(QString::fromUtf8(
            "1. 系统页：展示 Linux、CPU、内存、温度、摄像头与 NPU 状态。\n\n"
            "2. IO / ADC 页：旋转蓝色电位器，观察原始值、电压、滤波值和曲线；"
            "拖动阈值并演示 LED 联动，再切换 ADC 调速闪烁。\n\n"
            "3. 五点触摸页：先单指画轨迹，再逐步增加到五指，展示独立编号、坐标和距离。\n\n"
            "4. 音频页：录制一段语音，观察音量和波形，停止后触摸回放。\n\n"
            "5. 摄像头 / AI 页：读取 IMX415 格式，分别启动官方相机和 AiSpark 演示。\n\n"
            "6. 总线 / 按键页：查看 I2C、UART、CAN、PWM、SPI 状态，再按实体按键观察事件。\n\n"
            "7. 返回主页并退出，说明程序会恢复进入前的 LED 与音频混音状态。"), content);
        steps->setObjectName(QStringLiteral("helpText"));
        steps->setWordWrap(true);
        layout->addWidget(steps);
        root->addWidget(content, 1);

        auto *note = new QLabel(QString::fromUtf8(
            "这是运行在 Buildroot Linux 上的 ARM64 Qt5 程序，不是安卓 APK。"), this);
        note->setObjectName(QStringLiteral("notice"));
        note->setAlignment(Qt::AlignCenter);
        note->setWordWrap(true);
        root->addWidget(note);
    }
};

class MainWindow final : public QWidget
{
public:
    MainWindow()
    {
        setWindowTitle(QString::fromUtf8("RV1126B 触摸实验台"));
        setWindowFlags(Qt::FramelessWindowHint);
        setupStyle();
        setupUi();

        m_previousCpu = readCpuCounters();
        updateSystem();
        connect(&m_systemTimer, &QTimer::timeout, this, [this]() { updateSystem(); });
        m_systemTimer.start(1000);
    }

    void openPageForTest(int index)
    {
        showPage(index);
    }

    void runAudioTest(int durationMs)
    {
        showPage(4);
        m_audioPage->runAutomatedTest(durationMs);
    }

    void runVisionProbeTest()
    {
        showPage(5);
        m_visionPage->runAutomatedProbe();
    }

    void runVisionLaunchTest(const QString &binaryName, int durationMs)
    {
        showPage(5);
        m_visionPage->runAutomatedLaunch(binaryName, durationMs);
    }

    void openHardwareKeyForTest()
    {
        showPage(6);
        m_hardwarePage->openKeyForTest();
    }

    void runExitButtonTest()
    {
        m_bottomExitButton->click();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        layoutHeader();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
            if (m_stack->currentIndex() == 6) {
                event->accept();
                return;
            }
            if (m_stack->currentIndex() == 0)
                requestExit();
            else
                showPage(0);
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_header = new QFrame(this);
        m_header->setObjectName(QStringLiteral("header"));
        m_header->setFixedHeight(84);

        m_backButton = new QPushButton(QString::fromUtf8("‹ 主页"), m_header);
        m_backButton->setObjectName(QStringLiteral("headerButton"));
        m_backButton->setFixedWidth(118);
        connect(m_backButton, &QPushButton::clicked, this, [this]() { showPage(0); });
        m_titleLabel = new QLabel(QString::fromUtf8("RV1126B 实验台"), m_header);
        m_titleLabel->setObjectName(QStringLiteral("headerTitle"));
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setMinimumWidth(0);
        m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_exitButton = new QPushButton(QString::fromUtf8("返回桌面"), m_header);
        m_exitButton->setObjectName(QStringLiteral("exitButton"));
        m_exitButton->setFixedWidth(144);
        connect(m_exitButton, &QPushButton::pressed, this, [this]() { requestExit(); });
        root->addWidget(m_header);

        m_stack = new QStackedWidget(this);
        m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_homePage = new HomePage(m_stack);
        m_ioPage = new IoPage(m_stack);
        m_touchPage = new TouchPage(m_stack);
        m_systemPage = new SystemPage(m_stack);
        m_audioPage = new AudioPage(m_stack);
        m_visionPage = new VisionPage(m_stack);
        m_hardwarePage = new HardwarePage(m_stack);
        m_helpPage = new HelpPage(m_stack);
        m_stack->addWidget(m_homePage);
        m_stack->addWidget(m_ioPage);
        m_stack->addWidget(m_touchPage);
        m_stack->addWidget(m_systemPage);
        m_stack->addWidget(m_audioPage);
        m_stack->addWidget(m_visionPage);
        m_stack->addWidget(m_hardwarePage);
        m_stack->addWidget(m_helpPage);
        root->addWidget(m_stack, 1);

        m_homePage->onNavigate = [this](int page) { showPage(page); };
        m_ioPage->onAdcChanged = [this](int raw) { m_homePage->updateAdc(raw); };
        m_visionPage->onExternalAppActive = [this](bool active) {
            if (active) {
                hide();
            } else {
                showFullScreen();
                showPage(5);
                raise();
                activateWindow();
            }
        };

        m_returnBar = new QFrame(this);
        m_returnBar->setObjectName(QStringLiteral("returnBar"));
        m_returnBar->setFixedHeight(76);
        auto *returnLayout = new QHBoxLayout(m_returnBar);
        returnLayout->setContentsMargins(18, 8, 18, 8);
        m_bottomExitButton = new QPushButton(
            QString::fromUtf8("↑  点击或从这里上滑返回桌面"), m_returnBar);
        m_bottomExitButton->setObjectName(QStringLiteral("bottomExitButton"));
        connect(m_bottomExitButton, &QPushButton::pressed,
            this, [this]() { requestExit(); });
        returnLayout->addWidget(m_bottomExitButton);
        root->addWidget(m_returnBar);

        layoutHeader();
        showPage(0);
    }

    void layoutHeader()
    {
        if (!m_header || !m_backButton || !m_titleLabel || !m_exitButton)
            return;
        const int right = std::max(18, m_header->width() - 162);
        m_backButton->setGeometry(18, 12, 118, 60);
        m_exitButton->setGeometry(right, 12, 144, 60);
        const int titleLeft = m_backButton->isVisible() ? 148 : 18;
        m_titleLabel->setGeometry(titleLeft, 12,
            std::max(0, right - 12 - titleLeft), 60);
    }

    void showPage(int index)
    {
        if (index < 0 || index >= m_stack->count())
            return;
        if (m_stack->currentIndex() == 1 && index != 1)
            m_ioPage->leavePage();
        static const QStringList titles = {
            QString::fromUtf8("RV1126B 实验台"),
            QString::fromUtf8("IO / ADC 实验"),
            QString::fromUtf8("五点触摸实验"),
            QString::fromUtf8("系统监控"),
            QString::fromUtf8("音频采集与波形"),
            QString::fromUtf8("摄像头与 AI"),
            QString::fromUtf8("总线与板载按键"),
            QString::fromUtf8("实验说明")
        };
        m_stack->setCurrentIndex(index);
        m_titleLabel->setText(titles.value(index));
        m_backButton->setVisible(index != 0);
        for (QPushButton *button : { m_backButton, m_exitButton }) {
            button->style()->unpolish(button);
            button->style()->polish(button);
            button->update();
        }
        layoutHeader();
    }

    void requestExit()
    {
        if (m_exitRequested)
            return;
        m_exitRequested = true;
        m_exitButton->setEnabled(false);
        m_bottomExitButton->setEnabled(false);
        hide();
        QTimer::singleShot(0, qApp, []() { QCoreApplication::exit(0); });
    }

    void updateSystem()
    {
        CpuCounters current;
        const SystemSnapshot snapshot = sampleSystem(m_previousCpu, &current);
        if (current.valid)
            m_previousCpu = current;
        m_homePage->updateSystem(snapshot);
        m_systemPage->updateSnapshot(snapshot);
    }

    void setupStyle()
    {
        setStyleSheet(QStringLiteral(R"(
            QWidget {
                background-color: #0f172a;
                color: #f8fafc;
            }
            QFrame#header {
                background-color: #111c30;
                border-bottom: 1px solid #263449;
            }
            QLabel#headerTitle {
                font-size: 30px;
                font-weight: 700;
                color: #e8f5ff;
            }
            QPushButton#headerButton, QPushButton#exitButton {
                min-width: 116px;
                min-height: 58px;
                border: 1px solid #40516a;
                border-radius: 14px;
                background-color: #1d2a40;
                font-size: 22px;
                font-weight: 600;
            }
            QPushButton#exitButton {
                min-width: 142px;
                border-color: #7f3340;
                background-color: #702b37;
            }
            QFrame#returnBar {
                background-color: #111c30;
                border-top: 1px solid #33445f;
            }
            QPushButton#bottomExitButton {
                min-height: 58px;
                border: 2px solid #a34151;
                border-radius: 14px;
                background-color: #702b37;
                color: #ffffff;
                font-size: 24px;
                font-weight: 700;
            }
            QPushButton#bottomExitButton:pressed {
                background-color: #9b3d4e;
            }
            QFrame#panel {
                background-color: #172238;
                border: 1px solid #2a3b54;
                border-radius: 18px;
            }
            QLabel#heroTitle {
                font-size: 38px;
                font-weight: 750;
                color: #7dd3fc;
            }
            QLabel#sectionTitle {
                font-size: 27px;
                font-weight: 700;
                color: #7dd3fc;
            }
            QLabel#metricTitle, QLabel#muted {
                font-size: 20px;
                color: #9fb0c6;
            }
            QLabel#metricValue {
                font-size: 31px;
                font-weight: 700;
                color: #f8fafc;
            }
            QLabel#statusText {
                font-size: 22px;
                color: #d8e2ef;
            }
            QLabel#largeStatus {
                font-size: 27px;
                font-weight: 650;
                color: #f8fafc;
            }
            QLabel#adcValue {
                font-size: 31px;
                font-weight: 700;
                color: #f8fafc;
            }
            QLabel#error {
                font-size: 20px;
                color: #fda4af;
            }
            QLabel#notice {
                min-height: 62px;
                padding: 8px;
                border: 1px solid #2e7494;
                border-radius: 14px;
                background-color: #123249;
                color: #bae6fd;
                font-size: 21px;
            }
            QLabel#helpText {
                font-size: 25px;
                line-height: 1.5;
                color: #e4edf7;
            }
            QPushButton#moduleButton {
                min-height: 128px;
                padding: 16px;
                border: 2px solid #334b69;
                border-radius: 22px;
                background-color: #192a43;
                color: #f8fafc;
                font-size: 24px;
                font-weight: 650;
                text-align: left;
            }
            QPushButton#moduleButton:pressed {
                background-color: #254568;
                border-color: #38bdf8;
            }
            QPushButton#actionButton {
                min-height: 78px;
                border: 2px solid #40516a;
                border-radius: 16px;
                background-color: #1b2a42;
                font-size: 21px;
                font-weight: 600;
            }
            QPushButton#actionButton[active="true"] {
                border-color: #38bdf8;
                background-color: #075985;
            }
            QPushButton#smallButton {
                min-width: 132px;
                min-height: 58px;
                border: 1px solid #40516a;
                border-radius: 14px;
                background-color: #23344d;
                font-size: 21px;
                font-weight: 600;
            }
            QPushButton:pressed {
                background-color: #314863;
            }
            QProgressBar {
                min-height: 34px;
                border: 1px solid #33445c;
                border-radius: 11px;
                background-color: #0c1525;
            }
            QProgressBar::chunk {
                border-radius: 10px;
                background-color: #22a8d8;
            }
            QSlider {
                min-height: 68px;
            }
            QSlider::groove:horizontal {
                height: 18px;
                border-radius: 9px;
                background-color: #33445c;
            }
            QSlider::sub-page:horizontal {
                border-radius: 9px;
                background-color: #f59e0b;
            }
            QSlider::handle:horizontal {
                width: 52px;
                margin: -18px 0;
                border: 4px solid #f59e0b;
                border-radius: 26px;
                background-color: #fff7ed;
            }
        )"));
    }

    QStackedWidget *m_stack = nullptr;
    HomePage *m_homePage = nullptr;
    IoPage *m_ioPage = nullptr;
    TouchPage *m_touchPage = nullptr;
    SystemPage *m_systemPage = nullptr;
    AudioPage *m_audioPage = nullptr;
    VisionPage *m_visionPage = nullptr;
    HardwarePage *m_hardwarePage = nullptr;
    HelpPage *m_helpPage = nullptr;
    QFrame *m_header = nullptr;
    QFrame *m_returnBar = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_exitButton = nullptr;
    QPushButton *m_bottomExitButton = nullptr;
    QLabel *m_titleLabel = nullptr;
    bool m_exitRequested = false;
    QTimer m_systemTimer;
    CpuCounters m_previousCpu;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("rv1126blab"));

    QFont font = app.font();
    font.setPixelSize(22);
    app.setFont(font);

    MainWindow window;
    bool startPageOk = false;
    const int startPage = qEnvironmentVariableIntValue("RV1126BLAB_START_PAGE", &startPageOk);
    const QString screenshotPath = qEnvironmentVariable("RV1126BLAB_SCREENSHOT");
    if (screenshotPath.isEmpty()) {
        if (app.primaryScreen())
            window.setFixedSize(app.primaryScreen()->size());
        window.showFullScreen();
    } else {
        window.setFixedSize(720, 1280);
        window.show();
        QTimer::singleShot(800, &window, [&window, screenshotPath]() {
            window.grab().save(screenshotPath, "PNG");
        });
    }
    QTimer::singleShot(0, &window, [&window, startPageOk, startPage]() {
        window.openPageForTest(startPageOk ? startPage : 0);
    });
    bool audioTestOk = false;
    const int audioTestMs = qEnvironmentVariableIntValue(
        "RV1126BLAB_AUDIO_TEST_MS", &audioTestOk);
    if (audioTestOk && audioTestMs > 0) {
        QTimer::singleShot(100, &window, [&window, audioTestMs]() {
            window.runAudioTest(audioTestMs);
        });
    }
    if (qEnvironmentVariableIsSet("RV1126BLAB_VISION_PROBE")) {
        QTimer::singleShot(100, &window, [&window]() {
            window.runVisionProbeTest();
        });
    }
    const QString visionLaunchTest = qEnvironmentVariable("RV1126BLAB_VISION_LAUNCH_TEST");
    if (visionLaunchTest == QStringLiteral("camera")
        || visionLaunchTest == QStringLiteral("aispark")) {
        bool launchDurationOk = false;
        const int launchDuration = qEnvironmentVariableIntValue(
            "RV1126BLAB_VISION_LAUNCH_MS", &launchDurationOk);
        QTimer::singleShot(200, &window, [&window, visionLaunchTest,
            launchDurationOk, launchDuration]() {
            window.runVisionLaunchTest(visionLaunchTest,
                launchDurationOk ? launchDuration : 2500);
        });
    }
    if (qEnvironmentVariable("RV1126BLAB_HARDWARE_TAB") == QStringLiteral("key")) {
        QTimer::singleShot(100, &window, [&window]() {
            window.openHardwareKeyForTest();
        });
    }
    if (qEnvironmentVariableIsSet("RV1126BLAB_EXIT_TEST")) {
        QTimer::singleShot(500, &window, [&window]() {
            window.runExitButtonTest();
        });
    }

    bool autoExitOk = false;
    const int autoExitMs = qEnvironmentVariableIntValue("RV1126BLAB_AUTO_EXIT_MS", &autoExitOk);
    if (autoExitOk && autoExitMs > 0)
        QTimer::singleShot(autoExitMs, &app, &QApplication::quit);

    return app.exec();
}
