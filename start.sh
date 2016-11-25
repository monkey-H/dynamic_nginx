#! /bin/bash

rm -r nginx
cd nginx-1.10.2
./configure --prefix=/root/nginx --add-module=/root/load-balance --with-debug
make
make install

