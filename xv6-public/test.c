#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char** argv)
{
	printf(1, "My pid is %d\n", getpid());
	printf(1, "My ppid is %d\n", getppid());

	printf(1, "log num: %d\n", get_log_num());

	exit();
}
