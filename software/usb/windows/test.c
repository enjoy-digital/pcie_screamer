#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "FTD3XX.h"

#ifdef __linux__
#include <unistd.h>
#define my_sleep(x) sleep(x/1000)
#else
#define my_sleep(x) sleep(x)
#endif

#define FALSE 0
#define TRUE 1
static FT_60XCONFIGURATION oSaveConfigurationData = { 0 };

int save_config()
{
  FT_STATUS ftStatus = FT_OK;
  FT_HANDLE ftHandle;
  
  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
  if (FT_FAILED(ftStatus))
    {
      return FALSE;
    }
  
  ftStatus = FT_GetChipConfiguration(ftHandle, &oSaveConfigurationData);
  if (FT_FAILED(ftStatus))
    {
      FT_Close(ftHandle);
      return FALSE;
    }
  
  FT_Close(ftHandle);
  ftHandle = NULL;
  
  return TRUE;
}

int revert_config()
{
  FT_STATUS ftStatus = FT_OK;
  FT_HANDLE ftHandle;
  
  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
  if (FT_FAILED(ftStatus))
    {
      return FALSE;
    }
  
  ftStatus = FT_SetChipConfiguration(ftHandle, &oSaveConfigurationData);
  if (FT_FAILED(ftStatus))
    {
      FT_Close(ftHandle);
      return FALSE;
    }
  
  FT_Close(ftHandle);
  ftHandle = NULL;
  
  return TRUE;
}





static int get_device_lists(void)
{
        DWORD count;
        FT_DEVICE_LIST_INFO_NODE nodes[16];

        FT_CreateDeviceInfoList(&count);
        printf("Total %u device(s)\r\n", count);
        if (!count)
                return 0;

        if (FT_OK != FT_GetDeviceInfoList(nodes, &count))
                return 0;
        return 1;
}

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

#define BUFFER_SIZE 512
int loopback()
{
  int i;
  FT_STATUS ftStatus = FT_OK;
  FT_HANDLE ftHandle;
  char bResult = TRUE;
  
  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
  if (FT_FAILED(ftStatus))
    {
      return FALSE;
    }
  
  DWORD dwNumIterations = 1;
  for (i=0; i<dwNumIterations; i++)
    {
      char *acWriteBuf;
      acWriteBuf = malloc(BUFFER_SIZE);
      memset(acWriteBuf, 0xff, BUFFER_SIZE);
      unsigned long ulBytesWritten = 0;
      printf("\tWriting %d bytes!\n", BUFFER_SIZE);
      ftStatus = FT_WritePipe(ftHandle, 0x02, acWriteBuf, BUFFER_SIZE, &ulBytesWritten, NULL);
      printf("\tWriting %d bytes DONE!\n", ulBytesWritten);
      if (FT_FAILED(ftStatus))
        {
	  bResult = FALSE;
	  goto exit;
        }
      if (ulBytesWritten != BUFFER_SIZE)
        {
	  bResult = FALSE;
	  goto exit;
        }
      
      
      char *acReadBuf;
      acReadBuf = malloc(BUFFER_SIZE);
      unsigned long ulBytesRead = 0;
      printf("\tReading %d bytes!\n", BUFFER_SIZE);
      ftStatus = FT_ReadPipe(ftHandle, 0x82, acReadBuf, BUFFER_SIZE, &ulBytesRead, NULL);
      printf("\tReading %d bytes DONE!\n", ulBytesRead);
      if (FT_FAILED(ftStatus))
        {
	  bResult = FALSE;
	  goto exit;
        }
      if (ulBytesRead != BUFFER_SIZE)
        {
	  bResult = FALSE;
	  goto exit;
        }
      
      for(i = 0; i < ulBytesRead; i++)
	printf("%02x ", acReadBuf[i]);
      if (memcmp(acWriteBuf, acReadBuf, ulBytesRead))
        {
	  bResult = FALSE;
	  goto exit;
        }
    }
  
 exit:
  FT_Close(ftHandle);
  return bResult;

}





int modify_config()
{
  FT_STATUS ftStatus = FT_OK;
  FT_HANDLE ftHandle;
  
  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
  if (FT_FAILED(ftStatus))
    {
      printf("File to create handle\n");
      return FALSE;
      
    }

  FT_60XCONFIGURATION oldconfig = {0};
  FT_60XCONFIGURATION oConfigurationData = {0};

  ftStatus = FT_GetChipConfiguration(ftHandle, &oldconfig);
  if (FT_FAILED(ftStatus))
    {
      printf("Get Chip Error\n");
      FT_Close(ftHandle);
      return FALSE;
    }

  //show_config(&oldconfig,1);
  memcpy(&oConfigurationData, &oldconfig, sizeof(oConfigurationData));
  
  oConfigurationData.FIFOMode = CONFIGURATION_FIFO_MODE_245;
  oConfigurationData.ChannelConfig = CONFIGURATION_CHANNEL_CONFIG_1;

  oConfigurationData.OptionalFeatureSupport = CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN;

  if(memcmp(&oldconfig, &oConfigurationData,  sizeof(oConfigurationData)))
    {
      printf("Changing chip configuraiton !\n");
      ftStatus = FT_SetChipConfiguration(ftHandle, &oConfigurationData);
      if (FT_FAILED(ftStatus))
	{
	  printf("set chip error\n");
	  FT_Close(ftHandle);
	  return FALSE;
	}
      
      FT_Close(ftHandle);
      my_sleep(5000);
      ftHandle = NULL;
      ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
      if (FT_FAILED(ftStatus))
	{
	  printf("Get Chip Error2\n");
	  return FALSE;
	}
    } else {

      printf("Chip configuration was ok, don't need to chanrge !\n");
    }
    
  memset(&oConfigurationData, 0, sizeof(oConfigurationData));
  ftStatus = FT_GetChipConfiguration(ftHandle, &oConfigurationData);
  if (FT_FAILED(ftStatus))
    {
      printf("Get Chip Error2\n");
      FT_Close(ftHandle);
      return FALSE;
    }
  printf("\n\nnew config\n");
  //show_config(&oConfigurationData,1);
  if (oConfigurationData.OptionalFeatureSupport != CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN)
    {
      printf("Config is not ok\n");
      FT_Close(ftHandle);
      return FALSE;
    }
  
  if (oConfigurationData.FIFOMode != CONFIGURATION_FIFO_MODE_245 ||
      oConfigurationData.ChannelConfig != CONFIGURATION_CHANNEL_CONFIG_1 )
    {
      printf("Config is not ok\n");
      
      FT_Close(ftHandle);
      return FALSE;
    }
  
  FT_Close(ftHandle);
  return TRUE;
}





#define FALSE 0
#define TRUE 1

#define NUM_CPUCLOCK 4
#define NUM_FIFOCLOCK 4
#define NUM_FIFOMODE 2
#define NUM_BCCONFIGURATION 4
#define NUM_CHANNELCONFIG 5



const char *g_szFIFOClock[NUM_FIFOCLOCK] =
  {
    "100 MHz",
    "66 MHz",
    "50 MHz",
    "40 MHz",
  };

const char *g_szFIFOMode[NUM_FIFOMODE] =
  {
    "245 Mode",
    "600 Mode",
  };

const char *g_szBCDConfiguration[NUM_BCCONFIGURATION] =
  {
    "00 (GPIO1=0, GPIO2=0)",
    "01 (GPIO1=0, GPIO2=1)",
    "10 (GPIO1=1, GPIO2=0)",
    "11 (GPIO1=1, GPIO2=1)",
  };

const char *g_szChannelConfig[NUM_CHANNELCONFIG] =
  {
    "4 Channels",
    "2 Channels",
    "1 Channel",
    "1 OUT Pipe",
    "1 IN Pipe",
  };


int show_config(FT_60XCONFIGURATION *a_pConfigurationData, int a_bRead)
{


    printf("\n");
    printf("\tDevice Descriptor\n");
    printf("\tVendorID                 : 0x%04X\n",      a_pConfigurationData->VendorID);
    printf("\tProductID                : 0x%04X\n",      a_pConfigurationData->ProductID);

    char ucTemp[128] = {0};
    char *pOffset = NULL;
        printf("\n");
        printf("\tString Descriptor\n");
    pOffset = (char*)&a_pConfigurationData->StringDescriptors[0];
    memset(ucTemp, 0, sizeof(ucTemp));
    if (pOffset[0]-2)
    {
        memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
    printf("\tManufacturer             : %s\n",      ucTemp);
    pOffset += pOffset[0];
    memset(ucTemp, 0, sizeof(ucTemp));
    if (pOffset[0]-2)
    {
        memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
    printf("\tProduct Description      : %s\n",      ucTemp);
    pOffset += pOffset[0];
    memset(ucTemp, 0, sizeof(ucTemp));
    if (pOffset[0]-2)
    {
        memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
    printf("\tSerial Number            : %s\n",      ucTemp);

        printf("\n");
        printf("\tConfiguration Descriptor\n");
    printf("\tPowerAttributes          : %s %s\n",       FT_IS_BUS_POWERED(a_pConfigurationData->PowerAttributes) ? "Self-powered " : "Bus-powered ",
                                                            FT_IS_REMOTE_WAKEUP_ENABLED(a_pConfigurationData->PowerAttributes) ? "Remote wakeup " : "");
    printf("\tPowerConsumption         : %d\n",          a_pConfigurationData->PowerConsumption);

        printf("\n");
        printf("\tData Transfer\n");
    printf("\tFIFOClock                : %s\n",          g_szFIFOClock[a_pConfigurationData->FIFOClock]);
    printf("\tFIFOMode                 : %s\n",          g_szFIFOMode[a_pConfigurationData->FIFOMode]);
    printf("\tChannelConfig            : %s\n",          g_szChannelConfig[a_pConfigurationData->ChannelConfig]);

        printf("\n");
        printf("\tOptional Features\n");
    printf("\tDisableCancelOnUnderrun  : %d\n",          (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN) >> 1);
    printf("\tNotificationEnabled      : %d (%d %d %d %d)\n",
                (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL) >> 2,
                (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH1) ? 1 : 0,
                (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH2) ? 1 : 0,
                (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH3) ? 1 : 0,
                (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH4) ? 1 : 0
                                                            );
    printf("\tBatteryChargingEnabled   : %d\n",          a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLEBATTERYCHARGING);
    printf("\tBCGPIOPinConfig          : 0x%02X\n",      a_pConfigurationData->BatteryChargingGPIOConfig);

    if (a_bRead)
    {
        printf("\tFlashEEPROMDetection     : 0x%02X\n\n",a_pConfigurationData->FlashEEPROMDetection);
        printf("\t\tMemory Type            : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_ROM)                       ? "ROM"         : "Flash");
        printf("\t\tMemory Detected        : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_MEMORY_NOTEXIST)           ? "Not Exists"  : "Exists");
        printf("\t\tCustom Config Validity : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATA_INVALID)        ? "Invalid"     : "Valid");
        printf("\t\tCustom Config Checksum : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATACHKSUM_INVALID)  ? "Invalid"     : "Valid");
        printf("\t\tGPIO Input             : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)                ? "Used"        : "Ignore");
        printf("\t\tConfig Used            : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOM)                    ? "CUSTOM"      : "DEFAULT");
        if (a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT))
        {
            printf("\t\tGPIO Input             : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)  ? "Used"        : "Ignore");
            printf("\t\tGPIO 0                 : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_0)      ? "High"        : "Low");
            printf("\t\tGPIO 1                 : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_1)      ? "High"        : "Low");
        }
    }

        printf("\n");
        printf("\tMSIO and GPIO configuration\n");
    printf("\tMSIOControl              : 0x%08X\n",      a_pConfigurationData->MSIO_Control);
    printf("\tGPIOControl              : 0x%08X\n",      a_pConfigurationData->GPIO_Control);

    printf("\n");
}


int main(int argc,char *argv[])
{

  int ret;

  ret = modify_config();
  printf("ret: %d\n", ret);

  ret = loopback();
  printf("ret: %d\n", ret);

	return 0;
}
