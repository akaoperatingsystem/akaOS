export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus

lumen --input-test &
sleep 1
xserver &
sleep 2

/usr/local/bin/openbox --sm-disable &
sleep 45
x11-wm-client
echo CLIENT-EXIT=$?

/usr/local/bin/lxpanel --profile default &
sleep 15
ps
echo LXPANEL-RUNNING=$?

/usr/local/bin/pcmanfm --desktop --profile default &
sleep 15
ps
echo PCMANFM-RUNNING=$?

/usr/local/bin/xfwm4 --replace &
sleep 15
ps
echo XFWM4-RUNNING=$?

/usr/local/bin/xfce4-panel &
sleep 15
ps
echo XFCE4-PANEL-RUNNING=$?

/usr/local/bin/xfce4-session &
sleep 15
ps
echo XFCE4-SESSION-RUNNING=$?

/usr/local/bin/marco --replace &
sleep 15
ps
echo MARCO-RUNNING=$?

/usr/local/bin/mate-panel &
sleep 15
ps
echo MATE-PANEL-RUNNING=$?

/usr/local/bin/mate-session &
sleep 15
ps
echo MATE-SESSION-RUNNING=$?
