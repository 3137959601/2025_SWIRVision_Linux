#include "drawthread.h"
#include "qimage.h"
#include "widget_image.h"
#include "QMutex"
#include "mainwindow.h"

//#include "opencv2/opencv.hpp"
//#include "opencv2/highgui.hpp"
//#include "opencv2/imgproc/imgproc.hpp"
#include<qelapsedtimer.h>
//using namespace cv;

static QMutex mutex;
uchar drawThread::image8bit[2048][2048];
short drawThread::image16bit[2048][2048];
int i,j=0;
drawThread::drawThread(QObject *parent) : QThread(parent)
{
    //signalTimer.start(); // 开始计时
//    currentBuffer = imageBuffer1;
//    displayBuffer = imageBuffer2;
}
// drawthread.cpp
void drawThread::recv_data() {
    m_hasFrame.store(true, std::memory_order_relaxed);
}

void drawThread::run() {
    while (!stopFlag) {
        if (!m_hasFrame.exchange(false)) {
            QThread::msleep(1);
            continue;
        }

        // 读取源规格（transferThread 下发）
        const int srcW = m_srcW.load(std::memory_order_relaxed);
        const int srcH = m_srcH.load(std::memory_order_relaxed);
        const int srcBpp = m_srcBpp.load(std::memory_order_relaxed); // 1 或 2（你现在 USB 源是16bit => 2）

        // 读取目标规格（显示）
        QMutexLocker lock(&widget_image::s_imgMutex);
        const int dstW = widget_image::image.width();
        const int dstH = widget_image::image.height();
        const int dstBpp = widget_image::image.depth() / 8;
        const int bytesPerLine = widget_image::image.bytesPerLine();

        const int copyW = (std::min)(dstW, srcW);
        const int copyH = (std::min)(dstH, srcH);

        if (widget_image::image.format() == QImage::Format_Grayscale16) {
            auto *dst = reinterpret_cast<ushort*>(widget_image::image.bits());
            const int dstPixelsPerLine = bytesPerLine / (dstBpp ? dstBpp : 1);

            if (srcBpp == 2) {
                // 源16 -> 目标16：逐行 memcpy
                for (int y = 0; y < copyH; ++y) {
                    memcpy(dst + y * dstPixelsPerLine,
                           &widget_image::pic[y][0],
                           size_t(copyW) * sizeof(ushort));
                }
            } else {
                // 源8 -> 目标16：简单提升（左移8位）
                for (int y = 0; y < copyH; ++y) {
                    ushort* d = dst + y * dstPixelsPerLine;
                    const uchar* s = reinterpret_cast<const uchar*>(&widget_image::pic[y][0]); // 若真是8bit源，建议单独维护8bit缓存
                    for (int x = 0; x < copyW; ++x) d[x] = ushort(s[x]) << 8;
                }
            }
        } else { // 目标8位
            uchar *dst8 = widget_image::image.bits();

            if (srcBpp == 2) {
                // 源16 -> 目标8：右移8位
                for (int y = 0; y < copyH; ++y) {
                    uchar *row = dst8 + y * bytesPerLine;
                    const ushort* s = reinterpret_cast<const ushort*>(&widget_image::pic[y][0]);
                    for (int x = 0; x < copyW; ++x) row[x] = static_cast<uchar>(s[x] >> 8);
                }
            } else {
                // 源8 -> 目标8：直接行拷
                for (int y = 0; y < copyH; ++y) {
                    uchar *row = dst8 + y * bytesPerLine;
                    const uchar* s = reinterpret_cast<const uchar*>(&widget_image::pic[y][0]);
                    memcpy(row, s, size_t(copyW));
                }
            }
        }
        lock.unlock();

        emit updataimage(); // 解锁后再发信号，避免 UI 线程拿不到锁
    }
}


void drawThread::stop() {
    stopFlag = true;  // 外部调用时将标志位设为 true
}

//void drawThread::drawimage()
//{
//    uchar image8bit[512][640];
//    ushort image16bit[512][640];
//    memcpy(image16bit,widget_image::pic,512*640*2); // 16bit图像数组
////    ushort minValue = widget_image::pic[0][0];
////    ushort maxValue = widget_image::pic[0][0];
//    ushort minValue = image16bit[0][0];
//    ushort maxValue = image16bit[0][0];
//    // 寻找16bit图像数组的最小值和最大值
//    for(int i = 0; i < 512; i++){
//        for(int j = 0; j < 640; j++){
//            if(image16bit[i][j] < minValue){
//                minValue = image16bit[i][j];
//            }
//            if(image16bit[i][j] > maxValue){
//                maxValue = image16bit[i][j];
//            }
//        }
//    }
//    // 将16bit二维图像数组转换为8bit二维图像数组
//    double range = maxValue - minValue;

//    for(int i = 0; i < 512; i++){
//        for(int j = 0; j < 640; j++){
//            image8bit[i][j] = static_cast<uchar>((image16bit[i][j] - minValue) * 255.0/ range);
//             //widget_image::image.setPixel(j,i,static_cast<uchar>((image16bit[i][j] - minValue) * 255.0 / range));
//        }
//    }
//    for(int x=0;x<640;x++)
//    {
//        for(int y=0;y<512;y++)
//        {
//            uchar pixelValue = image8bit[y][x];
//            widget_image::image.setPixel(x, y, qRgb(pixelValue, pixelValue, pixelValue));

//        }
//    }
//    emit updataimage();
//}
//void drawThread::convert16bitTo8bit(ushort image16bit[][widget_image::image.width()], uchar image8bit[][widget_image::image.width()], int height, int width)
//{
//    ushort minValue = image16bit[0][0];
//    ushort maxValue = image16bit[0][0];

//    // 寻找16bit图像数组的最小值和最大值
//    for(int i = 0; i < height; i++){
//        for(int j = 0; j < width; j++){
//            if(image16bit[i][j] < minValue){
//                minValue = image16bit[i][j];
//            }
//            if(image16bit[i][j] > maxValue){
//                maxValue = image16bit[i][j];
//            }
//        }
//    }

//    // 将16bit二维图像数组转换为8bit二维图像数组
//    double range = maxValue - minValue;
//    for(int i = 0; i < height; i++){
//        for(int j = 0; j < width; j++){
//            image8bit[i][j] = static_cast<uchar>((image16bit[i][j] - minValue) * 255.0 / range);
//        }
//    }
//}

//void drawThread::drawimage()
//{
//    // 假设16bit二维数组图像为image16bit，width为图像宽度，height为图像高度
//    cv::Mat img16bit(512, 640, CV_16UC1, widget_image::pic); // 创建16bit Mat图像

//    // 将16bit Mat图像转换为8bit Mat图像
//    cv::Mat img8bit;
//    double minVal, maxVal;
//    cv::minMaxLoc(img16bit, &minVal, &maxVal);
//    img16bit.convertTo(img8bit, CV_8UC1, 255.0 / (maxVal - minVal), -minVal * 255.0 / (maxVal - minVal));


//    // 将8bit Mat图像转换为QImage图像
//    widget_image::image = Mat2QImage(img8bit);
//    emit updataimage();

//}
//QImage drawThread::Mat2QImage(cv::Mat const& src) {
//    cv::Mat temp; // 临时Mat对象
//    cvtColor(src, temp, cv::COLOR_GRAY2RGB); // 转换为RGB格式
//    QImage dest((uchar*) temp.data, temp.cols, temp.rows, temp.step, QImage::Format_RGB888);
//    dest.bits(); // 激活像素数据
//    return dest;
//}

//void drawThread::drawimage()
//{
//    //uchar image8bit[512][640];
//    //ushort image16bit[512][640];
//    memcpy(image16bit,widget_image::pic,512*640*2); // 16bit图像数组
////    ushort minValue = widget_image::pic[0][0];
////    ushort maxValue = widget_image::pic[0][0];
//    ushort minValue = image16bit[0][0];
//    ushort maxValue = image16bit[0][0];
//    // 寻找16bit图像数组的最小值和最大值
//    for(int i = 0; i < 512; i++){
//        for(int j = 0; j < 640; j++){
//            if(image16bit[i][j] < minValue){
//                minValue = image16bit[i][j];
//            }
//            if(image16bit[i][j] > maxValue){
//                maxValue = image16bit[i][j];
//            }
//        }
//    }
//    // 将16bit二维图像数组转换为8bit二维图像数组
//    double range = maxValue - minValue;

//    for(int i = 0; i < 512; i++){
//        for(int j = 0; j < 640; j++){
//            image8bit[i][j] = static_cast<uchar>((image16bit[i][j] - minValue) * 255.0/ range);
//             //widget_image::image.setPixel(j,i,static_cast<uchar>((image16bit[i][j] - minValue) * 255.0 / range));
//        }
//    }

//    //Mat mat2= Mat::zeros(512, 640, CV_8UC1);
////    Mat mat6;
////    Mat mat0(512,640,CV_16U,widget_image::pic);
////    Mat mat(512, 640, CV_64FC1);
////    mat0.convertTo(mat, CV_16FC1, 1 / 255.0);
////    mat0.convertTo(mat0, CV_8UC1, 1);
////    equalizeHist(mat0, mat);
////    mat.convertTo(mat, CV_64FC1, 255.0);

//    //Mat mat5(512,640,CV_16U,widget_image::pic);
//    //normalize(mat5, mat, 0., 255., cv::NORM_MINMAX, CV_8UC1);
//    //normalizeMat(mat5, mat, 0, 255);
//    QElapsedTimer mstimer;
//    mstimer.start();
//    Mat mat(512,640,CV_8U,image8bit);

//    UMat umat;
//    Mat mat1 = mat;
//    //th_medianBlur(mat1,mat,3);
//    Mat mat2 = Mat::zeros(512,640,CV_8U);//
//    UMat umat2;

//    if(Widget::b_medianblur){

//        mat.copyTo(umat);

//        mat2.copyTo(umat2);
//        medianBlur(umat,umat2,3);
//        umat2.copyTo(mat2);
//        comp_medianBlur(mat,mat2,mat);
//        if(Widget::b_frame_save==true)
//        {
//            imwrite("median_blur_image.jpg", mat);
//            Widget::b_frame_save=false;
//        }
////        mat.copyTo(umat);
////        Sobel(umat, umat2, CV_16U, 1, 1);
////        add(umat,umat2,umat);
////        umat.copyTo(mat);

//    }
//    //blur(umat,umat,Size(3,3));
//    //normalize(umat, umat, 65536, 0, NORM_MINMAX, CV_16U);

//    if(Widget::b_equalizehist)
//    {
//        //EqualizeHist_Array(mat,mat,256,8);
//        equalizeHist(mat,mat);
//        if(Widget::b_frame_save==true)
//        {
//            imwrite("histogram_equalized_image.jpg", mat);
//            Widget::b_frame_save=false;
//        }
//    }
//    QImage im( mat.data,mat.cols, mat.rows,static_cast<int>(mat.step),QImage::Format_Grayscale8 );
//    if(Widget::b_frame_save==true)
//    {
//        imwrite("original_image.jpg", mat);
//        Widget::b_frame_save=false;
//    }
//    //QPixmap pix  = QPixmap::fromImage( im );
//    widget_image::image = im;

//    float time = (double)mstimer.nsecsElapsed()/(double)1000000;
//    qDebug() <<"DrawImage TimeCost:"<< time<<"ms";// ms

//    emit updataimage();

//}
//void drawThread::normalizeMat(const cv::Mat& source, cv::Mat& dest, quint8 minv, quint8 maxv)
//{
//    int cols = source.cols;
//    int rows = source.rows;
//    for (int k = 0; k < rows; k++)
//    {
//        const ushort* matRowPtr = source.ptr<ushort>(k);
//        quint8* destMatRowPtr = dest.ptr<quint8>(k);
//        for (int j = 0; j < cols; j++)
//        {
//            quint8 pixData = static_cast<quint8>((*matRowPtr++ - minv) / (maxv -minv) * 255);
//            *destMatRowPtr++ = pixData;
//        }
//    }
//}

//void drawThread::comp_medianBlur(Mat& src,Mat& src2, Mat& dst )
//{

//    for (size_t i = 0; i < src.rows; i++)
//    {
//        ushort* pSrc = src.ptr<ushort>(i);
//        ushort* pSrc2 = src2.ptr<ushort>(i);
//        ushort* pDst = dst.ptr<ushort>(i);
//        for (size_t j = 0; j < src.cols; j++)
//        {
//            if((pSrc[j]<=pSrc2[j]/1.2)||(pSrc[j]>=pSrc2[j]*1.2))//60000
//                pDst[j] = pSrc2[j];
//                ;
//        }
//    }
//}
//void drawThread::th_medianBlur(Mat& src, Mat& dst, int size)
//{
//    //copy
////    for (size_t i = 0; i < src.rows; i++)
////    {
////        uchar* pSrc = src.ptr<uchar>(i);
////        uchar* pDst = dst.ptr<uchar>(i);
////        for (size_t j = 0; j < src.cols; j++)
////        {
////            pDst[j] = pSrc[j];
////        }
////    }

//    int _size = (size-1)/2;
//    int i = 0;
//    int half = (size*size+1)/2;
//    int num = size*size;
//    ushort res;

//    for (int m = _size; m < src.rows-_size; ++m){

//        ushort* ptr[9] ;
//        for( i = 0;i<size;i++){
//            ptr[i] = src.ptr<ushort>(m - _size + i);
//        }
//        unsigned short* pDst = dst.ptr<unsigned short>(i);
//        for (int n = _size; n < src.cols-_size; ++n)
//          {
//             //   Pick up window elements
//             int k = 0;
//             ushort window[81];
//             for ( i = 0; i < size; i++)
//                for (int j = n - _size; j < n - _size +i; j++)
//                   window[k++] = ptr[i][j];
//             //   Order elements (only half of them)
//             for (int j = 0; j < half; ++j)
//             {
//                //   Find position of minimum element
//                int min = j;
//                for (int l = j + 1; l < num; ++l){
//                    if (window[l] < window[min])
//                     min = l;
//                }
//                //   Put found minimum element in its place
//                ushort temp = window[j];
//                window[j] = window[min];
//                window[min] = temp;
//             }
//             //   Get result - the middle element
//             res = window[half - 1];
//             if((res<=ptr[_size][n]/1.5)&&(res>=ptr[_size][n]*1.5))
//                 pDst[n] = window[half - 1];
//          }
//    }
//}




//void drawThread::EqualizeHist(Mat& src, Mat& dst, int graylevel, int dataBit)
//{

//    //第1步：计算原始图像的像素总个数
//    int ss = src.cols * src.rows;
//    if (!src.data)
//    {
//        return;
//    }

//    //第2步：计算图像的直方图，即计算出每一取值范围内的像素值个数
//    uint* mp = new uint[graylevel];
//    memset(mp, 0, sizeof(uint) * graylevel);//初始化
//    for (size_t i = 0; i < src.rows; i++)
//    {
//        ushort* ptr = src.ptr<ushort>(i);
//        for (size_t j = 0; j < src.cols; j++)
//        {
//            int value = ptr[j];
//            mp[value]++;
//        }
//    }


//    //第3步：计算灰度分布频率+灰度累加分布频率+重新计算均衡化后的灰度值，四舍五入//第四步：计算灰度累计分布频率//第五步:重新计算均衡化后的灰度值，四舍五入。参考公式：(N-1)*T+0.5
//    double* valuePro = new double[graylevel];
//    memset(valuePro, 0, sizeof(double) * graylevel);//初始化  一定要记住

//    valuePro[0] = ((double)mp[0] / ss);
//    mp[0] = (ushort)(65535 * valuePro[0]);
//    for (size_t i = 1; i < graylevel; i++)
//    {
//        valuePro[i] = ((double)mp[i] / ss) + valuePro[i - 1] ;
//        mp[i] = (ushort)(65535 * valuePro[i]);
//    }


//    //第6步：灰度变换
//    if (dataBit == 8)
//    {
//        for (size_t i = 0; i < src.rows; i++)
//        {
//            uchar* pSrc = src.ptr<uchar>(i);
//            uchar* pDst = dst.ptr<uchar>(i);
//            for (size_t j = 0; j < src.cols; j++)
//            {
//                pDst[j] = mp[pSrc[j]];
//            }
//        }
//    }
//    else if(dataBit==16){
//        //第四步：灰度变换
//        for (size_t i = 0; i < src.rows; i++)
//        {
//            unsigned short* pSrc = src.ptr<unsigned short>(i);
//            unsigned short* pDst = dst.ptr<unsigned short>(i);
//            for (size_t j = 0; j < src.cols; j++)
//            {
//                pDst[j] = mp[pSrc[j]];
//            }
//        }
//    }
//    else
//    {

//    }
//    delete[]mp;
//    delete[]valuePro;
//}
//void drawThread::EqualizeHist_Array(Mat& src, Mat& dst, int graylevel,int dataBit)
//{

//    //第1步：计算原始图像的像素总个数
//    int ss = src.cols * src.rows;
//    if (!src.data)
//    {
//        return;
//    }

//    //第2步：计算图像的直方图，即计算出每一取值范围内的像素值个数
//    ushort *mp = new ushort[graylevel];
//    memset(mp, 0, sizeof(ushort) * graylevel);//初始化
//    for (size_t i = 0; i < src.rows; i++)
//    {
//        ushort* ptr = src.ptr<ushort>(i);
//        for (size_t j = 0; j < src.cols; j++)
//        {
//            int value = ptr[j];
//            mp[value]++;
//        }
//    }

//    //第3步：计算灰度分布频率+灰度累加分布频率+重新计算均衡化后的灰度值，四舍五入
//    double *valuePro= new double[graylevel];
//    memset(valuePro, 0, sizeof(double) * graylevel);//初始化
//    //单独处理第一个数据;
//    valuePro[0] = ((double)mp[0] / ss);
//    mp[0] = (ushort)(65535 * valuePro[0]);
//    for (size_t i = 1; i < graylevel; i++)
//    {
//        valuePro[i] = ((double)mp[i] / ss) + valuePro[i-1];
//        mp[i] = (ushort)(65535 * valuePro[i] );
//    }

//    //第4步：灰度变换
//    if (dataBit==8)
//    {
//        for (size_t i = 0; i < src.rows; i++)
//        {
//            uchar* pSrc = src.ptr<uchar>(i);
//            uchar* pDst = dst.ptr<uchar>(i);
//            for (size_t j = 0; j < src.cols; j++)
//            {
//                pDst[j] = mp[pSrc[j]];
//            }
//        }
//    }
//    else if(dataBit==16){
//        //第四步：灰度变换
//        for (size_t i = 0; i < src.rows; i++)
//        {
//            ushort* pSrc = src.ptr<ushort>(i);
//            ushort* pDst = dst.ptr<ushort>(i);
//            for (size_t j = 0; j < src.cols; j++)
//            {
//                pDst[j] = mp[pSrc[j]];
//            }
//        }
//    }
//    else
//    {

//    }
//    delete[]mp;
//    delete[]valuePro;
//}
