start=`date +%s`
$1
echo $?
#openssl s_client -ign_eof -connect 127.0.0.1:4443  > out 2>&1 
end=`date +%s`

runtime=$((end-start))
echo $runtime
if [ $runtime -lt 6 ] 
then
  echo "Accept timeout $runtime"
  exit 0
elif [ $runtime -lt 10 ]
then
  echo "Transaction inactivity timeout $runtime"
  exit 0
else 
  echo "Default inactivity timeout $runtime"
  exit 1
fi
