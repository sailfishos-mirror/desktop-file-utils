#! /bin/sh

###
### This is a shell script that runs all our tests. 
### it's invoked in turn from "make check"
###

function die() 
{
    echo $* 1>&2
    exit 1
}

RUN_TEST=`pwd`/run-test

test -x $RUN_TEST || die "$RUN_TEST does not exist or isn't executable"

TEST_DIRS=`echo test-data-*`

for D in $TEST_DIRS; do
    echo "Running tests in $D"
    for T in $D/*.results; do
        $RUN_TEST $T || die "Test $T failed"
    done
done

exit 0