# Etestd
System for organizing single/multiple choice tests for students. Server component.

## Building
Dependencies:
- gcc
- make
- [json-c](https://github.com/json-c/json-c)
- [libuuid](https://github.com/karelzak/util-linux/tree/master/libuuid) from util-linux
- [libnettle](https://www.lysator.liu.se/~nisse/nettle/)

### Building on Debian
```sh
apt-get install build-essential libjson-c-dev uuid-dev nettle-dev  
make
```

### Building on Fedora
```sh
yum install gcc make json-c-devel libuuid-devel nettle-devel  
make
```

## Documentation
See etestd [wiki](https://github.com/pmartycz/etestd/wiki).
