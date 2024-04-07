#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
x264_dir=$work_dir/3rdparty/x264
echo "work_dir: $work_dir"
echo "x264_dir: $x264_dir"

mkdir -p $x264_dir
if [ ! -d $x264_dir/x264-20191217-2245 ]; then
  echo "start tar x264"
  tar xf $work_dir/pkg/x264-20191217-2245.tar.gz -C $x264_dir
fi

if [ ! -d $x264_dir/release ]; then
  echo "start complie x264"
  mkdir -p $x264_dir/release
  cd $x264_dir/x264-20191217-2245
  ./configure --enable-shared --prefix=${x264_dir}/release
  make -j8
  make install
fi