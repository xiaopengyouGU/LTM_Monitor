#ifndef __SERIAL__
#define __SERIAL__
#include <stdint.h>
#include <stdbool.h>
#include <QSerialPort>
#include <QSerialPortInfo>
//实际上还是进行C语言开发
#define SERIAL_BUF_SIZE     512
typedef struct{
    uint8_t port;                       //端口号
    uint8_t check;                      //奇偶校验位
    uint8_t stop;                       //停止位
    uint8_t data;                       //数据位
    uint32_t baud;                      //波特率
}serial_config_t;                       //串口配置结构体

typedef struct{
    uint8_t buf[SERIAL_BUF_SIZE];       //串口接收缓冲区   
    QSerialPort comPort;                //串口对象
}serial_obj;


void serial_init(serial_config_t *config);             
void serial_send(uint8_t* buf, uint16_t len);
uint8_t* serial_recv(uint16_t *len);
void serial_close(void);        
bool serial_open(uint8_t flag);                 //flag = 0 : 读写， 1：只读， 2：只写   

void moveSerialToThread(QThread *thread);       //将串口对象移动到工作线程中，静态对象创建时在主线程中
QSerialPort* getSerial(void);


#endif