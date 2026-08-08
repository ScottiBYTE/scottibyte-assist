#include "audio_devices.h"
#include "customer_voice_audio.h"
#include "desktop_backend.h"
#include "lan_session.h"
#include "remote_view.h"
#include "wan_signaling_client.h"
#include <QScrollArea>

#if defined(Q_OS_WIN)
#include "windows_desktop_backend.h"
#else
#include "wayland_desktop_backend.h"
#include "x11_desktop_backend.h"
#endif

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHostInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTimer>
#include <QThread>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace
{

QString formattedSupportCode(
    QString code)
{
    code.remove(QChar(' '));
    code = code.trimmed();

    if (code.size() != 6) {
        return code;
    }

    return code.left(3) +
        QStringLiteral(" ") +
        code.mid(3);
}

QUrl configuredAssistServerUrl()
{
    QSettings settings(
        QStringLiteral("ScottiBYTE"),
        QStringLiteral("Assist"));

    QString value =
        settings.value(
            QStringLiteral(
                "connection/serverUrl"),
            QStringLiteral(
                "https://assist.scottibyte.com"))
            .toString()
            .trimmed();

    while (value.endsWith(QChar('/'))) {
        value.chop(1);
    }

    return QUrl(value);
}

QUrl assistWebSocketUrl(
    const QUrl &serverUrl)
{
    QUrl result = serverUrl;

    if (
        result.scheme().compare(
            QStringLiteral("https"),
            Qt::CaseInsensitive) == 0
    ) {
        result.setScheme(
            QStringLiteral("wss"));
    } else {
        result.setScheme(
            QStringLiteral("ws"));
    }

    QString path = result.path();

    if (path.endsWith(QChar('/'))) {
        path.chop(1);
    }

    result.setPath(
        path +
        QStringLiteral("/ws"));

    return result;
}

QString loadProviderCredential(
    QString *errorMessage)
{
    const QString path =
        QStandardPaths::writableLocation(
            QStandardPaths::ConfigLocation) +
        QStringLiteral(
            "/ScottiBYTE/Assist/provider.json");

    QFile file(path);

    if (
        !file.open(
            QIODevice::ReadOnly)
    ) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral(
                    "No provider credential is installed "
                    "on this computer.");
        }

        return {};
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll(),
            &parseError);

    if (
        parseError.error !=
            QJsonParseError::NoError ||
        !document.isObject()
    ) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral(
                    "The installed provider credential "
                    "file is invalid.");
        }

        return {};
    }

    const QString credential =
        document.object()
            .value(
                QStringLiteral(
                    "credential"))
            .toString()
            .trimmed();

    if (credential.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral(
                    "The installed provider credential "
                    "is missing.");
        }

        return {};
    }

    return credential;
}

QLabel *makeLabel(
    const QString &text,
    const QString &objectName = {})
{
    auto *label =
        new QLabel(text);

    label->setObjectName(
        objectName);

    return label;
}

QPushButton *makeButton(
    const QString &text,
    const QString &objectName = {})
{
    auto *button =
        new QPushButton(text);

    button->setObjectName(
        objectName);

    button->setCursor(
        Qt::PointingHandCursor);

    return button;
}

QFrame *makeCard(
    const QString &objectName)
{
    auto *card =
        new QFrame;

    card->setObjectName(
        objectName);

    return card;
}

class UserDismissTrackingFilter final
    : public QObject
{
public:
    explicit UserDismissTrackingFilter(
        QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    void setUserDismissedCallback(
        std::function<void()> callback)
    {
        userDismissedCallback_ =
            std::move(callback);
    }

protected:
    bool eventFilter(
        QObject *watched,
        QEvent *event) override
    {
        if (
            event->type() == QEvent::Close &&
            !watched->property(
                "programmaticClose").toBool()) {
            watched->setProperty(
                "userDismissed",
                true);

            if (userDismissedCallback_) {
                userDismissedCallback_();
            }
        }

        return QObject::eventFilter(
            watched,
            event);
    }

private:
    std::function<void()>
        userDismissedCallback_;
};

class AssistAudioComboBox final
    : public QComboBox
{
public:
    explicit AssistAudioComboBox(
        QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setCursor(
            Qt::PointingHandCursor);
    }

protected:
    void paintEvent(
        QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);

        QPainter painter(this);

        painter.setRenderHint(
            QPainter::Antialiasing,
            true);

        const int centerX =
            width() - 18;

        const int centerY =
            height() / 2;

        QPolygon arrow;

        arrow
            << QPoint(
                   centerX - 5,
                   centerY - 3)
            << QPoint(
                   centerX + 5,
                   centerY - 3)
            << QPoint(
                   centerX,
                   centerY + 4);

        painter.setPen(
            Qt::NoPen);

        painter.setBrush(
            isEnabled()
                ? QColor(
                      94,
                      228,
                      255)
                : QColor(
                      125,
                      137,
                      152));

        painter.drawPolygon(
            arrow);
    }
};

void populateAudioCombo(
    QComboBox *combo,
    const QList<AudioDevice> &devices,
    const QString &savedNodeName)
{
    combo->clear();

    combo->addItem(
        QStringLiteral("System Default"),
        QString());

    for (const AudioDevice &device :
         devices) {
        combo->addItem(
            device.description,
            device.nodeName);
    }

    const int savedIndex =
        combo->findData(
            savedNodeName);

    combo->setCurrentIndex(
        savedIndex >= 0
            ? savedIndex
            : 0);
}

void showSettingsDialog(
    QWidget *parent)
{
    QSettings settings(
        QStringLiteral("ScottiBYTE"),
        QStringLiteral("Assist"));

    QDialog dialog(parent);

    dialog.setObjectName(
        QStringLiteral("settingsDialog"));

    dialog.setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist Settings"));

    dialog.setMinimumWidth(620);

    dialog.setStyleSheet(
        QStringLiteral(
            R"CSS(
QDialog#settingsDialog {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 1,
        stop: 0 #09294c,
        stop: 0.55 #071d39,
        stop: 1 #151043
    );
}

QDialog#settingsDialog QLabel {
    color: #c8d8e7;
    font-size: 14px;
}

QDialog#settingsDialog QLabel#settingsStatus {
    color: #5ee4ff;
    background: rgba(7, 37, 67, 190);
    border: 1px solid #2d789b;
    border-radius: 10px;
    padding: 10px 12px;
    font-weight: 700;
}

QDialog#settingsDialog QLineEdit,
QDialog#settingsDialog QComboBox {
    min-height: 38px;
    padding: 0 12px;
    color: #ffffff;
    background: #071a34;
    border: 1px solid #39dfff;
    border-radius: 10px;
    selection-background-color: #245f96;
}

QDialog#settingsDialog QComboBox {
    padding-right: 38px;
    font-weight: 600;
}

QDialog#settingsDialog QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 34px;
    border: none;
    background: transparent;
}

QDialog#settingsDialog QComboBox::down-arrow {
    image: none;
    width: 0;
    height: 0;
}

QDialog#settingsDialog QComboBox QAbstractItemView {
    color: #ffffff;
    background: #071a34;
    border: 1px solid #39dfff;
    selection-background-color: #245f96;
    selection-color: #ffffff;
    outline: none;
}

QDialog#settingsDialog QPushButton {
    min-height: 38px;
    padding: 0 20px;
    color: #ffffff;
    font-weight: 700;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 0, y2: 1,
        stop: 0 #245f96,
        stop: 1 #07284b
    );
    border: 1px solid #28c7f7;
    border-radius: 10px;
}

QDialog#settingsDialog QPushButton:hover {
    background: #176da0;
}

QDialog#settingsDialog QPushButton#saveSettingsButton {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #1d9ac5,
        stop: 1 #6728bb
    );
}

QDialog#settingsDialog QPushButton#cancelSettingsButton {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 0, y2: 1,
        stop: 0 #a93650,
        stop: 1 #671227
    );
    border-color: #ff6b86;
}
)CSS"));

    auto *layout =
        new QVBoxLayout(&dialog);

    layout->setContentsMargins(
        20,
        20,
        20,
        20);

    layout->setSpacing(12);

    auto *form =
        new QFormLayout;

    form->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);

    auto *serverUrl =
        new QLineEdit;

    serverUrl->setText(
        settings.value(
            QStringLiteral(
                "connection/serverUrl"),
            QStringLiteral(
                "https://assist.scottibyte.com"))
            .toString());

    serverUrl->setPlaceholderText(
        QStringLiteral(
            "https://assist.example.com"));

    auto *inputDevice =
        new AssistAudioComboBox;

    inputDevice->setCursor(
        Qt::PointingHandCursor);

    inputDevice->setToolTip(
        QStringLiteral(
            "Select the microphone or audio input device."));

    auto *outputDevice =
        new AssistAudioComboBox;

    outputDevice->setCursor(
        Qt::PointingHandCursor);

    outputDevice->setToolTip(
        QStringLiteral(
            "Select the speakers, headphones, or "
            "audio output device."));

    auto *deviceStatus =
        new QLabel;

    deviceStatus->setObjectName(
        QStringLiteral("settingsStatus"));

    deviceStatus->setWordWrap(true);

    auto *refreshDevices =
        new QPushButton(
            QStringLiteral(
                "Refresh Audio Devices"));

    const auto refresh =
        [
            inputDevice,
            outputDevice,
            deviceStatus,
            &settings
        ]()
        {
            const QString selectedInput =
                inputDevice->count() > 0
                    ? inputDevice
                          ->currentData()
                          .toString()
                    : settings.value(
                          QStringLiteral(
                              "voice/inputNode"))
                          .toString();

            const QString selectedOutput =
                outputDevice->count() > 0
                    ? outputDevice
                          ->currentData()
                          .toString()
                    : settings.value(
                          QStringLiteral(
                              "voice/outputNode"))
                          .toString();

            const AudioDeviceInventory inventory =
                queryAudioDevices();

            populateAudioCombo(
                inputDevice,
                inventory.inputs,
                selectedInput);

            populateAudioCombo(
                outputDevice,
                inventory.outputs,
                selectedOutput);

            if (!inventory.error.isEmpty()) {
                deviceStatus->setText(
                    QStringLiteral(
                        "Audio device discovery failed: ") +
                    inventory.error);
                return;
            }

            if (
                inventory.inputs.isEmpty() &&
                inventory.outputs.isEmpty()) {
                deviceStatus->setText(
                    QStringLiteral(
                        "No audio input or output "
                        "devices were detected. "
                        "Assist sessions will still work "
                        "without voice."));
            } else if (
                inventory.inputs.isEmpty()) {
                deviceStatus->setText(
                    QStringLiteral(
                        "No audio input devices were "
                        "detected. Listening may still "
                        "be available."));
            } else if (
                inventory.outputs.isEmpty()) {
                deviceStatus->setText(
                    QStringLiteral(
                        "No audio output devices were "
                        "detected. Microphone transmission "
                        "may still be available."));
            } else {
                deviceStatus->setText(
                    QStringLiteral(
                        "%1 input device(s) and %2 output "
                        "device(s) detected.")
                        .arg(
                            inventory.inputs.size())
                        .arg(
                            inventory.outputs.size()));
            }
        };

    QObject::connect(
        refreshDevices,
        &QPushButton::clicked,
        &dialog,
        refresh);

    form->addRow(
        QStringLiteral(
            "Assist Server URL"),
        serverUrl);

    form->addRow(
        QStringLiteral(
            "Audio input"),
        inputDevice);

    form->addRow(
        QStringLiteral(
            "Audio output"),
        outputDevice);

    layout->addLayout(form);
    layout->addWidget(refreshDevices);
    layout->addWidget(deviceStatus);
    layout->addStretch();

    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Save |
            QDialogButtonBox::Cancel);

    if (
        QPushButton *saveButton =
            buttons->button(
                QDialogButtonBox::Save)) {
        saveButton->setObjectName(
            QStringLiteral(
                "saveSettingsButton"));

        saveButton->setText(
            QStringLiteral("Save"));
    }

    if (
        QPushButton *cancelButton =
            buttons->button(
                QDialogButtonBox::Cancel)) {
        cancelButton->setObjectName(
            QStringLiteral(
                "cancelSettingsButton"));

        cancelButton->setText(
            QStringLiteral("Cancel"));
    }

    QObject::connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        [
            &dialog,
            &settings,
            serverUrl,
            inputDevice,
            outputDevice
        ]()
        {
            QString normalizedServerUrl =
                serverUrl->text().trimmed();

            while (
                normalizedServerUrl.endsWith(
                    QChar('/'))) {
                normalizedServerUrl.chop(1);
            }

            settings.setValue(
                QStringLiteral(
                    "connection/serverUrl"),
                normalizedServerUrl);

            settings.setValue(
                QStringLiteral(
                    "voice/inputNode"),
                inputDevice
                    ->currentData()
                    .toString());

            settings.setValue(
                QStringLiteral(
                    "voice/outputNode"),
                outputDevice
                    ->currentData()
                    .toString());

            settings.sync();

            dialog.accept();
        });

    QObject::connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject);

    layout->addWidget(buttons);

    refresh();

    dialog.exec();
}

void showSessionDetailsDialog(
    QWidget *parent,
    WanSignalingClient *customerSignaling,
    WanSignalingClient *providerSignaling,
    LanSession *lanSession,
    const QString *previousSessionDiagnostics)
{
    QDialog dialog(parent);

    dialog.setObjectName(
        QStringLiteral(
            "sessionDetailsDialog"));

    dialog.setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Session Details"));

    dialog.setMinimumSize(
        720,
        650);

    dialog.resize(
        820,
        740);

    dialog.setStyleSheet(
        QStringLiteral(
            R"CSS(
QDialog#sessionDetailsDialog {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 1,
        stop: 0 #09294c,
        stop: 0.55 #071d39,
        stop: 1 #151043
    );
}

QDialog#sessionDetailsDialog QLabel#detailsTitle {
    color: #ffffff;
    font-size: 26px;
    font-weight: 800;
}

QDialog#sessionDetailsDialog QLabel#detailsSubtitle {
    color: #5ee4ff;
    font-size: 14px;
    font-weight: 700;
}

QDialog#sessionDetailsDialog QFrame#detailsCard {
    background: rgba(4, 24, 50, 205);
    border: 1px solid #1e87b8;
    border-radius: 14px;
}

QDialog#sessionDetailsDialog QPlainTextEdit {
    color: #dcecff;
    background: transparent;
    border: none;
    padding: 12px;
    font-family:
        "Ubuntu Mono",
        "Noto Sans Mono",
        monospace;
    font-size: 13px;
    selection-background-color: #245f96;
}

QDialog#sessionDetailsDialog QTabWidget::pane {
    background: rgba(4, 24, 50, 205);
    border: 1px solid #1e87b8;
    border-radius: 12px;
    top: -1px;
}

QDialog#sessionDetailsDialog QTabBar::tab {
    min-width: 165px;
    min-height: 38px;
    padding: 0 18px;
    color: #bdd5ed;
    background: #071d39;
    border: 1px solid #1e87b8;
    border-bottom: none;
    font-weight: 700;
}

QDialog#sessionDetailsDialog QTabBar::tab:selected {
    color: #ffffff;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #27bfe8,
        stop: 1 #8e35e8
    );
}

QDialog#sessionDetailsDialog QPushButton {
    min-height: 38px;
    padding: 0 24px;
    color: #ffffff;
    font-weight: 700;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 0, y2: 1,
        stop: 0 #245f96,
        stop: 1 #07284b
    );
    border: 1px solid #28c7f7;
    border-radius: 10px;
}

QDialog#sessionDetailsDialog QPushButton:hover {
    background: #176da0;
}
)CSS"));

    auto *layout =
        new QVBoxLayout(&dialog);

    layout->setContentsMargins(
        22,
        20,
        22,
        20);

    layout->setSpacing(12);

    auto *title =
        makeLabel(
            QStringLiteral(
                "Session details"),
            QStringLiteral(
                "detailsTitle"));

    auto *subtitle =
        makeLabel(
            QStringLiteral(
                "Current and previous connection diagnostics"),
            QStringLiteral(
                "detailsSubtitle"));

    auto *tabs =
        new QTabWidget;

    /*
     * Current session tab.
     */
    auto *currentPage =
        new QWidget;

    auto *currentLayout =
        new QVBoxLayout(currentPage);

    currentLayout->setContentsMargins(
        10,
        10,
        10,
        10);

    currentLayout->setSpacing(8);

    auto *copyCurrentButton =
        makeButton(
            QStringLiteral(
                "Copy Current Log"));

    copyCurrentButton->setToolTip(
        QStringLiteral(
            "Copy the current live diagnostics "
            "to the clipboard"));

    auto *currentButtonRow =
        new QHBoxLayout;

    currentButtonRow->addWidget(
        copyCurrentButton);

    currentButtonRow->addStretch();

    auto *currentText =
        new QPlainTextEdit;

    currentText->setReadOnly(true);

    currentText->setLineWrapMode(
        QPlainTextEdit::NoWrap);

    currentLayout->addLayout(
        currentButtonRow);

    currentLayout->addWidget(
        currentText,
        1);

    /*
     * Previous session tab.
     */
    auto *previousPage =
        new QWidget;

    auto *previousLayout =
        new QVBoxLayout(previousPage);

    previousLayout->setContentsMargins(
        10,
        10,
        10,
        10);

    previousLayout->setSpacing(8);

    auto *copyPreviousButton =
        makeButton(
            QStringLiteral(
                "Copy Previous Log"));

    copyPreviousButton->setToolTip(
        QStringLiteral(
            "Copy the frozen previous-session "
            "diagnostics to the clipboard"));

    auto *previousButtonRow =
        new QHBoxLayout;

    previousButtonRow->addWidget(
        copyPreviousButton);

    previousButtonRow->addStretch();

    auto *previousText =
        new QPlainTextEdit;

    previousText->setReadOnly(true);

    previousText->setLineWrapMode(
        QPlainTextEdit::NoWrap);

    previousLayout->addLayout(
        previousButtonRow);

    previousLayout->addWidget(
        previousText,
        1);

    tabs->addTab(
        currentPage,
        QStringLiteral(
            "Current session"));

    tabs->addTab(
        previousPage,
        QStringLiteral(
            "Previous session"));

    auto *closeButton =
        makeButton(
            QStringLiteral("Close"));

    auto *buttonRow =
        new QHBoxLayout;

    buttonRow->addStretch();

    buttonRow->addWidget(
        closeButton);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(tabs, 1);
    layout->addLayout(buttonRow);

    const auto currentSummary =
        [
            customerSignaling,
            providerSignaling,
            lanSession
        ]()
        {
            return
                customerSignaling->
                    diagnosticSummary(
                        QStringLiteral(
                            "Customer signaling")) +
                QStringLiteral(
                    "\n\n"
                    "----------------------------------------"
                    "\n\n") +
                providerSignaling->
                    diagnosticSummary(
                        QStringLiteral(
                            "Provider signaling")) +
                QStringLiteral(
                    "\n\n"
                    "----------------------------------------"
                    "\n\n") +
                lanSession->
                    diagnosticSummary();
        };

    const auto refreshCurrent =
        [
            currentText,
            currentSummary
        ]()
        {
            /*
             * Do not destroy Ctrl+A or mouse
             * selections during live refresh.
             */
            if (
                currentText->
                    textCursor().
                    hasSelection()
            ) {
                return;
            }

            const QString summary =
                currentSummary();

            if (
                currentText->toPlainText() ==
                summary
            ) {
                return;
            }

            const int scrollPosition =
                currentText->
                    verticalScrollBar()->
                    value();

            currentText->setPlainText(
                summary);

            currentText->
                verticalScrollBar()->
                setValue(
                    scrollPosition);
        };

    const auto refreshPrevious =
        [
            previousText,
            previousSessionDiagnostics
        ]()
        {
            if (
                previousText->
                    textCursor().
                    hasSelection()
            ) {
                return;
            }

            const QString summary =
                previousSessionDiagnostics != nullptr &&
                !previousSessionDiagnostics->
                    trimmed().
                    isEmpty()
                    ? *previousSessionDiagnostics
                    : QStringLiteral(
                          "No previous session "
                          "diagnostics have been "
                          "captured yet.");

            if (
                previousText->toPlainText() ==
                summary
            ) {
                return;
            }

            const int scrollPosition =
                previousText->
                    verticalScrollBar()->
                    value();

            previousText->setPlainText(
                summary);

            previousText->
                verticalScrollBar()->
                setValue(
                    scrollPosition);
        };

    refreshPrevious();

    QObject::connect(
        copyCurrentButton,
        &QPushButton::clicked,
        &dialog,
        [
            currentText
        ]()
        {
            QApplication::clipboard()->
                setText(
                    currentText->
                        toPlainText());
        });

    QObject::connect(
        copyPreviousButton,
        &QPushButton::clicked,
        &dialog,
        [
            previousText
        ]()
        {
            QApplication::clipboard()->
                setText(
                    previousText->
                        toPlainText());
        });

    QObject::connect(
        closeButton,
        &QPushButton::clicked,
        &dialog,
        &QDialog::accept);

    auto *refreshTimer =
        new QTimer(&dialog);

    refreshTimer->setInterval(
        500);

    QObject::connect(
        refreshTimer,
        &QTimer::timeout,
        &dialog,
        [
            refreshCurrent,
            refreshPrevious
        ]()
        {
            refreshCurrent();
            refreshPrevious();
        });

    refreshCurrent();
    refreshPrevious();
    refreshTimer->start();

    dialog.exec();
}

class FullScreenExitBubble final
    : public QPushButton
{
public:
    explicit FullScreenExitBubble(
        QWidget *fullScreenWindow)
        : QPushButton(
              QStringLiteral(
                  "Exit Full Screen"),
              fullScreenWindow),
          fullScreenWindow_(
              fullScreenWindow)
    {
        setCursor(
            Qt::OpenHandCursor);

        setFixedSize(
            150,
            42);

        setStyleSheet(
            QStringLiteral(
                "QPushButton {"
                "  background: rgba(24, 24, 27, 220);"
                "  color: white;"
                "  border: 1px solid rgba(255,255,255,90);"
                "  border-radius: 21px;"
                "  padding: 0 16px;"
                "  font-weight: 600;"
                "}"
                "QPushButton:hover {"
                "  background: rgba(45, 45, 50, 240);"
                "  border-color: rgba(255,255,255,150);"
                "}"));
    }

    void prepareForFullScreen()
    {
        if (!userPositioned_) {
            positionAtUpperRight();
        } else {
            keepInsideWindow();
        }

        show();
        raise();
    }

protected:
    void mousePressEvent(
        QMouseEvent *event) override
    {
        if (event->button() ==
            Qt::LeftButton) {
            dragging_ = true;
            movedDuringDrag_ = false;

            dragOffset_ =
                event->position()
                    .toPoint();

            setCursor(
                Qt::ClosedHandCursor);

            event->accept();
            return;
        }

        QPushButton::mousePressEvent(
            event);
    }

    void mouseMoveEvent(
        QMouseEvent *event) override
    {
        if (!dragging_ ||
            fullScreenWindow_ ==
                nullptr) {
            return;
        }

        QPoint requested =
            mapToParent(
                event->position()
                    .toPoint())
            - dragOffset_;

        const int maximumX =
            qMax(
                0,
                fullScreenWindow_->width()
                    - width());

        const int maximumY =
            qMax(
                0,
                fullScreenWindow_->height()
                    - height());

        requested.setX(
            qBound(
                0,
                requested.x(),
                maximumX));

        requested.setY(
            qBound(
                0,
                requested.y(),
                maximumY));

        if (requested != pos()) {
            move(requested);
            movedDuringDrag_ = true;
        }

        event->accept();
    }

    void mouseReleaseEvent(
        QMouseEvent *event) override
    {
        if (event->button() ==
                Qt::LeftButton &&
            dragging_) {
            dragging_ = false;

            setCursor(
                Qt::OpenHandCursor);

            if (movedDuringDrag_) {
                userPositioned_ = true;
            } else {
                click();
            }

            event->accept();
            return;
        }

        QPushButton::mouseReleaseEvent(
            event);
    }

private:
    void positionAtUpperRight()
    {
        if (fullScreenWindow_ ==
            nullptr) {
            return;
        }

        constexpr int margin = 20;

        int screenWidth =
            fullScreenWindow_->width();

        if (fullScreenWindow_->screen() !=
            nullptr) {
            screenWidth =
                fullScreenWindow_
                    ->screen()
                    ->geometry()
                    .width();
        }

        move(
            qMax(
                0,
                screenWidth
                    - width()
                    - margin),
            margin);
    }

    void keepInsideWindow()
    {
        if (fullScreenWindow_ ==
            nullptr) {
            return;
        }

        move(
            qBound(
                0,
                x(),
                qMax(
                    0,
                    fullScreenWindow_->width()
                        - width())),
            qBound(
                0,
                y(),
                qMax(
                    0,
                    fullScreenWindow_->height()
                        - height())));
    }

    QWidget *fullScreenWindow_ =
        nullptr;

    QPoint dragOffset_;

    bool dragging_ = false;
    bool movedDuringDrag_ = false;
    bool userPositioned_ = false;
};

}

int main(
    int argc,
    char *argv[])
{
#if !defined(Q_OS_WIN)
    QGuiApplication::setDesktopFileName(
        QStringLiteral(
            "scottibyte-assist"));
#endif

    QApplication application(
        argc,
        argv);

#if defined(Q_OS_WIN)
    const QDir applicationDirectory(
        QCoreApplication::applicationDirPath());

    const QString bundledPluginPath =
        applicationDirectory.filePath(
            QStringLiteral("gstreamer-1.0"));

    const QString bundledPluginScanner =
        applicationDirectory.filePath(
            QStringLiteral(
                "gstreamer-libexec/"
                "gst-plugin-scanner.exe"));

    if (QDir(bundledPluginPath).exists())
    {
        qputenv(
            "GST_PLUGIN_SYSTEM_PATH_1_0",
            QDir::toNativeSeparators(
                bundledPluginPath).toUtf8());

        qputenv(
            "GST_PLUGIN_PATH_1_0",
            QDir::toNativeSeparators(
                bundledPluginPath).toUtf8());
    }

    if (QFileInfo::exists(
            bundledPluginScanner))
    {
        qputenv(
            "GST_PLUGIN_SCANNER",
            QDir::toNativeSeparators(
                bundledPluginScanner).toUtf8());
    }
#endif

    application.setApplicationName(
        QStringLiteral(
            "ScottiBYTE Assist"));

    application.setApplicationVersion(
        QStringLiteral(
            SCOTTIBYTE_ASSIST_VERSION));

    const QIcon applicationIcon(
        QStringLiteral(
            ":/assets/scottibyte-assist.png"));

    application.setWindowIcon(
        applicationIcon);

    application.setStyleSheet(
        QStringLiteral(
R"CSS(
* {
    font-family: "Ubuntu", "Noto Sans", sans-serif;
}

QWidget#window {
    background: #06162d;
    color: #eef8ff;
}

QFrame#header {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #0b3157,
        stop: 0.64 #071a37,
        stop: 1 #170c3a
    );
    border-bottom: 1px solid #25bff3;
}

QLabel#logoBadge {
    min-width: 64px;
    max-width: 64px;
    min-height: 64px;
    max-height: 64px;
    border: none;
    background: transparent;
}

QLabel#brandTitle {
    font-size: 24px;
    font-weight: 800;
    color: #ffffff;
}

QLabel#brandSubtitle {
    color: #4edcff;
    font-size: 19px;
    font-weight: 900;
}

QLabel#versionLabel {
    color: #8fe8ff;
    font-size: 15px;
    font-weight: 800;
    padding: 1px 0 0 0;
}

QLabel#donateLink {
    color: #ffffff;
    font-size: 15px;
    font-weight: 900;
    padding: 3px 10px;
    border: 1px solid #ff6b86;
    border-radius: 10px;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 rgba(197, 62, 91, 210),
        stop: 1 rgba(122, 27, 57, 210)
    );
}

QLabel#donateLink:hover {
    border: 1px solid #ff9fb1;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 rgba(224, 83, 115, 230),
        stop: 1 rgba(146, 35, 72, 230)
    );
}

QPushButton#settingsButton,
QPushButton#secondaryButton {
    min-height: 38px;
    padding: 0 22px;
    border: 1px solid #28c7f7;
    border-radius: 12px;
    color: #ffffff;
    font-weight: 700;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 0, y2: 1,
        stop: 0 #245f96,
        stop: 1 #07284b
    );
}

QPushButton#settingsButton:hover,
QPushButton#secondaryButton:hover {
    background: #176da0;
}

QPushButton#secondaryButton:checked {
    border: 2px solid #73efff;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #1688bb,
        stop: 1 #6527b9
    );
}

QFrame#modeBar {
    background: #03142b;
    border-bottom: 1px solid #1a5d89;
}

QPushButton#modeButton {
    min-height: 54px;
    border: none;
    color: #afc0d3;
    background: transparent;
    font-size: 17px;
    font-weight: 700;
}

QPushButton#modeButton:checked {
    color: #ffffff;
    border-bottom: 4px solid #5ae6ff;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #1cc5ef,
        stop: 1 #962be0
    );
}

QFrame#mainCard {
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 1,
        stop: 0 #09294c,
        stop: 0.55 #071d39,
        stop: 1 #151043
    );
    border: 1px solid #1e87b8;
    border-radius: 18px;
}

QLabel#pageTitle {
    color: #ffffff;
    font-size: 29px;
    font-weight: 800;
}

QLabel#pageDescription {
    color: #c8d8e7;
    font-size: 16px;
}

QFrame#codeCard {
    min-height: 120px;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 1,
        stop: 0 #194d7d,
        stop: 0.52 #143663,
        stop: 1 #221252
    );
    border: 1px solid #35d8ff;
    border-radius: 18px;
}

QLabel#supportCode {
    color: #ffffff;
    font-size: 48px;
    font-weight: 900;
    letter-spacing: 9px;
}

QLabel#statusText {
    color: #5ee4ff;
    font-size: 16px;
    font-weight: 800;
}

QLabel#statusDot {
    min-width: 28px;
    max-width: 28px;
    min-height: 28px;
    max-height: 28px;
    border: 1px solid #55e6ff;
    border-radius: 14px;
    background: #06304c;
    color: #68eaff;
    font-weight: 800;
}

QPushButton#primaryButton {
    min-height: 44px;
    padding: 0 28px;
    border: 1px solid #57e7ff;
    border-radius: 12px;
    color: #ffffff;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 1, y2: 0,
        stop: 0 #1688bb,
        stop: 1 #58249e
    );
    font-size: 16px;
    font-weight: 800;
}

QPushButton#primaryButton:hover {
    background: #257fc3;
}

QPushButton#viewButton {
    min-width: 42px;
    max-width: 42px;
    min-height: 34px;
    max-height: 34px;
    border: 1px solid #35d8ff;
    border-radius: 9px;
    color: #ffffff;
    background: #0b3d67;
    font-size: 18px;
    font-weight: 800;
}

QPushButton#viewButton:hover {
    background: #176da0;
}

QPushButton#dangerButton {
    min-height: 44px;
    padding: 0 28px;
    border: 1px solid #ff6b86;
    border-radius: 12px;
    color: #ffffff;
    background: qlineargradient(
        x1: 0, y1: 0,
        x2: 0, y2: 1,
        stop: 0 #c53e5b,
        stop: 1 #691128
    );
    font-size: 16px;
    font-weight: 800;
}

QPushButton:disabled,
QPushButton#primaryButton:disabled,
QPushButton#dangerButton:disabled,
QPushButton#secondaryButton:disabled,
QPushButton#settingsButton:disabled {
    color: #7d8998;
    border-color: #465363;
    background: #293442;
}

QFrame#progressCard,
QFrame#remoteArea {
    background: rgba(7, 37, 67, 190);
    border: 1px solid #2d789b;
    border-radius: 14px;
}

QLabel#sectionHeading {
    color: #55e0ff;
    font-size: 16px;
    font-weight: 800;
}

QLabel#smallText {
    color: #c5d2df;
    font-size: 14px;
}

QLineEdit#codeEntry {
    min-height: 64px;
    border: 1px solid #39dfff;
    border-radius: 14px;
    padding: 0 20px;
    color: #ffffff;
    background: #071a34;
    font-size: 30px;
    font-weight: 900;
    letter-spacing: 5px;
}

QLabel#remotePlaceholder {
    color: #7deaff;
    font-size: 19px;
    font-weight: 700;
}
)CSS"));

    auto *window =
        new QWidget;

    window->setObjectName(
        QStringLiteral("window"));

    window->setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist"));

    window->setWindowIcon(
        applicationIcon);

    window->resize(
        900,
        720);

    window->setMinimumSize(
        820,
        650);

    auto *rootLayout =
        new QVBoxLayout(window);

    rootLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    rootLayout->setSpacing(0);

    auto *header =
        makeCard(
            QStringLiteral("header"));

    auto *headerLayout =
        new QHBoxLayout(header);

    headerLayout->setContentsMargins(
        24,
        14,
        24,
        14);

    headerLayout->setSpacing(14);

    auto *logo =
        makeLabel(
            QString(),
            QStringLiteral("logoBadge"));

    logo->setAlignment(
        Qt::AlignCenter);

    logo->setFixedSize(
        64,
        64);

    const QPixmap logoPixmap(
        QStringLiteral(
            ":/assets/scottibyte-assist.png"));

    logo->setPixmap(
        logoPixmap.scaled(
            64,
            64,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));

    auto *brandLayout =
        new QVBoxLayout;

    brandLayout->setSpacing(2);

    brandLayout->addWidget(
        makeLabel(
            QStringLiteral("ScottiBYTE"),
            QStringLiteral("brandTitle")));

    auto *brandMetadataLayout =
        new QHBoxLayout;

    brandMetadataLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    brandMetadataLayout->setSpacing(14);

    auto *assistLabel =
        makeLabel(
            QStringLiteral("Assist"),
            QStringLiteral("brandSubtitle"));

    auto *versionLabel =
        makeLabel(
            QStringLiteral("v%1")
                .arg(
                    QCoreApplication::
                        applicationVersion()),
            QStringLiteral("versionLabel"));

    auto *donateLink =
        makeLabel(
            QStringLiteral(
                "<a "
                "style=\"text-decoration:none;\" "
                "href=\"https://www.paypal.com/paypalme/ScottiBYTE\">"
                "<span style=\"color:#ff5d7a;\">♥</span> "
                "<span style=\"color:#5de2ff;\">Donate</span>"
                "</a>"),
            QStringLiteral("donateLink"));

    donateLink->setTextFormat(
        Qt::RichText);

    donateLink->setTextInteractionFlags(
        Qt::LinksAccessibleByMouse);

    donateLink->setOpenExternalLinks(
        true);

    donateLink->setFocusPolicy(
        Qt::NoFocus);

    donateLink->setCursor(
        Qt::PointingHandCursor);

    donateLink->setToolTip(
        QStringLiteral(
            "Open the ScottiBYTE PayPal donation page"));

    brandMetadataLayout->addWidget(
        assistLabel);

    brandMetadataLayout->addWidget(
        versionLabel);

    brandMetadataLayout->addWidget(
        donateLink);

    brandMetadataLayout->addStretch();

    brandLayout->addLayout(
        brandMetadataLayout);

    auto *detailsButton =
        makeButton(
            QStringLiteral("ⓘ  Details"),
            QStringLiteral("settingsButton"));

    auto *settingsButton =
        makeButton(
            QStringLiteral("⚙  Settings"),
            QStringLiteral("settingsButton"));

    headerLayout->addWidget(logo);
    headerLayout->addLayout(brandLayout);
    headerLayout->addStretch();
    headerLayout->addWidget(detailsButton);
    headerLayout->addWidget(settingsButton);

    rootLayout->addWidget(header);

    auto *modeBar =
        makeCard(
            QStringLiteral("modeBar"));

    auto *modeLayout =
        new QHBoxLayout(modeBar);

    modeLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    modeLayout->setSpacing(0);

    auto *receiveButton =
        makeButton(
            QStringLiteral("↓  Receive Support"),
            QStringLiteral("modeButton"));

    auto *provideButton =
        makeButton(
            QStringLiteral("♙  Provide Support"),
            QStringLiteral("modeButton"));

    receiveButton->setCheckable(true);
    provideButton->setCheckable(true);
    receiveButton->setChecked(true);

    auto *modeGroup =
        new QButtonGroup(window);

    modeGroup->setExclusive(true);
    modeGroup->addButton(receiveButton, 0);
    modeGroup->addButton(provideButton, 1);

    modeLayout->addWidget(receiveButton, 1);
    modeLayout->addWidget(provideButton, 1);

    rootLayout->addWidget(modeBar);

    auto *pages =
        new QStackedWidget;

    auto *pageMargins =
        new QWidget;

    auto *pageMarginsLayout =
        new QVBoxLayout(pageMargins);

    pageMarginsLayout->setContentsMargins(
        16,
        16,
        16,
        16);

    pages->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding);

    pageMarginsLayout->addWidget(pages);

    rootLayout->addWidget(
        pageMargins,
        1);

    // Receive Support page
    auto *receivePage =
        makeCard(
            QStringLiteral("mainCard"));

    auto *receiveLayout =
        new QVBoxLayout(receivePage);

    receiveLayout->setContentsMargins(
        24,
        20,
        24,
        20);

    receiveLayout->setSpacing(12);

    auto *receiveTitle =
        makeLabel(
            QStringLiteral("Need help?"),
            QStringLiteral("pageTitle"));

    receiveTitle->setAlignment(
        Qt::AlignCenter);

    auto *receiveDescription =
        makeLabel(
            QStringLiteral(
                "Tell this code to the person helping you."),
            QStringLiteral("pageDescription"));

    receiveDescription->setAlignment(
        Qt::AlignCenter);

    auto *codeCard =
        makeCard(
            QStringLiteral("codeCard"));

    auto *codeLayout =
        new QVBoxLayout(codeCard);

    auto *supportCode =
        makeLabel(
            QStringLiteral("--- ---"),
            QStringLiteral("supportCode"));

    supportCode->setAlignment(
        Qt::AlignCenter);

    codeLayout->addWidget(supportCode);

    auto *statusRow =
        new QHBoxLayout;

    statusRow->setAlignment(
        Qt::AlignCenter);

    auto *statusDot =
        makeLabel(
            QStringLiteral("◌"),
            QStringLiteral("statusDot"));

    statusDot->setAlignment(
        Qt::AlignCenter);

    auto *receiveStatus =
        makeLabel(
            QStringLiteral(
                "Waiting for the person helping you..."),
            QStringLiteral("statusText"));

    statusRow->addWidget(statusDot);
    statusRow->addSpacing(8);
    statusRow->addWidget(receiveStatus);

    auto *receiveActions =
        new QHBoxLayout;

    receiveActions->setAlignment(
        Qt::AlignCenter);

    auto *newCodeButton =
        makeButton(
            QStringLiteral("↻  New Code"),
            QStringLiteral("secondaryButton"));

    auto *endSupportButton =
        makeButton(
            QStringLiteral("■  End Support"),
            QStringLiteral("dangerButton"));

    endSupportButton->setEnabled(false);

    auto *customerStartVoiceButton =
        makeButton(
            QStringLiteral("Start Voice"),
            QStringLiteral("primaryButton"));

    customerStartVoiceButton->setEnabled(false);

    auto *customerStopVoiceButton =
        makeButton(
            QStringLiteral("Stop Voice"),
            QStringLiteral("secondaryButton"));

    customerStopVoiceButton->setEnabled(false);

    auto *customerMuteButton =
        makeButton(
            QStringLiteral("Mute Microphone"),
            QStringLiteral("secondaryButton"));

    customerMuteButton->setCheckable(true);
    customerMuteButton->setEnabled(false);

    auto *customerVoiceControls =
        new QHBoxLayout;

    customerVoiceControls->setAlignment(
        Qt::AlignCenter);

    customerVoiceControls->setSpacing(8);

    customerVoiceControls->addWidget(
        customerStartVoiceButton);

    customerVoiceControls->addWidget(
        customerStopVoiceButton);

    customerVoiceControls->addWidget(
        customerMuteButton);

    receiveActions->addWidget(newCodeButton);
    receiveActions->addWidget(endSupportButton);

    auto *progressCard =
        makeCard(
            QStringLiteral("progressCard"));

    auto *progressLayout =
        new QVBoxLayout(progressCard);

    progressLayout->setContentsMargins(
        22,
        18,
        22,
        18);

    auto *progressHeading =
        makeLabel(
            QStringLiteral(
                "Secure session ready"),
            QStringLiteral("sectionHeading"));

    auto *progressDetail =
        makeLabel(
            QStringLiteral(
                "Waiting for the person helping you "
                "to connect."),
            QStringLiteral("smallText"));

    progressLayout->addWidget(
        progressHeading);

    progressLayout->addWidget(
        progressDetail);

    receiveLayout->addWidget(receiveTitle);
    receiveLayout->addWidget(receiveDescription);
    receiveLayout->addWidget(codeCard);
    receiveLayout->addLayout(statusRow);
    receiveLayout->addLayout(receiveActions);
    receiveLayout->addLayout(
        customerVoiceControls);
    receiveLayout->addWidget(progressCard);
    receiveLayout->addStretch();

    pages->addWidget(receivePage);

    // Provide Support page
    auto *providePage =
        makeCard(
            QStringLiteral("mainCard"));

    auto *provideLayout =
        new QVBoxLayout(providePage);

    provideLayout->setContentsMargins(
        28,
        22,
        28,
        22);

    provideLayout->setSpacing(14);

    auto *provideTitle =
        makeLabel(
            QStringLiteral(
                "Provide support"),
            QStringLiteral("pageTitle"));

    provideTitle->setAlignment(
        Qt::AlignCenter);

    auto *provideDescription =
        makeLabel(
            QStringLiteral(
                "Enter the six-digit code shown on the other computer."),
            QStringLiteral("pageDescription"));

    provideDescription->setAlignment(
        Qt::AlignCenter);

    auto *codeEntry =
        new QLineEdit;

    codeEntry->setObjectName(
        QStringLiteral("codeEntry"));

    codeEntry->setAlignment(
        Qt::AlignCenter);

    codeEntry->setPlaceholderText(
        QStringLiteral("000 000"));

    codeEntry->setMaxLength(7);

    codeEntry->setValidator(
        new QRegularExpressionValidator(
            QRegularExpression(
                QStringLiteral(
                    R"(\d{0,3}\s?\d{0,3})")),
            codeEntry));

    auto *connectButton =
        makeButton(
            QStringLiteral(
                "Connect"),
            QStringLiteral("primaryButton"));

    auto *provideStatus =
        makeLabel(
            QStringLiteral(
                "Enter a support code to begin."),
            QStringLiteral("statusText"));

    provideStatus->setAlignment(
        Qt::AlignCenter);

    auto *providerWindowControls =
        new QHBoxLayout;

    providerWindowControls->setSpacing(8);

    auto *openRemoteWindowButton =
        makeButton(
            QStringLiteral("Remote Control Customer"),
            QStringLiteral(
                "secondaryButton"));

    openRemoteWindowButton->setToolTip(
        QStringLiteral(
            "Open the remote desktop in a larger "
            "resizable window"));

    openRemoteWindowButton->setEnabled(false);

    auto *fullScreenButton =
        makeButton(
            QStringLiteral("Full Screen Remote Control"),
            QStringLiteral(
                "secondaryButton"));

    fullScreenButton->setToolTip(
        QStringLiteral(
            "View remote desktop full screen"));

    fullScreenButton->setEnabled(false);

    providerWindowControls->addWidget(
        openRemoteWindowButton,
        1);

    providerWindowControls->addWidget(
        fullScreenButton);

    auto *shareSourceLayout =
        new QHBoxLayout;

    shareSourceLayout->setSpacing(8);

    auto *shareSourceLabel =
        makeLabel(
            QStringLiteral("Share source"),
            QStringLiteral("smallText"));

    auto *shareSourceCombo =
        new QComboBox;

    shareSourceCombo->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Fixed);

    shareSourceCombo->setEnabled(false);

    auto *refreshShareSourcesButton =
        makeButton(
            QStringLiteral("Refresh Sources"),
            QStringLiteral(
                "secondaryButton"));

    refreshShareSourcesButton->setEnabled(
        false);

    shareSourceLayout->addWidget(
        shareSourceLabel);

    shareSourceLayout->addWidget(
        shareSourceCombo,
        1);

    shareSourceLayout->addWidget(
        refreshShareSourcesButton);

    auto *shareProviderScreenButton =
        makeButton(
            QStringLiteral("Share My Screen"),
            QStringLiteral(
                "secondaryButton"));

    shareProviderScreenButton->setToolTip(
        QStringLiteral(
            "Share your screen with the customer"));

    shareProviderScreenButton->setEnabled(false);

    auto *providerStartVoiceButton =
        makeButton(
            QStringLiteral("Start Voice"),
            QStringLiteral("primaryButton"));

    providerStartVoiceButton->setEnabled(false);

    auto *providerStopVoiceButton =
        makeButton(
            QStringLiteral("Stop Voice"),
            QStringLiteral("secondaryButton"));

    providerStopVoiceButton->setEnabled(false);

    auto *providerMuteButton =
        makeButton(
            QStringLiteral("Mute Microphone"),
            QStringLiteral("secondaryButton"));

    providerMuteButton->setCheckable(true);
    providerMuteButton->setEnabled(false);

    providerMuteButton->setToolTip(
        QStringLiteral(
            "Provider microphone transmission "
            "will be added with two-way voice."));

    auto *providerVoiceControls =
        new QHBoxLayout;

    providerVoiceControls->setSpacing(8);

    providerVoiceControls->addWidget(
        providerStartVoiceButton,
        1);

    providerVoiceControls->addWidget(
        providerStopVoiceButton);

    providerVoiceControls->addWidget(
        providerMuteButton);

    auto *remoteWindow =
        new QWidget;

    QString selectedRemoteDisplayId;

    remoteWindow->setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Remote Desktop"));

    remoteWindow->resize(
        1280,
        800);

    remoteWindow->setMinimumSize(
        800,
        500);

    remoteWindow->setAttribute(
        Qt::WA_QuitOnClose,
        false);

    auto *remoteWindowDismissFilter =
        new UserDismissTrackingFilter(
            remoteWindow);

    remoteWindow->installEventFilter(
        remoteWindowDismissFilter);

    auto *remoteWindowLayout =
        new QVBoxLayout(
            remoteWindow);

    remoteWindowLayout->setContentsMargins(
        10,
        10,
        10,
        10);

    remoteWindowLayout->setSpacing(8);

    auto *remoteWindowHeader =
        new QHBoxLayout;

    auto *remoteWindowTitle =
        makeLabel(
            QStringLiteral(
                "Remote desktop"),
            QStringLiteral(
                "sectionHeading"));

    auto *remoteDisplayScrollArea =
        new QScrollArea;

    remoteDisplayScrollArea->setWidgetResizable(
        true);

    remoteDisplayScrollArea->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAsNeeded);

    remoteDisplayScrollArea->setVerticalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);

    remoteDisplayScrollArea->setFrameShape(
        QFrame::NoFrame);

    remoteDisplayScrollArea->setMinimumHeight(
        58);

    remoteDisplayScrollArea->setMaximumHeight(
        64);

    auto *remoteDisplayContainer =
        new QWidget;

    auto *remoteDisplayLayout =
        new QHBoxLayout(
            remoteDisplayContainer);

    remoteDisplayLayout->setContentsMargins(
        2,
        2,
        2,
        2);

    remoteDisplayLayout->setSpacing(8);

    auto *remoteDisplayStatus =
        makeLabel(
            QStringLiteral(
                "Choose Remote Control Customer "
                "to load displays."),
            QStringLiteral("smallText"));

    remoteDisplayLayout->addWidget(
        remoteDisplayStatus);

    remoteDisplayLayout->addStretch();

    remoteDisplayScrollArea->setWidget(
        remoteDisplayContainer);

    auto *remoteDisplayButtonGroup =
        new QButtonGroup(
            remoteWindow);

    remoteDisplayButtonGroup->setExclusive(
        true);

    auto *remoteWindowFullScreenButton =
        makeButton(
            QStringLiteral("Full Screen Remote Control"),
            QStringLiteral(
                "secondaryButton"));

    remoteWindowFullScreenButton->setEnabled(
        false);

    remoteWindowHeader->addWidget(
        remoteWindowTitle);

    remoteWindowHeader->addWidget(
        remoteDisplayScrollArea,
        1);

    remoteWindowHeader->addWidget(
        remoteWindowFullScreenButton);

    auto *remoteWindowView =
        new RemoteView;

    remoteWindowView->setMinimumSize(
        760,
        420);

    remoteWindowLayout->addLayout(
        remoteWindowHeader);

    remoteWindowLayout->addWidget(
        remoteWindowView,
        1);

    auto *fullScreenWindow =
        new QWidget;

    fullScreenWindow->setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Remote Desktop"));

    fullScreenWindow->setStyleSheet(
        QStringLiteral(
            "background: black;"));

    auto *fullScreenLayout =
        new QVBoxLayout(
            fullScreenWindow);

    fullScreenLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    auto *fullScreenRemoteView =
        new RemoteView;

    fullScreenLayout->addWidget(
        fullScreenRemoteView);

    auto *exitFullScreenBubble =
        new FullScreenExitBubble(
            fullScreenWindow);

    exitFullScreenBubble->raise();

    auto *exitFullScreenShortcut =
        new QShortcut(
            QKeySequence(
                Qt::Key_Escape),
            fullScreenWindow);

    /*
     * Full-screen exit is wired later so the normal
     * Remote Assistance window can be restored.
     */

    auto *disconnectButton =
        makeButton(
            QStringLiteral(
                "End Session"),
            QStringLiteral("dangerButton"));

    disconnectButton->setEnabled(false);

    provideLayout->addWidget(provideTitle);
    provideLayout->addWidget(provideDescription);
    provideLayout->addWidget(codeEntry);
    provideLayout->addWidget(connectButton);
    provideLayout->addWidget(provideStatus);
    provideLayout->addLayout(providerWindowControls);
    provideLayout->addLayout(
        shareSourceLayout);
    provideLayout->addWidget(
        shareProviderScreenButton);
    provideLayout->addLayout(
        providerVoiceControls);
    provideLayout->addStretch(1);
    provideLayout->addWidget(disconnectButton);

    pages->addWidget(providePage);

    auto *providerScreenWindow =
        new QWidget;

    providerScreenWindow->setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Provider Screen"));

    providerScreenWindow->resize(
        1280,
        800);

    providerScreenWindow->setMinimumSize(
        800,
        500);

    providerScreenWindow->setAttribute(
        Qt::WA_QuitOnClose,
        false);

    providerScreenWindow->setProperty(
        "userDismissed",
        false);

    providerScreenWindow->setProperty(
        "programmaticClose",
        false);

    auto *providerScreenDismissFilter =
        new UserDismissTrackingFilter(
            providerScreenWindow);

    providerScreenWindow->installEventFilter(
        providerScreenDismissFilter);

    auto *providerScreenLayout =
        new QVBoxLayout(
            providerScreenWindow);

    providerScreenLayout->setContentsMargins(
        10,
        10,
        10,
        10);

    providerScreenLayout->setSpacing(8);

    auto *providerScreenHeader =
        new QHBoxLayout;

    auto *providerScreenTitle =
        makeLabel(
            QStringLiteral(
                "Provider screen"),
            QStringLiteral(
                "sectionHeading"));

    auto *providerScreenCloseButton =
        makeButton(
            QStringLiteral("Close View"),
            QStringLiteral(
                "secondaryButton"));

    providerScreenHeader->addWidget(
        providerScreenTitle);

    providerScreenHeader->addStretch();

    providerScreenHeader->addWidget(
        providerScreenCloseButton);

    auto *providerScreenView =
        new RemoteView;

    providerScreenView->setMinimumSize(
        760,
        420);

    /*
     * This window is deliberately view-only.
     * Do not connect its input signals to LanSession.
     */
    providerScreenView->setFocusPolicy(
        Qt::NoFocus);

    providerScreenView->setAttribute(
        Qt::WA_TransparentForMouseEvents,
        true);

    providerScreenLayout->addLayout(
        providerScreenHeader);

    providerScreenLayout->addWidget(
        providerScreenView,
        1);

    QObject::connect(
        providerScreenCloseButton,
        &QPushButton::clicked,
        providerScreenWindow,
        &QWidget::close);

    auto *lanSession =
        new LanSession(window);

    auto *customerSignaling =
        new WanSignalingClient(window);

    auto *providerSignaling =
        new WanSignalingClient(window);

    QString previousSessionDiagnostics;

    bool customerDisconnectCaptured = false;
    bool providerDisconnectCaptured = false;

    const auto capturePreviousSession =
        [
            customerSignaling,
            providerSignaling,
            lanSession,
            &previousSessionDiagnostics
        ](
            const QString &reason)
        {
            previousSessionDiagnostics =
                QStringLiteral(
                    "Captured: %1\n"
                    "Reason: %2\n\n")
                    .arg(
                        QDateTime::
                            currentDateTime().
                            toString(
                                QStringLiteral(
                                    "yyyy-MM-dd "
                                    "HH:mm:ss")),
                        reason.isEmpty()
                            ? QStringLiteral(
                                  "Connection ended")
                            : reason) +
                customerSignaling->
                    diagnosticSummary(
                        QStringLiteral(
                            "Customer signaling")) +
                QStringLiteral(
                    "\n\n"
                    "----------------------------------------"
                    "\n\n") +
                providerSignaling->
                    diagnosticSummary(
                        QStringLiteral(
                            "Provider signaling")) +
                QStringLiteral(
                    "\n\n"
                    "----------------------------------------"
                    "\n\n") +
                lanSession->
                    diagnosticSummary();
        };

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::sessionSubscribed,
        window,
        [
            &customerDisconnectCaptured
        ]()
        {
            customerDisconnectCaptured = false;
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::sessionSubscribed,
        window,
        [
            &providerDisconnectCaptured
        ]()
        {
            providerDisconnectCaptured = false;
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::errorOccurred,
        window,
        [
            capturePreviousSession,
            &customerDisconnectCaptured
        ](
            const QString &message)
        {
            customerDisconnectCaptured = true;

            capturePreviousSession(
                QStringLiteral(
                    "Customer signaling error: ") +
                message);
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::errorOccurred,
        window,
        [
            capturePreviousSession,
            &providerDisconnectCaptured
        ](
            const QString &message)
        {
            providerDisconnectCaptured = true;

            capturePreviousSession(
                QStringLiteral(
                    "Provider signaling error: ") +
                message);
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::disconnecting,
        window,
        [
            capturePreviousSession,
            &customerDisconnectCaptured
        ](
            const QString &message)
        {
            if (customerDisconnectCaptured) {
                return;
            }

            customerDisconnectCaptured = true;

            capturePreviousSession(
                QStringLiteral(
                    "Customer signaling disconnected: ") +
                message);
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::disconnecting,
        window,
        [
            capturePreviousSession,
            &providerDisconnectCaptured
        ](
            const QString &message)
        {
            if (providerDisconnectCaptured) {
                return;
            }

            providerDisconnectCaptured = true;

            capturePreviousSession(
                QStringLiteral(
                    "Provider signaling disconnected: ") +
                message);
        });

    QObject::connect(
        lanSession,
        &LanSession::errorOccurred,
        window,
        [
            capturePreviousSession
        ](
            const QString &message)
        {
            capturePreviousSession(
                QStringLiteral(
                    "Desktop transport error: ") +
                message);
        });

    auto *providerCandidateFallbackTimer =
        new QTimer(window);

    providerCandidateFallbackTimer->
        setSingleShot(true);

    providerCandidateFallbackTimer->
        setInterval(3000);

    auto *voiceAudioThread =
        new QThread(window);

    voiceAudioThread->setObjectName(
        QStringLiteral(
            "ScottiBYTE Assist Voice Audio"));

    auto *customerVoiceAudio =
        new CustomerVoiceAudio();

    customerVoiceAudio->moveToThread(
        voiceAudioThread);

    QObject::connect(
        voiceAudioThread,
        &QThread::finished,
        customerVoiceAudio,
        &QObject::deleteLater);

    voiceAudioThread->start(
        QThread::HighPriority);

    QObject::connect(
        QCoreApplication::instance(),
        &QCoreApplication::aboutToQuit,
        window,
        [
            customerVoiceAudio,
            voiceAudioThread
        ]()
        {
            customerVoiceAudio->stop();

            voiceAudioThread->quit();
            voiceAudioThread->wait();
        },
        Qt::DirectConnection);

    providerScreenDismissFilter->
        setUserDismissedCallback(
            [lanSession]()
            {
                lanSession->
                    notifyProviderScreenClosed();
            });

    remoteWindowDismissFilter->
        setUserDismissedCallback(
            [
                lanSession,
                remoteWindowView,
                fullScreenRemoteView,
                fullScreenWindow,
                remoteWindowFullScreenButton,
                &selectedRemoteDisplayId
            ]()
            {
                lanSession->
                    requestRemoteControlStop();

                selectedRemoteDisplayId.clear();

                remoteWindowFullScreenButton->
                    setEnabled(false);

                remoteWindowView->clearFrame();
                fullScreenRemoteView->clearFrame();
                fullScreenWindow->close();
            });

    DesktopBackend *desktopBackend =
        nullptr;

#if defined(Q_OS_WIN)
    auto *windowsBackend =
        new WindowsDesktopBackend(window);

    desktopBackend =
        windowsBackend;

    lanSession->setDesktopBackend(
        desktopBackend);

    shareSourceCombo->clear();

    shareSourceCombo->addItem(
        QStringLiteral(
            "Primary Windows display"),
        QStringLiteral("primary"));

    shareSourceCombo->setEnabled(false);
    refreshShareSourcesButton->setEnabled(false);
#else
    const QString sessionType =
        qEnvironmentVariable(
            "XDG_SESSION_TYPE");

    const QString platformName =
        QGuiApplication::platformName();

    const bool waylandSession =
        sessionType.compare(
            QStringLiteral("wayland"),
            Qt::CaseInsensitive) == 0 ||
        (
            sessionType.isEmpty() &&
            platformName.contains(
                QStringLiteral("wayland"),
                Qt::CaseInsensitive)
        );

    WaylandDesktopBackend *waylandBackend =
        nullptr;

    X11DesktopBackend *x11Backend =
        nullptr;

    if (waylandSession) {
        waylandBackend =
            new WaylandDesktopBackend(window);

        desktopBackend =
            waylandBackend;
    } else {
        x11Backend =
            new X11DesktopBackend(window);

        desktopBackend =
            x11Backend;
    }

    lanSession->setDesktopBackend(
        desktopBackend);

    const auto refreshShareSources =
        [
            x11Backend,
            waylandBackend,
            shareSourceCombo
        ]()
        {
            const QString previousSource =
                shareSourceCombo
                    ->currentData()
                    .toString();

            shareSourceCombo->clear();

            if (x11Backend != nullptr) {
                const auto sources =
                    x11Backend->
                        availableShareSources();

                for (const auto &source :
                     sources) {
                    shareSourceCombo->addItem(
                        source.label,
                        source.id);
                }

                int selectedIndex =
                    shareSourceCombo->findData(
                        previousSource);

                if (selectedIndex < 0) {
                    selectedIndex =
                        shareSourceCombo->findData(
                            x11Backend->
                                shareSource());
                }

                if (
                    selectedIndex < 0 &&
                    shareSourceCombo->count() > 0) {
                    selectedIndex = 0;
                }

                if (selectedIndex >= 0) {
                    shareSourceCombo->
                        setCurrentIndex(
                            selectedIndex);
                }

                return;
            }

            if (waylandBackend != nullptr) {
                shareSourceCombo->addItem(
                    QStringLiteral(
                        "Choose through Wayland portal"),
                    QStringLiteral("portal"));
            }
        };

    refreshShareSources();

    QObject::connect(
        refreshShareSourcesButton,
        &QPushButton::clicked,
        window,
        refreshShareSources);

    QObject::connect(
        shareSourceCombo,
        &QComboBox::currentIndexChanged,
        window,
        [
            x11Backend,
            shareSourceCombo
        ](
            int)
        {
            if (x11Backend == nullptr) {
                return;
            }

            x11Backend->setShareSource(
                shareSourceCombo
                    ->currentData()
                    .toString());
        });
#endif

    QString lastClipboardText;

#if !defined(Q_OS_WIN)
    if (waylandBackend != nullptr) {
        QObject::connect(
            waylandBackend,
            &WaylandDesktopBackend::
                localClipboardTextChanged,
            lanSession,
            &LanSession::sendClipboardText);

        QObject::connect(
            lanSession,
            &LanSession::clipboardTextReceived,
            waylandBackend,
            &WaylandDesktopBackend::
                applyRemoteClipboardText);
    } else
#endif
    {
        QClipboard *clipboard =
            QApplication::clipboard();

        lastClipboardText =
            clipboard->text(
                QClipboard::Clipboard);

        QObject::connect(
            clipboard,
            &QClipboard::dataChanged,
            window,
            [
                clipboard,
                lanSession,
                &lastClipboardText
            ]()
            {
                const QString text =
                    clipboard->text(
                        QClipboard::Clipboard);

                if (text ==
                    lastClipboardText) {
                    return;
                }

                lastClipboardText = text;
                lanSession->sendClipboardText(text);
            });

        QObject::connect(
            lanSession,
            &LanSession::clipboardTextReceived,
            window,
            [
                clipboard,
                &lastClipboardText
            ](
                const QString &text)
            {
                if (text ==
                    lastClipboardText) {
                    return;
                }

                lastClipboardText = text;

                clipboard->setText(
                    text,
                    QClipboard::Clipboard);
            });
    }

    bool customerCodeConsumed = false;
    bool restartingCustomerSession = false;
    bool providerWasConnected = false;

    const auto startCustomerSession =
        [
            lanSession,
            customerSignaling,
            supportCode,
            receiveStatus,
            progressHeading,
            progressDetail,
            newCodeButton,
            endSupportButton,
            &customerCodeConsumed,
            &restartingCustomerSession
        ](
            bool)
        {
            restartingCustomerSession = true;
            customerCodeConsumed = false;

            lanSession->disconnectSession();
            customerSignaling->
                disconnectFromServer();

            supportCode->setText(
                QStringLiteral("--- ---"));

            newCodeButton->setEnabled(false);
            endSupportButton->setEnabled(false);

            receiveStatus->setText(
                QStringLiteral(
                    "Requesting a secure support code..."));

            progressHeading->setText(
                QStringLiteral(
                    "Creating secure session"));

            progressDetail->setText(
                QStringLiteral(
                    "Connecting to the ScottiBYTE "
                    "Assist server."));

            const QUrl serverUrl =
                configuredAssistServerUrl();

            customerSignaling->
                createCustomerSession(
                    serverUrl,
                    assistWebSocketUrl(
                        serverUrl),
                    QHostInfo::localHostName());

            restartingCustomerSession = false;
        };

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::
            sessionCodeAssigned,
        window,
        [
            lanSession,
            supportCode,
            receiveStatus,
            progressHeading,
            progressDetail,
            newCodeButton
        ](
            const QString &code)
        {
            supportCode->setText(
                formattedSupportCode(code));

            receiveStatus->setText(
                QStringLiteral(
                    "Waiting for the person helping you..."));

            progressHeading->setText(
                QStringLiteral(
                    "Secure session ready"));

            progressDetail->setText(
                QStringLiteral(
                    "Waiting for the person helping you "
                    "to connect."));

            newCodeButton->setEnabled(true);

            lanSession->startCustomer(
                code);
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::errorOccurred,
        window,
        [
            receiveStatus,
            progressHeading,
            progressDetail,
            newCodeButton
        ](
            const QString &message)
        {
            receiveStatus->setText(
                QStringLiteral("Error: ") +
                message);

            progressHeading->setText(
                QStringLiteral(
                    "Unable to create session"));

            progressDetail->setText(
                QStringLiteral(
                    "Check the Assist Server URL and "
                    "your network connection."));

            newCodeButton->setEnabled(true);
        });

    startCustomerSession(false);

    QObject::connect(
        modeGroup,
        &QButtonGroup::idClicked,
        pages,
        &QStackedWidget::setCurrentIndex);

    QObject::connect(
        newCodeButton,
        &QPushButton::clicked,
        window,
        [
            startCustomerSession
        ]()
        {
            startCustomerSession(true);
        });

    QObject::connect(
        endSupportButton,
        &QPushButton::clicked,
        window,
        [
            lanSession
        ]()
        {
            lanSession->disconnectSession();
        });

    QObject::connect(
        shareProviderScreenButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            shareProviderScreenButton
        ]()
        {
            const bool currentlySharing =
                shareProviderScreenButton->text() ==
                QStringLiteral(
                    "Stop Sharing");

            if (currentlySharing) {
                lanSession->stopProviderShare();
            } else {
                lanSession->startProviderShare();
            }
        });

    QObject::connect(
        detailsButton,
        &QPushButton::clicked,
        window,
        [
            window,
            customerSignaling,
            providerSignaling,
            lanSession,
            &previousSessionDiagnostics
        ]()
        {
            showSessionDetailsDialog(
                window,
                customerSignaling,
                providerSignaling,
                lanSession,
                &previousSessionDiagnostics);
        });

    QObject::connect(
        settingsButton,
        &QPushButton::clicked,
        window,
        [window]()
        {
            showSettingsDialog(
                window);
        });

    QObject::connect(
        codeEntry,
        &QLineEdit::returnPressed,
        connectButton,
        &QPushButton::click);

    QObject::connect(
        connectButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            providerSignaling,
            codeEntry,
            connectButton,
            disconnectButton,
            provideStatus
        ]()
        {
            QString code =
                codeEntry->text();

            code.remove(QChar(' '));
            code = code.trimmed();

            if (code.size() != 6) {
                provideStatus->setText(
                    QStringLiteral(
                        "Enter all six digits."));
                return;
            }

            QString credentialError;

            const QString credential =
                loadProviderCredential(
                    &credentialError);

            if (credential.isEmpty()) {
                provideStatus->setText(
                    credentialError);
                return;
            }

            const QUrl serverUrl =
                configuredAssistServerUrl();

            providerSignaling->
                claimSupportSession(
                    serverUrl,
                    assistWebSocketUrl(
                        serverUrl),
                    code,
                    credential,
                    QHostInfo::localHostName());

            /*
             * Begin LAN discovery immediately while
             * server signaling requests a direct TCP
             * candidate. The first successful path wins.
             */
            lanSession->connectProvider(
                code);

            provideStatus->setText(
                QStringLiteral(
                    "Claiming the support code and "
                    "looking on the LAN..."));

            connectButton->setEnabled(false);
            codeEntry->setEnabled(false);
            disconnectButton->setEnabled(true);
        });

    QObject::connect(
        disconnectButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            providerSignaling,
            providerCandidateFallbackTimer,
            shareProviderScreenButton,
            remoteWindowView,
            remoteWindow,
            fullScreenRemoteView,
            fullScreenWindow,
            codeEntry,
            connectButton,
            disconnectButton,
            provideStatus
        ]()
        {
            if (
                shareProviderScreenButton->text() ==
                QStringLiteral(
                    "Stop Sharing")) {
                lanSession->stopProviderShare();
            }

            lanSession->disconnectSession();

            providerSignaling->
                disconnectFromServer();

            providerCandidateFallbackTimer->
                stop();

            remoteWindowView->clearFrame();
            fullScreenRemoteView->clearFrame();

            remoteWindow->close();
            fullScreenWindow->close();

            provideStatus->setText(
                QStringLiteral(
                    "Disconnected."));

            codeEntry->setEnabled(true);
            connectButton->setEnabled(true);
            disconnectButton->setEnabled(false);
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::
            sessionSubscribed,
        window,
        [
            providerSignaling,
            provideStatus
        ]()
        {
            provideStatus->setText(
                QStringLiteral(
                    "Support code claimed. Requesting "
                    "the customer connection address..."));

            providerSignaling->
                sendCandidateRequest();
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::
            candidateRequestReceived,
        window,
        [
            customerSignaling,
            lanSession
        ]()
        {
            const QStringList addresses =
                lanSession->
                    customerCandidateAddresses();

            if (addresses.isEmpty()) {
                return;
            }

            customerSignaling->sendCandidate(
                addresses.constFirst(),
                lanSession->
                    customerSessionPort());
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::
            peerCandidateReceived,
        window,
        [
            lanSession,
            providerSignaling,
            providerCandidateFallbackTimer,
            provideStatus
        ](
            const QString &address,
            quint16 port)
        {
            if (lanSession->isConnected()) {
                return;
            }

            provideStatus->setText(
                QStringLiteral(
                    "Trying the customer connection "
                    "address..."));

            lanSession->connectProviderDirect(
                providerSignaling->
                    sessionCode(),
                address,
                port);

            providerCandidateFallbackTimer->
                start();
        });

    QObject::connect(
        providerCandidateFallbackTimer,
        &QTimer::timeout,
        window,
        [
            lanSession,
            providerSignaling,
            provideStatus
        ]()
        {
            if (lanSession->isConnected()) {
                return;
            }

            const QString code =
                providerSignaling->
                    sessionCode();

            if (code.isEmpty()) {
                return;
            }

            provideStatus->setText(
                QStringLiteral(
                    "Direct connection did not succeed. "
                    "Starting the Assist relay..."));

            providerSignaling->
                sendRelayRequest();

            providerSignaling->
                startRelay();
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::
            relayRequestReceived,
        window,
        [
            customerSignaling
        ]()
        {
            customerSignaling->
                startRelay();
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::
            relayRequestReceived,
        window,
        [
            providerSignaling,
            providerCandidateFallbackTimer,
            provideStatus
        ]()
        {
            providerCandidateFallbackTimer->
                stop();

            provideStatus->setText(
                QStringLiteral(
                    "Customer requested the Assist "
                    "relay. Starting relay..."));

            providerSignaling->
                startRelay();
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::relayReady,
        window,
        [
            lanSession
        ]()
        {
            if (!lanSession->isConnected()) {
                lanSession->
                    activateRelayTransport();
            }
        });

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::relayReady,
        window,
        [
            lanSession
        ]()
        {
            if (!lanSession->isConnected()) {
                lanSession->
                    activateRelayTransport();
            }
        });

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::disconnected,
        lanSession,
        &LanSession::relayTransportLost);

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::disconnected,
        lanSession,
        &LanSession::relayTransportLost);

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::
            relayBytesQueuedChanged,
        lanSession,
        &LanSession::setRelayBytesQueued);

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::
            relayBytesQueuedChanged,
        lanSession,
        &LanSession::setRelayBytesQueued);

    QObject::connect(
        lanSession,
        &LanSession::relayBytesReady,
        customerSignaling,
        &WanSignalingClient::sendRelayBytes);

    QObject::connect(
        lanSession,
        &LanSession::relayBytesReady,
        providerSignaling,
        &WanSignalingClient::sendRelayBytes);

    QObject::connect(
        customerSignaling,
        &WanSignalingClient::
            relayBytesReceived,
        lanSession,
        &LanSession::receiveRelayBytes);

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::
            relayBytesReceived,
        lanSession,
        &LanSession::receiveRelayBytes);

    QObject::connect(
        providerSignaling,
        &WanSignalingClient::errorOccurred,
        window,
        [
            provideStatus,
            codeEntry,
            connectButton,
            disconnectButton
        ](
            const QString &message)
        {
            provideStatus->setText(
                QStringLiteral("Error: ") +
                message);

            codeEntry->setEnabled(true);
            connectButton->setEnabled(true);
            disconnectButton->setEnabled(false);
        });

    QObject::connect(
        lanSession,
        &LanSession::statusChanged,
        window,
        [
            receiveButton,
            receiveStatus,
            provideStatus
        ](
            const QString &message)
        {
            if (receiveButton->isChecked()) {
                receiveStatus->setText(
                    message);
            } else {
                provideStatus->setText(
                    message);
            }
        });

    QObject::connect(
        lanSession,
        &LanSession::errorOccurred,
        window,
        [
            receiveButton,
            receiveStatus,
            provideStatus
        ](
            const QString &message)
        {
            const QString fullMessage =
                QStringLiteral("Error: ") +
                message;

            if (receiveButton->isChecked()) {
                receiveStatus->setText(
                    fullMessage);
            } else {
                provideStatus->setText(
                    fullMessage);
            }
        });

    /*
     * Voice is manually controlled. Disconnecting the
     * support session always stops the local audio path.
     */
    QObject::connect(
        lanSession,
        &LanSession::connectedChanged,
        window,
        [
            customerVoiceAudio,
            providerCandidateFallbackTimer,
            receiveButton,
            customerStartVoiceButton,
            customerStopVoiceButton,
            customerMuteButton,
            providerStartVoiceButton,
            providerStopVoiceButton,
            providerMuteButton
        ](
            bool connected)
        {
            if (connected) {
                providerCandidateFallbackTimer->
                    stop();
            }

            customerVoiceAudio->stop();
            customerVoiceAudio->setMuted(false);

            customerMuteButton->blockSignals(true);
            customerMuteButton->setChecked(false);
            customerMuteButton->setText(
                QStringLiteral(
                    "Mute Microphone"));
            customerMuteButton->blockSignals(false);

            providerMuteButton->blockSignals(true);
            providerMuteButton->setChecked(false);
            providerMuteButton->setText(
                QStringLiteral(
                    "Mute Microphone"));
            providerMuteButton->blockSignals(false);

            const bool customerConnected =
                connected &&
                receiveButton->isChecked();

            const bool providerConnected =
                connected &&
                !receiveButton->isChecked();

            customerStartVoiceButton->setEnabled(
                customerConnected);

            customerStopVoiceButton->setEnabled(false);
            customerMuteButton->setEnabled(false);

            providerStartVoiceButton->setEnabled(
                providerConnected);

            providerStopVoiceButton->setEnabled(false);
            providerMuteButton->setEnabled(false);
        });

    QObject::connect(
        customerVoiceAudio,
        &CustomerVoiceAudio::
            voicePacketReady,
        lanSession,
        &LanSession::sendVoicePacket,
        Qt::QueuedConnection);

    QObject::connect(
        lanSession,
        &LanSession::voicePacketReceived,
        customerVoiceAudio,
        &CustomerVoiceAudio::pushVoicePacket,
        Qt::QueuedConnection);

    QObject::connect(
        customerStartVoiceButton,
        &QPushButton::clicked,
        lanSession,
        &LanSession::requestVoiceStart);

    QObject::connect(
        customerStopVoiceButton,
        &QPushButton::clicked,
        lanSession,
        &LanSession::requestVoiceStop);

    QObject::connect(
        lanSession,
        &LanSession::voiceStartRequested,
        window,
        [
            customerVoiceAudio,
            lanSession,
            receiveButton,
            customerStartVoiceButton,
            customerStopVoiceButton,
            customerMuteButton,
            providerStartVoiceButton,
            providerStopVoiceButton,
            providerMuteButton,
            receiveStatus,
            provideStatus
        ]()
        {
            QSettings settings(
                QStringLiteral("ScottiBYTE"),
                QStringLiteral("ScottiBYTE Assist"));

            const QString inputNode =
                settings.value(
                    QStringLiteral(
                        "voice/inputNode"))
                    .toString();

            const QString outputNode =
                settings.value(
                    QStringLiteral(
                        "voice/outputNode"))
                    .toString();

            if (receiveButton->isChecked()) {
                const bool receiverStarted =
                    customerVoiceAudio->
                        startPacketReceiver(
                            outputNode);

                const bool senderStarted =
                    receiverStarted &&
                    customerVoiceAudio->
                        startPacketSender(
                            inputNode);

                if (!receiverStarted || !senderStarted) {
                    customerVoiceAudio->stop();

                    receiveStatus->setText(
                        QStringLiteral(
                            "Full-duplex voice could not start."));
                    return;
                }

                customerStartVoiceButton->setEnabled(false);
                customerStopVoiceButton->setEnabled(true);
                customerMuteButton->setEnabled(true);

                receiveStatus->setText(
                    QStringLiteral(
                        "Full-duplex voice is active."));

                return;
            }

            const bool receiverStarted =
                customerVoiceAudio->
                    startPacketReceiver(
                        outputNode);

            const bool senderStarted =
                receiverStarted &&
                customerVoiceAudio->
                    startPacketSender(
                        inputNode);

            if (!receiverStarted || !senderStarted) {
                customerVoiceAudio->stop();

                provideStatus->setText(
                    QStringLiteral(
                        "Full-duplex voice could not start."));
                return;
            }

            providerStartVoiceButton->setEnabled(false);
            providerStopVoiceButton->setEnabled(true);
            providerMuteButton->setEnabled(true);

            provideStatus->setText(
                QStringLiteral(
                    "Full-duplex voice is active."));
        });

    QObject::connect(
        lanSession,
        &LanSession::voiceStopRequested,
        window,
        [
            customerVoiceAudio,
            receiveButton,
            customerStartVoiceButton,
            customerStopVoiceButton,
            customerMuteButton,
            providerStartVoiceButton,
            providerStopVoiceButton,
            providerMuteButton,
            receiveStatus,
            provideStatus
        ]()
        {
            customerVoiceAudio->stop();
            customerVoiceAudio->setMuted(false);

            customerMuteButton->blockSignals(true);
            customerMuteButton->setChecked(false);
            customerMuteButton->setText(
                QStringLiteral(
                    "Mute Microphone"));
            customerMuteButton->blockSignals(false);

            providerMuteButton->blockSignals(true);
            providerMuteButton->setChecked(false);
            providerMuteButton->setText(
                QStringLiteral(
                    "Mute Microphone"));
            providerMuteButton->blockSignals(false);

            if (receiveButton->isChecked()) {
                customerStartVoiceButton->setEnabled(true);
                customerStopVoiceButton->setEnabled(false);
                customerMuteButton->setEnabled(false);

                receiveStatus->setText(
                    QStringLiteral(
                        "Voice stopped."));

                return;
            }

            providerStartVoiceButton->setEnabled(true);
            providerStopVoiceButton->setEnabled(false);
            providerMuteButton->setEnabled(false);

            provideStatus->setText(
                QStringLiteral(
                    "Voice stopped."));
        });

    QObject::connect(
        customerMuteButton,
        &QPushButton::toggled,
        window,
        [
            customerVoiceAudio,
            customerMuteButton,
            receiveStatus
        ](
            bool muted)
        {
            customerVoiceAudio->setMuted(muted);

            customerMuteButton->setText(
                muted
                    ? QStringLiteral(
                          "Unmute Microphone")
                    : QStringLiteral(
                          "Mute Microphone"));

            receiveStatus->setText(
                muted
                    ? QStringLiteral(
                          "Microphone muted.")
                    : QStringLiteral(
                          "Microphone unmuted."));
        });

    QObject::connect(
        providerMuteButton,
        &QPushButton::toggled,
        window,
        [
            customerVoiceAudio,
            providerMuteButton,
            provideStatus
        ](
            bool muted)
        {
            customerVoiceAudio->setMuted(muted);

            providerMuteButton->setText(
                muted
                    ? QStringLiteral(
                          "Unmute Microphone")
                    : QStringLiteral(
                          "Mute Microphone"));

            provideStatus->setText(
                muted
                    ? QStringLiteral(
                          "Microphone muted.")
                    : QStringLiteral(
                          "Microphone unmuted."));
        });

    QObject::connect(
        providerStartVoiceButton,
        &QPushButton::clicked,
        lanSession,
        &LanSession::requestVoiceStart);

    QObject::connect(
        providerStopVoiceButton,
        &QPushButton::clicked,
        lanSession,
        &LanSession::requestVoiceStop);

    QObject::connect(
        customerVoiceAudio,
        &CustomerVoiceAudio::errorOccurred,
        window,
        [
            receiveButton,
            receiveStatus,
            provideStatus
        ](
            const QString &message)
        {
            if (receiveButton->isChecked()) {
                receiveStatus->setText(
                    QStringLiteral(
                        "Voice error: ") +
                    message);
            } else {
                provideStatus->setText(
                    QStringLiteral(
                        "Voice error: ") +
                    message);
            }
        });

    QObject::connect(
        lanSession,
        &LanSession::connectedChanged,
        window,
        [
            receiveButton,
            progressHeading,
            progressDetail,
            remoteWindowView,
            remoteWindow,
            fullScreenRemoteView,
            fullScreenWindow,
            openRemoteWindowButton,
            fullScreenButton,
            shareProviderScreenButton,
            shareSourceCombo,
            refreshShareSourcesButton,
            providerScreenView,
            providerScreenWindow,
            connectButton,
            disconnectButton,
            codeEntry,
            provideStatus,
            newCodeButton,
            endSupportButton,
            startCustomerSession,
            &customerCodeConsumed,
            &restartingCustomerSession,
            &providerWasConnected,
            window
        ](
            bool connected)
        {
            openRemoteWindowButton->setEnabled(
                connected);

            fullScreenButton->setEnabled(
                connected);

            const bool providerConnected =
                connected &&
                !receiveButton->isChecked();

            shareProviderScreenButton->setEnabled(
                providerConnected);

            shareSourceCombo->setEnabled(
                providerConnected);

            refreshShareSourcesButton->setEnabled(
                providerConnected);

            if (!connected) {
                remoteWindowView->clearFrame();
                fullScreenRemoteView->clearFrame();

                remoteWindow->close();
                fullScreenWindow->close();

                providerScreenView->clearFrame();

                providerScreenWindow->setProperty(
                    "programmaticClose",
                    true);

                providerScreenWindow->close();

                providerScreenWindow->setProperty(
                    "programmaticClose",
                    false);

                providerScreenWindow->setProperty(
                    "userDismissed",
                    false);

                shareProviderScreenButton->setText(
                    QStringLiteral(
                        "Share My Screen"));
            }

            if (receiveButton->isChecked()) {
                if (connected) {
                    customerCodeConsumed = true;

                    progressHeading->setText(
                        QStringLiteral(
                            "Secure session connected"));

                    progressDetail->setText(
                        QStringLiteral(
                            "Remote assistance is active."));

                    newCodeButton->setEnabled(false);
                    endSupportButton->setEnabled(true);
                } else {
                    progressHeading->setText(
                        QStringLiteral(
                            "Secure session ready"));

                    progressDetail->setText(
                        QStringLiteral(
                            "Waiting for the person helping "
                            "you to connect."));

                    newCodeButton->setEnabled(true);
                    endSupportButton->setEnabled(false);

                    if (
                        customerCodeConsumed &&
                        !restartingCustomerSession
                    ) {
                        customerCodeConsumed = false;
                        restartingCustomerSession = true;

                        QTimer::singleShot(
                            0,
                            window,
                            [
                                startCustomerSession,
                                &restartingCustomerSession
                            ]()
                            {
                                restartingCustomerSession = false;
                                startCustomerSession(true);
                            });
                    }
                }

                return;
            }

            connectButton->setEnabled(
                !connected);

            disconnectButton->setEnabled(
                connected);

            codeEntry->setEnabled(
                !connected);

            if (connected) {
                providerWasConnected = true;

                provideStatus->setText(
                    QStringLiteral(
                        "Session connected. Choose Remote "
                        "Control Customer when you are "
                        "ready to view a display."));
            } else if (providerWasConnected) {
                providerWasConnected = false;
                codeEntry->clear();

                provideStatus->setText(
                    QStringLiteral(
                        "The support session ended."));
            }
        });

    QObject::connect(
        lanSession,
        &LanSession::providerFrameReceived,
        providerScreenView,
        &RemoteView::setFrame);

    QObject::connect(
        lanSession,
        &LanSession::providerFrameReceived,
        providerScreenWindow,
        [
            receiveButton,
            providerScreenWindow
        ](
            const QImage &)
        {
            if (!receiveButton->isChecked()) {
                return;
            }

            if (
                !providerScreenWindow->isVisible() &&
                !providerScreenWindow->property(
                    "userDismissed").toBool()) {
                providerScreenWindow->showNormal();
                providerScreenWindow->raise();
                providerScreenWindow->activateWindow();
            }
        });

    QObject::connect(
        lanSession,
        &LanSession::providerShareChanged,
        window,
        [
            receiveButton,
            providerScreenView,
            providerScreenWindow,
            shareProviderScreenButton,
            shareSourceCombo,
            refreshShareSourcesButton
        ](
            bool active)
        {
            if (receiveButton->isChecked()) {
                if (active) {
                    providerScreenWindow->setProperty(
                        "userDismissed",
                        false);
                } else {
                    providerScreenView->clearFrame();

                    providerScreenWindow->setProperty(
                        "programmaticClose",
                        true);

                    providerScreenWindow->close();

                    providerScreenWindow->setProperty(
                        "programmaticClose",
                        false);

                    providerScreenWindow->setProperty(
                        "userDismissed",
                        false);
                }

                return;
            }

            shareProviderScreenButton->setText(
                active
                    ? QStringLiteral(
                          "Stop Sharing")
                    : QStringLiteral(
                          "Share My Screen"));

            shareSourceCombo->setEnabled(
                !active);

            refreshShareSourcesButton->setEnabled(
                !active);
        });

    QObject::connect(
        lanSession,
        &LanSession::
            remoteControlDisplaysReceived,
        remoteWindow,
        [
            lanSession,
            remoteDisplayLayout,
            remoteDisplayButtonGroup,
            remoteWindowFullScreenButton,
            remoteWindowView,
            fullScreenRemoteView,
            &selectedRemoteDisplayId
        ](
            const QStringList &displayIds,
            const QStringList &displayLabels)
        {
            selectedRemoteDisplayId.clear();

            remoteWindowFullScreenButton->
                setEnabled(false);

            remoteWindowView->clearFrame();
            fullScreenRemoteView->clearFrame();

            while (
                QLayoutItem *item =
                    remoteDisplayLayout->
                        takeAt(0)
            ) {
                if (QWidget *widget =
                        item->widget()) {
                    widget->deleteLater();
                }

                delete item;
            }

            if (
                displayIds.isEmpty() ||
                displayIds.size() !=
                    displayLabels.size()
            ) {
                auto *emptyLabel =
                    makeLabel(
                        QStringLiteral(
                            "No customer displays "
                            "are available."),
                        QStringLiteral(
                            "smallText"));

                remoteDisplayLayout->addWidget(
                    emptyLabel);

                remoteDisplayLayout->addStretch();
                return;
            }

            for (
                int index = 0;
                index < displayIds.size();
                ++index
            ) {
                const QString displayId =
                    displayIds.at(index);

                const QString displayLabel =
                    displayLabels.at(index);

                auto *displayButton =
                    makeButton(
                        displayLabel,
                        QStringLiteral(
                            "secondaryButton"));

                displayButton->setCheckable(
                    true);

                displayButton->setToolTip(
                    QStringLiteral(
                        "View and control this "
                        "customer display"));

                remoteDisplayButtonGroup->
                    addButton(displayButton);

                remoteDisplayLayout->addWidget(
                    displayButton);

                QObject::connect(
                    displayButton,
                    &QPushButton::clicked,
                    displayButton,
                    [
                        lanSession,
                        displayButton,
                        remoteWindowFullScreenButton,
                        remoteWindowView,
                        fullScreenRemoteView,
                        displayId,
                        &selectedRemoteDisplayId
                    ]()
                    {
                        const bool changingDisplay =
                            selectedRemoteDisplayId !=
                            displayId;

                        selectedRemoteDisplayId =
                            displayId;

                        displayButton->setChecked(
                            true);

                        remoteWindowView->clearFrame();
                        fullScreenRemoteView->clearFrame();

                        if (changingDisplay) {
                            lanSession->
                                requestRemoteControlStop();
                        }

                        lanSession->
                            requestRemoteControlStart(
                                displayId);

                        remoteWindowFullScreenButton->
                            setEnabled(true);
                    });
            }

            remoteDisplayLayout->addStretch();
        });

    QObject::connect(
        lanSession,
        &LanSession::frameReceived,
        remoteWindowView,
        &RemoteView::setFrame);

    QObject::connect(
        lanSession,
        &LanSession::frameReceived,
        fullScreenRemoteView,
        &RemoteView::setFrame);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::pointerMoveRequested,
        lanSession,
        &LanSession::sendPointerMove);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::leftClickRequested,
        lanSession,
        &LanSession::sendLeftClick);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::leftButtonPressRequested,
        lanSession,
        &LanSession::sendLeftButtonPress);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::leftButtonReleaseRequested,
        lanSession,
        &LanSession::sendLeftButtonRelease);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::rightClickRequested,
        lanSession,
        &LanSession::sendRightClick);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::keyPressRequested,
        lanSession,
        &LanSession::sendKeyPress);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::keyReleaseRequested,
        lanSession,
        &LanSession::sendKeyRelease);

    QObject::connect(
        remoteWindowView,
        &RemoteView::pointerMoveRequested,
        lanSession,
        &LanSession::sendPointerMove);

    QObject::connect(
        remoteWindowView,
        &RemoteView::leftClickRequested,
        lanSession,
        &LanSession::sendLeftClick);

    QObject::connect(
        remoteWindowView,
        &RemoteView::leftButtonPressRequested,
        lanSession,
        &LanSession::sendLeftButtonPress);

    QObject::connect(
        remoteWindowView,
        &RemoteView::leftButtonReleaseRequested,
        lanSession,
        &LanSession::sendLeftButtonRelease);

    QObject::connect(
        remoteWindowView,
        &RemoteView::rightClickRequested,
        lanSession,
        &LanSession::sendRightClick);

    QObject::connect(
        remoteWindowView,
        &RemoteView::keyPressRequested,
        lanSession,
        &LanSession::sendKeyPress);

    QObject::connect(
        remoteWindowView,
        &RemoteView::keyReleaseRequested,
        lanSession,
        &LanSession::sendKeyRelease);

    QRect remoteWindowNormalGeometry;
    QScreen *remoteWindowScreen = nullptr;

    const auto exitRemoteFullScreen =
        [
            remoteWindow,
            remoteWindowView,
            fullScreenWindow,
            &remoteWindowNormalGeometry,
            &remoteWindowScreen
        ]()
        {
            fullScreenWindow->hide();

            if (remoteWindowScreen) {
                remoteWindow->setScreen(
                    remoteWindowScreen);
            }

            remoteWindow->showNormal();

            if (remoteWindowNormalGeometry.isValid()) {
                remoteWindow->setGeometry(
                    remoteWindowNormalGeometry);
            }

            remoteWindow->raise();
            remoteWindow->activateWindow();

            remoteWindowView->setFocus(
                Qt::OtherFocusReason);
        };

    QObject::connect(
        exitFullScreenShortcut,
        &QShortcut::activated,
        fullScreenWindow,
        exitRemoteFullScreen);

    QObject::connect(
        exitFullScreenBubble,
        &QPushButton::clicked,
        fullScreenWindow,
        exitRemoteFullScreen);

    const auto showRemoteFullScreen =
        [
            remoteWindow,
            fullScreenWindow,
            fullScreenRemoteView,
            exitFullScreenBubble,
            &remoteWindowNormalGeometry,
            &remoteWindowScreen
        ]()
        {
            remoteWindowNormalGeometry =
                remoteWindow->geometry();

            remoteWindowScreen =
                remoteWindow->screen();

            if (remoteWindowScreen) {
                fullScreenWindow->setScreen(
                    remoteWindowScreen);

                fullScreenWindow->setGeometry(
                    remoteWindowScreen->geometry());
            }

            remoteWindow->hide();

            fullScreenWindow->showFullScreen();
            fullScreenWindow->raise();
            fullScreenWindow->activateWindow();

            fullScreenRemoteView->setFocus(
                Qt::OtherFocusReason);

            exitFullScreenBubble->
                prepareForFullScreen();
        };

    QObject::connect(
        fullScreenButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            remoteWindow,
            remoteWindowView,
            remoteDisplayLayout,
            remoteWindowFullScreenButton,
            &selectedRemoteDisplayId
        ]()
        {
            selectedRemoteDisplayId.clear();

            remoteWindowView->clearFrame();

            remoteWindowFullScreenButton->
                setEnabled(false);

            while (
                QLayoutItem *item =
                    remoteDisplayLayout->
                        takeAt(0)
            ) {
                if (QWidget *widget =
                        item->widget()) {
                    widget->deleteLater();
                }

                delete item;
            }

            auto *loadingLabel =
                makeLabel(
                    QStringLiteral(
                        "Loading customer displays..."),
                    QStringLiteral(
                        "smallText"));

            remoteDisplayLayout->addWidget(
                loadingLabel);

            remoteDisplayLayout->addStretch();

            remoteWindow->showNormal();
            remoteWindow->raise();
            remoteWindow->activateWindow();

            lanSession->
                requestRemoteControlDisplays();
        });

    QObject::connect(
        remoteWindowFullScreenButton,
        &QPushButton::clicked,
        remoteWindow,
        showRemoteFullScreen);

    QObject::connect(
        openRemoteWindowButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            remoteWindow,
            remoteWindowView,
            remoteDisplayLayout,
            remoteWindowFullScreenButton,
            &selectedRemoteDisplayId
        ]()
        {
            selectedRemoteDisplayId.clear();

            remoteWindowView->clearFrame();

            remoteWindowFullScreenButton->
                setEnabled(false);

            while (
                QLayoutItem *item =
                    remoteDisplayLayout->
                        takeAt(0)
            ) {
                if (QWidget *widget =
                        item->widget()) {
                    widget->deleteLater();
                }

                delete item;
            }

            auto *loadingLabel =
                makeLabel(
                    QStringLiteral(
                        "Loading customer displays..."),
                    QStringLiteral(
                        "smallText"));

            remoteDisplayLayout->addWidget(
                loadingLabel);

            remoteDisplayLayout->addStretch();

            remoteWindow->showNormal();
            remoteWindow->raise();
            remoteWindow->activateWindow();

            remoteWindowView->setFocus(
                Qt::OtherFocusReason);

            lanSession->
                requestRemoteControlDisplays();
        });

    window->show();

    return application.exec();
}
