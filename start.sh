#! /bin/bash

rm -r nginx
cd nginx-1.9.9
./configure --prefix=/root/nginx --add-module=/root/load-balance/ --with-debug
make
make install

