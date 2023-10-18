#!/bin/sh
#set -x
#9) Write a shell script finder-app/finder.sh as described below:
#
#    Accepts the following runtime arguments:
#	the first argument is a path to a directory on the filesystem, referred to below as filesdir;
#	the second argument is a text string which will be searched within these files, referred to below as searchstr
#    Exits with return value 1 error and print statements if any of the parameters above were not specified
#    Exits with return value 1 error and print statements if filesdir does not represent a directory on the filesystem
#    Prints a message "The number of files are X and the number of matching lines are Y" where
#	X is the number of files in the directory and all subdirectories
#	and Y is the number of matching lines found in respective files
#		where a matching line refers to a line which contains searchstr (and may also contain additional content).
#
#Example invocation:
#
#       finder.sh /tmp/aesd/assignment1 linux
#

if [[ $# -ne 2 ]]; then
        echo -e "arguments were not specified\n"
	exit 1
fi

filesdir=$1
searchstr=$2

if [[ ! -d "$filesdir" ]]; then
        echo -e "the first parameter does not represent a directory on the filesystem\n"
	exit 1
fi

X=$(find "$filesdir" -type f -print 2> /dev/null| wc -l)
Y=$(grep -srl "$searchstr" "$filesdir"| wc -l)

echo "The number of files are $X and the number of matching lines are $Y"

exit 0

