#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
opencv_dir=$work_dir/3rdparty/opencv
echo "work_dir: $work_dir"
echo "opencv_dir: $opencv_dir"

mkdir -p $opencv_dir
if [ ! -d $opencv_dir/opencv-4.5.5 ]; then
  echo "start tar"
  tar xzf $work_dir/pkg/opencv-4.5.5.tar.gz -C $opencv_dir
fi

if [ ! -d $opencv_dir/release ]; then
  echo "start complie"
  mkdir -p $opencv_dir/release
  mkdir -p $opencv_dir/opencv-4.5.5/build
  cd $opencv_dir/opencv-4.5.5/build
  cmake -DCMAKE_BUILD_TYPE=RELEASE -DWITH_FFMPEG=ON -DCMAKE_INSTALL_PREFIX=${opencv_dir}/release ..
  make -j16
  make install
fi