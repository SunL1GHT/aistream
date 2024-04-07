#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
rabbitmq_dir=$work_dir/3rdparty/rabbitmq
echo "work_dir: $work_dir"
echo "rabbitmq_dir: $rabbitmq_dir"

mkdir -p $rabbitmq_dir
if [ ! -d $rabbitmq_dir/rabbitmq-c-0.11.0 ]; then
  echo "start tar"
  tar xzf $work_dir/pkg/rabbitmq-c-0.11.0.tar.gz -C $rabbitmq_dir
fi

if [ ! -d $rabbitmq_dir/release ]; then
  echo "start complie"
  mkdir -p $rabbitmq_dir/release
  mkdir -p $rabbitmq_dir/rabbitmq-c-0.11.0/build
  cd $rabbitmq_dir/rabbitmq-c-0.11.0/build
  cmake -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=${rabbitmq_dir}/release ..
  make -j8
  make install
fi