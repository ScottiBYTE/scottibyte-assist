#include "lan_session.h"
#include "remote_view.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

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
    min-width: 58px;
    max-width: 58px;
    min-height: 58px;
    max-height: 58px;
    border: 2px solid #22d6ff;
    border-radius: 29px;
    background: qradialgradient(
        cx: 0.5, cy: 0.5,
        radius: 0.8,
        stop: 0 #4d165e,
        stop: 0.55 #101b59,
        stop: 1 #020a1b
    );
    color: #ffffff;
    font-size: 22px;
    font-weight: 900;
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

QPushButton:disabled {
    color: #70849a;
    border-color: #31516d;
    background: #17304a;
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
            QStringLiteral("SB"),
            QStringLiteral("logoBadge"));

    logo->setAlignment(
        Qt::AlignCenter);

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

    auto *remoteArea =
        makeCard(
            QStringLiteral("remoteArea"));

    remoteArea->setMinimumHeight(260);

    auto *remoteLayout =
        new QVBoxLayout(remoteArea);

    remoteLayout->setContentsMargins(
        8,
        8,
        8,
        8);

    remoteLayout->setSpacing(6);

    auto *remoteHeaderLayout =
        new QHBoxLayout;

    auto *remoteHeaderLabel =
        makeLabel(
            QStringLiteral(
                "Remote desktop"),
            QStringLiteral(
                "sectionHeading"));

    auto *fullScreenButton =
        makeButton(
            QStringLiteral("⛶"),
            QStringLiteral(
                "viewButton"));

    fullScreenButton->setToolTip(
        QStringLiteral(
            "View remote desktop full screen"));

    remoteHeaderLayout->addWidget(
        remoteHeaderLabel);

    remoteHeaderLayout->addStretch();

    remoteHeaderLayout->addWidget(
        fullScreenButton);

    auto *remoteView =
        new RemoteView;

    remoteLayout->addLayout(
        remoteHeaderLayout);

    remoteLayout->addWidget(
        remoteView,
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
                "Disconnect"),
            QStringLiteral("dangerButton"));

    disconnectButton->setEnabled(false);

    provideLayout->addWidget(provideTitle);
    provideLayout->addWidget(provideDescription);
    provideLayout->addWidget(codeEntry);
    provideLayout->addWidget(connectButton);
    provideLayout->addWidget(provideStatus);
    provideLayout->addWidget(remoteArea, 1);
    provideLayout->addWidget(disconnectButton);

    pages->addWidget(providePage);

    auto *lanSession =
        new LanSession(window);

    lanSession->startCustomer(
        supportCode->text());

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
            lanSession,
            supportCode,
            receiveStatus
        ]()
        {
            const QString code =
                createSupportCode();

            supportCode->setText(code);

            lanSession->startCustomer(
                code);

            receiveStatus->setText(
                QStringLiteral(
                    "Waiting for the person helping you..."));
        });

    QObject::connect(
        endSupportButton,
        &QPushButton::clicked,
        window,
        [
            lanSession,
            receiveStatus
        ]()
        {
            lanSession->disconnectSession();

            receiveStatus->setText(
                QStringLiteral(
                    "Support session ended."));
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
            QMessageBox::information(
                window,
                QStringLiteral(
                    "Settings"),
                QStringLiteral(
                    "Settings will be added after the "
                    "LAN remote-control proof."));
        });

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
            remoteView,
            fullScreenRemoteView,
            fullScreenWindow,
            codeEntry,
            connectButton,
            disconnectButton,
            provideStatus
        ]()
        {
            lanSession->disconnectSession();
            remoteView->clearFrame();
            fullScreenRemoteView->clearFrame();
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

    QObject::connect(
        lanSession,
        &LanSession::connectedChanged,
        window,
        [
            receiveButton,
            connectButton,
            disconnectButton,
            codeEntry
        ](
            bool connected)
        {
            if (!receiveButton->isChecked()) {
                connectButton->setEnabled(
                    !connected);

                disconnectButton->setEnabled(
                    connected);

                codeEntry->setEnabled(
                    !connected);
            }
        });

    QObject::connect(
        lanSession,
        &LanSession::frameReceived,
        remoteView,
        &RemoteView::setFrame);

    QObject::connect(
        lanSession,
        &LanSession::frameReceived,
        fullScreenRemoteView,
        &RemoteView::setFrame);

    QObject::connect(
        remoteView,
        &RemoteView::pointerMoveRequested,
        lanSession,
        &LanSession::sendPointerMove);

    QObject::connect(
        remoteView,
        &RemoteView::leftClickRequested,
        lanSession,
        &LanSession::sendLeftClick);

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

    const auto showRemoteFullScreen =
        [
            fullScreenWindow
        ]()
        {
            fullScreenWindow->showFullScreen();
            fullScreenWindow->raise();
            fullScreenWindow->activateWindow();
        };

    QObject::connect(
        fullScreenButton,
        &QPushButton::clicked,
        window,
        showRemoteFullScreen);

    QObject::connect(
        remoteView,
        &RemoteView::fullScreenRequested,
        window,
        showRemoteFullScreen);

    QObject::connect(
        fullScreenRemoteView,
        &RemoteView::fullScreenRequested,
        fullScreenWindow,
        &QWidget::close);

    window->show();

    return application.exec();
}
