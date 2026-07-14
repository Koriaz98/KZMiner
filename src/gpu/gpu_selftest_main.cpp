#include <iostream>
#include <cstdlib>

int runGpuSelfTest(int deviceIndex);

int main(int argc, char **argv)
{
    int device = (argc > 1) ? std::atoi(argv[1]) : 0;
    return runGpuSelfTest(device);
}
