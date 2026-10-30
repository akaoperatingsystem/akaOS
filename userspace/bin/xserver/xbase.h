/* Base X11 types and protocol constants for the akaOS display server.
 *
 * The upstream protocol header supplies the request and reply layouts.  The
 * typedefs and constants it expects come from the Xlib headers, which pull in
 * the whole client API, so the server declares the small subset it needs
 * here.  _XSERVER64 keeps every identifier 32 bits wide, which matches the
 * wire format on every supported port.
 */
#ifndef AKAOS_XSERVER_XBASE_H
#define AKAOS_XSERVER_XBASE_H

#ifndef _XSERVER64
#define _XSERVER64 1
#endif

#include <X11/Xmd.h>
#include <X11/Xdefs.h>

typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Cursor;
typedef XID GContext;
typedef XID Colormap;
typedef XID KeyCode;
typedef CARD32 Time;
typedef CARD32 VisualID;
typedef CARD32 KeySym;

/* Protocol version reported during connection setup. */
#define X_PROTOCOL_MAJOR 11
#define X_PROTOCOL_MINOR 0

/* Byte order markers sent by the client. */
#define X_LSB_FIRST 0x6c
#define X_MSB_FIRST 0x42

/* Error codes. */
#define Success 0
#define BadRequest 1
#define BadValue 2
#define BadWindow 3
#define BadPixmap 4
#define BadAtom 5
#define BadCursor 6
#define BadFont 7
#define BadMatch 8
#define BadDrawable 9
#define BadAccess 10
#define BadAlloc 11
#define BadColor 12
#define BadGC 13
#define BadIDChoice 14
#define BadName 15
#define BadLength 16
#define BadImplementation 17

/* Event codes. */
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define KeymapNotify 11
#define Expose 12
#define GraphicsExpose 13
#define NoExpose 14
#define VisibilityNotify 15
#define CreateNotify 16
#define DestroyNotify 17
#define UnmapNotify 18
#define MapNotify 19
#define MapRequest 20
#define ReparentNotify 21
#define ConfigureNotify 22
#define ConfigureRequest 23
#define GravityNotify 24
#define ResizeRequest 25
#define CirculateNotify 26
#define CirculateRequest 27
#define PropertyNotify 28
#define SelectionClear 29
#define SelectionRequest 30
#define SelectionNotify 31
#define ColormapNotify 32
#define ClientMessage 33
#define MappingNotify 34

/* Event masks. */
#define NoEventMask 0L
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define EnterWindowMask (1L << 4)
#define LeaveWindowMask (1L << 5)
#define PointerMotionMask (1L << 6)
#define ButtonMotionMask (1L << 13)
#define KeymapStateMask (1L << 14)
#define ExposureMask (1L << 15)
#define VisibilityChangeMask (1L << 16)
#define StructureNotifyMask (1L << 17)
#define ResizeRedirectMask (1L << 18)
#define SubstructureNotifyMask (1L << 19)
#define SubstructureRedirectMask (1L << 20)
#define FocusChangeMask (1L << 21)
#define PropertyChangeMask (1L << 22)
#define ColormapChangeMask (1L << 23)
#define OwnerGrabButtonMask (1L << 24)

/* Window classes and map states. */
#define InputOutput 1
#define InputOnly 2
#define CopyFromParent 0
#define IsUnmapped 0
#define IsUnviewable 1
#define IsViewable 2

/* Grab and stacking modes. */
#define GrabSuccess 0
#define GrabAlreadyGrabbed 1
#define GrabInvalidTime 2
#define GrabNotViewable 3
#define GrabFrozen 4
#define GrabModeSync 0
#define GrabModeAsync 1
#define AsyncPointer 0
#define SyncPointer 1
#define ReplayPointer 2
#define AsyncKeyboard 0
#define SyncKeyboard 1
#define ReplayKeyboard 2
#define AsyncBoth 0
#define SyncBoth 1
#define RaiseLowest 0
#define LowerHighest 1
#define PlaceOnTop 0
#define PlaceOnBottom 1

/* Property change notifications. */
#define PropertyNewValue 0
#define PropertyDelete 1

/* Predefined atoms. */
#define XA_PRIMARY ((Atom)1)
#define XA_SECONDARY ((Atom)2)
#define XA_ATOM ((Atom)4)
#define XA_CARDINAL ((Atom)6)
#define XA_COLORMAP ((Atom)7)
#define XA_INTEGER ((Atom)19)
#define XA_PIXMAP ((Atom)20)
#define XA_STRING ((Atom)31)
#define XA_WINDOW ((Atom)33)
#define XA_WM_NAME ((Atom)39)
#define XA_WM_ICON_NAME ((Atom)37)
#define XA_WM_CLASS ((Atom)67)

#endif
