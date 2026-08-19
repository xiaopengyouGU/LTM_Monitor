#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""IAP UART 固件升级客户端（统一 BootLoader 的 UART 通道）

传输层：LTM 协议（帧头 0xA5B9 小端 + 数据类型 + 长度 + 载荷 + CRC16）
业务层：UDS 兼容 PDU（与 CAN-FD 通道完全一致），经 Data_User_Defined 通道收发

依赖：pyserial（pip install pyserial）
用法：
    python upgrade_client.py --port COM3 --firmware app.bin
    python upgrade_client.py --port COM3 --firmware app.bin --baud 115200

流程：0x10 02 -> 0x27 -> 0x34 -> 0x36 循环 -> 0x37 -> 0x11 01
"""
import argparse
import struct
import sys
import time
import zlib

import serial

DATA_USER_DEFINED = 0x1E        # 与 core/protocol/ltm_commut.h 枚举一致
FRAME_HEADER = 0xA5B9

SID_SESSION = 0x10
SID_UNLOCK = 0x27
SID_DOWNLOAD = 0x34
SID_DATA = 0x36
SID_EXIT = 0x37
SID_RESET = 0x11

NRC_NAMES = {
    0x11: "service not supported",
    0x12: "subfunction not supported",
    0x13: "incorrect length",
    0x22: "conditions not correct",
    0x31: "request out of range",
    0x33: "security access denied",
    0x72: "general programming fault",
}

CHUNK = 100                     # 单块数据 100B（LTM 载荷上限 128 - SID - seq = 126）


def crc16(data: bytes) -> int:
    """Modbus RTU CRC16，与 core/protocol/protocol.c 一致"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
    return crc


class IapClient:
    def __init__(self, ser):
        self.ser = ser
        self.rx = bytearray()

    def _send(self, payload: bytes):
        frame = bytearray()
        frame += struct.pack("<H", FRAME_HEADER)   # B9 A5
        frame.append(DATA_USER_DEFINED)
        frame.append(len(payload))
        frame += payload
        frame += struct.pack("<H", crc16(frame))
        self.ser.write(frame)

    def _recv(self, timeout=2.0) -> bytes:
        """扫描 RX 字节流，解出 Data_User_Defined 帧并校验 CRC，返回载荷"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(max(1, int(deadline - time.time()) * 100) or 1)
            if chunk:
                self.rx += chunk
                while True:
                    idx = self.rx.find(b"\xB9\xA5")
                    if idx < 0:
                        if len(self.rx) > 2:
                            del self.rx[:-1]
                        break
                    if idx > 0:
                        del self.rx[:idx]
                    if len(self.rx) < 4:
                        break
                    ftype = self.rx[2]
                    flen = self.rx[3]
                    total = 4 + flen + 2
                    if len(self.rx) < total:
                        break
                    frame = bytes(self.rx[:total])
                    del self.rx[:total]
                    if crc16(frame) != 0:
                        continue                    # CRC 错，继续找下一帧
                    if ftype == DATA_USER_DEFINED:
                        return frame[4:4 + flen]
        raise TimeoutError("IAP response timeout")

    def _request(self, payload: bytes) -> bytes:
        self._send(payload)
        resp = self._recv()
        if resp[0] == 0x7F:
            nrc = resp[2]
            raise RuntimeError(f"NRC 0x{nrc:02X}: {NRC_NAMES.get(nrc, 'unknown')}")
        return resp

    def enter_programming(self):
        self._request(bytes([SID_SESSION, 0x02]))
        print("[OK] 进入编程会话 (0x10 02)")

    def security_access(self):
        seed = self._request(bytes([SID_UNLOCK, 0x01]))
        key = bytes([0x00, 0x5A])                   # 与固件约定的简化密钥
        self._request(bytes([SID_UNLOCK, 0x02]) + key)
        print("[OK] 安全访问通过 (0x27, seed=%s)" % seed[2:].hex())

    def request_download(self, app_base: int, size: int):
        payload = bytes([SID_DOWNLOAD, 0x00, 0x00]) + struct.pack(">II", app_base, size)
        self._request(payload)
        print(f"[OK] 请求下载 (0x34): 地址 0x{app_base:08X} 大小 {size} 字节")

    def transfer(self, firmware: bytes):
        seq = 0
        total = len(firmware)
        for off in range(0, total, CHUNK):
            chunk = firmware[off:off + CHUNK]
            seq = (seq + 1) & 0xFF
            payload = bytes([SID_DATA, seq]) + chunk
            self._request(payload)
            pct = (off + len(chunk)) * 100 // total
            if pct % 20 == 0 or off + len(chunk) == total:
                print(f"    ... {pct}% (块 {seq})")
        print(f"[OK] 传输完成 (0x36): {total} 字节, {total // CHUNK + 1} 帧")

    def transfer_exit(self) -> int:
        resp = self._request(bytes([SID_EXIT]))
        crc = struct.unpack(">I", resp[1:5])[0]
        print(f"[OK] 退出传输 (0x37): CRC=0x{crc:08X}")
        return crc

    def ecu_reset(self):
        self._request(bytes([SID_RESET, 0x01]))
        print("[OK] ECU 复位 (0x11 01)，跳转新固件")


def main():
    ap = argparse.ArgumentParser(description="IAP UART 固件升级工具（LTM 协议）")
    ap.add_argument("--port", required=True, help="串口，如 COM3")
    ap.add_argument("--baud", type=int, default=115200, help="波特率（默认 115200）")
    ap.add_argument("--firmware", required=True, help="固件 bin 文件")
    ap.add_argument("--app-base", type=lambda x: int(x, 0), default=0x02008000, help="App 起始地址")
    args = ap.parse_args()

    with open(args.firmware, "rb") as f:
        firmware = f.read()
    if not firmware:
        sys.exit("[FAIL] 固件文件为空")
    print(f"[..] 固件: {args.firmware} ({len(firmware)} 字节)")
    print(f"[..] 期望 CRC: 0x{zlib.crc32(firmware) & 0xFFFFFFFF:08X}")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as e:
        sys.exit(f"[FAIL] 打开串口失败: {e}")

    try:
        client = IapClient(ser)
        client.enter_programming()
        client.security_access()
        client.request_download(args.app_base, len(firmware))
        client.transfer(firmware)
        crc = client.transfer_exit()
        expect = zlib.crc32(firmware) & 0xFFFFFFFF
        if crc != expect:
            sys.exit(f"[FAIL] CRC 不匹配: 设备 0x{crc:08X} vs 期望 0x{expect:08X}")
        client.ecu_reset()
        print("[OK] 升级完成，固件已生效")
    except (TimeoutError, RuntimeError, serial.SerialException) as e:
        sys.exit(f"[FAIL] {e}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
