#include "mainwindow.h"
#include <QApplication>
#include "windows.h"

int main(int argc, char *argv[])
{  
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // 修改VScode的字体即可显示中文
    //qDebug() << "你好，吕工";
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
