#ifndef __DATA_DEF_H__
#define __DATA_DEF_H__

#include <QObject>

//该结构体用于记录PID调试界面信息
typedef struct{
    float Kp;
    float Ki;
    float Kd;
    float dt;       //周期值
    float target;   //目标值
    float actual;   //实际值  
}pid_data_t;


#endif