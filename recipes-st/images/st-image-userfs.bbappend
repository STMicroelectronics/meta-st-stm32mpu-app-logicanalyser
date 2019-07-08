PACKAGE_INSTALL += "\
    ${@bb.utils.contains('DISTRO_FEATURES', 'wayland', 'logic-analyser-backend', '', d)} \
    "
