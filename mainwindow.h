#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

enum PacketType {
    NotStarted,
    DataPacket,  // 发送数据包
    StartPacket, // 开始包
    EndPacket    // 结束包
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void fillSerialPortInfo();
    void sendNextChunk();
    void handleReadyRead();
    void retryTransmission();
    void resetTransmissionState();
    void sendTestCmd();
    void sendEndPacket();

private slots:
    void on_pushButtonUart_released();
    void on_pushButtonFile_released();
    void on_pushButtonTransmit_clicked();
    void on_timeout();
    void on_updateTimer_timeout();

    void on_read_released();
    void on_testButton_released();

    void testTimer_timeout();

    void on_pushButtonTransmit_released();


    void on_ate_clicked();

    void on_sf_activated(const QString &arg1);

private:
    Ui::MainWindow *ui;

    QSerialPort serialPort;


    QString currentFileName; //文件名也要发送给服务器
    QByteArray fileData;      // 存储从文件中读取的数据
    int fileSize;             // 文件的大小
    int offset;               // 当前已发送数据的位置
    int currentChunkSize;     //当前发送包的大小
    uint8_t currentPacketIndex = 0;

    int retryCount = 0;       // 重试次数
    QTimer timeoutTimer;      // 超时计时器
    int timeoutValue = 100+16;// 超时计时器   默认按照SF5 BW125计算


    bool isTransmitImage = false;  // 添加此标志
    PacketType packetType = StartPacket;
    int packetsSent = 0;       // 实际发包数量
    int ackReceived = 0;       // 收到ACK的发包数量

    uint64_t imageStartTime;

    QByteArray accumulatedData;   //串口收到的内容累积

    //丢包率测试
    QTimer rfTimer;
    bool isTestRunning  = false;
    uint64_t totalPacketsSent  = 0;
    uint64_t acknowledgedPackets  = 0;
    bool isTxDone  = false;
    uint64_t testStartTime;

};

#endif // MAINWINDOW_H
