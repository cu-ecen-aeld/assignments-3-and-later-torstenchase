#!/bin/bash
writefile=$1
writestr=$2
if [ $# -ne 2 ];
then 
    echo "ERROR: $0 requires 2 input arguments"
    exit 1
fi
filepath=$(dirname ${writefile})
mkdir -p ${filepath}
echo ${writestr} > ${writefile}
if [ $? -ne 0 ];
then
    echo "ERROR: Could not create ${writefile}"
    exit 1
fi
