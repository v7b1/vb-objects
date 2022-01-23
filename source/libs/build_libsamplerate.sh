
# build libsamplerate
cd libsamplerate
echo "Building libsamplerate"
mkdir -p build && cd build
cmake -DLIBSAMPLERATE_EXAMPLES=OFF -DBUILD_TESTING=OFF -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
cmake --build . --config 'Release'
# mv src/libsamplerate.a ../../libsamplerate.a # should we move it, or just leave it where it is?