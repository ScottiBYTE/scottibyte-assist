#pragma once

#include <QList>
#include <QString>

struct AudioDevice
{
    QString nodeName;
    QString description;
};

struct AudioDeviceInventory
{
    QList<AudioDevice> inputs;
    QList<AudioDevice> outputs;

    QString error;
};

AudioDeviceInventory queryAudioDevices();
