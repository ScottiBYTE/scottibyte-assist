#include "audio_devices.h"
#include "customer_voice_audio.h"
#include "desktop_backend.h"
#include "lan_session.h"
#include "remote_view.h"
#include "wayland_desktop_backend.h"
#include "x11_desktop_backend.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace
{

QString createSupportCode()
{
    const int value =
        QRandomGenerator::global()->bounded(
            100000,
            1000000);

    const QString digits =
        QString::number(value);

    return digits.left(3) +
        QStringLiteral(" ") +
        digits.mid(3);
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
                        "No PipeWire audio input or "
                        "output devices were detected. "
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
            } else if (
                fullScreenWindow_ !=
                    nullptr) {
                fullScreenWindow_->close();
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
    QApplication application(
        argc,
        argv);

    application.setApplicationName(
        QStringLiteral(
            "ScottiBYTE Assist"));

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
    font-size: 15px;
    font-weight: 700;
    color: #4edcff;
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

    brandLayout->setSpacing(0);

    brandLayout->addWidget(
        makeLabel(
            QStringLiteral("ScottiBYTE"),
            QStringLiteral("brandTitle")));

    brandLayout->addWidget(
        makeLabel(
            QStringLiteral("Assist"),
            QStringLiteral("brandSubtitle")));

    auto *settingsButton =
        makeButton(
            QStringLiteral("⚙  Settings"),
            QStringLiteral("settingsButton"));

    headerLayout->addWidget(logo);
    headerLayout->addLayout(brandLayout);
    headerLayout->addStretch();
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
            createSupportCode(),
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

    auto *detailsButton =
        makeButton(
            QStringLiteral("ⓘ  Details"),
            QStringLiteral("secondaryButton"));

    receiveActions->addWidget(newCodeButton);
    receiveActions->addWidget(endSupportButton);
    receiveActions->addWidget(detailsButton);

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

    progressLayout->addWidget(
        makeLabel(
            QStringLiteral(
                "Preparing secure session..."),
            QStringLiteral("sectionHeading")));

    progressLayout->addWidget(
        makeLabel(
            QStringLiteral(
                "This usually takes only a moment."),
            QStringLiteral("smallText")));

    receiveLayout->addWidget(receiveTitle);
    receiveLayout->addWidget(receiveDescription);
    receiveLayout->addWidget(codeCard);
    receiveLayout->addLayout(statusRow);
    receiveLayout->addLayout(receiveActions);
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

    auto *remoteWindow =
        new QWidget;

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

    auto *remoteWindowFullScreenButton =
        makeButton(
            QStringLiteral("Full Screen Remote Control"),
            QStringLiteral(
                "secondaryButton"));

    remoteWindowHeader->addWidget(
        remoteWindowTitle);

    remoteWindowHeader->addStretch();

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

    QObject::connect(
        exitFullScreenShortcut,
        &QShortcut::activated,
        fullScreenWindow,
        &QWidget::close);

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

    auto *customerVoiceAudio =
        new CustomerVoiceAudio(window);

    providerScreenDismissFilter->
        setUserDismissedCallback(
            [lanSession]()
            {
                lanSession->
                    notifyProviderScreenClosed();
            });

    DesktopBackend *desktopBackend =
        nullptr;

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

                if (selectedIndex < 0 &&
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

    QString lastClipboardText;

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
    } else {
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
            supportCode,
            receiveStatus,
            newCodeButton,
            endSupportButton,
            &customerCodeConsumed,
            &restartingCustomerSession
        ](
            bool generateNewCode)
        {
            restartingCustomerSession = true;
            customerCodeConsumed = false;

            if (generateNewCode) {
                supportCode->setText(
                    createSupportCode());
            }

            newCodeButton->setEnabled(true);
            endSupportButton->setEnabled(false);

            receiveStatus->setText(
                QStringLiteral(
                    "Waiting for the person helping you..."));

            lanSession->startCustomer(
                supportCode->text());

            restartingCustomerSession = false;
        };

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
        [window]()
        {
            QMessageBox::information(
                window,
                QStringLiteral(
                    "Session details"),
                QStringLiteral(
                    "No active LAN connection.\n\n"
                    "Technical details will remain hidden "
                    "from the main interface."));
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
            codeEntry,
            connectButton,
            disconnectButton,
            provideStatus
        ]()
        {
            QString code =
                codeEntry->text();

            code.remove(' ');

            if (code.size() != 6) {
                provideStatus->setText(
                    QStringLiteral(
                        "Enter all six digits."));
                return;
            }

            lanSession->connectProvider(
                code);

            provideStatus->setText(
                QStringLiteral(
                    "Looking for the support computer on the LAN..."));

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
     * Provider-side voice receiver only.
     *
     * This does not alter LAN discovery or session transport.
     * Customer microphone transmission is not integrated yet.
     */
    QObject::connect(
        lanSession,
        &LanSession::connectedChanged,
        window,
        [
            customerVoiceAudio,
            lanSession,
            receiveButton,
            receiveStatus,
            provideStatus
        ](
            bool connected)
        {
            customerVoiceAudio->stop();

            if (!connected) {
                return;
            }

            QSettings settings(
                QStringLiteral("ScottiBYTE"),
                QStringLiteral("ScottiBYTE Assist"));

            if (receiveButton->isChecked()) {
                if (!customerVoiceAudio->
                        startCustomerSender(
                            lanSession->peerAddress(),
                            3100,
                            settings.value(
                                QStringLiteral(
                                    "voice/inputNode"))
                                .toString())) {
                    receiveStatus->setText(
                        QStringLiteral(
                            "Connected, but microphone "
                            "transmission could not start."));
                }

                return;
            }

            if (!customerVoiceAudio->
                    startProviderReceiver(
                        3100,
                        settings.value(
                            QStringLiteral(
                                "voice/outputNode"))
                            .toString())) {
                provideStatus->setText(
                    QStringLiteral(
                        "Connected, but voice playback "
                        "could not start."));
            }
        });

    QObject::connect(
        customerVoiceAudio,
        &CustomerVoiceAudio::errorOccurred,
        window,
        [
            receiveButton,
            provideStatus
        ](
            const QString &message)
        {
            if (receiveButton->isChecked()) {
                return;
            }

            provideStatus->setText(
                QStringLiteral(
                    "Voice playback error: ") +
                message);
        });

    QObject::connect(
        lanSession,
        &LanSession::connectedChanged,
        window,
        [
            receiveButton,
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

                    newCodeButton->setEnabled(false);
                    endSupportButton->setEnabled(true);
                } else {
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

                remoteWindow->showNormal();
                remoteWindow->raise();
                remoteWindow->activateWindow();

                remoteWindowView->setFocus(
                    Qt::OtherFocusReason);
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

    const auto showRemoteFullScreen =
        [
            fullScreenWindow,
            exitFullScreenBubble
        ]()
        {
            fullScreenWindow->showFullScreen();
            fullScreenWindow->raise();
            fullScreenWindow->activateWindow();

            exitFullScreenBubble->
                prepareForFullScreen();
        };

    QObject::connect(
        fullScreenButton,
        &QPushButton::clicked,
        window,
        showRemoteFullScreen);

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
            remoteWindow,
            remoteWindowView
        ]()
        {
            remoteWindow->showNormal();
            remoteWindow->raise();
            remoteWindow->activateWindow();

            remoteWindowView->setFocus(
                Qt::OtherFocusReason);
        });

    window->show();

    return application.exec();
}
