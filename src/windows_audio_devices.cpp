#include "audio_devices.h"

AudioDeviceInventory queryAudioDevices()
{
    AudioDeviceInventory inventory;

    inventory.error =
        QStringLiteral(
            "Windows audio device discovery is not "
            "implemented yet.");

    return inventory;
}
