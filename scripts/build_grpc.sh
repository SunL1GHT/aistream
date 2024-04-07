#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
grpc_dir=$work_dir/3rdparty/grpc
echo "work_dir: $work_dir"
echo "grpc_dir: $grpc_dir"

mkdir -p $grpc_dir
if [ ! -d $grpc_dir/grpc-1.50.0 ]; then
  echo "start tar"
  tar xzf $work_dir/pkg/grpc-1.50.0.tar.gz -C $grpc_dir
fi

if [ ! -d $grpc_dir/release ]; then
  echo "start complie"
  mkdir -p $grpc_dir/release
  mkdir -p $grpc_dir/grpc/cmake/build
  cd $grpc_dir/grpc/cmake/build
  cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=$grpc_dir/release ../..
  make -j8
  make install
fi