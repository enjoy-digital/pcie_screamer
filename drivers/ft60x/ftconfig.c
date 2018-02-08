#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ftd3xx.h"

#define NUM_CPUCLOCK 4
#define NUM_FIFOCLOCK 4
#define NUM_FIFOMODE 2
#define NUM_BCCONFIGURATION 4
#define NUM_CHANNELCONFIG 5

const char *g_szFIFOClock[NUM_FIFOCLOCK] = {
	"100 MHz",
	"66 MHz",
	"50 MHz",
	"40 MHz",
};

const char *g_szFIFOMode[NUM_FIFOMODE] = {
	"245 Mode",
	"600 Mode",
};

const char *g_szBCDConfiguration[NUM_BCCONFIGURATION] = {
	"00 (GPIO1=0, GPIO2=0)",
	"01 (GPIO1=0, GPIO2=1)",
	"10 (GPIO1=1, GPIO2=0)",
	"11 (GPIO1=1, GPIO2=1)",
};

const char *g_szChannelConfig[NUM_CHANNELCONFIG] = {
	"4 Channels",
	"2 Channels",
	"1 Channel",
	"1 OUT Pipe",
	"1 IN Pipe",
};

int show_config(FT_60XCONFIGURATION * a_pConfigurationData, int a_bRead)
{
	printf("\n");
	printf("\tDevice Descriptor\n");
	printf("\tVendorID                 : 0x%04X\n",
	       a_pConfigurationData->VendorID);
	printf("\tProductID                : 0x%04X\n",
	       a_pConfigurationData->ProductID);

	char ucTemp[128] = { 0 };
	char *pOffset = NULL;
	printf("\n");
	printf("\tString Descriptor\n");
	pOffset = (char *)&a_pConfigurationData->StringDescriptors[0];
	memset(ucTemp, 0, sizeof(ucTemp));
	if (pOffset[0] - 2) {
		memcpy(ucTemp, &pOffset[2], pOffset[0] - 2);
	}
	printf("\tManufacturer             : %s\n", ucTemp);
	pOffset += pOffset[0];
	memset(ucTemp, 0, sizeof(ucTemp));
	if (pOffset[0] - 2) {
		memcpy(ucTemp, &pOffset[2], pOffset[0] - 2);
	}
	printf("\tProduct Description      : %s\n", ucTemp);
	pOffset += pOffset[0];
	memset(ucTemp, 0, sizeof(ucTemp));
	if (pOffset[0] - 2) {
		memcpy(ucTemp, &pOffset[2], pOffset[0] - 2);
	}
	printf("\tSerial Number            : %s\n", ucTemp);

	printf("\n");
	printf("\tConfiguration Descriptor\n");
	printf("\tPowerAttributes          : %s %s\n",
	       FT_IS_BUS_POWERED(a_pConfigurationData->
				 PowerAttributes) ? "Self-powered " :
	       "Bus-powered ",
	       FT_IS_REMOTE_WAKEUP_ENABLED(a_pConfigurationData->
					   PowerAttributes) ? "Remote wakeup " :
	       "");
	printf("\tPowerConsumption         : %d\n",
	       a_pConfigurationData->PowerConsumption);

	printf("\n");
	printf("\tData Transfer\n");
	printf("\tFIFOClock                : %s\n",
	       g_szFIFOClock[a_pConfigurationData->FIFOClock]);
	printf("\tFIFOMode                 : %s\n",
	       g_szFIFOMode[a_pConfigurationData->FIFOMode]);
	printf("\tChannelConfig            : %s\n",
	       g_szChannelConfig[a_pConfigurationData->ChannelConfig]);

	printf("\n");
	printf("\tOptional Features\n");
	printf("\tDisableCancelOnUnderrun  : %d\n",
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN) >>
	       1);
	printf("\tNotificationEnabled      : %d (%d %d %d %d)\n",
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL)
	       >> 2,
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH1)
	       ? 1 : 0,
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH2)
	       ? 1 : 0,
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH3)
	       ? 1 : 0,
	       (a_pConfigurationData->
		OptionalFeatureSupport &
		CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCH4)
	       ? 1 : 0);
	printf("\tBatteryChargingEnabled   : %d\n",
	       a_pConfigurationData->
	       OptionalFeatureSupport &
	       CONFIGURATION_OPTIONAL_FEATURE_ENABLEBATTERYCHARGING);
	printf("\tBCGPIOPinConfig          : 0x%02X\n",
	       a_pConfigurationData->BatteryChargingGPIOConfig);

	if (a_bRead) {
		printf("\tFlashEEPROMDetection     : 0x%02X\n\n",
		       a_pConfigurationData->FlashEEPROMDetection);
		printf("\t\tMemory Type            : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_ROM)
		       ? "ROM" : "Flash");
		printf("\t\tMemory Detected        : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_MEMORY_NOTEXIST)
		       ? "Not Exists" : "Exists");
		printf("\t\tCustom Config Validity : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATA_INVALID)
		       ? "Invalid" : "Valid");
		printf("\t\tCustom Config Checksum : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_CUSTOMDATACHKSUM_INVALID)
		       ? "Invalid" : "Valid");
		printf("\t\tGPIO Input             : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)
		       ? "Used" : "Ignore");
		printf("\t\tConfig Used            : %s\n",
		       a_pConfigurationData->
		       FlashEEPROMDetection & (1 <<
					       CONFIGURATION_FLASH_ROM_BIT_CUSTOM)
		       ? "CUSTOM" : "DEFAULT");
		if (a_pConfigurationData->
		    FlashEEPROMDetection & (1 <<
					    CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT))
		{
			printf("\t\tGPIO Input             : %s\n",
			       a_pConfigurationData->
			       FlashEEPROMDetection & (1 <<
						       CONFIGURATION_FLASH_ROM_BIT_GPIO_INPUT)
			       ? "Used" : "Ignore");
			printf("\t\tGPIO 0                 : %s\n",
			       a_pConfigurationData->
			       FlashEEPROMDetection & (1 <<
						       CONFIGURATION_FLASH_ROM_BIT_GPIO_0)
			       ? "High" : "Low");
			printf("\t\tGPIO 1                 : %s\n",
			       a_pConfigurationData->
			       FlashEEPROMDetection & (1 <<
						       CONFIGURATION_FLASH_ROM_BIT_GPIO_1)
			       ? "High" : "Low");
		}
	}

	printf("\n");
	printf("\tMSIO and GPIO configuration\n");
	printf("\tMSIOControl              : 0x%08X\n",
	       a_pConfigurationData->MSIO_Control);
	printf("\tGPIOControl              : 0x%08X\n",
	       a_pConfigurationData->GPIO_Control);

	printf("\n");
}

int main()
{
	int in;
	unsigned char buf[0x100];
	int i;
	FT_60XCONFIGURATION oConfigurationData = { 0 };

	in = open("/dev/ft60x0", O_RDWR | O_CLOEXEC);
	ioctl(in, 0, &oConfigurationData);

	show_config(&oConfigurationData, 1);
	oConfigurationData.FIFOMode = CONFIGURATION_FIFO_MODE_245;
	oConfigurationData.ChannelConfig = CONFIGURATION_CHANNEL_CONFIG_1;
	oConfigurationData.OptionalFeatureSupport =
	    CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN |
	    CONFIGURATION_OPTIONAL_FEATURE_ENABLENOTIFICATIONMESSAGE_INCHALL;
	//oConfigurationData.OptionalFeatureSupport = 0; //CONFIGURATION_OPTIONAL_FEATURE_DISABLECANCELSESSIONUNDERRUN ;

	ioctl(in, 1, &oConfigurationData);
	close(in);

}
