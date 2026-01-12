#include "gl_image_widget.h"
#include <QImage>
#include <QDebug>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QCursor>

#include "widget_image.h"  // 直接复用现有的共享 QImage 缓冲与互斥锁

GLImageWidget::GLImageWidget(QWidget* p) : QOpenGLWidget(p) {
    setMinimumSize(64, 64);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent); // 让底层不透色
    setMouseTracking(true);

    //帧率计算
    paintFpsTimer.start();
}

GLImageWidget::~GLImageWidget() {
    makeCurrent();                          // 绑定当前 OpenGL 上下文
    if (vbo) glDeleteBuffers(1, &vbo);      // 删除顶点缓冲对象（VBO）
    if (vao) glDeleteVertexArrays(1, &vao); // 删除顶点数组对象（VAO）
    if (tex) glDeleteTextures(1, &tex);     // 删除纹理
    doneCurrent();                          // 释放 OpenGL 上下文
}

void GLImageWidget::setImageSpec(int w, int h, int bits) {
    if (w<=0 || h<=0) return;
    imgW = w; imgH = h; imgBits = (bits==8?8:16);
    specDirty = true;

    // ★ 关键：告诉布局系统我需要这么大，这样 QScrollArea 会按它来放
//    setMinimumSize(imgW, imgH);
//    updateGeometry();
    // ★ 不再强行把子控件最小尺寸设为图像尺寸
    //   让 ScrollArea 的 viewport 尺寸真实传入 resizeGL()，
    //   这样 minZoomFit 会以“可见范围”而不是“子控件像素”计算。
    updateMinZoomFit();
    if (zoom < minZoomFit) zoom = minZoomFit;
    clampOffset();
    update(); // 触发一次重建
}

void GLImageWidget::initializeGL() {
    initializeOpenGLFunctions();

    // 顶点缓冲：一个覆盖全屏的矩形（两个三角形）
    static const float quad[] = {
        // pos      // 纹理坐标uv [0, 1]（对应纹理图像的归一化坐标）
        -1, -1,     0, 0,  // 左下角
         1, -1,     1, 0,  // 右下角
        -1,  1,     0, 1,  // 左上角
        -1,  1,     0, 1,  // 左上角（重复）
         1, -1,     1, 0,  // 右下角（重复）
         1,  1,     1, 1   // 右上角
    };
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);//​​GL_STATIC_DRAW​​：提示 GPU 数据不会频繁修改

    // 简单着色器：把 R 通道归一化为灰度
    const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main(){ vUV=aUV; gl_Position=vec4(aPos,0,1); }\n";
    const char* fs =
        "#version 330 core\n"
        "in vec2 vUV; out vec4 FragColor;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec2 uImgSize;    // 图像尺寸（像素）\n"
        "uniform vec2 uViewSize;   // 视口尺寸（像素）\n"
        "uniform vec2 uOffsetPx;   // 左上角在图像坐标的偏移（像素）\n"
        "uniform float uZoom;      // 放大倍数\n"
        "void main(){\n"
        "  //// vUV 是 [0,1] 的屏幕归一坐标，换算到视口像素\n"
        "  //vec2 viewPx = vUV * uViewSize;\n"
        "  //// 对应的图像像素坐标 = 偏移 + 视口像素 / zoom\n"
        "  //vec2 imgPx  = uOffsetPx + viewPx / uZoom;\n"
        "  // 屏幕坐标改为“自上而下”，与鼠标/像素读取一致\n"
        "  vec2 viewPxTop = vec2(vUV.x, 1.0 - vUV.y) * uViewSize;\n"    //替换uv.y 上下翻转 因为QImage 的(0,0)在左上角，而OpenGL 纹理坐标 (0,0)在左下角。
        "  vec2 imgPx     = uOffsetPx + viewPxTop / uZoom;\n"
        "  // 归一化到纹理坐标，并做 Y 翻转（QImage 顶为0行）\n"
        "  vec2 uv = imgPx / uImgSize;\n"
        "  // 越界不采样，直接画黑（避免边界texel被拉伸）\n"
        "  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
        "      FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n    //黑"
        "      //FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n  // 白"
        "      return;\n"
        "  }\n"
        "  vec4 t = texture(uTex, uv);\n"
        "  float g = t.r;  // 灰度\n"
        "  FragColor = vec4(g,g,g,1);\n"
        "}\n";
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
        !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) ||
        !program.link()) {
        qWarning() << "GL shader link error:" << program.log();
    }
    program.bind();
    uTexLoc = program.uniformLocation("uTex");
    uImgSizeLoc  = program.uniformLocation("uImgSize");
    uViewSizeLoc = program.uniformLocation("uViewSize");
    uOffsetLoc   = program.uniformLocation("uOffsetPx");
    uZoomLoc     = program.uniformLocation("uZoom");
    program.setUniformValue(uTexLoc, 0);

    // 顶点属性
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // 1字节对齐，避免行对齐踩雷
}

void GLImageWidget::resizeGL(int /*w*/, int /*h*/) {
//    viewW = w; viewH = h;
//    glViewport(0,0,w,h);
    // 统一：视口/着色器/鼠标都使用“设备像素(px)”避免屏幕分辨率变化后坐标对弈错误
    const qreal dpr = devicePixelRatioF();
    const int fbW = int(std::round(width()  * dpr));
    const int fbH = int(std::round(height() * dpr));

    viewW = fbW;
    viewH = fbH;
    glViewport(0, 0, fbW, fbH);
    updateMinZoomFit();
    if (zoom < minZoomFit) zoom = minZoomFit;
    clampOffset(); // 视窗改变时，限制偏移
}

void GLImageWidget::ensureTexture() {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    // ★ 不做平滑：使用最近邻
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 背景想要白色：越界采样走“白色边框”
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float white[4] = {1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, white);

    if (specDirty && imgW>0 && imgH>0) {
        if (imgBits == 16)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, imgW, imgH, 0, GL_RED, GL_UNSIGNED_SHORT, nullptr);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,  imgW, imgH, 0, GL_RED, GL_UNSIGNED_BYTE,  nullptr);
        specDirty = false;
    }
}

void GLImageWidget::paintGL() {
    glClearColor(0,0,0,1);  //背景为黑色
    glClear(GL_COLOR_BUFFER_BIT);

    program.bind();
    ensureTexture();

    // 上传共享 QImage 到纹理（与 ImageProcessor 共用的帧缓冲）
    if (imgW>0 && imgH>0) {
        QMutexLocker lk(&widget_image::s_imgMutex);
        const QImage& qimg = widget_image::image;
        if (!qimg.isNull()) {
            // 若尺寸不一致，跟随 QImage 重建纹理，避免黑屏
            if (qimg.width()!=imgW || qimg.height()!=imgH) {
                imgW = qimg.width(); imgH = qimg.height();
                specDirty = true; ensureTexture();
            }
            glBindTexture(GL_TEXTURE_2D, tex);
            if (imgBits == 16 && qimg.format()==QImage::Format_Grayscale16) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, imgW, imgH,
                                GL_RED, GL_UNSIGNED_SHORT, qimg.constBits());
            } else if (imgBits == 8 && qimg.format()==QImage::Format_Grayscale8) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, imgW, imgH,
                                GL_RED, GL_UNSIGNED_BYTE,  qimg.constBits());
            }
        }
    }

    // 更新 uniforms（用于平移缩放）
    program.setUniformValue(uImgSizeLoc,  (float)imgW, (float)imgH);
//    program.setUniformValue(uViewSizeLoc, (float)width(), (float)height());
    program.setUniformValue(uViewSizeLoc, (float)viewW, (float)viewH);  //统一坐标系到设备像素（HiDPI 正确）：uViewSize & 鼠标点都用 FBO 尺寸
    program.setUniformValue(uOffsetLoc,   (float)offsetPx.x(), (float)offsetPx.y());
    program.setUniformValue(uZoomLoc,     (float)zoom);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    program.release();
    {
        // 把全局鼠标坐标换到本地
        const QPoint globalPos = QCursor::pos();
        const QPoint localPos  = mapFromGlobal(globalPos);

        if (rect().contains(localPos)) {
            // 鼠标仍在控件上：重算一次当前鼠标下的像素信息
            updateHoverInfo(localPos);
        } else {
            // 鼠标不在控件上：通知 UI 清空显示
            emit hoverInfoChanged(0, 0, 0, false);
        }
    }
    paintFpsCount++;
    qint64 ms = paintFpsTimer.elapsed();
    if (ms >= 1000) {
        lastDisplayFps = paintFpsCount * 1000.0 / ms;
        emit displayFpsChanged(lastDisplayFps);
        paintFpsCount = 0;
        paintFpsTimer.restart();
    }
}

void GLImageWidget::repaintFromSharedImage() {
    update(); // 纹理上传在 paintGL
}

void GLImageWidget::repaintFromRaw16(const void* ptr, int w, int h) {
    if (!ptr || w<=0 || h<=0) return;
    if (w!=imgW || h!=imgH || imgBits!=16) {
        imgW=w; imgH=h; imgBits=16; specDirty=true;
    }
    makeCurrent();
    ensureTexture();
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, imgW, imgH, GL_RED, GL_UNSIGNED_SHORT, ptr);
    doneCurrent();
    update();
}

// ====================== 交互：缩放/拖拽 ======================
//边界钳位（避免显示越界）
void GLImageWidget::clampOffset() {
    if (imgW<=0 || imgH<=0 || viewW<=0 || viewH<=0) return;

    const double winW = double(viewW) / zoom;   // 视口映射到图像坐标的宽
    const double winH = double(viewH) / zoom;   // 高

    // X 方向
    if (winW >= imgW) {
        // ★ 图像比窗口“窄” → 居中（允许留黑边）
        offsetPx.setX( (imgW - winW) * 0.5 );
    } else {
        // 图像比窗口“宽” → 约束在可视范围内
        const double minX = 0.0;
        const double maxX = imgW - winW;
        if (offsetPx.x() < minX) offsetPx.setX(minX);
        if (offsetPx.x() > maxX) offsetPx.setX(maxX);
    }

    // Y 方向
    if (winH >= imgH) {
        // ★ 图像比窗口“矮” → 居中（允许留黑边）
        offsetPx.setY( (imgH - winH) * 0.5 );
    } else {
        const double minY = 0.0;
        const double maxY = imgH - winH;
        if (offsetPx.y() < minY) offsetPx.setY(minY);
        if (offsetPx.y() > maxY) offsetPx.setY(maxY);
    }
}


void GLImageWidget::wheelEvent(QWheelEvent* e) {
    const bool ctrl  = (e->modifiers() & Qt::ControlModifier);
    const bool shift = (e->modifiers() & Qt::ShiftModifier);

    if (!ctrl) {
        // ------ 平移模式：滚轮上下/Shift+滚轮左右 ------
        e->accept();

        // 1) 取像素级滚动（触控板）优先；没有则用档位*步长像素
        QPoint pd = e->pixelDelta();     // 真实像素（可能为 0,0）
        QPoint ad = e->angleDelta();     // 以 1/8 度为单位，y 通常是垂直滚动
        double base = 60.0;              // 每档滚动映射到约 60 个屏幕像素

        double dx_px = 0.0, dy_px = 0.0;
        if (!pd.isNull()) { dx_px = pd.x(); dy_px = pd.y(); }
        else { dx_px = (ad.x() / 120.0) * base; dy_px = (ad.y() / 120.0) * base; }

        // 2) 按住 Shift：把“竖滚”当“横滚”
        if (shift && dx_px == 0.0 && dy_px != 0.0) { dx_px = dy_px; dy_px = 0.0; }

        // 3) 映射到图像坐标（offset 是以“图像像素”为单位）
        offsetPx.rx() -= dx_px / zoom;   // 滚轮“向上” => ad.y()>0 => dx_px/dy_px>0
        offsetPx.ry() -= dy_px / zoom;   // 约定：向上滚=向上移动图像（看更上方内容）
        clampOffset();
        update();
        return;
    }
    // ------ 缩放模式 ------
    e->accept();

    const int steps = e->angleDelta().y() / 120;
    if (steps == 0) return;

    const double oldZoom = zoom;
    double newZoom = zoom * std::pow(1.25, steps);

    updateMinZoomFit();
    if (newZoom < minZoomFit) newZoom = minZoomFit;
    if (newZoom > maxZoom)    newZoom = maxZoom;
    if (std::abs(newZoom - oldZoom) < 1e-9) return;

    // 1) 鼠标锚点（图像坐标）
//    const QPointF m = e->position(); // 视口像素
    const QPointF m = e->position() * devicePixelRatioF(); // 设备像素，与 viewW/viewH 对齐
    const double imgX = offsetPx.x() + m.x() / oldZoom;
    const double imgY = offsetPx.y() + m.y() / oldZoom;

    // 2) 应用缩放
    zoom = newZoom;

    // 3) 反算新的 offset，使鼠标下仍是同一像素
    offsetPx.setX(imgX - m.x() / zoom);
    offsetPx.setY(imgY - m.y() / zoom);
    // 4) 最后做边界钳位（可能触发居中/贴边）
    clampOffset();
    update();
}

void GLImageWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        panning = true;
        lastMousePos = e->pos();
        // 记录锚点：按下时光标对应的图像坐标
        pressAnchorImgPx = QPointF(
            offsetPx.x() + double(e->pos().x())/zoom,
            offsetPx.y() + double(e->pos().y())/zoom
        );
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    e->ignore();
}

void GLImageWidget::mouseMoveEvent(QMouseEvent* e) {
    if (panning) {
        const QPoint delta = e->pos() - lastMousePos;
        lastMousePos = e->pos();
        // ★ 图像“跟手走”：向右拖→图像向右，向下拖→图像向下
        offsetPx.rx() += double(delta.x()) / zoom;
        offsetPx.ry() += double(delta.y()) / zoom;
        clampOffset();
        update();
        e->accept();
        return;
    }
    // 非拖拽：更新悬浮像素信息
    updateHoverInfo(e->pos());
    update();
    e->ignore();
}


void GLImageWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (panning && e->button()==Qt::LeftButton) {
        panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    e->ignore();
}

void GLImageWidget::leaveEvent(QEvent* e) {
    Q_UNUSED(e);
    // 鼠标离开就不再显示浮标
    emit hoverInfoChanged(0, 0, 0, false);
    if (panning) { panning = false; unsetCursor(); }
}

void GLImageWidget::updateMinZoomFit() {
    if (imgW<=0 || imgH<=0 || viewW<=0 || viewH<=0) {
        minZoomFit = 0.1;
        return;
    }
    // 可见范围：优先取父级（ScrollArea 的 viewport），回退到自身
    qreal dpr = devicePixelRatioF();
    int visW = viewW, visH = viewH; // 默认：FBO 尺寸
    if (QWidget* vp = parentWidget()) {            // 在 ScrollArea 中时，parent 就是 viewport
        visW = std::min(visW, int(vp->width()  * dpr));
        visH = std::min(visH, int(vp->height() * dpr));
    }
    const double sx = double(visW) / double(imgW);
    const double sy = double(visH) / double(imgH);
//    minZoomFit = std::max(0.1, std::min(sx, sy));  // 允许“完整适配”（无黑边）minZoomFit = std::min(sx,sy); 全图可见 + 留黑边
    minZoomFit = std::min(sx,sy);
}

void GLImageWidget::updateHoverInfo(const QPoint& widgetPos)
{
    lastMousePos = widgetPos;  // 逻辑像素；用于在屏上画浮标

    if (imgW<=0 || imgH<=0 || viewW<=0 || viewH<=0) { emit hoverInfoChanged(0, 0, 0, false); return; }

    // 1) 逻辑像素 → 设备像素（与 paintGL 里的 uViewSize 一致）
    const qreal dpr = devicePixelRatioF();
    const QPointF m = widgetPos * dpr;

    // 2) 设备像素 → 图像像素坐标（与shader 的公式一致：imgPx = offset + viewPx/zoom）
    const double fx = offsetPx.x() + m.x() / zoom;
    const double fy = offsetPx.y() + m.y() / zoom;

    // 用 round 与 GL_NEAREST 的取样行为保持一致（避免边界错一行）
//    int ix = int(std::round(fx));
//    int iy = int(std::round(fy));

    // 先做越界判断（浮点判定），越界就发 invalid
    if (fx < 0.0 || fy < 0.0 || fx >= double(imgW) || fy >= double(imgH)) {
        emit hoverInfoChanged(0, 0, 0, false);
        return;
    }

    // 与 GL_NEAREST 对齐：用 floor 取当前像素（而不是 round）
    const int ix = int(std::floor(fx));
    const int iy = int(std::floor(fy));

    // 3) 读取像素值（从共享 QImage）
    QMutexLocker lk(&widget_image::s_imgMutex);
    const QImage& qimg = widget_image::image;
    if (qimg.isNull() || qimg.width()!=imgW || qimg.height()!=imgH) {emit hoverInfoChanged(0, 0, 0, false); return; }

    quint32 dn = 0;
    if (qimg.format() == QImage::Format_Grayscale16) {
        const uchar* row = qimg.constScanLine(iy);
        const quint16* p  = reinterpret_cast<const quint16*>(row);
//        dn = p[ix];                           // 0..65535
        // 端序安全：多数采集是小端，qFromLittleEndian() 可确保显示为正确 DN
        dn = qFromLittleEndian<quint16>(p[ix]);
    } else if (qimg.format() == QImage::Format_Grayscale8) {
        dn = qimg.constScanLine(iy)[ix];     // 0..255
    } else {
        // 其他格式暂不支持悬浮读取
        emit hoverInfoChanged(0, 0, 0, false); return;
    }

    emit hoverInfoChanged(ix, iy, dn, true);   // 把结果发出去
}
//确保鼠标离开scrollarea后小浮窗消失
void GLImageWidget::ensureViewportFilter() {
    QWidget* vp = parentWidget();             // ScrollArea::viewport()
    if (vp && vp != watchedViewport) {
        if (watchedViewport) watchedViewport->removeEventFilter(this);
        vp->installEventFilter(this);
        vp->setMouseTracking(true);
        watchedViewport = vp;
    }
}

bool GLImageWidget::event(QEvent* ev) {
    // 当 parent 发生变化或显示时，尝试安装过滤器
    if (ev->type() == QEvent::ParentChange || ev->type() == QEvent::Show) {
        ensureViewportFilter();
    }
    return QOpenGLWidget::event(ev);
}

bool GLImageWidget::eventFilter(QObject* watched, QEvent* ev) {
    if (watched == watchedViewport) {
        if (ev->type() == QEvent::Leave) {
            // 鼠标离开 ScrollArea 的可视区域
            emit hoverInfoChanged(0, 0, 0, false);
            update();                  // 立刻消失浮窗
        }
    }
    return false; // 交回默认处理
}
