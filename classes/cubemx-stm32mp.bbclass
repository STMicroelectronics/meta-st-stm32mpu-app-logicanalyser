# Provides CubeMX device tree file management:
# User can configure recipe file so that extra device tree files provided by
# CubeMX can be integrated in original source code (and so get compiled)

# CubeMX device tree file name
CUBEMX_DTB ??= ""
# Relative path (from mx folder) including project name for CubeMX device tree file location
CUBEMX_PROJECT ??= ""
# Absolute path for CubeMX device tree file location
CUBEMX_DTB_PATH ??= ""
# Component path to copy CubeMX device tree file
CUBEMX_DTB_SRC_PATH ??= ""

CUBEMX_DTB_FILE_ORIGIN = "${CUBEMX_DTB_PATH}/${CUBEMX_DTB}.dts"

# Append to CONFIGURE_FILES var the CubeMX device tree file to make sure that
# any device tree file update implies new compilation
CONFIGURE_FILES += "${@' '.join(map(str, ('${CUBEMX_DTB_PATH}'+'/'+f for f in os.listdir('${CUBEMX_DTB_PATH}')))) if os.path.isdir(d.getVar('CUBEMX_DTB_PATH')) else ''}"

# Append to EXTERNALSRC_SYMLINKS var the CubeMX device tree config to manage
# symlink creation through externalsrc class
EXTERNALSRC_SYMLINKS += "${@' '.join(map(str, ('${CUBEMX_DTB_SRC_PATH}'+'/'+f+':'+'${CUBEMX_DTB_PATH}'+'/'+f for f in os.listdir('${CUBEMX_DTB_PATH}')))) if os.path.isdir(d.getVar('CUBEMX_DTB_PATH')) else ''}"

# In order to take care of any change in CUBEMX_DTB file when user has not set
# recipe source code management through devtool, we should add the same extra
# file checksums for the 'do_configure' task than the one done in externalsrc
# class
python () {
    cubemxdtb = d.getVar('CUBEMX_DTB')
    if cubemxdtb:
        # Manage warning if needed
        d.prependVarFlag('do_compile', 'prefuncs', "cubemx_compile_prefunc ")
        # Append specific actions when not under externalscr class management
        externalsrc = d.getVar('EXTERNALSRC')
        if not externalsrc:
            d.prependVarFlag('do_configure', 'prefuncs', "externalsrc_configure_prefunc ")
            d.setVarFlag('do_configure', 'file-checksums', '${@srctree_configure_hash_files(d)}')
}

python cubemx_compile_prefunc() {
    # Alert user in case CubeMX device tree file is not available
    if not os.path.exists(d.getVar('CUBEMX_DTB_FILE_ORIGIN')):
        bb.warn('File %s not found: compilation will be done using default %s devicetree.' % (d.getVar('CUBEMX_DTB_FILE_ORIGIN'),d.getVar('PN')))
}

# =========================================================================
# Import and adapt functions from openembedded-core 'externalsrc.bbclass'
# =========================================================================

python externalsrc_configure_prefunc() {
    s_dir = d.getVar('S')
    # Create desired symlinks
    symlinks = (d.getVar('EXTERNALSRC_SYMLINKS') or '').split()
    newlinks = []
    for symlink in symlinks:
        symsplit = symlink.split(':', 1)
        lnkfile = os.path.join(s_dir, symsplit[0])
        target = d.expand(symsplit[1])
        if len(symsplit) > 1:
            if os.path.islink(lnkfile):
                # Link already exists, leave it if it points to the right location already
                if os.readlink(lnkfile) == target:
                    continue
                os.unlink(lnkfile)
            elif os.path.exists(lnkfile):
                # File/dir exists with same name as link, just leave it alone
                continue
            os.symlink(target, lnkfile)
            newlinks.append(symsplit[0])
    # Hide the symlinks from git
    try:
        git_exclude_file = os.path.join(s_dir, '.git/info/exclude')
        if os.path.exists(git_exclude_file):
            with open(git_exclude_file, 'r+') as efile:
                elines = efile.readlines()
                for link in newlinks:
                    if link in elines or '/'+link in elines:
                        continue
                    efile.write('/' + link + '\n')
    except IOError as ioe:
        bb.note('Failed to hide EXTERNALSRC_SYMLINKS from git')
}

def srctree_configure_hash_files(d):
    """
    Get the list of files that should trigger do_configure to re-execute,
    based on the value of CONFIGURE_FILES
    """
    in_files = (d.getVar('CONFIGURE_FILES') or '').split()
    out_items = []
    search_files = []
    for entry in in_files:
        if entry.startswith('/'):
            out_items.append('%s:%s' % (entry, os.path.exists(entry)))
        else:
            search_files.append(entry)
    if search_files:
        #s_dir = d.getVar('EXTERNALSRC')
        s_dir = d.getVar('S')
        for root, _, files in os.walk(s_dir):
            for f in files:
                if f in search_files:
                    out_items.append('%s:True' % os.path.join(root, f))
    return ' '.join(out_items)
