#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <atomic>
#include <QMutex>
#include <qelapsedtimer.h>
#include <QFileInfo>
#include <QDir>
#include <QtEndian>     // 用于大小端安全

#include <vector>
#include <algorithm>
#include <cmath>

#include "opencv2/opencv.hpp"
#include <opencv2/imgproc.hpp>
#include <omp.h>

#include "widget_image.h"

//using namespace cv;

class ImageProcessor : public QThread
{
    Q_OBJECT
public:
    explicit ImageProcessor(QObject *parent = nullptr);
    void stop();  // 声明一个 stop 函数

    QElapsedTimer signalTimer; // 用于计时信号发送间隔的定时器
signals:
    void updataimage();
    void captureStatus(const QString& msg);            // 采集状态
    void calibrationReady(bool ok);                    // 校准完成信号
    void sourceFpsChanged(double fps);                 //帧率更新信号
public slots:
    void setSourceSpec(int w, int h, int bytesPerPixel) {
        m_srcW.store(w, std::memory_order_relaxed);
        m_srcH.store(h, std::memory_order_relaxed);
        m_srcBpp.store(bytesPerPixel, std::memory_order_relaxed);
    }
    void setOutputBits(int bits) { m_outputBits = (bits==8?8:16); }          // 8 或 16
    bool saveFrame(const QString& filePath);                            //图像保存
    // 两点校正控制
    void enableTwoPoint(bool on) { twoPointEnabled = on; /*qDebug()<<"NUC toggled"<<on<<this;*/}
    void startCaptureLow();
    void startCaptureHigh();
    void startCaptureDarkest();
    void enableDarkestOffsetCorrection(bool on) { darkestOffsetEnabled = on; }
    void clearCalibration();
    void setSampleFrames(int n)  { sampleFrameNum = std::clamp(n, 1, 128); } // 参考帧数

    // 开/关坏点
    void enableBadPixelFix(bool on) { bpmEnabled = on; }
    // 用两点数据生成 BPM（需要 lowMean/highMean/K/B 已计算好）
    void rebuildBPM() { QMutexLocker lk(&calibMutex); buildBPMFromTwoPointLocked(); }
    // 清空 BPM
    void clearBPM()   { QMutexLocker lk(&calibMutex); bpm.release(); }
    // ===== BPM 参数在线调整 =====
    void setGainMin(double v);
    void setGainMax(double v);
    void setDsnuAbs(int v);
    void setDsnuKmad(double v);
    void setLinResidualDN(int v);
    void setBlackThresh(int v);
    void setWhiteThresh(int v);
    // =====中值滤波算法 =====
    void enableMedianFiltering(bool on) { MedianFilterEnabled = on; /*qDebug()<<"MedianFilter toggled"<<on<<this;*/}
    // =====直方图均衡算法 =====
    void enableEqualizeHist(bool on) { EqualizeHistEnabled = on; /*qDebug()<<"EqualizeHistEnabled toggled"<<on<<this;*/}
    void setEqualizeHistThresholds(int upper, int lower);
    void enableEqualizeHistDownsample(bool on) { EqualizeHistDownsampleEnabled.store(on, std::memory_order_release); }
    void equalizeHist16(cv::Mat1w& img16, int bitMaxEff);
    // 可选：统一设置并一次重建
    void setBpmParams(double gmin, double gmax, int dsnuAbsV, double dsnuKmadK,
                      int linResid, int blackT, int whiteT, bool rebuild=true);
    void recv_data();
    // 保存当前 BPM 到文件（PNG/JPG/BMP 均可，推荐 .png）
    bool saveBPM(const QString& filePath);
    // 从文件加载 BPM（尺寸必须与当前源一致；非0视为坏点）
    bool loadBPM(const QString& filePath);

protected:
    void run();
private:
    bool stopFlag=false;  // 用于控制线程终止的标志位

    std::atomic<int> m_srcW{2048};
    std::atomic<int> m_srcH{2048};
    std::atomic<int> m_srcBpp{2};   // 1=8bit, 2=16bit

    std::atomic<bool> m_hasFrame{false}; // 简单“新帧到达”标记
    //统计帧率
    QElapsedTimer fpsTimer;
    int fpsCount = 0;
    // ===== 两点校正相关 =====
    QMutex calibMutex;              // 保护下列校准资源
    bool twoPointEnabled = false;   // UI 开关
    bool darkestOffsetEnabled = false;
    bool capturingLow  = false;
    bool capturingHigh = false;
    bool capturingDarkest = false;
    bool calibrated    = false;
    bool darkestCalibrated = false;
    int  sampleFrameNum = 8;        // 每个参考至少采几帧求均值
    int  m_inputBits  = 16;         // 输入有效位（12/14/16 或 8）
    int  m_outputBits = 16;         // 输出位宽（8 或 16）

    std::vector<cv::Mat> lowBuf, highBuf, darkestBuf; // 暂存参考帧（16U）
    cv::Mat lowMean, highMean;            // 参考均值（32F）
    cv::Mat darkestMean;                  // 最暗场参考均值（32F）
    cv::Mat Kmat, Bmat;                   // 每像素增益/偏置（32F）
    cv::Mat Kq, Bq, BqDarkest;                // CV_32SC1（int32）
    int     Qfrac = 14;                       // 定点小数位（推荐14）
    bool    kbQuantized = false;              // Kq/Bq 是否已量化就绪
    // === 有效满量程与健壮性参数 ===
    int   bitMaxEff   = 65535;   // 根据解码方式设置（标准左移=65535；若丢最高位再<<2≈32764）
    int   minDeltaDN  = 50;      // 最小分母：H-L 小于该值时，进入兜底/钳位（可按噪声调节）
    float KClampMin   = 0.20f;   // K 的下限（避免过小）
    float KClampMax   = 5.00f;   // K 的上限（避免过大）

    // 生成 K/B
    void computeCalibrationLocked(int bitMax);
    void computeDarkestOffsetLocked();
    // 应用 NUC：dstF = clip(K*srcF + B, 0..bitMax)
    void applyTwoPointNUC(const cv::Mat& srcF, cv::Mat& dstF, int bitMax);
    // 整型 NUC：src16 -> out16（0..65535）
    void applyTwoPointNUC_Int(const cv::Mat &src16, cv::Mat &out16);
    void applyDarkestOffsetNUC_Int(const cv::Mat &src16, cv::Mat &out16);

    // ======= 静态坏点掩膜：参数与存储 =======
    bool bpmEnabled = false;     // UI 开关：是否启用坏点替代
    cv::Mat1b bpm;               // 0=好像素, 1=坏像素（与图像同尺寸）

    // 阈值（可按需改成 setXXX 接口供 UI 调整）
    float gainMinRatio = 0.5f;   // 增益下限（相对中位数）
    float gainMaxRatio = 1.5f;   // 增益上限（相对中位数）
    int   dsnuAbs      = 120;    // 低场偏置的绝对阈值（DN）
    float dsnuKmad     = 5.0f;   // 低场偏置的鲁棒阈值：k*MAD
    int   linResidualDN= 1300;   // 线性残差阈值（DN），≈2% of 65535 1300
    int   blackThresh  = 64;     // 死黑阈值（低场）
    int   whiteThresh  = 64;     // 死白阈值（高场）

    // 生成/应用 BPM 的内部函数
    void buildBPMFromTwoPointLocked();
    void applyStaticBPM(cv::Mat1w& img16); // 在 16U 工作图上替代
    void applyStaticBPM_LeftThenUp(const cv::Mat1b& mask1, cv::Mat1w& img16);
    void applyStaticBPM_LeftThenUp_Optimized(const cv::Mat1b& mask1, cv::Mat1w& img16);     //复杂都O(HW)

    //生成盲元替代表并存储，直接替代盲元，不用每次都查询
    // 仅坏点的“目的->来源”映射（线性索引）
    struct BpmRemapEntry { int dst; int src; }; // dst = y*W + x, src = sy*W + sx

    std::vector<BpmRemapEntry> bpmRemap; // 只存坏点的拷贝关系
    int remapW = 0, remapH = 0;          // 记录分辨率，便于校验是否需要重建

    // 构建与应用
    void buildBPMRemap_LeftThenUp_Locked();     // 需在持有 calibMutex 时调用
    void applyBPM_WithRemap(cv::Mat1w& img16);  // 每帧按表替换（O(B)）

    // 统计量工具（鲁棒中位数 & MAD）
    static float medianOfMat32F(const cv::Mat& m);
    static float madOfMat32F(const cv::Mat& m, float med);
    std::atomic_int  EqualizeHistUpperThreshold{75};
    std::atomic_int  EqualizeHistLowerThreshold{15};
    std::atomic_bool EqualizeHistDownsampleEnabled{false};
    //中值滤波
    bool MedianFilterEnabled = false;   // UI 开关
    //直方图均衡化
    bool EqualizeHistEnabled = false;   //UI开关
};

#endif // IMAGE_PROCESSOR_H
