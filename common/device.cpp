#include <windows.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include "device.h"

static const GUID GUID_DEVINTERFACE_USB_DEVICE = {
    0xA5DCBF10, 0x6530, 0x11D2,
    {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}
};


QStringList RetrieveDevice(uint16_t vid, uint16_t pid)
{
    QStringList ret;
    TCHAR     temp[260] = {0};
    CONFIGRET cr = CR_SUCCESS;
    HRESULT   hr = S_OK;
    PTSTR     DeviceInterfaceList = NULL;
    ULONG     DeviceInterfaceListLength = 0;
    ULONG     i, j, idx;
    QString   idFilter, dev;

    idFilter = QString("USB#VID_%1&PID_%2")
                      .arg(vid, 4, 16, QChar('0'))
                      .arg(pid, 4, 16, QChar('0')).toUpper();

    /**
     * Enumerate all devices exposing the interface. Do this in a loop
     * in case a new interface is discovered while this code is executing,
     * causing CM_Get_Device_Interface_List to return CR_BUFFER_SMALL.
     */
    do {
        cr = CM_Get_Device_Interface_List_Size(&DeviceInterfaceListLength,
                                               (LPGUID)&GUID_DEVINTERFACE_USB_DEVICE,
                                               NULL,
                                               CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr != CR_SUCCESS) {
            //hr = HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
            break;
        }

        DeviceInterfaceList = (PTSTR)HeapAlloc(GetProcessHeap(),
                                               HEAP_ZERO_MEMORY,
                                               DeviceInterfaceListLength * sizeof(TCHAR));
        if (DeviceInterfaceList == NULL) {
            hr = E_OUTOFMEMORY;
            break;
        }

        cr = CM_Get_Device_Interface_List((LPGUID)&GUID_DEVINTERFACE_USB_DEVICE,
                                          NULL,
                                          DeviceInterfaceList,
                                          DeviceInterfaceListLength,
                                          CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr != CR_SUCCESS) {
            HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
            if (cr != CR_BUFFER_SMALL) {
                //hr = HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
            }
        }
    } while (cr == CR_BUFFER_SMALL);

    if (FAILED(hr)) {
        return ret;
    }

    /* If the interface list is empty, no devices were found. */
    if (*DeviceInterfaceList == TEXT('\0')) {
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
        return ret;
    }

    /* Save all devices retrieved by "GUID_DEVINTERFACE_USBApplication1" */
    for (i = 0, j = 0; j < DeviceInterfaceListLength; j++) {
        if (DeviceInterfaceList[j] == '\0') {
            if (i) {
                dev = QString::fromWCharArray(temp);
                idx = dev.indexOf(idFilter);
                if (idx > 0 && idx < 10)
                    ret.append(dev);

                i = 0;
            }
            ZeroMemory(temp, sizeof(temp));
        } else {
            temp[i++] = DeviceInterfaceList[j];
        }
    }

    HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);

    return ret;
}
