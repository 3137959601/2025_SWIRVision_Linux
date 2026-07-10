#include <QTime>
#include <QDebug>
#include <math.h>
#include "transfer_thread.h"
#include <string.h>
#include <atomic>

#include "widget_image.h"
//#define PACK_CONTINUE_CHECK
#include <vector>
#include <QSemaphore>

#ifdef PACK_CONTINUE_CHECK
#define MAX_REQ_QUEUE    1
#else
#define MAX_REQ_QUEUE    64     //256
#endif

extern uint64_t volatile g_transOk;
extern uint64_t volatile g_transErr;
static QMutex mutex;

QFile errFile("./err_log.txt");
int errLogFlag = 0;

// 全局变量声明
uchar *buf_1[2*MAX_REQ_QUEUE];
//unsigned short usb_pic[2048][2048];
std::vector<uint16_t> usb_pic;

int index_cnt=0;

QMutex bufferMutex;  // 用于线程安全的互斥锁
QSemaphore bufferAccess(1);  // 信号量初始值为 1，表示一个线程可以访问

// === 数据流保存的静态资源 ===
QMutex transferThread::s_streamMutex;
QFile  transferThread::s_streamFile;
bool   transferThread::s_streamEnabled = false;

transferThread::transferThread(tihUSBDevice *dev, bool check, bool mode)
{
    usbInterface = dev;
    dataCheckEnable = check;
    highSpeedModeEn = mode;

    transferDataType = DATA_PATTERN_CONSTANT;
    transferPackSize = 0;

    transBuf = NULL;
    stopFlag = false;

//    signalTimer.start(); // 开始计时
}

transferThread::~transferThread()
{
    if (isoHandle != INVALID_HANDLE_VALUE) {
        mutex.lock();
        usbInterface->unRegisterIsoBuffer(isoHandle);
        mutex.unlock();
    }

    if (transBuf) {
        delete[] transBuf;
    }

    mutex.lock();
    usbInterface->flush(usbPipeID);
    mutex.unlock();
}
void transferThread::startStreamSave(const QString &fileName)
{
    QMutexLocker locker(&s_streamMutex);

    if (s_streamFile.isOpen())
        s_streamFile.close();

    s_streamFile.setFileName(fileName);
    // 用 Truncate 是为了每次点“开始”都是一个干净文件
    if (s_streamFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        s_streamEnabled = true;
    } else {
        s_streamEnabled = false;
    }
}

void transferThread::stopStreamSave()
{
    QMutexLocker locker(&s_streamMutex);
    s_streamEnabled = false;
    if (s_streamFile.isOpen())
        s_streamFile.close();
}

void transferThread::setDataPattern(uint32_t type, uint32_t size)
{
    transferDataType = type;
    transferPackSize = size;
}

void transferThread::setIsoInfo(uint32_t nbytes, uint32_t interval)
{
    isoNbytes = nbytes;
    isoInterval = interval;
}

void transferThread::setUsbPipe(uint8_t id, USBD_PIPE_TYPE type)
{
    usbPipeID = id;
    usbPipeType = type;
}

void transferThread::stop()
{
    stopFlag = true;
    while (this->isRunning());
}

void transferThread::dataCheck(char *buf, uint32_t nBytes)
{
    uint32_t *word = (uint32_t *)buf;
    uint32_t nWords = nBytes / 4;
    uint32_t i = 0;

    if (nBytes % 4) {
        mutex.lock();
        errFile.write(QString("%1[EP_0x%2]:PackSizeErr[%3]-HeadWord[0x%4]\n")
                      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                      .arg(usbPipeID, 2, 16, QChar('0'))
                      .arg(nBytes)
                      .arg(word[0], 8, 16, QChar('0')).toLatin1());
        errFile.flush();
        mutex.unlock();
    }

#ifdef PACK_CONTINUE_CHECK
    if ((lastWord + 1) != word[0]) {
        mutex.lock();
        errFile.write(QString("%1[EP_0x%2]:PackDisContinue-Expacted[0x%3]-Actual[0x%4]\n")
                      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                      .arg(usbPipeID, 2, 16, QChar('0'))
                      .arg(lastWord, 8, 16, QChar('0'))
                      .arg(word[0], 8, 16, QChar('0')).toLatin1());
        errFile.flush();
        mutex.unlock();
    }
#endif

    currentWord = word[0];

    for (i = 0; i < nWords; i++, currentWord++) {
        if (currentWord != word[i]) {
            mutex.lock();
            errFile.write(QString("%1[EP_0x%2]:WordDisContinue[%3]-Expacted[0x%4]-Actual[0x%5]\n")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                          .arg(usbPipeID, 2, 16, QChar('0'))
                          .arg(i)
                          .arg(currentWord, 8, 16, QChar('0'))
                          .arg(word[i], 8, 16, QChar('0')).toLatin1());
            errFile.flush();
            currentWord = word[i];
            mutex.unlock();
        }
    }

    lastWord = word[i - 1];
}

void transferThread::isoTransfer()
{
//    uint32_t i;
//    int seed;
//    char random;
//    uint32_t transfered;
//    bool ret;
//    long errCode;

//    OVERLAPPED ov;
//    uint32_t packetCnt;
//    PUSBD_ISO_PACKET_DESCRIPTOR isoRd = NULL;

//    /* select iso buffer size: use 100ms */
//    /**
//     * Interval = 125us * n; ==> n = 2^(isoInterval - 1)
//     * minimum service interval for ISO, it's include how many 125us
//     *
//     * So we calculate maxNbytes in 10ms:
//     * maxNbytes = ((100 * 1000us) / Interval) * isoNbytes
//     */
//    packetCnt = 100000 / (125 * pow(2, isoInterval - 1));
//    transferPackSize = packetCnt * isoNbytes;

//    if (usbPipeID & 0x80) {
//        isoRd = new USBD_ISO_PACKET_DESCRIPTOR[packetCnt];
//        if (isoRd == NULL)
//            goto END;
//    }

//    /* create iso buffer */
//    transBuf = new unsigned char[transferPackSize]();

//    /* register iso buffer */
//    mutex.lock();
//    isoHandle = usbInterface->registerIsoBuffer(usbPipeID,
//                                                (UCHAR *)transBuf,
//                                                transferPackSize);
//    mutex.unlock();
//    if (isoHandle == INVALID_HANDLE_VALUE) {
//        goto END;
//    }

//    /* init data */
//    switch (transferDataType) {
//        case DATA_PATTERN_CONSTANT:
//            memset(transBuf, 0xEF, transferPackSize);
//            break;
//        case DATA_PATTERN_RANDOM:
//            seed = QTime::currentTime().msec();
//            qsrand(seed);
//            for (i = 0; i < transferPackSize; i++) {
//                random = (char)(qrand() & 0xFF);
//                transBuf[i] = random;
//            }
//            break;
//        default:
//            break;
//    }

//    while (1) {
//        if (usbPipeID & 0x80)
//            memset(isoRd, 0, sizeof(USBD_ISO_PACKET_DESCRIPTOR) * packetCnt);
//        memset(&ov, 0, sizeof(OVERLAPPED));
//        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

//        mutex.lock();
//        if (usbPipeID & 0x80) {
//            ret = usbInterface->reqIsoRead(isoHandle,
//                                           transferPackSize,
//                                           packetCnt,
//                                           isoRd,
//                                           &ov);
//        } else {
//            ret = usbInterface->reqIsoWrite(isoHandle,
//                                            transferPackSize,
//                                            &ov);
//        }
//        mutex.unlock();

//        do {
//            mutex.lock();
//            ret = usbInterface->checkXferOver((ULONG *)&transfered, &ov);
//            errCode = GetLastError();
//            if (!ret) {
//                if (errCode != ERROR_IO_INCOMPLETE) {
//                    errFile.write(QString("%1[EP_%2]:errCode[%3]--[%4:%5]\n")
//                                  .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
//                                  .arg(usbPipeID)
//                                  .arg(errCode).arg(transferPackSize).arg(transfered).toLatin1());
//                    errFile.flush();
//                    mutex.unlock();
//                    break;
//                } else
//                    usleep(1);
//            }
//            mutex.unlock();
//        } while (!ret);

//        CloseHandle(ov.hEvent);

//        mutex.lock();
//        if (usbPipeID & 0x80) {
//            for (i = 0, transfered = 0; i < packetCnt; i++)
//                transfered += isoRd[i].Length;
//            g_transOk += transfered;
//            g_transErr += transferPackSize - transfered;
//        } else {
//            if (ret)
//                g_transOk += transferPackSize;
//            else
//                g_transErr += transferPackSize;
//        }
//        mutex.unlock();

//        if (stopFlag) {
//            break;
//        }

////        if (ret && dataCheckEnable && (usbPipeID & 0x80)) {
////            dataCheck(transBuf, transferPackSize);
////        }
//    }

//    END:
//    if (isoHandle != INVALID_HANDLE_VALUE) {
//        usbInterface->unRegisterIsoBuffer(isoHandle);
//        isoHandle = INVALID_HANDLE_VALUE;
//    }

//    if (isoRd != NULL)
//        delete[] isoRd;

//    if (transBuf) {
//        delete[] transBuf;
//        transBuf = NULL;
//    }
}

void transferThread::bulkTransfer()
{
    uint32_t i;
    uint32_t index;
    int seed;
    char random;
    uint32_t transfered;
    bool ret, reqNext = false;
    long errCode;
    uchar *buf[MAX_REQ_QUEUE];

    OVERLAPPED ov[MAX_REQ_QUEUE];
    OVERLAPPED ovcmd[MAX_REQ_QUEUE];

    out_cmd_t outcmd[MAX_REQ_QUEUE];

    transBuf = new unsigned char[transferPackSize * MAX_REQ_QUEUE]();
    if (transBuf == NULL)
        return;

    for (i = 0; i < MAX_REQ_QUEUE; i++){
        buf[i] = &transBuf[i * transferPackSize];

    }
    /* init data */
    switch (transferDataType) {
        case DATA_PATTERN_CONSTANT:
            memset(transBuf, 0xEF, transferPackSize * MAX_REQ_QUEUE);
            break;
        case DATA_PATTERN_RANDOM:
            seed = QTime::currentTime().msec();
            srand(seed);
            for (i = 0; i < transferPackSize * MAX_REQ_QUEUE; i++) {
                random = (char)(rand() & 0xFF);
                transBuf[i] = random;
            }
            break;
        default:
            break;
    }
    for (int i = 0; i < 2 * MAX_REQ_QUEUE; i++) {
        buf_1[i] = new uchar[transferPackSize]; // 为每个 buf_1[i] 分配内存
    }

    /* start request */
    for (i = 0; i < MAX_REQ_QUEUE; i++) {
        memset(ov + i, 0, sizeof(OVERLAPPED));
        ov[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (highSpeedModeEn) {
            memset(ovcmd + i, 0, sizeof(OVERLAPPED));
            ovcmd[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            memset(outcmd + i, 0, sizeof(out_cmd_t));
            outcmd[i].signature = SIGNATURE_HIGH_SPEED;
            outcmd[i].nbytes = transferPackSize;
            mutex.lock();
            ret = usbInterface->reqXfer((CHAR)usbPipeID,
                                        (UCHAR *)(outcmd + i),
                                        (ULONG)16,
                                        ovcmd + i);
            mutex.unlock();
        }


        mutex.lock();
        ret = usbInterface->reqXfer((CHAR)usbPipeID,
                                    (UCHAR *)buf[i],
                                    (ULONG)transferPackSize,
                                    ov + i);
        mutex.unlock();
    }

    index = 0;
    int frame_num;
    int package_num;
    int last_package_num = 0;

    uint32_t length;
    uint32_t j;
//    uchar buffer_1[1296];
//    uchar buffer_2[1051168];
    const int payloadBytes = frameWidth * pixelBytes;     // 每包的像素载荷字节数
    const int lookahead    = frameHeader + payloadBytes;  // 用于跨包拼接的追补字节

    std::vector<uchar> buffer_1(lookahead);
    std::vector<uchar> buffer_2(transferPackSize + lookahead);

//    std::vector<uchar> buffer_1;

    //std::vector<uint16_t> merged_values;
//    uint16_t merged_values[525584];
    usb_pic.resize(size_t(frameWidth) * frameHeight);
//     std::vector<ushort> usb_pic_temp;
//     usb_pic_temp.resize(512 * 640);
//    std::vector<std::vector<unsigned short>> usb_pic_temp(512, std::vector<unsigned short>(640));
    while (1) {
        if (ov[index].hEvent) {


//            mutex.lock();
            ret = usbInterface->checkXferOver((ULONG *)&transfered, ov + index);
            errCode = GetLastError();
            if (ret) {

//                QString USBPipeID_dec=QString::number(usbPipeID,16);
//                qDebug()<<"USBPipeID："<<USBPipeID_dec<<"index:"<<index<<"index_cnt："<<index_cnt<<"buf[0][0]:"<<buf[0][0];

                g_transOk += transfered;
                g_transErr += (transferPackSize - transfered);
                length=transfered;
                // === 如果开启了数据流保存，就把这一包原始USB数据写进去 ===
                if (s_streamEnabled) {
                    QMutexLocker lk(&s_streamMutex);
                    if (s_streamFile.isOpen()) {
                        s_streamFile.write(reinterpret_cast<const char*>(buf[index]), transfered);
                        // 想更保险一点也可以 s_streamFile.flush();
                    }
                }
bufferAccess.acquire();
//bufferMutex.lock();
                memcpy(buf_1[index_cnt], buf[index], length);

                if(index_cnt==0)
                {
                    memcpy(buffer_2.data(), buf_1[2*MAX_REQ_QUEUE-1], length);
                    memcpy(buffer_1.data(), buf_1[0], lookahead);
                }
                else
                {
                    memcpy(buffer_2.data(), buf_1[index_cnt-1], length);
                    memcpy(buffer_1.data(), buf_1[index_cnt],   lookahead);
                }
                if(++index_cnt==2*MAX_REQ_QUEUE)
                {
                    index_cnt=0;
                }
//                memcpy(buffer_2+length,buffer_1,1296);
                memcpy(buffer_2.data() + length, buffer_1.data(), lookahead);
//bufferMutex.unlock();
 bufferAccess.release();  // 释放信号量，允许其他线程访问 buf_1


                auto commitFrame = [&] {
                    QMutexLocker lk(&widget_image::s_imgMutex);
                    uint16_t* dst = widget_image::rawPtr();
                    const int dstStridePx = widget_image::rawStridePx();
                    if (dst && dstStridePx >= frameWidth) {
                        for (int y = 0; y < frameHeight; ++y) {
                            const uint16_t* s = usb_pic.data() + size_t(y) * frameWidth;
                            uint16_t* d = dst + size_t(y) * dstStridePx;
                            memcpy(d, s, size_t(frameWidth) * sizeof(uint16_t));
                        }
                    }
                    emit updatapic();
                };

                for(j=0;j<length;j++)
                {
                    if(buffer_2[j]==0x90&&buffer_2[j+1]==0xeb&&buffer_2[j+2]==0x00&&buffer_2[j+3]==0x00){

                        frame_num = buffer_2[j+10] + buffer_2[j+11]*256;
                        package_num = buffer_2[j+8] + buffer_2[j+9]*256;
                        const bool pkg_ok  = (package_num >= 1 && package_num <= frameHeight);
                        const bool span_ok = (j + frameHeader + payloadBytes) <= buffer_2.size();
                        if(pkg_ok && span_ok)
                        {
                            if (usb_pic.size() != size_t(frameWidth) * frameHeight)
                                usb_pic.resize(size_t(frameWidth) * frameHeight);
                            if (package_num == 1 && last_package_num > 1) {
                                commitFrame();
                            }
                        mutex.lock();
//                            memcpy(usb_pic[package_num-1],&buffer_2[j+frameHeader],payloadBytes);
                            uint16_t* dstRow = usb_pic.data() + size_t(package_num - 1) * frameWidth;
                            memcpy(dstRow, &buffer_2[j + frameHeader], size_t(frameWidth) * sizeof(uint16_t));
//                            memcpy(usb_pic_temp.data() + (package_num - 1)* frameWidth, &buffer_2[j + frameHeader], size_t(frameWidth) * sizeof(uint16_t));
                        mutex.unlock();
                            last_package_num = package_num;
                        }
                        j += (lookahead - 1);  // 跳过当前帧的头+载荷
                        if(package_num==frameHeight)
                        {
                            commitFrame();
                            // ===== USB 实际帧率统计（按完整帧）受4线程共用影响并不准确 =====
//                            usbFpsCount++;
//                            const qint64 ms = signalTimer.elapsed();
//                            if (ms >= 1000) {
//                                const double fps = usbFpsCount * 1000.0 / double(ms);
//                                emit usbFpsChanged(fps);
//                                usbFpsCount = 0;
//                                signalTimer.restart();
//                                qDebug()<<"USBFps:"<<fps;
//                            }
                        }

                    }
                }

                reqNext = true;
            } else {
                if (stopFlag)
                    reqNext = true;
                else {
                    if (errCode != ERROR_IO_INCOMPLETE) {
                        errFile.write(QString("%1:errCode[%2]--[%3:%4]-%5\n")
                                      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                                      .arg(errCode).arg(transferPackSize).arg(transfered).arg(index).toLatin1());
                        errFile.flush();
                        g_transErr += (transferPackSize - transfered);
                        reqNext = true;
                    } else{
                        //usleep(1); // cpu利用率 换 数据收发速率
//                        Sleep(0);
                    }
                }
            }
//            mutex.unlock();

            if (!stopFlag) {
                if (ret && dataCheckEnable && (usbPipeID & 0x80)) {
                    dataCheck((char *)buf[index], transferPackSize);
                }
            }

        }

        if (reqNext) {
            reqNext = false;
            CloseHandle(ov[index].hEvent);
            memset(ov + index, 0, sizeof(OVERLAPPED));
            if (highSpeedModeEn) {
                CloseHandle(ovcmd[index].hEvent);
                memset(ovcmd + index, 0, sizeof(OVERLAPPED));
            }

            if (stopFlag) {
                for (i = 0; i < MAX_REQ_QUEUE; i++) {
                    if (ov[i].hEvent)
                        break;
                }
                if (i == MAX_REQ_QUEUE)
                    goto END;
            } else {
                /* submit a request */
                ov[index].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (highSpeedModeEn) {
                    ovcmd[index].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                    mutex.lock();
                    ret = usbInterface->reqXfer((CHAR)usbPipeID,
                                                (UCHAR *)(outcmd + index),
                                                (ULONG)16,
                                                ovcmd + index);
                    // QString USBPipeID_dec=QString::number(usbPipeID,16);
                    // qDebug()<<"USBPipeID"<<USBPipeID_dec;

                    mutex.unlock();
                }

                mutex.lock();
                ret = usbInterface->reqXfer((CHAR)usbPipeID,
                                            (UCHAR *)buf[index],
                                            (ULONG)transferPackSize,
                                            ov + index);
                // QString USBPipeID_dec=QString::number(usbPipeID,16);
                // qDebug()<<"USBPipeID："<<USBPipeID_dec<<"index1:"<<index;
                //memcpy(buf_1[index_cnt], buf[index], length);
                mutex.unlock();

            }
        }

        if (++index == MAX_REQ_QUEUE)
        {
            index = 0;
        }

    }

    END:
//    for (int i = 0; i < 2 * MAX_REQ_QUEUE; i++) {
//        delete[] buf_1[i];
//    }
    if (transBuf) {
        delete[] transBuf;
        transBuf = NULL;

    }
    index_cnt=0;
    //qDebug()<<"-----------------------------------------";
}


void transferThread::run()
{
    //qDebug()<<"xferThread"<<QThread::currentThread();
    if (usbPipeID & 0x80)
        highSpeedModeEn = false;

    mutex.lock();
    errLogFlag++;
    if (!errFile.isOpen())
        errFile.open(QIODevice::ReadWrite | QIODevice::Append);
    mutex.unlock();

    if (usbPipeType == UsbdPipeTypeIsochronous)
        isoTransfer();
    else
        bulkTransfer();

    mutex.lock();
    errLogFlag--;
    if (errLogFlag == 0)
        errFile.close();
    mutex.unlock();
}
