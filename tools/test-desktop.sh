lumen --input-test &
sleep 2
wayland-bridge &
sleep 2
xserver &
sleep 2

env DISPLAY=:0 /usr/local/bin/openbox --sm-disable &
sleep 45
env DISPLAY=:0 x11-wm-client
echo CLIENT-EXIT=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/lxpanel --profile default &
sleep 15
ps
echo LXPANEL-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/pcmanfm --desktop --profile default &
sleep 15
ps
echo PCMANFM-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfwm4 --replace &
sleep 15
ps
echo XFWM4-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfce4-panel &
sleep 15
ps
echo XFCE4-PANEL-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfce4-session &
sleep 15
ps
echo XFCE4-SESSION-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/marco --replace &
sleep 15
ps
echo MARCO-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/mate-panel &
sleep 15
ps
echo MATE-PANEL-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/mate-session &
sleep 15
ps
echo MATE-SESSION-RUNNING=$?

env GDK_BACKEND=x11 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/muffin --replace &
sleep 15
ps
echo MUFFIN-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/i3 --replace &
sleep 15
ps
echo I3-RUNNING=$?

env DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland QT_PLUGIN_PATH=/usr/local/plugins:/usr/lib/qt6/plugins XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/lxqt-panel &
sleep 15
ps
echo LXQT-PANEL-RUNNING=$?

env DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland QT_PLUGIN_PATH=/usr/local/plugins:/usr/lib/qt6/plugins XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/lxqt-session &
sleep 15
ps
echo LXQT-SESSION-RUNNING=$?

env DISPLAY=:0 /usr/local/bin/dwm &
sleep 15
ps
echo DWM-RUNNING=$?