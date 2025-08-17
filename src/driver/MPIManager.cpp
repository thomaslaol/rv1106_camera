#include "driver/MPIManager.hpp"

extern "C"
{
#include "rk_mpi_sys.h"
}

namespace driver
{
    int MPIManager::init()
    {
        if (RK_MPI_SYS_Init() != RK_SUCCESS)
        {
            RK_LOGE("rk mpi sys init fail!");
            return -1;
        }
        return 0;
    }
} // namespace driver
