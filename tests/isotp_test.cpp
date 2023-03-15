#include "CppUTest/CommandLineTestRunner.h"

int main(int ac, char **av)
{	
	int ret = 0;

	if( ac < 2)
	{
		const char * av_override[] = { "exe", "-v", "-c" }; //turn on verbose mode
		int ret = CommandLineTestRunner::RunAllTests(3, av_override);

	} else {

		ret = CommandLineTestRunner::RunAllTests(ac, av);
	}

	return ret;
}
