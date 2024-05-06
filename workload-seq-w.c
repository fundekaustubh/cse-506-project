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
	// Sequential reads and writes
	for (int i = 0; i < 100; i++)
	{
		// Write to files
		char str[256];
		int_to_string(i, str);
		int wr = open(str, O_WRONLY);
		if (wr < 0)
			printf(1, "open %s failed\n", str);
		int FILE_SIZE = 50;
		char buf[FILE_SIZE];
		for (int i = 0; i < FILE_SIZE; i++)
		{
			buf[i] = '1';
		}
		wr = write(wr, buf, FILE_SIZE);
		if (wr < 0)
			printf(1, "write %s failed\n", str);
		close(wr);
	}
	// int hits = open("hits.txt", 0x001 | 0x200);
	int h = 0;
	int m = 0;
	printf(2, "Hits: %d", h);
	printf(2, "Misses: %d", m);
	exit();
}