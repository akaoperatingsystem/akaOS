/* Check the reply structures of the X11 protocol.
 *
 * A reply of the protocol is always thirty two bytes long, and the data that
 * follows it is counted in the length field of the reply.  A reply structure
 * that is shorter than thirty two bytes would make the client read the next
 * reply at the wrong place, so the size of every reply structure is compared
 * with the size the protocol asks for.
 *
 * The program is built for the host and prints the sizes, so that the
 * comparison can be made against the sizes in the protocol header. */
#include <stdio.h>
#include <X11/Xproto.h>

int main(void) {
    printf("xAllocColorReply %zu %d\n", sizeof(xAllocColorReply), sz_xAllocColorReply);
    printf("xAllocNamedColorReply %zu %d\n", sizeof(xAllocNamedColorReply), sz_xAllocNamedColorReply);
    printf("xGetAtomNameReply %zu %d\n", sizeof(xGetAtomNameReply), sz_xGetAtomNameReply);
    printf("xGetFontPathReply %zu %d\n", sizeof(xGetFontPathReply), sz_xGetFontPathReply);
    printf("xGetGeometryReply %zu %d\n", sizeof(xGetGeometryReply), sz_xGetGeometryReply);
    printf("xGetImageReply %zu %d\n", sizeof(xGetImageReply), sz_xGetImageReply);
    printf("xGetInputFocusReply %zu %d\n", sizeof(xGetInputFocusReply), sz_xGetInputFocusReply);
    printf("xGetKeyboardMappingReply %zu %d\n", sizeof(xGetKeyboardMappingReply), sz_xGetKeyboardMappingReply);
    printf("xGetModifierMappingReply %zu %d\n", sizeof(xGetModifierMappingReply), sz_xGetModifierMappingReply);
    printf("xGetPropertyReply %zu %d\n", sizeof(xGetPropertyReply), sz_xGetPropertyReply);
    printf("xGetSelectionOwnerReply %zu %d\n", sizeof(xGetSelectionOwnerReply), sz_xGetSelectionOwnerReply);
    printf("xGetWindowAttributesReply %zu %d\n", sizeof(xGetWindowAttributesReply), sz_xGetWindowAttributesReply);
    printf("xGrabPointerReply %zu %d\n", sizeof(xGrabPointerReply), sz_xGrabPointerReply);
    printf("xInternAtomReply %zu %d\n", sizeof(xInternAtomReply), sz_xInternAtomReply);
    printf("xListExtensionsReply %zu %d\n", sizeof(xListExtensionsReply), sz_xListExtensionsReply);
    printf("xListFontsReply %zu %d\n", sizeof(xListFontsReply), sz_xListFontsReply);
    printf("xListPropertiesReply %zu %d\n", sizeof(xListPropertiesReply), sz_xListPropertiesReply);
    printf("xLookupColorReply %zu %d\n", sizeof(xLookupColorReply), sz_xLookupColorReply);
    printf("xQueryColorsReply %zu %d\n", sizeof(xQueryColorsReply), sz_xQueryColorsReply);
    printf("xQueryExtensionReply %zu %d\n", sizeof(xQueryExtensionReply), sz_xQueryExtensionReply);
    printf("xQueryKeymapReply %zu %d\n", sizeof(xQueryKeymapReply), sz_xQueryKeymapReply);
    printf("xQueryPointerReply %zu %d\n", sizeof(xQueryPointerReply), sz_xQueryPointerReply);
    printf("xQueryTreeReply %zu %d\n", sizeof(xQueryTreeReply), sz_xQueryTreeReply);
    printf("xTranslateCoordsReply %zu %d\n", sizeof(xTranslateCoordsReply), sz_xTranslateCoordsReply);
    return 0;
}
