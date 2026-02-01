python do_patch:append() {
    import os

    override_path = os.path.join(d.getVar('S'), 'config', 'override.m4')
    if not os.path.exists(override_path):
        bb.fatal(f'binutils override.m4 not found at {override_path}')

    with open(override_path, 'r', encoding='utf-8') as handle:
        content = handle.read()

    old = ('[m4_fatal([Please use exactly Autoconf ]'
           '_GCC_AUTOCONF_VERSION[ instead of ]'
           'm4_defn([m4_PACKAGE_VERSION])[.])])')
    new = ('[m4_if(m4_defn([m4_PACKAGE_VERSION]), [2.72e], [],\n'
           '    [m4_fatal([Please use exactly Autoconf ]'
           '_GCC_AUTOCONF_VERSION[ instead of ]'
           'm4_defn([m4_PACKAGE_VERSION])[.])])])')

    if old not in content:
        bb.fatal('Expected Autoconf version check not found in override.m4')

    content = content.replace(old, new)

    with open(override_path, 'w', encoding='utf-8') as handle:
        handle.write(content)
}
