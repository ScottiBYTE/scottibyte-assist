#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>

struct ei;
struct ei_device;

class QSocketNotifier;

class LibeiInput final : public QObject
{
    Q_OBJECT

public:
    explicit LibeiInput(QObject *parent = nullptr);
    ~LibeiInput() override;

    bool pointerReady() const;
    bool absolutePointerReady() const;
    bool buttonReady() const;
    bool scrollReady() const;
    bool keyboardReady() const;

public slots:
    void start(int portalFileDescriptor);
    void stop();

    void movePointer();

    void movePointerRelative(
        int deltaX,
        int deltaY);

    void movePointerAbsolute(
        int x,
        int y,
        int frameWidth,
        int frameHeight);

    void clickLeftButton();
    void scrollDown();
    void typeLowercaseA();

signals:
    void statusChanged(const QString &status);

    void pointerReadyChanged(bool ready);
    void absolutePointerReadyChanged(bool ready);
    void buttonReadyChanged(bool ready);
    void scrollReadyChanged(bool ready);
    void keyboardReadyChanged(bool ready);

    void errorOccurred(const QString &message);

private slots:
    void processEvents();

private:
    void setPointerDevice(struct ei_device *device);

    void setAbsolutePointerDevice(
        struct ei_device *device);

    void setButtonDevice(struct ei_device *device);
    void setScrollDevice(struct ei_device *device);
    void setKeyboardDevice(struct ei_device *device);

    void clearPointerDevice();
    void clearAbsolutePointerDevice();
    void clearButtonDevice();
    void clearScrollDevice();
    void clearKeyboardDevice();

    void updateDeviceResumeState(
        struct ei_device *device,
        bool resumed);

    void removeDevice(
        struct ei_device *device);

    void startDeviceEmulation(
        struct ei_device *device);

    void stopDeviceEmulation(
        struct ei_device *device);

    uint32_t nextSequence();

    struct ei *ei_ = nullptr;

    struct ei_device *pointerDevice_ = nullptr;

    struct ei_device *absolutePointerDevice_ =
        nullptr;

    struct ei_device *buttonDevice_ = nullptr;
    struct ei_device *scrollDevice_ = nullptr;
    struct ei_device *keyboardDevice_ = nullptr;

    QSocketNotifier *notifier_ = nullptr;

    bool pointerResumed_ = false;
    bool absolutePointerResumed_ = false;
    bool buttonResumed_ = false;
    bool scrollResumed_ = false;
    bool keyboardResumed_ = false;

    QSet<struct ei_device *> emulatingDevices_;

    uint32_t sequence_ = 0;
};
