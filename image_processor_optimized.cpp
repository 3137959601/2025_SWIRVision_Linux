#include "image_processor.h"

// =================== 工具：统计量 ===================
float ImageProcessor::medianOfMat32F(const cv::Mat &m) {
    std::vector<float> v; v.reserve(size_t(m.total()));
    for (int y=0; y<m.rows; ++y) {
        const float* row = m.ptr<float>(y);
        v.insert(v.end(), row, row + m.cols);
    }
    if (v.empty()) return 0.f;
    auto mid = v.begin() + v.size()/2;
    std::nth_element(v.begin(), mid, v.end());
    float med = *mid;
    if ((v.size() & 1) == 0) { // 偶数：取中间两数均值（可选）
        auto mid2 = v.begin() + v.size()/2 - 1;
        std::nth_element(v.begin(), mid2, v.end());
        med = 0.5f * (med + *mid2);
    }
    return med;
}

float ImageProcessor::madOfMat32F(const cv::Mat &m, float med) {
    std::vector<float> dev; dev.reserve(size_t(m.total()));
    for (int y=0; y<m.rows; ++y) {
        const float* row = m.ptr<float>(y);
        for (int x=0; x<m.cols; ++x) dev.push_back(std::fabs(row[x] - med));
    }
    if (dev.empty()) return 0.f;
    auto mid = dev.begin() + dev.size()/2;
    std::nth_element(dev.begin(), mid, dev.end());
    return *mid; // MAD（未乘 1.4826，这里阈值侧乘）
}

// =================== 参考采集控制 ===================
ImageProcessor::ImageProcessor(QObject *parent): QThread(parent) {}

void ImageProcessor::startCaptureLow()  {
    QMutexLocker lk(&calibMutex);
    lowBuf.clear();  capturingLow  = true;
    capturingHigh = false; calibrated = false; kbQuantized = false;
    emit captureStatus("开始采集低温参考");
}
void ImageProcessor::startCaptureHigh() {
    QMutexLocker lk(&calibMutex);
    highBuf.clear(); capturingHigh = true;
    capturingLow  = false; calibrated = false; kbQuantized = false;
    emit captureStatus("开始采集高温参考");
}
void ImageProcessor::clearCalibration() {
    QMutexLocker lk(&calibMutex);
    lowBuf.clear(); highBuf.clear();
    lowMean.release(); highMean.release();
    Kmat.release(); Bmat.release();
    Kq.release();   Bq.release();
    calibrated=false; kbQuantized=false;
    emit calibrationReady(false);
    emit captureStatus("已清空两点校准");
}

void ImageProcessor::rebuildBPM() {
    QMutexLocker lk(&calibMutex);
    buildBPMFromTwoPointLocked();
}
void ImageProcessor::clearBPM() {
    QMutexLocker lk(&calibMutex);
    bpm.release();
    emit captureStatus("已清空坏点掩膜");
}

// =================== 两点校正：计算 K/B 并量化 ===================
void ImageProcessor::computeCalibrationLocked() {
    // 前置：参考满足
    if (lowBuf.size() < size_t(sampleFrameNum) ||
        highBuf.size()< size_t(sampleFrameNum)) return;

    // 求均值（16U -> 32F）
    auto meanOfU16 = [](const std::vector<cv::Mat>& vec)->cv::Mat {
        if (vec.empty()) return cv::Mat();
        const int H = vec[0].rows, W = vec[0].cols;
        cv::Mat acc(H, W, CV_32FC1, cv::Scalar(0));
        for (const auto &m : vec) {
            CV_Assert(m.type()==CV_16UC1 && m.size()==cv::Size(W,H));
            cv::Mat f; m.convertTo(f, CV_32FC1);
            acc += f;
        }
        acc /= float(vec.size());
        return acc;
    };

    lowMean  = meanOfU16(lowBuf);
    highMean = meanOfU16(highBuf);
    if (lowMean.empty() || highMean.empty() || lowMean.size()!=highMean.size()) {
        calibrated=false; kbQuantized=false;
        emit calibrationReady(false);
        emit captureStatus("参考均值无效");
        return;
    }

    // 计算 K/B
    constexpr float bitMax = 65535.0f;
    Kmat.create(lowMean.size(), CV_32FC1);
    Bmat.create(lowMean.size(), CV_32FC1);
    const float eps = 1e-6f;
    for (int y=0; y<lowMean.rows; ++y) {
        const float *L=lowMean.ptr<float>(y), *H=highMean.ptr<float>(y);
        float *K=Kmat.ptr<float>(y), *B=Bmat.ptr<float>(y);
        for (int x=0; x<lowMean.cols; ++x) {
            float d = H[x] - L[x];
            if (std::fabs(d) < eps) { K[x]=1.f; B[x]=0.f; }
            else { K[x] = bitMax / d; B[x] = -K[x] * L[x]; }
        }
    }
    calibrated = true;
    emit calibrationReady(true);
    emit captureStatus("两点校准完成");

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

    // （可选）自动生成一版 BPM，省得手动点
    buildBPMFromTwoPointLocked();
}

// 整型 NUC：out = clamp(((Kq * in) >> Qfrac) + Bq, 0..65535)
void ImageProcessor::applyTwoPointNUC_Int(const cv::Mat &src16, cv::Mat &out16) {
    CV_Assert(src16.type()==CV_16UC1);
    out16.create(src16.size(), CV_16UC1);
    const int H=src16.rows, W=src16.cols;
    const int Q = Qfrac;

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
            if (v < 0) v = 0; else if (v > 65535) v = 65535;
            d[x] = (uint16_t)v;
        }
    }
}

// =================== BPM：构建 / 填洞 / 替代 ===================

// 填洞：将 mask（1=坏）的内部 0 小岛填为 1，避免“环形坏点”
void ImageProcessor::fillHolesInPlace(cv::Mat &mask1) {
    CV_Assert(mask1.type()==CV_8UC1);
    if (mask1.empty()) return;
    cv::Mat inv; cv::bitwise_not(mask1, inv); // 1->254/255, 0->255/0（作为背景）
    cv::Mat pad(inv.rows+2, inv.cols+2, CV_8UC1, cv::Scalar(0));
    inv.copyTo(pad(cv::Rect(1,1,inv.cols,inv.rows)));
    // 从边界泛洪，标出与外部连通的“背景”
    cv::floodFill(pad, cv::Point(0,0), cv::Scalar(128));
    // pad==0 的区域是“洞”（被完全包围），在原 mask 上置为 1
    for (int y=0; y<inv.rows; ++y) {
        const uchar* prow = pad.ptr<uchar>(y+1);
        uchar* mrow = mask1.ptr<uchar>(y);
        for (int x=0; x<inv.cols; ++x) {
            if (prow[x+1]==0) mrow[x] = 1;
        }
    }
}

// 基于两点参考，构建静态坏点掩膜（1=坏）
void ImageProcessor::buildBPMFromTwoPointLocked() {
    bpm.release();

    if (!calibrated || lowMean.empty() || highMean.empty() ||
        Kmat.empty() || Bmat.empty() ||
        lowMean.size()!=highMean.size() ||
        Kmat.size()!=lowMean.size()) {
        emit captureStatus("BPM 生成失败：两点数据不完整/尺寸不匹配");
        return;
    }

    const int H = lowMean.rows, W = lowMean.cols;
    cv::Mat1b mask(H, W, uchar(0));

    // 指标1：响应异常（基于 high-low 的全局中位数）
    cv::Mat gain32F; cv::subtract(highMean, lowMean, gain32F); // 32F
    float gMed = medianOfMat32F(gain32F);
    float gMin = gainMinRatio * gMed;
    float gMax = gainMaxRatio * gMed;
    for (int y=0; y<H; ++y) {
        const float* g = gain32F.ptr<float>(y);
        uchar* m = mask.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (g[x] < gMin || g[x] > gMax) m[x] = 1;
        }
    }

    // 指标2：低场偏置异常（DSNU）：abs(L - medianL) > max(dsnuAbs, dsnuKmad * 1.4826 * MAD)
    float Lmed = medianOfMat32F(lowMean);
    float Lmad = madOfMat32F(lowMean, Lmed);
    float robust = 1.4826f * Lmad;
    float Tdsnu = std::max<float>(dsnuAbs, dsnuKmad * robust);
    for (int y=0; y<H; ++y) {
        const float* L = lowMean.ptr<float>(y);
        uchar* m = mask.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (std::fabs(L[x]-Lmed) > Tdsnu) m[x] = 1;
        }
    }

    // 指标3：线性残差（K*low+B ≈ 0；K*high+B ≈ 65535）
    for (int y=0; y<H; ++y) {
        const float *L=lowMean.ptr<float>(y), *Hh=highMean.ptr<float>(y);
        const float *K=Kmat.ptr<float>(y), *B=Bmat.ptr<float>(y);
        uchar* m = mask.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            float r1 = std::fabs(K[x]*L[x] + B[x]);
            float r2 = std::fabs(K[x]*Hh[x]+ B[x] - 65535.f);
            if (std::max(r1, r2) > linResidualDN) m[x] = 1;
        }
    }

    // 指标4：死黑/死白
    for (int y=0; y<H; ++y) {
        const float *L=lowMean.ptr<float>(y), *Hh=highMean.ptr<float>(y);
        uchar* m = mask.ptr<uchar>(y);
        for (int x=0; x<W; ++x) {
            if (L[x] < blackThresh || Hh[x] > (65535.f - whiteThresh))
                m[x] = 1;
        }
    }

    // 形态学：仅做 CLOSE（填小孔、让边界更光顺）；不要做 OPEN（会删掉孤立坏点）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, {3,3});
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    // ★ 填洞：将环形坏点变为实心块
    fillHolesInPlace(mask);

    bpm = mask;
    emit captureStatus(QString("BPM 生成完成：坏点数=%1").arg(cv::countNonZero(bpm)));
}

// 前沿队列替代：只处理坏点前沿 -> 线性复杂度，覆盖大块（已填洞）
void ImageProcessor::applyStaticBPM(const cv::Mat &mask1, cv::Mat &img16) {
    CV_Assert(mask1.type()==CV_8UC1 && img16.type()==CV_16UC1);
    if (mask1.empty()) return;

    const int H=img16.rows, W=img16.cols;
    // 先取坏点 ROI，减少访问范围
    cv::Rect roi = cv::boundingRect(mask1);
    if (roi.area()<=0) return;

    cv::Mat1b bad = mask1(roi).clone();      // 1=坏
    cv::Mat1w im  = img16(roi);              // 工作子图

    static const int dx[8]={-1,0,1,-1,1,-1,0,1};
    static const int dy[8]={-1,-1,-1,0,0,1,1,1};

    std::deque<cv::Point> q;

    auto inb = [&](int y,int x){ return (unsigned)y < (unsigned)bad.rows && (unsigned)x < (unsigned)bad.cols; };

    auto enqueue_if_frontier = [&](int y,int x){
        if (!bad(y,x)) return;
        for (int k=0;k<8;++k){
            int yy=y+dy[k], xx=x+dx[k];
            if (!inb(yy,xx)) continue;
            if (!bad(yy,xx)) { q.emplace_back(xx,y); break; } // 注意存 (x,y)
        }
    };

    // 初始：所有前沿坏点入队
    for (int y=0;y<bad.rows;++y) for (int x=0;x<bad.cols;++x) enqueue_if_frontier(y,x);

    // BFS 式扩散：每个坏点只处理一次
    while(!q.empty()){
        cv::Point p = q.front(); q.pop_front();
        int x=p.x, y=p.y;
        if (!inb(y,x) || !bad(y,x)) continue;

        uint16_t neigh[8]; int n=0;
        for (int k=0;k<8;++k){
            int yy=y+dy[k], xx=x+dx[k];
            if (!inb(yy,xx)) continue;
            if (bad(yy,xx)) continue;
            neigh[n++] = im(yy,xx);
        }
        if (n==0) continue; // 不是前沿

        // 取中位数（n≤8）
        auto mid = neigh + n/2;
        std::nth_element(neigh, mid, neigh+n);
        uint16_t med = *mid;
        if ((n & 1)==0) {
            auto mid2 = neigh + n/2 - 1;
            std::nth_element(neigh, mid2, neigh+n);
            med = uint16_t((uint32_t(*mid)+*mid2)/2);
        }
        im(y,x) = med;      // 修复
        bad(y,x) = 0;       // 变“好”

        // 其坏邻居成为下一层前沿
        for (int k=0;k<8;++k){
            int yy=y+dy[k], xx=x+dx[k];
            if (inb(yy,xx) && bad(yy,xx)) q.emplace_back(xx,yy);
        }
    }
    // 子图 im 已写回 img16（共享视图）
}

// =================== 主循环 ===================
void ImageProcessor::run() {
    constexpr int bitMax = 65535;

    // 复用缓冲，避免反复分配
    cv::Mat src16;         // 零拷贝视图（指向 widget_image::pic）
    cv::Mat nuc16;         // NUC 输出 16U
    cv::Mat work16;        // 应用 BPM 后的 16U（用于最终显示）

    while(!m_stop.load(std::memory_order_relaxed)) {
        if (!m_hasFrame.exchange(false, std::memory_order_relaxed)) {
            QThread::msleep(1);
            continue;
        }

        const int W = m_srcW.load(std::memory_order_relaxed);
        const int H = m_srcH.load(std::memory_order_relaxed);
        if (W<=0 || H<=0) continue;

        // 1) 源视图（16U）——不拷贝，直接包装 widget_image::pic
        src16 = cv::Mat(H, W, CV_16UC1, (void*)&widget_image::pic[0][0]);

        // 2) 参考采集（直接用 16U）
        {
            QMutexLocker lk(&calibMutex);
            if (capturingLow) {
                lowBuf.emplace_back(); src16.copyTo(lowBuf.back());
                if ((int)lowBuf.size() >= sampleFrameNum) {
                    capturingLow = false;
                    emit captureStatus("低温参考完成");
                }
            }
            if (capturingHigh) {
                highBuf.emplace_back(); src16.copyTo(highBuf.back());
                if ((int)highBuf.size() >= sampleFrameNum) {
                    capturingHigh = false;
                    emit captureStatus("高温参考完成");
                }
            }
            // 样本到齐则计算 K/B 并量化
            if (!calibrated &&
                (int)lowBuf.size()  >= sampleFrameNum &&
                (int)highBuf.size() >= sampleFrameNum) {
                computeCalibrationLocked();
            }
        }

        // 3) 两点校正（整数乘加）：若未开启或未校准，就直接拷贝
        if (twoPointEnabled) {
            QMutexLocker lk(&calibMutex);
            if (calibrated && kbQuantized && !Kq.empty() && !Bq.empty() &&
                Kq.size()==src16.size()) {
                applyTwoPointNUC_Int(src16, nuc16);
            } else {
                src16.copyTo(nuc16);
            }
        } else {
            src16.copyTo(nuc16);
        }

        // 4) 静态坏点替代（先填洞，再用前沿替代；只处理坏点 ROI）
        {
            cv::Mat maskLocal;
            {   // 复制一份 BPM（无锁下处理，避免长时间占锁）
                QMutexLocker lk(&calibMutex);
                if (bpmEnabled && !bpm.empty() && bpm.size()==nuc16.size())
                    maskLocal = bpm.clone();
            }
            if (!maskLocal.empty()) {
                // 保险：再次填洞（防环形），然后替代
                fillHolesInPlace(maskLocal);
                work16 = nuc16;             // 以 NUC 输出为底图
                applyStaticBPM(maskLocal, work16);
            } else {
                work16 = nuc16;             // 无坏点则直接沿用
            }
        }

        // 5) 写回 QImage（16bit 或 8bit=右移8位），只在写入时短锁
        if (m_outputBits == 16) {
            QMutexLocker ui(&widget_image::s_imgMutex);
            const int dstW = widget_image::image.width();
            const int dstH = widget_image::image.height();
            const int bytesPerLine = widget_image::image.bytesPerLine();
            const int copyW = std::min(dstW, W);
            const int copyH = std::min(dstH, H);
            auto *dst = reinterpret_cast<uint16_t*>(widget_image::image.bits());
            const int dstStridePx = bytesPerLine/2;
            for (int y=0; y<copyH; ++y) {
                memcpy(dst + y*dstStridePx,
                       work16.ptr<uint16_t>(y),
                       size_t(copyW) * sizeof(uint16_t));
            }
        } else { // 8bit 显示：直接丢弃低 8 位（>>8）
            QMutexLocker ui(&widget_image::s_imgMutex);
            const int dstW = widget_image::image.width();
            const int dstH = widget_image::image.height();
            const int bytesPerLine = widget_image::image.bytesPerLine();
            const int copyW = std::min(dstW, W);
            const int copyH = std::min(dstH, H);
            uchar *dst8 = widget_image::image.bits();
            for (int y=0; y<copyH; ++y) {
                const uint16_t* srow = work16.ptr<uint16_t>(y);
                uchar* drow = dst8 + y*bytesPerLine;
                for (int x=0; x<copyW; ++x) {
                    drow[x] = uchar(uint32_t(srow[x]) >> 8);
                }
            }
        }

        emit updataimage(); // 解锁后刷新 UI
    }
}
