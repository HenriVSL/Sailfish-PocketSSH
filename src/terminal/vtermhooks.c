/* See vtermhooks.h. This is the only file that looks inside libvterm's private
 * structures (the parser callback table); the vendored sources stay unmodified. */
#include "vtermhooks.h"

#include "../../3rdparty/libvterm/src/vterm_internal.h"

static const VTermParserCallbacks *orig_callbacks;
static VTermParserCallbacks hooked_callbacks;

/* Number of following arguments that belong to an SGR 38/48/58 colour given in the
 * semicolon form (38;5;n or 38;2;r;g;b), so they are not mistaken for SGR codes. */
static int colour_args(const long args[], int i, int argcount)
{
  if(i + 1 >= argcount || CSI_ARG_HAS_MORE(args[i]))
    return 0; /* colon sub-parameters travel with the 38 itself */
  switch(CSI_ARG(args[i + 1])) {
    case 5: return 2;
    case 2: return 4;
    default: return 1;
  }
}

static int hooked_csi(const char *leader, const long args[], int argcount, const char *intermed,
                      char command, void *user)
{
  if(command == 'm' && (!leader || !leader[0]) && (!intermed || !intermed[0]) && argcount > 0) {
    long out[CSI_ARGS_MAX * 2];
    int n = 0;
    int changed = 0;
    for(int i = 0; i < argcount; i++) {
      long a = args[i];
      long v = CSI_ARG(a);
      out[n++] = a;
      /* Skip sub-parameters (4:3, 38:2:...) */
      while(CSI_ARG_HAS_MORE(args[i]) && i + 1 < argcount)
        out[n++] = args[++i];
      if(CSI_ARG_HAS_MORE(a))
        continue;
      if(v == 38 || v == 48 || v == 58) {
        int skip = colour_args(args, i, argcount);
        for(int k = 0; k < skip && i + 1 < argcount; k++)
          out[n++] = args[++i];
      }
      else if(v == 2) {
        out[n - 1] = 11;
        changed = 1;
      }
      else if(v == 22) {
        out[n++] = 10;
        changed = 1;
      }
    }
    if(changed)
      return orig_callbacks->csi(leader, out, n, intermed, command, user);
  }
  return orig_callbacks->csi(leader, args, argcount, intermed, command, user);
}

void pocketssh_vterm_install_hooks(VTerm *vt)
{
  if(!vt->parser.callbacks || vt->parser.callbacks == &hooked_callbacks)
    return;
  /* libvterm's state layer uses one static callback table for every VTerm. */
  orig_callbacks = vt->parser.callbacks;
  hooked_callbacks = *orig_callbacks;
  hooked_callbacks.csi = hooked_csi;
  vt->parser.callbacks = &hooked_callbacks;
}
