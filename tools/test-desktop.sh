lumen --input-test &
sleep 1
xserver &
sleep 2

env DISPLAY=:0 /usr/local/bin/openbox --sm-disable &
sleep 45
env DISPLAY=:0 x11-wm-client
echo CLIENT-EXIT=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/lxpanel --profile default &
sleep 15
ps
echo LXPANEL-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/pcmanfm --desktop --profile default &
sleep 15
ps
echo PCMANFM-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfwm4 --replace &
sleep 15
ps
echo XFWM4-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfce4-panel &
sleep 15
ps
echo XFCE4-PANEL-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/xfce4-session &
sleep 15
ps
echo XFCE4-SESSION-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/marco --replace &
sleep 15
ps
echo MARCO-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/mate-panel &
sleep 15
ps
echo MATE-PANEL-RUNNING=$?

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus /usr/local/bin/mate-session &
sleep 15
ps
echo MATE-SESSION-RUNNING=$?
