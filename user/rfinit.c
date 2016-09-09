/******************************************************************************
 * Copyright 2016 Vowstar
 *
 * FileName: init.c
 *
 * Description: System and user APP initialization.
 *
 * Modification history:
 *     2016/03/24, v1.0 create this file.
*******************************************************************************/

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

/******************************************************************************
* FunctionName : user_rf_cal_sector_set
* Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
*                We add this function to force users to set rf cal sector, since
*                we don't know which sector is free in user's application.
*                sector map for last several sectors : ABCCC
*                A : rf cal
*                B : rf init data
*                C : sdk parameters
* Parameters   : none
* Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR __attribute__((weak))
user_rf_cal_sector_set(void)
{
  enum flash_size_map size_map = system_get_flash_size_map();
  uint32 rf_cal_sec = 0;

  switch (size_map) {
    case FLASH_SIZE_4M_MAP_256_256:
      rf_cal_sec = 128 - 5;
      break;

    case FLASH_SIZE_8M_MAP_512_512:
      rf_cal_sec = 256 - 5;
      break;

    case FLASH_SIZE_16M_MAP_512_512:
    case FLASH_SIZE_16M_MAP_1024_1024:
      rf_cal_sec = 512 - 5;
      break;

    case FLASH_SIZE_32M_MAP_512_512:
    case FLASH_SIZE_32M_MAP_1024_1024:
      rf_cal_sec = 1024 - 5;
      break;

    default:
      rf_cal_sec = 0;
      break;
  }

  return rf_cal_sec;
}

void __attribute__((weak))
user_rf_pre_init(void)
{
  // Warning: IF YOU DON'T KNOW WHAT YOU ARE DOING, DON'T TOUCH THESE CODE

  // Control RF_CAL by esp_init_data_default.bin(0~127byte) 108 byte when wakeup
  // Will low current
  // system_phy_set_rfoption(0)；

  // Process RF_CAL when wakeup.
  // Will high current
  system_phy_set_rfoption(1);

  // Set Wi-Fi Tx Power, Unit: 0.25dBm, Range: [0, 82]
  system_phy_set_max_tpw(82);
}
