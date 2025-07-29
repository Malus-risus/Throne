#!/bin/bash
set -e

source script/env_deploy.sh

# This logic now correctly handles all targets, including the new x86_64-v3.
if [[ $1 == 'i686' ]]; then
  ARCH="windowslegacy-386"
  DEST=$DEPLOYMENT/windows32
elif [[ $1 == 'x86_64' ]]; then
  # This case is for the legacy 64-bit build with Qt 6.2.12
  ARCH="windowslegacy-amd64"
  DEST=$DEPLOYMENT/windowslegacy64
elif [[ $1 == 'x86_64-v3' ]]; then
  # This case is for the modern v3 build
  ARCH="windows-amd64-v3"
  DEST=$DEPLOYMENT/windows64-v3
else
  # This is the default case for the modern standard x86_64 build
  ARCH="windows-amd64"
  DEST=$DEPLOYMENT/windows64
fi

echo "---> Cleaning and creating destination: $DEST"
rm -rf $DEST
mkdir -p $DEST

#### get the pdb ####
# This part is likely only needed for Debug builds.
# Since we are on Release, you might consider removing this block in the future.
if [ -f "./build/Throne.exe" ]; then
    echo "---> Processing PDB files..."
    curl -fLJO https://github.com/rainers/cv2pdb/releases/download/v0.53/cv2pdb-0.53.zip
    7z x cv2pdb-0.53.zip -ocv2pdb
    ./cv2pdb/cv2pdb64.exe ./build/Throne.exe ./tmp.exe ./Throne.pdb
    rm -rf cv2pdb-0.53.zip cv2pdb
    cd build
    strip -s Throne.exe
    cd ..
    rm tmp.exe
    mv Throne.pdb $DEST
fi


#### copy exe ####
echo "---> Copying C++ executable..."
cp $BUILD/Throne.exe $DEST

#### extract Go core ####
echo "---> Extracting Go core for arch: $ARCH"
cd download-artifact

# Find the directory for the corresponding Go artifact
GO_ARTIFACT_DIR=$(find . -type d -name "*$ARCH" | head -n 1)
if [ -z "$GO_ARTIFACT_DIR" ]; then
    echo "::error:: Could not find Go artifact directory for ARCH: $ARCH"
    exit 1
fi

echo "---> Found Go artifact directory: $GO_ARTIFACT_DIR"
cd "$GO_ARTIFACT_DIR"

# Extract here first, then move. This is more robust than using 'tar -C' with complex paths.
echo "---> Unpacking Go core locally..."
tar xvzf artifacts.tgz

echo "---> Moving Go core files to final destination..."
# The Go artifact contains a 'deployment' directory with the core files inside.
mv ./deployment/* ../../$DEST

cd ../..

#### extract public_res ####
echo "---> Extracting public resources..."
cd download-artifact
cd *public_res

# Extract here first, then move.
echo "---> Unpacking public resources locally..."
tar xvzf artifacts.tgz

echo "---> Moving public resource files to final destination..."
# The public_res artifact contains a 'deployment/public_res' directory.
mv ./deployment/public_res/* ../../$DEST

cd ../..
