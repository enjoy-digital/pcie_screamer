#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "ftd3xx.h"

static bool ft600_mode;
static uint8_t channel;
static long in_cnt;
static long out_cnt;

static void get_version(void)
{
        DWORD dwVersion;

        FT_GetDriverVersion(NULL, &dwVersion);
        printf("Driver version:%d.%d.%d.%d\r\n", dwVersion >> 24,
                        (uint8_t)(dwVersion >> 16), (uint8_t)(dwVersion >> 8),
                        dwVersion & 0xFF);

        FT_GetLibraryVersion(&dwVersion);
        printf("Library version:%d.%d.%d\r\n", dwVersion >> 24,
                        (uint8_t)(dwVersion >> 16), dwVersion & 0xFFFF);
}

static void get_vid_pid(FT_HANDLE handle)
{
        WORD vid, pid;

        if (FT_OK != FT_GetVIDPID(handle, &vid, &pid))
                return;
        printf("VID:%04X PID:%04X\r\n", vid, pid);
}

static void turn_off_all_pipes(void)
{
        FT_TRANSFER_CONF conf;

        memset(&conf, 0, sizeof(FT_TRANSFER_CONF));
        conf.wStructSize = sizeof(FT_TRANSFER_CONF);
        conf.pipe[FT_PIPE_DIR_IN].fPipeNotUsed = true;
        conf.pipe[FT_PIPE_DIR_OUT].fPipeNotUsed = true;
        for (DWORD i = 0; i < 4; i++)
                FT_SetTransferParams(&conf, i);
}

static bool get_device_lists(void)
{
        DWORD count;
        FT_DEVICE_LIST_INFO_NODE nodes[16];

        FT_CreateDeviceInfoList(&count);
        printf("Total %u device(s)\r\n", count);
        if (!count)
                return false;

        if (FT_OK != FT_GetDeviceInfoList(nodes, &count))
                return false;
        return true;
}

static bool set_channel_config(void)
{
        FT_HANDLE handle;
        FT_60XCONFIGURATION cfg;
        DWORD dwVersion;
        bool current_is_600mode;
        bool needs_update = false;
        bool rev_a_chip;

        /* Must turn off all pipes before changing chip configuration */
        turn_off_all_pipes();

        FT_GetDeviceInfoDetail(0, NULL, NULL, NULL, NULL, NULL, NULL, &handle);

        if (!handle)
                return false;

        FT_GetFirmwareVersion(handle, &dwVersion);
        rev_a_chip = dwVersion <= 0x105;

        get_vid_pid(handle);

        if (FT_OK != FT_GetChipConfiguration(handle, &cfg)) {
                printf("Failed to get chip conf\r\n");
                goto _Exit;
        }
        if (cfg.OptionalFeatureSupport &
                        CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL) {
                /* Notification in D3XX for Linux is implemented at OS level
                 * Turn off notification feature in firmware */
                cfg.OptionalFeatureSupport &=
                        ~CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL;
                needs_update = true;
                printf("Turn off firmware notification feature\r\n");
        }

        if (!(cfg.OptionalFeatureSupport &
                        CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN)) {
                /* Turn off feature not supported by D3XX for Linux */
                cfg.OptionalFeatureSupport |=
                        CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN;
                needs_update = true;
                printf("disable cancel session on FIFO underrun 0x%X\r\n",
                                cfg.OptionalFeatureSupport);
        }

        if (cfg.FIFOMode == CONFIGURATION_FIFO_MODE_245) {
                printf("FIFO is running at FT245 mode\r\n");
                current_is_600mode = false;
        } else if (cfg.FIFOMode == CONFIGURATION_FIFO_MODE_600) {
                printf("FIFO is running at FT600 mode\r\n");
                current_is_600mode = true;
        } else {
                printf("FIFO is running at unknown mode\r\n");
                goto _Exit;
        }

        UCHAR ch;

        if (channel == 1) {
                if (out_cnt == 0)
                        ch = CONFIGURATION_CHANNEL_CONFIG_1_INPIPE;
                else if (in_cnt == 0)
                        ch = CONFIGURATION_CHANNEL_CONFIG_1_OUTPIPE;
                else
                        ch = CONFIGURATION_CHANNEL_CONFIG_1;
        } else if (channel == 3 || channel == 4)
                ch = CONFIGURATION_CHANNEL_CONFIG_4;
        else if (channel == 2)
                ch = CONFIGURATION_CHANNEL_CONFIG_2;
        else
                goto _Exit;

        if (cfg.ChannelConfig == ch && current_is_600mode == ft600_mode &&
                        !needs_update)
                goto _Exit;
        cfg.ChannelConfig = ch;
        cfg.FIFOMode = ft600_mode ? CONFIGURATION_FIFO_MODE_600 :
                CONFIGURATION_FIFO_MODE_245;
        if (FT_OK != FT_SetChipConfiguration(handle, &cfg)) {
                printf("Failed to set chip conf\r\n");
        } else
                printf("Configuration changed CH:%d ft600:%d\r\n", ch, ft600_mode);
        FT_Close(handle);

        sleep(3);
        get_device_lists();
        return rev_a_chip;

_Exit:
        FT_Close(handle);
        return rev_a_chip;
}

static void show_help(const char *bin)
{
        printf("Usage: %s <write length> <read length> <channel> [mode]\r\n", bin);
        printf("  length: in bytes\r\n");
        printf("  channel: which channel to use, must be 1 for FT245 mode, [1, 4] for FT600 mode\r\n");
        printf("  mode: set fifo mode. 0 = FT245 mode (default), 1 = FT600 mode\r\n");
}

static void turn_off_thread_safe(void)
{
        FT_TRANSFER_CONF conf;

        memset(&conf, 0, sizeof(FT_TRANSFER_CONF));
        conf.wStructSize = sizeof(FT_TRANSFER_CONF);
        conf.pipe[FT_PIPE_DIR_IN].fNonThreadSafeTransfer = true;
        conf.pipe[FT_PIPE_DIR_OUT].fNonThreadSafeTransfer = true;
        for (DWORD i = 0; i < 4; i++)
                FT_SetTransferParams(&conf, i);
}


static bool validate_arguments(int argc, char *argv[])
{
        if (argc != 5)
                return false;

        int val = atoi(argv[4]);
        if (val != 0 && val != 1)
                return false;
        ft600_mode = (bool)val;

        in_cnt = atol(argv[1]);
        out_cnt = atol(argv[2]);
        if (in_cnt == 0 && out_cnt == 0)
                return false;

        if (!ft600_mode)
                channel = 1;
        else {
                channel = atoi(argv[3]);
                if (channel < 1 || channel > 4)
                        return false;
        }

        return true;
}

int main(int argc, char *argv[])
{

  FT_SetDebug(NULL, 10);


        int i;
        get_version();

        if (!validate_arguments(argc, argv)) {
                show_help(argv[0]);
                return 1;
        }

        if (!get_device_lists())
                return 1;

        bool rev_a_chip = set_channel_config();

        /* Must be called before FT_Create is called */
        turn_off_thread_safe();

        FT_HANDLE handle;

        FT_Create(0, FT_OPEN_BY_INDEX, &handle);

        if (!handle) {
                printf("Failed to create device\r\n");
                return -1;
        }
        printf("Device created\r\n");

        uint8_t *in_buf = (uint8_t *)malloc(in_cnt);
        uint8_t *out_buf = (uint8_t *)malloc(out_cnt);
        DWORD count;


        // write 0x12345678 @ 0x40000000

        // preamble
        out_buf[3] = 0x5a;
        out_buf[2] = 0xa5;
        out_buf[1] = 0x5a;
        out_buf[0] = 0xa5;

        // reserved
        out_buf[7] = 0x00;
        out_buf[6] = 0x00;
        out_buf[5] = 0x00;
        // destination
        out_buf[4] = 0x00;

        // length
        out_buf[11] = 0x00;
        out_buf[10] = 0x00;
        out_buf[9]  = 0x00;
        out_buf[8]  = 0x14;

        // etherbone packet
        // 1
        out_buf[12] = 0x4e;
        out_buf[13] = 0x6f;
        out_buf[14] = 0x10;
        out_buf[15] = 0x44;
        // 2
        out_buf[16] = 0x00;
        out_buf[17] = 0x00;
        out_buf[18] = 0x00;
        out_buf[19] = 0x00;
        // 3
        out_buf[20] = 0x00;
        out_buf[21] = 0x0f;
        out_buf[22] = 0x01;
        out_buf[23] = 0x00;
        // 4
        out_buf[24] = 0x40;
        out_buf[25] = 0x00;
        out_buf[26] = 0x00;
        out_buf[27] = 0x00;
        // 5
        out_buf[28] = 0x12;
        out_buf[29] = 0x34;
        out_buf[30] = 0x56;
        out_buf[31] = 0x78;

        if (FT_OK != FT_WritePipeEx(handle, channel - 1, out_buf, 32,
                                &count, 0xFFFFFFFF)) {
                printf("Failed to write\r\n");
                goto _Exit;
        }
        printf("Wrote %d bytes\r\n", count);

        // read @ 0x40000000

        // preamble
        out_buf[3] = 0x5a;
        out_buf[2] = 0xa5;
        out_buf[1] = 0x5a;
        out_buf[0] = 0xa5;

        // reserved
        out_buf[7] = 0x00;
        out_buf[6] = 0x00;
        out_buf[5] = 0x00;
        // destination
        out_buf[4] = 0x00;

        // length
        out_buf[11] = 0x00;
        out_buf[10] = 0x00;
        out_buf[9]  = 0x00;
        out_buf[8]  = 0x14;

        // etherbone packet
        // 1
        out_buf[12] = 0x4e;
        out_buf[13] = 0x6f;
        out_buf[14] = 0x10;
        out_buf[15] = 0x44;
        // 2
        out_buf[16] = 0x00;
        out_buf[17] = 0x00;
        out_buf[18] = 0x00;
        out_buf[19] = 0x00;
        // 3
        out_buf[20] = 0x00;
        out_buf[21] = 0x0f;
        out_buf[22] = 0x00;
        out_buf[23] = 0x01;
        // 4
        out_buf[24] = 0x00;
        out_buf[25] = 0x00;
        out_buf[26] = 0x00;
        out_buf[27] = 0x00;
        // 5
        out_buf[28] = 0x40;
        out_buf[29] = 0x00;
        out_buf[30] = 0x00;
        out_buf[31] = 0x00;

        if (FT_OK != FT_WritePipeEx(handle, channel - 1, out_buf, 32,
                                &count, 0xFFFFFFFF)) {
                printf("Failed to write\r\n");
                goto _Exit;
        }
        printf("Wrote %d bytes\r\n", count);

        memset(in_buf, 0, in_cnt);
        if (FT_OK != FT_ReadPipeEx(handle, channel - 1, in_buf, 32,
                                &count, 0xFFFFFFFF)) {
                printf("Failed to read\r\n");
                goto _Exit;
        }
        printf("Read %d bytes\r\n", count);
        for(i = 0; i < count; i++)
          printf("%02x ", in_buf[i]);
_Exit:
        free(in_buf);
        free(out_buf);

        /* Workaround for FT600/FT601 Rev.A device: Stop session before exit */
        if (rev_a_chip)
                FT_ResetDevicePort(handle);
        FT_Close(handle);
        return 0;
}
