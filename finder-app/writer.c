/*
    Write a C application “writer” (finder-app/writer.c)  which can be used as an alternative to the “writer.sh” test script created in assignment1
    and using File IO as described in LSP chapter 2.
    See the Assignment 1 requirements for the writer.sh test script and these additional instructions:
	Accepts the following arguments:
		the first argument is a full path to a file (including filename) on the filesystem, referred to below as writefile;
		the second argument is a text string which will be written within this file, referred to below as writestr
	Exits with value 1 error and print statements if any of the arguments above were not specified
	Creates a new file with name and path writefile with content writestr, overwriting any existing file
    	Exits with value 1 and error print statement if the file could not be created.
		Example:
			writer /tmp/aesd/assignment1/sample.txt ios
		Creates file:
		    	/tmp/aesd/assignment1/sample.txt
	With content:
		ios

    One difference from the write.sh instructions in Assignment 1
	You do not need to make your "writer" utility create directories which do not exist
	You can assume the directory is created by the caller.
    Setup syslog logging for your utility using the LOG_USER facility.
    Use the syslog capability to write a message “Writing <string> to <file>”
	where <string> is the text string written to file (second argument)
	and <file> is the file created by the script.
	This should be written with LOG_DEBUG level.
    Use the syslog capability to log any unexpected errors with LOG_ERR level.
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>

/*
	retrieves path part, sample usage below
	char path[]  = "a/b/c/file";
	get_path(path);
	printf("%s\n", path);
*/

char *get_path(char *path)
{
	char n;
	char *s;

	if (!path)
		return NULL;
	n = strlen(path);
	s = path + n;
	while(s != path && *s !='/')
		s--;
	*s = '\0';
    	return path;
}


/*
  if NULL
 	return 1
  if file exists and is a reguiar file
  	return 0
  else
	error
  get path
	if path doers not exists
		error
	if path exists and is not a directory
		error
  return 0
*/
int bad_path(const char *s)
{
	struct stat statbuf;
	char path[128];

	if (!s)
		return 1;

	if (stat(s, &statbuf) == 0) {
	        if (S_ISREG(statbuf.st_mode))
			return 0;
		syslog(LOG_ERR, "'%s' not a regular file", s);
		return 1;
	}

	if (strlen(s) > (sizeof(path)-1)) {
		syslog(LOG_ERR, "'%s' too long", s);
		return 1;
	}

	strcpy(path, s);
	get_path(path);

	if (stat(path, &statbuf) == -1) {
		syslog(LOG_ERR, "'%s' path does not exists", path);
		return 1;
	}

	if (!S_ISDIR(statbuf.st_mode)) {
		syslog(LOG_ERR, "'%s' not a directory", path);
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *writefile;
	char *writestr;

	FILE *f;

	openlog(NULL, LOG_PID, LOG_USER);
 	if (argc !=3) {
	        syslog(LOG_ERR, "Required arguments not specified");
		return 1;
	}

	writefile=argv[1];
	writestr=argv[2];

	if (bad_path(writefile))
		return 1;

	if ((f = fopen(writefile, "w")) == NULL) {
		syslog(LOG_ERR, "'%s' could not be created", writefile);
		return 1;
	}
	if (fputs(writestr, f) == EOF) {
		syslog(LOG_ERR, "cannot write <%s> to <%s>", writestr, writefile);
		return 1;
	} else
		syslog(LOG_DEBUG,"Writing <%s> to <%s>",writestr, writefile);
	fclose(f);
	/* ignoring possible errors*/

	closelog();
 	return 0;
}
