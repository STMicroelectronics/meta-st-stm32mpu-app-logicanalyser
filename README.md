## meta-st-stm32mpu-app-logicanalyser

OpenEmbedded meta layer to install logic analyser demo which includes a user interface embedded in a web page.

## Installation of the meta layer

* Clone following git repositories into [your STM32MP1 Distribution path]/layers/meta-st/
   > cd [your STM32MP1 Distribution path]/layers/meta-st/ <br>
   > git clone https://github.com/STMicroelectronics/meta-st-stm32mpu-app-logicanalyser.git<br>
   

* Setup the build environement
   > source [your STM32MP1 Distribution path]/layers/meta-st/scripts/envsetup.sh
   > * Select your DISTRO (ex: openstlinux-weston)
   > * Select the  logicanalyser MACHINE (ex: stm32mp1-demo-logicanalyser)

* Build your image
   > bitbake st-image-weston
