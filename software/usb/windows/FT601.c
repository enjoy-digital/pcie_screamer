#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "FTD3XX.h"

#ifdef __linux__
#include <unistd.h>
#define my_sleep(x) sleep(x/1000)
#else
#define my_sleep(x) Sleep(x)
#endif

int fake_printf( const char * format, ... )
{
    return 0;
}

#ifdef DEBUG
#define my_printf printf
#else
#define my_printf fake_printf
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

int modify_config()
{
  FT_STATUS ftStatus = FT_OK;
  FT_HANDLE ftHandle;
  
  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
  if (FT_FAILED(ftStatus))
    {
      my_printf("File to create handle\n");
      return FALSE;
    }

  FT_60XCONFIGURATION oldconfig = {0};
  FT_60XCONFIGURATION oConfigurationData = {0};

  ftStatus = FT_GetChipConfiguration(ftHandle, &oldconfig);
  if (FT_FAILED(ftStatus))
    {
      my_printf("Get Chip Error\n");
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
      my_printf("Changing chip configuraiton !\n");
      ftStatus = FT_SetChipConfiguration(ftHandle, &oConfigurationData);
      if (FT_FAILED(ftStatus))
        {
          my_printf("Set chip error\n");
          FT_Close(ftHandle);
          return FALSE;
        }

      FT_Close(ftHandle);
      my_sleep(5000);
      ftHandle = NULL;
      ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &ftHandle);
      if (FT_FAILED(ftStatus))
        {
          my_printf("Get Chip Error2\n");
          return FALSE;
        }
    } else {
      my_printf("Chip configuration was ok, don't need to change !\n");
    }
    
  memset(&oConfigurationData, 0, sizeof(oConfigurationData));
  ftStatus = FT_GetChipConfiguration(ftHandle, &oConfigurationData);
  if (FT_FAILED(ftStatus))
    {
      my_printf("Get Chip Error2\n");
      FT_Close(ftHandle);
      return FALSE;
    }
  my_printf("\n\nNew config\n");
  //show_config(&oConfigurationData,1);
  if (oConfigurationData.OptionalFeatureSupport != CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN)
    {
      my_printf("Config is not ok\n");
      FT_Close(ftHandle);
      return FALSE;
    }
  
  if (oConfigurationData.FIFOMode != CONFIGURATION_FIFO_MODE_245 ||
      oConfigurationData.ChannelConfig != CONFIGURATION_CHANNEL_CONFIG_1 )
    {
      my_printf("Config is not ok\n");
      
      FT_Close(ftHandle);
      return FALSE;
    }
  
  FT_Close(ftHandle);
  return TRUE;
}

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

void show_config(FT_60XCONFIGURATION *a_pConfigurationData, int a_bRead)
{
  my_printf("\n");
  my_printf("\tDevice Descriptor\n");
  my_printf("\tVendorID                 : 0x%04X\n",      a_pConfigurationData->VendorID);
  my_printf("\tProductID                : 0x%04X\n",      a_pConfigurationData->ProductID);

  char ucTemp[128] = {0};
  char *pOffset = NULL;
  my_printf("\n");
  my_printf("\tString Descriptor\n");
  pOffset = (char*)&a_pConfigurationData->StringDescriptors[0];
  memset(ucTemp, 0, sizeof(ucTemp));
  if (pOffset[0]-2)
    {
      memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
  my_printf("\tManufacturer             : %s\n",      ucTemp);
  pOffset += pOffset[0];
  memset(ucTemp, 0, sizeof(ucTemp));
  if (pOffset[0]-2)
    {
      memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
  my_printf("\tProduct Description      : %s\n",      ucTemp);
  pOffset += pOffset[0];
  memset(ucTemp, 0, sizeof(ucTemp));
  if (pOffset[0]-2)
    {
      memcpy(ucTemp, &pOffset[2], pOffset[0]-2);
    }
  my_printf("\tSerial Number            : %s\n",      ucTemp);

  my_printf("\n");
  my_printf("\tConfiguration Descriptor\n");
  my_printf("\tPowerAttributes          : %s %s\n",       FT_IS_BUS_POWERED(a_pConfigurationData->PowerAttributes) ? "Self-powered " : "Bus-powered ",
                                                          FT_IS_REMOTE_WAKEUP_ENABLED(a_pConfigurationData->PowerAttributes) ? "Remote wakeup " : "");
  my_printf("\tPowerConsumption         : %d\n",          a_pConfigurationData->PowerConsumption);

  my_printf("\n");
  my_printf("\tData Transfer\n");
  my_printf("\tFIFOClock                : %s\n",          g_szFIFOClock[a_pConfigurationData->FIFOClock]);
  my_printf("\tFIFOMode                 : %s\n",          g_szFIFOMode[a_pConfigurationData->FIFOMode]);
  my_printf("\tChannelConfig            : %s\n",          g_szChannelConfig[a_pConfigurationData->ChannelConfig]);

  my_printf("\n");
  my_printf("\tOptional Features\n");
  my_printf("\tDisableCancelOnUnderrun  : %d\n",          (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN) >> 1);
  my_printf("\tNotificationEnabled      : %d (%d %d %d %d)\n",
              (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL) >> 2,
              (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH1) ? 1 : 0,
              (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH2) ? 1 : 0,
              (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH3) ? 1 : 0,
              (a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH4) ? 1 : 0
                                                          );
  my_printf("\tBatteryChargingEnabled   : %d\n",          a_pConfigurationData->OptionalFeatureSupport & CONFIGURATION_OPTIONAL_FEATURE_ENABLEBATTERYCHARGING);
  my_printf("\tBCGPIOPinConfig          : 0x%02X\n",      a_pConfigurationData->BatteryChargingGPIOConfig);

  if (a_bRead)
    {
      my_printf("\tFlashEEPROMDetection     : 0x%02X\n\n",a_pConfigurationData->FlashEEPROMDetection);
      my_printf("\t\tMemory Type            : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_ROM)                       ? "ROM"         : "Flash");
      my_printf("\t\tMemory Detected        : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_MEMORY_NOTEXIST)           ? "Not Exists"  : "Exists");
      my_printf("\t\tCustom Config Validity : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATA_INVALID)        ? "Invalid"     : "Valid");
      my_printf("\t\tCustom Config Checksum : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATACHKSUM_INVALID)  ? "Invalid"     : "Valid");
      my_printf("\t\tGPIO Input             : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)                ? "Used"        : "Ignore");
      my_printf("\t\tConfig Used            : %s\n",      a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_CUSTOM)                    ? "CUSTOM"      : "DEFAULT");
      if (a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT))
        {
          my_printf("\t\tGPIO Input             : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)  ? "Used"        : "Ignore");
          my_printf("\t\tGPIO 0                 : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_0)      ? "High"        : "Low");
          my_printf("\t\tGPIO 1                 : %s\n",  a_pConfigurationData->FlashEEPROMDetection & (1<<CONFIGURATION_FLASH_ROM_BIT_GPIO_1)      ? "High"        : "Low");
        }
    }

  my_printf("\n");
  my_printf("\tMSIO and GPIO configuration\n");
  my_printf("\tMSIOControl              : 0x%08X\n",      a_pConfigurationData->MSIO_Control);
  my_printf("\tGPIOControl              : 0x%08X\n",      a_pConfigurationData->GPIO_Control);

  my_printf("\n");
}

struct device {
  FT_HANDLE ftHandle;
};

int FT601_Open(struct device *dev)
{
  FT_STATUS ftStatus = FT_OK;

  ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &dev->ftHandle);
  if (FT_FAILED(ftStatus))
    {
      return FALSE;
    }
  return TRUE;
}

int FT601_Close(struct device *dev)
{
  FT_Close(dev->ftHandle);
  return TRUE;
}

int FT601_Read(struct device *dev, void *buf, size_t len)
{
  FT_STATUS ftStatus = FT_OK;
  unsigned long ulBytesRead = 0;
  
  ftStatus = FT_ReadPipe(dev->ftHandle, 0x82, buf, len, &ulBytesRead, NULL);
  my_printf("Status wr %d\n", ftStatus);
  return ulBytesRead;
}

int FT601_Write(struct device *dev, void *buf, size_t len)
{
  FT_STATUS ftStatus = FT_OK;
  unsigned long ulBytesWritten = 0;
  
  ftStatus = FT_WritePipe(dev->ftHandle, 0x02, buf, len, &ulBytesWritten, NULL);
  my_printf("Status wr %d\n", ftStatus);
  return ulBytesWritten;
}
