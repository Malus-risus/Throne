#!/bin/bash
set -e

source script/env_deploy.sh

# This logic now correctly handles all targets, including the new x86_64-v3.
if [[ $1 == 'i686' ]]; then
  ARCH="windowslegacy-386"
  DEST=$DEPLOYMENT/windows32
elif [[ $1 == 'x86_64' ]]; then
  ARCH="windowslegacy-amd64"
  DEST=$DEPLOYMENT/windowslegacy64
elif [[ $1 == 'x86_64-v3' ]]; then
  ARCH="windows-amd64-v3"
  DEST=$DEPLOYMENT/windows64-v3
else
  ARCH="windows-amd64"
  DEST=$DEPLOYMENT/windows64
fi

rm -rf $DEST
mkdir -p $DEST

#### get the pdb ####
# This part is likely only needed for Debug builds, but we'll keep it for now.
# Consider removing it if you only ship Release builds.
curl -fLJO https://github.com/rainers/cv2pdb/releases/download/v0.53/cv2pdb-0.53.zip
7z x cv2pdb-0.53.zip -ocv2pdb
./cv2pdb/cv2pdb64.exe ./build/Throne.exe ./tmp.exe ./Throne.pdb
rm -rf cv2pdb-0.53.zip cv2pdb
cd build
strip -s Throne.exe
cd ..
rm tmp.exe
mv Throne.pdb $DEST

#### copy exe ####
cp $BUILD/Throne.exe $DEST

cd download-artifact
# The directory name now correctly includes the ARCH variable which will match the Go artifact name.
cd *$ARCH
tar xvzf artifacts.tgz -C ../../
cd ..
cd *public_res
tar xvzf artifacts.tgz -C ../../
cd ../..

mv $DEPLOYMENT/public_res/* $DEST
