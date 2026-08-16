#include "image_processor.h"


static QMutex mutex;

ImageProcessor::ImageProcessor(QObject *parent) : QThread(parent)
{
    //signalTimer.start(); // 开始计时
    fpsTimer.start();   //fps计时
}

void ImageProcessor::recv_data() {
    m_hasFrame.store(true, std::memory_order_relaxed);
}

void ImageProcessor::run()
{
    constexpr int bitMax = 65535;
//    constexpr int bitMax = 32764;   //硬件端像素最大值8191左移2位->32764
    cv::Mat src16;         // 零拷贝视图（指向 widget_image::pic）
    cv::Mat1w  proc16;         // NUC 输出 16U
    cv::Mat1w srcShifted;      // ★新增：左移1位（×2）后的工作图
    cv::Mat1w srcStable;   // 本线程的稳定快照
    while(!stopFlag.load(std::memory_order_acquire)){
        if (!m_hasFrame.exchange(false, std::memory_order_relaxed)) {
            QThread::msleep(1);
            continue;
        }

        const int W = m_srcW.load(std::memory_order_relaxed);
        const int H = m_srcH.load(std::memory_order_relaxed);
        if (W<=0 || H<=0) continue;

        // 1) 源视图（16U）——不拷贝，直接包装 widget_image::pic
//        src16 = cv::Mat(H, W, CV_16UC1, (void*)&widget_image::pic[0][0]);
        // 显式 stride（以像素为单位的步长 * sizeof(uint16_t)）
        {
            QMutexLocker lk(&widget_image::s_imgMutex); // 只在封装瞬间短锁
            uint16_t* base = widget_image::rawPtr();
            const int stridePx = widget_image::rawStridePx();
            if (!base || stridePx <= 0) { continue; }
            src16 = cv::Mat(H, W, CV_16UC1, base, size_t(stridePx) * sizeof(uint16_t));
        }
        // === 立刻做整帧快照，避免撕裂 ===
        if (srcStable.rows != H || srcStable.cols != W) srcStable.create(H, W);
        src16.copyTo(srcStable);   // ← 关键！固定当前帧

        // 1.1) ★新增：输入预处理 —— 左移1位（×2，16U饱和）
        // 说明：cv::add 会做饱和运算，避免 16bit 溢出。
//        cv::add(src16, src16, srcShifted, cv::noArray(), CV_16U);
//        cv::add(srcStable, srcStable, srcShifted, cv::noArray(), CV_16U);
//        src16.convertTo(srcShifted, CV_16U, 4.0, 0.0);  // ×4，16U饱和
        // 可选：若担心原始缓冲区在拷贝/计算期间被生产者覆盖，可先做一次快照再 add：
        // cv::Mat raw0; src16.copyTo(raw0);
        // cv::add(raw0, raw0, srcShifted, cv::noArray(), CV_16U);

        // 2) 参考采集（直接用 16U）
        {
            QMutexLocker lk(&calibMutex);
            if (capturingLow) {
                lowBuf.emplace_back(); srcStable.copyTo(lowBuf.back());
                if ((int)lowBuf.size() >= sampleFrameNum) {
                    capturingLow = false;
                    emit captureStatus("暗场参考完成");
                }
            }
            if (capturingHigh) {
                highBuf.emplace_back(); srcStable.copyTo(highBuf.back());
                if ((int)highBuf.size() >= sampleFrameNum) {
                    capturingHigh = false;
                    emit captureStatus("亮场参考完成");
                }
            }
            if (capturingDarkest) {
                darkestBuf.emplace_back(); srcStable.copyTo(darkestBuf.back());
                if ((int)darkestBuf.size() >= sampleFrameNum) {
                    capturingDarkest = false;
                    computeDarkestOffsetLocked();
                }
            }
            // 样本到齐则计算 K/B 并量化
            if (!calibrated &&
                (int)lowBuf.size()  >= sampleFrameNum &&
                (int)highBuf.size() >= sampleFrameNum) {
                computeCalibrationLocked(bitMax);
            }
        }

        // 3) 两点校正（整数乘加）：若未开启或未校准，就直接拷贝
        if (darkestOffsetEnabled) {
            QMutexLocker lk(&calibMutex);
            if (darkestCalibrated && kbQuantized && !Kq.empty() && !BqDarkest.empty() &&
                Kq.size()==src16.size() && BqDarkest.size()==src16.size()) {
                cv::Mat rawSnap; srcStable.copyTo(rawSnap);
                applyDarkestOffsetNUC_Int(rawSnap, proc16);
            } else if (calibrated && kbQuantized && !Kq.empty() && !Bq.empty() &&
                       Kq.size()==src16.size() && Bq.size()==src16.size()) {
                cv::Mat rawSnap; srcStable.copyTo(rawSnap);
                applyTwoPointNUC_Int(rawSnap, proc16);
            } else {
                srcStable.copyTo(proc16);
            }
        } else if (twoPointEnabled) {
            QMutexLocker lk(&calibMutex);
            if (calibrated && kbQuantized && !Kq.empty() && !Bq.empty() &&
                Kq.size()==src16.size() && Bq.size()==src16.size()) {
                // 1) 封装零拷贝视图后，立即快照 多拷贝一次，避免帧撕裂的现象，后期可优化
                cv::Mat rawSnap; srcStable.copyTo(rawSnap);
                applyTwoPointNUC_Int(rawSnap, proc16);
//                applyTwoPointNUC_Int(src16, proc16);    // ← out: 16U
            } else {
                srcStable.copyTo(proc16);
            }
        } else {
            srcStable.copyTo(proc16);
        }
        // 4.5) 先转换成 16U 工作图（便于做 BPM）
        cv::Mat1w work16;
        proc16.copyTo(work16);
        // 4.6) 如果启用坏点替代且 BPM 有效，则应用
//        {
//            QMutexLocker lk(&calibMutex); // 复用校准互斥，保护 bpm 读
//            if (bpmEnabled && !bpm.empty() && bpm.size()==work16.size()) {
//                applyStaticBPM(work16);

//            }
//        }
        cv::Mat1b maskLocal;
        {
            QMutexLocker lk(&calibMutex); // 只在复制期间短锁，避免UI卡顿
            if (bpmEnabled && !bpm.empty() && bpm.size()==work16.size()) {
                maskLocal = bpm.clone();
            }
        }
        if (!maskLocal.empty()) {
            // 可选：若有填洞函数，避免环形坏点：fillHolesInPlace(maskLocal);
//            applyStaticBPM_LeftThenUp(maskLocal, work16);
//            applyStaticBPM_LeftThenUp_Optimized(maskLocal, work16);
            {
                QMutexLocker lk(&calibMutex);
                // 若 BPM 尺寸或内容变了（已在 set/rebuild 里重建），此处直接用表
                applyBPM_WithRemap(work16);
            }
        }
        // ===== 4.7) 中值滤波（16bit，OpenCV）=====
        if (MedianFilterEnabled) {
            // medianBlur 不支持真正的原地操作，这里用 thread_local 复用缓冲，避免频繁分配
            static thread_local cv::Mat1w medianTmp;
            if (medianTmp.size() != work16.size()) {
                medianTmp.create(work16.size());
            }
            // 3x3 中值：速度/效果折中最好；需要更强可改成 5（注意会更慢）
            cv::medianBlur(work16, medianTmp, 3);
            // work16.swap(medianTmp); // O(1) 交换，避免 copy
            std::swap(work16, medianTmp);// O(1) 交换，避免 copy
        }
        // ===== 4.8) 16bit 直方图均衡（手写）=====
        if (EqualizeHistEnabled) {
            equalizeHist16(work16, bitMaxEff);
        }
        // 5) 写回显示 QImage（16bit 或 8bit=右移8位）——用 work16 作为源
        if (m_outputBits==16) {
            QMutexLocker ui(&widget_image::s_imgMutex);
            const int dstW = widget_image::image.width();
            const int dstH = widget_image::image.height();
            const int bytesPerLine = widget_image::image.bytesPerLine();
            const int copyW = std::min(dstW, W);
            const int copyH = std::min(dstH, H);
            auto *dst = reinterpret_cast<uint16_t*>(widget_image::image.bits());
            const int dstStridePx = bytesPerLine/2;
            for (int y=0; y<copyH; ++y) {
                memcpy(dst + y*dstStridePx, work16.ptr<uint16_t>(y),
                       size_t(copyW) * sizeof(uint16_t));
            }
        } else {
            QMutexLocker ui(&widget_image::s_imgMutex);
            const int dstW = widget_image::image.width();
            const int dstH = widget_image::image.height();
            const int bytesPerLine = widget_image::image.bytesPerLine();
            const int copyW = std::min(dstW, W);
            const int copyH = std::min(dstH, H);
            uchar *dst8 = widget_image::image.bits();
            for (int y=0; y<copyH; ++y) {
                uchar* drow = dst8 + y*bytesPerLine;
                const uint16_t* srow = work16.ptr<uint16_t>(y);
                for (int x=0; x<copyW; ++x) drow[x] = uchar(uint32_t(srow[x]) >> 8); // 丢低 8 位
            }
        }
        emit updataimage(); // 解锁后刷新
        // === 源 FPS 统计 ===
        fpsCount++;
        qint64 ms = fpsTimer.elapsed();
        if (ms >= 1000) {
            double fps = fpsCount * 1000.0 / ms;
            emit sourceFpsChanged(fps);
            fpsCount = 0;
            fpsTimer.restart();
        }
    }
}

void ImageProcessor::stop() {
    stopFlag.store(true, std::memory_order_release);
}



void ImageProcessor::startCaptureLow()  {
    QMutexLocker lk(&calibMutex);
    lowBuf.clear();
    darkestBuf.clear();
    darkestMean.release();
    BqDarkest.release();
    capturingLow  = true;
    capturingHigh = false;
    capturingDarkest = false;
    calibrated = false;
    darkestCalibrated = false;
    emit captureStatus("开始采集暗场参考");
}
void ImageProcessor::startCaptureHigh() {
    QMutexLocker lk(&calibMutex);
    highBuf.clear();
    darkestBuf.clear();
    darkestMean.release();
    BqDarkest.release();
    capturingHigh = true;
    capturingLow  = false;
    capturingDarkest = false;
    calibrated = false;
    darkestCalibrated = false;
    emit captureStatus("开始采集亮场参考");
}
void ImageProcessor::startCaptureDarkest() {
    QMutexLocker lk(&calibMutex);
    if (!calibrated || !kbQuantized || Kq.empty() || Bq.empty()) {
        emit captureStatus("请先完成亮/暗场两点校准");
        return;
    }
    darkestBuf.clear();
    darkestMean.release();
    BqDarkest.release();
    capturingDarkest = true;
    capturingLow = false;
    capturingHigh = false;
    darkestCalibrated = false;
    emit captureStatus("开始采集最暗场参考");
}
void ImageProcessor::clearCalibration() {
    QMutexLocker lk(&calibMutex);
    lowBuf.clear();
    highBuf.clear();
    darkestBuf.clear();
    capturingLow=false;
    capturingHigh=false;
    capturingDarkest=false;
    lowMean.release();
    highMean.release();
    darkestMean.release();
    Kmat.release();
    Bmat.release();
    Kq.release();
    Bq.release();
    BqDarkest.release();
    calibrated=false;
    darkestCalibrated=false;
    kbQuantized=false;
    emit calibrationReady(false);
    emit captureStatus("校正参数已清除");
}

//================================================NUP=============================================
// ========== 工具：求多帧均值 ==========
static cv::Mat meanOf(const std::vector<cv::Mat>& vec16U) {
    if (vec16U.empty()) return cv::Mat();
    const int h = vec16U[0].rows, w = vec16U[0].cols;
    cv::Mat acc(h, w, CV_32FC1, cv::Scalar(0));
    for (const auto& m : vec16U) {
        CV_Assert(m.type() == CV_16UC1 && m.rows==h && m.cols==w);
        cv::Mat f;
        m.convertTo(f, CV_32FC1);
        acc += f;
    }
    acc /= float(vec16U.size());
    return acc;
}

//两点校正公式：
//V_low_i = K_i * R_low_i + B_i
//V_high_i = K_i * R_high_i + B_i
//解得：K_i = (V_low - V_high) / (R_low_i - R_high_i)
//     B_i = (R_low_i * V_high - R_high_i * V_low) / (R_low_i - R_high_i)
//其中V_low和V_high是期望的输出值(通常取两幅参考图像的平均值)。
//它们通常取两幅参考图像（低温参考帧和高温参考帧）的全局平均值。
//代表了传感器在两种温度下的​​整体响应水平​​。通过让所有像素的校正结果向全局平均值对齐，可以消除像素间的非均匀性。

void ImageProcessor::computeCalibrationLocked(int bitMax)
{
    // 记录有效满量程，供运行期钳位使用
    bitMaxEff = bitMax;
    // 低高均值
    lowMean  = meanOf(lowBuf);
    highMean = meanOf(highBuf);
    if (lowMean.empty() || highMean.empty() || lowMean.size()!=highMean.size()) {
        calibrated = false;
        emit calibrationReady(false);
        emit captureStatus("参考均值无效");
        return;
    }

    const int H = lowMean.rows, W = lowMean.cols;
    // 2) 计算两幅参考的“全局平均”（标量）——作为目标输出 V_low / V_high
    const float T0_DN = static_cast<float>(cv::mean(lowMean)[0]);   // V_low
    const float T1_DN = static_cast<float>(cv::mean(highMean)[0]);  // V_high
    // 两个目标不能太接近，否则不可稳定求解
    const float tgtDiff = std::fabs(T0_DN - T1_DN);
    if (tgtDiff < 1.0f) { // 阈值可按噪声调，这里 1 DN 仅作保护
        calibrated = false;
        emit calibrationReady(false);
        emit captureStatus("两点目标过近，无法稳定校准");
        return;
    }
    // 3) 分母：逐像素 diff = (R_low - R_high)
    cv::Mat diff32F; cv::subtract(lowMean, highMean, diff32F); // 保留符号
    // 为了健壮性：记录全局分母的“鲁棒量级”，用于极端像素回退
    float dMedAbs; {
        // 取 |diff| 的中位数作为量级
        std::vector<float> v; v.reserve(size_t(diff32F.total()));
        for (int y=0; y<H; ++y) {
            const float* r = diff32F.ptr<float>(y);
            for (int x=0; x<W; ++x) v.push_back(std::fabs(r[x]));
        }
        if (v.empty()) { calibrated=false; emit calibrationReady(false); emit captureStatus("内部错误：diff为空"); return; }
        auto mid=v.begin()+v.size()/2;
        std::nth_element(v.begin(), mid, v.end());
        dMedAbs = *mid;
        if ((v.size()&1)==0) {
            auto mid2=v.begin()+v.size()/2-1;
            std::nth_element(v.begin(), mid2, v.end());
            dMedAbs = 0.5f*(dMedAbs + *mid2);
        }
        dMedAbs = std::max(dMedAbs, float(minDeltaDN)); // 不小于最小分母
    }
    qDebug()<<"dMedAbs"<<dMedAbs;
    // 4) 逐像素计算 K/B，并做健壮性处理与钳位
    Kmat.create(lowMean.size(), CV_32FC1);
    Bmat.create(lowMean.size(), CV_32FC1);

    for (int y=0; y<H; ++y) {
        const float* L = lowMean.ptr<float>(y);
        const float* Hh= highMean.ptr<float>(y);
        const float* D = diff32F.ptr<float>(y);
        float* K = Kmat.ptr<float>(y);
        float* B = Bmat.ptr<float>(y);

        for (int x=0; x<W; ++x) {
            float d = D[x]; // R_low - R_high（带符号）
            // a) 分母过小 → 用带符号的最小分母（保留 d 的符号，保证方向一致）
            if (std::fabs(d) < float(minDeltaDN)) {
                d = (d >= 0.f ? +float(minDeltaDN) : -float(minDeltaDN));
            }
            // （也可选择回退到全局分母 sign(d)*dMedAbs，这里采用固定最小分母更稳定）

            // b) 公式：K = (V_low - V_high) / (R_low - R_high)
            float k = (T0_DN - T1_DN) / d;

            // c) B = (R_low * V_high - R_high * V_low) / (R_low - R_high)
            float b = (L[x] * T1_DN - Hh[x] * T0_DN) / d;

            // d) K/B 钳位（防极端值放大噪声；K 典型接近 1）
            k = std::clamp(k, KClampMin, KClampMax);
            // B 的范围按经验给个对称上限（可收紧）
            const float bAbsMax = 4.0f * bitMaxEff;
            if (b >  bAbsMax) b =  bAbsMax;
            if (b < -bAbsMax) b = -bAbsMax;

            K[x] = k; B[x] = b;
        }
    }

    // 量化到定点（帧内整型乘加）
    Qfrac = 14; // 可按动态范围调（14~15 够用）
    const float scale = float(1 << Qfrac);
    Kq.create(Kmat.size(), CV_32SC1);
    Bq.create(Bmat.size(), CV_32SC1);
    for (int y=0; y<Kmat.rows; ++y) {
        const float *Kf=Kmat.ptr<float>(y), *Bf=Bmat.ptr<float>(y);
        int *Kqi=Kq.ptr<int>(y), *Bqi=Bq.ptr<int>(y);
        for (int x=0; x<Kmat.cols; ++x) {
            Kqi[x] = int(std::lround(Kf[x] * scale));
            Bqi[x] = int(std::lround(Bf[x]));
        }
    }
    kbQuantized = true;
    darkestCalibrated = false;
    BqDarkest.release();

    calibrated = true;
    if (!darkestMean.empty() && darkestMean.size() == Bq.size()) {
        computeDarkestOffsetLocked();
    }
    emit calibrationReady(true);
    emit captureStatus(QString("校准完成：%1x%2").arg(lowMean.cols).arg(lowMean.rows));
    buildBPMFromTwoPointLocked();   //同时生成生成静态坏点掩膜
    buildBPMRemap_LeftThenUp_Locked();  //同时生成盲元替换表
//    emit captureStatus(QString("BPM Remap 构建完成：坏点=%1，映射条目=%2")
//                       .arg(cv::countNonZero(bpm)).arg((int)bpmRemap.size()));
}

// ========== 应用 NUC：dstF = clip(K*srcF + B, 0..bitMax) ==========
void ImageProcessor::computeDarkestOffsetLocked()
{
    if (!calibrated || !kbQuantized || Bq.empty()) {
        darkestCalibrated = false;
        emit captureStatus("请先完成亮/暗场两点校准");
        return;
    }

    if (!darkestBuf.empty()) {
        darkestMean = meanOf(darkestBuf);
    }
    if (darkestMean.empty() || darkestMean.size() != Bq.size()) {
        darkestCalibrated = false;
        BqDarkest.release();
        emit captureStatus("最暗场参考无效或尺寸不匹配");
        return;
    }

    const int darkestGlobalMean = int(std::lround(cv::mean(darkestMean)[0]));
    BqDarkest.create(Bq.size(), CV_32SC1);
    for (int y = 0; y < Bq.rows; ++y) {
        const int* b = Bq.ptr<int>(y);
        int* out = BqDarkest.ptr<int>(y);
        for (int x = 0; x < Bq.cols; ++x) {
            out[x] = b[x] - darkestGlobalMean;
        }
    }

    darkestCalibrated = true;
    emit captureStatus(QString("最暗场参考完成：%1x%2，全局均值=%3 DN")
                           .arg(darkestMean.cols).arg(darkestMean.rows).arg(darkestGlobalMean));
}

void ImageProcessor::applyTwoPointNUC(const cv::Mat& srcF, cv::Mat& dstF, int bitMax)
{
    CV_Assert(!Kmat.empty() && !Bmat.empty());
    dstF.create(srcF.size(), CV_32FC1);
    for (int y = 0; y < srcF.rows; ++y) {
        const float* s = srcF.ptr<float>(y);
        const float* K = Kmat.ptr<float>(y);
        const float* B = Bmat.ptr<float>(y);
        float* d = dstF.ptr<float>(y);
        for (int x = 0; x < srcF.cols; ++x) {
            float v = K[x] * s[x] + B[x];
            if (v < 0.f) v = 0.f;
            else if (v > bitMax) v = float(bitMax);
            d[x] = v;
        }
    }
}
// 整型 NUC：out = clamp(((Kq * in) >> Qfrac) + Bq, 0..65535)
void ImageProcessor::applyTwoPointNUC_Int(const cv::Mat &src16, cv::Mat &out16) {
    CV_Assert(src16.type()==CV_16UC1);
    out16.create(src16.size(), CV_16UC1);
    const int H=src16.rows, W=src16.cols;
    const int Q = Qfrac;
    const int vmax = bitMaxEff;        // ★ 按当前有效满量程钳位
    // 开 OMP 可再提速（如需）：
    // #pragma omp parallel for
    for (int y=0; y<H; ++y) {
        const uint16_t *s = src16.ptr<uint16_t>(y);
        const int *Kqi = Kq.ptr<int>(y);
        const int *Bqi = Bq.ptr<int>(y);
        uint16_t *d = out16.ptr<uint16_t>(y);
        for (int x=0; x<W; ++x) {
            int v = (int)(((int64_t)Kqi[x] * s[x]) >> Q);
            v += Bqi[x];
            if (v < 0) v = 0; else if (v > vmax) v = vmax;
            d[x] = (uint16_t)v;
        }
    }
}

void ImageProcessor::applyDarkestOffsetNUC_Int(const cv::Mat &src16, cv::Mat &out16) {
    CV_Assert(src16.type()==CV_16UC1);
    out16.create(src16.size(), CV_16UC1);
    const int H=src16.rows, W=src16.cols;
    const int Q = Qfrac;
    const int vmax = bitMaxEff;
    for (int y=0; y<H; ++y) {
        const uint16_t *s = src16.ptr<uint16_t>(y);
        const int *Kqi = Kq.ptr<int>(y);
        const int *Bqi = BqDarkest.ptr<int>(y);
        uint16_t *d = out16.ptr<uint16_t>(y);
        for (int x=0; x<W; ++x) {
            int v = (int)(((int64_t)Kqi[x] * s[x]) >> Q);
            v += Bqi[x];
            if (v < 0) v = 0; else if (v > vmax) v = vmax;
            d[x] = (uint16_t)v;
        }
    }
}
// ====================================================BPM==========================================
// ===== 统计工具：中位数 & MAD（鲁棒尺度）=====
float ImageProcessor::medianOfMat32F(const cv::Mat& m) {
    std::vector<float> v; v.reserve(size_t(m.total()));
    for (int y=0; y<m.rows; ++y) {
        const float* row = m.ptr<float>(y);
        v.insert(v.end(), row, row + m.cols);   //将矩阵数据复制到向量中​
    }
    if (v.empty()) return 0.f;
    auto mid = v.begin() + v.size()/2;
    std::nth_element(v.begin(), mid, v.end());
    float med = *mid;
    if ((v.size() & 1) == 0) { // 偶数个：取中间两数均值（可选）
        auto mid2 = v.begin() + v.size()/2 - 1;
        //部分排序算法,它会重新排列向量 [v.begin(), v.end())范围内的元素
        //std::nth_element会​​修改向量 v中元素的顺序​​。但因为 v是矩阵数据的副本，所以不会影响原始的输入矩阵 m
        std::nth_element(v.begin(), mid2, v.end());
        med = 0.5f * (med + *mid2);
    }
    return med;
}
//​​计算 MAD（中位数绝对偏差） 即​​所有数据点与其中位数绝对偏差的中位数,​​ MAD = median( |X_i - median(X)| )
//与标准差的对比​​：
//​​标准差​​：衡量数据围绕​​均值​​的波动情况。但它对​​异常值（Outliers）非常敏感​​，一个巨大的异常值就会使标准差变得很大，从而失去代表性。
//​​MAD​​：衡量数据围绕​​中位数​​的波动情况。因为中位数本身对异常值不敏感，所以 MAD 也对异常值​​不敏感​​（鲁棒）。它反映了大多数“正常”数据点的离散程度。
//用途​​：MAD 是检测异常值的黄金标准之一。一个常用的经验法则是：如果某个数据点与中位数的偏差​​超过 3 * MAD * 1.4826​​，那么它很可能是一个异常值。（系数 1.4826 是为了让 MAD 在正态分布下与标准差估计一致）。
float ImageProcessor::madOfMat32F(const cv::Mat& m, float med) {
    std::vector<float> dev; dev.reserve(size_t(m.total()));
    for (int y=0; y<m.rows; ++y) {
        const float* row = m.ptr<float>(y);
        for (int x=0; x<m.cols; ++x) dev.push_back(std::fabs(row[x] - med));
    }
    if (dev.empty()) return 0.f;
    auto mid = dev.begin() + dev.size()/2;
    std::nth_element(dev.begin(), mid, dev.end());
    return *mid; // MAD（未乘 1.4826，阈值里再乘）
}

// ===== 用两点参考 + K/B 生成静态坏点掩膜 =====
void ImageProcessor::buildBPMFromTwoPointLocked() {
    const int bitMax = bitMaxEff;  // 替代原来的 constexpr 65535
    bpm.release();

    // 计算两幅参考的全局平均（与 NUC 目标一致）
    const float T0_DN = static_cast<float>(cv::mean(lowMean)[0]);
    const float T1_DN = static_cast<float>(cv::mean(highMean)[0]);


    // 条件：已完成两点；尺寸一致
    if (!calibrated || lowMean.empty() || highMean.empty() ||
        Kmat.empty() || Bmat.empty() || lowMean.size()!=highMean.size() ||
        Kmat.size()!=lowMean.size() || Bmat.size()!=lowMean.size()) {
        emit captureStatus("BPM 生成失败：两点数据不完整或尺寸不匹配");
        return;
    }

    const int H = lowMean.rows, W = lowMean.cols;
    cv::Mat1b mask(H, W, uchar(0));
    // ====== 四类规则各自的掩膜（0/1） ======
    cv::Mat1b maskGain(H, W, uchar(0));
    cv::Mat1b maskDsnu(H, W, uchar(0));
    cv::Mat1b maskResid(H, W, uchar(0));
    cv::Mat1b maskDead(H, W, uchar(0));
    // ---- 指标1：响应（增益）异常（基于 high-low 的全局中位数区间）----
    cv::Mat gain32F; cv::subtract(highMean, lowMean, gain32F); // 32F
    float gMed = medianOfMat32F(gain32F);
    float gMin = gainMinRatio * gMed;
    float gMax = gainMaxRatio * gMed;

    for (int y=0; y<H; ++y) {
        const float* g = gain32F.ptr<float>(y);
        uchar* m = maskGain.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (g[x] < gMin || g[x] > gMax) m[x] = 1;
        }
    }

    // ---- 指标2：低场偏置异常（DSNU-Dark Signal Non-Uniformity，低场的全局中位数 + k*MAD / 绝对阈值）----
    // ----​​原理​​：在均匀低温场下，正常像素的响应值应该紧密聚集。偏离中心太远的像素是坏点。
    float Lmed = medianOfMat32F(lowMean);
    float Lmad = madOfMat32F(lowMean, Lmed);         // MAD
    float robust = 1.4826f * Lmad;                   // 约等价 sigma
    float Tdsnu = std::max<float>(dsnuAbs, dsnuKmad * robust);

    for (int y=0; y<H; ++y) {
        const float* L = lowMean.ptr<float>(y);
        uchar* m = maskDsnu.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (std::fabs(L[x] - Lmed) > Tdsnu) m[x] = 1;
        }
    }

    // ---- 指标3：线性残差（K*low+B ≈ 0，K*high+B ≈ bitMax）----
    //​​原理​​：两点校正的理想结果是，低温场校正后输出应为T0_DN，高温场校正后输出应为T1_DN。​​残差过大​​说明该像素的响应无法被线性校正公式很好地拟合，是非线性坏点。
    for (int y=0; y<H; ++y) {
        const float* L = lowMean.ptr<float>(y);
        const float* Hh= highMean.ptr<float>(y);
        const float* K = Kmat.ptr<float>(y);
        const float* B = Bmat.ptr<float>(y);
        uchar* m = maskResid.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            float r1 = std::fabs(K[x]*L[x] + B[x] - T0_DN);       // 低场残差 对齐 T0
            float r2 = std::fabs(K[x]*Hh[x] + B[x] - T1_DN);      // 高场残差 对齐 T1
            if (std::max(r1, r2) > linResidualDN) m[x] = 1;
        }
    }

    // ---- 指标4：死黑/死白（低场近 0 / 高场近饱和）----
    for (int y=0; y<H; ++y) {
        const float* L = lowMean.ptr<float>(y);
        const float* Hh= highMean.ptr<float>(y);
        uchar* m = maskDead.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (L[x] < blackThresh || Hh[x] > (bitMax - whiteThresh)) m[x] = 1;
        }
    }
    // ====== 统计每条规则原始命中数（未去重） ======
    const int cntGain  = cv::countNonZero(maskGain);
    const int cntDsnu  = cv::countNonZero(maskDsnu);
    const int cntResid = cv::countNonZero(maskResid);
    const int cntDead  = cv::countNonZero(maskDead);

    // ====== 合并掩膜（规则 OR） ======
    mask = maskGain | maskDsnu | maskResid | maskDead;
    // ---- 形态学清理：去孤点/补小孔（3×3 开-闭）----
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
//    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,  kernel);  // 开运算：先腐蚀再膨胀，会直接去除孤立的坏点，不能使用！
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);  // 闭运算：先膨胀再腐蚀，它会填小孔、让坏点团边缘更连贯

    bpm = mask;
    const int cntFinal = cv::countNonZero(bpm);
    // ====== 打印命中统计（规则命中为原始、Final 为形态学后） ======
    emit captureStatus(
        QString("BPM规则命中：增益=%1，DSNU=%2，残差=%3，死黑/白=%4；合并后(形态学)=%5")
            .arg(cntGain).arg(cntDsnu).arg(cntResid).arg(cntDead).arg(cntFinal)
    );
}

// ===== 在 16U 图像上做静态坏点替代：3×3 邻域中值（无好邻居则跳过）=====
void ImageProcessor::applyStaticBPM(cv::Mat1w& img16) {
    if (bpm.empty() || img16.size()!=bpm.size()) return;
    const int H = img16.rows, W = img16.cols;

    // 8邻域偏移
    static const int dx[8]={-1,0,1,-1,1,-1,0,1};
    static const int dy[8]={-1,-1,-1,0,0,1,1,1};

    for (int y=0; y<H; ++y) {
        uint16_t* row = img16.ptr<uint16_t>(y);
        const uchar* m = bpm.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (!m[x]) continue; // 好像素跳过
            uint16_t neigh[8]; int n=0;

            // 收集好邻居
            for (int k=0; k<8; ++k) {
                int yy=y+dy[k], xx=x+dx[k];
                if (yy<0||yy>=H||xx<0||xx>=W) continue;
                if (bpm(yy,xx)) continue; // 邪像素不纳入
                neigh[n++] = img16(yy,xx);
            }
            if (n==0) continue;  // 周围全是坏点：先保持原值（可按需迭代或外扩）

            // 取中位数（n<=8，nth_element 代价很小）
            auto mid = neigh + n/2;
            std::nth_element(neigh, mid, neigh + n);
            uint16_t med = *mid;
            if ((n & 1) == 0) { // 偶数个：取中间两数均值
                auto mid2 = neigh + n/2 - 1;
                std::nth_element(neigh, mid2, neigh + n);
                med = uint16_t( (uint32_t(*mid) + *mid2) / 2 );
            }
            row[x] = med;
        }
    }
}
// 迭代扩散版坏点替代：可覆盖较大斑块
//void ImageProcessor::applyStaticBPM(cv::Mat1w& img16) {
//    if (bpm.empty() || img16.size()!=bpm.size()) return;

//    const int H = img16.rows, W = img16.cols;
//    static const int dx[8]={-1,0,1,-1,1,-1,0,1};
//    static const int dy[8]={-1,-1,-1,0,0,1,1,1};

//    cv::Mat1b workMask = bpm.clone(); // 1=坏，0=好
//    cv::Mat1w tmp = img16.clone();

//    const int maxIters = 8;           // 迭代层数，斑块半径小于该值基本能填满
//    const int minGoodN = 1;           // 至少需要多少个好邻居才替代
//    for (int it=0; it<maxIters; ++it) {
//        bool any = false;
//        for (int y=0; y<H; ++y) {
//            const uchar* mrow = workMask.ptr<uchar>(y);
//            for (int x=0; x<W; ++x) {
//                if (!mrow[x]) continue; // 好像素跳过
//                uint16_t neigh[8]; int n=0;
//                for (int k=0; k<8; ++k) {
//                    int yy=y+dy[k], xx=x+dx[k];
//                    if ((unsigned)yy>= (unsigned)H || (unsigned)xx>= (unsigned)W) continue;
//                    if (workMask(yy,xx)) continue; // 邪像素邻居不纳入
//                    neigh[n++] = img16(yy,xx);     // 注意：使用上一轮/边界已经修好的值
//                }
//                if (n >= minGoodN) {
//                    auto mid = neigh + n/2;
//                    std::nth_element(neigh, mid, neigh+n);
//                    uint16_t med = *mid;
//                    if ((n & 1)==0) {
//                        auto mid2 = neigh + n/2 - 1;
//                        std::nth_element(neigh, mid2, neigh+n);
//                        med = uint16_t((uint32_t(*mid)+*mid2)/2);
//                    }
//                    tmp(y,x) = med;   // 暂存新值
//                }
//            }
//        }

//        // 把本轮成功替代的像素并入“好像素”，并落到 img16
//        for (int y=0; y<H; ++y) {
//            for (int x=0; x<W; ++x) {
//                if (workMask(y,x) && tmp(y,x) != img16(y,x)) {
//                    img16(y,x) = tmp(y,x);
//                    workMask(y,x) = 0;    // 变成好像素，下一轮可作为邻居
//                    any = true;
//                }
//            }
//        }
//        if (!any) break; // 没有新的像素被修复，结束
//    }
//}
// 简单方向性坏点替代：优先左边，左边也坏则继续向左；左侧找不到则改用上方
// mask1: CV_8UC1，1=坏，0=好
// img16: CV_16UC1，原地替代
void ImageProcessor::applyStaticBPM_LeftThenUp(const cv::Mat1b& mask1, cv::Mat1w& img16)
{
    CV_Assert(mask1.type()==CV_8UC1 && img16.type()==CV_16UC1);
    CV_Assert(mask1.size()==img16.size());

    const int H = img16.rows, W = img16.cols;

    // 行优先扫描：保证同一行内“往左找”可以使用已处理过的像素值
    for (int y = 0; y < H; ++y) {
        const uchar* mrow = mask1.ptr<uchar>(y);
        uint16_t*    irow = img16.ptr<uint16_t>(y);

        for (int x = 0; x < W; ++x) {
            if (!mrow[x]) continue;  // 好像素跳过

            // 1) 先向左寻找第一个非坏点
            int xl = x - 1;
            while (xl >= 0 && mask1(y, xl)) {
                --xl;
            }
            if (xl >= 0) {
                // 找到了左边的好像素
                irow[x] = img16(y, xl);
                continue;
            }

            // 2) 左侧没有可用像素（或到达边界），向上寻找第一个非坏点
            int yu = y - 1;
            while (yu >= 0 && mask1(yu, x)) {
                --yu;
            }
            if (yu >= 0) {
                // 找到了上方的好像素
                irow[x] = img16(yu, x);
                continue;
            }

            // 3) 极端：左和上都找不到（例如左上大片坏点），保持原值或置 0（按需）
            // irow[x] = 0; // 如果你希望直接置零，可改成这一行

        }
    }
}
// 用一条列向量 + 一个行标量，单遍实现“左优先，上备援”
void ImageProcessor::applyStaticBPM_LeftThenUp_Optimized(const cv::Mat1b& mask1, cv::Mat1w& img16)
{
    CV_Assert(mask1.type()==CV_8UC1 && img16.type()==CV_16UC1);
    CV_Assert(mask1.size()==img16.size());
    const int H = img16.rows, W = img16.cols;

    // 列向“最近上方好值”的值/有效标志
    std::vector<uint16_t> colUpVal(W, 0);
    std::vector<uint8_t>  colUpValid(W, 0);  // 0/1

    uint16_t lastLeftVal = 0;
    bool     lastLeftValid = false;
    for (int y = 0; y < H; ++y) {
        const uchar* mrow = mask1.ptr<uchar>(y);
        uint16_t*    irow = img16.ptr<uint16_t>(y);

        for (int x = 0; x < W; ++x) {
            if (mrow[x] == 0) {
                // 好像素：更新左右/上下“最近原生好值”
                lastLeftVal   = irow[x];    //当前行，当前位置左侧最近的原生好像素值
                lastLeftValid = true;
                colUpVal[x]   = irow[x];    //当前列，当前位置上方最近的原生好像素值
                colUpValid[x] = 1;
            } else {
                // 坏像素：优先左（若左无效则用上；若两者都无效则保持/置默认）
                if (lastLeftValid) {
                    irow[x] = lastLeftVal;
                } else if (colUpValid[x]) {
                    irow[x] = colUpVal[x];
                } else {
                    // 左上都还没有原生好值（例如左上角大片坏点）
                    // 这里按需求：保持原值或置0或延后到另一轮处理
                    // irow[x] = 0;
                }
                // 注意：不要用替代值去更新 lastLeft/colUp（只追踪“原生好值”）。
            }
        }
    }
}

// 规则：左优先（同一行最近原生好像素），若无则用同列最近上方原生好像素。
// 仅依赖 bpm 掩膜，O(HW)；只为坏点记录 (dst <- src)。
void ImageProcessor::buildBPMRemap_LeftThenUp_Locked()
{
    bpmRemap.clear();
    remapW = remapH = 0;
    if (bpm.empty()) return;

    const int H = bpm.rows, W = bpm.cols;
    std::vector<int> upGoodY(W, -1); // 每列到当前行为止最近“上方好像素”的 y；-1 表示无
    bpmRemap.reserve(std::max(1, cv::countNonZero(bpm)));

    for (int y = 0; y < H; ++y) {
        const uchar* mrow = bpm.ptr<uchar>(y);
        int lastLeftX = -1; // 本行到当前位置最近左侧“原生好像素”的 x

        for (int x = 0; x < W; ++x) {
            if (mrow[x] == 0) {
                // 好像素：更新左/上索引状态（仅跟踪“原生好像素”）
                lastLeftX  = x;
                upGoodY[x] = y;
            } else {
                // 坏点：决定来源坐标
                int sx = -1, sy = -1;
                if (lastLeftX >= 0) {          // 有左侧好像素
                    sx = lastLeftX; sy = y;
                } else if (upGoodY[x] >= 0) {  // 否则用上方好像素
                    sx = x; sy = upGoodY[x];
                } else {
                    // 左与上都找不到：先留下空白（可选在第二遍做右/下回填）
                    // 这里我们选择暂不加入映射；这种像素将保持原值或由后续策略处理
                }
                if (sx >= 0) {
                    const int dst = y*W + x;
                    const int src = sy*W + sx;
                    bpmRemap.push_back({dst, src});
                }
            }
        }
    }
    remapW = W; remapH = H;

    // ★ 可选增强：如果需要“极端左上大斑块”也能填，可在此再做一遍
    // 从右->左、下->上 扫描，为仍未映射的坏点回填 (右/下优先)；此处略。
}

void ImageProcessor::applyBPM_WithRemap(cv::Mat1w &img16)
{
    if (bpmRemap.empty()) return;
    const int H = img16.rows, W = img16.cols;
    if (W != remapW || H != remapH) return; // 分辨率变化：需重建

    uint16_t* base = img16.ptr<uint16_t>(0);
    for (const auto& e : bpmRemap) {
        base[e.dst] = base[e.src];
    }
}


void ImageProcessor::setGainMin(double v)        { QMutexLocker lk(&calibMutex); gainMinRatio = float(v); }
void ImageProcessor::setGainMax(double v)        { QMutexLocker lk(&calibMutex); gainMaxRatio = float(v); }
void ImageProcessor::setDsnuAbs(int v)           { QMutexLocker lk(&calibMutex); dsnuAbs      = v;        }
void ImageProcessor::setDsnuKmad(double v)       { QMutexLocker lk(&calibMutex); dsnuKmad     = float(v); }
void ImageProcessor::setLinResidualDN(int v)     { QMutexLocker lk(&calibMutex); linResidualDN= v;        }
void ImageProcessor::setBlackThresh(int v)       { QMutexLocker lk(&calibMutex); blackThresh  = v;        }
void ImageProcessor::setWhiteThresh(int v)       { QMutexLocker lk(&calibMutex); whiteThresh  = v;        }

void ImageProcessor::setEqualizeHistThresholds(int upper, int lower)
{
    upper = std::clamp(upper, 1, 1000000);
    lower = std::clamp(lower, 0, 1000000);
    if (upper <= lower) {
        upper = lower + 1;
    }

    EqualizeHistUpperThreshold.store(upper, std::memory_order_release);
    EqualizeHistLowerThreshold.store(lower, std::memory_order_release);
}

void ImageProcessor::equalizeHist16(cv::Mat1w &img16, int bitMaxEff)
{
    int bitMax = bitMaxEff;
    if (bitMax < 1) bitMax = 65535;
    if (bitMax > 65535) bitMax = 65535;

    const int bins = bitMax + 1;
    const bool useDownsample = EqualizeHistDownsampleEnabled.load(std::memory_order_acquire);
    const int sampleStep = useDownsample ? 4 : 1;

    // thread_local 复用直方图 / LUT，避免每帧 malloc
    static thread_local std::vector<uint32_t> hist;
    static thread_local std::vector<uint16_t> lut;

    hist.assign(bins, 0);

    // 1) 统计直方图；FPGA 对比模式下每 4 行、每 4 列抽 1 个像素。
    uint64_t samplePix = 0;
    for (int y = 0; y < img16.rows; y += sampleStep) {
        const uint16_t* row = img16.ptr<uint16_t>(y);
        for (int x = 0; x < img16.cols; x += sampleStep) {
            uint16_t v = row[x];
            if (v > bitMax) v = (uint16_t)bitMax;
            hist[v]++;
            ++samplePix;
        }
    }
    if (samplePix == 0) return;

    // 2) 双平台阈值：上平台压峰值，下平台抬升非零低计数。
    int upper = EqualizeHistUpperThreshold.load(std::memory_order_acquire);
    int lower = EqualizeHistLowerThreshold.load(std::memory_order_acquire);
    upper = std::clamp(upper, 1, 1000000);
    lower = std::clamp(lower, 0, upper - 1);

    uint64_t clippedTotal = 0;
    for (int i = 0; i < bins; ++i) {
        if (hist[i] > (uint32_t)upper) {
            hist[i] = (uint32_t)upper;
        } else if (hist[i] > 0 && hist[i] < (uint32_t)lower) {
            hist[i] = (uint32_t)lower;
        }
        clippedTotal += hist[i];
    }
    if (clippedTotal == 0) return;

    // 3) 构造 LUT：平台阈值后的累计分布映射到完整有效位宽。
    uint64_t cdf = 0;
    lut.resize(bins);
    for (int i = 0; i < bins; ++i) {
        cdf += hist[i];
        int mapped = (int)std::llround(
            (double)cdf * bitMax / (double)clippedTotal
            );
        if (mapped < 0) mapped = 0;
        if (mapped > bitMax) mapped = bitMax;
        lut[i] = (uint16_t)mapped;
    }

    // 4) 应用 LUT（原地）
    for (int y = 0; y < img16.rows; ++y) {
        uint16_t* row = img16.ptr<uint16_t>(y);
        for (int x = 0; x < img16.cols; ++x) {
            uint16_t v = row[x];
            if (v > bitMax) v = (uint16_t)bitMax;
            row[x] = lut[v];
        }
    }
}

void ImageProcessor::setBpmParams(double gmin, double gmax, int dsnuAbsV, double dsnuKmadK,
                                  int linResid, int blackT, int whiteT, bool rebuild)
{
    {
        QMutexLocker lk(&calibMutex);
        gainMinRatio = float(gmin);
        gainMaxRatio = float(gmax);
        dsnuAbs      = dsnuAbsV;
        dsnuKmad     = float(dsnuKmadK);
        linResidualDN= linResid;
        blackThresh  = blackT;
        whiteThresh  = whiteT;

        if (rebuild && calibrated && !lowMean.empty() && !highMean.empty()) {
            buildBPMFromTwoPointLocked();       // 使用最新参数重建 BPM
            buildBPMRemap_LeftThenUp_Locked();  // 使用最新参数重建盲元替代表
        }
    }
    if (rebuild) emit captureStatus("BPM 参数已更新并重建完成");
    else         emit captureStatus("BPM 参数已更新");
}

// 保存 BPM：将 0/1 掩膜保存为 8-bit 图（0=好，255=坏）
bool ImageProcessor::saveBPM(const QString& filePath)
{
    cv::Mat bpmLocal;
    {
        QMutexLocker lk(&calibMutex);
        if (bpm.empty()) {
            emit captureStatus("BPM 为空，未保存");
            return false;
        }
        bpmLocal = bpm.clone(); // 锁内复制，锁外写盘
    }

    // 0/1 -> 0/255
    cv::Mat out8;
    out8.create(bpmLocal.size(), CV_8UC1);
    for (int y=0; y<bpmLocal.rows; ++y) {
        const uchar* s = bpmLocal.ptr<uchar>(y);
        uchar* d = out8.ptr<uchar>(y);
        for (int x=0; x<bpmLocal.cols; ++x) d[x] = s[x] ? uchar(255) : uchar(0);
    }

    // 确保目录存在
    QFileInfo finfo(filePath);
    if (!finfo.dir().exists()) {
        QDir().mkpath(finfo.dir().absolutePath());
    }

    bool ok = false;
    try {
        ok = cv::imwrite(filePath.toStdString(), out8);
    } catch (...) { ok = false; }

    emit captureStatus(ok
        ? QString("BPM 已保存：%1 (%2x%3)").arg(filePath).arg(out8.cols).arg(out8.rows)
        : QString("BPM 保存失败：%1").arg(filePath));
    return ok;
}

// 加载 BPM：读取 8-bit 图（非0即坏），尺寸必须匹配当前源
bool ImageProcessor::loadBPM(const QString& filePath)
{
    // 1) 读取图像（灰度）
    cv::Mat in8;
    try {
        in8 = cv::imread(filePath.toStdString(), cv::IMREAD_GRAYSCALE);
    } catch (...) {
        in8.release();
    }
    if (in8.empty()) {
        emit captureStatus(QString("加载 BPM 失败：无法读取 %1").arg(filePath));
        return false;
    }

    // 2) 尺寸校验（用当前源规格）
    const int W = m_srcW.load(std::memory_order_relaxed);
    const int H = m_srcH.load(std::memory_order_relaxed);
    if (in8.cols != W || in8.rows != H) {
        emit captureStatus(QString("加载 BPM 失败：尺寸不匹配，文件=%1x%2 期望=%3x%4")
                           .arg(in8.cols).arg(in8.rows).arg(W).arg(H));
        return false;
    }

    // 3) 灰度 -> 0/1 掩膜（非0视为坏）
    cv::Mat bpmNew(H, W, CV_8UC1);
    for (int y=0; y<H; ++y) {
        const uchar* s = in8.ptr<uchar>(y);
        uchar* d = bpmNew.ptr<uchar>(y);
        for (int x=0; x<W; ++x) d[x] = (s[x] ? uchar(1) : uchar(0));
    }

    // 4) 写入（加锁）
    {
        QMutexLocker lk(&calibMutex);
        bpm = std::move(bpmNew);
    }

    int badCnt = cv::countNonZero(bpm);
    emit captureStatus(QString("BPM 已加载：%1（坏点=%2）").arg(filePath).arg(badCnt));
    return true;
}
// 写 RAW：按行写有效像素宽度，不写 QImage 的对齐填充。
// 8bit: 逐行写 width 字节
// 16bit: 逐像素转小端写出（跨平台安全）
static bool writeRawFromQImage(const QImage& img, const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;

    const int w = img.width();
    const int h = img.height();

    if (img.format() == QImage::Format_Grayscale16) {
        for (int y = 0; y < h; ++y) {
            const quint16* row = reinterpret_cast<const quint16*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const quint16 v_host = qFromLittleEndian<quint16>(row[x]); // 读入为主机端序
                const quint16 v_le   = qToLittleEndian(v_host);            // 转回小端写盘
                f.write(reinterpret_cast<const char*>(&v_le), sizeof(v_le));
            }
        }
    } else {
        // 非 16 位按 8 位写。若不是灰度 8，则转成灰度 8。
        QImage gray = (img.format() == QImage::Format_Grayscale8)
                      ? img
                      : img.convertToFormat(QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            const uchar* row = gray.constScanLine(y);
            f.write(reinterpret_cast<const char*>(row), w);
        }
    }
    f.close();
    return true;
}

// 先用 OpenCV 按后缀编码到内存，再用 Qt 写文件，避免 Windows 下 Unicode 路径导致 imwrite 失败。
static bool writeMatByImencode(const cv::Mat& mat, const QString& suffixLower, const QString& filePath) {
    if (mat.empty()) return false;

    QString ext = suffixLower;
    if (ext == "jpg") ext = "jpeg";
    if (ext == "tif") ext = "tiff";

    std::vector<uchar> encoded;
    bool ok = false;
    try {
        ok = cv::imencode(("." + ext).toStdString(), mat, encoded);
    } catch (...) {
        ok = false;
    }
    if (!ok || encoded.empty()) return false;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const qint64 n = f.write(reinterpret_cast<const char*>(encoded.data()), qint64(encoded.size()));
    f.close();
    return n == qint64(encoded.size());
}

bool ImageProcessor::saveFrame(const QString& filePath) {
    QImage copy;
    {   // 线程安全地复制当前显示缓冲
        QMutexLocker lk(&widget_image::s_imgMutex);
        copy = widget_image::image.copy();
    }
    if (copy.isNull()) {
        emit captureStatus("保存失败：当前无图像");
        return false;
    }
    QFileInfo finfo(filePath);
    if (!finfo.dir().exists()) QDir().mkpath(finfo.dir().absolutePath());

    const QString suffixLower = finfo.suffix().toLower();
    const bool toRaw = (suffixLower == "raw");
    bool ok = false;
    if (toRaw) {
        ok = writeRawFromQImage(copy, filePath);
        emit captureStatus(ok
            ? QString("已保存 RAW：%1").arg(filePath)
            : QString("保存 RAW 失败：%1").arg(filePath));
        return ok;
    }
    if (copy.format() == QImage::Format_Grayscale16) {
        // 16U 原样写盘（优先 PNG/TIFF），避免直接 imwrite(filePath.toStdString()) 的路径编码问题。
        cv::Mat m(copy.height(), copy.width(), CV_16UC1,
                  const_cast<uchar*>(copy.constBits()), copy.bytesPerLine());
        cv::Mat contiguous; m.copyTo(contiguous); // 确保连续

        if (suffixLower == "png" || suffixLower == "tif" || suffixLower == "tiff") {
            ok = writeMatByImencode(contiguous, suffixLower, filePath);
            if (!ok && suffixLower == "png") {
                // 回退：若当前环境缺少 OpenCV PNG 编码器，则退化为 Qt 保存。
                ok = copy.save(filePath, "PNG");
                if (!ok) {
                    QImage gray8 = copy.convertToFormat(QImage::Format_Grayscale8);
                    ok = gray8.save(filePath, "PNG");
                }
            }
        } else {
            // 其他格式维持原逻辑，失败时回退 Qt。
            try { ok = cv::imwrite(filePath.toStdString(), contiguous); } catch(...) { ok = false; }
            if (!ok) ok = copy.save(filePath);
        }
    } else {
        ok = copy.save(filePath);
    }

    emit captureStatus(ok ? QString("已保存：%1").arg(filePath)
                          : QString("保存失败：%1").arg(filePath));
    return ok;
}
