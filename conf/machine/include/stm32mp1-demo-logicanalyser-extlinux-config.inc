# =========================================================================
# EXTLINUX is dynamic according to FLASHLAYOUT CONFIG set
# =========================================================================
# Configure only original CubeMX machine for sdcard in order to add specific
# CubeMX label(s) to this default configuration
UBOOT_EXTLINUX_DEVICEFLAG_sdcard = "${CUBEMX_DT_FILE_BASE}"

# Configs below only available with EVAL board
UBOOT_EXTLINUX_DEVICEFLAG_emmc = "${CUBEMX_DT_FILE_BASE}"
UBOOT_EXTLINUX_DEVICEFLAG_nand-4-256 = "${CUBEMX_DT_FILE_BASE}"
UBOOT_EXTLINUX_DEVICEFLAG_nor-sdcard = "${CUBEMX_DT_FILE_BASE}"
UBOOT_EXTLINUX_DEVICEFLAG_nor-emmc = "${CUBEMX_DT_FILE_BASE}"
UBOOT_EXTLINUX_DEVICEFLAG_nor-nand-4-256 = "${CUBEMX_DT_FILE_BASE}"


############################################################################################
# Define overrides for CUBEMX labels
UBOOT_EXTLINUX_FDT_cubemx-sdcard = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-sdcard = "${EXTLINUX_ROOT_SDCARD}"

# Configs below only available with EVAL board
UBOOT_EXTLINUX_FDT_cubemx-emmc = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-emmc = "${EXTLINUX_ROOT_EMMC}"

UBOOT_EXTLINUX_FDT_cubemx-nand-4-256 = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-nand-4-256 = "${EXTLINUX_ROOT_NAND}"

UBOOT_EXTLINUX_FDT_cubemx-nor-sdcard = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-nor-sdcard = "${EXTLINUX_ROOT_NORSDCARD}"

UBOOT_EXTLINUX_FDT_cubemx-nor-emmc = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-nor-emmc = "${EXTLINUX_ROOT_NOREMMC}"

UBOOT_EXTLINUX_FDT_cubemx-nor-nand-4-256 = "/${CUBEMX_DTB}.dtb"
UBOOT_EXTLINUX_ROOT_cubemx-nor-nand-4-256 = "${EXTLINUX_ROOT_NAND}"


############################################################################################
#EVAL BOARD
# Append CUBEMX label to the original configured target (if cubemx devicetree exists)
UBOOT_EXTLINUX_LABELS_mp157c-ev1_sdcard += "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"
UBOOT_EXTLINUX_LABELS_mp157c-ev1_emmc += "${@'cubemx-emmc' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"
UBOOT_EXTLINUX_LABELS_mp157c-ev1_nand-4-256 += "${@'cubemx-nand-4-256' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"
UBOOT_EXTLINUX_LABELS_mp157c-ev1_nor-sdcard += "${@'cubemx-nor-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"
UBOOT_EXTLINUX_LABELS_mp157c-ev1_nor-emmc += "${@'cubemx-nor-emmc' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"
UBOOT_EXTLINUX_LABELS_mp157c-ev1_nor-nand-4-256 += "${@'cubemx-nor-nand-4-256' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"

# Set default boot config to CUBEMX label (if cubemx devicetree exists)
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_sdcard = "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-sdcard'}"
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_emmc = "${@'cubemx-emmc' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-emmc'}"
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_nand-4-256 = "${@'cubemx-nand-4-256' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-nand-4-256'}"
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_nor-sdcard = "${@'cubemx-nor-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-nor-sdcard'}"
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_nor-emmc = "${@'cubemx-nor-emmc' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-nor-emmc'}"
UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-ev1_nor-nand-4-256 = "${@'cubemx-nor-nand-4-256' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-nor-nand-4-256'}"


############################################################################################
#DK2 BOARD
UBOOT_EXTLINUX_LABELS_mp157c-dk2_sdcard += "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"

UBOOT_EXTLINUX_DEFAULT_LABEL_mp157c-dk2_sdcard = "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-sdcard'}"


############################################################################################
#DK1 BOARD
UBOOT_EXTLINUX_LABELS_mp157a-dk1_sdcard += "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else ''}"

UBOOT_EXTLINUX_DEFAULT_LABEL_mp157a-dk1_sdcard = "${@'cubemx-sdcard' if os.path.isfile('${CUBEMX_DTB_PATH_pn-u-boot-stm32mp}/${CUBEMX_DTB}.dts') else '${CUBEMX_DT_FILE_BASE}-sdcard'}"
