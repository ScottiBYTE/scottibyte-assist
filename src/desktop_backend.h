#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class DesktopBackend : public QObject
{
    Q_OBJECT

public:
    explicit DesktopBackend(
        QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DesktopBackend() override = default;

    virtual bool isSupported() const = 0;

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

signals:
    void frameReady(
        const QImage &frame);

    void statusChanged(
        const QString &status);

    void errorOccurred(
        const QString &message);
};
