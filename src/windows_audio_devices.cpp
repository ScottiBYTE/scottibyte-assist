#include "audio_devices.h"

#include <algorithm>

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include <QString>

namespace
{

QString wideToQString(
    const wchar_t *value)
{
    if (value == nullptr) {
        return QString();
    }

    return QString::fromWCharArray(value);
}

void appendDevices(
    QList<AudioDevice> &destination,
    IMMDeviceEnumerator *enumerator,
    EDataFlow dataFlow)
{
    IMMDeviceCollection *collection = nullptr;

    const HRESULT collectionResult =
        enumerator->EnumAudioEndpoints(
            dataFlow,
            DEVICE_STATE_ACTIVE,
            &collection);

    if (
        FAILED(collectionResult) ||
        collection == nullptr
    ) {
        return;
    }

    UINT count = 0;

    if (FAILED(collection->GetCount(&count))) {
        collection->Release();
        return;
    }

    for (UINT index = 0; index < count; ++index) {
        IMMDevice *device = nullptr;

        if (
            FAILED(
                collection->Item(
                    index,
                    &device)) ||
            device == nullptr
        ) {
            continue;
        }

        LPWSTR deviceId = nullptr;

        QString nodeName;
        QString description;

        if (SUCCEEDED(device->GetId(&deviceId))) {
            nodeName =
                wideToQString(deviceId)
                    .trimmed();

            CoTaskMemFree(deviceId);
        }

        IPropertyStore *properties = nullptr;

        if (
            SUCCEEDED(
                device->OpenPropertyStore(
                    STGM_READ,
                    &properties)) &&
            properties != nullptr
        ) {
            PROPVARIANT value;
            PropVariantInit(&value);

            if (
                SUCCEEDED(
                    properties->GetValue(
                        PKEY_Device_FriendlyName,
                        &value)) &&
                value.vt == VT_LPWSTR &&
                value.pwszVal != nullptr
            ) {
                description =
                    wideToQString(
                        value.pwszVal)
                        .trimmed();
            }

            PropVariantClear(&value);
            properties->Release();
        }

        if (!nodeName.isEmpty()) {
            bool duplicate = false;

            for (
                const AudioDevice &existing :
                destination
            ) {
                if (
                    existing.nodeName ==
                    nodeName
                ) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                destination.append(
                    {
                        nodeName,
                        description.isEmpty()
                            ? nodeName
                            : description
                    });
            }
        }

        device->Release();
    }

    collection->Release();
}

}

AudioDeviceInventory queryAudioDevices()
{
    AudioDeviceInventory inventory;

    const HRESULT initializeResult =
        CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED);

    const bool uninitializeCom =
        SUCCEEDED(initializeResult);

    if (
        FAILED(initializeResult) &&
        initializeResult !=
            RPC_E_CHANGED_MODE
    ) {
        inventory.error =
            QStringLiteral(
                "Windows Core Audio could not "
                "be initialized.");

        return inventory;
    }

    IMMDeviceEnumerator *enumerator = nullptr;

    const HRESULT enumeratorResult =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void **>(
                &enumerator));

    if (
        FAILED(enumeratorResult) ||
        enumerator == nullptr
    ) {
        inventory.error =
            QStringLiteral(
                "Windows audio devices could "
                "not be enumerated.");

        if (uninitializeCom) {
            CoUninitialize();
        }

        return inventory;
    }

    appendDevices(
        inventory.inputs,
        enumerator,
        eCapture);

    appendDevices(
        inventory.outputs,
        enumerator,
        eRender);

    enumerator->Release();

    if (uninitializeCom) {
        CoUninitialize();
    }

    const auto byDescription =
        [](
            const AudioDevice &left,
            const AudioDevice &right)
        {
            return left.description
                       .localeAwareCompare(
                           right.description) < 0;
        };

    std::sort(
        inventory.inputs.begin(),
        inventory.inputs.end(),
        byDescription);

    std::sort(
        inventory.outputs.begin(),
        inventory.outputs.end(),
        byDescription);

    return inventory;
}
