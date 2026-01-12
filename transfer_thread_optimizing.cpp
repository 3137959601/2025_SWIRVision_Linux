#include <QTime>
#include <QMutex>
#include <QDebug>
#include <math.h>
#include "transfer_thread.h"
#include <QFile>
#include <string.h>

#include "widget_image.h"
//#define PACK_CONTINUE_CHECK
#include <vector>
#include <QElapsedTimer>
#include <QSemaphore>

#ifdef PACK_CONTINUE_CHECK
#define MAX_REQ_QUEUE    1
#else
#define MAX_REQ_QUEUE    32     //256 128   //修改后不能大于64，因为Windows 的 WaitForMultipleObjects 一次最多只能等 64 个句柄
#endif

extern uint64_t volatile g_transOk;
extern uint64_t volatile g_transErr;
static QMutex mutex;

QFile errFile("./err_log.txt");
int errLogFlag = 0;

// 全局变量声明
uchar *buf_1[2*MAX_REQ_QUEUE];
unsigned short usb_pic[2048][2048];
int index_cnt=0;

QMutex bufferMutex;  // 用于线程安全的互斥锁
QSemaphore bufferAccess(1);  // 信号量初始值为 1，表示一个线程可以访问

transferThread::transferThread(tihUSBDevice *dev, bool check, bool mode)
{
    usbInterface = dev;
    dataCheckEnable = check;
    highSpeedModeEn = mode;

    transferDataType = DATA_PATTERN_CONSTANT;
    transferPackSize = 0;

    transBuf = NULL;
    stopFlag = false;

    signalTimer.start(); // 开始计时
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
//    stopFlag = true;
    // 原子写：让采集线程“必然能看到”
    stopFlag.store(true, std::memory_order_release);

    // 立刻取消挂起 I/O，让 WaitForMultipleObjects 立即返回
//    usbInterface->flush(usbPipeID);

    //自添加
    mutex.lock();
    usbInterface->flush(usbPipeID); // 主动取消未决 I/O，唤醒等待
    mutex.unlock();

//    while (this->isRunning());
    // 不要 while(isRunning()) 自旋！改成带超时的等待，避免卡 UI
//    this->wait(1000);  // 最多等 1 秒；若仍未退出，先返回让 UI 不阻塞

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
    HANDLE hEvents[MAX_REQ_QUEUE];  //自添加
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
        hEvents[i]   = ov[i].hEvent;    //自添加
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

    uint32_t j;

    const int payloadBytes = frameWidth * pixelBytes;     // 每包的像素载荷字节数
    const int lookahead    = frameHeader + payloadBytes;  // 用于跨包拼接的追补字节

    std::vector<uchar> buffer_1(lookahead);
    std::vector<uchar> buffer_2(transferPackSize + lookahead);
    // 直接使用 buffer_1 作为“跨包追补”窗口（容量 = 帧头 + 一行像素载荷）
    size_t la_used = 0; // lookahead 已用字节数
    int currentFrame = -1;
    int lastEmittedFrame = -1;
    //构建活动等待集
    auto buildWaitList = [&](HANDLE* list, int &count) {
        count = 0;
        for (uint32_t k = 0; k < MAX_REQ_QUEUE; ++k) {
            if (ov[k].hEvent != NULL) {
                list[count++] = ov[k].hEvent;
            }
        }
    };

    while (1) {
        if (stopFlag.load(std::memory_order_acquire)) {
            // 这里的收尾逻辑你已经写好了：close 所有事件 → break
            // （无需再调用 flush，这里可以保留或删掉，留着也没问题）
            mutex.lock();
            usbInterface->flush(usbPipeID);
            mutex.unlock();

            for (uint32_t k = 0; k < MAX_REQ_QUEUE; ++k) {
                if (ov[k].hEvent) { CloseHandle(ov[k].hEvent); ov[k].hEvent = NULL; }
                if (highSpeedModeEn && ovcmd[k].hEvent) { CloseHandle(ovcmd[k].hEvent); ovcmd[k].hEvent = NULL; }
            }
            break;
        }

        // 仅等待仍然有效的事件；当 stop 且已无活动句柄时直接收尾退出
        HANDLE waitList[MAX_REQ_QUEUE];
        int    active = 0;
        buildWaitList(waitList, active);

        if (active == 0) {
            if (stopFlag) break;     // ★ 没有活动句柄且要求停止 → 跳出 while
            Sleep(1);
            continue;
        }

        DWORD dw = WaitForMultipleObjects(active, waitList, FALSE, 10);
        if (dw == WAIT_TIMEOUT)  { continue; }
        if (dw == WAIT_FAILED)   {
            DWORD le = GetLastError();
            errFile.write(QString("%1: WFMO FAILED, err=%2\n")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                          .arg(le).toLatin1());
            errFile.flush();
            Sleep(1);
            continue;
        }
        // 把 waitList 返回的句柄，反查到对应的 index
        HANDLE signaled = waitList[dw - WAIT_OBJECT_0];
        for (index = 0; index < MAX_REQ_QUEUE; ++index) {
            if (ov[index].hEvent == signaled) break;
        }
        if (index >= MAX_REQ_QUEUE) continue;  // 理论上不会发生，稳妥起见


        // —— 以下“checkXferOver / 解析 / 设 reqNext / 重投递”逻辑保持原有写法 ——
        // （注意：这里不用再 if (ov[index].hEvent) 和单槽位 WaitForSingleObject 了）
        mutex.lock();
        ret = usbInterface->checkXferOver((ULONG *)&transfered, ov + index);
        errCode = GetLastError();
        mutex.unlock();
        if (ret&& transfered > 0) {
            // 本次收到的数据指针/长度
             uchar* src = buf[index];
             uint32_t len = transfered;
//                QString USBPipeID_dec=QString::number(usbPipeID,16);
//                qDebug()<<"USBPipeID："<<USBPipeID_dec<<"index:"<<index<<"index_cnt："<<index_cnt<<"buf[0][0]:"<<buf[0][0];

            g_transOk += transfered;
            g_transErr += (transferPackSize - transfered);
            len=transfered;

            // -------- 只保留“跨包追补”所需的一点拷贝，避免多级 memcpy --------

            auto try_parse_and_commit = [&](uchar* base, size_t nbytes) {
                // 在 [base, base + nbytes) 中搜索帧头 90 EB 00 00
                const size_t payloadBytes = size_t(frameWidth) * size_t(pixelBytes);
                const size_t need = size_t(frameHeader) + payloadBytes;

                for (size_t j = 0; j + need <= nbytes; ) {
                    if (base[j] == 0x90 && base[j+1] == 0xEB && base[j+2] == 0x00 && base[j+3] == 0x00) {

                        int package_num = int(base[j+8]) + (int(base[j+9]) << 8);
                        int frame_num = int(base[j+10]) + (int(base[j+11]) << 8); // 如需可用

                        // 初始化或换帧（遇到新 frame_num 时，如果上一帧还没发布，先发布上一帧）
                        if (currentFrame == -1) currentFrame = frame_num;
                        if (frame_num != currentFrame) {
                            if (currentFrame != lastEmittedFrame) {
                                bufferMutex.lock();
                                memcpy(&widget_image::pic[0][0], &usb_pic[0][0], size_t(frameWidth) * size_t(frameHeight) * size_t(pixelBytes));
                                bufferMutex.unlock();
                                emit updatapic();
                                lastEmittedFrame = currentFrame;
                            }
                            currentFrame = frame_num;
                        }
                        if (package_num >= 1 && package_num <= int(frameHeight)) {
                            // ③ 直接把一行像素写入最终显示缓冲，避免 usb_pic 等中转
                            //    widget_image::pic 是 short[H][W]，payload 为 16bit 像素
                            bufferMutex.lock();
                            memcpy(&usb_pic[package_num - 1][0],
                                   &base[j + frameHeader],
                                   payloadBytes);
                            bufferMutex.unlock();
                            // 当前帧末行：一次性发布整帧
                            if (frame_num == currentFrame && package_num == int(frameHeight)) {
                                bufferMutex.lock();
                                memcpy(&widget_image::pic[0][0],
                                       &usb_pic[0][0],
                                       size_t(frameWidth) * size_t(frameHeight) * size_t(pixelBytes));
                                bufferMutex.unlock();
                                emit updatapic();
                                lastEmittedFrame = currentFrame;
                            }
                        }

                        j += need; // 跳过“帧头 + 一行载荷”
                    } else {
                        ++j; // 继续找帧头
                    }
                }
            };

            // 先把上一次没凑满“帧头+一行”的尾巴接上来解析
            if (la_used) {
                size_t take = std::min<size_t>(len, buffer_1.size() - la_used);
                memcpy(buffer_1.data() + la_used, src, take);
                try_parse_and_commit(buffer_1.data(), la_used + take);
                la_used = 0;
                src += take;
                len -= (uint32_t)take;
            }

            // 直接在本次缓冲上解析（零拷贝）
            if (len > 0) {
                try_parse_and_commit(src, len);

                // 保留最后最多 (frameHeader + payloadBytes) 字节作为下次的追补
                size_t keep = std::min<size_t>(len, buffer_1.size());
                memcpy(buffer_1.data(), src + (len - keep), keep);
                la_used = keep;
            }

            // 本次请求用完，需要重新提交
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
                    } //else
                        //usleep(1); // cpu利用率 换 数据收发速率                    // 否则是“还没好”，但我们已经等过一次事件了，换下一个槽位
                }
            }

            if (!stopFlag) {
                if (ret && dataCheckEnable && (usbPipeID & 0x80)) {
                    dataCheck((char *)buf[index], transferPackSize);
                }
            }

            // ③ 重新投递本槽位（保持在途容量）
        if (reqNext) {
            reqNext = false;
            CloseHandle(ov[index].hEvent);
            memset(ov + index, 0, sizeof(OVERLAPPED));
            hEvents[index] = NULL;  // ★ 关键：避免无效句柄溜进等待集
            if (highSpeedModeEn) {
                CloseHandle(ovcmd[index].hEvent);
                memset(ovcmd + index, 0, sizeof(OVERLAPPED));
            }

            if (stopFlag) {
                 // 退出前检查是否所有槽位都回收
                for (i = 0; i < MAX_REQ_QUEUE; i++) {
                    if (ov[i].hEvent)
                        break;
                }
                if (i == MAX_REQ_QUEUE)
                    goto END;
            } else {
                /* submit a request */
                ov[index].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                hEvents[index]   = ov[index].hEvent;  // ★ 自添加  更新事件数组
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
                mutex.unlock();

            }
            // 轮到下一个槽位
            index = (index + 1) % MAX_REQ_QUEUE;
        }
    }
    END:
    if (transBuf) {
        delete[] transBuf;
        transBuf = NULL;

    }
    for (uint32_t k = 0; k < MAX_REQ_QUEUE; ++k) {
        if (ov[k].hEvent) {
            CloseHandle(ov[k].hEvent);
            ov[k].hEvent = NULL;
        }
        if (highSpeedModeEn && ovcmd[k].hEvent) {
            CloseHandle(ovcmd[k].hEvent);
            ovcmd[k].hEvent = NULL;
        }
        hEvents[k] = NULL;
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
