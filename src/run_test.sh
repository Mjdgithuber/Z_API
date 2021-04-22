block=6
runs=10000000
#runs=10
counter=0

pwd
for i in {0..5}
do
	fn=$( printf 'cr%u-%u.raw' $i $((i+1)) )
	./emma data/$fn $block $runs 0
	tmp="${fn}_reg.gmon"
	mv -f gmon.out gmons/$tmp
	./emma data/$fn $block $runs 1
	tmp="${fn}_prc.gmon" 
	mv -f gmon.out gmons/$tmp
done
let "counter++"


