#!/bin/bash

start()
{
    echo 'startup logsvr, wait ...'
    cd logsvr;./logsvr ./conf/logsvr.ini;cd ..;
    echo 'startup bus, wait ...'
    cd bus;./bus ./conf/bus.ini;cd ..;
    echo 'startup user, wait ...'
    cd user;./user ./conf/user.ini;cd ..;
    echo 'startup loader, wait ...'
    cd user;./loader ./conf/loader.ini;cd ..;
    echo 'startup writer, wait ...'
    cd user;./writer ./conf/writer.ini;cd ..;
    echo 'startup auth, wait ...'
    cd auth;./auth ./conf/auth.ini;cd ..;
    echo 'startup conn, wait ...'
    cd conn;./conn ./conf/conn.ini;cd ..;
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

