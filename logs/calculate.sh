crt_total=0
rch_total=0
for((mark=1;mark<11;mark++))
do
    crt=$(cat node_${mark}.txt|grep -c "MSG-CRT")
    echo "node${mark} generate ->$crt messages"
    crt_total=$[$crt_total+$crt]
    rch=$(cat node_${mark}.txt|grep -c "RCV-RCH")
    echo "node${mark} recieve  ->$rch messages"
    rch_total=$[$rch+$rch_total]
done
echo create total message: $crt_total
echo reach destination message: $rch_total
echo average dilivery: $[$rch_total/$crt_total]

