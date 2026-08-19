#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

class DesktopBackend : public QObject
{
    Q_OBJECT

public:
    struct DisplaySource
    {
        QString id;
        QString label;
    };

    explicit DesktopBackend(
        QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DesktopBackend() override = default;

    virtual bool isSupported() const = 0;

    virtual QList<DisplaySource>
        availableRemoteControlDisplays() const = 0;

    virtual bool setRemoteControlDisplay(
        const QString &displayId) = 0;

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void movePointerTo(
        int x,
        int y) = 0;

    virtual void clickLeftAt(
        int x,
        int y) = 0;

    virtual void pressLeftAt(
        int x,
        int y) = 0;

    virtual void releaseLeftAt(
        int x,
        int y) = 0;

    virtual void clickRightAt(
        int x,
        int y) = 0;

    virtual void scrollWheel(
        int delta) = 0;

    virtual void pressKey(
        int qtKey) = 0;

    virtual void releaseKey(
        int qtKey) = 0;

signals:
    void frameReady(
        const QImage &frame);

    void cursorPositionChanged(
        int x,
        int y);

    void cursorImageChanged(
        const QImage &image,
        int hotspotX,
        int hotspotY);

    void statusChanged(
        const QString &status);

    void errorOccurred(
        const QString &message);
};
