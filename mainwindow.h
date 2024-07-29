#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define TEST_TIME_OUT 50//ms    8个ms空口时间   8+10ms  linux轮询时间    //实际上50不够   100ms也是会有偶尔丢  150测试稳定 linux不是事实性的
#define CHUNK_SIZE 200

typedef struct _loraParam
{
    uint32_t frequency;
    uint8_t sf;
    uint8_t cr;
    uint8_t bw;
    uint8_t pl;
}loraParam_t;


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

private:
    Ui::MainWindow *ui;
    QSerialPort serialPort;
    QByteArray fileData;      // 存储从文件中读取的数据
    int fileSize;             // 文件的大小
    int offset;               // 当前已发送数据的位置
    bool isWaitingForConfirmation = false; // 是否正在等待确认
    int retryCount = 0;       // 重试次数
    QTimer timeoutTimer;      // 超时计时器
    int currentChunkSize;
    bool isTransmitting = false;  // 添加此标志
    bool isWaitingForStartConfirmation = false;

    QTimer updateTimer;       // 更新速率计时器
    QElapsedTimer elapsedTimer; // 计时
    int lastOffset = 0;          // 上次检查时的偏移量
    QString currentFileName; //文件名也要发送给服务器
    QByteArray accumulatedData;   //串口收到的内容累积
    loraParam_t sx1280 ;


    //丢包率测试
    QTimer rfTimer;
    bool isTestRunning  = false;
    uint64_t totalPacketsSent  = 0;
    uint64_t acknowledgedPackets  = 0;
    bool waitingTestForAck = false;
    bool isTxDone  = false;
    uint64_t testStartTime;

};

#endif // MAINWINDOW_H
