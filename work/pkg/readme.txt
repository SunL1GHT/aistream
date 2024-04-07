Build:
    ./configure --add-module=/path/to/nginx-http-flv-module
    make
    make install
Run:
    /usr/local/nginx/sbin/nginx
Test:
    ffmpeg -re -i test.264 -c copy -f flv rtmp://127.0.0.1:1935/myapp/stream0
    ffplay rtmp://127.0.0.1:1935/myapp/stream0
    ffplay http://127.0.0.1:8080/live?port=1935&app=myapp&stream=stream0
Others:
    hls conf : conf/servers/hls.conf

