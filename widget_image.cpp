#include "widget_image.h"

QImage widget_image::image = QImage(640,512,QImage::Format_Grayscale16);
//short widget_image::pic[512][640];
//short widget_image::pic[2048][2048];
// 新的 vector + 规格
std::vector<uint16_t> widget_image::pic;
int widget_image::picW = 0;
int widget_image::picH = 0;
int widget_image::picStridePx = 0;

// int signal_cnt=0;
// qint64 interval_avg=0;
QMutex widget_image::s_imgMutex;

widget_image::widget_image(QWidget *parent) : QWidget(parent)
{
    //signalTimer.start(); // 开始计时
}

widget_image::~widget_image()
{

}

void widget_image::setImageSpec(int w, int h, QImage::Format fmt)
{
    // 重新分配 QImage（如果规格不同才重建）
    if (image.width() != w || image.height() != h || image.format() != fmt) {
        QImage newImg(w, h, fmt);
        newImg.fill(0);
        image = std::move(newImg);
        updateGeometry(); // 通知布局系统 sizeHint 改了（滚动区域会更新）
        update();
    }
}

void widget_image::paintEvent(QPaintEvent*)
{
    QMutexLocker lock(&widget_image::s_imgMutex);
    QPainter p(this);
    p.drawImage(0, 0, image); // 1:1
//    // 如果窗口比图像小，则等比缩放以全貌可见；否则 1:1 绘制
//    const QSize imgSize = image.size();
//    const QSize winSize = size();

//    if (imgSize.width() > winSize.width() || imgSize.height() > winSize.height()) {
//        // 等比缩放以适应窗口
//        const double sx = (double)winSize.width()  / imgSize.width();
//        const double sy = (double)winSize.height() / imgSize.height();
//        const double s  = std::min(sx, sy);
//        p.scale(s, s);
//        p.drawImage(0, 0, image);
//    } else {
//        // 居中显示（窗口更大时）
//        const int x = (winSize.width()  - imgSize.width())  / 2;
//        const int y = (winSize.height() - imgSize.height()) / 2;
//        p.drawImage(QPoint(x, y), image);
//    }
}

void widget_image::repaintImage()
{
    // qint64 interval = signalTimer.nsecsElapsed()/1000; // 获取上一次信号发送以来的时间，单位为微秒
    // signal_cnt++;
    // interval_avg=interval_avg+interval;
    // if(signal_cnt==100)
    // {
    //     interval_avg=interval_avg/100;
    //     signal_cnt=0;
    //     qDebug() << "-----------update()--------- :" << interval_avg << "us";//平均差不多
    //     interval_avg=0;
    // }
    // signalTimer.restart(); // 重启计时器以记录下一次信号发送的时间间隔

    update();
}
