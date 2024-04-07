#!/bin/bash

basepath=$(cd `dirname $0`; pwd)
work_dir=$(cd $basepath/../work; pwd)
nasm_dir=$work_dir/3rdparty/nasm
echo "work_dir: $work_dir"
echo "nasm_dir: $nasm_dir"

if dpkg -l | grep 'libssl-dev';
then
    echo "SSL packages are already installed."
else
    echo "SSL packages are not installed. Installing now..."
    apt-get install libicu-dev libssl-dev -y
fi

mkdir -p $nasm_dir
if [ ! -d $nasm_dir/nasm-2.13.03 ]; then
  echo "start tar nasm"
  tar xzf $work_dir/pkg/nasm-2.13.03.tar.gz -C $nasm_dir
fi

if whereis 'nasm';
then
  echo "nasm packages are already installed."
else
  echo "nasm packages are not installed. Installing now..."
  echo "start complie nasm"
  cd $nasm_dir/nasm-2.13.03
  ./configure
  make
  make install
fi