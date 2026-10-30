/* Guest probe for the akaOS X11 display server.
 *
 * The probe connects to the display with the real upstream libX11 client
 * library, creates and maps a window, draws into it and then waits for a
 * pointer and a keyboard event.  Every stage prints a marker so the QEMU
 * harness can verify that the whole client path works.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int probe_socket(const char *path) {
    int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    printf("x11 client: probe socket() returned %d errno=%d\n", descriptor,
           errno);
    if (descriptor < 0) return 1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1U);
    int result = connect(descriptor, (struct sockaddr *)&address,
                          sizeof(address));
    printf("x11 client: probe connect(%s) returned %d errno=%d\n", path, result,
           errno);
    close(descriptor);
    return result == 0 ? 0 : 1;
}

static int io_error_handler(Display *display) {
    (void)display;
    printf("x11 client: fatal X I/O error while talking to the server\n");
    fflush(stdout);
    return 0;
}

static int error_handler(Display *display, XErrorEvent *event) {
    char text[128];
    XGetErrorText(display, event->error_code, text, sizeof(text));
    printf("x11 client: X error %u (%s) request %u.%u\n",
           (unsigned)event->error_code, text,
           (unsigned)event->request_code, (unsigned)event->minor_code);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--probe") == 0) {
        return probe_socket(argc > 2 ? argv[2] : "/tmp/.X11-unix/X0");
    }
    const char *display_name = getenv("DISPLAY");
    printf("x11 client: DISPLAY=%s\n", display_name ? display_name : "(unset)");
    fflush(stdout);
    XSetIOErrorHandler(io_error_handler);
    XSetErrorHandler(error_handler);

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        printf("x11 client: FAILED to open the display\n");
        fflush(stdout);
        return 1;
    }
    printf("x11 client: connected to display %s\n", DisplayString(display));
    printf("x11 client: vendor %s\n", ServerVendor(display));
    fflush(stdout);

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    printf("x11 client: default screen %d root 0x%lx\n", screen,
           (unsigned long)root);
    fflush(stdout);
    unsigned long black = BlackPixel(display, screen);
    unsigned long white = WhitePixel(display, screen);

    /* The window keeps a known position and size so the geometry path is
     * verified, and the pointer is moved onto it by the test harness. */
    Window window = XCreateSimpleWindow(display, root, 120, 90, 320, 240, 2,
                                        black, white);
    if (!window) {
        printf("x11 client: FAILED to create the window\n");
        fflush(stdout);
        return 1;
    }
    printf("x11 client: created window 0x%lx\n", (unsigned long)window);
    fflush(stdout);

    XStoreName(display, window, "akaOS X11 client");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                     ButtonReleaseMask | StructureNotifyMask);
    XMapWindow(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
    printf("x11 client: mapped window\n");
    fflush(stdout);

    GC gc = XCreateGC(display, window, 0, NULL);
    if (!gc) {
        printf("x11 client: FAILED to create the graphics context\n");
        fflush(stdout);
        return 1;
    }

    /* Draw a filled block and a rectangle outline. */
    XSetForeground(display, gc, 0x002288ff);
    XFillRectangle(display, window, gc, 24, 24, 140, 80);
    XSetForeground(display, gc, 0x00ff8800);
    XDrawRectangle(display, window, gc, 184, 24, 100, 80);
    XFlush(display);
    printf("x11 client: drew into the window\n");
    fflush(stdout);

    /* The core requests a desktop leans on.  Each one prints its own marker
     * so a failure points at the request that broke. */
    {
        Atom atom = XInternAtom(display, "_AKAOS_PROBE_ATOM", False);
        char *atom_name = atom ? XGetAtomName(display, atom) : NULL;
        printf("x11 client: interned atom %lu named %s\n", (unsigned long)atom,
               atom_name ? atom_name : "(none)");
        if (atom_name) XFree(atom_name);
        fflush(stdout);

        Pixmap pixmap = XCreatePixmap(display, window, 32, 24, 24);
        printf("x11 client: created pixmap %lu\n", (unsigned long)pixmap);
        if (pixmap) {
            XFreePixmap(display, pixmap);
            printf("x11 client: freed pixmap\n");
        }
        fflush(stdout);

        const char *payload = "akaos";
        Atom property = XInternAtom(display, "_AKAOS_PROBE_PROPERTY", False);
        XChangeProperty(display, window, property, XA_STRING, 8, PropModeReplace,
                        (const unsigned char *)payload, 5);
        Atom actual_type = 0;
        int actual_format = 0;
        unsigned long items = 0;
        unsigned long bytes_after = 0;
        unsigned char *value = NULL;
        if (XGetWindowProperty(display, window, property, 0, 16, False,
                                AnyPropertyType, &actual_type, &actual_format,
                                &items, &bytes_after, &value) == Success) {
            printf("x11 client: read property %lu bytes type %lu\n", items,
                   (unsigned long)actual_type);
            if (value) XFree(value);
        }
        fflush(stdout);

        GC copy = XCreateGC(display, window, 0, NULL);
        if (copy) {
            XCopyGC(display, gc, GCForeground | GCBackground, copy);
            printf("x11 client: copied the graphics context\n");
            XFreeGC(display, copy);
        }
        fflush(stdout);

        int font_count = 0;
        char **font_names = XListFonts(display, "*", 4, &font_count);
        printf("x11 client: listed %d fonts\n", font_count);
        if (font_names) {
            for (int index = 0; index < font_count; ++index) {
                printf("x11 client: font %d is %s\n", index, font_names[index]);
            }
            XFreeFontNames(font_names);
        }
        fflush(stdout);

        Font font = XLoadFont(display, "fixed");
        if (font) {
            XSetFont(display, gc, font);
            XSetForeground(display, gc, 0x00ffffff);
            XDrawString(display, window, gc, 24, 150, "akaOS core font", 15);
            printf("x11 client: drew a string with the core font\n");
        }
        fflush(stdout);
    }

    XWindowAttributes attributes;
    if (XGetWindowAttributes(display, window, &attributes)) {
        printf("x11 client: window geometry %dx%d at %d,%d map state %d\n",
               attributes.width, attributes.height, attributes.x, attributes.y,
               attributes.map_state);
        fflush(stdout);
    }

    {
        Window root_return = 0;
        Window child_return = 0;
        int root_x = 0;
        int root_y = 0;
        int window_x = 0;
        int window_y = 0;
        unsigned int mask_return = 0;
        if (XQueryPointer(display, window, &root_return, &child_return,
                          &root_x, &root_y, &window_x, &window_y, &mask_return)) {
            printf("x11 client: pointer at %d,%d in window 0x%lx\n", root_x,
                   root_y, (unsigned long)child_return);
            fflush(stdout);
        }
    }

    /* The probe waits for a pointer event and a keyboard event so both
     * halves of the X11 input path are proven. */
    int saw_expose = 0;
    int saw_button = 0;
    int saw_key = 0;
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == Expose) {
            if (!saw_expose) {
                saw_expose = 1;
                printf("x11 client: received expose event\n");
                fflush(stdout);
            }
            XSetForeground(display, gc, 0x002288ff);
            XFillRectangle(display, window, gc, 24, 24, 140, 80);
            XFlush(display);
            continue;
        }
        if (event.type == ButtonPress) {
            if (!saw_button) {
                saw_button = 1;
                printf("x11 client: received button press at %d,%d\n",
                       event.xbutton.x, event.xbutton.y);
                fflush(stdout);
            }
            continue;
        }
        if (event.type == KeyPress) {
            if (!saw_key) {
                saw_key = 1;
                printf("x11 client: received key press keycode %u\n",
                       (unsigned)event.xkey.keycode);
                fflush(stdout);
            }
            if (saw_button) break;
            continue;
        }
        if (event.type == ConfigureNotify) {
            printf("x11 client: received configure notify %dx%d\n",
                   event.xconfigure.width, event.xconfigure.height);
            fflush(stdout);
        }
    }

    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    printf("x11 client: X11 client path verified\n");
    fflush(stdout);
    return 0;
}
