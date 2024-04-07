#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
mongodb_dir=$work_dir/3rdparty/mongodb
echo "work_dir: $work_dir"
echo "mongodb_dir: $mongodb_dir"

mkdir -p $mongodb_dir
if [ ! -d $mongodb_dir/mongo-c-driver-1.20.1 ]; then
  echo "start tar"
  tar xzf $work_dir/pkg/mongo-c-driver-1.20.1.tar.gz -C $mongodb_dir
fi

if [ ! -d $mongodb_dir/release ]; then
  echo "start complie"
  mkdir -p $mongodb_dir/release
  mkdir -p $mongodb_dir/mongo-c-driver-1.20.1/build
  cd $mongodb_dir/mongo-c-driver-1.20.1/build
  cmake -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_INSTALL_PREFIX=${mongodb_dir}/release ..
  make -j16
  make install
fi