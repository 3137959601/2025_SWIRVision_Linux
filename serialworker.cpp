#include "serialworker.h"
#include <QDebug>
#include <QtGlobal>
#include <cmath>

namespace {
constexpr float kTecVoltageMin = 0.3f;
constexpr float kTecVoltageMax = 2.39f;
constexpr float kTecDacVref = 2.5f;
constexpr float kTecDacFullScale = 65535.0f;

float tecVoltageToTemp(float voltage)
{
    return 23.5f * voltage * voltage * voltage
         - 98.5f * voltage * voltage
         + 170.0f * voltage
         - 79.7f;
}
/**
 * 因为三次方程 没有简单解析反函数，所以：
 * 用 二分法（Bisection Method）
 * 每次迭代把电压区间缩小一半
 * 32 次迭代 ≈ 精度提高 2^-32
 * （远超实际需求）
 */
float tecTempToVoltage(float temp)
{
    float lo = kTecVoltageMin;
    float hi = kTecVoltageMax;
    const float minTemp = tecVoltageToTemp(lo);
    const float maxTemp = tecVoltageToTemp(hi);
    const float target = qBound(minTemp, temp, maxTemp);

    for (int i = 0; i < 32; ++i) {
        const float mid = (lo + hi) * 0.5f;
        if (tecVoltageToTemp(mid) < target)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) * 0.5f;
}

unsigned short tecVoltageToDacCode(float voltage)
{
    const float limited = qBound(kTecVoltageMin, voltage, kTecVoltageMax);
    const int code = qRound(limited * kTecDacFullScale / kTecDacVref);
    return static_cast<unsigned short>(qBound(0, code, 65535));
}
}

bool serial_bind_flag = false;
//QByteArray baRcvData;
SerialWorker::SerialWorker(QObject *parent)
    : QObject{parent}
{

}

SerialWorker::~SerialWorker()
{
    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
    if (serialWorker) {
        serialWorker->close();
        delete serialWorker;
        serialWorker = nullptr;
    }

}

void SerialWorker::SerialPortInit(QString com_name)
{
    if (serialWorker) {
        SerialClose();
    }
    serialWorker = new QSerialPort;
    timer = new QTimer;

    QSerialPort::BaudRate baudRate;
    QSerialPort::DataBits dataBits;
    QSerialPort::StopBits stopBits;
    QSerialPort::Parity checkBits;

    baudRate = QSerialPort::Baud115200;
    dataBits = QSerialPort::Data8;
    stopBits = QSerialPort::OneStop;
    checkBits = QSerialPort::NoParity;

//    if(ui->baundrateCb->currentText()=="4800"){
//        baudRate = QSerialPort::Baud4800;
//    }else if(ui->baundrateCb->currentText()=="9600"){
//        baudRate = QSerialPort::Baud9600;
//    }else if(ui->baundrateCb->currentText()=="115200"){
//        baudRate = QSerialPort::Baud115200;
//    }

//    if(ui->dataCb->currentText()=="8"){
//        dataBits = QSerialPort::Data8;
//    }else if(ui->dataCb->currentText()=="7"){
//        dataBits = QSerialPort::Data7;
//    }else if(ui->dataCb->currentText()=="6"){
//        dataBits = QSerialPort::Data6;
//    }else if(ui->dataCb->currentText()=="5"){
//        dataBits = QSerialPort::Data5;
//    }

//    if(ui->stopCb->currentText()=="1"){
//        stopBits = QSerialPort::OneStop;
//    }else if(ui->stopCb->currentText()=="1.5"){
//        stopBits = QSerialPort::OneAndHalfStop;
//    }else if(ui->stopCb->currentText()=="2"){
//        stopBits = QSerialPort::TwoStop;
//    }

//    if(ui->checkCb->currentText()=="none"){
//        checkBits = QSerialPort::NoParity;
//    }

    serialWorker->setPortName(com_name);
    serialWorker->setBaudRate(baudRate);
    serialWorker->setDataBits(dataBits);
    serialWorker->setStopBits(stopBits);
    serialWorker->setParity(checkBits);

    connect(serialWorker,&QSerialPort::readyRead,this,&SerialWorker::SerialPortReadyRead_Slot);
    connect(timer, &QTimer::timeout, this, &SerialWorker::timeUpdate);
    timer->setInterval(50);

    if(serialWorker->open(QIODevice::ReadWrite)==true)
    {
        serial_bind_flag = true;
        //qDebug()<<"串口打开成功";
    }else{
        serial_bind_flag = false;
        //qDebug()<<"串口打开失败";
    }
    //qDebug()<<"serial_bind_flag"<<serial_bind_flag;

}

void SerialWorker::SerialOpen()
{

}

void SerialWorker::SerialClose()
{
    if (serialWorker) {
        serialWorker->close();
        delete serialWorker; // 释放内存
        serialWorker = nullptr; // 防止悬空指针
        serial_bind_flag = false;
    }
    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
    //qDebug()<<"serial_bind_flag"<<serial_bind_flag;
}


void SerialWorker::ADInstructionCode(QList<float> ADSetVals)
{
    //先放大1000倍然后转换十六进制
    QString hexValues;
    for(int i=0;i<ADSetVals.size();i++)
    {
        float value = ADSetVals[i]*1000;
        //qDebug()<<"value"<<value;
        // 转换为整数,需要两字节存储
        unsigned short usValue = static_cast<unsigned short>(static_cast<int>(value));

        unsigned char lowByte = static_cast<unsigned char>(usValue&0xFF);
        unsigned char highByte = static_cast<unsigned char>((usValue>>8)&0xFF);
        // 生成十六进制字符串
        QString lowHexString = QString::number(lowByte, 16).toUpper().rightJustified(2, '0'); // 确保是2位
        QString highHexString = QString::number(highByte, 16).toUpper().rightJustified(2, '0'); // 确保是2位

        //然后插入帧头，长度，校验码，帧尾

        // 计算长度（长度始终为4个字节）
        QString lengthString = QString::number(4, 16).toUpper().rightJustified(2, '0'); // 确保长度是2位
        //计算异或校验
        QString sequenceNumber;
        unsigned char checksum = 0; // 存储校验和
        checksum ^= 0x04; // 进行异或运算
        checksum ^= 0xAD; // 进行异或运算

        if(i<5)
        {
            sequenceNumber = QString("0%1").arg(i+1, 1, 16, QLatin1Char('0')).toUpper(); // 序列号，单字节，填充0
            checksum ^= static_cast<unsigned char>(0x01 + i); // 将 0x(0i+1) 纳入异或计算

        }
        else if(i ==5)
        {
            //sequenceNumber =QString("30");
            sequenceNumber = QString("3%1").arg(0, 1, 16, QLatin1Char('0')).toUpper(); // 序列号，单字节，填充3
            checksum ^= static_cast<unsigned char>(0x30); // 将 0x(0i+1) 纳入异或计算
        }
        else if((i>5)&&(i<21))
        {
            sequenceNumber = QString("5%1").arg(i-5, 1, 16, QLatin1Char('0')).toUpper(); // 序列号，单字节，填充5
            checksum ^= static_cast<unsigned char>(0x50+i-5); // 将 0x51~0x5f 纳入异或计算
        }
        else if((i>=21)&&(i<ADSetVals.size()))
        {
            sequenceNumber = QString("6%1").arg(i-20, 1, 16, QLatin1Char('0')).toUpper(); // 序列号，单字节，填充6
            checksum ^= static_cast<unsigned char>(0x60+i-20); // 将 0x61~65 纳入异或计算
        }
        checksum ^= highByte; // 进行异或运算
        checksum ^= lowByte;
        //qDebug() << "checksum" << checksum;
        // 插入帧头和其它信息
        QString framedHexString = "EA" + lengthString + "AD" + sequenceNumber + highHexString + lowHexString + QString::number(checksum, 16).toUpper().rightJustified(2, '0') +"0A";

        // 将结果添加到 hexValues 列表
        hexValues.append(framedHexString);

    }
    //最后发送给串口输出槽函数
    emit AD_instruction_signal(hexValues);

//    qDebug() << "Hex Values:" << hexValues;

}
//指令编码函数，将所以指令将所有AD的数据放入一条指令中发送
//*********************注：区别于BJUT上位机，InstructionCode函数全部重写
//指令格式
/*帧头   +   设备码   +   指令码   +   指令内容   +   校验   +   帧尾
EA			01			XX			XXXXXX   	XX+XXXXXX    0A
56-63	   48-55	   40-47        16-39        8-15       0-7
                                32-39 24-31 16-23
*/
void SerialWorker::InstructionCode(unsigned char flag,QList<float> SetVals)
{
    //计算异或校验
    unsigned char checksum = 0; // 存储校验和
    QString dataString;
    float value = 0.0;

    unsigned short usValue;
    unsigned char lowByte;      //第3个指令内容
    unsigned char highByte;     //第2个指令内容
    unsigned char firstByte;    //第1个指令内容，一般用于开关
    QString lowHexString;
    QString highHexString;
    QString firstHexString;
    unsigned char deviceID = 0x01;    //设备码
//    unsigned char length = 4;   //指令长度固定为4Byte，但是并没有使用到
    for(int i=0;i<SetVals.size();i++)
    {
        switch(flag)
        {
        case 0x01:  //本底亮场/暗场
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);

            break;
        case 0x02:  //两点(开、G、O、关）
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);

            break;
        case 0x03:  //盲元开关
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);

            break;
        case 0x04:  //增强开关
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);

            break;
        case 0x05:  //积分时间
            value = SetVals[i]*100; //SetVals[i]单位为us，转换为毫秒，设置上限为20.47ms
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));

            lowByte = static_cast<unsigned char>(usValue&0xFF);
            highByte = static_cast<unsigned char>((usValue>>8)&0xFF);
            firstByte = 0xFF;

            break;
        case 0x06:  //com电压 VREF = 2.5V->0xFFF。

            value = SetVals[i]*4095/2.5;
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));

            lowByte = static_cast<unsigned char>(usValue&0xFF);
            highByte = static_cast<unsigned char>((usValue>>8)&0xFF);
            firstByte = 0xFF;
            break;
        case 0x09:  //翻转开关
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        case 0x0B:  //固化配置
            lowByte = 0x00;
            highByte = 0x00;
            firstByte = 0xff;
            break;
        case 0x0C:  //固化两点
            lowByte = 0x00;
            highByte = 0x00;
            firstByte = 0xff;
            break;
        case 0x0D:  //读取两点及配置
            lowByte = 0x00;
            highByte = 0x00;
            firstByte = 0xff;
            break;
        case 0x0E:  //线性
            lowByte = 0x00;
            highByte = 0x02;    //为什么这里设置为0x02
            firstByte = 0xff;
            break;
        case 0x1F:  //TEC EN
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        case 0x0F:
            value = tecTempToVoltage(SetVals[i]);
            usValue = tecVoltageToDacCode(value);

            lowByte = static_cast<unsigned char>(usValue&0xFF);
            highByte = static_cast<unsigned char>((usValue>>8)&0xFF);
            firstByte = 0xFF;
            break;
        case 0x10:  //帧间滤波
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        case 0x11:  //中值滤波
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        case 0x12:  //旋转
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        case 0x13:  //自动曝光
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        //case 0x14://测速指令，未显示在上位机中
        case 0x15:  //探测器增益
            lowByte = 0x00;
            highByte = 0x00;
            value = SetVals[i];
            // 转换为整数,为了与后面统一，使用两字节存储
            usValue = static_cast<unsigned short>(static_cast<int>(value));
            firstByte = static_cast<unsigned char>(usValue&0xFF);
            break;
        default:
            qDebug() << "指令发送标志无法识别";
            break;
        }

    }
    // 生成十六进制字符串
    lowHexString = QString::number(lowByte, 16).toUpper().rightJustified(2, '0'); // 确保是2位
    highHexString = QString::number(highByte, 16).toUpper().rightJustified(2, '0'); // 确保是2位
    firstHexString = QString::number(firstByte, 16).toUpper().rightJustified(2, '0'); // 确保是2位

    checksum += firstByte; // 进行累加和运算
    checksum += highByte;
    checksum += lowByte;
    dataString = dataString + firstHexString + highHexString + lowHexString;

    QString framedHexString;
    QString deviceIDString;
    QString flagString;
    deviceIDString = QString::number(deviceID, 16).toUpper().rightJustified(2, '0'); // 确保长度是2位
    flagString = QString::number(flag, 16).toUpper().rightJustified(2, '0'); // 确保长度是2位
//    checksum += deviceID; // 设备码
    checksum += flag; // 进行累加和运算
    // 插入帧头和其它信息
    framedHexString = "EA" + deviceIDString + flagString + dataString + QString::number(checksum, 16).toUpper().rightJustified(2, '0') +"0A";
    //qDebug() << "checksum" << checksum;
    //最后发送给串口输出槽函数
    emit instruction_send_signal(framedHexString);
//    qDebug() << "deviceIDString" << deviceIDString;
//    qDebug() << "flagString" << flagString;
//    qDebug() << "dataString" << dataString;
    qDebug() << "串口输出指令framedHexString:" << framedHexString;
}


void SerialWorker::SerialSendData_Slot(QString buf)
{
    QByteArray senddata;
    string2Hex(buf,senddata);
    if(serial_bind_flag == true)
        //serialWorker->write(buf.toLocal8Bit().data());
        serialWorker->write(senddata);
    //qDebug()<<"开启sendSlot线程"<<QThread::currentThreadId();//查看槽函数在哪个线程运行
}

//串口解析，包括校验指令是否传输正确，以及将正确的指令解析处理发送到主窗口中
//同样，不同于BJUT上位机，进行了个性化修改
void SerialWorker::SerialAnalyse(QByteArray &recvdata)
{
    //将strList转换为std::vector<unsigned char>后进行数据传递，这样使用的时候不需要每次用到就进行数据转换进行转换，时间更快些
    // 开始计时
    //auto start = std::chrono::high_resolution_clock::now();
    const int minFrameLen = 8;
    const int pollingLen   = 18;
    const int pollingExtLen = 20;  // 含锐度的轮询响应
    QString str = recvdata.toHex(' ').toUpper().append(' ');
    QStringList strList = str.split(" ");
    strList.pop_back(); //移除最后一个元素，即空格

    std::vector<unsigned char> byteArray(strList.size());
    bool ok;
    for (int i = 0; i < strList.size(); ++i) {
        byteArray[i] = strList[i].toInt(&ok,16);
    }
    // 打印每个字节
//    for (size_t i = 0; i < byteArray.size(); ++i) {
//        qDebug() << "byteArray[" << i << "]" << QString::number(byteArray[i],16).toUpper();
//    }
    // 工具：从指定位置寻找下一个帧头 0xEA
    auto find_next_header = [&](size_t from) {
        return std::find(byteArray.begin() + static_cast<long>(from), byteArray.end(), 0xEA);
    };
    auto isValidFrame = [&](int len) -> bool {
        if (byteArray.size() < static_cast<size_t>(len)) return false;
        if (byteArray[len - 1] != 0x0A) return false;
        if (byteArray[1] != 0x01) return false;
        unsigned int sum = 0;
        for (int i = 2; i <= len - 3; ++i) sum += byteArray[i];
        const unsigned char calcSum = static_cast<unsigned char>(sum & 0xFF);
        return calcSum == byteArray[len - 2];
    };

    while(true)
    {

        // 检查帧头和帧尾
        if (byteArray.empty() ) {
            break;
        }

        if (byteArray[0] != 0xEA ) {
            qDebug()<<"帧头不匹配";
            auto it =  find_next_header(0);
            if(it!=byteArray.end())
            {
                byteArray.erase(byteArray.begin(), it);
                continue;
            }
            else
            {
                break;
            }
        }
//        int length = byteArray[1]; // 使用 unsigned char 类型
        // Not enough data for a minimal frame yet
        if (byteArray.size() < static_cast<size_t>(minFrameLen)) break;

        // 根据指令码（byteArray[2]）确定预期帧长度
        int frameLength = 0;
        if (byteArray.size() >= 3) {
            const unsigned char cmd = byteArray[2];
            if (cmd == 0xAA) {
                // 轮询响应: 先尝试20字节(含锐度)，再尝试18字节
                if (byteArray.size() >= static_cast<size_t>(pollingExtLen) && isValidFrame(pollingExtLen))
                    frameLength = pollingExtLen;
                else if (byteArray.size() >= static_cast<size_t>(pollingLen) && isValidFrame(pollingLen))
                    frameLength = pollingLen;
            } else {
                // 标准8字节帧 (0x31锐度返回, 0xA0/0xA1/0xA2/0xA3等)
                if (isValidFrame(minFrameLen))
                    frameLength = minFrameLen;
            }
        }

        if (frameLength == 0) {
            if (byteArray.size() >= 2 && byteArray[1] != 0x01) {
                qDebug() << "设备码不匹配:" << QString::number(byteArray[1], 16).toUpper();
            }
            auto it = find_next_header(1);
            if (it != byteArray.end()) {
                byteArray.erase(byteArray.begin(), it);
                continue;
            } else {
                break;
            }
        }
        // 提取内容： [指令码, 数据...] -> 交给 InstructionAnalyse()
        std::vector<unsigned char> content(byteArray.begin() + 2, byteArray.begin() + frameLength - 2);
        InstructionAnalyse(content);

        // 丢弃已解析完的一帧，继续解析后续数据
        byteArray.erase(byteArray.begin(), byteArray.begin() + frameLength);
    }

    recvdata.clear();
    recvdata.reserve(static_cast<int>(byteArray.size()));
    for (const auto b : byteArray) {
        recvdata.append(static_cast<char>(b));
    }

    // 结束计时
//    auto end = std::chrono::high_resolution_clock::now();
//    std::chrono::duration<double> elapsed = end - start;

    // 打印运行时间
//    qDebug() << "代码执行时间：" << elapsed.count() << "秒";

}
//异或校验
bool SerialWorker::XorCorrect(const std::vector<unsigned char> &byteArray)
{
    // 计算异或校验
    unsigned char checksum = 0; // 存储校验和
    for (size_t i = 1; i < byteArray.size() - 2; ++i) { // 从长度字节开始到倒数第二个字节
        checksum ^= byteArray[i]; // 进行异或运算
    }
//    qDebug() << "checksum" << checksum;
    // 验证校验和
    return checksum == byteArray[byteArray.size() - 2];
}

//接收指令解析
//void SerialWorker::InstructionAnalyse(const std::vector<unsigned char> &content)
//{
//    if (content.size() < 4) return; // 保护：新协议至少 4 字节
//    const unsigned char cmd = content[0];
//    const unsigned char d1  = content[1];
//    const unsigned char d2  = content[2];
//    const unsigned char d3  = content[3];

//    if(content[0] == 0xA0 )//自动积分时间
//    {
//        const uint16_t code = static_cast<uint16_t>(static_cast<uint16_t>(d2) << 8 | d3);
//        const float time_ms = static_cast<float>(code) / 100.0f;   // data*0.001ms
//        emit Int_LCDNumShow(time_ms);
//        return;
//    }
//    // 2) 板间温度（芯片原始 16bit）：CMD = 0xA1
//    // 说明：最高位为符号位（0 正 / 1 负），温度值使用 data[10:0]，单位 0.0625℃
//    // 若为负温：temp = -((~data[10:0] + 1) * 0.0625)
//    else if(content[0] == 0xA1)   //板间温度
//    {
//        const uint16_t raw16   = static_cast<uint16_t>(static_cast<uint16_t>(d2) << 8 | d3);
//        const bool      isNeg  = (raw16 & 0x8000) != 0;
//        const uint16_t  bits11 = static_cast<uint16_t>(raw16 & 0x07FF); // 取 data[10:0]

//        uint16_t mag11 = bits11;
//        if (isNeg) {
//            mag11 = static_cast<uint16_t>((~bits11) & 0x07FF); // 11 位求反
//            mag11 = static_cast<uint16_t>(mag11 + 1);          // +1 得到补码幅值
//        }

//        float tempC = static_cast<float>(mag11) * 0.0625f;
//        if (isNeg) tempC = -tempC;

//        emit BoardTemp_LCDNumShow(tempC);
//        return;
//    }
//    else if(content[0] == 0xA2) //TEC制冷温度
//    {

//    }
//    else if(content[0] == 0xA3)
//    {

//    }
//}
void SerialWorker::InstructionAnalyse(const std::vector<unsigned char> &content)
{
    if (content.size() < 4) return; // 保护：新协议至少 14 字节，兼容旧协议固定4字节长度

    if(content[0] == 0xAA){ //定时轮询指令
        if (content.size() < 14) return;
        //自动积分时间
        const uint16_t code = static_cast<uint16_t>(static_cast<uint16_t>(content[2]) << 8 | content[3]);
        const float time_ms = static_cast<float>(code) / 100.0f;   // data*0.01ms
        emit Int_LCDNumShow(time_ms);
        //板间温度
        // 说明：最高位为符号位（0 正 / 1 负），温度值使用 data[10:0]，单位 0.0625℃
        // 若为负温：temp = -((~data[10:0] + 1) * 0.0625)
        const uint16_t raw16   = static_cast<uint16_t>(static_cast<uint16_t>(content[4]) << 8 | content[5]);
        const bool      isNeg  = (raw16 & 0x8000) != 0;
        const uint16_t  bits11 = static_cast<uint16_t>(raw16 & 0x07FF); // 取 data[10:0]

        uint16_t mag11 = bits11;
        if (isNeg) {
            mag11 = static_cast<uint16_t>((~bits11) & 0x07FF); // 11 位求反
            mag11 = static_cast<uint16_t>(mag11 + 1);          // +1 得到补码幅值
        }

        float tempC = static_cast<float>(mag11) * 0.0625f;
        if (isNeg) tempC = -tempC;

        emit BoardTemp_LCDNumShow(tempC);
        //TEC
        const uint16_t ITEC = static_cast<uint16_t>(static_cast<uint16_t>(content[6]) << 8 | content[7]);
        const float ITEC_A = static_cast<float>(ITEC) / 1000.0f;   // data*0.001
        const uint16_t VTEC = static_cast<uint16_t>(static_cast<uint16_t>(content[8]) << 8 | content[9]);
        const float VTEC_V = static_cast<float>(VTEC) / 1000.0f;   // data*0.001
        const uint16_t TMPACTUAL = static_cast<uint16_t>(static_cast<uint16_t>(content[10]) << 8 | content[11]);
        const float TMPACTUAL_value = tecVoltageToTemp(static_cast<float>(TMPACTUAL) / 1000.0f);
        const uint16_t TMPSET = static_cast<uint16_t>(static_cast<uint16_t>(content[12]) << 8 | content[13]);
        const float TMPSET_value = tecVoltageToTemp(static_cast<float>(TMPSET) / 1000.0f);
        std::vector<float>temp;
        temp.push_back(ITEC_A);
        temp.push_back(VTEC_V);
        temp.push_back(TMPACTUAL_value);
        temp.push_back(TMPSET_value);
        emit TECTemp_LCDNumShow(temp);
        if (content.size() >= 16) {
            const uint16_t sharpness = static_cast<uint16_t>(static_cast<uint16_t>(content[14]) << 8 | content[15]);
            emit Sharpness_LCDNumShow(static_cast<int>(sharpness));
        }
        return;

    }
    else if(content[0] == 0xA0 )//自动积分时间
    {
        const uint16_t code = static_cast<uint16_t>(static_cast<uint16_t>(content[2]) << 8 | content[3]);
        const float time_ms = static_cast<float>(code) / 100.0f;   // data*0.001ms
        emit Int_LCDNumShow(time_ms);
        return;
    }
    // 2) 板间温度（芯片原始 16bit）：CMD = 0xA1
    // 说明：最高位为符号位（0 正 / 1 负），温度值使用 data[10:0]，单位 0.0625℃
    // 若为负温：temp = -((~data[10:0] + 1) * 0.0625)
    else if(content[0] == 0xA1)   //板间温度
    {
        const uint16_t raw16   = static_cast<uint16_t>(static_cast<uint16_t>(content[2]) << 8 | content[3]);
        const bool      isNeg  = (raw16 & 0x8000) != 0;
        const uint16_t  bits11 = static_cast<uint16_t>(raw16 & 0x07FF); // 取 data[10:0]

        uint16_t mag11 = bits11;
        if (isNeg) {
            mag11 = static_cast<uint16_t>((~bits11) & 0x07FF); // 11 位求反
            mag11 = static_cast<uint16_t>(mag11 + 1);          // +1 得到补码幅值
        }

        float tempC = static_cast<float>(mag11) * 0.0625f;
        if (isNeg) tempC = -tempC;

        emit BoardTemp_LCDNumShow(tempC);
        return;
    }
    else if(content[0] == 0xA2) //TEC制冷温度
    {

    }
    else if(content[0] == 0xA3)
    {

    }
    else if (content[0] == 0x31) // sharpness standalone return
    {
        const uint16_t sharpness = static_cast<uint16_t>(static_cast<uint16_t>(content[2]) << 8 | content[3]);
        emit Sharpness_LCDNumShow(static_cast<int>(sharpness));
        return;
    }
}
void SerialWorker::SerialPortReadyRead_Slot()
{
    if (!serialWorker || !timer) return;

    baRcvData.append(serialWorker->readAll());
    SerialAnalyse(baRcvData);

//    qDebug()<<baRcvData;
//    QByteArray data = serialWorker->readAll();
//    // 解析接收到的数据
//    SerialAnalyse(data);
//    QString str = data.toHex(' ').toUpper().append(' ');//十六进制显示
//    emit recvDataSignal(str);

    //qDebug()<<"开启recvSlot线程"<<QThread::currentThreadId();//查看槽函数在哪个线程运行
}

void SerialWorker::timeUpdate()
{
    timer->stop();
    if (baRcvData.length() != 0) {
        SerialAnalyse(baRcvData);
        QString str = baRcvData.toHex(' ').toUpper().append(' ');
        emit recvDataSignal(str);
        // 清除超过1KB的无法解析的残留数据，防止内存无限增长
        if (baRcvData.length() > 1024) {
            baRcvData.clear();
        }
    }
}

void SerialWorker::string2Hex(QString str, QByteArray &senddata)
{
    int hexdata, lowhexdata;
    int hexdatalen = 0;
    int len = str.length();
    senddata.resize(len / 2);
    char lstr, hstr;
    for (int i = 0; i < len;) {
        // char lstr,
        // hstr=str[i].toAscii();//qt4

        hstr = str[i].toLatin1();  // qt5
        if (hstr == ' ') {
            i++;
            continue;
        }
        i++;
        if (i >= len)
            break;
        // lstr = str[i].toAscii();//qt4
        lstr = str[i].toLatin1();  // qt5
        hexdata = this->hex2Char(hstr);
        lowhexdata = this->hex2Char(lstr);
        if ((hexdata == 16) || (lowhexdata == 16))
            break;
        else
            hexdata = hexdata * 16 + lowhexdata;
        i++;
        senddata[hexdatalen] = (char)hexdata;
        hexdatalen++;
    }
    senddata.resize(hexdatalen);
}

char SerialWorker::hex2Char(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
        return ch - 0x30;
    else if ((ch >= 'A') && (ch <= 'F'))
        return ch - 'A' + 10;
    else if ((ch >= 'a') && (ch <= 'f'))
        return ch - 'a' + 10;
    else
        return (-1);
}

