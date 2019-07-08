FILESEXTRAPATHS_prepend_stm32mpcommonmx := "${THISDIR}/${PN}:"

SRC_URI_append_stm32mpcommonmx = " \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-ev1', 'file://asound-stm32mp157c-ev.state', '', d)} \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-ev1', 'file://asound-stm32mp157c-ev.conf', '', d)} \
    \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157a-dk1', 'file://asound-stm32mp157c-dk.state', '', d)} \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157a-dk1', 'file://asound-stm32mp157c-dk.conf', '', d)} \
    \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-dk2', 'file://asound-stm32mp157c-dk.state', '', d)} \
    ${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-dk2', 'file://asound-stm32mp157c-dk.conf', '', d)} \
    "

pkg_postinst_${PN}_stm32mpcommonmx() {
    if test -z "$D"
    then
        if test -x ${sbindir}/alsactl
        then
            if [ "${CUBEMX_DT_FILE_BASE}" = "stm32mp157c-ev1" ]; then
                ${sbindir}/alsactl -f ${localstatedir}/lib/alsa/asound-stm32mp157c-ev.state restore
            fi
            if [ "${CUBEMX_DT_FILE_BASE}" = "stm32mp157a-dk1" ]; then
                ${sbindir}/alsactl -f ${localstatedir}/lib/alsa/asound-stm32mp157c-dk.state restore
            fi
            if [ "${CUBEMX_DT_FILE_BASE}" = "stm32mp157c-dk2" ]; then
                ${sbindir}/alsactl -f ${localstatedir}/lib/alsa/asound-stm32mp157c-dk.state restore
            fi
        fi
    fi
}
