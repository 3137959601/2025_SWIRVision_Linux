#ifndef TRANSFER_THREAD_H
#define TRANSFER_THREAD_H

#include <QObject>
#include <QThread>
#include <QFile>
#include <QMutex>
#include "common/tih_usb_device.h"
#include "common/usb_frame_pipeline.h"
#include "common/usb_types.h"
#include <QImage>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#ifdef Q_OS_LINUX
#include <condition_variable>
#include <mutex>
#include <vector>
struct libusb_transfer;
#endif
class transferThread : public QThread
{
    Q_OBJECT
public:
    enum {
        DATA_PATTERN_RANDOM,
        DATA_PATTERN_INCREMENT,
        DATA_PATTERN_CONSTANT
    };

    transferThread(tihUSBDevice *dev, bool check, bool mode,
                   std::shared_ptr<swir::usb::FrameAssembler> frameAssembler = {});
    ~transferThread();
    void setUsbPipe(uint8_t id, UsbPipeType type);
    void setDataPattern(uint32_t type, uint32_t size);
    void setIsoInfo(uint32_t nbytes, uint32_t interval);

    void stop();
    /***************************************************/
    void setFrameSpec(int w, int h, int headerBytes = 16,
                      int bytesPerPixel = 2);
    // === 数据流保存的全局开关 ===
    static void startStreamSave(const QString &fileName);
    static void stopStreamSave();
    QImage image;

    QElapsedTimer signalTimer; // 用于计时信号发送间隔的定时器
    //static bool end_flag;
public slots:
    //void working();

signals:
    void sendpic(uchar usb_pic[]);
    void updatapic();
    void specChanged(int w, int h, int bytesPerPixel);
    // USB 实际到达帧率
    void usbFpsChanged(double fps);
private:
    int frameWidth  = 640;
    int frameHeight = 512;
    int frameHeader = 16; // 你协议里帧头长度
    int pixelBytes  = 2;  // 16bit

    // 1秒内累计的完整帧数
    int usbFpsCount = 0;
    // === 所有USB线程共享的保存资源 ===
    static QMutex s_streamMutex;
    static QFile  s_streamFile;
    static bool   s_streamEnabled;
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
    UsbPipeType usbPipeType = UsbPipeType::Unknown;

    uint32_t transferPackSize;
    uint32_t transferDataType;

    uchar *transBuf;

    std::atomic_bool stopFlag{false};

    // 每个Bulk IN端点保留独立残包状态，四个端点共享同一个完整帧组装器。
    swir::usb::RowStreamParser rowStreamParser;
    std::shared_ptr<swir::usb::FrameAssembler> frameAssembler;

#ifdef Q_OS_LINUX
    std::mutex linuxTransferMutex;
    std::condition_variable linuxTransferFinished;
    std::vector<libusb_transfer *> linuxTransfers;
    std::size_t linuxActiveTransfers = 0;
    std::uint64_t linuxNextStatsBytes = 64ULL * 1024ULL * 1024ULL;
    bool linuxFirstBytesLogged = false;
    void cancelLinuxTransfers();
    static void linuxTransferCallback(libusb_transfer *transfer);
#endif



    /* ISO EP only */
    uint32_t isoNbytes = 0;
    uint32_t isoInterval = 0;
#ifdef Q_OS_WIN
    HANDLE isoHandle = INVALID_HANDLE_VALUE;
#else
    void *isoHandle = nullptr;
#endif

    void isoTransfer();
    void bulkTransfer();
    void processReceivedBytes(const std::uint8_t *data, std::size_t size);

    /* data check */
    uint32_t lastWord = 0, currentWord = 0;
    void dataCheck(char *buf, uint32_t nBytes);
};


#endif // TRANSFER_THREAD_H
