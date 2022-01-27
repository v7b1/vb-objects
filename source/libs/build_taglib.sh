# build taglib
cd taglib
echo "Building taglib"

# mkdir build; cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release \
#   -DBUILD_TESTING=OFF \
#   -DBUILD_SHARED_LIBS=OFF \
#   -DCMAKE_OSX_DEPLOYMENT_TARGET=10.10 \
#   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# make


mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_FRAMEWORK=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.10 \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
make