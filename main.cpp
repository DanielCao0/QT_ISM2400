#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    //QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);


    //设置全局字体
    QFont font("Tahoma", 9);
    a.setFont(font);

    MainWindow w;
    w.show();
    return a.exec();
}
