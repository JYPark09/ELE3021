#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char** argv)
{
	char* buf = "Hello xv6!";
	int ret_val;
	ret_val = myfunction(buf);
	printf(1, "Return value : 0x%x\n", ret_val);

	sleep(500);
	printf(1, "sleep1 done!\n");\
	sleep(500);
	printf(1, "sleep2 done!\n");

	exit();
}
