#ifndef TRANSFER_THREAD_H
#define TRANSFER_THREAD_H

#include <QObject>
#include <QThread>
#include <Windows.h>
#include "../common/tih_usb_device.h"
#include <QImage>
#include <atomic>

class transferThread : public QThread
{
    Q_OBJECT
public:
    enum {
        DATA_PATTERN_RANDOM,
        DATA_PATTERN_INCREMENT,
        DATA_PATTERN_CONSTANT
    };

    transferThread(tihUSBDevice *dev, bool check, bool mode);
    ~transferThread();
    void setUsbPipe(uint8_t id, USBD_PIPE_TYPE type);
    void setDataPattern(uint32_t type, uint32_t size);
    void setIsoInfo(uint32_t nbytes, uint32_t interval);

    void stop();
    std::atomic_bool stopFlag{false};
    /***************************************************/
    void setFrameSpec(int w, int h, int headerBytes = 16, int bytesPerPixel = 2) {
        frameWidth = w;
        frameHeight = h;
        frameHeader = headerBytes;
        pixelBytes  = bytesPerPixel;
        emit specChanged(frameWidth, frameHeight, pixelBytes);
    }

    QImage image;

    QElapsedTimer signalTimer; // 用于计时信号发送间隔的定时器
    //static bool end_flag;
public slots:
    //void working();

signals:
    void sendpic(uchar usb_pic[]);
    void updatapic();
    void specChanged(int w, int h, int bytesPerPixel);
private:
    int frameWidth  = 640;
    int frameHeight = 512;
    int frameHeader = 16; // 你协议里帧头长度
    int pixelBytes  = 2;  // 16bit


protected:
    void run();

#define SIGNATURE_HIGH_SPEED   0x55AA55AA
#define SIGNATURE_LOW_SPEED    0xAA55AA55

    typedef struct out_cmd {
        uint32_t signature;
        uint32_t nbytes;
        uint32_t channel;
        uint32_t reserved;
    } out_cmd_t;

    tihUSBDevice *usbInterface;
    bool dataCheckEnable = false;
    bool highSpeedModeEn = false;
    uint8_t usbPipeID;
    USBD_PIPE_TYPE usbPipeType;

    uint32_t transferPackSize;
    uint32_t transferDataType;

    uchar *transBuf;

//    bool stopFlag = false;



    /* ISO EP only */
    uint32_t isoNbytes = 0;
    uint32_t isoInterval = 0;
    HANDLE   isoHandle = INVALID_HANDLE_VALUE;

    void isoTransfer();
    void bulkTransfer();

    /* data check */
    uint32_t lastWord = 0, currentWord = 0;
    void dataCheck(char *buf, uint32_t nBytes);
};


#endif // TRANSFER_THREAD_H
