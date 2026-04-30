#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QMutex>
#include <QPointF>
#include <QtEndian>   // 端序安全读取 16bit
#include <QElapsedTimer>

class GLImageWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLImageWidget(QWidget* parent=nullptr);
    ~GLImageWidget() override;

    // 设置图像规格（宽高 + 位深：8 或 16）
    void setImageSpec(int w, int h, int bits /*8 or 16*/);

public slots:
    // 从共享 QImage 刷新（widget_image::image）
    void repaintFromSharedImage();
    // 可选：从原始16U指针直接上传（当前工程可不用）
    void repaintFromRaw16(const void* ptr, int w, int h);
signals:
    // 悬浮像素信息：x, y, DN；valid=false 表示鼠标越界/离开
    void hoverInfoChanged(int x, int y, quint32 dn, bool valid,
                          double avgAllDn, bool avgAllValid,
                          double avgRoiDn, bool avgRoiValid);
    void displayFpsChanged(double fps);     //显示帧率
protected:
    void initializeGL() override;
    void resizeGL(int /*w*/, int /*h*/) override;
    void paintGL() override;

    // 交互
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool event(QEvent* ev) override;                 //  拦截 ParentChange/Show
    bool eventFilter(QObject* watched, QEvent* ev) override; //  过滤 viewport
private:
    void ensureTexture();
    void clampOffset(); // 限制 offset 不越界
    // 帮助函数
    void updateMinZoomFit();          // 计算 minZoomFit
    void updateHoverInfo(const QPoint& widgetPos);  // 根据鼠标位置更新 hover 数据

    void ensureViewportFilter();   // 安装/更新事件过滤器
    QWidget* watchedViewport = nullptr;  // 当前已安装过滤器的 viewport

    QOpenGLShaderProgram program;
    GLuint tex = 0;
    GLuint vao = 0, vbo = 0;

    // 图像规格
    int imgW = 0, imgH = 0;
    int imgBits = 16; // 8/16
    bool specDirty = true;

    // 视口大小（像素）
    int viewW = 0, viewH = 0;

    // 交互参数：缩放 & 平移（单位：像素）
    double zoom = 1.0;                 // 放大倍数（>= minZoom）
    QPointF offsetPx = QPointF(0, 0);  // 左上角在图像中的偏移（像素）
    const double minZoom = 0.1;
    double minZoomFit = 0.1;          // 动态：铺满窗口的最小缩放
    const double maxZoom = 64.0;

    // 拖拽状态
    bool panning = false;
    QPoint lastMousePos;
    QPointF pressAnchorImgPx;         // 按下时的“光标下图像坐标”

    // uniforms
    GLint uTexLoc     = -1;
    GLint uImgSizeLoc = -1;
    GLint uViewSizeLoc= -1;
    GLint uOffsetLoc  = -1;
    GLint uZoomLoc    = -1;

    //帧率
    QElapsedTimer paintFpsTimer;
    int paintFpsCount = 0;
    double lastDisplayFps = 0.0;

    double lastFrameMeanAllDn = 0.0;
    bool hasFrameMeanAllDn = false;
    double lastFrameMeanRoiDn = 0.0;
    bool hasFrameMeanRoiDn = false;
};
