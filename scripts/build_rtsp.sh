#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
rtsp_dir=$work_dir/3rdparty/rtsp
echo "work_dir: $work_dir"
echo "rtsp_dir: $rtsp_dir"

mkdir -p $rtsp_dir
if [ ! -d $rtsp_dir/rtsp-client ]; then
  echo "start tar rtsp"
  tar xzf $work_dir/pkg/rtsp-client.tar.gz -C $rtsp_dir
fi

if [ ! -d $rtsp_dir/release ]; then
  echo "start complie rtsp"
  mkdir -p $rtsp_dir/release/inc
  mkdir -p $rtsp_dir/release/lib
  cd $rtsp_dir/rtsp-client/player
  make -j8
fi