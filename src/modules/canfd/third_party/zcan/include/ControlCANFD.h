#ifndef CONTROLCANFD_H
#define CONTROLCANFD_H

#include"config.h"

#define UINT    unsigned int
#define BYTE    unsigned char
#define USHORT  unsigned short
#define UCHAR   unsigned char
#define UINT64  unsigned long long
#define CHAR    char
#define DWORD   unsigned int
#define ULONG   unsigned long
#define PVOID   void *
#define INT     int

//接口卡类型定义
#define ZCAN_USBCANFD_200U			41
#define VCI_USBCAN_E_U 				20
#define VCI_USBCAN_2E_U 			21

//函数调用返回状态值
#define STATUS_ERR					0
#define	STATUS_OK					1
#define STATUS_ONLINE               2
#define	STATUS_OFFLINE              3
#define STATUS_UNSUPPORTED          4

#define INVALID_CHANNEL_HANDLE 		0
#define INVALID_DEVICE_HANDLE 		0
	
/* special address description flags for the MAKE_CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */ 
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error message frame */
#define CAN_ID_FLAG  0x1FFFFFFFU /* id */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */
// make id
#define MAKE_CAN_ID(id, eff, rtr, err) (id | (!!(eff) << 31) | (!!(rtr) << 30) | (!!(err) << 29))
#define IS_EFF(id) (!!(id & CAN_EFF_FLAG)) //1:extend frame 0:standard frame
#define IS_RTR(id) (!!(id & CAN_RTR_FLAG)) //1:remote frame 0:data frame
#define IS_ERR(id) (!!(id & CAN_ERR_FLAG)) //1:error frame 0:normal frame
#define GET_ID(id) (id & CAN_ID_FLAG)


#define CAN_MAX_DLEN		8
#define CANFD_MAX_DLEN		64
typedef struct {
	UINT	can_id;  /* 32 bit MAKE_CAN_ID + EFF/RTR/ERR flags */
	BYTE    can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
	BYTE    __pad;   /* padding */
	BYTE    __res0;  /* reserved / padding */ 
	BYTE    __res1;  /* reserved / padding */
	BYTE    data[CAN_MAX_DLEN]/* __attribute__((aligned(8)))*/;
}can_frame;

typedef struct {
	UINT    can_id;  /* 32 bit MAKE_CAN_ID + EFF/RTR/ERR flags */
	BYTE    len;     /* frame payload length in byte */
	BYTE    flags;   /* additional flags for CAN FD,i.e error code */
	BYTE    __res0;  /* reserved / padding */
	BYTE    __res1;  /* reserved / padding */
	BYTE    data[CANFD_MAX_DLEN]/* __attribute__((aligned(8)))*/;
}canfd_frame;


#define TYPE_CAN   0
#define TYPE_CANFD 1
typedef struct tagZCAN_DEVICE_INFO {
    USHORT hw_Version;
    USHORT fw_Version;
    USHORT dr_Version;
    USHORT in_Version;
    USHORT irq_Num;
    BYTE   can_Num;
    UCHAR  str_Serial_Num[20];
    UCHAR  str_hw_Type[40];
    USHORT reserved[4];
}ZCAN_DEVICE_INFO;

typedef struct tagZCAN_CHANNEL_INIT_CONFIG {
    UINT can_type; //type:TYPE_CAN(0) TYPE_CANFD(1)
    union
    {
        struct
        {
            UINT  acc_code;
            UINT  acc_mask;
            UINT  reserved;
            BYTE  filter;
            BYTE  timing0;
            BYTE  timing1;
            BYTE  mode;
        }can;
        struct
        {
            UINT   acc_code;
            UINT   acc_mask;
            UINT   abit_timing;
            UINT   dbit_timing;
            UINT   brp;
            BYTE   filter;
            BYTE   mode;
            USHORT pad;
            UINT   reserved;
        }canfd;
    };
}ZCAN_CHANNEL_INIT_CONFIG;



typedef struct tagZCAN_Transmit_Data
{
    can_frame frame;
    UINT transmit_type;
}ZCAN_Transmit_Data;

typedef struct tagZCAN_Receive_Data
{
    can_frame frame;
    UINT64    timestamp;//us
}ZCAN_Receive_Data;

typedef struct tagZCAN_TransmitFD_Data
{
    canfd_frame frame;
    UINT transmit_type;
}ZCAN_TransmitFD_Data;

typedef struct tagZCAN_ReceiveFD_Data
{
    canfd_frame frame;
    UINT64      timestamp;//us
}ZCAN_ReceiveFD_Data;


//********************以下内容与ControlCAN.h相同************************************************************//

/*------------兼容ZLG的函数及数据类型---------------------------------*/

//1.ZLG系列CAN接口卡信息的数据类型。
typedef  struct  _VCI_BOARD_INFO{
		USHORT	hw_Version;
		USHORT	fw_Version;
		USHORT	dr_Version;
		USHORT	in_Version;
		USHORT	irq_Num;
		BYTE	can_Num;
        CHAR	str_Serial_Num[20];
		CHAR	str_hw_Type[40];
		USHORT	Reserved[4];	
} VCI_BOARD_INFO,*PVCI_BOARD_INFO; //共63个字节

//2.定义CAN信息帧的数据类型。           现在用到。
typedef  struct  _VCI_CAN_OBJ{         //32字节。

	UINT	ID;
	UINT	TimeStamp;//保留不用       32位，四字节。
	BYTE	TimeFlag;//保留不用
	BYTE	SendType;//保留不用
	BYTE	RemoteFlag;//是否是远程帧
	BYTE	ExternFlag;//是否是扩展帧
	BYTE	DataLen;
	BYTE	Data[8];
	BYTE	Reserved[3];//Reserved[0]~kCanIndex(for CAN receive)
}VCI_CAN_OBJ,*PVCI_CAN_OBJ;

//3.定义CAN控制器状态的数据类型。
typedef struct _VCI_CAN_STATUS{
	UCHAR	ErrInterrupt;
	UCHAR	regMode;
	UCHAR	regStatus;
	UCHAR	regALCapture;
	UCHAR	regECCapture;
	UCHAR	regEWLimit;
	UCHAR	regRECounter;
	UCHAR	regTECounter;
	DWORD	Reserved;
}VCI_CAN_STATUS,*PVCI_CAN_STATUS;

//4.定义错误信息的数据类型。
typedef struct _ERR_INFO{
		UINT	ErrCode;
		BYTE	Passive_ErrData[3];
		BYTE	ArLost_ErrData;
} VCI_ERR_INFO,*PVCI_ERR_INFO;

//5.定义初始化CAN的数据类型
typedef struct _INIT_CONFIG{
	DWORD	AccCode;
	DWORD	AccMask;
	DWORD	Reserved;
	UCHAR	Filter;//0,1接收所有帧。2标准帧滤波，3是扩展帧滤波。
	UCHAR	Timing0;	
	UCHAR	Timing1;	
	UCHAR	Mode; //模式，0表示正常模式，1表示只听模式,2自测模式
}VCI_INIT_CONFIG,*PVCI_INIT_CONFIG;

/*
///////// new add struct for filter /////////
typedef struct _VCI_FILTER_RECORD{
	DWORD ExtFrame;	//是否为扩展帧
	DWORD Start;
	DWORD End;
}VCI_FILTER_RECORD,*PVCI_FILTER_RECORD;
*/

#define FUNC_CALL __stdcall
typedef void * DEVICE_HANDLE;
typedef void * CHANNEL_HANDLE;

#ifdef __cplusplus
extern "C"
{
#endif
DEVICE_HANDLE FUNC_CALL ZCAN_OpenDevice(UINT device_type, UINT device_index, UINT reserved);
UINT FUNC_CALL ZCAN_CloseDevice(DEVICE_HANDLE device_handle);
UINT FUNC_CALL ZCAN_GetDeviceInf(DEVICE_HANDLE device_handle, ZCAN_DEVICE_INFO* pInfo);
UINT FUNC_CALL ZCAN_IsDeviceOnLine(DEVICE_HANDLE device_handle);
CHANNEL_HANDLE FUNC_CALL ZCAN_InitCAN(DEVICE_HANDLE device_handle, UINT can_index, ZCAN_CHANNEL_INIT_CONFIG* pInitConfig);
UINT FUNC_CALL ZCAN_StartCAN(CHANNEL_HANDLE channel_handle);
UINT FUNC_CALL ZCAN_ResetCAN(CHANNEL_HANDLE channel_handle);
UINT FUNC_CALL ZCAN_ClearBuffer(CHANNEL_HANDLE channel_handle);
UINT FUNC_CALL ZCAN_GetReceiveNum(CHANNEL_HANDLE channel_handle, BYTE type);//type:TYPE_CAN TYPE_CANFD
UINT FUNC_CALL ZCAN_Transmit(CHANNEL_HANDLE channel_handle, ZCAN_Transmit_Data* pTransmit, UINT len);
UINT FUNC_CALL ZCAN_Receive(CHANNEL_HANDLE channel_handle, ZCAN_Receive_Data* pReceive, UINT len, int wait_time);
UINT FUNC_CALL ZCAN_TransmitFD(CHANNEL_HANDLE channel_handle, ZCAN_TransmitFD_Data* pTransmit, UINT len);
UINT FUNC_CALL ZCAN_ReceiveFD(CHANNEL_HANDLE channel_handle, ZCAN_ReceiveFD_Data* pReceive, UINT len, int wait_time);
IProperty* FUNC_CALL GetIProperty(DEVICE_HANDLE device_handle);
UINT FUNC_CALL ReleaseIProperty(IProperty * pIProperty);
UINT FUNC_CALL ZCAN_SetAbitBaud(DEVICE_HANDLE device_handle, UINT can_index, UINT abitbaud);
UINT FUNC_CALL ZCAN_SetDbitBaud(DEVICE_HANDLE device_handle, UINT can_index, UINT dbitbaud);
UINT FUNC_CALL ZCAN_SetCANFDStandard(DEVICE_HANDLE device_handle, UINT can_index, UINT canfd_standard);
UINT FUNC_CALL ZCAN_SetResistanceEnable(DEVICE_HANDLE device_handle, UINT can_index, UINT enable);
UINT FUNC_CALL ZCAN_SetBaudRateCustom(DEVICE_HANDLE device_handle, UINT can_index, char * RateCustom);
UINT FUNC_CALL ZCAN_ClearFilter(CHANNEL_HANDLE channel_handle);
UINT FUNC_CALL ZCAN_AckFilter(CHANNEL_HANDLE channel_handle);
UINT FUNC_CALL ZCAN_SetFilterMode(CHANNEL_HANDLE channel_handle, UINT mode);
UINT FUNC_CALL ZCAN_SetFilterStartID(CHANNEL_HANDLE channel_handle, UINT startID);
UINT FUNC_CALL ZCAN_SetFilterEndID(CHANNEL_HANDLE channel_handle, UINT EndID);


//******************************************************************************************************
//******************************************************************************************************
// 以下函数为 兼容CANtest等软件 需要的原ControlCAN库函数
DWORD  __stdcall  VCI_OpenDevice(DWORD DeviceType,DWORD DeviceInd,DWORD Reserved);
  __declspec(dllexport)
DWORD __stdcall VCI_CloseDevice(DWORD DeviceType,DWORD DeviceInd);
  __declspec(dllexport)
DWORD __stdcall  VCI_InitCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd, PVCI_INIT_CONFIG pInitConfig);
  __declspec(dllexport)
DWORD __stdcall VCI_ReadBoardInfo(DWORD DeviceType,DWORD DeviceInd,PVCI_BOARD_INFO pInfo);
  __declspec(dllexport)
DWORD __stdcall VCI_ReadErrInfo(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_ERR_INFO pErrInfo);
  __declspec(dllexport)
DWORD __stdcall VCI_ReadCANStatus(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_CAN_STATUS pCANStatus);
  __declspec(dllexport)
DWORD __stdcall VCI_GetReference(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,DWORD RefType,PVOID pData);
  __declspec(dllexport)
DWORD __stdcall VCI_SetReference(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,DWORD RefType,PVOID pData);
  __declspec(dllexport)
ULONG __stdcall VCI_GetReceiveNum(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
  __declspec(dllexport)
DWORD __stdcall VCI_ClearBuffer(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
  __declspec(dllexport)
DWORD __stdcall VCI_StartCAN(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
  __declspec(dllexport)
DWORD __stdcall VCI_ResetCAN(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
  __declspec(dllexport)
ULONG  __stdcall VCI_Transmit(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_CAN_OBJ pSend,DWORD Length);
  __declspec(dllexport)
ULONG __stdcall  VCI_Receive(DWORD DevType, DWORD DevIndex, DWORD  CANIndex, PVCI_CAN_OBJ pReceive, ULONG Len, INT WaitTime);

#ifdef __cplusplus
}
#endif

#endif
