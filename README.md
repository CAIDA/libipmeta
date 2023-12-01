# libipmeta

Libipmeta is a library to support the execution of historical and
realtime IP metadata lookups using Maxmind GeoIP,
NetAcuity (Digital Element) geolocation, and CAIDA Prefix-To-AS databases.

## Installation
### Debian/Ubuntu

Add CAIDA package repository:
```
curl https://pkg.caida.org/os/ubuntu/bootstrap.sh | bash
```
(of course, you should inspect this script before running it)

Install libipmeta
```
sudo apt install ipmeta
```

### From Release Tarball

1. Install [libwandio](https://github.com/LibtraceTeam/wandio).
2. Download a [release](https://github.com/CAIDA/libipmeta/releases).

Then:
```
./configure
make
sudo make install
```
On some systems you may need to also run `sudo ldconfig` after installation.

### Install via clone (initializing submodules)

First, make sure you have automake, autoconf and libtool installed on your
system (e.g. `sudo apt-get install autoconf automake libtool` on
Debian/Ubuntu).

Then run the following commands in sequence:

```
git clone https://github.com/CAIDA/libipmeta.git
cd libipmeta
git submodule init
git submodule update
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
```
