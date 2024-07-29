#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QDebug>
#include <QFileDialog>
#include <QFile>
#include <QStandardPaths>
#include <QImageReader>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QDateTime>
#include <QThread>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    fillSerialPortInfo();
    connect(&serialPort, &QSerialPort::readyRead, this, &MainWindow::handleReadyRead);
    connect(&timeoutTimer, &QTimer::timeout, this, &MainWindow::on_timeout);
    timeoutTimer.setSingleShot(false);

    ui->progressBar->setValue(0);

    // 初始化时禁用文件选择和发送按钮
    ui->pushButtonFile->setEnabled(false);
    ui->pushButtonTransmit->setEnabled(false);
    ui->testButton->setEnabled(false);
    ui->read->setEnabled(false);


    // 设置lineEditFile为只读
    ui->lineEditFile->setReadOnly(true);

    //设置日志串口的滚动条

    connect(&updateTimer, &QTimer::timeout, this, &MainWindow::on_updateTimer_timeout);
    connect(&rfTimer, &QTimer::timeout, this, &MainWindow::testTimer_timeout);

    rfTimer.setInterval(TEST_TIME_OUT);
    rfTimer.setSingleShot(true);
}


// MainWindow 析构函数
MainWindow::~MainWindow()
{
    delete ui;
}

// 填充串口信息
void MainWindow::fillSerialPortInfo() {
    auto portsInfo = QSerialPortInfo::availablePorts();
    for (const auto& info : portsInfo) {
        ui->comboBoxUart->addItem(info.portName() + " " + info.description(), info.portName());
    }
}

// 处理打开/关闭串口的按钮
void MainWindow::on_pushButtonUart_released()
{
    if (serialPort.isOpen()) {
        serialPort.close();
        ui->pushButtonUart->setText("Open Port");

        // 串口关闭时禁用文件选择和发送按钮
        ui->pushButtonFile->setEnabled(false);
        ui->pushButtonTransmit->setEnabled(false);
        ui->testButton->setEnabled(false);
        ui->read->setEnabled(false);

        ui->comboBoxUart->setEnabled(true);

        resetTransmissionState();

    } else {
        auto portName = ui->comboBoxUart->currentData().toString();
        serialPort.setPortName(portName);
        serialPort.setBaudRate(QSerialPort::Baud115200);
        serialPort.setDataBits(QSerialPort::Data8);
        serialPort.setParity(QSerialPort::NoParity);
        serialPort.setStopBits(QSerialPort::OneStop);
        serialPort.setFlowControl(QSerialPort::NoFlowControl);

        if (!serialPort.open(QIODevice::ReadWrite)) {
            QMessageBox::warning(this, "Warning", portName + " open failed: " + serialPort.errorString());
        } else {
            ui->pushButtonUart->setText("Close Port");

            // 串口打开成功时启用文件选择和发送按钮
            ui->pushButtonFile->setEnabled(true);
            ui->pushButtonTransmit->setEnabled(true);
            ui->testButton->setEnabled(true);
            ui->read->setEnabled(true);

            ui->comboBoxUart->setEnabled(false);
        }
    }
}

void MainWindow::on_pushButtonFile_released()
{
    auto filename = QFileDialog::getOpenFileName(this, "Select File",
                                                 QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                                                 "Images (*.jpg;*.png);;All Files (*.*)");
    if (!filename.isEmpty()) {
        ui->lineEditFile->setText(filename);

        //QImageReader reader(filename);
        //reader.setScaledSize(QSize(380, 230));  // 设置缩放后的目标大小
        //reader.setQuality(90);  // 如果是JPEG，可以设置读取质量
        //reader.setTransformation(QImageIOHandler::Transformation::TransformationNone);  // 无特殊变换
        //QImage image = reader.read();  // 读取图像



        // 获取文件大小信息
        QFileInfo fileInfo(filename);
        qint64 sizeInBytes = fileInfo.size();
        double sizeInKB = sizeInBytes / 1024.0;  // 转换为千字节

        // 设置 QLabel 显示文件大小
        ui->labelPictureSize->setText("size: " + QString::number(sizeInKB, 'f', 2) + " KB");

        // 使用QFileInfo来获取纯文件名
        QString fileOnlyName = fileInfo.fileName();  // 获取不包含路径的文件名
        currentFileName = fileOnlyName;   //  currentFileName 已经在类中声明
    }
}

// 发送文件按钮
void MainWindow::on_pushButtonTransmit_clicked()
{
    auto filename = ui->lineEditFile->text();
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Warning", filename + " open failed: " + file.errorString());
        return;
    }

    QString confString = QString("AT+PRECV=0\r\n");
    serialPort.write(confString.toLocal8Bit());

    confString = QString("AT+PRECV=65533\r\n");
    serialPort.write(confString.toLocal8Bit());


    fileData = file.readAll();
    fileSize = fileData.size();
    offset = 0;
    isTransmitting = true;  // 在开始传输时设置标志

    // 发送开始命令
    QByteArray fileNameBytes = currentFileName.toUtf8();
    // 将QByteArray转换为16进制表示
    QString hexFileName = fileNameBytes.toHex();

    QString startCommand = QString("AT+PSEND=000055550000")+ hexFileName +"\r\n";
    serialPort.write(startCommand.toLocal8Bit());

    //等待txdone  在启动超时

    while(!this->isTxDone )
    {
        QApplication::processEvents();
    }
    isWaitingForStartConfirmation = true;
    timeoutTimer.start(TEST_TIME_OUT);

    //速率统计
    updateTimer.start(1000);  // 每秒更新一次速率显示
    elapsedTimer.start();  // 开始计时

    ui->pushButtonTransmit->setEnabled(false);
    ui->lineEditFile->setEnabled(false);
    ui->pushButtonFile->setEnabled(false);
    ui->testButton->setEnabled(false);
    ui->read->setEnabled(false);

}

void MainWindow::sendNextChunk() {
    if (offset < fileSize && !isWaitingForConfirmation) {
        const int chunkSize = CHUNK_SIZE;
        currentChunkSize = qMin(chunkSize, fileSize - offset);  // 更新 currentChunkSize
        QByteArray chunk = fileData.mid(offset, currentChunkSize);
        QString hexData = chunk.toHex();

        QString command = "AT+PSEND=" + hexData + "\r\n";
        serialPort.write(command.toLocal8Bit());

        while(!this->isTxDone )
        {
            QApplication::processEvents();
        }

        isWaitingForConfirmation = true;
        timeoutTimer.start(TEST_TIME_OUT);
        qDebug() << "Data sent, waiting for confirmation...";
    }
}

void MainWindow::handleReadyRead() {
    QByteArray responseData = serialPort.readAll();

    //qDebug()<<"responseData"<<responseData;

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString logMessage = QString("[%1] %2").arg(timestamp).arg(QString::fromUtf8(responseData));
    ui->textEditLog->append(logMessage);

    ui->textEditLog->verticalScrollBar();

    this->accumulatedData += responseData;

    qDebug()<<QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")<<"accumulatedData"<<accumulatedData;

    QString rssi(accumulatedData);    //这个要放在前面 不然放在后面都清空了
    //提取RSSI 和 SNR
    QRegExp rx("\\+EVT:RXP2P:([-]?\\d+):([-]?\\d+)");

    if (rx.indexIn(rssi) != -1) {
        QString rssi = rx.cap(1);
        QString snr = rx.cap(2);

        ui->rssi->setText(rssi);
        ui->snr->setText(snr);

        qDebug() << "RSSI:" << rssi << "SNR:" << snr;
    } else {
        //qDebug() << "No match found";
    }


    if (accumulatedData.contains("+EVT:TXP2P DONE"))  //不区分测试模式还是图传
    {
        qDebug()<<"+EVT:TXP2P DONE";
        this->isTxDone = true;   //点可能在这  这里是true 会引起一个sendTestCmd发包
        accumulatedData.clear();
    }


    if (accumulatedData.contains("+EVT:SEND CONFIRM OK") || (accumulatedData.contains("55AA55"))) {

        accumulatedData.clear();    //这里是重点  在调用sendTestCmd之前要清一下   其实handleReadyRead 应该触发一个槽函数是最合理的  不应该直接在这里处理  这里只处理底层
        if(isTestRunning)
        {
            acknowledgedPackets += 1;

            if(isTxDone)
            {
                sendTestCmd();//但是同时也收到了 55AA55 又来了一次sendTestCmd
            }else
            {
                qDebug()<<QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")<<"无效接收ACK";
            }
        }



        if (isWaitingForStartConfirmation) {
            // 收到开始确认，开始发送数据
            isWaitingForStartConfirmation = false;
            timeoutTimer.stop();
            retryCount = 0;
            sendNextChunk();
        } else if (isTransmitting) {
            // 成功确认，继续发送下一块数据
            offset += currentChunkSize;
            isWaitingForConfirmation = false;
            timeoutTimer.stop();
            retryCount = 0;
            //accumulatedData.clear(); // Clear the buffer after processing

            // 更新进度条
            int progress = static_cast<int>((static_cast<double>(offset) / fileSize) * 100);
            ui->progressBar->setValue(progress);

            if (offset < fileSize) {
                sendNextChunk();
            } else {
                qDebug() << "All data has been sent successfully.";
                ui->progressBar->setValue(100);  // 确保进度条达到 100%

                // 发送结束命令
                QString stopCommand = "AT+SEND=FEFDFC\r\n";
                serialPort.write(stopCommand.toLocal8Bit());
                isTransmitting = false;

                QMessageBox::information(this, "Transfer Complete", "The file has been successfully sent!");
                //elapsedTimer.stop();

            }
        } else
        {
            //
        }

    }

    //大于100字节 清空buffer
    if(accumulatedData.size()>256)
    {
        accumulatedData.clear();
    }
}



void MainWindow::on_timeout() {
    if (isWaitingForConfirmation || isWaitingForStartConfirmation) {
        retryCount++;
        qDebug() << "Timeout reached, retry count: " << retryCount;

        if (retryCount <= 3) {
            isWaitingForConfirmation = false;  // 重置等待数据块确认标志
            isWaitingForStartConfirmation = false;  // 重置等待开始确认标志   //这个标志位有BUG

            if (isWaitingForStartConfirmation) {
                // 重试发送开始命令
                QString startCommand = "AT+SEND=START\r\n";   //
                serialPort.write(startCommand.toLocal8Bit());
            } else {
                // 重试发送当前数据块
                sendNextChunk();
            }
        } else {
            QMessageBox::critical(this, "Error", "Transmission failed after 3 retries.");
            retryCount = 0;  // 重置重试计数
            isWaitingForConfirmation = false;
            isWaitingForConfirmation = false;  // 重置等待开始确认标志
            timeoutTimer.stop();
        }
    }
}


void MainWindow::resetTransmissionState() {
    // 重置传输相关的变量和状态
    accumulatedData.clear(); // Also clear the accumulated data buffer
    fileData.clear();
    fileSize = 0;
    offset = 0;
    isTransmitting = false;
    isWaitingForConfirmation = false;
    retryCount = 0;
    timeoutTimer.stop();
    ui->progressBar->setValue(0);
    qDebug() << "Transmission state has been reset.";
}



void MainWindow::on_updateTimer_timeout() {
    if(offset == fileSize)  //发送完了就不需要更新速率
    {
        return ;
    }

    int currentOffset = offset; // 获取当前偏移量
    int bytesTransmitted = currentOffset - lastOffset; // 计算这一秒内传输的字节
    double timeElapsed = 1.0; // 计算时间差（秒）

    double rate = bytesTransmitted / timeElapsed; // 计算速率

    int elapsedTime = elapsedTimer.elapsed();
    QString timeText = QString::number(elapsedTime / 1000) + " seconds";  // 整数秒

    ui->labelRate->setText("Current Rate: " + QString::number(rate, 'f', 2) + " Bytes/s \t" + timeText);

    lastOffset = currentOffset; // 更新上次偏移量
    //elapsedTimer.restart(); // 重启计时器
}

void MainWindow::on_read_released()
{
    int currentIndex;
    QString confCmd;

    //    confCmd = "AT+NWM=0r\n";
    //    serialPort.write(confCmd.toLocal8Bit());
    //    QThread::msleep(1000);  // 睡眠500毫秒


    confCmd = "AT+PRECV=0\r\n";
    serialPort.write(confCmd.toLocal8Bit());

    currentIndex = ui->channelBox->currentIndex();
    QString channeString = ui->channelBox->itemText(currentIndex);
    confCmd = "AT+PFREQ="+ channeString+ "\r\n";
    serialPort.write(confCmd.toLocal8Bit());

    currentIndex = ui->bwBox->currentIndex();
    QString bwString = ui->bwBox->itemText(currentIndex);
    confCmd = "AT+PBW="+ bwString + "\r\n";
    serialPort.write(confCmd.toLocal8Bit());

    currentIndex =  ui->sf->currentIndex();
    QString sfString = ui->sf->itemText(currentIndex);
    confCmd = "AT+PSF="+ sfString + "\r\n";
    serialPort.write(confCmd.toLocal8Bit());


    confCmd = "AT+PTP=22\r\n";
    serialPort.write(confCmd.toLocal8Bit());

    if(sfString.toInt()==5||sfString.toInt()==6 )
    {
        confCmd = "AT+SYNCWORD=1424\r\n";
        serialPort.write(confCmd.toLocal8Bit());
    }else
    {
        confCmd = "AT+SYNCWORD=3444\r\n";
        serialPort.write(confCmd.toLocal8Bit());
    }


}


void MainWindow::on_testButton_released()
{
    if(ui->testButton->text() == "Start Test")
    {
        ui->testButton->setText("Stop Test");

        //        QString conf = "AT+PRECV=0\r\n";   //先退出接收模式
        //        serialPort.write(conf.toLocal8Bit());

        //conf = "AT+PRECV=65533\r\n";   //在进入接收模式
        //serialPort.write(conf.toLocal8Bit());
        isTestRunning  = true;
        sendTestCmd();

        totalPacketsSent= 0;
        acknowledgedPackets = 0;

        testStartTime = QDateTime::currentMSecsSinceEpoch();

    }
    else
    {
        ui->testButton->setText("Start Test");
        rfTimer.stop();
        isTestRunning  = false;
    }

}

void MainWindow::sendTestCmd()
{
    QString conf = "AT+PRECV=0\r\n";   //先退出接收模式
    serialPort.write(conf.toLocal8Bit());

    this->isTxDone = false;
    rfTimer.stop();
    qDebug()<<QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")<<"TX";
    int mtu = ui->lineEditFile_mtu->text().toInt();

    uint64_t maxPacket = ui->MaxPacket->text().toInt();

    if(totalPacketsSent >= maxPacket)
    {
        ui->testButton->setText("Start Test");
        rfTimer.stop();
        isTestRunning  = false;
        totalPacketsSent = 0;
        return;
    }

    QByteArray payload;
    for (int i = 0; i < mtu; i++) {
        // 添加一个字节到 QByteArray，这里我们简单地添加了循环的索引值
        payload.append(static_cast<char>(i));
    }


    QString testCmd = "AT+PSEND=" + payload.toHex().toUpper() + "\r\n";


    serialPort.write(testCmd.toLocal8Bit());
    totalPacketsSent += 1;

    // 设置超时时间（毫秒）
    int timeout = 1000; //
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsedTime = 0;

    while(!this->isTxDone && elapsedTime < timeout)
    {
        QApplication::processEvents();
        elapsedTime = QDateTime::currentMSecsSinceEpoch() - startTime;
    }
    //    if (!this->isTxDone) {

    //        // 处理超时情况，如重试或报错
    //        conf = "ATZ\r\n";   //先退出接收模式     //忘记一点  死机之后ATZ都不行了
    //        serialPort.write(conf.toLocal8Bit());
    //    }

    conf = "AT+PRECV=65535\r\n";   //开启接收模式    //收到发送中断开启接收    //收到数据会自动退出接收模式   //超时的话 我强关接收模式
    serialPort.write(conf.toLocal8Bit());

    rfTimer.start(); //这里要改一下收到+EVT:TXP2P DONE 在开始结算超时

    double lossRatePercentage = 0.0;
    if (totalPacketsSent > 0) {  // 防止除以零
        lossRatePercentage = (double)acknowledgedPackets / totalPacketsSent * 100.0;
    }

    ui->lostRate->setText(QString::number(acknowledgedPackets) + "/" +
                          QString::number(totalPacketsSent) + "\t\t" +
                          QString::number(lossRatePercentage, 'f', 2) + "%");

    double bytesPerSecond =(double)acknowledgedPackets*mtu /((QDateTime::currentMSecsSinceEpoch() - this->testStartTime)/1000.0) * 8 / 1000;
    ui->bytesPerSecond->setText("Rate: "+QString::number(bytesPerSecond,'f', 3)+" kbps"+"\t\t"+QString::number(QDateTime::currentMSecsSinceEpoch()/1000 - this->testStartTime/1000)\
                                +" s");


}


void MainWindow::testTimer_timeout()    //先捋一下问题 就是超时之后 会发送一个sendTestCmd   但是这个时候也会收到55AA55 也会触发一个sendTestCmd 就会出现连续两次的写
{
    if(isTestRunning)
    {
        qDebug()<<QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")<<"testTimer_timeout";
        sendTestCmd();
    }
}

void MainWindow::on_pushButtonTransmit_released()
{

}


//一个完整的测试流  是先发送收到ACK  或者发送超时    如果一个正在执行单次未完成 是不能进行第二次发送的
//为了放在在没有收到TX DONE 同时收到了上一包的55AA55 需要手动控制接收 比较好

//现在手动控制也有问题  就是刚要关闭RX的时候 收到包了 就是单片机在操作的射频的时候  来了一个AT中断引起的异常

//accumulatedData:[0] "+EVT:RXP2P:-7:6:55AA55\r\n"   可能是收的时候 我调用了发送（会导致单片机异常）    增加超时时间可以避免这个问题   应该是正在接收 打断了接收中断
//无效接收ACK
//accumulatedData:[0] "OK\r\n"
//accumulatedData:[0] "OK\r\n+EVT:TXP2P DONE\r\n"
//+EVT:TXP2P DONE
//testTimer_timeout
//sendTestCmd
//accumulatedData:[0] "+EVT:RXP2P:-7:6:55AA55\r\n"
//无效接收ACK




void MainWindow::on_ate_clicked()
{
    QString confCmd;
    confCmd = "ATE\r\n";
    serialPort.write(confCmd.toLocal8Bit());
}

