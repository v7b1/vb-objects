# building FAT aubio

# relies on python!
cd aubio
# CFLAGS="-arch x86_64 -arch arm64" ./waf configure build
CFLAGS="-arch x86_64 -arch arm64" ./waf distclean configure build install --disable-tests --disable-examples --disable-docs --prefix=build/dist

# -enable-fat doesn't work for us, as this ties to build i386 and x86_64...


# using make
# cd aubio
# make test_lib_only CFLAGS="-arch x86_64 -arch arm64"
# make configure build install CFLAGS="-arch x86_64 -arch arm64"


# more on windows installation etc.
# https://aubio.org/manual/latest/installing.html

# also look at wscript 'options'
# e.g. check out --enable-fftw3 / fftw3f etc.