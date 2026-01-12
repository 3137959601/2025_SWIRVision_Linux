#ifndef WIDGET_IMAGE_H
#define WIDGET_IMAGE_H

#include <QWidget>
#include <QPainter>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>

class widget_image : public QWidget
{
    Q_OBJECT
public:
    explicit widget_image(QWidget *parent = nullptr);
    ~ widget_image();
    void paintEvent(QPaintEvent *e);
    void repaintImage();
    // === 动态配置图像规格（宽/高/格式） ===
    void setImageSpec(int w, int h, QImage::Format fmt);

    // === 原始16U缓冲（供采集/处理） ===
    static void resizeRaw(int w, int h, int stridePx = -1) {
        if (stridePx <= 0) stridePx = w;
//        QMutexLocker lk(&s_imgMutex);
        picW = w; picH = h; picStridePx = stridePx;
        pic.assign(size_t(picStridePx) * picH, 0);
    }
    static inline uint16_t* rawPtr() {
        return pic.empty() ? nullptr : pic.data();
    }
    static inline int rawStridePx() { return picStridePx; }
    static inline int rawWidth()    { return picW; }
    static inline int rawHeight()   { return picH; }

    // 让 QScrollArea 知道“实际大小”，从而出现滚动条
    QSize sizeHint() const override { return image.size(); }

    static QMutex s_imgMutex;

    static QImage image;
//    static  short pic[512][640];
//    static  short pic[2048][2048];
    static std::vector<uint16_t> pic;
    static int picW, picH, picStridePx;
    QElapsedTimer signalTimer; // 用于计时信号发送间隔的定时器
signals:

};

#endif // WIDGET_IMAGE_H
