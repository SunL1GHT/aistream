#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
libevent_dir=$work_dir/3rdparty/libevent
echo "work_dir: $work_dir"
echo "libevent_dir: $libevent_dir"

mkdir -p $libevent_dir
if [ ! -d $libevent_dir/libevent-2.1.12-stable ]; then
  echo "start tar libevent"
  tar xzf $work_dir/pkg/libevent-2.1.12-stable.tar.gz -C $libevent_dir
fi

if [ ! -d $libevent_dir/release ]; then
  echo "start complie libevent"
  mkdir -p $libevent_dir/release
  cd $libevent_dir/libevent-2.1.12-stable
  ./configure --prefix=$libevent_dir/release
  make -j8
  make install
fi