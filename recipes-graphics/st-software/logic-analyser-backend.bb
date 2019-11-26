DESCRIPTION = "logicanalyser demo"
HOMEPAGE = ""
LICENSE = "GPLv2 & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libmicrohttpd"

SRC_URI = " file://backend.c;subdir=backend \
            file://backend_gtk.c;subdir=backend \
            file://la.css;subdir=backend \
            file://keyboard.c;subdir=backend \
            file://Makefile;subdir=backend \
            file://run_la.sh;subdir=backend \
            file://start_up_la.sh;subdir=backend \
            file://www;subdir=backend \
    "

S = "${WORKDIR}"

do_configure[noexec] = "1"

do_compile () {
    oe_runmake -C ${S}/backend/ || die "make backend C code failed"
}

do_install() {
    install -d 										${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/
    install -d 										${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/
    install -m 0755 ${B}/backend/backend 		    ${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/
    install -m 0755 ${B}/backend/backend_gtk 		${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/
    install -m 0755 ${B}/backend/la.css     		${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/
    install -m 0755 ${B}/backend/keyboard 			${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/
    install -m 0755 ${S}/backend/run_la.sh          ${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/
    install -m 0644 ${B}/backend/www/*              ${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/

    install -d ${D}/lib/firmware/
    #install -m 0644 ${STM32MP_LOGICANALYSER_BASE}/mx/STM32MP157C-DK2/demo-logic-analyser/firmware/rprochdrlawc01100.elf ${D}/lib/firmware/
    install -m 0644 ${STM32MP_LOGICANALYSER_BASE}/mx/STM32MP157C-DK2/demo-logic-analyser/firmware/how2eldb02110.elf ${D}/lib/firmware/

    # start at startup
    install -d ${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/weston-start-at-startup/
    install -m 0755 ${S}/backend/start_up_la.sh ${D}${STM32MP_USERFS_MOUNTPOINT_IMAGE}/weston-start-at-startup/
}

do_clean_be() {
  cd ${S}
  git status --porcelain | grep \?\? | cut -d ' ' -f 2 | xargs rm -rf
}
addtask do_clean_be after do_clean before do_cleansstate

PACKAGES =+ "${PN}-imageuserfs"
FILES_${PN} += "${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/"
FILES_${PN} += "${STM32MP_USERFS_MOUNTPOINT_IMAGE}/demo/la/bin/"
FILES_${PN} += "${STM32MP_USERFS_MOUNTPOINT_IMAGE}/weston-start-at-startup/"
FILES_${PN} += "/lib/firmware/"

RDEPENDS_${PN} += "libmicrohttpd"
