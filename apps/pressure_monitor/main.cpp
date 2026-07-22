#include <QApplication>
#include <QCloseEvent>
#include <QDir>
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
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
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
constexpr int kDefaultVoiceCooldownMs = 2000;
constexpr int kDefaultLightThreshold = 3;
constexpr int kDefaultMediumThreshold = 25;
constexpr int kDefaultHeavyThreshold = 60;

enum class PressureState {
    Released,
    Light,
    Medium,
    Heavy
};

enum class AudioMode {
    TextToSpeech,
    Clips
};

enum class PlaybackKind {
    None,
    TextToSpeech,
    Clip
};

struct PlayerCommand {
    QString program;
    QStringList arguments;

    bool isValid() const { return !program.isEmpty(); }
};

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QString executablePath(const QString &candidate)
{
    if (candidate.isEmpty())
        return {};
    const QFileInfo info(candidate);
    if (info.isAbsolute() || candidate.contains(QLatin1Char('/'))
        || candidate.contains(QLatin1Char('\\'))) {
        return info.exists() && info.isFile() && info.isExecutable()
            ? info.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(candidate);
}

QString stateSpeech(PressureState state)
{
    switch (state) {
    case PressureState::Light:
        return QString::fromUtf8("轻按");
    case PressureState::Medium:
        return QString::fromUtf8("中等压力");
    case PressureState::Heavy:
        return QString::fromUtf8("压力较大");
    case PressureState::Released:
        return {};
    }
    return {};
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
        // The audio controls share the fixed 720x1280 screen with the chart.
        // Keep enough room for the graph without forcing the bottom controls off-screen.
        setMinimumHeight(320);
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
        reloadConfiguration(true);
        setupAudio();
        inspectHardware();
        resetCalibration();
        connect(&m_sampleTimer, &QTimer::timeout, this, [this]() { sample(); });
        m_sampleTimer.start(kSampleIntervalMs);
        connect(&m_configTimer, &QTimer::timeout,
            this, [this]() { reloadConfiguration(false); });
        m_configTimer.start(1000);
    }

    ~PressureWindow() override
    {
        stopPlayback();
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

    void closeEvent(QCloseEvent *event) override
    {
        stopPlayback();
        QWidget::closeEvent(event);
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

        auto *audioPanel = makePanel(this);
        auto *audioLayout = new QVBoxLayout(audioPanel);
        audioLayout->setContentsMargins(16, 10, 16, 10);
        audioLayout->setSpacing(7);
        auto *audioButtons = new QHBoxLayout;
        auto *audioTitle = new QLabel(QString::fromUtf8("声音反馈"), audioPanel);
        audioTitle->setObjectName(QStringLiteral("sectionTitle"));
        m_ttsButton = new QPushButton(QString::fromUtf8("声音反馈：关闭"), audioPanel);
        m_ttsButton->setObjectName(QStringLiteral("ttsButton"));
        m_ttsButton->setProperty("active", false);
        connect(m_ttsButton, &QPushButton::clicked,
            this, [this]() { setFeedbackEnabled(!m_feedbackEnabled, true); });
        m_modeButton = new QPushButton(QString::fromUtf8("模式：语音合成"), audioPanel);
        m_modeButton->setObjectName(QStringLiteral("modeButton"));
        connect(m_modeButton, &QPushButton::clicked,
            this, [this]() {
                setAudioMode(m_audioMode == AudioMode::TextToSpeech
                    ? AudioMode::Clips : AudioMode::TextToSpeech, true);
            });
        audioButtons->addWidget(audioTitle);
        audioButtons->addStretch();
        audioButtons->addWidget(m_ttsButton);
        audioButtons->addWidget(m_modeButton);
        audioLayout->addLayout(audioButtons);
        m_audioStatusLabel = new QLabel(audioPanel);
        m_audioStatusLabel->setObjectName(QStringLiteral("audioStatus"));
        m_audioStatusLabel->setWordWrap(true);
        audioLayout->addWidget(m_audioStatusLabel);
        root->addWidget(audioPanel);

        auto *chartHeader = new QHBoxLayout;
        auto *chartTitle = new QLabel(QString::fromUtf8("相对力度曲线"), this);
        chartTitle->setObjectName(QStringLiteral("sectionTitle"));
        auto *resetButton = new QPushButton(QString::fromUtf8("重新校准"), this);
        resetButton->setObjectName(QStringLiteral("resetButton"));
        connect(resetButton, &QPushButton::clicked,
            this, [this]() { resetCalibration(); });
        chartHeader->addWidget(chartTitle);
        chartHeader->addStretch();
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
            QLabel#audioStatus {
                min-height: 64px;
                color: #cbd5e1;
                font-size: 17px;
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
            QPushButton#ttsButton, QPushButton#modeButton {
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
                min-width: 190px;
                background-color: #25334a;
                border-color: #52647f;
            }
            QPushButton#modeButton {
                min-width: 190px;
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

    QString configPath() const
    {
        const QString override = qEnvironmentVariable("PRESSUREMONITOR_CONFIG_PATH");
        return override.isEmpty()
            ? QStringLiteral("/userdata/pressure_monitor/config.ini") : override;
    }

    QString resolvedAudioPath(const QString &configuredPath) const
    {
        if (configuredPath.isEmpty())
            return {};
        const QFileInfo info(configuredPath);
        if (info.isAbsolute())
            return info.absoluteFilePath();
        return QDir(QFileInfo(configPath()).absolutePath())
            .absoluteFilePath(configuredPath);
    }

    static bool parseBoolean(const QVariant &value, bool defaultValue, bool *valid)
    {
        const QString text = value.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("1")
            || text == QStringLiteral("yes") || text == QStringLiteral("on")) {
            *valid = true;
            return true;
        }
        if (text == QStringLiteral("false") || text == QStringLiteral("0")
            || text == QStringLiteral("no") || text == QStringLiteral("off")) {
            *valid = true;
            return false;
        }
        *valid = false;
        return defaultValue;
    }

    void reloadConfiguration(bool force)
    {
        QFile file(configPath());
        const bool exists = file.exists();
        QByteArray contents;
        if (exists && file.open(QIODevice::ReadOnly))
            contents = file.readAll();
        if (!force && exists == m_configFileExists && contents == m_configSnapshot) {
            refreshAudioAvailability();
            return;
        }

        const AudioMode oldMode = m_audioMode;
        const bool oldEnabled = m_feedbackEnabled;
        const QString oldLight = m_lightClipPath;
        const QString oldMedium = m_mediumClipPath;
        const QString oldHeavy = m_heavyClipPath;

        m_configFileExists = exists;
        m_configSnapshot = contents;
        m_configIssue.clear();

        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        bool invalid = false;

        const QString mode = settings.value(QStringLiteral("audio/mode"),
            QStringLiteral("tts")).toString().trimmed().toLower();
        if (mode == QStringLiteral("clips")) {
            m_audioMode = AudioMode::Clips;
        } else {
            m_audioMode = AudioMode::TextToSpeech;
            if (mode != QStringLiteral("tts"))
                invalid = true;
        }

        bool boolOk = false;
        m_feedbackEnabled = parseBoolean(settings.value(
            QStringLiteral("audio/enabled"), false), false, &boolOk);
        invalid = invalid || !boolOk;
        m_fallbackTts = parseBoolean(settings.value(
            QStringLiteral("audio/fallback_tts"), true), true, &boolOk);
        invalid = invalid || !boolOk;

        m_lightClipPath = resolvedAudioPath(settings.value(
            QStringLiteral("audio/light"),
            QStringLiteral("/userdata/pressure_monitor/audio/light.wav"))
            .toString().trimmed());
        m_mediumClipPath = resolvedAudioPath(settings.value(
            QStringLiteral("audio/medium"),
            QStringLiteral("/userdata/pressure_monitor/audio/medium.wav"))
            .toString().trimmed());
        m_heavyClipPath = resolvedAudioPath(settings.value(
            QStringLiteral("audio/heavy"),
            QStringLiteral("/userdata/pressure_monitor/audio/heavy.wav"))
            .toString().trimmed());

        bool volumeOk = false;
        const int volume = settings.value(QStringLiteral("audio/volume_percent"),
            100).toInt(&volumeOk);
        m_volumePercent = volumeOk && volume >= 0 && volume <= 100 ? volume : 100;
        invalid = invalid || !volumeOk || volume < 0 || volume > 100;

        bool lightOk = false;
        bool mediumOk = false;
        bool heavyOk = false;
        const int light = settings.value(QStringLiteral("pressure/light_threshold"),
            kDefaultLightThreshold).toInt(&lightOk);
        const int medium = settings.value(QStringLiteral("pressure/medium_threshold"),
            kDefaultMediumThreshold).toInt(&mediumOk);
        const int heavy = settings.value(QStringLiteral("pressure/heavy_threshold"),
            kDefaultHeavyThreshold).toInt(&heavyOk);
        if (lightOk && mediumOk && heavyOk && light > 0 && light < medium
            && medium < heavy && heavy <= 100) {
            m_lightThreshold = light;
            m_mediumThreshold = medium;
            m_heavyThreshold = heavy;
        } else {
            m_lightThreshold = kDefaultLightThreshold;
            m_mediumThreshold = kDefaultMediumThreshold;
            m_heavyThreshold = kDefaultHeavyThreshold;
            invalid = true;
        }

        bool cooldownOk = false;
        const int cooldown = settings.value(QStringLiteral("pressure/cooldown_ms"),
            kDefaultVoiceCooldownMs).toInt(&cooldownOk);
        m_voiceCooldownMs = cooldownOk && cooldown >= 0 && cooldown <= 60000
            ? cooldown : kDefaultVoiceCooldownMs;
        invalid = invalid || !cooldownOk || cooldown < 0 || cooldown > 60000;

        if (settings.status() != QSettings::NoError) {
            m_audioMode = AudioMode::TextToSpeech;
            m_feedbackEnabled = false;
            m_fallbackTts = true;
            m_lightClipPath = QStringLiteral("/userdata/pressure_monitor/audio/light.wav");
            m_mediumClipPath = QStringLiteral("/userdata/pressure_monitor/audio/medium.wav");
            m_heavyClipPath = QStringLiteral("/userdata/pressure_monitor/audio/heavy.wav");
            m_volumePercent = 100;
            m_lightThreshold = kDefaultLightThreshold;
            m_mediumThreshold = kDefaultMediumThreshold;
            m_heavyThreshold = kDefaultHeavyThreshold;
            m_voiceCooldownMs = kDefaultVoiceCooldownMs;
        }
        if (settings.status() == QSettings::FormatError) {
            m_configIssue = QString::fromUtf8("格式错误，已使用安全默认值");
        } else if (settings.status() == QSettings::AccessError) {
            m_configIssue = QString::fromUtf8("读取失败，已使用安全默认值");
        } else if (invalid) {
            m_configIssue = QString::fromUtf8("部分配置无效，已使用默认值");
        } else if (!exists) {
            m_configIssue = QString::fromUtf8("文件未创建，当前使用默认值");
        }

        const bool playbackConfigChanged = m_configLoaded
            && (oldMode != m_audioMode || oldEnabled != m_feedbackEnabled
                || oldLight != m_lightClipPath || oldMedium != m_mediumClipPath
                || oldHeavy != m_heavyClipPath);
        if (playbackConfigChanged)
            stopPlayback();
        if (m_configLoaded && (oldMode != m_audioMode || oldEnabled != m_feedbackEnabled))
            resetFeedbackState();
        m_configLoaded = true;

        refreshPrograms();
        refreshAudioAvailability();
    }

    bool persistSetting(const QString &key, const QVariant &value)
    {
        const QFileInfo configInfo(configPath());
        if (!QDir().mkpath(configInfo.absolutePath())) {
            m_configIssue = QString::fromUtf8("无法创建配置目录");
            updateAudioUi();
            return false;
        }
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.setValue(key, value);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            m_configIssue = QString::fromUtf8("配置保存失败");
            updateAudioUi();
            return false;
        }
        reloadConfiguration(true);
        return true;
    }

    void setupAudio()
    {
        connect(&m_playbackProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const PlaybackKind finishedKind = m_playbackKind;
                const QString fallbackText = m_currentFallbackText;
                m_playbackKind = PlaybackKind::None;
                m_currentFallbackText.clear();
                if (finishedKind == PlaybackKind::Clip
                    && (exitStatus != QProcess::NormalExit || exitCode != 0)
                    && m_fallbackTts && !fallbackText.isEmpty()) {
                    scheduleTtsFallback(fallbackText);
                    return;
                }
                if (!m_pendingEnableAnnouncement || !m_feedbackEnabled)
                    return;
                m_pendingEnableAnnouncement = false;
                startTts(QString::fromUtf8("语音播报已开启"), true);
            });
        connect(&m_playbackProcess, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                const PlaybackKind failedKind = m_playbackKind;
                const QString failedProgram = m_playbackProcess.program();
                const QString fallbackText = m_currentFallbackText;
                m_playbackKind = PlaybackKind::None;
                m_currentFallbackText.clear();
                if (failedKind == PlaybackKind::TextToSpeech) {
                    m_failedTtsProgram = failedProgram;
                    QTimer::singleShot(5000, this, [this, failedProgram]() {
                        if (m_failedTtsProgram != failedProgram)
                            return;
                        m_failedTtsProgram.clear();
                        refreshAudioAvailability();
                    });
                } else if (failedKind == PlaybackKind::Clip) {
                    m_failedPlayerProgram = failedProgram;
                    QTimer::singleShot(5000, this, [this, failedProgram]() {
                        if (m_failedPlayerProgram != failedProgram)
                            return;
                        m_failedPlayerProgram.clear();
                        refreshAudioAvailability();
                    });
                }
                refreshPrograms();
                refreshAudioAvailability();
                if (failedKind == PlaybackKind::Clip && m_fallbackTts
                    && !fallbackText.isEmpty()) {
                    scheduleTtsFallback(fallbackText);
                } else {
                    m_pendingEnableAnnouncement = false;
                }
            });

        m_playbackProcess.setStandardOutputFile(QProcess::nullDevice());
        m_playbackProcess.setStandardErrorFile(QProcess::nullDevice());
        refreshPrograms();
        refreshAudioAvailability();

        bool autoEnableOk = false;
        const int autoEnable = qEnvironmentVariableIntValue(
            "PRESSUREMONITOR_AUTO_ENABLE_SOUND", &autoEnableOk);
        if (autoEnableOk && autoEnable != 0 && !m_feedbackEnabled) {
            m_feedbackEnabled = true;
            updateAudioUi();
        }
    }

    void refreshPrograms()
    {
        const QString tts = executablePath(speechProgram());
        m_ttsProgram = tts == m_failedTtsProgram ? QString() : tts;

        const QString override = qEnvironmentVariable("PRESSUREMONITOR_AUDIO_PLAYER");
        if (!override.isEmpty()) {
            const QString player = executablePath(override);
            m_playerOverrideProgram = player == m_failedPlayerProgram
                ? QString() : player;
            m_ffplayProgram.clear();
            m_aplayProgram.clear();
        } else {
            m_playerOverrideProgram.clear();
            const QString ffplay = executablePath(QStringLiteral("/usr/bin/ffplay"));
            const QString aplay = executablePath(QStringLiteral("/usr/bin/aplay"));
            m_ffplayProgram = ffplay == m_failedPlayerProgram ? QString() : ffplay;
            m_aplayProgram = aplay == m_failedPlayerProgram ? QString() : aplay;
        }
    }

    PlayerCommand playerCommand(const QString &path) const
    {
        const bool wav = QFileInfo(path).suffix().compare(
            QStringLiteral("wav"), Qt::CaseInsensitive) == 0;
        QString program;
        bool useAplay = false;
        if (!qEnvironmentVariable("PRESSUREMONITOR_AUDIO_PLAYER").isEmpty()) {
            program = m_playerOverrideProgram;
            useAplay = QFileInfo(program).fileName().contains(
                QStringLiteral("aplay"), Qt::CaseInsensitive);
            if (useAplay && !wav)
                return {};
        } else if (!m_ffplayProgram.isEmpty()) {
            program = m_ffplayProgram;
        } else if (wav && !m_aplayProgram.isEmpty()) {
            program = m_aplayProgram;
            useAplay = true;
        }
        if (program.isEmpty())
            return {};
        if (useAplay)
            return { program, { QStringLiteral("-q"), path } };
        return { program, {
            QStringLiteral("-nodisp"), QStringLiteral("-autoexit"),
            QStringLiteral("-loglevel"), QStringLiteral("quiet"),
            QStringLiteral("-volume"), QString::number(m_volumePercent),
            QStringLiteral("-i"), path
        } };
    }

    QString clipPath(PressureState state) const
    {
        switch (state) {
        case PressureState::Light:
            return m_lightClipPath;
        case PressureState::Medium:
            return m_mediumClipPath;
        case PressureState::Heavy:
            return m_heavyClipPath;
        case PressureState::Released:
            return {};
        }
        return {};
    }

    bool isClipAvailable(PressureState state) const
    {
        const QString path = clipPath(state);
        const QFileInfo file(path);
        return !path.isEmpty() && file.exists() && file.isFile()
            && file.isReadable() && playerCommand(path).isValid();
    }

    void refreshAudioAvailability()
    {
        refreshPrograms();
        m_speechAvailable = !m_ttsProgram.isEmpty();
        m_lightClipAvailable = isClipAvailable(PressureState::Light);
        m_mediumClipAvailable = isClipAvailable(PressureState::Medium);
        m_heavyClipAvailable = isClipAvailable(PressureState::Heavy);
        updateAudioUi();
    }

    bool feedbackAvailable() const
    {
        if (m_audioMode == AudioMode::TextToSpeech)
            return m_speechAvailable;
        return m_lightClipAvailable || m_mediumClipAvailable || m_heavyClipAvailable
            || (m_fallbackTts && m_speechAvailable);
    }

    void updateAudioUi()
    {
        if (!m_ttsButton || !m_modeButton || !m_audioStatusLabel)
            return;
        m_ttsButton->setEnabled(m_feedbackEnabled || feedbackAvailable());
        m_ttsButton->setText(m_feedbackEnabled
            ? QString::fromUtf8("声音反馈：开启")
            : (feedbackAvailable() ? QString::fromUtf8("声音反馈：关闭")
                                   : QString::fromUtf8("声音反馈：不可用")));
        m_ttsButton->setProperty("active", m_feedbackEnabled);
        m_ttsButton->style()->unpolish(m_ttsButton);
        m_ttsButton->style()->polish(m_ttsButton);
        m_ttsButton->update();

        m_modeButton->setText(m_audioMode == AudioMode::Clips
            ? QString::fromUtf8("模式：音频片段")
            : QString::fromUtf8("模式：语音合成"));

        QString playerName = QString::fromUtf8("不可用");
        if (!m_playerOverrideProgram.isEmpty())
            playerName = QFileInfo(m_playerOverrideProgram).fileName();
        else if (!m_ffplayProgram.isEmpty())
            playerName = QStringLiteral("ffplay");
        else if (!m_aplayProgram.isEmpty())
            playerName = QStringLiteral("aplay（仅 WAV）");
        const QString configState = m_configIssue.isEmpty()
            ? QString::fromUtf8("已加载") : m_configIssue;
        m_audioStatusLabel->setText(QString::fromUtf8(
            "素材：轻按 %1 · 中等 %2 · 重按 %3　TTS %4 · 播放器 %5\n"
            "配置：%6（%7）")
            .arg(m_lightClipAvailable ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"))
            .arg(m_mediumClipAvailable ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"))
            .arg(m_heavyClipAvailable ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"))
            .arg(m_speechAvailable ? QString::fromUtf8("可用") : QString::fromUtf8("不可用"))
            .arg(playerName, configPath(), configState));
        m_audioStatusLabel->setToolTip(QString::fromUtf8(
            "轻按：%1\n中等：%2\n重按：%3")
            .arg(m_lightClipPath, m_mediumClipPath, m_heavyClipPath));
    }

    void resetFeedbackState()
    {
        m_voiceCandidateState = PressureState::Released;
        m_voiceCandidateCount = 0;
        m_lastFeedbackState = PressureState::Released;
        m_pendingEnableAnnouncement = false;
    }

    void setAudioMode(AudioMode mode, bool persist)
    {
        if (mode == m_audioMode)
            return;
        stopPlayback();
        if (persist) {
            if (!persistSetting(QStringLiteral("audio/mode"),
                    mode == AudioMode::Clips ? QStringLiteral("clips")
                                             : QStringLiteral("tts"))) {
                m_audioMode = mode;
            }
        } else {
            m_audioMode = mode;
        }
        resetFeedbackState();
        refreshAudioAvailability();
    }

    void setFeedbackEnabled(bool enabled, bool persist)
    {
        if (enabled && !feedbackAvailable())
            return;
        stopPlayback();
        if (persist) {
            if (!persistSetting(QStringLiteral("audio/enabled"), enabled))
                m_feedbackEnabled = enabled;
        } else {
            m_feedbackEnabled = enabled;
        }
        resetFeedbackState();
        updateAudioUi();

        if (!enabled) {
            return;
        }

        if (m_audioMode == AudioMode::TextToSpeech && m_speechAvailable
            && !startTts(QString::fromUtf8("语音播报已开启"), true)) {
            m_pendingEnableAnnouncement = true;
        }
    }

    void stopPlayback()
    {
        m_pendingEnableAnnouncement = false;
        m_currentFallbackText.clear();
        m_playbackKind = PlaybackKind::None;
        if (m_playbackProcess.state() != QProcess::NotRunning)
            m_playbackProcess.kill();
    }

    bool cooldownReady(bool ignoreCooldown) const
    {
        return ignoreCooldown || !m_lastSpeechTimer.isValid()
            || m_lastSpeechTimer.elapsed() >= m_voiceCooldownMs;
    }

    bool startTts(const QString &text, bool ignoreCooldown = false)
    {
        if (!m_feedbackEnabled || !m_speechAvailable
            || m_playbackProcess.state() != QProcess::NotRunning
            || !cooldownReady(ignoreCooldown)) {
            return false;
        }

        m_playbackKind = PlaybackKind::TextToSpeech;
        m_currentFallbackText.clear();
        m_playbackProcess.start(m_ttsProgram, {
            QStringLiteral("-v"), QStringLiteral("zh"),
            QStringLiteral("-s"), QStringLiteral("145"), text
        });
        m_lastSpeechTimer.restart();
        return true;
    }

    bool startClip(PressureState state, const QString &fallbackText)
    {
        const QString path = clipPath(state);
        const PlayerCommand command = playerCommand(path);
        if (!m_feedbackEnabled || !command.isValid()
            || !QFileInfo(path).isReadable()
            || m_playbackProcess.state() != QProcess::NotRunning
            || !cooldownReady(false)) {
            return false;
        }

        m_playbackKind = PlaybackKind::Clip;
        m_currentFallbackText = fallbackText;
        m_playbackProcess.start(command.program, command.arguments);
        m_lastSpeechTimer.restart();
        return true;
    }

    void scheduleTtsFallback(const QString &text)
    {
        if (!m_feedbackEnabled || !m_fallbackTts || !m_speechAvailable)
            return;
        QTimer::singleShot(0, this, [this, text]() {
            if (m_feedbackEnabled && m_audioMode == AudioMode::Clips
                && m_fallbackTts && m_speechAvailable) {
                startTts(text, true);
            }
        });
    }

    bool playStateFeedback(PressureState state)
    {
        const QString text = stateSpeech(state);
        if (text.isEmpty())
            return false;
        if (m_audioMode == AudioMode::Clips) {
            if (isClipAvailable(state))
                return startClip(state, text);
            return m_fallbackTts && startTts(text);
        }
        return startTts(text);
    }

    void handleFeedbackSample(PressureState state)
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
            m_lastFeedbackState = PressureState::Released;
            return;
        }
        if (!m_feedbackEnabled || state == m_lastFeedbackState)
            return;

        if (playStateFeedback(state))
            m_lastFeedbackState = state;
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
        handleFeedbackSample(pressureState);

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
        if (relative < m_lightThreshold) {
            setState(QString::fromUtf8("○ 松开"), QStringLiteral("released"));
            return PressureState::Released;
        }
        if (relative < m_mediumThreshold) {
            setState(QString::fromUtf8("● 轻按"), QStringLiteral("light"));
            return PressureState::Light;
        }
        if (relative < m_heavyThreshold) {
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
        m_lastFeedbackState = PressureState::Released;
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
    QLabel *m_audioStatusLabel = nullptr;
    QPushButton *m_ttsButton = nullptr;
    QPushButton *m_modeButton = nullptr;
    PressureChart *m_chart = nullptr;
    QTimer m_sampleTimer;
    QTimer m_configTimer;
    QProcess m_playbackProcess;
    QElapsedTimer m_lastSpeechTimer;
    QVector<double> m_history;
    QVector<int> m_filterWindow;
    QByteArray m_configSnapshot;
    QString m_configIssue;
    QString m_lightClipPath;
    QString m_mediumClipPath;
    QString m_heavyClipPath;
    QString m_ttsProgram;
    QString m_playerOverrideProgram;
    QString m_ffplayProgram;
    QString m_aplayProgram;
    QString m_failedTtsProgram;
    QString m_failedPlayerProgram;
    QString m_currentFallbackText;
    qint64 m_calibrationSum = 0;
    int m_calibrationCount = 0;
    int m_baseline = 0;
    int m_currentRaw = 0;
    int m_volumePercent = 100;
    int m_lightThreshold = kDefaultLightThreshold;
    int m_mediumThreshold = kDefaultMediumThreshold;
    int m_heavyThreshold = kDefaultHeavyThreshold;
    int m_voiceCooldownMs = kDefaultVoiceCooldownMs;
    double m_filtered = 0.0;
    double m_scaleMillivolts = 0.0;
    bool m_hardwareReady = false;
    bool m_scaleReady = false;
    bool m_calibrated = false;
    bool m_filterReady = false;
    bool m_configFileExists = false;
    bool m_configLoaded = false;
    bool m_speechAvailable = false;
    bool m_feedbackEnabled = false;
    bool m_fallbackTts = true;
    bool m_lightClipAvailable = false;
    bool m_mediumClipAvailable = false;
    bool m_heavyClipAvailable = false;
    bool m_pendingEnableAnnouncement = false;
    AudioMode m_audioMode = AudioMode::TextToSpeech;
    PlaybackKind m_playbackKind = PlaybackKind::None;
    PressureState m_voiceCandidateState = PressureState::Released;
    PressureState m_lastFeedbackState = PressureState::Released;
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
