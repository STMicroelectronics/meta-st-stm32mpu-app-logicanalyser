SUMMARY = "Example of how to build an external Linux kernel module"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://stm32_rpmsg_sdb.c \
           file://75-rpmsg-sdb.rules \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

do_install:append() {
    # udev rules for rpmsg-sdb
    install -d ${D}${sysconfdir}/udev/rules.d/
    install -m 0644 ${WORKDIR}/75-rpmsg-sdb.rules ${D}${sysconfdir}/udev/rules.d/75-rpmsg-sdb.rules
}
FILES:${PN} += "${sysconfdir}/udev/rules.d/"

RPROVIDES:${PN} += "kernel-module-rpmsg-sdb"
