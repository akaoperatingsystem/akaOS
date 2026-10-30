/* Guest probe for the upstream window managers on the akaOS X11 server.
 *
 * The probe creates a window, asks the server to map it and then waits for
 * the window manager to take the window over.  A window manager that manages
 * a window reparents it into a frame, so the probe reports the frame it was
 * moved into and the size the window manager gave it.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int reparented;

static int error_handler(Display *display, XErrorEvent *event) {
    char text[128];
    XGetErrorText(display, event->error_code, text, sizeof(text));
    printf("x11 wm client: X error %u (%s) request %u.%u\n",
           (unsigned)event->error_code, text,
           (unsigned)event->request_code, (unsigned)event->minor_code);
    fflush(stdout);
    return 0;
}

int main(void) {
    puts("x11 wm client: main enter");
    fflush(stdout);
    if (setenv("DISPLAY", ":0", 1) != 0) {
        puts("x11 wm client: environment setup failed");
        return 1;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        puts("x11 wm client: display connect failed");
        fflush(stdout);
        return 2;
    }
    XSetErrorHandler(error_handler);
    printf("x11 wm client: connected to display %s\n", DisplayString(display));
    fflush(stdout);

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 60, 60, 200, 120, 2,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));
    XStoreName(display, window, "akaOS window manager probe");
    XSelectInput(display, window, StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);
    printf("x11 wm client: mapped window 0x%lx\n", (unsigned long)window);
    fflush(stdout);

    /* The window manager answers a map request by taking the window over, so
     * the probe waits for the window to be moved into a frame. */
    Window frame = 0;
    for (int attempt = 0; attempt < 600 && !reparented; ++attempt) {
        XEvent event;
        if (!XCheckTypedWindowEvent(display, window, ReparentNotify, &event)) {
            usleep(50000);
            continue;
        }
        reparented = 1;
        frame = event.xreparent.parent;
        XWindowAttributes attributes;
        int known = XGetWindowAttributes(display, event.xreparent.window,
                                         &attributes);
        printf("x11 wm client: reparented into frame 0x%lx size %dx%d\n",
               (unsigned long)event.xreparent.parent,
               known ? attributes.width : 0, known ? attributes.height : 0);
        fflush(stdout);
    }

    if (!reparented) {
        puts("x11 wm client: no window manager took the window over");
        fflush(stdout);
        XCloseDisplay(display);
        return 3;
    }

    /* The window belongs to the frame of the window manager now.  Check the
     * parent it reports for itself against the frame of the event. */
    Window root_return = 0;
    Window parent_return = 0;
    Window *children = NULL;
    unsigned int child_count = 0;
    if (!XQueryTree(display, window, &root_return, &parent_return, &children,
                    &child_count)) {
        puts("x11 wm client: window tree query failed");
        fflush(stdout);
        XCloseDisplay(display);
        return 4;
    }
    if (children) XFree(children);
    if (parent_return != frame) {
        printf("x11 wm client: window reports parent 0x%lx instead of frame "
               "0x%lx\n", (unsigned long)parent_return, (unsigned long)frame);
        fflush(stdout);
        XCloseDisplay(display);
        return 5;
    }
    printf("x11 wm client: window parent matches frame 0x%lx\n",
           (unsigned long)frame);
    fflush(stdout);

    /* Openbox can reparent a window before its startup sequence has mapped
     * the frame. Wait until the frame is viewable before checking the screen. */
    int frame_viewable = 0;
    int client_viewable = 0;
    for (int attempt = 0; attempt < 1200; ++attempt) {
        XWindowAttributes frame_attributes;
        XWindowAttributes client_attributes;
        frame_viewable = XGetWindowAttributes(display, frame, &frame_attributes) &&
            frame_attributes.map_state == IsViewable;
        client_viewable = XGetWindowAttributes(display, window, &client_attributes) &&
            client_attributes.map_state == IsViewable;
        if (frame_viewable && client_viewable) break;
        usleep(50000);
    }
    if (!frame_viewable) {
        puts("x11 wm client: frame did not become viewable");
        fflush(stdout);
        XCloseDisplay(display);
        return 12;
    }
    if (!client_viewable) {
        puts("x11 wm client: child did not become viewable in frame");
        fflush(stdout);
        XCloseDisplay(display);
        return 13;
    }
    puts("x11 wm client: frame and child became viewable");
    fflush(stdout);

    /* Draw into the framed window and read the pixel back.  A window that
     * the window manager reparented has to keep accepting the drawing
     * requests of its client and show the pixels in its frame. */
    GC gc = XCreateGC(display, window, 0, NULL);
    if (!gc) {
        puts("x11 wm client: graphics context creation failed");
        fflush(stdout);
        XCloseDisplay(display);
        return 6;
    }
    const unsigned long marker_green = 0x00ff00UL;
    XSetForeground(display, gc, marker_green);
    XFillRectangle(display, window, gc, 10, 10, 60, 40);
    XSync(display, False);

    XImage *image = XGetImage(display, window, 20, 20, 1, 1, AllPlanes, ZPixmap);
    if (!image) {
        puts("x11 wm client: pixel read-back failed");
        fflush(stdout);
        XFreeGC(display, gc);
        XCloseDisplay(display);
        return 7;
    }
    unsigned long pixel = XGetPixel(image, 0, 0);
    XDestroyImage(image);
    XFreeGC(display, gc);
    if ((pixel & 0xffffffUL) != marker_green) {
        printf("x11 wm client: framed window pixel is 0x%06lx instead of "
               "0x%06lx\n", pixel & 0xffffffUL, marker_green);
        fflush(stdout);
        XCloseDisplay(display);
        return 8;
    }
    puts("x11 wm client: drew into the framed window");
    fflush(stdout);

    /* Read the same pixel back from the root window at its translated screen
     * coordinates to verify that the composited frame on screen shows the
     * client's drawing. */
    int screen_x = 0;
    int screen_y = 0;
    Window child_return = 0;
    if (!XTranslateCoordinates(display, window, root, 20, 20, &screen_x,
                               &screen_y, &child_return)) {
        puts("x11 wm client: screen coordinate translation failed");
        fflush(stdout);
        XCloseDisplay(display);
        return 9;
    }
    XImage *screen_image = XGetImage(display, root, screen_x, screen_y, 1, 1,
                                     AllPlanes, ZPixmap);
    if (!screen_image) {
        puts("x11 wm client: root screen pixel read-back failed");
        fflush(stdout);
        XCloseDisplay(display);
        return 10;
    }
    unsigned long screen_pixel = XGetPixel(screen_image, 0, 0);
    XDestroyImage(screen_image);
    if ((screen_pixel & 0xffffffUL) != marker_green) {
        printf("x11 wm client: root screen pixel at %d,%d is 0x%06lx instead "
               "of 0x%06lx\n", screen_x, screen_y, screen_pixel & 0xffffffUL,
               marker_green);
        fflush(stdout);
        XCloseDisplay(display);
        return 11;
    }
    printf("x11 wm client: screen pixel at %d,%d matches 0x%06lx\n",
           screen_x, screen_y, marker_green);
    fflush(stdout);

    puts("x11 wm client: window manager frame verified");
    fflush(stdout);
    XCloseDisplay(display);
    return 0;
}
