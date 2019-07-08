# Boot Mode Choice
# =========================================================================
# boot scheme
# =========================================================================
#BOOTSCHEME_LABELS ?= "basic"
BOOTSCHEME_LABELS ?= "trusted"
#BOOTSCHEME_LABELS ?= "optee"

# Boot Device Choice
# =========================================================================
# flashlayout
# =========================================================================
# Define the config labels to use to generate flashlayout file
FLASHLAYOUT_CONFIG_LABELS ?= "sdcard"

# Configs below only available with EVAL board
#FLASHLAYOUT_CONFIG_LABELS ?= "emmc"
#FLASHLAYOUT_CONFIG_LABELS ?= "nand-4-256"
#FLASHLAYOUT_CONFIG_LABELS ?= "nor-sdcard"
#FLASHLAYOUT_CONFIG_LABELS ?= "nor-emmc"
#FLASHLAYOUT_CONFIG_LABELS ?= "nor-nand-4-256"

# Board Type Choice
# =========================================================================
# Assign CubeMX Board DT and project name
CUBEMX_DTB ?= "stm32mp157c-demo-logic-analyser-mx"
CUBEMX_PROJECT ?= "STM32MP157C-DK2/demo-logic-analyser/DeviceTree/demo-logic-analyser"

# Define the original board devicetree
#CUBEMX_DT_FILE_BASE ?= "stm32mp157a-dk1"
#CUBEMX_DT_FILE_BASE ?= "stm32mp157c-dk2"
CUBEMX_DT_FILE_BASE ?= "stm32mp157c-dk2"
