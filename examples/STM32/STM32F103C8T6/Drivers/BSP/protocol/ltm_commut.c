#include "protocol/ltm_commut.h"
#include "protocol/protocol.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>


/************************* LTM_Monitor 通讯协议  ***********************************/
/* 静态实例 */
static ltm_commut s_commut;

/* 内部发送：打包并调用底层发送 */
static void my_send(uint8_t data_type, uint8_t *datas, uint16_t len)
{
    if (!s_commut.init) return;
    protocol_package(data_type, (uint8_t *)datas, len);     /* 数据打包 */
    uint16_t out_len;
    const uint8_t *out = protocol_datas(&out_len);
    if (out && out_len && s_commut.send) {
        s_commut.send((uint8_t *)out, out_len);
    }
}

/* 内部格式化打印 */
static void my_printf(const char *fmt, ...)
{
    if (!s_commut.init) 	return;
    va_list args;
    va_start(args, fmt);
    char buf[80];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        my_send(Data_CMD_Text, (uint8_t *)buf, len);
    }
}

/*==============================================================================
 * 公共 API 实现
 *==============================================================================*/

void ltm_commut_init(void)
{
    protocol_init(0);                       /* 初始化协议对象 */
    memset(&s_commut, 0, sizeof(s_commut));
    s_commut.init = 1;
}

void ltm_commut_set_send(void (*send)(uint8_t *datas, uint16_t len))	
{
	if(!s_commut.init)		return;					/* 协议对象未初始化，直接返回 */
	if(!send)				return; 				/* 判空 */
	s_commut.send = send;							/* 设置底层发送接口 */
}

void ltm_commut_recv(uint8_t *datas, uint16_t len)	/* 数据接收接口：封装 protocol_recv */
{
	if(!s_commut.init)		return;					/* 协议对象未初始化，直接返回 */
	protocol_recv(datas, len);						/* 将数据写入 protocol 环形缓冲区 */
}

void ltm_commut_send(uint8_t data_type, uint8_t *datas, uint16_t len)
{
    my_send(data_type, datas, len);
}

void ltm_commut_printf(const char *fmt, ...)
{
    my_printf(fmt);
}

void ltm_commut_send_curve(uint8_t ch, float value)
{
    if(!s_commut.init) return;
    if(ch < 1 || ch > 5) return;
    uint8_t type = Data_Channel1 + ch - 1;   /* 协议层需定义 Data_Channel1~5 */
    my_send(type, (uint8_t *)&value, sizeof(value));
}

void ltm_commut_send_curves(ltm_curves * curves){
	if(!s_commut.init || !curves) return;
	uint8_t size = curves->size;
	if(size == 0 || size > 5)	return;
	my_send(Data_Channel_ALL, (uint8_t *)&(curves->values), size * 4);
}

bool ltm_commut_process(uint8_t *data_type, uint8_t* datas, uint16_t *len)
{
	uint8_t *_buf = s_commut.rx_buf;
	uint8_t _type = Data_Unknown;			//未知数据,说明接收错误或数据不完整
	uint16_t _len;

	if(!protocol_process(&_type, _buf, &_len))	return false;//未接收到完整的数据帧
	s_commut.rx_len = _len;
	s_commut.data_type = _type;

	/* 输出协议解析结果 */
	*data_type = _type;
	*len       = _len;
	memcpy(datas, _buf, _len);				/* 将数据拷贝到用户的缓冲区中 */

	return true;
}
