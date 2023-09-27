#!/bin/bash
#set -x
#10) Write a shell script finder-app/writer.sh as described below
#
#    Accepts the following arguments:
#	the first argument is a full path to a file (including filename) on the filesystem, referred to below as writefile;
#	the second argument is a text string which will be written within this file, referred to below as writestr
#    Exits with value 1 error and print statements if any of the arguments above were not specified
#    Creates a new file with name and path writefile with content writestr,
#	overwriting any existing file
#	and creating the path if it doesn’t exist.
#    Exits with value 1 and error print statement if the file could not be created.
#
#Example:
#       writer.sh /tmp/aesd/assignment1/sample.txt ios
#
#Creates file:
#    /tmp/aesd/assignment1/sample.txt
#
#With content:
#            ios
#

if [[ $# -ne 2 ]]; then
        echo -e "arguments were not specified\n"
	exit 1
fi

writefile=$1
writestr=$2
if [[ -d "$writefile" ]]; then
	echo -e "$writefile is a directory"
	exit 1
fi
writepath=${writefile%/*}
if [[ ! -d "$writepath" ]]; then
	mkdir -p "$writepath"
	if [[ $? -ne 0 ]]; then
		echo -e "cannot create $writepath\n"
		exit 1
	fi
fi
echo "$writestr" > "$writefile"
if [[ $? -ne 0 ]]; then
        echo -e "the file could not be created\n"
	exit 1
fi
