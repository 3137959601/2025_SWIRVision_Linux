#ifndef DRAWTHREEAD_H
#define DRAWTHREEAD_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <atomic>

//#include "widget_image.h"
//#include "opencv2/opencv.hpp"
//#include "opencv2/highgui.hpp"
class drawThread : public QThread
{
    Q_OBJECT
public:
    explicit drawThread(QObject *parent = nullptr);

//    void EqualizeHist_Array(cv::Mat& src, cv::Mat& dst, int graylevel,int dataBit);//直方图均衡
//    void EqualizeHist(cv::Mat& src, cv::Mat& dst, int graylevel, int dataBit);
//    void comp_medianBlur(cv::Mat& src,cv::Mat& src2, cv::Mat& dst );
//    void th_medianBlur(cv::Mat& src, cv::Mat& dst, int N);

    //void convert16bitTo8bit(ushort image16bit[][widget_image::image.width()], uchar image8bit[][widget_image::image.width()], int height, int width);
//    QImage Mat2QImage(cv::Mat const& src);
//    void normalizeMat(const cv::Mat& source, cv::Mat& dest, quint8 minv, quint8 maxv);
    static  uchar image8bit[2048][2048];    //512*640
    static  short image16bit[2048][2048];   //512*640

    void stop();  // 声明一个 stop 函数

    QElapsedTimer signalTimer; // 用于计时信号发送间隔的定时器
signals:
    void updataimage();
    void newImageReceived(); // 定义一个信号，表示有新图像接收

public slots:
    void setSourceSpec(int w, int h, int bytesPerPixel) {
        m_srcW.store(w, std::memory_order_relaxed);
        m_srcH.store(h, std::memory_order_relaxed);
        m_srcBpp.store(bytesPerPixel, std::memory_order_relaxed);
    }
    void recv_data();
    //void drawimage();//数据传输和解析
protected:
    void run();
private:
    bool stopFlag=false;  // 用于控制线程终止的标志位

    ushort imageBuffer1[512][640];  // 双缓冲区之一
    ushort imageBuffer2[512][640];  // 双缓冲区之二
    ushort (*currentBuffer)[640];   // 指向当前用于接收的缓冲区
    ushort (*displayBuffer)[640];   // 指向当前用于显示的缓冲区

    std::atomic<int> m_srcW{640};
    std::atomic<int> m_srcH{512};
    std::atomic<int> m_srcBpp{2};   // 1=8bit, 2=16bit

    std::atomic<bool> m_hasFrame{false}; // 简单“新帧到达”标记
};

#endif // DRAWTHREEAD_H
