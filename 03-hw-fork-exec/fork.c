#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
	int pid;

	printf("Starting program; process has pid %d\n", getpid());

	int fd[2];

	pipe(fd);

	FILE *file = fopen("fork-output.txt", "w");
	fprintf(file, "BEFORE FORK (%d)\n", fileno(file));
	fflush(file);

	if ((pid = fork()) < 0)
	{
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */

	printf("Section A;  pid %d\n", getpid());
	// sleep(5);

	/* END SECTION A */
	if (pid == 0)
	{
		/* BEGIN SECTION B */

		// sleep(5);
		fprintf(file, "Section B (%d)\n", fileno(file));
		fflush(file);

		printf("Section B\n");

		close(fd[0]);
		char hello[] = "hello from section B\n";
		sleep(10);
		write(fd[1], hello, sizeof(hello));
		fflush(file);
		sleep(10);
		close(fd[1]);

		// sleep(30);
		// sleep(30);
		//	printf("Section B done sleeping\n");

		char *newenviron[] = {NULL};

		printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
		// sleep(30);

		if (argc <= 1)
		{
			printf("No program to exec.  Exiting...\n");
			exit(0);
		}

		printf("Running exec of \"%s\"\n", argv[1]);
		dup2(fileno(file), 1);
		execve(argv[1], &argv[1], newenviron);
		printf("End of program \"%s\".\n", argv[0]);

		exit(0);

		/* END SECTION B */
	}
	else
	{
		/* BEGIN SECTION C */
		// wait(NULL);

		// sleep(5);
		fprintf(file, "Section C (%d)\n", fileno(file));
		fclose(file);

		printf("Section C\n");
		close(fd[1]);
		char buffer[100];
		int bytes = read(fd[0], buffer, sizeof(buffer));
		buffer[bytes] = '\0';
		printf("Number of bytes: %d\n", bytes);
		printf("String: %s", buffer);

		char buffer2[100];
		int bytes2 = read(fd[0], buffer2, sizeof(buffer2));
		buffer2[bytes2] = '\0';
		printf("Number of bytes: %d\n", bytes2);
		printf("String: %s", buffer2);

		close(fd[0]);
		//	sleep(30);
		//	printf("Section C done sleeping\n");

		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	printf("Section D\n");
	// sleep(30);

	/* END SECTION D */
}
