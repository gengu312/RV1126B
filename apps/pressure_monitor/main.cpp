#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kAdcMaximum = 8191;
constexpr int kSampleIntervalMs = 100;
constexpr int kCalibrationSamples = 20;
constexpr int kHistorySamples = 300;
constexpr int kVoiceStableSamples = 5;
constexpr int kVoiceCooldownMs = 2000;

enum class PressureState {
    Released,
    Light,
    Medium,
    Heavy
};

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QFrame *makePanel(QWidget *parent = nullptr)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    return panel;
}

class PressureChart final : public QWidget
{
public:
    explicit PressureChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(390);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setValues(const QVector<double> &values)
    {
        m_values = values;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#0b1220")));

        const QRectF plot = rect().adjusted(52, 22, -20, -42);
        painter.setFont(QFont(painter.font().family(), 14));
        painter.setPen(QPen(QColor(QStringLiteral("#334155")), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(QColor(QStringLiteral("#94a3b8")));
            painter.drawText(QRectF(0, y - 12, 44, 24), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(100 - i * 25));
            painter.setPen(QPen(QColor(QStringLiteral("#334155")), 1));
        }
        for (int i = 0; i <= 6; ++i) {
            const qreal x = plot.left() + plot.width() * i / 6.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        painter.setPen(QColor(QStringLiteral("#94a3b8")));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 28),
            Qt::AlignCenter, QString::fromUtf8("最近 30 秒（右侧为当前值）"));

        if (m_values.size() < 2)
            return;

        QPainterPath path;
        for (int i = 0; i < m_values.size(); ++i) {
            const qreal x = plot.left()
                + plot.width() * i / std::max(1, m_values.size() - 1);
            const qreal y = plot.bottom()
                - plot.height() * qBound(0.0, m_values.at(i), 100.0) / 100.0;
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }

        QPainterPath area(path);
        area.lineTo(plot.right(), plot.bottom());
        area.lineTo(plot.left(), plot.bottom());
        area.closeSubpath();
        painter.fillPath(area, QColor(14, 165, 233, 42));
        painter.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 5,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

private:
    QVector<double> m_values;
};

class PressureWindow final : public QWidget
{
public:
    explicit PressureWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(QString::fromUtf8("压力监测"));
        setupUi();
        setupStyle();
        setupSpeech();
        inspectHardware();
        resetCalibration();
        connect(&m_sampleTimer, &QTimer::timeout, this, [this]() { sample(); });
        m_sampleTimer.start(kSampleIntervalMs);
    }

    ~PressureWindow() override
    {
        if (m_ttsProcess.state() != QProcess::NotRunning)
            m_ttsProcess.kill();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void setupUi()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 18, 24, 22);
        root->setSpacing(14);

        auto *header = makePanel(this);
        header->setObjectName(QStringLiteral("header"));
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(20, 12, 14, 12);
        auto *title = new QLabel(QString::fromUtf8("N1 压力监测"), header);
        title->setObjectName(QStringLiteral("headerTitle"));
        auto *exitButton = new QPushButton(QString::fromUtf8("退出"), header);
        exitButton->setObjectName(QStringLiteral("exitButton"));
        connect(exitButton, &QPushButton::clicked, this, &QWidget::close);
        headerLayout->addWidget(title);
        headerLayout->addStretch();
        headerLayout->addWidget(exitButton);
        root->addWidget(header);

        auto *wiringPanel = makePanel(this);
        auto *wiringLayout = new QVBoxLayout(wiringPanel);
        wiringLayout->setContentsMargins(18, 12, 18, 12);
        auto *wiringTitle = new QLabel(QString::fromUtf8("接线与采样"), wiringPanel);
        wiringTitle->setObjectName(QStringLiteral("sectionTitle"));
        auto *wiring = new QLabel(QString::fromUtf8(
            "1.8 V → 压力传感器 → N1 公共节点 → 10 kΩ → GND\n"
            "每 100 ms 只读一次 N1 ADC，不会驱动 GPIO。"), wiringPanel);
        wiring->setObjectName(QStringLiteral("statusText"));
        wiring->setWordWrap(true);
        wiringLayout->addWidget(wiringTitle);
        wiringLayout->addWidget(wiring);
        root->addWidget(wiringPanel);

        auto *valuePanel = makePanel(this);
        auto *valueLayout = new QVBoxLayout(valuePanel);
        valueLayout->setContentsMargins(18, 14, 18, 14);
        valueLayout->setSpacing(8);
        m_stateLabel = new QLabel(QString::fromUtf8("正在校准"), valuePanel);
        m_stateLabel->setObjectName(QStringLiteral("pressureState"));
        m_stateLabel->setProperty("level", QStringLiteral("calibrating"));
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_percentLabel = new QLabel(QString::fromUtf8("相对力度 --"), valuePanel);
        m_percentLabel->setObjectName(QStringLiteral("pressureValue"));
        m_percentLabel->setAlignment(Qt::AlignCenter);
        m_valueLabel = new QLabel(QString::fromUtf8(
            "原始值 -- / 8191    平滑值 --    电压 -- V"), valuePanel);
        m_valueLabel->setObjectName(QStringLiteral("statusText"));
        m_valueLabel->setAlignment(Qt::AlignCenter);
        valueLayout->addWidget(m_stateLabel);
        valueLayout->addWidget(m_percentLabel);
        valueLayout->addWidget(m_valueLabel);
        root->addWidget(valuePanel);

        auto *chartHeader = new QHBoxLayout;
        auto *chartTitle = new QLabel(QString::fromUtf8("相对力度曲线"), this);
        chartTitle->setObjectName(QStringLiteral("sectionTitle"));
        auto *resetButton = new QPushButton(QString::fromUtf8("重新校准"), this);
        resetButton->setObjectName(QStringLiteral("resetButton"));
        connect(resetButton, &QPushButton::clicked,
            this, [this]() { resetCalibration(); });
        m_ttsButton = new QPushButton(QString::fromUtf8("语音播报：关闭"), this);
        m_ttsButton->setObjectName(QStringLiteral("ttsButton"));
        m_ttsButton->setProperty("active", false);
        connect(m_ttsButton, &QPushButton::clicked,
            this, [this]() { setSpeechEnabled(!m_speechEnabled); });
        chartHeader->addWidget(chartTitle);
        chartHeader->addStretch();
        chartHeader->addWidget(m_ttsButton);
        chartHeader->addWidget(resetButton);
        root->addLayout(chartHeader);

        m_chart = new PressureChart(this);
        root->addWidget(m_chart, 1);

        m_baselineLabel = new QLabel(this);
        m_baselineLabel->setObjectName(QStringLiteral("notice"));
        m_baselineLabel->setAlignment(Qt::AlignCenter);
        m_baselineLabel->setWordWrap(true);
        root->addWidget(m_baselineLabel);

        m_errorLabel = new QLabel(this);
        m_errorLabel->setObjectName(QStringLiteral("error"));
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        root->addWidget(m_errorLabel);

        auto *note = new QLabel(QString::fromUtf8(
            "进入页面或重新校准后的约 2 秒内请完全松开传感器。"
            "相对力度只用于比较轻重，不代表牛顿值。"), this);
        note->setObjectName(QStringLiteral("note"));
        note->setAlignment(Qt::AlignCenter);
        note->setWordWrap(true);
        root->addWidget(note);
    }

    void setupStyle()
    {
        setStyleSheet(QStringLiteral(R"(
            QWidget {
                background-color: #0f172a;
                color: #f8fafc;
                font-size: 21px;
            }
            QFrame#header {
                background-color: #111c30;
                border: 1px solid #334155;
                border-radius: 18px;
            }
            QFrame#panel {
                background-color: #172238;
                border: 1px solid #2a3b54;
                border-radius: 18px;
            }
            QLabel#headerTitle {
                font-size: 32px;
                font-weight: 700;
                color: #e8f5ff;
            }
            QLabel#sectionTitle {
                font-size: 26px;
                font-weight: 700;
                color: #7dd3fc;
            }
            QLabel#statusText {
                font-size: 21px;
                color: #d8e2ef;
            }
            QLabel#pressureValue {
                font-size: 35px;
                font-weight: 700;
            }
            QLabel#pressureState {
                min-height: 78px;
                border: 2px solid #3b4d66;
                border-radius: 16px;
                background-color: #1d293c;
                font-size: 36px;
                font-weight: 750;
            }
            QLabel#pressureState[level="calibrating"] {
                border-color: #38bdf8;
                background-color: #123249;
                color: #bae6fd;
            }
            QLabel#pressureState[level="released"] {
                border-color: #64748b;
                background-color: #1e293b;
                color: #e2e8f0;
            }
            QLabel#pressureState[level="light"] {
                border-color: #22c55e;
                background-color: #14532d;
                color: #dcfce7;
            }
            QLabel#pressureState[level="medium"] {
                border-color: #f59e0b;
                background-color: #78350f;
                color: #fef3c7;
            }
            QLabel#pressureState[level="heavy"],
            QLabel#pressureState[level="error"] {
                border-color: #fb7185;
                background-color: #7f1d1d;
                color: #ffe4e6;
            }
            QLabel#notice, QLabel#note {
                min-height: 54px;
                padding: 7px;
                border: 1px solid #2e7494;
                border-radius: 13px;
                background-color: #123249;
                color: #bae6fd;
                font-size: 19px;
            }
            QLabel#note {
                border-color: #334155;
                background-color: #172238;
                color: #cbd5e1;
            }
            QLabel#error {
                color: #fda4af;
                font-size: 19px;
            }
            QPushButton#exitButton, QPushButton#resetButton,
            QPushButton#ttsButton {
                min-width: 126px;
                min-height: 58px;
                border: 2px solid #40516a;
                border-radius: 14px;
                background-color: #23344d;
                color: #ffffff;
                font-size: 21px;
                font-weight: 700;
            }
            QPushButton#ttsButton {
                min-width: 220px;
                background-color: #25334a;
                border-color: #52647f;
            }
            QPushButton#ttsButton[active="true"] {
                background-color: #14532d;
                border-color: #22c55e;
                color: #dcfce7;
            }
            QPushButton#ttsButton:disabled {
                background-color: #273244;
                border-color: #475569;
                color: #94a3b8;
            }
            QPushButton#exitButton {
                background-color: #702b37;
                border-color: #a34151;
            }
            QPushButton:pressed {
                background-color: #314863;
            }
        )"));
    }

    QString rawPath() const
    {
        const QString override = qEnvironmentVariable("PRESSUREMONITOR_ADC_PATH");
        return override.isEmpty()
            ? QStringLiteral("/sys/bus/iio/devices/iio:device0/in_voltage1_raw")
            : override;
    }

    QString scalePath() const
    {
        const QString override = qEnvironmentVariable("PRESSUREMONITOR_SCALE_PATH");
        return override.isEmpty()
            ? QStringLiteral("/sys/bus/iio/devices/iio:device0/in_voltage_scale")
            : override;
    }

    QString speechProgram() const
    {
        const QString override = qEnvironmentVariable("PRESSUREMONITOR_TTS_PROGRAM");
        return override.isEmpty() ? QStringLiteral("/usr/bin/espeak") : override;
    }

    void setupSpeech()
    {
        connect(&m_ttsProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (!m_pendingEnableAnnouncement || !m_speechEnabled)
                    return;
                m_pendingEnableAnnouncement = false;
                speak(QString::fromUtf8("语音播报已开启"), true);
            });
        connect(&m_ttsProcess, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                m_pendingEnableAnnouncement = false;
                m_speechEnabled = false;
                m_speechAvailable = false;
                updateSpeechButton();
                showSpeechUnavailable(QString::fromUtf8(
                    "语音程序启动失败：%1").arg(speechProgram()));
            });

        const QFileInfo program(speechProgram());
        m_speechAvailable = program.exists() && program.isFile()
            && program.isExecutable();
        if (!m_speechAvailable) {
            showSpeechUnavailable(QString::fromUtf8(
                "未找到可执行的语音程序：%1").arg(speechProgram()));
        }
        updateSpeechButton();
    }

    void showSpeechUnavailable(const QString &message)
    {
        if (!m_ttsButton)
            return;
        m_ttsButton->setToolTip(message);
        m_ttsButton->setAccessibleDescription(message);
        qWarning("%s", qPrintable(message));
    }

    void updateSpeechButton()
    {
        if (!m_ttsButton)
            return;
        m_ttsButton->setEnabled(m_speechAvailable);
        m_ttsButton->setText(m_speechAvailable
            ? (m_speechEnabled ? QString::fromUtf8("语音播报：开启")
                               : QString::fromUtf8("语音播报：关闭"))
            : QString::fromUtf8("语音播报：不可用"));
        m_ttsButton->setProperty("active", m_speechEnabled);
        m_ttsButton->style()->unpolish(m_ttsButton);
        m_ttsButton->style()->polish(m_ttsButton);
        m_ttsButton->update();
    }

    void setSpeechEnabled(bool enabled)
    {
        if (enabled && !m_speechAvailable)
            return;

        m_speechEnabled = enabled;
        m_voiceCandidateCount = 0;
        m_lastSpokenState = PressureState::Released;
        m_pendingEnableAnnouncement = false;
        updateSpeechButton();

        if (!enabled) {
            if (m_ttsProcess.state() != QProcess::NotRunning)
                m_ttsProcess.kill();
            return;
        }

        if (!speak(QString::fromUtf8("语音播报已开启"), true))
            m_pendingEnableAnnouncement = true;
    }

    bool speak(const QString &text, bool ignoreCooldown = false)
    {
        if (!m_speechEnabled || !m_speechAvailable
            || m_ttsProcess.state() != QProcess::NotRunning) {
            return false;
        }
        if (!ignoreCooldown && m_lastSpeechTimer.isValid()
            && m_lastSpeechTimer.elapsed() < kVoiceCooldownMs) {
            return false;
        }

        m_ttsProcess.start(speechProgram(), {
            QStringLiteral("-v"), QStringLiteral("zh"),
            QStringLiteral("-s"), QStringLiteral("145"), text
        });
        m_lastSpeechTimer.restart();
        return true;
    }

    void handleSpeechSample(PressureState state)
    {
        if (state != m_voiceCandidateState) {
            m_voiceCandidateState = state;
            m_voiceCandidateCount = 1;
        } else if (m_voiceCandidateCount < kVoiceStableSamples) {
            ++m_voiceCandidateCount;
        }

        if (m_voiceCandidateCount < kVoiceStableSamples)
            return;

        if (state == PressureState::Released) {
            m_lastSpokenState = PressureState::Released;
            return;
        }
        if (!m_speechEnabled || state == m_lastSpokenState)
            return;

        QString text;
        switch (state) {
        case PressureState::Light:
            text = QString::fromUtf8("轻按");
            break;
        case PressureState::Medium:
            text = QString::fromUtf8("中等压力");
            break;
        case PressureState::Heavy:
            text = QString::fromUtf8("压力较大");
            break;
        case PressureState::Released:
            return;
        }
        if (speak(text))
            m_lastSpokenState = state;
    }

    void inspectHardware()
    {
        m_hardwareReady = QFile::exists(rawPath());
        bool scaleOk = false;
        const double scale = readText(scalePath()).toDouble(&scaleOk);
        m_scaleReady = scaleOk && scale > 0.0;
        if (m_scaleReady)
            m_scaleMillivolts = scale;

        if (!m_hardwareReady) {
            m_errorLabel->setText(QString::fromUtf8("未找到 N1 ADC：%1").arg(rawPath()));
        } else if (!m_scaleReady) {
            m_errorLabel->setText(QString::fromUtf8(
                "ADC 原始值可读，但 scale 不可用，暂不显示电压。"));
        } else {
            m_errorLabel->clear();
        }
    }

    void sample()
    {
        if (!m_hardwareReady) {
            inspectHardware();
            if (!m_hardwareReady) {
                setState(QString::fromUtf8("压力输入不可用"), QStringLiteral("error"));
                return;
            }
        }

        bool ok = false;
        const int raw = readText(rawPath()).toInt(&ok);
        if (!ok) {
            m_errorLabel->setText(QString::fromUtf8("N1 ADC 读取失败。"));
            setState(QString::fromUtf8("压力输入不可用"), QStringLiteral("error"));
            return;
        }
        if (m_scaleReady)
            m_errorLabel->clear();

        m_currentRaw = qBound(0, raw, kAdcMaximum);
        m_filterWindow.append(m_currentRaw);
        if (m_filterWindow.size() > 5)
            m_filterWindow.removeFirst();
        QVector<int> sorted = m_filterWindow;
        std::sort(sorted.begin(), sorted.end());
        const int median = sorted.at(sorted.size() / 2);
        if (!m_filterReady) {
            m_filtered = median;
            m_filterReady = true;
        } else {
            m_filtered = 0.65 * m_filtered + 0.35 * median;
        }
        updateValues();

        if (!m_calibrated) {
            m_calibrationSum += median;
            ++m_calibrationCount;
            m_baselineLabel->setText(QString::fromUtf8(
                "零点校准 %1 / %2：请保持完全松开")
                .arg(m_calibrationCount).arg(kCalibrationSamples));
            setState(QString::fromUtf8("正在校准"), QStringLiteral("calibrating"));
            m_percentLabel->setText(QString::fromUtf8("相对力度 --"));
            if (m_calibrationCount < kCalibrationSamples)
                return;

            m_baseline = qBound(0, static_cast<int>(std::lround(
                static_cast<double>(m_calibrationSum) / m_calibrationCount)),
                kAdcMaximum - 1);
            m_calibrated = true;
            m_baselineLabel->setText(QString::fromUtf8(
                "零点基线 %1 · 100 ms/点 · 曲线窗口约 30 秒")
                .arg(m_baseline));
        }

        const double span = std::max(1, kAdcMaximum - m_baseline);
        const double relative = qBound(0.0,
            100.0 * (m_filtered - m_baseline) / span, 100.0);
        m_percentLabel->setText(QString::fromUtf8("相对力度 %1%")
            .arg(relative, 0, 'f', 1));
        const PressureState pressureState = updatePressureState(relative);
        handleSpeechSample(pressureState);

        m_history.append(relative);
        if (m_history.size() > kHistorySamples)
            m_history.remove(0, m_history.size() - kHistorySamples);
        m_chart->setValues(m_history);
    }

    void updateValues()
    {
        const QString voltage = m_scaleReady
            ? QString::number(m_currentRaw * m_scaleMillivolts / 1000.0, 'f', 3)
            : QStringLiteral("--");
        m_valueLabel->setText(QString::fromUtf8(
            "原始值 %1 / 8191    平滑值 %2    电压 %3 V")
            .arg(m_currentRaw)
            .arg(static_cast<int>(std::lround(m_filtered)))
            .arg(voltage));
    }

    PressureState updatePressureState(double relative)
    {
        if (relative < 3.0) {
            setState(QString::fromUtf8("○ 松开"), QStringLiteral("released"));
            return PressureState::Released;
        }
        if (relative < 25.0) {
            setState(QString::fromUtf8("● 轻按"), QStringLiteral("light"));
            return PressureState::Light;
        }
        if (relative < 60.0) {
            setState(QString::fromUtf8("● 中等"), QStringLiteral("medium"));
            return PressureState::Medium;
        }
        setState(QString::fromUtf8("● 重按"), QStringLiteral("heavy"));
        return PressureState::Heavy;
    }

    void setState(const QString &text, const QString &level)
    {
        m_stateLabel->setText(text);
        if (m_stateLabel->property("level").toString() == level)
            return;
        m_stateLabel->setProperty("level", level);
        m_stateLabel->style()->unpolish(m_stateLabel);
        m_stateLabel->style()->polish(m_stateLabel);
        m_stateLabel->update();
    }

    void resetCalibration()
    {
        m_calibrated = false;
        m_filterReady = false;
        m_filterWindow.clear();
        m_calibrationSum = 0;
        m_calibrationCount = 0;
        m_baseline = 0;
        m_history.clear();
        m_voiceCandidateState = PressureState::Released;
        m_voiceCandidateCount = 0;
        m_lastSpokenState = PressureState::Released;
        if (m_chart)
            m_chart->setValues(m_history);
        if (m_baselineLabel)
            m_baselineLabel->setText(QString::fromUtf8(
                "零点校准 0 / %1：请保持完全松开").arg(kCalibrationSamples));
        if (m_percentLabel)
            m_percentLabel->setText(QString::fromUtf8("相对力度 --"));
        if (m_stateLabel)
            setState(QString::fromUtf8("正在校准"), QStringLiteral("calibrating"));
    }

    QLabel *m_stateLabel = nullptr;
    QLabel *m_percentLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_baselineLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_ttsButton = nullptr;
    PressureChart *m_chart = nullptr;
    QTimer m_sampleTimer;
    QProcess m_ttsProcess;
    QElapsedTimer m_lastSpeechTimer;
    QVector<double> m_history;
    QVector<int> m_filterWindow;
    qint64 m_calibrationSum = 0;
    int m_calibrationCount = 0;
    int m_baseline = 0;
    int m_currentRaw = 0;
    double m_filtered = 0.0;
    double m_scaleMillivolts = 0.0;
    bool m_hardwareReady = false;
    bool m_scaleReady = false;
    bool m_calibrated = false;
    bool m_filterReady = false;
    bool m_speechAvailable = false;
    bool m_speechEnabled = false;
    bool m_pendingEnableAnnouncement = false;
    PressureState m_voiceCandidateState = PressureState::Released;
    PressureState m_lastSpokenState = PressureState::Released;
    int m_voiceCandidateCount = 0;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("pressuremonitor"));

    QFont font = app.font();
    font.setPixelSize(21);
    app.setFont(font);

    PressureWindow window;
    window.setFixedSize(720, 1280);
    const QString screenshotPath = qEnvironmentVariable("PRESSUREMONITOR_SCREENSHOT");
    if (screenshotPath.isEmpty()) {
        window.showFullScreen();
    } else {
        window.show();
        bool delayOk = false;
        int delayMs = qEnvironmentVariableIntValue(
            "PRESSUREMONITOR_SCREENSHOT_DELAY_MS", &delayOk);
        if (!delayOk || delayMs <= 0)
            delayMs = 2500;
        QTimer::singleShot(delayMs, &window, [&window, screenshotPath]() {
            if (!window.grab().save(screenshotPath, "PNG"))
                qCritical("Failed to save pressure monitor screenshot.");
        });
    }

    bool autoExitOk = false;
    const int autoExitMs = qEnvironmentVariableIntValue(
        "PRESSUREMONITOR_AUTO_EXIT_MS", &autoExitOk);
    if (autoExitOk && autoExitMs > 0)
        QTimer::singleShot(autoExitMs, &app, []() { QCoreApplication::exit(0); });

    return app.exec();
}
