#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QMutex>
#include <QElapsedTimer>

#include <atomic>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstdint>

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// 你的图像小部件（提供 s_imgMutex / image / pic）
#include "widget_image.h"

// ImageProcessor：专职图像处理线程
// - 固定 16bit 输入；输出 16bit 或 8bit（右移 8 位）
// - 两点校正（整数乘加版）
// - 静态坏点（BPM）：两点参考生成 + 填洞 + 前沿替代
class ImageProcessor : public QThread
{
    Q_OBJECT
public:
    explicit ImageProcessor(QObject *parent=nullptr);
    void stop() { m_stop.store(true, std::memory_order_relaxed); }

signals:
    void updataimage();                   // 通知 UI 重绘（widget_image::repaintImage）
    void captureStatus(const QString&);   // 状态提示（采集/校准/BPM）
    void calibrationReady(bool ok);       // 两点校准完成与否

public slots:
    // 规格：仅关心宽高，输入固定 16bit
    void setSourceSpec(int w, int h, int /*bytesPerPixel*/) {
        m_srcW.store(w, std::memory_order_relaxed);
        m_srcH.store(h, std::memory_order_relaxed);
    }

    // 显示位宽：16 或 8（8 就是丢弃低 8 位）
    void setOutputBits(int bits) { m_outputBits = (bits==8 ? 8 : 16); }

    // 两点校正开关
    void enableTwoPoint(bool on) { twoPointEnabled = on; }

    // 参考采集（低 / 高）
    void startCaptureLow();
    void startCaptureHigh();
    void clearCalibration();

    // 静态坏点：开关 / 重建 / 清除
    void enableBadPixelFix(bool on) { bpmEnabled = on; }
    void rebuildBPM();   // 用两点参考生成 BPM
    void clearBPM();     // 清空 BPM

    // 有新帧（被 USB/拼帧聚合器触发）
    void recv_data() { m_hasFrame.store(true, std::memory_order_relaxed); }

protected:
    void run() override;

private:
    // ========= 输入规格 / 线程控制 =========
    std::atomic<int>  m_srcW{640}, m_srcH{512};
    std::atomic<bool> m_hasFrame{false};
    std::atomic<bool> m_stop{false};
    int  m_outputBits = 16;          // 16 或 8
    bool twoPointEnabled = false;    // NUC 开关

    // ========= 两点校正（参考/参数）=========
    QMutex calibMutex;               // 保护参考/校准/BPM
    bool capturingLow=false, capturingHigh=false, calibrated=false;
    int  sampleFrameNum = 8;

    // 参考帧缓存（16U，直接存 DN）
    std::vector<cv::Mat> lowBuf, highBuf;     // CV_16UC1
    // 参考均值（32F）
    cv::Mat lowMean, highMean;                // CV_32FC1
    // K/B（32F，用于可视化或将来算法），以及整数量化后的 Kq/Bq（帧内使用）
    cv::Mat Kmat, Bmat;                       // CV_32FC1
    cv::Mat Kq, Bq;                           // CV_32SC1（int32）
    int     Qfrac = 14;                       // 定点小数位（推荐14）
    bool    kbQuantized = false;              // Kq/Bq 是否已量化就绪

    void computeCalibrationLocked();          // 计算 K/B，并量化到 Kq/Bq

    // 整型 NUC：src16 -> out16（0..65535）
    void applyTwoPointNUC_Int(const cv::Mat &src16, cv::Mat &out16);

    // ========= 静态坏点（BPM）=========
    bool    bpmEnabled = false;
    cv::Mat bpm;                               // CV_8UC1：0=好，1=坏

    // 阈值（默认值稳定，后续可做 UI 配置）
    float gainMinRatio = 0.6f;   // 响应（high-low）相对中位数下限
    float gainMaxRatio = 1.4f;   // 响应（high-low）相对中位数上限
    int   dsnuAbs      = 120;    // 低场偏置绝对阈值（DN）
    float dsnuKmad     = 5.0f;   // 低场偏置鲁棒阈值：k * MAD（含 1.4826）
    int   linResidualDN= 1300;   // 线性残差阈值（DN）
    int   blackThresh  = 64;     // 死黑阈值（低场）
    int   whiteThresh  = 64;     // 死白阈值（高场）

    void buildBPMFromTwoPointLocked();        // 用两点参考生成 BPM
    static void fillHolesInPlace(cv::Mat &mask1); // 填洞：防环形坏点
    void applyStaticBPM(const cv::Mat &mask1, cv::Mat &img16); // 前沿队列替代（16U）

    // 统计辅助：中位数 / MAD（鲁棒）
    static float medianOfMat32F(const cv::Mat &m);
    static float madOfMat32F(const cv::Mat &m, float med);
};

#endif // IMAGE_PROCESSOR_H
