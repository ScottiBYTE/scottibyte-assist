#pragma once

#include "desktop_backend.h"

#include <QList>
#include <QPoint>
#include <QString>

class QTimer;

class X11DesktopBackend final
    : public DesktopBackend
{
    Q_OBJECT

public:
    struct ShareSource
    {
        QString id;
        QString label;
    };

    explicit X11DesktopBackend(
        QObject *parent = nullptr);

    bool isSupported() const override;

    QList<DisplaySource>
        availableRemoteControlDisplays() const override;

    bool setRemoteControlDisplay(
        const QString &displayId) override;

    QList<ShareSource>
        availableShareSources() const;

    void setShareSource(
        const QString &sourceId);

    QString shareSource() const;

public slots:
    void start() override;
    void stop() override;

    void movePointerTo(
        int x,
        int y) override;

    void clickLeftAt(
        int x,
        int y) override;

    void pressLeftAt(
        int x,
        int y) override;

    void releaseLeftAt(
        int x,
        int y) override;

    void clickRightAt(
        int x,
        int y) override;

    void scrollWheel(
        int delta) override;

    void pressKey(
        int qtKey) override;

    void releaseKey(
        int qtKey) override;

private slots:
    void captureFrame();

private:
    QPoint remoteInputPoint(
        int x,
        int y) const;

    QImage captureEntireDesktop() const;
    QImage captureScreen(
        int screenIndex) const;

    QImage captureWindow(
        unsigned long windowId) const;

    QTimer *captureTimer_ = nullptr;

    QString shareSourceId_ =
        QStringLiteral("desktop");

    unsigned long lastCursorSerial_ = 0;
};
