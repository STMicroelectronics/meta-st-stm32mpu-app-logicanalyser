PACKAGE_INSTALL += "\
    ${@bb.utils.contains('DISTRO_FEATURES', 'wayland', 'logic-analyser-backend', '', d)} \
    "

CORE_IMAGE_EXTRA_INSTALL += "rpmsg-sdb-mod"
