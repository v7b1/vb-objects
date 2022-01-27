

# get sources
echo "downloading fftw-3.3.10"
curl -O http://www.fftw.org/fftw-3.3.10.tar.gz
gunzip -c fftw-3.3.10.tar.gz | tar xopf -


# build FFTW
cd fftw-3.3.10
echo "Building fftw"
# echo "$0"
# dirname "$0"
# relative path for prefix geht nicht... 
#CFLAGS="-arch arm64 -arch x86_64" ./configure --prefix=./dist
CFLAGS="-arch arm64 -arch x86_64" ./configure --enable-float
make
# make install
