start()
{
    echo 'wait'
}

start1()
{
    ./bus/bus ./bus/conf/bus.ini
    sleep 1
    ./logsvr/logsvr ./logsvr/conf/logsvr.ini
    sleep 1
    ./conn/conn ./conn/conf/conn.ini
}

start2()
{
    echo 'wait2'
}

stop()
{
    killall bus
    killall logsvr
    killall conn
    killall auth
    killall gns
}

usage()
{
    echo "$0 <start|stop|restart>"
}

if [ "$1" = "start1" ];then
    start1

elif [ "$1" = "start2" ];then
    start2

elif [ "$1" = "stop" ];then
    stop

elif [ "$1" = "restart" ];then
    stop
    start
else
    usage
fi

