#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

void int_to_string(int num, char *str)
{
	int i = 0;
	int is_negative = 0;
	if (num < 0)
	{
		is_negative = 1;
		num = -num;
	}
	do
	{
		str[i++] = num % 10 + '0';
		num /= 10;
	} while (num > 0);
	if (is_negative)
	{
		str[i++] = '-';
	}
	int j = 0;
	char temp;
	for (j = 0; j < i / 2; j++)
	{
		temp = str[j];
		str[j] = str[i - j - 1];
		str[i - j - 1] = temp;
	}
	str[i++] = '.';
	str[i++] = 't';
	str[i++] = 'x';
	str[i++] = 't';
	str[i] = '\0';
}

int main()
{
	int NUM_FILES = 100;
	int LAST_CHARS_TO_COPY = 5;
	int FILE_SIZE = 50;
	for (int i = 0; i < NUM_FILES; i++)
	{
		char filename[20];
		char read_buf[FILE_SIZE];
		int_to_string(i, filename);

		// Open file for reading
		int read_fd = open(filename, O_RDONLY);
		if (read_fd < 0)
		{
			printf(1, "open %s failed\n", filename);
			continue;
		}

		// Read last characters from file
		int bytes_read = read(read_fd, read_buf, LAST_CHARS_TO_COPY);
		if (bytes_read != LAST_CHARS_TO_COPY)
		{
			printf(1, "read %s failed\n", filename);
			close(read_fd);
			continue;
		}

		close(read_fd);

		// Determine destination file index
		int dest_index = (i + LAST_CHARS_TO_COPY) % NUM_FILES;

		// Open destination file for appending
		int_to_string(dest_index, filename);
		int write_fd = open(filename, O_WRONLY | O_CREATE);
		if (write_fd < 0)
		{
			printf(1, "open %s failed\n", filename);
			continue;
		}

		// Write read characters to destination file
		int bytes_written = write(write_fd, read_buf, bytes_read);
		if (bytes_written != bytes_read)
		{
			printf(1, "append %s failed\n", filename);
			close(write_fd);
			continue;
		}
		close(write_fd);
	}
	exit();
}