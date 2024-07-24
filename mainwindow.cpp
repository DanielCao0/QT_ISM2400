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

    fileData = file.readAll();
    fileSize = fileData.size();
    offset = 0;
    isTransmitting = true;  // 在开始传输时设置标志

    // 发送开始命令
    QByteArray fileNameBytes = currentFileName.toUtf8();
    // 将QByteArray转换为16进制表示
    QString hexFileName = fileNameBytes.toHex();

    QString startCommand = QString("AT+SEND=000055550000")+ hexFileName +"\r\n";
    serialPort.write(startCommand.toLocal8Bit());
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

        QString command = "AT+SEND=" + hexData + "\r\n";
        // 使用 qDebug() 打印
        //qDebug() << "Command:" << command;
        serialPort.write(command.toLocal8Bit());
        //serialPort.waitForBytesWritten(1000);
        isWaitingForConfirmation = true;
        timeoutTimer.start(TEST_TIME_OUT);
        qDebug() << "Data sent, waiting for confirmation...";
    }
}

void MainWindow::handleReadyRead() {
    QByteArray responseData = serialPort.readAll();
    ui->textEditLog->append(responseData);
    //
    QScrollBar *scrollBar = ui->textEditLog->verticalScrollBar();
    this->accumulatedData += responseData;

    //大于100字节 清空buffer
    if(responseData.size()>100)
    {
        responseData.clear();
    }

    if (accumulatedData.contains("+EVT:SEND CONFIRM OK")) {


        if(isTestRunning)
        {
            acknowledgedPackets += 1;
            rfTimer.stop();
            sendTestCmd();
        }

        QString rssi(accumulatedData);
        //提取RSSI 和 SNR
        QRegExp rx("\\+EVT:RX:([-]?\\d+):([-]?\\d+):");
        if (rx.indexIn(rssi) != -1) {
            QString rssi = rx.cap(1);
            QString snr = rx.cap(2);

            ui->rssi->setText(rssi);
            ui->snr->setText(snr);

            qDebug() << "RSSI:" << rssi << "SNR:" << snr;
        } else {
            //qDebug() << "No match found";
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
        accumulatedData.clear(); // Clear the buffer after processing    收到confirmok 就可以清空buffer了
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

    currentIndex = ui->channelBox->currentIndex();
    QString channeString = ui->channelBox->itemText(currentIndex);

    currentIndex = ui->bwBox->currentIndex();
    QString bwString = ui->bwBox->itemText(currentIndex);

    currentIndex =  ui->sf->currentIndex();
    QString sfString = ui->sf->itemText(currentIndex);

    currentIndex =  ui->crBox->currentIndex();
    //QString crString = ui->crBox->itemText(currentIndex);

    switch (bwString.toInt())
    {
    case 200: bwString = "0";break;
    case 400:bwString = "1";break;
    case 800:bwString = "2";break;
    case 1600:bwString = "3";break;
    default :bwString = "2";break;
    }

    QString confCmd = "AT+TCONF="+channeString+ ":" + sfString+ ":" + bwString +":" + QString::number(ui->crBox->currentIndex()+1) +QString("\r\n");   //
    serialPort.write(confCmd.toLocal8Bit());

}


void MainWindow::on_testButton_released()
{
    if(ui->testButton->text() == "Start Test")
    {
        ui->testButton->setText("Stop Test");

        sendTestCmd();
        //testTimer.start();
        totalPacketsSent= 0;
        acknowledgedPackets = 0;
        isTestRunning  = true;
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
    QString testCmd = "AT+SEND=00010203040506070809\r\n";
    serialPort.write(testCmd.toLocal8Bit());
    totalPacketsSent += 1;
    rfTimer.start();

    ui->lostRate->setText(QString::number(acknowledgedPackets) + "/" + QString::number(totalPacketsSent));

}


void MainWindow::testTimer_timeout()
{
    sendTestCmd();
}

void MainWindow::on_pushButtonTransmit_released()
{

}

