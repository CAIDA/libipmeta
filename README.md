# libipmeta

Libipmeta it's a library to support the execution of historical and realtime geolocation metadata lookups using Maxmind GeoIP and/or NetAcuity (Digital Element) geolocation databases.

### Quick Start
To get started using `libipmeta`, either clone or download the contents from the GitHub repository.

You will also need the libcurl and wandio libraries installed before building `libipmeta` (libcurl must be installed prior to building wandio).

#### Step 1. Install Dependencies

##### Required Libraries
First, install the following dependencies of wandio using your favorite package manager:

 * libbz2
 * zlib
 * libcurl (> 7.18.0)

##### Dependencies installation, OS-specific instructions
**Ubuntu/Debian**

`sudo apt-get install zlib1g-dev libbz2-dev libcurl4-openssl-dev`

Then, install `wandio` as follows:
```
$ mkdir ~/src
$ cd ~/src/
$ curl -O https://research.wand.net.nz/software/wandio/wandio-1.0.4.tar.gz
$ tar zxf wandio-1.0.4.tar.gz
$ cd wandio-1.0.4/
$ ./configure
$ make
$ make install
```
Note: Ensure that the last lines from configure show a Yes result for at least `zlib`, `bz2`, and `libcurl` like the following:

```
configure: WANDIO version 1.0.4
configure: Compiled with compressed file (zlib) support: Yes
configure: Compiled with compressed file (bz2) support: Yes
configure: Compiled with compressed file (lzo write only) support: No
configure: Compiled with compressed file (lzma) support: No
configure: Compiled with http read (libcurl) support: Yes
```
Note: If you do not want to install system-wide, then you can specify an installation directory (`/INSTALL/PATH`) as follows:

`$ ./configure --prefix=/INSTALL/PATH`
Then, depending on your OS, you may need to set LD_LIBRRAY_PATH as follows:

`$ export LD_LIBRARY_PATH="/INSTALL/PATH/lib:$LD_LIBRARY_PATH"`
If required libraries are not in the system library paths, specify their paths when running configure as follows:

`$ ./configure CPPFLAGS='-I/path/to/deps/include' LDFLAGS='-L/path/to/deps/lib'`
You may test that wandio works by running `$ wandiocat http://google.com`

#### Step 2. Install `libipmeta` via clone

```
$ git clone https://github.com/CAIDA/libipmeta.git
$ cd libipmeta
$ ./build_latest.sh
$ make install
```
