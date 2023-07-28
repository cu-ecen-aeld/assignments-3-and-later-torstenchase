#!/bin/bash
filesdir=$1
searchstr=$2
if [ $# -ne 2 ];
then 
    echo "ERROR: $0 requires 2 input arguments"
    exit 1
fi
if [ ! -d "$filesdir" ];
then
    echo "ERROR: Could not find ${filesdir}"
    exit 1
fi
numfiles=$(ls -1 ${filesdir} | wc -l)
numlines=$(grep -r ${searchstr} ${filesdir}/* | wc -l)
echo "The number of files are ${numfiles} and the number of matching lines are ${numlines}"
