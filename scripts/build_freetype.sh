#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
freetype_dir=$work_dir/3rdparty/freetype
echo "work_dir: $work_dir"
echo "freetype_dir: $freetype_dir"

mkdir -p $freetype_dir
if [ ! -d $freetype_dir/freetype-2.10.0 ]; then
  echo "start tar freetype"
  tar xzf $work_dir/pkg/freetype-2.10.0.tar.gz -C $freetype_dir
fi

if [ ! -d $freetype_dir/release ]; then
  echo "start complie freetype"
  mkdir -p $freetype_dir/release
  cd $freetype_dir/freetype-2.10.0
  ./configure --prefix=${FREETYPE_DIR}/release
  make -j8
  make install
fi