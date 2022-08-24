SUMMARY = "logicanalyser demo"
HOMEPAGE = ""
LICENSE = "GPL-2.0-only & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "gtk+3"

inherit pkgconfig

SRC_URI = " file://backend.c;subdir=backend \
            file://la.css;subdir=backend \
            file://keyboard.c;subdir=backend \
            file://Makefile;subdir=backend \
            file://run_la.sh;subdir=backend \
            file://start_up_la.sh;subdir=backend \
    "

S = "${WORKDIR}"

do_compile () {
    oe_runmake -C ${S}/backend/ || die "make backend C code failed"
}

do_install() {
    install -d 										${D}/usr/local/demo/la/
    install -d 										${D}/usr/local/demo/la/bin/
    install -m 0755 ${B}/backend/backend    		${D}/usr/local/demo/la/bin/
    install -m 0755 ${B}/backend/la.css     		${D}/usr/local/demo/la/bin/
    install -m 0755 ${B}/backend/keyboard 			${D}/usr/local/demo/la/bin/
    install -m 0755 ${B}/backend/run_la.sh          ${D}/usr/local/demo/la/

    install -d ${D}/lib/firmware/
    install -m 0644 ${STM32MPU_LOGICANALYSER_BASE}/recipes-graphics/st-software/logic-analyser-firmware/how2eldb04140.elf ${D}/lib/firmware/

    # start at startup
    install -d ${D}/usr/local/weston-start-at-startup/
    install -m 0755 ${B}/backend/start_up_la.sh ${D}/usr/local/weston-start-at-startup/
}

FILES:${PN} += "/usr/local/demo/la/"
FILES:${PN} += "/usr/local/demo/la/bin/"
FILES:${PN} += "/usr/local/weston-start-at-startup/"
FILES:${PN} += "/lib/firmware/"
