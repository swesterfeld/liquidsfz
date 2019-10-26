for f in sin.wav sinmono.wav
do
for i in $(seq -100 5 100)
do
  echo "<region>sample=$f key=60 pan=$i" > pantest.tmp
  echo $f $(testliquid pantest.tmp | awk '/left_peak/ { l=$2} /right_peak/ { r=$2; } END { print '$i',l,r }')
done
done
# plot "< grep sin.wav t" using 2:3, "< grep sinmono.wav t" using 2:3 with lines, "< grep sin.wav t" using 2:4, "< grep sinmono.wav t" using 2:4 with lines
