#!/bin/bash
set -e

VERSION="$1"
ARCH="$2"

mkdir -p Throne/DEBIAN
mkdir -p Throne/opt
cp -r linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Throne/opt
mv Throne/opt/linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Throne/opt/Throne
rm Throne/opt/Throne/Throne.debug

# basic
cat >Throne/DEBIAN/control <<-EOF
Package: Throne
Version: $VERSION
Architecture: $ARCH
Maintainer: Mahdi Mahdi.zrei@gmail.com
Depends: desktop-file-utils$([[ $3 == "systemqt" ]] && echo ", libqt6core6, libqt6gui6, libqt6network6, libqt6widgets6, qt6-qpa-plugins, qt6-wayland, qt6-gtk-platformtheme, qt6-xdgdesktopportal-platformtheme, libxcb-cursor0, fonts-noto-color-emoji")
Description: Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
EOF

cat >Throne/DEBIAN/postinst <<-EOF
cat >/usr/share/applications/Throne.desktop<<-END
[Desktop Entry]
Name=Throne
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/Throne:\$PATH /opt/Throne/Throne -appdata"
Icon=/opt/Throne/Throne.png
Terminal=false
Type=Application
Categories=Network;Application;
StartupWMClass=Throne
END

update-desktop-database
EOF

sudo chmod 0755 Throne/DEBIAN/postinst

# desktop && PATH

sudo dpkg-deb --build Throne
