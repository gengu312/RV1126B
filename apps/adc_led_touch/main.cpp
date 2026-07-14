#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFont>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

class AdcLedWindow final : public QWidget
{
public:
    AdcLedWindow()
        : m_ledRoot(qEnvironmentVariable("LED_PATH", "/sys/class/leds/work"))
        , m_iioRoot(qEnvironmentVariable("IIO_DEVICE", "/sys/bus/iio/devices/iio:device0"))
    {
        setWindowTitle(QString::fromUtf8("ADC调速灯"));
        setWindowFlags(Qt::FramelessWindowHint);
        setupUi();

        const QString triggerText = readText(m_ledRoot + "/trigger");
        const QRegularExpressionMatch match =
            QRegularExpression("\\[([^\\]]+)\\]").match(triggerText);
        m_originalTrigger = match.hasMatch() ? match.captured(1) : QStringLiteral("none");
        m_originalBrightness = readText(m_ledRoot + "/brightness").trimmed();

        bool scaleOk = false;
        const double detectedScale = readText(m_iioRoot + "/in_voltage_scale").toDouble(&scaleOk);
        if (scaleOk)
            m_scaleMillivolts = detectedScale;

        m_hardwareReady = QFile::exists(m_ledRoot + "/trigger")
            && QFile::exists(m_ledRoot + "/brightness")
            && QFile::exists(m_iioRoot + "/in_voltage4_raw");

        connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
            if (!setLed(!m_ledOn))
                m_blinkTimer.stop();
        });
        connect(&m_adcTimer, &QTimer::timeout, this, [this]() { updateAdc(); });

        if (!m_hardwareReady) {
            m_errorLabel->setText(QString::fromUtf8("找不到LED或ADC接口，请检查系统驱动。"));
            for (const auto &entry : m_modeButtons)
                entry.second->setEnabled(false);
            m_speedSlider->setEnabled(false);
            return;
        }

        if (!writeText(m_ledRoot + "/trigger", "none\n")) {
            m_errorLabel->setText(QString::fromUtf8("无法接管用户灯，请检查权限。"));
            return;
        }

        setMode(Mode::Off);
        updateAdc();
        m_adcTimer.start(100);
    }

    ~AdcLedWindow() override
    {
        restoreLed();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        restoreLed();
        event->accept();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    enum class Mode {
        Off,
        On,
        Slow,
        Fast,
        Touch,
        Adc
    };

    void setupUi()
    {
        setStyleSheet(QStringLiteral(R"(
            QWidget {
                background-color: #0f172a;
                color: #f8fafc;
            }
            QLabel#title {
                font-size: 44px;
                font-weight: 700;
                color: #7dd3fc;
            }
            QLabel#adcValue {
                font-size: 34px;
                font-weight: 600;
            }
            QLabel#modeValue {
                font-size: 27px;
                color: #cbd5e1;
            }
            QLabel#hint {
                font-size: 22px;
                color: #94a3b8;
            }
            QLabel#error {
                font-size: 22px;
                color: #fca5a5;
            }
            QProgressBar {
                min-height: 54px;
                border: 2px solid #334155;
                border-radius: 16px;
                background-color: #1e293b;
            }
            QProgressBar::chunk {
                border-radius: 13px;
                background-color: #22c55e;
            }
            QSlider {
                min-height: 70px;
            }
            QSlider::groove:horizontal {
                height: 22px;
                border-radius: 11px;
                background-color: #334155;
            }
            QSlider::sub-page:horizontal {
                border-radius: 11px;
                background-color: #38bdf8;
            }
            QSlider::handle:horizontal {
                width: 56px;
                margin: -18px 0;
                border: 4px solid #0284c7;
                border-radius: 28px;
                background-color: #f8fafc;
            }
            QPushButton {
                min-height: 108px;
                border: 2px solid #475569;
                border-radius: 20px;
                background-color: #1e293b;
                color: #f8fafc;
                font-size: 28px;
                font-weight: 600;
            }
            QPushButton:pressed {
                background-color: #334155;
            }
            QPushButton[active="true"] {
                border-color: #38bdf8;
                background-color: #0369a1;
            }
            QPushButton#exitButton {
                border-color: #7f1d1d;
                background-color: #991b1b;
            }
        )"));

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(42, 36, 42, 36);
        root->setSpacing(24);

        auto *title = new QLabel(QString::fromUtf8("ADC 调速灯"), this);
        title->setObjectName(QStringLiteral("title"));
        title->setAlignment(Qt::AlignCenter);
        root->addWidget(title);

        m_adcLabel = new QLabel(this);
        m_adcLabel->setObjectName(QStringLiteral("adcValue"));
        m_adcLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_adcLabel);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 8191);
        m_progress->setTextVisible(false);
        root->addWidget(m_progress);

        m_modeLabel = new QLabel(this);
        m_modeLabel->setObjectName(QStringLiteral("modeValue"));
        m_modeLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_modeLabel);

        m_sliderLabel = new QLabel(this);
        m_sliderLabel->setObjectName(QStringLiteral("hint"));
        m_sliderLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_sliderLabel);

        m_speedSlider = new QSlider(Qt::Horizontal, this);
        m_speedSlider->setRange(0, 100);
        m_speedSlider->setValue(50);
        m_speedSlider->setPageStep(10);
        updateSliderLabel();
        connect(m_speedSlider, &QSlider::sliderPressed, this, [this]() {
            setMode(Mode::Touch);
        });
        connect(m_speedSlider, &QSlider::valueChanged, this, [this](int) {
            updateSliderLabel();
            applyTouchSpeed();
        });
        root->addWidget(m_speedSlider);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(18);
        addModeButton(grid, QString::fromUtf8("熄灭"), Mode::Off, 0, 0);
        addModeButton(grid, QString::fromUtf8("常亮"), Mode::On, 0, 1);
        addModeButton(grid, QString::fromUtf8("慢闪"), Mode::Slow, 1, 0);
        addModeButton(grid, QString::fromUtf8("快闪"), Mode::Fast, 1, 1);
        addModeButton(grid, QString::fromUtf8("ADC 调速"), Mode::Adc, 2, 0, 1, 2);
        root->addLayout(grid);

        auto *hint = new QLabel(
            QString::fromUtf8("选择“ADC调速”后旋转蓝色电位器：数值越大，闪烁越快。"), this);
        hint->setObjectName(QStringLiteral("hint"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        root->addWidget(hint);

        m_errorLabel = new QLabel(this);
        m_errorLabel->setObjectName(QStringLiteral("error"));
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        root->addWidget(m_errorLabel);

        auto *exitButton = new QPushButton(QString::fromUtf8("退出并恢复LED"), this);
        exitButton->setObjectName(QStringLiteral("exitButton"));
        connect(exitButton, &QPushButton::clicked, this, [this]() { close(); });
        root->addWidget(exitButton);
    }

    void addModeButton(QGridLayout *layout, const QString &text, Mode mode,
        int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        auto *button = new QPushButton(text, this);
        button->setProperty("active", false);
        connect(button, &QPushButton::clicked, this, [this, mode]() { setMode(mode); });
        layout->addWidget(button, row, column, rowSpan, columnSpan);
        m_modeButtons.append(qMakePair(mode, button));
    }

    void setMode(Mode mode)
    {
        if (!m_hardwareReady)
            return;

        m_blinkTimer.stop();
        m_mode = mode;

        switch (mode) {
        case Mode::Off:
            m_halfPeriodMs = 0;
            setLed(false);
            break;
        case Mode::On:
            m_halfPeriodMs = 0;
            setLed(true);
            break;
        case Mode::Slow:
            startBlinking(800);
            break;
        case Mode::Fast:
            startBlinking(150);
            break;
        case Mode::Touch:
            startBlinking(touchHalfPeriod(m_speedSlider->value()));
            break;
        case Mode::Adc:
            startBlinking(adcHalfPeriod(m_currentRaw));
            break;
        }

        refreshModeLabel();
        refreshButtonStyles();
    }

    void startBlinking(int halfPeriodMs)
    {
        m_halfPeriodMs = halfPeriodMs;
        setLed(true);
        m_blinkTimer.start(m_halfPeriodMs);
    }

    void updateAdc()
    {
        bool ok = false;
        const int raw = readText(m_iioRoot + "/in_voltage4_raw").trimmed().toInt(&ok);
        if (!ok) {
            m_errorLabel->setText(QString::fromUtf8("ADC读取失败。"));
            return;
        }

        m_errorLabel->clear();
        m_currentRaw = qBound(0, raw, 8191);
        const double percent = m_currentRaw * 100.0 / 8191.0;
        const double millivolts = m_currentRaw * m_scaleMillivolts;

        m_progress->setValue(m_currentRaw);
        m_adcLabel->setText(QString::fromUtf8("ADC：%1%   raw=%2   %3 mV")
            .arg(percent, 0, 'f', 1)
            .arg(m_currentRaw)
            .arg(millivolts, 0, 'f', 1));

        if (m_mode == Mode::Adc) {
            const int newHalfPeriod = adcHalfPeriod(m_currentRaw);
            if (qAbs(newHalfPeriod - m_halfPeriodMs) >= 20) {
                m_halfPeriodMs = newHalfPeriod;
                m_blinkTimer.setInterval(m_halfPeriodMs);
                refreshModeLabel();
            }
        }
    }

    int adcHalfPeriod(int raw) const
    {
        return 1500 - (1400 * qBound(0, raw, 8191) / 8191);
    }

    int touchHalfPeriod(int value) const
    {
        return 1500 - (14 * qBound(0, value, 100));
    }

    void applyTouchSpeed()
    {
        if (!m_hardwareReady)
            return;

        const int newHalfPeriod = touchHalfPeriod(m_speedSlider->value());
        if (m_mode != Mode::Touch) {
            setMode(Mode::Touch);
            return;
        }

        m_halfPeriodMs = newHalfPeriod;
        m_blinkTimer.setInterval(m_halfPeriodMs);
        refreshModeLabel();
    }

    void updateSliderLabel()
    {
        m_sliderLabel->setText(QString::fromUtf8("触摸调速：慢  ←  %1%  →  快")
            .arg(m_speedSlider->value()));
    }

    void refreshModeLabel()
    {
        QString name;
        switch (m_mode) {
        case Mode::Off: name = QString::fromUtf8("熄灭"); break;
        case Mode::On: name = QString::fromUtf8("常亮"); break;
        case Mode::Slow: name = QString::fromUtf8("慢闪"); break;
        case Mode::Fast: name = QString::fromUtf8("快闪"); break;
        case Mode::Touch: name = QString::fromUtf8("触摸调速"); break;
        case Mode::Adc: name = QString::fromUtf8("ADC调速"); break;
        }

        if (m_halfPeriodMs > 0) {
            m_modeLabel->setText(QString::fromUtf8("模式：%1   半周期：%2 ms")
                .arg(name).arg(m_halfPeriodMs));
        } else {
            m_modeLabel->setText(QString::fromUtf8("模式：%1").arg(name));
        }
    }

    void refreshButtonStyles()
    {
        for (const auto &entry : m_modeButtons) {
            QPushButton *button = entry.second;
            button->setProperty("active", entry.first == m_mode);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }

    bool setLed(bool on)
    {
        if (!writeText(m_ledRoot + "/brightness", on ? "1\n" : "0\n")) {
            m_errorLabel->setText(QString::fromUtf8("LED写入失败。"));
            return false;
        }
        m_ledOn = on;
        return true;
    }

    void restoreLed()
    {
        if (m_restored || !m_hardwareReady)
            return;

        m_restored = true;
        m_adcTimer.stop();
        m_blinkTimer.stop();
        if (!m_originalTrigger.isEmpty())
            writeText(m_ledRoot + "/trigger", m_originalTrigger.toUtf8() + '\n');
        if (m_originalTrigger == QStringLiteral("none") && !m_originalBrightness.isEmpty())
            writeText(m_ledRoot + "/brightness", m_originalBrightness.toUtf8() + '\n');
    }

    static QString readText(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll());
    }

    static bool writeText(const QString &path, const QByteArray &value)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(value) == value.size();
    }

    QString m_ledRoot;
    QString m_iioRoot;
    QString m_originalTrigger;
    QString m_originalBrightness;
    QLabel *m_adcLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_sliderLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QSlider *m_speedSlider = nullptr;
    QVector<QPair<Mode, QPushButton *>> m_modeButtons;
    QTimer m_adcTimer;
    QTimer m_blinkTimer;
    Mode m_mode = Mode::Off;
    int m_currentRaw = 0;
    int m_halfPeriodMs = 0;
    double m_scaleMillivolts = 0.219726562;
    bool m_ledOn = false;
    bool m_hardwareReady = false;
    bool m_restored = false;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("adcled"));

    QFont font = app.font();
    font.setPixelSize(26);
    app.setFont(font);

    AdcLedWindow window;
    window.showFullScreen();

    bool autoExitOk = false;
    const int autoExitMs = qEnvironmentVariableIntValue("ADCLED_AUTO_EXIT_MS", &autoExitOk);
    if (autoExitOk && autoExitMs > 0)
        QTimer::singleShot(autoExitMs, &app, &QApplication::quit);

    return app.exec();
}
