MariaDB package demonstration
=============================

This is a Duda web service that demonstrates the usage of mariadb package.

## Introduction ##
This web service fetches and displays data from MariaDB server.

## Installation ##
clone the Monkey HTTP Daemon repository from my Github:

    git clone https://github.com/swpd/monkey

change to Monkey's directory:

    cd monkey

check out `mariadb` branch:

    git checkout mariadb

configure monkey with duda plugin enabled:

    ./configure --enable-plugins=duda

if you prefer verbose message output of monkey, configure it with `--trace` option
(this option should not be used in a production environment):

    ./configure --enable-plugins=duda --trace

build Monkey(go get yourself a cup of coffee, it might take a while):

    make

Notice: The MariaDB client library is compiled from source because it may be
unavailable on some distribution. And it requires `cmake` and `libaio` to be
installed on your machine:

    Debian/Ubuntu              : apt-get install libaio-dev cmake
    RedHat/Fedora/Oracle Linux : yum install libaio-devel cmake
    SUSE                       : zypper install libaio-devel cmake

edit `conf/plugins.load` to make sure `duda` is enabled:

    Load /path/to/monkey-duda.so

edit `conf/sites/default` to add this web service:

    [WEB_SERVICE]
        Name mariadb_demo
        Enabled on

run the server:

    `bin/monkey`

### Serving Requests ###
use your favorite client to visit the following URL to access this web service:

    http://localhost:2001/mariadb_demo/mariadb/
