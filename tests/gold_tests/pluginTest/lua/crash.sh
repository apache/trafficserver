hammer ()
{
let COUNT=0
while [[ ! -f success ]]
do
    for I in $( seq 1 300 )
    do
        let COUNT=COUNT+1
        curl http://127.0.0.1:$1 2>/dev/null >/dev/null
        if [[ $? != 0 ]] ; then
            echo failed requests=$COUNT
            touch fail
            return 1
        fi
    done
done
echo requests=$COUNT
return 0
}

time_run ()
{
for I in $( seq 1 540 )
do
    sleep 1
    if [[ -f fail ]] ; then
        return 0
    fi
done
touch success
return 0
}

time_run &

PIDS=""
for I in $( seq 1 16 )
do
    hammer $1 &
    PIDS="$PIDS $!"
done

wait $PIDS
