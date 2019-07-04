echo "Initializing submodules"
git submodule init
git submodule update
echo "Running autoconf"
autoreconf -vfi
