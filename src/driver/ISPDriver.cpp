#include "driver/ISPDriver.hpp"

int driver::ISPDriver::init()
{
    int ret = 0;
    ret = SAMPLE_COMM_ISP_Init(cam_id_, hdr_mode_, multi_sensor_, iq_dir_);
    ret |= SAMPLE_COMM_ISP_Run(cam_id_);
    return ret;
}
