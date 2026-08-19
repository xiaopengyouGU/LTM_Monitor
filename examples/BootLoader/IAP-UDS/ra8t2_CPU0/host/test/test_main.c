#include "mock.h"
#include "bootloader.h"
#include "uds_server.h"
#include "iso15765.h"
#include "protocol/ltm_commut.h"
#include "port_led.h"
#include "bootloader_config.h"

#include <stdio.h>
#include <stdlib.h>

/* ============ CRC16（与 core/protocol/protocol.c 一致） ============ */
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
    return crc;
}

static uint32_t crc32_ref(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
    }
    return ~crc;
}

/* ============ CAN-FD 通道工具（UDS） ============ */
static void inject_sf(uint8_t *payload, uint16_t len)
{
    uint8_t frame[64];
    frame[0] = 0x00U;
    frame[1] = (uint8_t)len;
    memcpy(&frame[2], payload, len);
    mock_canfd_inject(BL_CANFD_UPGRADE_ID, frame, (uint16_t)(len + 2));
}

static int find_can_resp(uint8_t *out, uint16_t maxlen)
{
    for (int i = (int)mock_canfd_tx_count() - 1; i >= 0; i--) {
        if (mock_canfd_tx_id((uint16_t)i) != BL_CANFD_RESPONSE_ID) continue;
        uint16_t n = mock_canfd_tx_len((uint16_t)i);
        if (n < 2) continue;
        uint8_t pci = mock_canfd_tx_data((uint16_t)i)[0];
        if ((pci & 0xF0) != 0x00) continue;         /* 仅处理单帧响应 */
        uint16_t payload;
        const uint8_t *base;
        if (pci == 0x00U) { payload = mock_canfd_tx_data((uint16_t)i)[1]; base = mock_canfd_tx_data((uint16_t)i) + 2; }
        else              { payload = pci & 0x0F;    base = mock_canfd_tx_data((uint16_t)i) + 1; }
        if (payload > n - 2) payload = n - 2;
        if (payload > maxlen) payload = maxlen;
        memcpy(out, base, payload);
        return payload;
    }
    return -1;
}

/* ============ UART 通道工具（IAP / LTM） ============ */
static void uart_send_payload(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[140];
    frame[0] = 0xB9; frame[1] = 0xA5;       /* FRAME_HEADER 0xA5B9 小端 */
    frame[2] = type;
    frame[3] = (uint8_t)len;
    memcpy(&frame[4], payload, len);
    uint16_t crc = crc16(frame, 4 + len);
    frame[4 + len]     = (uint8_t)crc;
    frame[4 + len + 1] = (uint8_t)(crc >> 8);
    mock_uart_inject(frame, (uint16_t)(4 + len + 2));
    mock_uart_deliver();
}

/* 从 TX 字节流找最后一帧 Data_User_Defined（校验 CRC16），返回载荷 */
static int find_uart_resp(uint8_t *out, uint16_t maxlen)
{
    static uint8_t tx[4096];
    uint16_t txn = mock_uart_tx_len();
    mock_uart_tx_dump(tx, sizeof(tx));

    int best = -1;
    uint16_t best_len = 0;
    for (uint16_t i = 0; i + 5 < txn; ) {
        if (tx[i] == 0xB9 && tx[i + 1] == 0xA5) {
            uint8_t type = tx[i + 2];
            uint8_t len  = tx[i + 3];
            uint16_t total = (uint16_t)(4 + len + 2);
            if (i + total > txn) break;
            uint16_t crc = (uint16_t)(tx[i + 4 + len] | (tx[i + 4 + len + 1] << 8));
            if (crc == crc16(&tx[i], 4 + len)) {
                if (type == Data_User_Defined) { best = (int)i; best_len = len; }
                i += total;
                continue;
            }
        }
        i++;
    }
    if (best < 0) return -1;
    if (best_len > maxlen) best_len = maxlen;
    memcpy(out, &tx[best + 4], best_len);
    return (int)best_len;
}

/* ============ 用例 1：LTM 协议帧解析（协议层回环） ============ */
static void test_ltm_roundtrip(void)
{
    mock_uart_tx_clear();
    ltm_commut_init();
    ltm_commut_set_send(mock_uart_emit);

    uint8_t payload[8] = { 0x10, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    ltm_commut_send(Data_User_Defined, payload, sizeof(payload));

    uint8_t tx[64];
    uint16_t txn = mock_uart_tx_len();
    mock_uart_tx_dump(tx, sizeof(tx));
    TEST_ASSERT(txn == 4 + 8 + 2);
    TEST_ASSERT(tx[0] == 0xB9 && tx[1] == 0xA5);
    TEST_ASSERT(tx[2] == Data_User_Defined && tx[3] == 8);

    /* 回灌解析 */
    ltm_commut_recv(tx, txn);
    uint8_t type = 0; uint8_t out[16]; uint16_t out_len = 0;
    TEST_ASSERT(ltm_commut_process(&type, out, &out_len) == true);
    TEST_ASSERT(type == Data_User_Defined && out_len == 8);
    TEST_ASSERT(memcmp(out, payload, 8) == 0);
}

/* ============ 用例 2：UART 批量接收（帧拆分注入，模拟 FIFO 分批） ============ */
static void test_uart_batch_rx(void)
{
    mock_flash_reset();
    mock_uart_tx_clear();
    bl_init();

    /* 构造完整帧并拆成 [3][1][5] 三批注入，模拟三次 RXI 批量回调 */
    uint8_t req[4] = { 0x10, 0x02, 0x00, 0x00 };
    uint8_t frame[140];
    frame[0] = 0xB9; frame[1] = 0xA5;
    frame[2] = Data_User_Defined;
    frame[3] = 2;
    memcpy(&frame[4], req, 2);
    uint16_t crc = crc16(frame, 6);
    frame[6] = (uint8_t)crc; frame[7] = (uint8_t)(crc >> 8);

    mock_uart_inject(frame, 3); mock_uart_deliver();   /* 第一批：帧头前 3 字节 */
    mock_uart_inject(&frame[3], 1); mock_uart_deliver(); /* 第二批：第 4 字节 */
    mock_uart_inject(&frame[4], 4); mock_uart_deliver(); /* 第三批：载荷 + CRC */
    bl_process();

    uint8_t r[16];
    int rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x50 && r[1] == 0x02);
}

/* ============ 用例 3：UART 完整升级（IAP 通道） ============ */
static int do_upgrade_uart(const uint8_t *fw, uint32_t size, uint16_t chunk)
{
    uint8_t r[32];
    int rn;
    uint32_t app_base = BL_APP_RUN_START;

    mock_uart_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    uart_send_payload(Data_User_Defined, p10, 2); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x50)) return -1;

    mock_uart_tx_clear();
    uint8_t p27a[2] = { 0x27, 0x01 };
    uart_send_payload(Data_User_Defined, p27a, 2); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_uart_tx_clear();
    uint8_t p27b[4] = { 0x27, 0x02, 0x00, 0x5A };
    uart_send_payload(Data_User_Defined, p27b, 4); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_uart_tx_clear();
    uint8_t p34[11];
    p34[0] = 0x34; p34[1] = 0x00; p34[2] = 0x00;
    p34[3] = (uint8_t)(app_base >> 24); p34[4] = (uint8_t)(app_base >> 16);
    p34[5] = (uint8_t)(app_base >> 8);  p34[6] = (uint8_t)app_base;
    p34[7] = (uint8_t)(size >> 24); p34[8] = (uint8_t)(size >> 16);
    p34[9] = (uint8_t)(size >> 8);  p34[10] = (uint8_t)size;
    uart_send_payload(Data_User_Defined, p34, 11); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x74)) return -1;

    uint8_t seq = 0;
    for (uint32_t off = 0; off < size; off += chunk) {
        mock_uart_tx_clear();
        uint32_t n = size - off; if (n > chunk) n = chunk;
        seq = (uint8_t)(seq + 1);
        uint8_t p36[130];
        p36[0] = 0x36; p36[1] = seq;
        memcpy(&p36[2], &fw[off], n);
        uart_send_payload(Data_User_Defined, p36, (uint16_t)(n + 2)); bl_process();
        rn = find_uart_resp(r, sizeof(r));
        if (!(rn >= 2 && r[0] == 0x76 && r[1] == seq)) return -1;
    }

    mock_uart_tx_clear();
    uint8_t p37[1] = { 0x37 };
    uart_send_payload(Data_User_Defined, p37, 1); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 5 && r[0] == 0x77)) return -1;
    uint32_t dev_crc = ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) |
                       ((uint32_t)r[3] << 8) | r[4];
    if (dev_crc != crc32_ref(fw, size)) return -1;

    uint8_t *back = (uint8_t *)malloc(size);
    if (!back) return -1;
    mock_flash_dump(app_base, size, back);
    int ok = (memcmp(back, fw, size) == 0);
    free(back);
    if (!ok) return -1;

    mock_uart_tx_clear();
    uint8_t p11[2] = { 0x11, 0x01 };
    uart_send_payload(Data_User_Defined, p11, 2); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x51)) return -1;
    return 0;
}

static void test_uart_full_upgrade(void)
{
    uint8_t fw[600];
    for (int i = 0; i < 600; i++) fw[i] = (uint8_t)(i * 7 + 1);
    mock_flash_reset();
    mock_uart_tx_clear();
    bl_init();
    TEST_ASSERT(do_upgrade_uart(fw, sizeof(fw), 100) == 0);
}

/* ============ 用例 4：CAN-FD 完整升级（UDS 通道，回归） ============ */
static int do_upgrade_can(const uint8_t *fw, uint32_t size, uint16_t chunk)
{
    uint8_t r[16];
    int rn;
    uint32_t app_base = BL_APP_RUN_START;

    mock_canfd_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    inject_sf(p10, 2); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x50)) return -1;

    mock_canfd_tx_clear();
    uint8_t p27a[2] = { 0x27, 0x01 };
    inject_sf(p27a, 2); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_canfd_tx_clear();
    uint8_t p27b[4] = { 0x27, 0x02, 0x00, 0x5A };
    inject_sf(p27b, 4); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_canfd_tx_clear();
    uint8_t p34[11];
    p34[0] = 0x34; p34[1] = 0x00; p34[2] = 0x00;
    p34[3] = (uint8_t)(app_base >> 24); p34[4] = (uint8_t)(app_base >> 16);
    p34[5] = (uint8_t)(app_base >> 8);  p34[6] = (uint8_t)app_base;
    p34[7] = (uint8_t)(size >> 24); p34[8] = (uint8_t)(size >> 16);
    p34[9] = (uint8_t)(size >> 8);  p34[10] = (uint8_t)size;
    inject_sf(p34, 11); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x74)) return -1;

    uint8_t seq = 0;
    for (uint32_t off = 0; off < size; off += chunk) {
        mock_canfd_tx_clear();
        uint32_t n = size - off; if (n > chunk) n = chunk;
        seq = (uint8_t)(seq + 1);
        uint8_t p36[64];
        p36[0] = 0x36; p36[1] = seq;
        memcpy(&p36[2], &fw[off], n);
        inject_sf(p36, (uint16_t)(n + 2)); bl_process();
        rn = find_can_resp(r, sizeof(r));
        if (!(rn >= 2 && r[0] == 0x76 && r[1] == seq)) return -1;
    }

    mock_canfd_tx_clear();
    uint8_t p37[1] = { 0x37 };
    inject_sf(p37, 1); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 5 && r[0] == 0x77)) return -1;
    uint32_t dev_crc = ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) |
                       ((uint32_t)r[3] << 8) | r[4];
    if (dev_crc != crc32_ref(fw, size)) return -1;

    uint8_t *back = (uint8_t *)malloc(size);
    if (!back) return -1;
    mock_flash_dump(app_base, size, back);
    int ok = (memcmp(back, fw, size) == 0);
    free(back);
    if (!ok) return -1;

    mock_canfd_tx_clear();
    uint8_t p11[2] = { 0x11, 0x01 };
    inject_sf(p11, 2); bl_process();
    rn = find_can_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x51)) return -1;
    return 0;
}

static void test_can_full_upgrade(void)
{
    uint8_t fw[500];
    for (int i = 0; i < 500; i++) fw[i] = (uint8_t)(i * 3 + 9);
    mock_flash_reset();
    mock_canfd_tx_clear();
    bl_init();
    TEST_ASSERT(do_upgrade_can(fw, sizeof(fw), 60) == 0);
}

/* ============ 用例 5：跨通道混用（共享会话/下载状态） ============ */
static void test_cross_channel(void)
{
    uint8_t fw[300];
    for (int i = 0; i < 300; i++) fw[i] = (uint8_t)(i * 11 + 3);
    mock_flash_reset();
    mock_canfd_tx_clear();
    mock_uart_tx_clear();
    bl_init();

    /* UART：会话 + 解锁 */
    mock_uart_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    uart_send_payload(Data_User_Defined, p10, 2); bl_process();
    uint8_t r[16]; int rn;
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x50);

    mock_uart_tx_clear();
    uint8_t p27[4] = { 0x27, 0x02, 0x00, 0x5A };
    uart_send_payload(Data_User_Defined, p27, 4); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x67);

    /* CAN：请求下载 + 传输 + 退出（状态与 UART 会话共享） */
    uint32_t app_base = BL_APP_RUN_START;
    mock_canfd_tx_clear();
    uint8_t p34[11];
    p34[0] = 0x34; p34[1] = 0x00; p34[2] = 0x00;
    p34[3] = (uint8_t)(app_base >> 24); p34[4] = (uint8_t)(app_base >> 16);
    p34[5] = (uint8_t)(app_base >> 8);  p34[6] = (uint8_t)app_base;
    p34[7] = (uint8_t)(sizeof(fw) >> 24); p34[8] = (uint8_t)(sizeof(fw) >> 16);
    p34[9] = (uint8_t)(sizeof(fw) >> 8);  p34[10] = (uint8_t)sizeof(fw);
    inject_sf(p34, 11); bl_process();
    rn = find_can_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x74);

    uint8_t seq = 0;
    for (uint32_t off = 0; off < sizeof(fw); off += 60) {
        mock_canfd_tx_clear();
        uint32_t n = sizeof(fw) - off; if (n > 60) n = 60;
        seq = (uint8_t)(seq + 1);
        uint8_t p36[64];
        p36[0] = 0x36; p36[1] = seq;
        memcpy(&p36[2], &fw[off], n);
        inject_sf(p36, (uint16_t)(n + 2)); bl_process();
        rn = find_can_resp(r, sizeof(r));
        TEST_ASSERT(rn >= 2 && r[0] == 0x76);
    }

    /* UART：退出传输（CRC 与 CAN 传输累计一致） */
    mock_uart_tx_clear();
    uint8_t p37[1] = { 0x37 };
    uart_send_payload(Data_User_Defined, p37, 1); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 5 && r[0] == 0x77);
    uint32_t dev_crc = ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) |
                       ((uint32_t)r[3] << 8) | r[4];
    TEST_ASSERT(dev_crc == crc32_ref(fw, sizeof(fw)));

    uint8_t back[300];
    mock_flash_dump(app_base, sizeof(fw), back);
    TEST_ASSERT(memcmp(back, fw, sizeof(fw)) == 0);
}

/* ============ 用例 6：UART 通道负向测试（NRC） ============ */
static void test_uart_negative(void)
{
    mock_flash_reset();
    mock_uart_tx_clear();
    bl_init();

    /* 未进编程会话直接 0x34 -> 0x22 */
    mock_uart_tx_clear();
    uint8_t p34[11] = { 0x34, 0x00, 0x00, 0,0,0,0, 0,0,0,1 };
    uart_send_payload(Data_User_Defined, p34, 11); bl_process();
    uint8_t r[16]; int rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x22);

    /* 进会话未解锁 0x34 -> 0x33 */
    mock_uart_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    uart_send_payload(Data_User_Defined, p10, 2); bl_process();
    mock_uart_tx_clear();
    uart_send_payload(Data_User_Defined, p34, 11); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x33);

    /* 错误密钥 -> 0x33 */
    mock_uart_tx_clear();
    uint8_t p27[3] = { 0x27, 0x02, 0x00 };
    uart_send_payload(Data_User_Defined, p27, 3); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x33);

    /* 未知命令 -> 0x11 */
    mock_uart_tx_clear();
    uint8_t p99[1] = { 0x99 };
    uart_send_payload(Data_User_Defined, p99, 1); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x11);

    /* 长度错误（0x10 带数据）-> 0x13 */
    mock_uart_tx_clear();
    uint8_t p10b[3] = { 0x10, 0x02, 0x00 };
    uart_send_payload(Data_User_Defined, p10b, 3); bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x13);

    /* 非 Data_User_Defined 类型：BootLoader 不响应 */
    mock_uart_tx_clear();
    uart_send_payload(Data_CMD_Text, (uint8_t *)"hello", 5); bl_process();
    TEST_ASSERT(find_uart_resp(r, sizeof(r)) == -1);
}

/* ============ 用例 7：运行区 + 备份区 / 崩溃回滚 ============ */
static void test_run_backup_rollback(void)
{
    mock_flash_reset();
    mock_canfd_tx_clear();

    bl_init();
    mock_sys_tick(1000);
    bl_process();
    TEST_ASSERT(mock_led_toggle_count(PORT_LED_RUN) >= 1);

    /* 写入运行区有效向量表（旧固件 V1） */
    uint32_t vec1[2] = { 0x20000000, BL_APP_RUN_START + 1 };
    mock_flash_erase(BL_APP_RUN_START, 64);
    mock_flash_write(BL_APP_RUN_START, (uint8_t *)vec1, sizeof(vec1));
    TEST_ASSERT(bl_app_is_valid(BL_APP_RUN_START) == true);

    /* 请求升级 -> 备份运行区 -> IDLE */
    bl_request_upgrade();
    bl_init();

    uint32_t backed[2] = { 0 };
    mock_flash_read(BL_APP_BACKUP_START, (uint8_t *)backed, sizeof(backed));
    TEST_ASSERT(backed[0] == vec1[0] && backed[1] == vec1[1]);

    /* 模拟 0x37 完成置 pending + 写入新固件 V2 */
    bl_mark_pending();
    uint32_t vec2[2] = { 0x20000000, BL_APP_RUN_START + 1 };
    mock_flash_erase(BL_APP_RUN_START, 64);
    mock_flash_write(BL_APP_RUN_START, (uint8_t *)vec2, sizeof(vec2));

    /* V2 崩溃：连续复位，attempts 超限 -> 从备份区恢复 V1 */
    for (int i = 0; i < BL_MAX_ATTEMPTS; i++)
        bl_init();

    uint32_t rolled[2] = { 0 };
    mock_flash_read(BL_APP_RUN_START, (uint8_t *)rolled, sizeof(rolled));
    TEST_ASSERT(rolled[0] == vec1[0] && rolled[1] == vec1[1]);
    TEST_ASSERT(bl_app_is_valid(BL_APP_RUN_START) == true);
}

/* ============ 用例 8：有效程序也先驻留升级窗口（复位后 6s 内可烧录，超时跳转） ============ */
static void test_valid_app_idle_window(void)
{
    mock_flash_reset();
    mock_uart_tx_clear();
    mock_sys_tick(0);
    mock_sys_jump_clear();

    /* 运行区写入有效向量表（"有效但崩溃"的程序） */
    uint32_t vec[2] = { 0x20000000, BL_APP_RUN_START + 1 };
    mock_flash_erase(BL_APP_RUN_START, 64);
    mock_flash_write(BL_APP_RUN_START, (uint8_t *)vec, sizeof(vec));

    /* A：复位后无命令 —— 6s 窗口内不跳转，超时才跳转 */
    bl_init();
    bl_process();
    mock_sys_tick(mock_sys_now() + BL_IDLE_TIMEOUT_MS - 100);
    bl_process();
    TEST_ASSERT(mock_sys_jump_count() == 0);

    mock_sys_tick(mock_sys_now() + 200);
    bl_process();       /* 超时触发：状态转 JUMP */
    bl_process();       /* 执行跳转 */
    TEST_ASSERT(mock_sys_jump_count() == 1);

    /* B：窗口内上位机可立即进编程会话（有效程序也能烧录） */
    mock_flash_reset();
    mock_uart_tx_clear();
    mock_sys_tick(0);
    mock_sys_jump_clear();
    mock_flash_erase(BL_APP_RUN_START, 64);
    mock_flash_write(BL_APP_RUN_START, (uint8_t *)vec, sizeof(vec));
    bl_init();

    mock_uart_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    uart_send_payload(Data_User_Defined, p10, 2);
    bl_process();
    uint8_t r[16];
    int rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x50);       /* 窗口内可进编程会话 */
    TEST_ASSERT(mock_sys_jump_count() == 0);
}

int main(void)
{
    printf("==== 统一 BootLoader 平台无关测试（UDS + IAP 双通道） ====\n");

    test_ltm_roundtrip();
    test_uart_batch_rx();
    test_uart_full_upgrade();
    test_can_full_upgrade();
    test_cross_channel();
    test_uart_negative();
    test_run_backup_rollback();
    test_valid_app_idle_window();

    printf("\n==== 结果：PASS %d, FAIL %d ====\n", test_passes, test_failures);
    return test_failures ? 1 : 0;
}
