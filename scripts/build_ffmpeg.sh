#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
ffmpeg_dir=$work_dir/3rdparty/ffmpeg
echo "work_dir: $work_dir"
echo "ffmpeg_dir: $ffmpeg_dir"

mkdir -p $ffmpeg_dir
if [ ! -d $ffmpeg_dir/ffmpeg-3.4.9 ]; then
  echo "start tar ffmpeg"
  tar xf $work_dir/pkg/ffmpeg-3.4.9.tar.xz -C $ffmpeg_dir
fi

if [ ! -d $ffmpeg_dir/release ]; then
  echo "start complie ffmpeg"
  mkdir -p $ffmpeg_dir/release
  cd $ffmpeg_dir/ffmpeg-3.4.9
  ./configure --enable-shared --enable-gpl --enable-libx264 --extra-cflags=-I${work_dir}/3rdparty/x264/release/include --extra-ldflags=-L${work_dir}/3rdparty/x264/release/lib --prefix=${ffmpeg_dir}/release
  make -j8
  make install
fi