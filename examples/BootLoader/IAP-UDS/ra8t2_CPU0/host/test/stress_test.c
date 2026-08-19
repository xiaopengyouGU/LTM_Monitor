#include "mock.h"
#include "bootloader.h"
#include "uds_server.h"
#include "iso15765.h"
#include "protocol/ltm_commut.h"
#include "bootloader_config.h"
#include "port_led.h"

#include <stdio.h>
#include <stdlib.h>

/* ============ 与 test_main 相同的工具 ============ */
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

/* CAN 响应查找（单帧） */
static int find_can_resp_local(uint8_t *out, uint16_t maxlen);

static void uart_send_payload(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[140];
    frame[0] = 0xB9; frame[1] = 0xA5;
    frame[2] = type;
    frame[3] = (uint8_t)len;
    memcpy(&frame[4], payload, len);
    uint16_t crc = crc16(frame, 4 + len);
    frame[4 + len]     = (uint8_t)crc;
    frame[4 + len + 1] = (uint8_t)(crc >> 8);
    mock_uart_inject(frame, (uint16_t)(4 + len + 2));
    mock_uart_deliver();
}

/* 随机拆分注入：把一帧拆成若干随机大小的批次，模拟 RXI FIFO 触发 + 15ETU 兜底 */
static void uart_send_payload_split(uint8_t type, const uint8_t *payload, uint16_t len, uint32_t *seed)
{
    uint8_t frame[140];
    frame[0] = 0xB9; frame[1] = 0xA5;
    frame[2] = type;
    frame[3] = (uint8_t)len;
    memcpy(&frame[4], payload, len);
    uint16_t crc = crc16(frame, 4 + len);
    frame[4 + len]     = (uint8_t)crc;
    frame[4 + len + 1] = (uint8_t)(crc >> 8);
    uint16_t total = (uint16_t)(4 + len + 2);

    uint16_t off = 0;
    while (off < total) {
        *seed = *seed * 1103515245U + 12345U;
        uint16_t n = (uint16_t)((*seed >> 8) % 7) + 1;   /* 1~7 字节一批 */
        if (n > total - off) n = total - off;
        mock_uart_inject(&frame[off], n);
        mock_uart_deliver();
        off += n;
    }
}

static int find_uart_resp(uint8_t *out, uint16_t maxlen)
{
    static uint8_t tx[8192];
    uint16_t txn = mock_uart_tx_len();
    if (txn > sizeof(tx)) txn = sizeof(tx);
    mock_uart_tx_dump(tx, txn);

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

/* UART 完整升级（支持随机拆分注入 + 非 32B 对齐块长） */
static int do_upgrade_uart(const uint8_t *fw, uint32_t size, uint16_t chunk, bool split, uint32_t *seed)
{
    uint8_t r[32];
    int rn;
    uint32_t app_base = BL_APP_RUN_START;

    mock_uart_tx_clear();
    uint8_t p10[2] = { 0x10, 0x02 };
    if (split) uart_send_payload_split(Data_User_Defined, p10, 2, seed);
    else       uart_send_payload(Data_User_Defined, p10, 2);
    bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x50)) return -1;

    mock_uart_tx_clear();
    uint8_t p27a[2] = { 0x27, 0x01 };
    if (split) uart_send_payload_split(Data_User_Defined, p27a, 2, seed);
    else       uart_send_payload(Data_User_Defined, p27a, 2);
    bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_uart_tx_clear();
    uint8_t p27b[4] = { 0x27, 0x02, 0x00, 0x5A };
    if (split) uart_send_payload_split(Data_User_Defined, p27b, 4, seed);
    else       uart_send_payload(Data_User_Defined, p27b, 4);
    bl_process();
    rn = find_uart_resp(r, sizeof(r));
    if (!(rn >= 2 && r[0] == 0x67)) return -1;

    mock_uart_tx_clear();
    uint8_t p34[11];
    p34[0] = 0x34; p34[1] = 0x00; p34[2] = 0x00;
    p34[3] = (uint8_t)(app_base >> 24); p34[4] = (uint8_t)(app_base >> 16);
    p34[5] = (uint8_t)(app_base >> 8);  p34[6] = (uint8_t)app_base;
    p34[7] = (uint8_t)(size >> 24); p34[8] = (uint8_t)(size >> 16);
    p34[9] = (uint8_t)(size >> 8);  p34[10] = (uint8_t)size;
    if (split) uart_send_payload_split(Data_User_Defined, p34, 11, seed);
    else       uart_send_payload(Data_User_Defined, p34, 11);
    bl_process();
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
        if (split) uart_send_payload_split(Data_User_Defined, p36, (uint16_t)(n + 2), seed);
        else       uart_send_payload(Data_User_Defined, p36, (uint16_t)(n + 2));
        bl_process();
        rn = find_uart_resp(r, sizeof(r));
        if (!(rn >= 2 && r[0] == 0x76 && r[1] == seq)) return -1;
    }

    mock_uart_tx_clear();
    uint8_t p37[1] = { 0x37 };
    if (split) uart_send_payload_split(Data_User_Defined, p37, 1, seed);
    else       uart_send_payload(Data_User_Defined, p37, 1);
    bl_process();
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
    return 0;
}

/* ============ 压测 1：大固件（200KB，接近运行区上限）UART 100B 块 ============ */
static void stress_large_uart(void)
{
    const uint32_t SIZE = 200 * 1024;
    uint8_t *fw = (uint8_t *)malloc(SIZE);
    if (!fw) { TEST_ASSERT(0); return; }
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < SIZE; i++) {
        seed = seed * 1103515245U + 12345U;
        fw[i] = (uint8_t)(seed >> 16);
    }

    mock_flash_reset();
    mock_uart_tx_clear();
    bl_init();
    TEST_ASSERT(do_upgrade_uart(fw, SIZE, 100, false, &seed) == 0);
    free(fw);
}

/* ============ 压测 2：多轮升级，块长覆盖 1/7/32/100/126（含非 32B 对齐） ============ */
static void stress_repeat_uart(void)
{
    static const uint16_t chunks[] = { 1, 7, 32, 100, 126 };
    for (int round = 0; round < 3; round++) {
        uint8_t fw[4000];
        for (int i = 0; i < 4000; i++)
            fw[i] = (uint8_t)(i * 31 + round * 7);
        for (size_t c = 0; c < sizeof(chunks)/sizeof(chunks[0]); c++) {
            mock_flash_reset();
            bl_init();
            uint32_t seed = 777 + round;
            TEST_ASSERT(do_upgrade_uart(fw, sizeof(fw), chunks[c], false, &seed) == 0);
        }
        printf("[STRESS] UART round %d: %d chunk sizes OK\n", round + 1, (int)(sizeof(chunks)/sizeof(chunks[0])));
    }
}

/* ============ 压测 3：随机拆分注入（模拟 FIFO 分批/乱序到达） ============ */
static void stress_split_inject(void)
{
    for (int round = 0; round < 3; round++) {
        uint8_t fw[3000];
        for (int i = 0; i < 3000; i++)
            fw[i] = (uint8_t)(i * 13 + round);
        mock_flash_reset();
        bl_init();
        uint32_t seed = 555 + round * 100;
        TEST_ASSERT(do_upgrade_uart(fw, sizeof(fw), 60, true, &seed) == 0);
        printf("[STRESS] split-inject round %d OK\n", round + 1);
    }
}

/* ============ 压测 4：恶意/损坏帧 + 恢复 ============ */
static void stress_malformed_uart(void)
{
    uint8_t r[16];
    mock_flash_reset();
    mock_uart_tx_clear();
    bl_init();

    /* 4.1 垃圾字节流 + 中间夹一帧合法 0x10 02，验证帧头重同步 */
    mock_uart_tx_clear();
    uint8_t garbage[40];
    for (int i = 0; i < 40; i++) garbage[i] = (uint8_t)(i * 7 + 3);
    mock_uart_inject(garbage, sizeof(garbage));
    mock_uart_deliver();
    uint8_t p10[2] = { 0x10, 0x02 };
    uart_send_payload(Data_User_Defined, p10, 2);
    bl_process();
    int rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x50);

    /* 4.2 CRC 错误的帧被丢弃，随后合法帧正常响应 */
    mock_uart_tx_clear();
    uint8_t bad[8] = { 0xB9, 0xA5, Data_User_Defined, 2, 0x27, 0x01, 0x11, 0x11 };  /* CRC 错 */
    mock_uart_inject(bad, sizeof(bad));
    mock_uart_deliver();
    bl_process();
    TEST_ASSERT(find_uart_resp(r, sizeof(r)) == -1);

    mock_uart_tx_clear();
    uart_send_payload(Data_User_Defined, p10, 2);
    bl_process();
    rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 2 && r[0] == 0x50);

    /* 4.3 超长 data_len 帧被协议层拒绝（>128） */
    mock_uart_tx_clear();
    uint8_t over[8] = { 0xB9, 0xA5, Data_User_Defined, 200, 0x27, 0x01, 0x00, 0x00 };
    mock_uart_inject(over, sizeof(over));
    mock_uart_deliver();
    bl_process();
    TEST_ASSERT(find_uart_resp(r, sizeof(r)) == -1);
}

/* ============ 压测 5：跨通道混用压测（UART 会话/解锁 + CAN 传输 + UART 退出，多轮） ============ */
static void stress_cross_channel(void)
{
    for (int round = 0; round < 3; round++) {
        uint8_t fw[2000];
        for (int i = 0; i < 2000; i++) fw[i] = (uint8_t)(i * 5 + round);
        mock_flash_reset();
        mock_canfd_tx_clear();
        mock_uart_tx_clear();
        bl_init();

        /* UART：会话 + 解锁 */
        mock_uart_tx_clear();
        uint8_t p10[2] = { 0x10, 0x02 };
        uart_send_payload(Data_User_Defined, p10, 2); bl_process();
        uint8_t r[16]; int rn = find_uart_resp(r, sizeof(r));
        TEST_ASSERT(rn >= 2 && r[0] == 0x50);

        mock_uart_tx_clear();
        uint8_t p27[4] = { 0x27, 0x02, 0x00, 0x5A };
        uart_send_payload(Data_User_Defined, p27, 4); bl_process();
        rn = find_uart_resp(r, sizeof(r));
        TEST_ASSERT(rn >= 2 && r[0] == 0x67);

        /* CAN：请求下载 + 传输 */
        uint32_t app_base = BL_APP_RUN_START;
        mock_canfd_tx_clear();
        uint8_t p34[11];
        p34[0] = 0x34; p34[1] = 0x00; p34[2] = 0x00;
        p34[3] = (uint8_t)(app_base >> 24); p34[4] = (uint8_t)(app_base >> 16);
        p34[5] = (uint8_t)(app_base >> 8);  p34[6] = (uint8_t)app_base;
        p34[7] = (uint8_t)(sizeof(fw) >> 24); p34[8] = (uint8_t)(sizeof(fw) >> 16);
        p34[9] = (uint8_t)(sizeof(fw) >> 8);  p34[10] = (uint8_t)sizeof(fw);
        uint8_t sf[64];
        sf[0] = 0x00; sf[1] = 11;
        memcpy(&sf[2], p34, 11);
        mock_canfd_inject(BL_CANFD_UPGRADE_ID, sf, 13); bl_process();
        rn = find_can_resp_local(r, sizeof(r));
        TEST_ASSERT(rn >= 2 && r[0] == 0x74);

        uint8_t seq = 0;
        for (uint32_t off = 0; off < sizeof(fw); off += 60) {
            mock_canfd_tx_clear();
            uint32_t n = sizeof(fw) - off; if (n > 60) n = 60;
            seq = (uint8_t)(seq + 1);
            uint8_t p36[64];
            p36[0] = 0x36; p36[1] = seq;
            memcpy(&p36[2], &fw[off], n);
            sf[0] = 0x00; sf[1] = (uint8_t)(n + 2);
            memcpy(&sf[2], p36, n + 2);
            mock_canfd_inject(BL_CANFD_UPGRADE_ID, sf, (uint16_t)(n + 4)); bl_process();
            rn = find_can_resp_local(r, sizeof(r));
            TEST_ASSERT(rn >= 2 && r[0] == 0x76);
        }

        /* UART：退出 + CRC 校验 */
        mock_uart_tx_clear();
        uint8_t p37[1] = { 0x37 };
        uart_send_payload(Data_User_Defined, p37, 1); bl_process();
        rn = find_uart_resp(r, sizeof(r));
        TEST_ASSERT(rn >= 5 && r[0] == 0x77);
        uint32_t dev_crc = ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) |
                           ((uint32_t)r[3] << 8) | r[4];
        TEST_ASSERT(dev_crc == crc32_ref(fw, sizeof(fw)));
        printf("[STRESS] cross-channel round %d OK\n", round + 1);
    }
}

static int find_can_resp_local(uint8_t *out, uint16_t maxlen)
{
    for (int i = (int)mock_canfd_tx_count() - 1; i >= 0; i--) {
        if (mock_canfd_tx_id((uint16_t)i) != BL_CANFD_RESPONSE_ID) continue;
        uint16_t n = mock_canfd_tx_len((uint16_t)i);
        if (n < 2) continue;
        uint8_t pci = mock_canfd_tx_data((uint16_t)i)[0];
        if ((pci & 0xF0) != 0x00) continue;
        uint16_t payload = (pci == 0x00U) ? mock_canfd_tx_data((uint16_t)i)[1] : (pci & 0x0F);
        const uint8_t *base = (pci == 0x00U) ? mock_canfd_tx_data((uint16_t)i) + 2 : mock_canfd_tx_data((uint16_t)i) + 1;
        if (payload > n - 2) payload = n - 2;
        if (payload > maxlen) payload = maxlen;
        memcpy(out, base, payload);
        return payload;
    }
    return -1;
}

/* ============ 压测 6：超时恢复 ============ */
static void stress_timeout(void)
{
    mock_flash_reset();
    mock_sys_tick(0);

    /* App 无效：IDLE 超时后继续等待，不崩溃 */
    bl_init();
    mock_sys_tick(BL_IDLE_TIMEOUT_MS + 100);
    bl_process();
    TEST_ASSERT(mock_led_state(PORT_LED_ON_OFF) == 1);

    /* 编程会话接收超时：回默认会话 */
    uint8_t p10[2] = { 0x10, 0x02 };
    mock_uart_tx_clear();
    uart_send_payload(Data_User_Defined, p10, 2); bl_process();
    mock_sys_tick(mock_sys_now() + BL_RX_TIMEOUT_MS + 100);
    bl_process();
    /* 会话已回默认：0x34 应被拒（0x22） */
    mock_uart_tx_clear();
    uint8_t p34[11] = { 0x34, 0x00, 0x00, 0,0,0,0, 0,0,0,1 };
    uart_send_payload(Data_User_Defined, p34, 11); bl_process();
    uint8_t r[16];
    int rn = find_uart_resp(r, sizeof(r));
    TEST_ASSERT(rn >= 3 && r[0] == 0x7F && r[2] == 0x22);
}

int main(void)
{
    printf("==== 统一 BootLoader 压力测试（UDS + IAP 双通道） ====\n");

    printf("[STRESS] 大固件 200KB UART（100B 块）...\n");
    stress_large_uart();
    printf("[STRESS] 多轮升级（1/7/32/100/126B 块）...\n");
    stress_repeat_uart();
    printf("[STRESS] 随机拆分注入（FIFO 分批）...\n");
    stress_split_inject();
    printf("[STRESS] 恶意/损坏帧恢复...\n");
    stress_malformed_uart();
    printf("[STRESS] 跨通道混用压测...\n");
    stress_cross_channel();
    printf("[STRESS] 超时恢复...\n");
    stress_timeout();

    printf("\n==== 压测结果：PASS %d, FAIL %d ====\n", test_passes, test_failures);
    return test_failures ? 1 : 0;
}
