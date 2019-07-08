PROJECTS_LIST_stm32mpcommonmx = " \
${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-ev1', 'STM32MP157C-EV1/Applications/OpenAMP/OpenAMP_TTY_echo', '', d)} \
${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157c-dk2', 'STM32MP157C-DK2/Applications/OpenAMP/OpenAMP_TTY_echo', '', d)} \
${@bb.utils.contains('CUBEMX_DT_FILE_BASE', 'stm32mp157a-dk1', 'STM32MP157C-DK2/Applications/OpenAMP/OpenAMP_TTY_echo', '', d)} \
"
