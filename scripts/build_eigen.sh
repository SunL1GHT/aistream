#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
eigen_dir=$work_dir/3rdparty/eigen
echo "work_dir: $work_dir"
echo "eigen_dir: $eigen_dir"

mkdir -p $eigen_dir
if [ ! -d $eigen_dir/eigen-3.3.9 ]; then
  echo "start tar eigen"
  tar xzf $work_dir/pkg/eigen-3.3.9.tar.gz -C $eigen_dir
fi

if [ ! -d $eigen_dir/release ]; then
  echo "start complie eigen"
  mkdir -p $eigen_dir/release
  mkdir -p $eigen_dir/eigen-3.3.9/build
  cd $eigen_dir/eigen-3.3.9/build
  cmake -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_INSTALL_PREFIX=${eigen_dir}/release ..
  make -j16
  make install
fi