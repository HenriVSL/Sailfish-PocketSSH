#ifndef VTERMHOOKS_H
#define VTERMHOOKS_H

#include <vterm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Adds SGR 2 (faint) on top of the unmodified libvterm 0.3, which ignores it.
// Installs a wrapper around the parser's CSI handler that rewrites SGR parameters
// before libvterm sees them: 2 becomes 11 (alternative font 1, which nothing uses)
// and 22 (normal intensity) also resets the font with 10. The renderer then draws
// cells with font == 1 as faint. Call once, after vterm_obtain_state().
void pocketssh_vterm_install_hooks(VTerm *vt);

#ifdef __cplusplus
}
#endif

#endif // VTERMHOOKS_H
