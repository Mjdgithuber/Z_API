runs=10000000
#runs=10

for block in {0..7}
do
	for i in {0..5}
	do
		fn=$( printf 'cr%u-%u.raw' $i $((i+1)) )
		time ./emma data/$fn $block $runs 0
		tmp="${block}.${fn}_reg.gmon"
		mv -f gmon.out gmons_full/$tmp
		time ./emma data/$fn $block $runs 1
		tmp="${block}.${fn}_prc.gmon" 
		mv -f gmon.out gmons_full/$tmp
	done
done

