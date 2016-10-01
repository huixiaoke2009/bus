#!/bin/bash

start()
{
    echo 'wait'
}


stop()
{
    killall bus
    killall logsvr
    killall conn
    killall auth
    killall user
    killall loader
    killall writer
}

usage()
{
    echo "$0 <start|stop|restart>"
}

if [ "$1" = "start" ];then
    start

elif [ "$1" = "stop" ];then
    stop

elif [ "$1" = "restart" ];then
    stop
    start
else
    usage
fi

