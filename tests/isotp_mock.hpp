#ifndef __ISOTP_CPPUTEST_MOCK_HPP__
#define __ISOTP_CPPUTEST_MOCK_HPP__

#include <stdint.h>
#include "CppUTestExt/MockSupport.h"

void isotp_user_debug(const char* message)
{
  mock().actualCall("isotp_user_debug");
  printf("%s", message);

}

int  isotp_user_send_can(const uint32_t arbitration_id,
                         const uint8_t* data, const uint8_t size)
{
  mock().actualCall("isotp_user_send_can");
  return 0;
}

uint32_t isotp_user_get_us(void)
{
  static uint32_t microsecond;
  microsecond = microsecond + 5;
  
  mock().actualCall("isotp_user_get_us");   

  return microsecond;
}


#endif /* __ISOTP_CPPUTEST_MOCK_HPP__ */