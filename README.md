# meta-st-stm32mpu-app-logicanalyser

## Overview
This layer contains the source code for the [**logic analyzer example**](https://wiki.st.com/stm32mpu/wiki/How_to_exchange_data_buffers_with_the_coprocessor).

This example can be executed on the **STM32MP135C/F-DK2 Discovery kit** boards.

This layer is linked with the [**logicanalyser** project](https://github.com/STMicroelectronics/logicanalyser) that contains the source code of the STM32MP157 Cortex-M4 firmware for the logic analyzer example.

This version is based on the [**STM32MP1-ecosystem-v4.0.0 ecosystem release**](https://wiki.st.com/stm32mpu/wiki/STM32_MPU_ecosystem_release_note_-_v4.0.0) of the STM32MPU Embedded Software distribution.

The "mx machine" is not used: instead, a patch on the Linux device tree is provided.

## Table of Contents
1. Documentation
2. HW requirements
3. SW requirements
4. How to add the meta-st-stm32mpu-app-logicanalyser layer to your build?
5. How to use the example?
6. Extra explanations
7. Limitations - issues

## 1. Documentation
- ["How to exchange data buffers with the coprocessor"](https://wiki.st.com/stm32mpu/wiki/How_to_exchange_data_buffers_with_the_coprocessor) wiki article
- [STM32MP157x-DKx Discovery kit schematics](https://wiki.st.com/stm32mpu/wiki/STM32MP15_resources#MB1272_schematics) through the STM32MP15 resources wiki article
- [STM32MP157x-DKx Discovery kit hardware description wiki article](https://wiki.st.com/stm32mpu/wiki/STM32MP157x-DKx_-_hardware_description)

## 2. HW requirements
- STM32MP157C/F-DK2 Discovery kit board.
- No hardware modification is needed for this example.

## 3. SW requirements
- This example uses [GTK](https://www.gtk.org/) for building the UI.

## 4. How to add the meta-st-stm32mpu-app-logicanalyser layer to your build?
- Create the working directory to install the baseline:
```
PC $> cd <working_directory_path>
```
- Fetch the **openstlinux-5.15-yocto-kirkstone-mp1-v22.06.15** OpenSTLinux version delivered with the STM32MP1-ecosystem-v4.0.0 release as explained [here](https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package).
- Add the meta-st-stm32mpu-app-logicanalyser layer (**kirkstone** branch):
```
PC $> cd <working_directory_path>/layers/meta-st
PC $> git clone https://github.com/STMicroelectronics/meta-st-stm32mpu-app-logicanalyser.git -b kirkstone
```
- Configure the build environment as explained [here](https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package#Initializing_the_OpenEmbedded_build_environment):
  - DISTRO  = **openstlinux-weston**
  - MACHINE = **stm32mp1**
  - IMAGE   = **st-image-weston**
```
PC $> cd <working_directory_path>
PC $> DISTRO=openstlinux-weston MACHINE=stm32mp1 source layers/meta-st/scripts/envsetup.sh
```
- Add the meta-st-stm32mpu-app-logicanalyser  layer to the image to build:
```
PC $> pwd
<working_directory_path>/build-openstlinuxweston-stm32mp1
PC $> bitbake-layers add-layer ../layers/meta-st/meta-st-stm32mpu-app-logicanalyser
```
- Build the image as explained [here](https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package#Building_the_OpenSTLinux_distribution):
```
PC $> bitbake st-image-weston
```
- Populate the SD card thanks to the **STM32CubeProgrammer** tool as explained [here](https://wiki.st.com/stm32mpu/wiki/STM32MP13_Discovery_kits_-_Starter_Package#Image_flashing) or to the **create_sdcard_from_flashlayout.sh** script as explained [here](https://wiki.st.com/stm32mpu/wiki/How_to_populate_the_SD_card_with_dd_command).

## 5. How to use the example?
1. Press either the "USER1" button or the "USER2" button to start (resp. to stop) the example
2. Select the sampling frequency (4 MHz per default)
3. Start the sampling:
- For high data rate (more than 5 MHz sampling), it relies on a SDB Linux driver which provides DDR buffers allocations, and DDR DMA transfers<br>
- For low data rate (less or equal to 5MHz sampling), it relies on virtual UART over RPMSG<br>
- Data compression algorithm is done on Cortex-M4 side<br>
- Compressed buffers are transfered to DDR by DMA or virtual UART<br>
- In order to insure dynamic input data on PE8..12, these ports are initialized as output. Values are changed every 23 times<br>
- On user interface, refresh is done every new MB of compressed data

## 6. Extra explanations
For more details, see the ["How to exchange data buffers with the coprocessor"](https://wiki.st.com/stm32mpu/wiki/How_to_exchange_data_buffers_with_the_coprocessor) wiki article.

## 7. Limitations - issues
Nothing to report.
